/**
 * @file    lte_ct02.c
 * @brief   DX-CT02-A 4G+GNSS 模块驱动实现（AT 指令 + TCP 非透传）
 *
 * 指令集（见 docs/DX-CT02-A应用指导.pdf）：
 *   AT              测试 / OK
 *   ATE0            关回显
 *   AT+REHEAD=0     关闭数据报头（接收纯 TCP 数据，避免混入 +IPD 前缀）
 *   AT+CEREG?       查询网络注册状态（stat=1 或 5 表示已注册）
 *   AT+MDNSGIP=域名 域名解析 → +MDNSGIP:域名,IP
 *   AT+TCPNUM=0,"IP",端口  建立 TCP 会话 → +CIPOPEN: SUCCESS,0
 *   AT+CIPSEND=0    进入发送模式 → >，随后发数据自动提交
 */
#include "lte_ct02.h"
#include "main.h"
#include <string.h>
#include <stdio.h>

extern UART_HandleTypeDef huart2;      /* CubeMX 生成，USART2 接模块 */
#define LTE_UART huart2

/* ============ 读串口数据（阻塞超时） ============ */
static int uart_read(uint8_t *buf, uint16_t max_len, uint32_t timeout_ms)
{
    uint16_t len = 0;
    uint32_t start = HAL_GetTick();
    while (HAL_GetTick() - start < timeout_ms) {
        uint8_t ch;
        if (HAL_UART_Receive(&LTE_UART, &ch, 1, 10) == HAL_OK) {
            if (len < max_len - 1) {
                buf[len++] = ch;
            }
        }
    }
    buf[len] = '\0';
    return len;
}

/* ============ 发送 AT 指令并等待期望响应 ============ */
static int at_command(const char *cmd, const char *expect, uint32_t timeout_ms)
{
    uint8_t buf[256] = {0};
    if (cmd != NULL) {
        HAL_UART_Transmit(&LTE_UART, (uint8_t *)cmd, strlen(cmd), 1000);
        HAL_UART_Transmit(&LTE_UART, (uint8_t *)"\r\n", 2, 1000);
    }
    uart_read(buf, sizeof(buf) - 1, timeout_ms);
    if (expect == NULL) {
        return 0;
    }
    return (strstr((char *)buf, expect) != NULL) ? 0 : -1;
}

/* ============ 查询网络注册状态（1=已注册） ============ */
static int lte_check_registered(void)
{
    uint8_t buf[64] = {0};
    HAL_UART_Transmit(&LTE_UART, (uint8_t *)"AT+CEREG?\r\n", 11, 1000);
    uart_read(buf, sizeof(buf) - 1, 1500);
    /* 响应形如 +CEREG:0,1 或 +CEREG:0,5（stat=1/5 已注册） */
    if (strstr((char *)buf, ",1") != NULL || strstr((char *)buf, ",5") != NULL) {
        return 1;
    }
    return 0;
}

/* ============ 域名解析（AT+MDNSGIP） ============ */
static int resolve_domain(const char *domain, char *ip, int ip_len)
{
    uint8_t buf[200] = {0};
    char cmd[160];
    snprintf(cmd, sizeof(cmd), "AT+MDNSGIP=%s", domain);
    HAL_UART_Transmit(&LTE_UART, (uint8_t *)cmd, strlen(cmd), 1000);
    HAL_UART_Transmit(&LTE_UART, (uint8_t *)"\r\n", 2, 1000);
    uart_read(buf, sizeof(buf) - 1, 8000);

    char *p = strstr((char *)buf, "+MDNSGIP:");
    if (p == NULL) {
        return -1;
    }
    char *comma = strchr(p, ',');
    if (comma == NULL) {
        return -1;
    }
    char *s = comma + 1;
    char *e = s;
    while (*e && *e != '\r' && *e != '\n' && *e != ' ' && *e != 'O') {
        e++;
    }
    if (e - s <= 0 || e - s >= ip_len) {
        return -1;
    }
    memcpy(ip, s, e - s);
    ip[e - s] = '\0';
    return 0;
}

/* ============ 初始化：等待就绪 + 网络注册 ============ */
int LTE_Init(void)
{
    /* 模块上电启动需要时间 */
    HAL_Delay(3000);

    /* 反复发 AT 直到模块响应（等待就绪） */
    int ready = 0;
    for (int i = 0; i < 10; i++) {
        if (at_command("AT", "OK", 1000) == 0) {
            ready = 1;
            break;
        }
        HAL_Delay(1000);
    }
    if (!ready) {
        return -1;
    }

    /* 关回显、关数据报头（保证 TCP 接收为纯数据） */
    at_command("ATE0", "OK", 1000);
    at_command("AT+REHEAD=0", "OK", 1000);

    /* 等待网络注册，最多 60 秒 */
    for (int i = 0; i < 30; i++) {
        if (lte_check_registered()) {
            return 0;
        }
        HAL_Delay(2000);
    }
    return -1;
}

/* ============ 建立 TCP 连接 ============ */
int LTE_ConnectServer(void)
{
    char ip[32] = {0};
    char cmd[160];

    /* 先域名解析，再用 IP 建 TCP（避免个别固件 TCP 不支持域名） */
    if (resolve_domain(SERVER_IP, ip, sizeof(ip)) != 0) {
        /* 解析失败，回退为直接域名连接 */
        snprintf(cmd, sizeof(cmd), "AT+TCPNUM=0,\"%s\",%d", SERVER_IP, SERVER_PORT);
    } else {
        snprintf(cmd, sizeof(cmd), "AT+TCPNUM=0,\"%s\",%d", ip, SERVER_PORT);
    }

    if (at_command(cmd, "+CIPOPEN: SUCCESS", 8000) != 0) {
        return -1;
    }
    return 0;
}

/* ============ 发送 TCP 数据 ============ */
int LTE_Send(const uint8_t *buf, uint16_t len)
{
    /* 进入发送模式，等待 > 提示符 */
    if (at_command("AT+CIPSEND=0", ">", 3000) != 0) {
        return -1;
    }
    /* 发送实际数据（非透传模式直接发，模块自动提交） */
    if (HAL_UART_Transmit(&LTE_UART, (uint8_t *)buf, len, 3000) != HAL_OK) {
        return -1;
    }
    /* 读掉发送确认（+CIPSEND:SUCCESS...），避免污染后续 CONNACK 接收 */
    uint8_t ack[64] = {0};
    uart_read(ack, sizeof(ack) - 1, 2000);
    return 0;
}

/* ============ 接收 TCP 数据 ============ */
int LTE_Recv(uint8_t *buf, uint16_t max_len, uint32_t timeout)
{
    return uart_read(buf, max_len, timeout);
}
