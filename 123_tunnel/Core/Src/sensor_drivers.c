/**
 * @file    sensor_drivers.c
 * @brief   10 类传感器底层驱动实现
 *
 * 说明：具体传感器型号未最终确定，本文件按「常见方案」实现框架：
 *   - 模拟量传感器（振动/应力/水位/位移/裂缝/荷载）走 ADC（12 位）采样；
 *   - 温湿度走 I2C（以 SHT30 为例）；
 *   - 渗水走 GPIO 数字量（低电平=有水）。
 * 外设句柄（hadc1/hi2c1/GPIO）由 CubeMX 生成，引脚见 README 配置指南。
 * 换算系数（ADC_XXX_xxx 宏）需根据实际传感器数据手册标定后修改。
 */
#include "sensor_drivers.h"
#include "main.h"                 /* CubeMX 生成：外设句柄声明 */
#include "soft_i2c.h"
#include "sht30.h"
#include "vl53l1x.h"

/* ============ 外设句柄（CubeMX 生成，需按实际命名核对） ============ */
extern ADC_HandleTypeDef  hadc1;  /* ADC1，模拟量采样 */

/* ============ ADC 通道宏（隧道板实际使用的通道） ============
 * PA0/PA1 摇杆（见 key.h）、PA2/PA3 留 USART2（ESP8266）、PA8 MCO
 * 注：原先从桥板带过来的 VIBRATION/STRESS/DISPLACEMENT/CRACK/LOAD/ILLUMINANCE
 *     六个宏在隧道板从不使用（位移走 VL53L1X、振动应力荷载在桥板），
 *     其中 DISPLACEMENT/CRACK 还占着 PA5/PA6 与新传感器冲突，故一并清除。
 */
#define ADC_CH_WATER_LEVEL_1  ADC_CHANNEL_4   /* PA4 水位1 */
#define ADC_CH_WATER_LEVEL_2  ADC_CHANNEL_5   /* PA5 水位2（本次新增） */
#define ADC_CH_DOME_PRESSURE  ADC_CHANNEL_6   /* PA6 穹顶压力-薄膜（本次新增） */
#define ADC_CH_SEEPAGE        ADC_CHANNEL_8   /* PB0 渗水1(MH-RD雨滴 #1) */
#define ADC_CH_SEEPAGE_2      ADC_CHANNEL_9   /* PB1 渗水2(MH-RD雨滴 #2) */

/* ============ 换算系数（需按实际传感器标定） ============ */
#define WATER_LEVEL_SCALE     0.001f    /* 1 LSB 对应 m      */
#define DOME_PRESSURE_SCALE   1.0f      /* 1 LSB 对应 kPa（占位，待真机按压标定）*/
#define SEEPAGE_SCALE         0.05f     /* 1 LSB 对应 %（土壤含水率，待标定）*/

/* ============ 通用 ADC 采样 ============ */
uint16_t adc_read_channel(uint32_t channel)
{
    ADC_ChannelConfTypeDef sConfig = {0};
    sConfig.Channel      = channel;
    sConfig.Rank         = ADC_REGULAR_RANK_1;
    sConfig.SamplingTime = ADC_SAMPLETIME_239CYCLES_5;
    HAL_ADC_ConfigChannel(&hadc1, &sConfig);
    HAL_ADC_Start(&hadc1);
    HAL_ADC_PollForConversion(&hadc1, 10);
    uint16_t val = HAL_ADC_GetValue(&hadc1);
    HAL_ADC_Stop(&hadc1);
    return val;
}

/* ============ 位移（VL53L1X 穹顶，XSHUT=PC8，时分复用） ============ */
void Displacement_Init(void)
{
    VL53L1X_InitAll();   /* XSHUT 三脚配置 + 全断电（读时逐个上电） */
}

int Displacement_Read(float *value)
{
    uint16_t dist_mm;
    if (VL53L1X_ReadSingle(VL53L1X_XSHUT_2, &dist_mm) != 0) {
        return -1;   /* PC8 没接/无目标，隐藏 */
    }
    *value = (float)dist_mm;
    return 0;
}

/* ============ 净空收敛（两侧壁 VL53L1X 距之和：左 PC7 + 右 PC6） ============ */
void Convergence_Init(void) {}   /* 与位移共用 VL53L1X_InitAll */

