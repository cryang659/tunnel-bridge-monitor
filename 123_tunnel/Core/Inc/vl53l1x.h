/**
 * @file    vl53l1x.h
 * @brief   VL53L1X 激光测距（ToF）简化驱动接口（I2C）
 *
 * 简化驱动：仅软复位 + 电源配置 + 连续测距 + 读距离，未做完整校准，
 * 精度/稳定性需真板调试校准。
 */
#ifndef __VL53L1X_H
#define __VL53L1X_H

#include <stdint.h>
#include "stm32f1xx_hal.h"

/* VL53L1X I2C 地址（默认 0x29，XSHUT 重映射可改 0x30/0x31） */
#define VL53L1X_ADDR_0   0x29
#define VL53L1X_ADDR_1   0x30
#define VL53L1X_ADDR_2   0x31

/* XSHUT 引脚（时分复用：读谁先给谁上电，三颗同地址 0x29）：
 *   VL53L1X_XSHUT_0 = PC6 = 收敛右
 *   VL53L1X_XSHUT_1 = PC7 = 收敛左
 *   VL53L1X_XSHUT_2 = PC8 = 穹顶（位移） */
#define VL53L1X_XSHUT_PORT   GPIOC
#define VL53L1X_XSHUT_0      GPIO_PIN_6
#define VL53L1X_XSHUT_1      GPIO_PIN_7
#define VL53L1X_XSHUT_2      GPIO_PIN_8

/**
 * @brief 初始化单个器件：软复位 + 电源配置 + 启动连续测距
 */
void VL53L1X_Init(uint8_t addr);

/**
 * @brief 初始化全部 3 个：XSHUT 逐个上电 + 地址重映射（0x29/0x30/0x31）
 *        上电时调用一次，之后 3 个共存于 I2C 总线。
 */
void VL53L1X_InitAll(void);

/**
 * @brief 读一次测距结果（单位 mm）
 * @return 0 成功
 */
int VL53L1X_ReadDist(uint8_t addr, uint16_t *dist_mm);

/**
 * @brief 读 16 位寄存器（诊断用；模型 ID 0x010F，VL53L1X 应为 0xEACC）
 * @return 0 成功
 */
int VL53L1X_ReadReg16(uint8_t addr, uint16_t reg, uint16_t *val);

/**
 * @brief 读取指定 XSHUT 引脚上的那颗 VL53L1X（三颗同地址 0x29 时分复用）
 * @param xshut_pin VL53L1X_XSHUT_0/1/2
 * @return 0 成功；非 0 失败（含该脚没接设备，上层应隐藏该行）
 */
int VL53L1X_ReadSingle(uint16_t xshut_pin, uint16_t *dist_mm);

#endif /* __VL53L1X_H */
