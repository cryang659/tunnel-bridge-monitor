/**
 * @file    ultrasonic.c
 * @brief   RCWL-1670 超声波测距驱动实现
 *
 * 引脚：Trig = PB15（推挽输出）、Echo = PB13（EXTI 中断输入）
 * 接线与电平注意事项见 ultrasonic.h 顶部说明。
 *
 * ⭐ 为什么用 EXTI 中断测脉宽，而不是在 Tick 里 while 轮询：
 *   RCWL-1670 量程 4m，对应 Echo 脉宽最长约 23ms，而主循环一轮只有 10ms。
 *   若在 Tick 里阻塞等回波，状态机会长期卡住 —— 既拖垮主循环（索力 FFT
 *   要求每 10ms 采一个点，采样间隔一乱频谱就泄漏），又会让 Tick 推进不了。
 *   改用中断后：Trig 由 Tick 每 100ms 发一次，回波的上升/下降沿由 EXTI 捕获，
 *   中断里只读 DWT 计数器做减法，全程不阻塞主循环。
 *
 *   触发 →  IDLE 分支发 12us 高脉冲，状态转 WAITING
 *   上升沿 → EXTI 记下 DWT 时刻
 *   下降沿 → EXTI 算出脉宽 → 距离 = us/58 → 存缓存、状态回 IDLE
 *   超时   → Tick 里检查，30ms 无回波则回 IDLE（模块未接/超量程）
 */
#include "ultrasonic.h"
#include "main.h"

#define US_TRIG_PORT    GPIOB
#define US_TRIG_PIN     GPIO_PIN_15
#define US_ECHO_PORT    GPIOB
#define US_ECHO_PIN     GPIO_PIN_13

#define US_PERIOD_MS    100    /* 测距周期：100ms（模块自身要求 ≥50ms） */
#define US_TIMEOUT_MS   30     /* 等回波超时：30ms 对应约 5m，超量程即放弃 */

/* 有效量程。RCWL-1670 标称 2cm~400cm：
 *   下限 2cm = 模块盲区，以下的读数都是余震/串扰；
 *   上限 400cm = 模块标称上限，超出的读数不可信（原来写 500cm 就是「5 米内照单全收」）。
 * ⚠️ 别再往下收紧 —— 曾一度改成 250cm，若对着 3 米外的墙测，数据会被全部丢弃，
 *    表现为「完全没有数据」。拿不准时宁可放宽。 */
#define US_MIN_CM       2.0f
#define US_MAX_CM       400.0f

/* 跳变抑制：与上次有效值相差超过这个比例就丢弃。
 * 物体不可能在一帧（100ms）内瞬移，所以突变基本是野值。
 * 但真移动时也得跟得上 —— 连续 US_REJECT_MAX 次同向跳变就认可，避免滤波把真实移动也滤掉。 */
#define US_JUMP_REJECT  0.5f   /* 相差超过上次值的 50% 视为可疑 */
#define US_JUMP_MIN_CM  20.0f  /* 但允许变化量**至少**这么大（绝对下限） */
#define US_REJECT_MAX   3      /* 连续可疑这么多次后强制接受（物体真在快速移动） */

typedef enum { US_IDLE, US_WAITING } UsState;

static volatile UsState  s_state = US_IDLE;
static volatile uint32_t s_next_ms = 0;    /* 下次可启动测距的时刻 */
static volatile uint32_t s_trig_ms = 0;    /* 本次触发的时刻（判超时） */
static volatile uint32_t s_rise_cc = 0;    /* Echo 上升沿的 DWT 周期数（0=还没到） */
static volatile float    s_cm = 0.0f;
static volatile uint8_t  s_valid = 0;

static uint32_t s_cycles_per_us = 0;       /* SystemCoreClock / 1e6，ISR 里避免除法 */
static uint8_t  s_reject_cnt = 0;          /* 连续被判可疑的次数，够 US_REJECT_MAX 就强制接受 */

