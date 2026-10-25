/**
 * @file    k210.h
 * @brief   CanMv K210 视觉模块协议层（裂缝 / 车辆 / 人员识别）
 *
 * 链路：STM32 USART1(PA9=TX / PA10=RX) ←→ K210
 *       识别在 K210 上跑，STM32 只做「问 - 答」取值，不参与图像处理。
 *
 * 接线（在核心板上直接插杜邦线，右侧排针第 13 行）：
 *   STM32 PA9  (TX) → K210 RX
 *   STM32 PA10 (RX) → K210 TX
 *   GND 共地；K210 独立 5V 供电（不要从板上 3V3 取）
 *
 * ⚠️ 用硬件 USART1 而非软件模拟串口，是因为核心板右侧排针把 PA9/PA10
 *    引出来了。若日后把核心板插到本项目自制的载板上，载板**没有引出**
 *    PA9/PA10（只引出 PB13/PB15/PC9 三个空闲脚），届时需改回软串口接
 *    PB13/PB15——那版实现见 git 历史（soft_uart.c/h）。
 *
 * 协议（ASCII，便于用串口助手单独调试 K210）：
 *   请求  STM32 → K210 ：  "$Q#"
 *   应答  K210 → STM32 ：  "$K,<status>,<crack>,<vehicle>,<person>#"
 *          status   位掩码：bit0=裂缝有效 bit1=车辆有效 bit2=人员有效
 *          crack    0~100 整数，裂缝置信度百分比（无裂缝为 0）
 *          vehicle  0~255 整数，车辆计数
 *          person   0~255 整数，人员计数
 *
 * ⚠️ K210 端必须「立即应答」：后台持续识别、把最新结果存变量，
 *    收到 $Q# 立刻回缓存值，不要等当前这一帧识别完。
 *    原因：STM32 是阻塞等应答的，若 K210 拖几百 ms，主循环（摇杆/菜单）
 *    会跟着卡。隧道板已有 4G 初始化阻塞的前车之鉴，此处须避免。
 *    ⇒ K210 端的 UART 接收**务必用中断/定时器回调**（见 k210_crack.py），
 *      不要写在「等这一帧识别完」的主循环里。
 *
 * 三路结果的「有效位」是必需的：当前只实现裂缝识别，车辆/人员
 * 预留未做，靠 status 位区分「确实没检测到」和「该功能没接」。
 */
#ifndef __K210_H
#define __K210_H

#include <stdint.h>

/* 等应答的总超时。K210 正常时「几 ms 开始回 + 一帧约 6ms」就完了，
 * 这里取 80ms 是给 K210 端留余量。
 *
 * ⚠️ 这个值同时决定了「K210 没接」时的代价：接收端要等起始位等到超时，
 *    每 2 秒卡主循环 80ms（4%）。取太大（如 300ms）会让菜单/摇杆发涩，
 *    故不宜再放大——要放宽就先把 K210 端响应做成中断驱动。 */
#define K210_REPLY_TIMEOUT_MS   80U

/* 连续多少轮（上报周期 2s）无有效应答后判为掉线，读数转为无效 */
#define K210_LOST_ROUNDS        3U

/* 结果有效位 */
#define K210_VALID_CRACK   0x01U
#define K210_VALID_VEHICLE 0x02U
#define K210_VALID_PERSON  0x04U

/**
 * @brief 初始化 K210 链路（配置软件串口）
 * @note  必须在 SystemClock_Config() 之后调用
 */
void K210_Init(void);

/**
 * @brief 查询一次 K210（发请求 + 等应答 + 解析），阻塞约几 ms
 * @note  周期调用（当前挂在主循环 2 秒上报节拍里）。
 *        K210 无应答时不断开链路，连续 K210_LOST_ROUNDS 轮超时才判掉线。
 */
void K210_Poll(void);

/**
 * @brief 查询链路是否在线（连续多轮无应答则离线）
 * @return 1 在线
 */
int K210_IsOnline(void);

/* ---- 三路识别结果读取：返回 0 成功（结果有效且在线），-1 无效 ---- */

/**
 * @brief 裂缝置信度
 * @param value 输出 0.0~1.0（0 = 无裂缝）
 * @return 0 成功，-1 无效（未接/离线/本轮未识别裂缝）
 */
int CrackConf_Read(float *value);

/**
 * @brief 车辆计数（预留，K210 端尚未实现）
 * @return 0 成功，-1 无效
 */
int VehicleCount_Read(float *value);

/**
 * @brief 人员计数（预留，K210 端尚未实现）
 * @return 0 成功，-1 无效
 */
int PersonCount_Read(float *value);

#endif /* __K210_H */
