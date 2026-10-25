/**
 * @file    vl53l1x.c
 * @brief   VL53L1X 激光测距驱动（XSHUT 时分复用，多颗同地址 0x29）
 *
 * 多颗 VL53L1X 共用一个 I2C 地址 0x29，靠 XSHUT 引脚错时工作：
 *   读哪颗 → 其余 XSHUT 全拉低断电 → 只给目标那颗上电(boot 成默认 0x29)
 *   → 完整初始化 → 单次测距 → 断电。无需软件改地址，天然支持任意颗数。
 *
 * 寄存器流程依据 ST 官方(参考 Pololu 寄存器级实现)：
 *   boot 状态 0x00E5→软复位→读晶振 0x0006→standard_ranging preset→
 *   长距(VCSEL 0x0F/0x0D)→50ms 预算→单次触发 0x0087=0x10→
 *   等 0x0031 中断→突发读 0x0089 起 17 字节→距离在偏移 13/14(=0x0096)
 *   ×2011/2048。
 */
#include "vl53l1x.h"
#include "soft_i2c.h"
#include "main.h"

/* ================= 寄存器地址 ================= */
#define R_SOFT_RESET                    0x0000
#define R_OSC_FAST_OSC_FREQ             0x0006
#define R_MM_OUTER_OFFSET_MM            0x0022
#define R_DSS_TARGET_TOTAL_RATE_MCPS    0x0024
#define R_GPIO_TIO_HV_STATUS            0x0031
#define R_SIGMA_PULSE_WIDTH_NS          0x0036
#define R_SIGMA_AMBIENT_WIDTH_NS        0x0037
#define R_ALGO_CROSSTALK_VALID_H_MM     0x0039
#define R_ALGO_RANGE_IGNORE_VALID_H_MM  0x003E
#define R_ALGO_RANGE_MIN_CLIP           0x003F
#define R_ALGO_CONSISTENCY_TOLERANCE    0x0040
#define R_PHASECAL_TIMEOUT_MACROP       0x004B
#define R_DSS_ROI_MODE_CONTROL          0x004F
#define R_SYSTEM_THRESH_RATE_HIGH       0x0050
#define R_SYSTEM_THRESH_RATE_LOW        0x0052
#define R_DSS_MANUAL_SPADS_SELECT       0x0054
#define R_DSS_APERTURE_ATTENUATION      0x0057
#define R_MM_TIMEOUT_MACROP_A           0x005A
#define R_MM_TIMEOUT_MACROP_B           0x005C
#define R_RANGE_TIMEOUT_MACROP_A        0x005E
#define R_RANGE_VCSEL_PERIOD_A          0x0060
#define R_RANGE_TIMEOUT_MACROP_B        0x0061
#define R_RANGE_VCSEL_PERIOD_B          0x0063
#define R_RANGE_SIGMA_THRESH            0x0064
#define R_RANGE_MIN_COUNT_RATE_LIMIT    0x0066
#define R_RANGE_VALID_PHASE_HIGH        0x0069
#define R_SYSTEM_GROUPED_HOLD_0         0x0071
#define R_SYSTEM_SEED_CONFIG            0x0077
#define R_SD_WOI_SD0                    0x0078
#define R_SD_WOI_SD1                    0x0079
#define R_SD_INITIAL_PHASE_SD0          0x007A
#define R_SD_INITIAL_PHASE_SD1          0x007B
#define R_SYSTEM_GROUPED_HOLD_1         0x007C
#define R_SD_QUANTIFIER                 0x007E
#define R_SYSTEM_SEQUENCE_CONFIG        0x0081
#define R_SYSTEM_GROUPED_HOLD           0x0082
#define R_SYSTEM_INTERRUPT_CLEAR        0x0086
#define R_SYSTEM_MODE_START             0x0087
#define R_RESULT_RANGE_STATUS           0x0089
#define R_RESULT_OSC_CALIBRATE_VAL      0x00DE
#define R_FIRMWARE_SYSTEM_STATUS        0x00E5