/* 中值滤波：3 个样本取中间值，抹掉单次野值（中值对离群点最有效，且不引入滞后） */
static float s_med[3] = {0.0f, 0.0f, 0.0f};
static uint8_t s_med_n = 0;                /* 已填样本数，不足 3 个时直接用最新值 */
static uint8_t s_med_w = 0;                /* 环形写入游标（不能用 s_med_n % 3 —— 填满后它恒为 3） */

static float us_median3(void)
{
    if (s_med_n < 3) { return s_med[s_med_w == 0 ? 0 : (uint8_t)((s_med_w + 2) % 3)]; }
    float a = s_med[0], b = s_med[1], c = s_med[2];
    /* 三数取中 */
    if (a > b) { float t = a; a = b; b = t; }
    if (b > c) { float t = b; b = c; c = t; }
    if (a > b) { float t = a; a = b; b = t; }
    return b;
}

/* 接受一个原始测距值：先过跳变抑制，再进中值滤波 */
static void us_accept(float cm)
{
    if (s_valid) {
        float diff = cm - s_cm;
        if (diff < 0.0f) { diff = -diff; }
        /* 容差 = max(上次值的 50%, 20cm)。
         * 取 max 是为了应对「上次恰好是个小野值」的情况：若只按比例算，
         * 上次读到 3cm 时容差才 1.5cm，之后所有正常读数都会被无休止地判可疑。 */
        float tol = s_cm * US_JUMP_REJECT;
        if (tol < US_JUMP_MIN_CM) { tol = US_JUMP_MIN_CM; }

        /* 跳太多 → 可疑。连续多次可疑就不再拦（物体真在快速移动） */
        if (diff > tol && s_reject_cnt < US_REJECT_MAX) {
            s_reject_cnt++;
            return;
        }
    }
    s_reject_cnt = 0;

    s_med[s_med_w] = cm;
    s_med_w = (uint8_t)((s_med_w + 1) % 3);
    if (s_med_n < 3) { s_med_n++; }

    s_cm    = us_median3();
    s_valid = 1;
}

/* 微秒延时（DWT 计数器，与 soft_i2c.c / ds18b20.c 一致） */
static void us_delay_us(uint32_t us)
{
    uint32_t start = DWT->CYCCNT;
    uint32_t ticks = us * s_cycles_per_us;
    while ((DWT->CYCCNT - start) < ticks) { }
}

void Ultrasonic_Init(void)
{
    /* DWT 周期计数器（soft_i2c.c / ws2812.c 里也开过，重复开无副作用） */
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    s_cycles_per_us = SystemCoreClock / 1000000U;

    __HAL_RCC_GPIOB_CLK_ENABLE();

    /* Trig：推挽输出，初始低 */
    GPIO_InitTypeDef gpio = {0};
    gpio.Pin   = US_TRIG_PIN;
    gpio.Mode  = GPIO_MODE_OUTPUT_PP;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(US_TRIG_PORT, &gpio);
    HAL_GPIO_WritePin(US_TRIG_PORT, US_TRIG_PIN, GPIO_PIN_RESET);

    /* Echo：EXTI 双边沿中断 + **下拉**。
     * 下拉很重要：模块没接时引脚悬空，不拉会让它随机读到高电平，
     * 中断误触发 → 算出乱跳的假距离 → 灯带乱闪。
     * 加下拉后没接模块时恒为低 → 每周期超时 → s_valid 稳定为 0，
     * 灯带确定性地退回「按颜色常亮」。下拉约 40k 很弱，
     * 模块输出（推挽，或经 1k+2k 分压后等效 667Ω）驱动能力远强于它，不影响测距。 */
    gpio.Pin  = US_ECHO_PIN;
    gpio.Mode = GPIO_MODE_IT_RISING_FALLING;
    gpio.Pull = GPIO_PULLDOWN;
    HAL_GPIO_Init(US_ECHO_PORT, &gpio);

    /* PB13 走 EXTI 线 13，落在 EXTI15_10 这个向量里。
     * 优先级给低（2）：中断里只做减法，不该抢占 SysTick 或其他时序敏感的任务。 */
    HAL_NVIC_SetPriority(EXTI15_10_IRQn, 2, 0);
    HAL_NVIC_EnableIRQ(EXTI15_10_IRQn);

    s_state   = US_IDLE;
    s_valid   = 0;
    s_next_ms = HAL_GetTick();
}

