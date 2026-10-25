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
#include "mpu6050.h"
#include "ds18b20.h"
#include "ultrasonic.h"
#include <math.h>                 /* sqrtf（桥面振动、索力幅值合成） */

/* ============ 外设句柄（CubeMX 生成，需按实际命名核对） ============ */
extern ADC_HandleTypeDef  hadc1;  /* ADC1，模拟量采样 */

/* ============ ADC 通道宏（按 CubeMX 配置的通道序号修改） ============
 * 注意：PA2/PA3 留给 USART2（4G 模块），ADC 通道用 PA0/1/4/5/6/7 + PB0/1
 */
#define ADC_CH_VIBRATION      ADC_CHANNEL_0   /* PA0  */
#define ADC_CH_STRESS         ADC_CHANNEL_1   /* PA1  */
#define ADC_CH_WATER_LEVEL    ADC_CHANNEL_4   /* PA4  */
#define ADC_CH_DISPLACEMENT   ADC_CHANNEL_5   /* PA5  */
#define ADC_CH_CRACK          ADC_CHANNEL_6   /* PA6  */
#define ADC_CH_LOAD           ADC_CHANNEL_7   /* PA7  */
#define ADC_CH_SEEPAGE        ADC_CHANNEL_8   /* PB0 土壤湿度 */
#define ADC_CH_ILLUMINANCE    ADC_CHANNEL_9   /* PB1 5506 照度 */

/* ============ 换算系数（需按实际传感器标定） ============ */
#define VIBRATION_SCALE       0.0001f   /* 1 LSB 对应 g      */
#define STRESS_SCALE          0.1f      /* 1 LSB 对应 με     */
#define WATER_LEVEL_SCALE     0.001f    /* 1 LSB 对应 m      */
#define DISPLACEMENT_SCALE    0.01f     /* 1 LSB 对应 mm     */
#define CRACK_SCALE           0.001f    /* 1 LSB 对应 mm     */
#define LOAD_SCALE            0.02f     /* 1 LSB 对应 t      */
#define SEEPAGE_SCALE         0.05f     /* 1 LSB 对应 %（土壤含水率，待标定）*/
#define ILLUMINANCE_SCALE     0.2f      /* 1 LSB 对应 lx（5506，待标定）*/

/* ============ 索力测量参数（振动幅值法） ============
 * ⚠️ 2026-09-25 由「频率法 T = 4·m·L²·f₁²」改为「窗口内取最大振动幅值」。
 * 原方法是测基频 f₁ 再算张力，但实物是 15cm 橡皮筋、振动幅度达不到
 * CABLE_PEAK_RATIO 显著性门槛，FFT 从未判定出主频 → 数值始终是初始 0.00。
 *
 * 现方法（与桥面振动同源，只是取窗口峰值）：
 *   窗口内采样 N 点 → 逐点算三轴合成加速度 sqrt(ax²+ay²+az²) - 1g
 *   → 取窗口内最大值作本次读数（拨动是瞬态，取峰值才不会错过）
 *
 * 单位仍是 kN（前后端标签/阈值/文档都不动），靠 CABLE_DEMO_SCALE 把
 * g 值映射到演示量级。这是**演示放大**，不是真实索力。
 */
#define CABLE_DEMO_SCALE      500.0f    /* 1g 振动 → 500 kN（演示用，可调） */

/* 窗口点数：窗口时长 = CABLE_WIN_SIZE × SAMPLE_INTERVAL_MS。
 * 取 128 → 1.28 秒，短于 2 秒上报周期，能整个塞进两次 4G 阻塞之间；
 * 若用 256（2.56 秒）必然跨过一次上报阻塞，中间挖出的空洞会漏掉拨动瞬间。 */
#define CABLE_WIN_SIZE        128       /* 窗口采样点数 */
#define SAMPLE_RATE_HZ        100       /* 采样率（Hz） */
#define SAMPLE_INTERVAL_MS    10        /* 采样间隔（=1000/SAMPLE_RATE_HZ） */

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

/* ============ 索力测量状态机（非阻塞采样，MPU-6050 #1 = 0x68） ============ */
typedef enum { CABLE_IDLE, CABLE_SAMPLING } CableState;

static CableState s_cable_state = CABLE_IDLE;
static int      s_cable_idx = 0;
static uint32_t s_cable_next = 0;
/* 三轴原始数据：索的振动方向随安装姿态而变，固定读某一轴会因方向不对测不出 */
static float    s_cable_buf[3][CABLE_WIN_SIZE];
static float    s_cable_force = 0.0f;   /* 最近一次索力（kN），无振动时为 0 */
static int      s_cable_valid = 0;      /* 1=模块在线且已完成一轮采样；0=模块不在/尚未采满 */