#define RESULT_RANGE_OFFSET_MM          13       /* 17 字节结果区偏移（=寄存器 0x0096） */
#define DSS_TARGET_RATE                 0x0A00
#define MEASUREMENT_TIMING_BUDGET_US    50000UL
#define TIMING_GUARD_US                 4528UL

#define ALL_XSHUT_PINS (VL53L1X_XSHUT_0 | VL53L1X_XSHUT_1 | VL53L1X_XSHUT_2)

/* ================= 底层 I2C 读写（16 位寄存器，变长突发） ================= */
static int reg_wr8(uint8_t addr, uint16_t reg, uint8_t v)
{
    uint8_t d = v;
    return SoftI2C_WriteReg16(addr, reg, &d, 1);
}
static int reg_wr16(uint8_t addr, uint16_t reg, uint16_t v)
{
    uint8_t d[2] = {(uint8_t)(v >> 8), (uint8_t)(v & 0xFF)};
    return SoftI2C_WriteReg16(addr, reg, d, 2);
}
static int reg_rd8(uint8_t addr, uint16_t reg, uint8_t *v)
{
    return SoftI2C_ReadReg16(addr, reg, v, 1);
}
static int reg_rd16(uint8_t addr, uint16_t reg, uint16_t *v)
{
    uint8_t d[2];
    if (SoftI2C_ReadReg16(addr, reg, d, 2) != 0) {
        return -1;
    }
    *v = (uint16_t)((d[0] << 8) | d[1]);
    return 0;
}

/* ================= 超时换算 ================= */
static uint32_t g_fast_osc_freq = 0;

static uint32_t calc_macro_period(uint8_t vcsel_period)
{
    uint32_t pll = ((uint32_t)0x01u << 30) / g_fast_osc_freq;
    uint8_t pclks = (uint8_t)((vcsel_period + 1) << 1);
    uint32_t mp = ((uint32_t)2304u * pll) >> 6;
    mp *= pclks;
    mp >>= 6;
    return mp;
}
static uint32_t us_to_mclks(uint32_t us, uint32_t mp)
{
    return (((uint32_t)us << 12) + (mp >> 1)) / mp;
}
static uint16_t encode_timeout(uint32_t mclks)
{
    uint32_t ls;
    uint16_t ms = 0;
    if (mclks == 0) {
        return 0;
    }
    ls = mclks - 1;
    while ((ls & 0xFFFFFF00u) > 0) {
        ls >>= 1;
        ms++;
    }
    return (uint16_t)((ms << 8) | (ls & 0xFF));
}

static int apply_timing_budget(uint8_t addr)
{
    uint32_t budget = MEASUREMENT_TIMING_BUDGET_US - TIMING_GUARD_US;
    uint32_t rt = budget / 2;
    uint8_t pa, pb;
    if (reg_rd8(addr, R_RANGE_VCSEL_PERIOD_A, &pa) != 0 ||
        reg_rd8(addr, R_RANGE_VCSEL_PERIOD_B, &pb) != 0) {
        return -1;
    }
    uint32_t mp = calc_macro_period(pa);
    uint32_t m = us_to_mclks(1000, mp);
    if (m > 0xFF) m = 0xFF;
    if (reg_wr8(addr, R_PHASECAL_TIMEOUT_MACROP, (uint8_t)m) != 0) return -1;
    if (reg_wr16(addr, R_MM_TIMEOUT_MACROP_A, encode_timeout(us_to_mclks(1, mp))) != 0) return -1;
    if (reg_wr16(addr, R_RANGE_TIMEOUT_MACROP_A, encode_timeout(us_to_mclks(rt, mp))) != 0) return -1;
    mp = calc_macro_period(pb);
    if (reg_wr16(addr, R_MM_TIMEOUT_MACROP_B, encode_timeout(us_to_mclks(1, mp))) != 0) return -1;
    if (reg_wr16(addr, R_RANGE_TIMEOUT_MACROP_B, encode_timeout(us_to_mclks(rt, mp))) != 0) return -1;
    return 0;
}