void Ultrasonic_Tick(void)
{
    uint32_t now = HAL_GetTick();

    if (s_state == US_IDLE) {
        if ((int32_t)(now - s_next_ms) >= 0) {
            /* 先把下一次排上——中断可能在发脉冲后立刻到来并改状态 */
            s_next_ms = now + US_PERIOD_MS;
            s_rise_cc = 0;
            s_trig_ms = now;

            HAL_GPIO_WritePin(US_TRIG_PORT, US_TRIG_PIN, GPIO_PIN_SET);
            us_delay_us(12);                       /* >10us 触发脉冲 */
            HAL_GPIO_WritePin(US_TRIG_PORT, US_TRIG_PIN, GPIO_PIN_RESET);

            s_state = US_WAITING;
        }
    } else {
        if ((uint32_t)(now - s_trig_ms) > US_TIMEOUT_MS) {
            s_state = US_IDLE;                     /* 无回波：保留上次有效值 */
        }
    }
}

/* EXTI13 的服务由 HAL 转到这里（stm32f1xx_it.c 的 EXTI15_10_IRQHandler 调用
 * HAL_GPIO_EXTI_IRQHandler，后者清标志后回调本函数）。 */
void HAL_GPIO_EXTI_Callback(uint16_t pin)
{
    if (pin != US_ECHO_PIN || s_state != US_WAITING) {
        return;
    }

    /* 判断是上升沿还是下降沿：靠**引脚当前电平 + 是否已起测**双重判断。
     *
     * ⚠️ 不要读 EXTI->PR：HAL_GPIO_EXTI_IRQHandler 是「先清标志、再调回调」，
     *    等进到这里标志已被清掉，读 PR 恒为 0 —— 曾经这么写过，导致完全测不到距离。
     *
     * 读电平为什么够用：中断延迟的主要来源是发灯带时的关中断（最长约 211us/段），
     * 而 Echo 脉宽最短也有 116us(2cm)、20cm 就是 1160us，远大于延迟，
     * 所以进来时引脚电平基本就是触发它的那个边沿对应的电平。
     *
     * 双重判断的作用：万一有噪声重复触发，
     *   · 已起测时再来「高」→ s_rise_cc!=0，不会重复记录起点
     *   · 未起测时来个「低」→ s_rise_cc==0，不会拿垃圾值算距离 */
    uint8_t level = (HAL_GPIO_ReadPin(US_ECHO_PORT, US_ECHO_PIN) == GPIO_PIN_SET);

    if (level) {
        if (s_rise_cc == 0) {
            s_rise_cc = DWT->CYCCNT;               /* 上升沿：起测 */
        }
    } else if (s_rise_cc != 0) {
        /* 下降沿：算脉宽 */
        uint32_t us = (DWT->CYCCNT - s_rise_cc) / s_cycles_per_us;
        float cm = (float)us / 58.0f;              /* 声速 340m/s，往返 → /58 */
        s_rise_cc = 0;
        s_state   = US_IDLE;

        /* 量程 + 跳变双重判据（见 US_MIN_CM / US_MAX_CM / US_JUMP_REJECT 说明） */
        if (cm >= US_MIN_CM && cm <= US_MAX_CM) {
            us_accept(cm);
        }
    }
}

int Ultrasonic_Read(float *cm)
{
    if (!s_valid || cm == 0) {
        return -1;
    }
    *cm = s_cm;
    return 0;
}