int Convergence_Read(float *value)
{
    uint16_t d1, d2;
    if (VL53L1X_ReadSingle(VL53L1X_XSHUT_1, &d1) != 0) {
        return -1;   /* 收敛左（PC7）没接/无目标 */
    }
    if (VL53L1X_ReadSingle(VL53L1X_XSHUT_0, &d2) != 0) {
        return -1;   /* 收敛右（PC6）没接/无目标 */
    }
    *value = (float)(d1 + d2);   /* 两侧壁距之和（mm），越大表示断面越收敛 */
    return 0;
}

/* ============ 水位（ADC ×2：PA4 水位1 / PA5 水位2） ============ */
void WaterLevel_Init(void)  {}
void WaterLevel2_Init(void) {}

int WaterLevel_Read(float *value)
{
    *value = adc_read_channel(ADC_CH_WATER_LEVEL_1) * WATER_LEVEL_SCALE;
    return 0;
}

int WaterLevel2_Read(float *value)
{
    *value = adc_read_channel(ADC_CH_WATER_LEVEL_2) * WATER_LEVEL_SCALE;
    return 0;
}

/* ============ 隧板无此传感器：返回 -1 跳过 ============ */
void Vibration_Init(void)   {}
void Stress_Init(void)      {}
void Load_Init(void)        {}

int Vibration_Read(float *value)
{
    (void)value;
    return -1;   /* 隧板无振动传感器（MPU-6050 在桥板） */
}

int Stress_Read(float *value)
{
    (void)value;
    return -1;   /* 预留，待补购应变片 */
}

int Load_Read(float *value)
{
    (void)value;
    return -1;   /* 压力模块在桥板 */
}

/* ============ 温湿度（SHT30 ×2，软 I2C，靠模块 ADDR 脚区分地址） ============
 * #1 ADDR 接 GND → 0x44（温度1 / 湿度1）
 * #2 ADDR 接 VCC → 0x45（温度2 / 湿度2）
 */
void Temperature_Init(void)
{
    SoftI2C_Init();
    SHT30_Init();
}

void Humidity_Init(void) {}     /* SHT30 温湿度共用一次初始化 */
void Temperature2_Init(void) {} /* 与 #1 同总线，无需重复初始化 */
void Humidity2_Init(void) {}

int Temperature_Read(float *value)
{
    float h;
    return SHT30_Read(SHT30_ADDR_1, value, &h);
}

int Humidity_Read(float *value)
{
    float t;
    return SHT30_Read(SHT30_ADDR_1, &t, value);
}

int Temperature2_Read(float *value)
{
    float h;
    return SHT30_Read(SHT30_ADDR_2, value, &h);
}

int Humidity2_Read(float *value)
{
    float t;
    return SHT30_Read(SHT30_ADDR_2, &t, value);
}

/* ============ 穹顶压力（薄膜压力传感器，PA6 ADC，输出 kPa） ============
 * 静止时读数接近 0、按压时上升；DOME_PRESSURE_SCALE 为占位值，
 * 待真机按压标定（与桥板薄膜压力传感器一样，不追求真实力学数值）。 */
void DomePressure_Init(void) {}

int DomePressure_Read(float *value)
{
    *value = adc_read_channel(ADC_CH_DOME_PRESSURE) * DOME_PRESSURE_SCALE;
    return 0;
}

/* ============ 渗水（MH-RD 雨滴传感器 ×2，ADC 模拟量，输出含水率 %） ============ */
void Seepage_Init(void) {}
void Seepage2_Init(void) {}

/* 渗水1：PB0 单路（MH-RD 雨滴，渗水越多 AO 越低，取反后数值越大） */
int Seepage_Read(float *value)
{
    float s = 4095.0f - (float)adc_read_channel(ADC_CH_SEEPAGE);
    *value = s * SEEPAGE_SCALE;
    return 0;
}

/* 渗水2：PB1 单路 */
int Seepage2_Read(float *value)
{
    float s = 4095.0f - (float)adc_read_channel(ADC_CH_SEEPAGE_2);
    *value = s * SEEPAGE_SCALE;
    return 0;
}

/* ============ K210 视觉（裂缝 / 车辆 / 人员）============
 * 三路都直接在 k210.c 里读「上一轮 K210_Poll() 缓存的识别结果」，
 * 这里不重复写一份，只做转发——保持 sensor_drivers 这层薄。
 * K210_Poll() 在主循环 2 秒上报节拍里调用（见 main.c）。
 * 车辆/人员当前 K210 端未实现，Read 恒返回 -1（预留）。 */