/* 采满一个窗口后算索力：逐点算三轴合成加速度幅值，取窗口内**最大值**。
 *
 * 为什么取最大值而不是像桥面振动那样读瞬时值：
 *   橡皮筋是「拨一下才动」的瞬态过程，而读数每 2 秒才上报一次，
 *   瞬时采样极易刚好抓在静止的那一刻读到 0。窗口取峰值必定能抓到拨动。
 *
 * 为什么三轴合成而不是选单轴：
 *   合成幅值 sqrt(ax²+ay²+az²) 与安装姿态无关，免去「选哪根轴」的判断。 */
static void cable_compute(void)
{
    float peak = 0.0f;

    for (int i = 0; i < CABLE_WIN_SIZE; i++) {
        float ax = s_cable_buf[0][i];
        float ay = s_cable_buf[1][i];
        float az = s_cable_buf[2][i];
        /* 与 Vibration_Read 同一套算法：合成幅值减去 1g 重力，静止≈0 */
        float g = sqrtf(ax * ax + ay * ay + az * az) - 1.0f;
        if (g > peak) { peak = g; }
    }

    /* 能采满一个窗口就证明模块在线，本轮采样有效 */
    s_cable_valid = 1;

    if (peak < 0.0f) { peak = 0.0f; }

    /* 单位保持 kN（前后端标签/阈值/文档都不动），靠 CABLE_DEMO_SCALE 映射到演示量级。
     * ⚠️ 这是演示放大，不是真实索力——真实索力要用频率法 T = 4·m·L²·f₁²。 */
    s_cable_force = peak * CABLE_DEMO_SCALE;
}

static void cable_start(void)
{
    s_cable_state = CABLE_SAMPLING;
    s_cable_idx = 0;
    s_cable_next = HAL_GetTick() + SAMPLE_INTERVAL_MS;
}

/* 主循环每轮调用，推进索力采样（非阻塞），采满一个窗口后自动重采 */
void Cable_Tick(void)
{
    if (s_cable_state != CABLE_SAMPLING) { return; }
    if (HAL_GetTick() < s_cable_next) { return; }

    float ax, ay, az;
    if (MPU6050_ReadAccel(MPU6050_ADDR_0, &ax, &ay, &az) != 0) {
        /* 读失败（模块未接/掉线）：标为无数据，重置本次采样、下个周期重试。
         * 置 valid=0 是关键——否则采不满一个窗口就永不重算，旧值会一直冻在屏上 */
        s_cable_valid = 0;
        s_cable_idx = 0;
        s_cable_next = HAL_GetTick() + SAMPLE_INTERVAL_MS;
        return;
    }
    s_cable_buf[0][s_cable_idx] = ax;
    s_cable_buf[1][s_cable_idx] = ay;
    s_cable_buf[2][s_cable_idx] = az;
    s_cable_idx++;
    s_cable_next = HAL_GetTick() + SAMPLE_INTERVAL_MS;

    if (s_cable_idx >= CABLE_WIN_SIZE) {
        cable_compute();
        cable_start();
    }
}

/* ============ 振动（桥面震动，MPU-6050 #2 = 0x69） ============ */
void Vibration_Init(void)
{
    SoftI2C_Init();
    MPU6050_Init(MPU6050_ADDR_1);
}

/* ============ 索力（MPU-6050 #1 = 0x68） ============ */
void CableForce_Init(void)
{
    MPU6050_Init(MPU6050_ADDR_0);
    cable_start();
}

/* ============ 其余模拟量传感器（ADC） ============ */
void Stress_Init(void)      {}
void WaterLevel_Init(void)  {}
void Displacement_Init(void){}
void Load_Init(void)        {}

int Vibration_Read(float *value)
{
    float ax, ay, az;
    if (MPU6050_ReadAccel(MPU6050_ADDR_1, &ax, &ay, &az) != 0) {
        return -1;
    }
    /* 桥面振动：三轴合成加速度幅值（减去 1g 重力，静止≈0，振动>0） */
    *value = sqrtf(ax * ax + ay * ay + az * az) - 1.0f;
    if (*value < 0.0f) { *value = 0.0f; }   /* 静止时噪声钳到 0 */
    return 0;
}

int CableForce_Read(float *value)
{
    if (!s_cable_valid) { return -1; }   /* 模块未接/掉线，无数据（同其他传感器） */
    *value = s_cable_force;              /* 每轮窗口采完即刷新（静止≈0，拨动时跳动） */
    return 0;
}

