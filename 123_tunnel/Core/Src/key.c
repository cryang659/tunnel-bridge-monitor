/**
 * @file    key.c
 * @brief   摇杆导航实现（Y 轴上下 / X 轴左=返回 / SW 按下=确认）
 *
 * 摇杆 VRy/VRx 为模拟量，中间约 1.65V（ADC≈2048），推杆后偏离中间。
 * 方向通过阈值判断，采用「进入沿」触发（回中后才能再次触发同一方向）；
 * SW 为机械按键，按下沿触发。消抖依赖主循环 50ms 轮询。
 */
#include "key.h"
#include "sensor_drivers.h"

/* 摇杆阈值：偏离中心超过此值算「推」（约 0.56V，可调） */
#define JOY_CENTER    2048
#define JOY_THRESH    700

/* 摇杆方向 */
typedef enum {
    JOY_NONE = 0,
    JOY_UP,
    JOY_DOWN,
    JOY_LEFT,
    JOY_RIGHT,
} JoyDir;

void Key_Init(void)
{
    __HAL_RCC_GPIOB_CLK_ENABLE();
    GPIO_InitTypeDef gpio = {0};
    gpio.Pin  = JOY_SW_PIN;
    gpio.Mode = GPIO_MODE_INPUT;
    gpio.Pull = GPIO_PULLUP;                /* 上拉，按下接地 */
    HAL_GPIO_Init(JOY_SW_PORT, &gpio);
}

KeyEvent Key_Scan(void)
{
    static JoyDir last_dir = JOY_NONE;

    /* 读摇杆两轴 ADC（VRy 上下、VRx 左右） */
    uint16_t vy = adc_read_channel(JOY_VRY_CH);
    uint16_t vx = adc_read_channel(JOY_VRX_CH);

    /* 判断方向（上/下优先，其次左/右；斜推算主方向）
     * 注：上推 = VRy 降低、右推 = VRx 升高（真机标定方向）；
     *     若方向反了，把对应轴的 > 和 < 对调即可。 */
    JoyDir dir = JOY_NONE;
    if (vy < JOY_CENTER - JOY_THRESH) {
        dir = JOY_UP;          /* 上推 VRy 降低 */
    } else if (vy > JOY_CENTER + JOY_THRESH) {
        dir = JOY_DOWN;        /* 下推 VRy 升高 */
    } else if (vx < JOY_CENTER - JOY_THRESH) {
        dir = JOY_LEFT;        /* 左推 VRx 降低 */
    } else if (vx > JOY_CENTER + JOY_THRESH) {
        dir = JOY_RIGHT;       /* 右推 VRx 升高 */
    }

    /* 进入沿：回中后重新推才算一次 */
    KeyEvent ev = KEY_NONE;
    if (dir != JOY_NONE && dir != last_dir) {
        if (dir == JOY_UP)         ev = KEY_UP;
        else if (dir == JOY_DOWN)  ev = KEY_DOWN;
        else if (dir == JOY_LEFT)  ev = KEY_BACK;   /* 左推 = 返回 */
        else if (dir == JOY_RIGHT) ev = KEY_OK;     /* 右推 = 确认 */
    }
    last_dir = dir;

    return ev;
}
