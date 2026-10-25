/**
 * @file    k210.c
 * @brief   CanMv K210 视觉模块协议层实现（USART1 硬件串口）
 *
 * 链路：STM32 USART1(PA9=TX/PA10=RX) ←→ K210
 *       识别在 K210 上跑，STM32 只做「问 - 答」取值，不参与图像处理。
 *
 * 解析器刻意用「纯整数手写解析」而不是 sscanf("%f")：
 *   协议里所有字段都是整数（置信度用 0~100 百分比），既省 flash
 *   又不依赖标准库浮点扫描，出错面小。
 */
#include "k210.h"
#include "main.h"      /* huart1 / HAL_UART_* / HAL_GetTick */
#include <string.h>

/* USART1 句柄定义在 main.c 的 USER CODE 区 */
extern UART_HandleTypeDef huart1;

/* 最近一次有效结果 */
static uint8_t s_status  = 0;   /* 有效位掩码，见 K210_VALID_xxx */
static uint8_t s_crack   = 0;   /* 0~100 */
static uint8_t s_vehicle = 0;   /* 0~255 */
static uint8_t s_person  = 0;   /* 0~255 */

static uint8_t s_lost   = 0;    /* 连续无有效应答的轮数 */
static uint8_t s_online = 0;    /* 1 = 在线（未连续超时） */

/* 解析一个十进制整数；成功返回 0 并把后继位置写入 *next。 */
static int parse_uint(const char *p, int *out, const char **next)
{
    if (*p < '0' || *p > '9') {
        return -1;
    }
    int v = 0;
    while (*p >= '0' && *p <= '9') {
        v = v * 10 + (*p - '0');
        if (v > 999999) {
            v = 999999;             /* 钳位，防长串数字溢出 */
        }
        p++;
    }
    *out  = v;
    *next = p;
    return 0;
}

/* 解析一行 "$K,<status>,<crack>,<vehicle>,<person>#"；成功返回 0 */
static int parse_reply(const char *line)
{
    const char *p = strstr(line, "$K,");
    if (p == 0) {
        return -1;
    }
    p += 3;

    int status, crack, veh, per;
    const char *q;

    if (parse_uint(p, &status, &q) != 0 || *q != ',') return -1;
    p = q + 1;
    if (parse_uint(p, &crack, &q) != 0 || *q != ',') return -1;
    p = q + 1;
    if (parse_uint(p, &veh, &q) != 0 || *q != ',') return -1;
    p = q + 1;
    if (parse_uint(p, &per, &q) != 0 || *q != '#') return -1;

    /* 范围钳位，防止 K210 端异常值污染物模型 */
    if (crack > 100) crack = 100;
    if (veh   > 255) veh   = 255;
    if (per   > 255) per   = 255;

    s_status  = (uint8_t)(status & 0x07);
    s_crack   = (uint8_t)crack;
    s_vehicle = (uint8_t)veh;
    s_person  = (uint8_t)per;
    return 0;
}

/* 收一个字节，超时返回 -1 */
static int recv_byte(uint8_t *ch, uint32_t timeout_ms)
{
    if (timeout_ms == 0U) {
        timeout_ms = 1U;            /* HAL 里 0 表示不超时（永久等），必须避开 */
    }
    return (HAL_UART_Receive(&huart1, ch, 1, timeout_ms) == HAL_OK) ? 0 : -1;
}

/* 收一行（以 '\n' 结束，兼容 '\r\n'），带总超时 */
static int recv_line(char *out, uint16_t max_len, uint32_t timeout_ms)
{
    uint32_t deadline = HAL_GetTick() + timeout_ms;
    uint16_t n = 0;

    while (n + 1U < max_len) {
        uint32_t now = HAL_GetTick();
        if ((int32_t)(deadline - now) <= 0) {
            break;
        }
        uint8_t ch;
        if (recv_byte(&ch, deadline - now) != 0) {
            break;
        }
        if (ch == '\r') {
            continue;
        }
        out[n++] = (char)ch;
        if (ch == '\n') {
            break;
        }
    }

    out[n] = '\0';
    return (int)n;
}

void K210_Init(void)
{
    /* USART1 的时钟与 GPIO 由 main.c 的 MX_USART1_UART_Init() →
     * HAL_UART_MspInit() 配好（本函数之前已调用），这里只复位状态。 */
    s_status  = 0;
    s_lost    = 0;
    s_online  = 0;
}

void K210_Poll(void)
{
    /* 发请求帧 */
    static const uint8_t req[] = "$Q#";
    HAL_UART_Transmit(&huart1, (uint8_t *)req, sizeof(req) - 1, 50);

    /* 等应答。可能读到上一轮残留的旧帧，故循环读到「像 $K 开头的行」为止；
     * 整段受 K210_REPLY_TIMEOUT_MS 总超时约束，K210 只回半截也不会卡死。 */
    char line[48];
    uint32_t deadline = HAL_GetTick() + K210_REPLY_TIMEOUT_MS;
    int got = 0;

    while ((int32_t)(deadline - HAL_GetTick()) > 0) {
        int n = recv_line(line, sizeof(line), (uint32_t)(deadline - HAL_GetTick()));
        if (n <= 0) {
            break;                        /* 超时，无数据 */
        }
        if (strstr(line, "$K,") != 0) {
            got = (parse_reply(line) == 0);
            break;                        /* 拿到应答帧（无论解析成败） */
        }
        /* 否则是残留/噪声行，继续读下一行 */
    }

    if (got) {
        s_lost   = 0;
        s_online = 1;
    } else {
        if (s_lost < 255U) {
            s_lost++;
        }
        if (s_lost >= K210_LOST_ROUNDS) {
            s_online = 0;
            s_status = 0;                 /* 掉线：所有结果作废，菜单显示空白 */
        }
    }
}

int K210_IsOnline(void)
{
    return s_online ? 1 : 0;
}

int CrackConf_Read(float *value)
{
    if (value == 0 || !s_online || !(s_status & K210_VALID_CRACK)) {
        return -1;
    }
    *value = (float)s_crack / 100.0f;
    return 0;
}

int VehicleCount_Read(float *value)
{
    if (value == 0 || !s_online || !(s_status & K210_VALID_VEHICLE)) {
        return -1;      /* K210 端尚未实现车辆识别，保持预留 */
    }
    *value = (float)s_vehicle;
    return 0;
}

int PersonCount_Read(float *value)
{
    if (value == 0 || !s_online || !(s_status & K210_VALID_PERSON)) {
        return -1;      /* K210 端尚未实现人员识别，保持预留 */
    }
    *value = (float)s_person;
    return 0;
}
