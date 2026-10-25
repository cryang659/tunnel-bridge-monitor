/**
 * @file    edge_alarm.c
 * @brief   边缘阈值告警实现
 */
#include "edge_alarm.h"
#include "stm32f1xx_hal.h"   /* HAL_FLASH 阈值持久化 */
#include <string.h>           /* memcpy */

/* 告警阈值（默认值，与服务器 config.py 的 THRESHOLDS 保持一致；可被板上菜单改写） */
static float THRESHOLDS[SENSOR_TYPE_COUNT] = {
    0.10f,    /* vibration    振动加速度  g     */
    300.0f,   /* stress       应力/应变   με    */
    50.0f,    /* temperature  温度        ℃     */
    90.0f,    /* humidity     湿度        %     */
    0.8f,     /* water_level  水位        m     */
    80.0f,    /* seepage_1    渗水1(PB0)  %     */
    10.0f,    /* displacement 位移        mm    */
    40.0f,    /* load         车辆荷载    t     */
    50.0f,    /* convergence  净空收敛    mm    */
    80.0f,    /* seepage_2    渗水2(PB1)  %     */
    50.0f,    /* temperature_2 温度2(SHT30#2) ℃ */
    90.0f,    /* humidity_2    湿度2(SHT30#2) % */
    0.8f,     /* water_level_2 水位2(PA5) m     */
    50.0f,    /* dome_pressure 穹顶压力(PA6) kPa */
    0.70f,    /* crack    裂缝置信度 0~1（K210 视觉，≥0.7 判为有裂缝） */
    10.0f,    /* vehicle  车辆计数 辆（预留，K210 端未实现） */
    5.0f,     /* person   人员计数 人（预留，K210 端未实现） */
};

/* 阈值持久化：存内部 Flash 末页（页 127，2KB，程序仅 ~30KB 不会冲突） */
#define TH_FLASH_ADDR   0x0803F800UL
#define TH_FLASH_MAGIC  0xA5A5C3C3UL

AlarmLevel EdgeAlarm_CheckOne(const SensorData *d)
{
    float th = THRESHOLDS[d->type];
    if (d->value >= th) {
        return ALARM_LEVEL_ALARM;
    }
    if (d->value >= th * 0.8f) {   /* 达到阈值 80% 进入预警 */
        return ALARM_LEVEL_WARNING;
    }
    return ALARM_LEVEL_NORMAL;
}

float EdgeAlarm_GetThreshold(SensorType type)
{
    if (type >= SENSOR_TYPE_COUNT) {
        return 0.0f;
    }
    return THRESHOLDS[type];
}

void EdgeAlarm_SetThreshold(SensorType type, float value)
{
    if (type < SENSOR_TYPE_COUNT) {
        THRESHOLDS[type] = value;
    }
}

/* 从 Flash 末页加载阈值：有有效魔数则覆盖默认值 */
void EdgeAlarm_LoadFlash(void)
{
    const uint32_t *p = (const uint32_t *)TH_FLASH_ADDR;
    if (p[0] != TH_FLASH_MAGIC || p[1] != SENSOR_TYPE_COUNT) {
        return;   /* 无有效数据，用默认值 */
    }
    const uint32_t *vals = &p[2];
    for (int i = 0; i < SENSOR_TYPE_COUNT; i++) {
        float v;
        memcpy(&v, &vals[i], sizeof(v));
        if (v > 0.0f && v < 100000.0f) {   /* 合理范围校验，防坏数据 */
            THRESHOLDS[i] = v;
        }
    }
}

/* 把当前阈值写入 Flash 末页（擦页 + 写魔数/数量/float 数组） */
void EdgeAlarm_SaveFlash(void)
{
    HAL_FLASH_Unlock();

    FLASH_EraseInitTypeDef erase = {0};
    erase.TypeErase   = FLASH_TYPEERASE_PAGES;
    erase.PageAddress = TH_FLASH_ADDR;
    erase.NbPages     = 1;
    uint32_t page_err = 0;
    if (HAL_FLASHEx_Erase(&erase, &page_err) == HAL_OK) {
        uint32_t addr = TH_FLASH_ADDR;
        HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, addr, TH_FLASH_MAGIC);
        addr += 4;
        HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, addr, SENSOR_TYPE_COUNT);
        addr += 4;
        for (int i = 0; i < SENSOR_TYPE_COUNT; i++) {
            uint32_t word;
            memcpy(&word, &THRESHOLDS[i], sizeof(word));
            HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, addr, word);
            addr += 4;
        }
    }

    HAL_FLASH_Lock();
}