int Stress_Read(float *value)
{
    *value = adc_read_channel(ADC_CH_STRESS) * STRESS_SCALE;
    return 0;
}

int WaterLevel_Read(float *value)
{
    (void)value;
    return -1;   /* 桥板不测水位（WaterSensor 在隧道板） */
}

int Displacement_Read(float *value)
{
    (void)value;
    return -1;   /* 桥板不测位移（VL53L1X 在隧道板） */
}

int Load_Read(float *value)
{
    *value = adc_read_channel(ADC_CH_LOAD) * LOAD_SCALE;
    return 0;
}

/* ============ 温湿度（SHT30，I2C） ============
 * 温湿度是同一次测量的两个值，但 Sensor_ReadAll 会分别调用本模块的两个读函数。
 * 若各读一次，每周期要白等 2 × HAL_Delay(20ms)；且温湿度是缓变量，无需 2 秒一测。
 * 故一次测量缓存两个值、按 SHT30_CACHE_MS 降频，两个读函数都走缓存。 */
#define SHT30_CACHE_MS   5000U

void Temperature_Init(void)
{
    SoftI2C_Init();
    SHT30_Init();
}

void Humidity_Init(void) {}   /* SHT30 温湿度共用一次初始化 */

static float    s_sht_temp = 0.0f;
static float    s_sht_humi = 0.0f;
static uint32_t s_sht_next = 0;   /* 下次允许测量的时刻 */
static int      s_sht_ok   = 0;   /* 1=已成功测过一次 */

/* 缓存过期才真正测量（唯一会阻塞 20ms 的地方）；失败则下次调用立即重试 */
static void sht30_update(void)
{
    uint32_t now = HAL_GetTick();
    if (s_sht_ok && (int32_t)(now - s_sht_next) < 0) {
        return;                   /* 缓存有效 */
    }
    float t, h;
    if (SHT30_Read(&t, &h) == 0) {
        s_sht_temp = t;
        s_sht_humi = h;
        s_sht_ok = 1;
    }
    s_sht_next = now + SHT30_CACHE_MS;
}

int Temperature_Read(float *value)
{
    sht30_update();
    if (!s_sht_ok) { return -1; }   /* 传感器未接，无数据 */
    *value = s_sht_temp;
    return 0;
}

int Humidity_Read(float *value)
{
    sht30_update();
    if (!s_sht_ok) { return -1; }
    *value = s_sht_humi;
    return 0;
}

/* ============ 渗水（土壤湿度传感器，ADC 模拟量，输出含水率 %） ============ */
void Seepage_Init(void) {}

int Seepage_Read(float *value)
{
    (void)value;
    return -1;   /* 桥板不测渗水（土壤湿度在隧道板） */
}

/* ============ 桥面/桥墩固体温度（DS18B20，单总线） ============ */
void DeckTemp_Init(void)
{
    DS18B20_Init();
}

void PierTemp_Init(void) {}   /* 与桥面共用 DS18B20 初始化 */
void DeckTemp2_Init(void) {}  /* 与桥面共用 DS18B20 初始化 */
void PierTemp2_Init(void) {}  /* 与桥墩共用 DS18B20 初始化 */

/* 桥面温度1（DS18B20#0） */
int DeckTemp_Read(float *value)
{
    if (DS18B20_GetTemp(0, value) != 0) {
        return -1;
    }
    return 0;
}

/* 桥面温度2（DS18B20#1） */
int DeckTemp2_Read(float *value)
{
    if (DS18B20_GetTemp(1, value) != 0) {
        return -1;
    }
    return 0;
}

/* 桥墩温度1（DS18B20#2） */
int PierTemp_Read(float *value)
{
    if (DS18B20_GetTemp(2, value) != 0) {
        return -1;
    }
    return 0;
}

/* 桥墩温度2（DS18B20#3） */
int PierTemp2_Read(float *value)
{
    if (DS18B20_GetTemp(3, value) != 0) {
        return -1;
    }
    return 0;
}

/* 物体位置（RCWL-1670 超声波，Trig=PB15 / Echo=PB13）
 * 非阻塞：Tick 由主循环推进，这里只取缓存值（同 DS18B20 的做法）。
 * 未接模块时 Ultrasonic_Read 返回 -1，实时数据页那一行显示空白。 */
void Position_Init(void)
{
    Ultrasonic_Init();
}

int Position_Read(float *value)
{
    if (Ultrasonic_Read(value) != 0) {
        return -1;
    }
    return 0;
}