/* ================= 单颗初始化：boot + preset + 长距 50ms ================= */
static int vl53_init_one(uint8_t addr)
{
    /* 软复位（上电已 boot，复位更干净） */
    if (reg_wr8(addr, R_SOFT_RESET, 0x00) != 0 ||
        reg_wr8(addr, R_SOFT_RESET, 0x01) != 0) {
        return -1;
    }
    HAL_Delay(5);

    /* 等固件 boot：0x00E5 bit0；无应答(NACK)连续 10 次即判无设备，避免空等 */
    uint8_t st = 0;
    uint32_t t0 = HAL_GetTick();
    uint32_t nack = 0;
    do {
        if (reg_rd8(addr, R_FIRMWARE_SYSTEM_STATUS, &st) != 0) {
            st = 0;
            if (++nack > 10) return -1;
        }
    } while ((st & 0x01) == 0 && (HAL_GetTick() - t0) < 150U);
    if ((st & 0x01) == 0) {
        return -1;
    }

    uint16_t osc = 0;
    if (reg_rd16(addr, R_OSC_FAST_OSC_FREQ, &osc) != 0) return -1;
    g_fast_osc_freq = osc;

    /* standard_ranging 预设 + 长距模式 */
    reg_wr16(addr, R_DSS_TARGET_TOTAL_RATE_MCPS, DSS_TARGET_RATE);
    reg_wr8(addr, R_GPIO_TIO_HV_STATUS, 0x02);
    reg_wr8(addr, R_SIGMA_PULSE_WIDTH_NS, 8);
    reg_wr8(addr, R_SIGMA_AMBIENT_WIDTH_NS, 16);
    reg_wr8(addr, R_ALGO_CROSSTALK_VALID_H_MM, 0x01);
    reg_wr8(addr, R_ALGO_RANGE_IGNORE_VALID_H_MM, 0xFF);
    reg_wr8(addr, R_ALGO_RANGE_MIN_CLIP, 0x00);
    reg_wr8(addr, R_ALGO_CONSISTENCY_TOLERANCE, 2);
    reg_wr16(addr, R_SYSTEM_THRESH_RATE_HIGH, 0x0000);
    reg_wr16(addr, R_SYSTEM_THRESH_RATE_LOW, 0x0000);
    reg_wr8(addr, R_DSS_APERTURE_ATTENUATION, 0x38);
    reg_wr16(addr, R_RANGE_SIGMA_THRESH, 360);
    reg_wr16(addr, R_RANGE_MIN_COUNT_RATE_LIMIT, 192);
    reg_wr8(addr, R_SYSTEM_GROUPED_HOLD_0, 0x01);
    reg_wr8(addr, R_SYSTEM_GROUPED_HOLD_1, 0x01);
    reg_wr8(addr, R_SD_QUANTIFIER, 2);
    reg_wr8(addr, R_SYSTEM_GROUPED_HOLD, 0x00);
    reg_wr8(addr, R_SYSTEM_SEED_CONFIG, 1);
    reg_wr8(addr, R_SYSTEM_SEQUENCE_CONFIG, 0x8B);
    reg_wr16(addr, R_DSS_MANUAL_SPADS_SELECT, (uint16_t)(200u << 8));
    reg_wr8(addr, R_DSS_ROI_MODE_CONTROL, 2);
    reg_wr8(addr, R_RANGE_VCSEL_PERIOD_A, 0x0F);
    reg_wr8(addr, R_RANGE_VCSEL_PERIOD_B, 0x0D);
    reg_wr8(addr, R_RANGE_VALID_PHASE_HIGH, 0xB8);
    reg_wr8(addr, R_SD_WOI_SD0, 0x0F);
    reg_wr8(addr, R_SD_WOI_SD1, 0x0D);
    reg_wr8(addr, R_SD_INITIAL_PHASE_SD0, 14);
    reg_wr8(addr, R_SD_INITIAL_PHASE_SD1, 14);
    if (apply_timing_budget(addr) != 0) return -1;

    uint16_t outer = 0;
    if (reg_rd16(addr, R_MM_OUTER_OFFSET_MM, &outer) == 0) {
        reg_wr16(addr, 0x001E, (uint16_t)(outer * 4));
    }
    return 0;
}

