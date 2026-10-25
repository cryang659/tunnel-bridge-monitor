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

/* 阻塞读，但读到数据后 50ms 没新字节就提前返回（AT 响应读完即返回，不空等满 timeout） */
static int uart_read_idle(uint8_t *buf, uint16_t max_len, uint32_t timeout_ms)
{
    uint16_t len = 0;
    uint32_t start = HAL_GetTick();
    uint32_t last_rx = 0;
    int got_first = 0;
    while ((int32_t)(HAL_GetTick() - start) < (int32_t)timeout_ms) {
        uint8_t ch;
        if (HAL_UART_Receive(&LTE_UART, &ch, 1, 10) == HAL_OK) {
            if (len < max_len - 1) {
                buf[len++] = ch;
            }
            last_rx = HAL_GetTick();
            got_first = 1;
        }
        /* 读到第一个字节后，50ms 没新字节才认为响应读完，提前返回 */
        if (got_first && (int32_t)(HAL_GetTick() - last_rx) > 50) {
            break;
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
    uart_read_idle(buf, sizeof(buf) - 1, timeout_ms);
    if (expect == NULL) {
        return 0;
    }
    return (strstr((char *)buf, expect) != NULL) ? 0 : -1;
}

/* ============ 非阻塞 AT 命令（跨 tick 累积串口响应） ============ */
typedef struct {
    int      busy;        /* 1 = 命令执行中 */
    const char *expect;   /* 期望匹配串（NULL 表示只等超时） */
    uint32_t deadline;    /* 超时时刻（tick） */
    uint8_t  rx[256];
    uint16_t rx_len;
    int      result;      /* 完成后：0 成功 / -1 失败 */
} LteNbCmd;

static LteNbCmd s_nb;

static void nb_reset(void)
{
    s_nb.busy = 0;
    s_nb.rx_len = 0;
    s_nb.result = -1;
}

static void nb_start(const char *cmd, const char *expect, uint32_t timeout_ms)
{
    s_nb.expect = expect;
    s_nb.rx_len = 0;
    s_nb.result = -1;
    if (cmd != NULL) {
        HAL_UART_Transmit(&LTE_UART, (uint8_t *)cmd, strlen(cmd), 1000);
        HAL_UART_Transmit(&LTE_UART, (uint8_t *)"\r\n", 2, 1000);
    }
    /* 发完命令立即阻塞读响应（uart_read_idle 循环读不漏字节），读完设结果 */
    s_nb.rx_len = uart_read_idle(s_nb.rx, sizeof(s_nb.rx) - 1, timeout_ms);
    s_nb.rx[s_nb.rx_len] = '\0';
    s_nb.result = (expect != NULL && strstr((char *)s_nb.rx, expect) != NULL) ? 0 : -1;
    s_nb.busy = 0;
}

/* 轮询一次：非阻塞收数据。返回 0=进行中 / 1=已完成（结果看 s_nb.result） */
static int nb_poll(void)
{
    return 1;   /* nb_start 已阻塞读完响应，这里直接返回完成 */
}

/* ============ 非阻塞 4G 初始化状态机 ============ */
typedef enum {
    LTS_POWERUP,   /* 等模块上电 3 秒 */
    LTS_AT,        /* AT 就绪探测（重试 10 次） */
    LTS_ATE0,      /* 关回显 */
    LTS_REHEAD,    /* 关数据报头 */
    LTS_CEREG,     /* 查网络注册（重试 30 次，间隔 2 秒） */
    LTS_DNS,       /* 域名解析 */
    LTS_TCP,       /* 建立 TCP */
    LTS_WAIT,      /* 通用等待（结束后回到 s_resume_state） */
    LTS_DONE,      /* TCP 就绪 */
    LTS_FAIL,      /* 失败 */
} LteTaskState;

static LteTaskState s_ts;
static LteTaskState s_resume_state; /* LTS_WAIT 结束后回到的状态 */
static uint32_t s_wait_deadline;    /* 状态内等待的截止 tick */
static int      s_retry;            /* 当前步骤重试计数 */
static char     s_resolved_ip[32];  /* DNS 解析出的 IP */

/* 从非阻塞缓冲解析 DNS 返回的 IP（+MDNSGIP:domain,ip） */
static int parse_ip_from_rx(char *ip, int ip_len)
{
    char *p = strstr((char *)s_nb.rx, "+MDNSGIP:");
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

void LTE_TaskInit(void)
{
    nb_reset();
    s_ts = LTS_POWERUP;
    s_wait_deadline = HAL_GetTick() + 3000;   /* 模块上电启动约 3 秒 */
    s_retry = 0;
    s_resolved_ip[0] = '\0';
}

int LTE_TaskPoll(void)
{
    switch (s_ts) {

    case LTS_POWERUP:
        if ((int32_t)(HAL_GetTick() - s_wait_deadline) >= 0) {
            s_retry = 0;
            nb_start("AT", "OK", 1000);
            s_ts = LTS_AT;
        }
        break;

    case LTS_AT:
        if (nb_poll()) {
            if (s_nb.result == 0) {
                nb_start("ATE0", "OK", 1000);
                s_ts = LTS_ATE0;
            } else if (++s_retry < 10) {
                s_resume_state = LTS_AT;
                s_wait_deadline = HAL_GetTick() + 1000;
                s_ts = LTS_WAIT;
            } else {
                s_ts = LTS_FAIL;
            }
        }
        break;

    case LTS_ATE0:
        if (nb_poll()) {
            nb_start("AT+REHEAD=0", "OK", 1000);
            s_ts = LTS_REHEAD;
        }
        break;

    case LTS_REHEAD:
        if (nb_poll()) {
            s_retry = 0;
            nb_start("AT+CEREG?", "OK", 1500);
            s_ts = LTS_CEREG;
        }
        break;

    case LTS_CEREG:
        if (nb_poll()) {
            /* 响应形如 +CEREG:0,1 或 +CEREG:0,5（stat=1/5 已注册） */
            if (strstr((char *)s_nb.rx, ",1") != NULL || strstr((char *)s_nb.rx, ",5") != NULL) {
                char cmd[160];
                snprintf(cmd, sizeof(cmd), "AT+MDNSGIP=%s", SERVER_IP);
                nb_start(cmd, "OK", 8000);
                s_ts = LTS_DNS;
            } else if (++s_retry < 30) {
                s_resume_state = LTS_CEREG;
                s_wait_deadline = HAL_GetTick() + 2000;
                s_ts = LTS_WAIT;
            } else {
                s_ts = LTS_FAIL;
            }
        }
        break;

    case LTS_DNS:
        if (nb_poll()) {
            char cmd[160];
            if (parse_ip_from_rx(s_resolved_ip, sizeof(s_resolved_ip)) == 0) {
                snprintf(cmd, sizeof(cmd), "AT+TCPNUM=0,\"%s\",%d", s_resolved_ip, SERVER_PORT);
            } else {
                /* 解析失败，回退为直接域名连接 */
                snprintf(cmd, sizeof(cmd), "AT+TCPNUM=0,\"%s\",%d", SERVER_IP, SERVER_PORT);
            }
            nb_start(cmd, "+CIPOPEN: SUCCESS", 8000);
            s_ts = LTS_TCP;
        }
        break;

    case LTS_TCP:
        if (nb_poll()) {
            s_ts = (s_nb.result == 0) ? LTS_DONE : LTS_FAIL;
        }
        break;

    case LTS_WAIT:
        if ((int32_t)(HAL_GetTick() - s_wait_deadline) >= 0) {
            if (s_resume_state == LTS_AT) {
                nb_start("AT", "OK", 1000);
            } else if (s_resume_state == LTS_CEREG) {
                nb_start("AT+CEREG?", "OK", 1500);
            }
            s_ts = s_resume_state;
        }
        break;

    case LTS_DONE:
    case LTS_FAIL:
    default:
        break;
    }

    if (s_ts == LTS_DONE) {
        return 0;   /* TCP 就绪 */
    }
    if (s_ts == LTS_FAIL) {
        return 1;   /* 失败 */
    }
    return -1;      /* 进行中 */
}

/* 返回当前状态机状态名（ASCII，供 LCD 调试显示） */
const char *LTE_GetStateStr(void)
{
    switch (s_ts) {
        case LTS_POWERUP: return "POWERUP";
        case LTS_AT:      return "AT";
        case LTS_ATE0:    return "ATE0";
        case LTS_REHEAD:  return "REHEAD";
        case LTS_CEREG:   return "CEREG";
        case LTS_DNS:     return "DNS";
        case LTS_TCP:     return "TCP";
        case LTS_WAIT:    return "WAIT";
        case LTS_DONE:    return "DONE";
        case LTS_FAIL:    return "FAIL";
        default:          return "?";
    }
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
    uart_read_idle(ack, sizeof(ack) - 1, 2000);   /* 读发送确认，提前返回不吞掉后续 CONNACK */
    return 0;
}

/* ============ 接收 TCP 数据 ============ */
int LTE_Recv(uint8_t *buf, uint16_t max_len, uint32_t timeout)
{
    return uart_read(buf, max_len, timeout);
}