/* ================= 单颗上电测距（时分复用核心） ================= */
static int measure_once(uint8_t addr, uint16_t *dist_mm)
{
    if (reg_wr8(addr, R_SYSTEM_INTERRUPT_CLEAR, 0x01) != 0) return -1;
    if (reg_wr8(addr, R_SYSTEM_MODE_START, 0x10) != 0) return -1;   /* 单次 */

    uint32_t t0 = HAL_GetTick();
    uint8_t hv = 0;
    do {
        if (reg_rd8(addr, R_GPIO_TIO_HV_STATUS, &hv) != 0) return -1;
        if ((hv & 0x01) == 0) break;
        HAL_Delay(1);
    } while (HAL_GetTick() - t0 < 200U);
    if ((hv & 0x01) != 0) return -1;

    uint8_t buf[17];
    if (SoftI2C_ReadReg16(addr, R_RESULT_RANGE_STATUS, buf, sizeof(buf)) != 0) return -1;
    reg_wr8(addr, R_SYSTEM_INTERRUPT_CLEAR, 0x01);

    uint16_t range = (uint16_t)((buf[RESULT_RANGE_OFFSET_MM] << 8) |
                                 buf[RESULT_RANGE_OFFSET_MM + 1]);
    if (range == 0) return -1;   /* 无有效目标 */
    *dist_mm = (uint16_t)(((uint32_t)range * 2011u + 0x400u) / 0x800u);
    return 0;
}

/* ================= 对外接口 ================= */

/* 一次性初始化：XSHUT 三脚配置 + 全部断电（启动时调用一次） */
void VL53L1X_InitAll(void)
{
    __HAL_RCC_GPIOC_CLK_ENABLE();
    GPIO_InitTypeDef gpio = {0};
    gpio.Mode  = GPIO_MODE_OUTPUT_PP;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    gpio.Pin   = ALL_XSHUT_PINS;
    HAL_GPIO_Init(VL53L1X_XSHUT_PORT, &gpio);
    HAL_GPIO_WritePin(VL53L1X_XSHUT_PORT, ALL_XSHUT_PINS, GPIO_PIN_RESET);
    SoftI2C_Init();
}

/**
 * @brief 读取指定 XSHUT 引脚上的那颗 VL53L1X（三颗同地址 0x29 时分复用）
 * @param xshut_pin VL53L1X_XSHUT_0/1/2
 * @return 0 成功；非 0 失败（含该引脚没接设备，调用方可隐藏）
 */
int VL53L1X_ReadSingle(uint16_t xshut_pin, uint16_t *dist_mm)
{
    /* 全部断电，只给目标颗上电，确保它独占 0x29 */
    HAL_GPIO_WritePin(VL53L1X_XSHUT_PORT, ALL_XSHUT_PINS, GPIO_PIN_RESET);
    HAL_Delay(1);
    HAL_GPIO_WritePin(VL53L1X_XSHUT_PORT, xshut_pin, GPIO_PIN_SET);
    HAL_Delay(10);   /* 固件 boot */

    if (vl53_init_one(VL53L1X_ADDR_0) != 0) {
        HAL_GPIO_WritePin(VL53L1X_XSHUT_PORT, xshut_pin, GPIO_PIN_RESET);
        return -1;
    }

    int rc = measure_once(VL53L1X_ADDR_0, dist_mm);

    /* 读完断电，下颗读取时再独占 0x29 */
    HAL_GPIO_WritePin(VL53L1X_XSHUT_PORT, xshut_pin, GPIO_PIN_RESET);
    return rc;
}

/* 兼容旧接口：单颗（直接已上电）测距 */
int VL53L1X_ReadDist(uint8_t addr, uint16_t *dist_mm)
{
    return measure_once(addr, dist_mm);
}

int VL53L1X_ReadReg16(uint8_t addr, uint16_t reg, uint16_t *val)
{
    return reg_rd16(addr, reg, val);
}

void VL53L1X_Init(uint8_t addr)
{
    vl53_init_one(addr);
}
