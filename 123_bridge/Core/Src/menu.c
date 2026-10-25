/**
 * @file    menu.c
 * @brief   128x128 LCD 中文菜单实现（4 键导航）
 *
 * 布局：标题栏(蓝底白字) + 内容区 + 底部操作提示。
 * 主菜单光标高亮（蓝底白字 + ">" 箭头），上下移动、确认进入、返回退出。
 * 实时数据页与阈值页按每页 5 个分两页显示（上下键翻页）。
 */
#include "menu.h"
#include "menu_text.h"
#include "st7735.h"
#include "key.h"
#include "edge_alarm.h"
#include "main.h"          /* Actuator_*（执行器页，实现见 main.c） */
#include "data_report.h"   /* DEVICE_ID */
#include "lte_ct02.h"      /* LTE_GetStateStr */
#include <stdio.h>
#include <string.h>

/* ============ 页面 ============ */
typedef enum {
    PAGE_MAIN = 0,
    PAGE_REALTIME,
    PAGE_THRESHOLD,
    PAGE_ALARM,
    PAGE_NET,
    PAGE_SYSINFO,
    PAGE_ACTUATOR,
} MenuPage;

/* ============ 布局常量 ============ */
#define TITLE_Y           0      /* 标题栏 y */
#define CONTENT_Y         24     /* 内容起始 y */
#define ROW_H             16     /* 行高（16px 中文） */
#define HINT_Y            112    /* 底部时间行 y（子页面用） */
#define PER_PAGE          5      /* 实时数据/阈值每页条目数 */

/* ============ 菜单项 / 传感器文案 ============ */
static const char *MAIN_ITEMS[] = {
    TXT_REALTIME, TXT_THRESHOLD, TXT_ALARM, TXT_NET, TXT_SYSINFO, TXT_ACTUATOR
};
#define MAIN_ITEM_COUNT 6

static const char *SENSOR_NAME[SENSOR_TYPE_COUNT] = {
    TXT_VIBRATION, TXT_STRESS, TXT_TEMPERATURE, TXT_HUMIDITY,
    TXT_WATER_LEVEL, TXT_SEEPAGE, TXT_DISPLACEMENT, TXT_LOAD,
    TXT_CABLE_FORCE,
    "\xC7\xC5\xC3\xE6" "1",   /* 桥面1 */
    "\xC7\xC5\xB6\xD5" "1",   /* 桥墩1 */
    "\xC7\xC5\xC3\xE6" "2",   /* 桥面2 */
    "\xC7\xC5\xB6\xD5" "2",   /* 桥墩2 */
    TXT_POSITION,
};
static const char *SENSOR_UNIT[SENSOR_TYPE_COUNT] = {
    "g", "uE", "C", "%", "m", "%", "mm", "t", "kN", "C", "C", "C", "C", "cm"
};

/* 本板启用的传感器（实时数据页只显示这些）：桥板 */
static const SensorType ENABLED_SENSORS[] = {
    SENSOR_VIBRATION, SENSOR_CABLE_FORCE, SENSOR_STRESS,
    SENSOR_TEMPERATURE, SENSOR_HUMIDITY, SENSOR_LOAD,
    SENSOR_DECK_TEMP, SENSOR_DECK_TEMP_2, SENSOR_PIER_TEMP, SENSOR_PIER_TEMP_2,
    SENSOR_POSITION,
};
#define ENABLED_COUNT (sizeof(ENABLED_SENSORS) / sizeof(ENABLED_SENSORS[0]))
/* 总页数：11 项 ÷ 每页 5 = 3 页（最后 1 页只有 1 项） */
#define PAGE_COUNT ((ENABLED_COUNT + PER_PAGE - 1) / PER_PAGE)

/* ============ 运行状态 ============ */
static MenuPage g_page = PAGE_MAIN;
static uint8_t  g_cursor = 0;          /* 主菜单光标 0..4 */
static uint8_t  g_sub_page = 0;        /* 分页页号 0/1 */
static uint32_t g_last_refresh = 0;
static const MenuCtx *g_ctx = 0;

static uint8_t  g_th_cursor = 0;       /* 阈值页选中项在 ENABLED_SENSORS 中的索引 */
static uint8_t  g_th_edit = 0;         /* 0=选择态，1=编辑态 */
static float    g_th_old = 0.0f;       /* 进入编辑前的旧值，取消时恢复 */

static uint8_t  g_act_cursor = 0;      /* 执行器页选中行：0=蜂鸣器 1=继电器 2=开关 3=颜色 4=灯效 5=跟随 */
static int      g_time_sec = -1;       /* 上次绘制时间的秒数，用于每秒局部刷新 */

#define REFRESH_MS 500

/* 阈值编辑步进（按单位合理取值） */
static float th_step(SensorType type)
{
    switch (type) {
    case SENSOR_VIBRATION:  return 0.01f;
    case SENSOR_STRESS:     return 10.0f;
    case SENSOR_TEMPERATURE:
    case SENSOR_DECK_TEMP:
    case SENSOR_PIER_TEMP:  return 1.0f;
    case SENSOR_HUMIDITY:   return 1.0f;
    case SENSOR_WATER_LEVEL:return 0.1f;
    case SENSOR_SEEPAGE:    return 5.0f;
    case SENSOR_DISPLACEMENT:return 1.0f;
    case SENSOR_LOAD:       return 1.0f;
    case SENSOR_CABLE_FORCE:return 10.0f;
    case SENSOR_POSITION:   return 5.0f;    /* 超声波测距阈值，5cm 一档 */
    default:                return 1.0f;
    }
}

/* ============ 通用绘制 ============ */
static void draw_title(const char *title)
{
    ST7735_FillRect(0, TITLE_Y, LCD_WIDTH, ROW_H, LCD_BLUE);
    int n = (int)strlen(title) / 2;            /* 汉字数（GBK 每字 2 字节） */
    int x = (LCD_WIDTH - n * 16) / 2;
    ST7735_DrawCnStr(x, TITLE_Y, title, LCD_WHITE, LCD_BLUE);
}

/* 底部行显示实时时间 HH:MM:SS（服务器对时校准，见 data_report.c）。
 * 各子页面底部统一显示时间，不再显示操作提示。 */
static void draw_time_at(int y)
{
    char t[9];
    TimeSync_GetHMS(t);
    ST7735_FillRect(0, y, LCD_WIDTH, ROW_H, LCD_BLACK);
    int x = (LCD_WIDTH - (int)strlen(t) * 8) / 2;
    ST7735_DrawString(x, y + 4, t, LCD_YELLOW, LCD_BLACK);
}

static void draw_time_bottom(void)
{
    draw_time_at(HINT_Y);
}

/* 各子页面底部时间每秒刷新一次（只重画底部行，不动内容区） */
static void update_time_if_needed(void)
{
    char t[9];
    int sec;
    TimeSync_GetHMS(t);
    sec = (t[6] - '0') * 10 + (t[7] - '0');
    if (sec != g_time_sec) {
        g_time_sec = sec;
        draw_time_bottom();
    }
}

/* ---- 名称列：汉字(16px) + 半角数字/ASCII(8px) 混排绘制与宽度 ---- */
static int name_px(const char *s)
{
    int w = 0;
    while (*s) {
        if ((uint8_t)*s < 0x80) { w += 8; s++; }
        else { w += 16; s += 2; }
    }
    return w;
}

static void draw_name(int y, const char *s, uint16_t color, uint16_t bg)
{
    int x = 0;
    while (*s) {
        if ((uint8_t)*s < 0x80) {
            ST7735_DrawChar(x, y + 4, *s, color, bg);
            x += 8;
            s++;
        } else {
            char pr[3] = { s[0], s[1], '\0' };
            ST7735_DrawCnStr(x, y, pr, color, bg);
            x += 16;
            s += 2;
        }
    }
}

/* ============ 执行器页文案（定义在文件后部，滑动渲染要用，故前置声明） ============ */
/* 第 1、2 行是蜂鸣器/继电器的开关模式，第 3~6 行是灯带的 开关/颜色/灯效/跟随。
 * ⚠️ 6 行占 y24~119（24 + 6×16 = 120），而底部时间行在 y112 —— 会重叠 8px。
 *    故本页**不画时间**，见 draw_actuator 末尾。其他页面仍是 5 行，时间照常。
 * ⚠️ 行名最宽 3 字（蜂鸣器/继电器），值从 x=64 起；最长值「距离变色」「一直打开」
 *    都是 4 字 64px，画到 x=127 正好贴边不折行 —— 再加字就会折行。 */
#define ACT_ROW_COUNT 6
static const char *ACT_NAMES[ACT_ROW_COUNT] = {
    TXT_ACT_BUZZER, TXT_ACT_RELAY, TXT_LED_SWITCH,
    TXT_LED_COLOR, TXT_LED_EFFECT, TXT_LED_FOLLOW
};
static const char *act_value_str(uint8_t i);
static uint16_t    act_value_color(uint8_t i);

/* ============================================================
 * 高亮条滑动动画（主菜单光标移动）
 *
 * 时序约束：主循环 10ms 一轮，但重画两行(128x32)要几毫秒，
 * 故按 SLIDE_FRAME_MS 出帧。进度按「绝对时间」推进而非帧计数：
 * 某一轮被采集/上报拖慢时，下一帧算出的位置依然正确（允许跳帧）。
 * ============================================================ */
#define SLIDE_MS        160     /* 滑动总时长 */
#define SLIDE_FRAME_MS  25      /* 出帧间隔 */

#define TEXT_X   32             /* 菜单项文字列起点 */
#define ARROW_X  8              /* 选中箭头 x */

typedef struct {
    uint8_t  active;
    int      from_y, to_y;      /* 起止高亮条顶部 y */
    int      bar_y;             /* 当前高亮条顶部 y（滑动中会落在两行之间） */
    uint32_t start_ms;
    uint32_t last_frame;
} SlideAnim;

static SlideAnim g_slide;

/* slide_render 的定义在滑动段末尾（它要分派到各页面的行绘制函数，
 * 而那些函数定义更靠后），故在此先声明。 */
static void slide_render(void);

/* 缓出：p = 1-(1-t)^2，t 与返回值均为 Q12 定点（4096 = 1.0）。
 * 起步快、收尾慢；只需一次乘法，不必查表。 */
static uint16_t ease_out_q12(uint32_t t_q12)
{
    int32_t u;
    if (t_q12 >= 4096u) { return 4096u; }
    u = (int32_t)(4096u - t_q12);              /* u 最大 4096，u*u 不超 2^24，不溢出 */
    return (uint16_t)(4096 - (uint32_t)((u * u) >> 12));
}

/* 把 [y, y+h) 按高亮条位置切成三段（条上 / 条内 / 条下），各填一个底色。
 * ⚠️ 只允许用于**不含文字**的区域。 */
static void fill_band(int x, int y, int w, int h, int bar_y,
                      uint16_t above, uint16_t bar, uint16_t below)
{
    int bs = bar_y, be = bar_y + ROW_H;

    if (bs > y) {                                   /* 条上方 */
        int e = (bs < y + h) ? bs : y + h;
        ST7735_FillRect(x, y, w, e - y, above);
    }
    if (be > y && bs < y + h) {                     /* 条内 */
        int s = (bs > y) ? bs : y;
        int e = (be < y + h) ? be : y + h;
        ST7735_FillRect(x, s, w, e - s, bar);
    }
    if (be < y + h) {                               /* 条下方 */
        int s = (be > y) ? be : y;
        ST7735_FillRect(x, s, w, y + h - s, below);
    }
}

/* ============ 主菜单 ============ */

/* 重画 [r0,r1] 这几行的「高亮条 + 文字」。
 *
 * ⚠️ 本屏是软件 SPI 直接写屏、没有帧缓冲：任何"先清空再重画"的中间态都看得见。
 * 由此得出一条铁律——**每一笔画下去的都必须是最终颜色**：
 *   · 不含文字的区域（箭头列、文字右侧）→ 分界底色直接填
 *   · 文字区 → 交给 DrawCnStrSplit 把底色和文字一次写完
 *     若先 FillRect 铺一层底色再画字，"整片黑/蓝"的瞬间每帧都会闪一次。
 *
 * ⚠️ 每行必须用**自己的**分界线：条只有 16px 高，跨两行时上行的分界在
 *    「条顶」（黑→蓝）、下行的分界在「条底」（蓝→黑），位置和颜色都不同。
 *
 * ⚠️ 文字右侧填充**必须逐行做**：菜单项宽度不一（「系统信息」64px、「执行器」48px），
 *    若按最宽的一项算统一右边界，窄的那几行会留一段从没画过的区域——
 *    平时背景黑看不出来，一旦被选中、蓝条上来，就嵌出一块黑。 */
static void draw_main_rows(int r0, int r1, int bar_y)
{
    int y0 = CONTENT_Y + r0 * ROW_H;
    int y1 = CONTENT_Y + (r1 + 1) * ROW_H;
    int h  = y1 - y0;

    /* 1. 文字左侧（含箭头列）：无文字，整段直接填最终底色 */
    fill_band(0, y0, TEXT_X, h, bar_y, LCD_BLACK, LCD_BLUE, LCD_BLACK);

    /* 2. 逐行：画文字 + 补该行文字右侧的底色 */
    for (int i = r0; i <= r1; i++) {
        int yi = CONTENT_Y + i * ROW_H;
        int tx = TEXT_X + name_px(MAIN_ITEMS[i]);
        int split;
        uint16_t above, below;

        if (bar_y <= yi) {
            /* 条从行顶（或更上方）起 → 该行先蓝，到条底转黑 */
            split = bar_y + ROW_H;
            above = LCD_BLUE;  below = LCD_BLACK;
            if (split > yi + ROW_H) { split = yi + ROW_H; }   /* 条盖满整行 */
        } else {
            /* 条从行中间起 → 该行先黑，到条顶转蓝 */
            split = bar_y;
            above = LCD_BLACK; below = LCD_BLUE;
        }

        ST7735_DrawCnStrSplit(TEXT_X, yi, MAIN_ITEMS[i], LCD_WHITE,
                              split, above, below);

        if (tx < LCD_WIDTH) {
            fill_band(tx, yi, LCD_WIDTH - tx, ROW_H, bar_y,
                      LCD_BLACK, LCD_BLUE, LCD_BLACK);
        }
    }

    /* 3. 箭头最后画（第 1 步填底色时把它盖掉了） */
    ST7735_DrawChar(ARROW_X, bar_y + 4, '>', LCD_WHITE, LCD_BLUE);
}

/* 立即落位到目标（动画被打断时调用，避免残留半截高亮条） */
static void slide_finish(void)
{
    if (!g_slide.active) { return; }
    g_slide.active = 0;
    g_slide.bar_y  = g_slide.to_y;
    slide_render();
}

/* 每轮主循环调用：到点出一帧 */
static void slide_tick(void)
{
    uint32_t now, el;
    uint16_t p;

    if (!g_slide.active) { return; }
    now = HAL_GetTick();
    if (now - g_slide.last_frame < SLIDE_FRAME_MS) { return; }
    g_slide.last_frame = now;

    el = now - g_slide.start_ms;
    if (el >= SLIDE_MS) {                       /* 结束：落位并停 */
        g_slide.active = 0;
        g_slide.bar_y  = g_slide.to_y;
        slide_render();
        return;
    }
    p = ease_out_q12((el * 4096u) / SLIDE_MS);
    g_slide.bar_y = g_slide.from_y
                  + (int)(((int32_t)(g_slide.to_y - g_slide.from_y) * (int32_t)p) >> 12);
    slide_render();
}

/* 启动滑动 */
static void slide_start(int from_row, int to_row)
{
    slide_finish();        /* 上一次没滑完先落位，避免两条互相残留 */
    g_slide.active     = 1;
    g_slide.from_y     = CONTENT_Y + from_row * ROW_H;
    g_slide.to_y       = CONTENT_Y + to_row   * ROW_H;
    g_slide.bar_y      = g_slide.from_y;
    g_slide.start_ms   = HAL_GetTick();
    g_slide.last_frame = g_slide.start_ms;
}

/* 整页绘制：只在进入主菜单时调一次（返回主菜单 / 首次进入） */
static void draw_main_menu(void)
{
    ST7735_Clear(LCD_BLACK);
    draw_title(TXT_MAIN_MENU);
    /* 重置动画状态：高亮条静止在光标行上 */
    g_slide.active = 0;
    g_slide.bar_y  = CONTENT_Y + g_cursor * ROW_H;
    g_slide.from_y = g_slide.bar_y;
    g_slide.to_y   = g_slide.bar_y;
    /* 6 项从 CONTENT_Y 起占 24~120；主菜单底部不显示时间 */
    draw_main_rows(0, MAIN_ITEM_COUNT - 1, g_slide.bar_y);
}

/* 光标移动：启动滑动（不再瞬移）。只重画受影响的 2 行，不整屏重绘。 */
static void move_cursor(int from, int to)
{
    g_cursor = (uint8_t)to;
    slide_start(from, to);
}

/* ============================================================
 * 列表页单行的「分界底色」重画（滑动动画用）
 *
 * 与整页绘制把底色当参数传入不同，这里底色要按高亮条位置**逐行算**：
 *   条从行顶起 → 该行先蓝、到条底转黑
 *   条从行中起 → 该行先黑、到条顶转蓝
 * 算出 (split, above, below) 后交给驱动层的 *Split 系列一次写完。
 * ============================================================ */

/* 算出某行的分界参数 */
static void row_split(int yi, int *split, uint16_t *above, uint16_t *below)
{
    if (g_slide.bar_y <= yi) {
        *split = g_slide.bar_y + ROW_H;
        *above = LCD_BLUE;  *below = LCD_BLACK;
        if (*split > yi + ROW_H) { *split = yi + ROW_H; }   /* 条盖满整行 */
    } else {
        *split = g_slide.bar_y;
        *above = LCD_BLACK; *below = LCD_BLUE;
    }
}

/* 阈值页一行：名称 + 阈值 + 单位，底色按高亮条分界 */
static void draw_th_row_split(int r, int idx, SensorType type)
{
    int yi = CONTENT_Y + r * ROW_H;
    int sel = (idx == g_th_cursor);
    int split;
    uint16_t above, below, vc;
    int tvx, tx;
    char val[16];

    row_split(yi, &split, &above, &below);
    vc = (sel && g_th_edit) ? LCD_YELLOW : LCD_GREEN;
    tvx = (name_px(SENSOR_NAME[type]) > 56) ? (name_px(SENSOR_NAME[type]) + 8) : 56;

    /* 名称（汉字+数字混排），底色随分界一起写 */
    ST7735_DrawNameSplit(0, yi, SENSOR_NAME[type], LCD_WHITE,
                         split, above, below);
    tx = name_px(SENSOR_NAME[type]);
    if (tx < tvx) {
        fill_band(tx, yi, tvx - tx, ROW_H, g_slide.bar_y,
                  LCD_BLACK, LCD_BLUE, LCD_BLACK);
    }

    /* 数值（固定宽度铺底，避免长短变化残留） */
    snprintf(val, sizeof(val), "%5.2f", EdgeAlarm_GetThreshold(type));
    {
        int w = 56;                       /* 与整页绘制一致的数值区宽度 */
        if (tvx + w > LCD_WIDTH) { w = LCD_WIDTH - tvx; }
        fill_band(tvx, yi, w, ROW_H, g_slide.bar_y, LCD_BLACK, LCD_BLUE, LCD_BLACK);
        ST7735_DrawAsciiSplit(tvx, yi, val, vc, split, above, below);
    }

    /* 单位 */
    {
        const char *u = SENSOR_UNIT[type];
        int uw = (int)strlen(u) * 8;
        int ux = tvx + 48;
        if (ux + uw > LCD_WIDTH) { ux = LCD_WIDTH - uw; }
        if (ux < tvx + 56) { ux = tvx + 56; }
        if (ux > tvx + 56) {
            fill_band(tvx + 56, yi, ux - (tvx + 56), ROW_H, g_slide.bar_y,
                      LCD_BLACK, LCD_BLUE, LCD_BLACK);
        }
        if (ux + uw <= LCD_WIDTH) {
            fill_band(ux, yi, uw, ROW_H, g_slide.bar_y, LCD_BLACK, LCD_BLUE, LCD_BLACK);
            ST7735_DrawAsciiSplit(ux, yi, u, vc, split, above, below);
            tx = ux + uw;
        } else {
            tx = tvx + 56;
        }
    }
    if (tx < LCD_WIDTH) {
        fill_band(tx, yi, LCD_WIDTH - tx, ROW_H, g_slide.bar_y,
                  LCD_BLACK, LCD_BLUE, LCD_BLACK);
    }
}

/* 执行器页一行：名称 + 模式文字，底色按高亮条分界 */
/* 执行器页第 i 行（行名 + 该行当前值）。值统一从 x=64 起画。 */
static void draw_act_row_split(int i, const char *name)
{
    int yi = CONTENT_Y + i * ROW_H;
    int split;
    uint16_t above, below;
    int tx;

    row_split(yi, &split, &above, &below);

    ST7735_DrawNameSplit(0, yi, name, LCD_WHITE, split, above, below);
    tx = name_px(name);
    /* 值文字从 x=64 起；先补齐底色再画，避免长文字残留半截 */
    fill_band(tx, yi, LCD_WIDTH - tx, ROW_H, g_slide.bar_y,
              LCD_BLACK, LCD_BLUE, LCD_BLACK);
    ST7735_DrawCnStrSplit(64, yi, act_value_str(i), act_value_color(i),
                          split, above, below);
}

/* 按当前页面分派：滑动时重画受影响的行 */
static void slide_render(void)
{
    int a = g_slide.from_y, b = g_slide.to_y;
    int top    = (a < b) ? a : b;
    int bottom = ((a > b) ? a : b) + ROW_H;
    int r0 = (top    - CONTENT_Y) / ROW_H;
    int r1 = (bottom - CONTENT_Y - 1) / ROW_H;
    if (r0 < 0) { r0 = 0; }

    switch (g_page) {
    case PAGE_MAIN:
        if (r1 > MAIN_ITEM_COUNT - 1) { r1 = MAIN_ITEM_COUNT - 1; }
        draw_main_rows(r0, r1, g_slide.bar_y);
        break;

    case PAGE_THRESHOLD: {
        int start = g_sub_page * PER_PAGE;
        int cnt = ENABLED_COUNT - start;
        if (cnt > PER_PAGE) { cnt = PER_PAGE; }
        if (r1 > cnt - 1) { r1 = cnt - 1; }
        for (int r = r0; r <= r1; r++) {
            int idx = start + r;
            draw_th_row_split(r, idx, ENABLED_SENSORS[idx]);
        }
        break;
    }

    case PAGE_ACTUATOR: {
        if (r1 > ACT_ROW_COUNT - 1) { r1 = ACT_ROW_COUNT - 1; }
        for (int i = r0; i <= r1; i++) {
            draw_act_row_split(i, ACT_NAMES[i]);
        }
        break;
    }

    default:
        break;
    }
}

/* ============ 实时数据页（分页，数值局部刷新） ============ */
static void draw_realtime_values(void)
{
    int start = g_sub_page * PER_PAGE;
    int cnt = ENABLED_COUNT - start;
    if (cnt > PER_PAGE) cnt = PER_PAGE;

    for (int r = 0; r < cnt; r++) {
        SensorType type = ENABLED_SENSORS[start + r];
        int y = CONTENT_Y + r * ROW_H;
        /* 数值列起点：4 字名称占 64px，值从 64 起防与名称重叠；2 字名称保持 40 */
        int vx = (name_px(SENSOR_NAME[type]) > 48) ? (name_px(SENSOR_NAME[type]) + 8) : 48;
        /* 清空数值+单位区域（y+4 起 8px 高，8px 字体垂直居中） */
        ST7735_FillRect(vx, y + 4, LCD_WIDTH - vx, 8, LCD_BLACK);
        /* 在采集数据里找该类型，找到则显示数值 */
        for (int i = 0; i < g_ctx->count; i++) {
            if (g_ctx->data[i].type == type) {
                char val[16];
                snprintf(val, sizeof(val), "%5.2f", g_ctx->data[i].value);
                ST7735_DrawString(vx, y + 4, val, LCD_GREEN, LCD_BLACK);
                ST7735_DrawString(vx + 48, y + 4, SENSOR_UNIT[type], LCD_GREEN, LCD_BLACK);
                break;
            }
        }
    }
}

/* 只重画**内容区**（标题与时间行不动）。
 *
 * ⚠️ 不要整屏 ST7735_Clear：本屏无帧缓冲，清屏就是直接写全屏（12ms），
 *    翻页时会看到明显闪黑。翻页只换内容区的 5 行，标题栏和时间行没变，
 *    清掉再画纯属浪费且可见。
 * 逐行铺黑 + 画名称，每行一次写完（不先清后画，避免中间态可见）。 */
static void draw_realtime_rows(void)
{
    int start = g_sub_page * PER_PAGE;
    int cnt = ENABLED_COUNT - start;
    if (cnt > PER_PAGE) cnt = PER_PAGE;

    for (int r = 0; r < cnt; r++) {
        int y = CONTENT_Y + r * ROW_H;
        ST7735_FillRect(0, y, LCD_WIDTH, ROW_H, LCD_BLACK);
        draw_name(y, SENSOR_NAME[ENABLED_SENSORS[start + r]], LCD_WHITE, LCD_BLACK);
    }
    /* 本页条目不足一整屏时，把剩余行清掉（否则残留上一页的内容） */
    for (int r = cnt; r < PER_PAGE; r++) {
        ST7735_FillRect(0, CONTENT_Y + r * ROW_H, LCD_WIDTH, ROW_H, LCD_BLACK);
    }
    draw_realtime_values();
}

/* 整页绘制：只在进入页面时调一次（标题 + 时间 + 行区） */
static void draw_realtime(void)
{
    ST7735_Clear(LCD_BLACK);
    draw_title(TXT_REALTIME);
    draw_time_bottom();
    draw_realtime_rows();
}

/* ============ 阈值设置页（可编辑：选择态光标高亮，编辑态数值黄色） ============ */
static void draw_threshold(void)
{
    ST7735_Clear(LCD_BLACK);
    draw_title(TXT_THRESHOLD);

    int start = g_sub_page * PER_PAGE;
    int cnt = ENABLED_COUNT - start;
    if (cnt > PER_PAGE) cnt = PER_PAGE;
    for (int r = 0; r < cnt; r++) {
        int idx = start + r;                       /* 在 ENABLED_SENSORS 中的索引 */
        SensorType type = ENABLED_SENSORS[idx];
        int y = CONTENT_Y + r * ROW_H;
        char val[16];
        int sel = (idx == g_th_cursor);
        uint16_t bg  = sel ? LCD_BLUE : LCD_BLACK;
        uint16_t vc  = (sel && g_th_edit) ? LCD_YELLOW : LCD_GREEN;
        if (sel) {
            ST7735_FillRect(0, y, LCD_WIDTH, ROW_H, LCD_BLUE);   /* 选中行高亮 */
        }
        draw_name(y, SENSOR_NAME[type], LCD_WHITE, bg);
        /* 数值列起点：名称宽度超 48px 则从 64 起，否则 48 */
        int tvx = (name_px(SENSOR_NAME[type]) > 56) ? (name_px(SENSOR_NAME[type]) + 8) : 56;
        snprintf(val, sizeof(val), "%5.2f", EdgeAlarm_GetThreshold(type));
        ST7735_DrawString(tvx, y + 4, val, vc, bg);
        ST7735_DrawString(tvx + 48, y + 4, SENSOR_UNIT[type], vc, bg);
    }
    draw_time_bottom();
}

/* ============ 告警记录页 ============
 * 拆两个函数：draw_alarm 整页绘制（进入页面时一次），draw_alarm_values 只重画
 * 级别文字区（定时刷新）。级别文字每 500ms 可能变化，若整页重绘会闪。 */
#define ALARM_LV_X      32    /* 级别文字区：2 字 32px，取 64px 宽留余量 */
#define ALARM_LV_W      64
#define ALARM_LV_Y      64
#define ALARM_NONE_Y    88    /* 「无告警」行，仅 NORMAL 时显示 */

/* 只重画告警级别 + 无告警提示（局部刷新，不动标题/标签/时间） */
static void draw_alarm_values(void)
{
    AlarmLevel max = ALARM_LEVEL_NORMAL;
    for (int i = 0; i < g_ctx->count; i++) {
        if (g_ctx->alarm[i] > max) max = g_ctx->alarm[i];
    }

    /* 先清两行文字区，避免级别变化时残留旧字（如「无告警」3 字比级别 2 字宽） */
    ST7735_FillRect(ALARM_LV_X, ALARM_LV_Y, ALARM_LV_W, ROW_H, LCD_BLACK);
    ST7735_FillRect(ALARM_LV_X, ALARM_NONE_Y, ALARM_LV_W, ROW_H, LCD_BLACK);

    const char *lv = TXT_NORMAL;
    uint16_t color = LCD_GREEN;
    if (max == ALARM_LEVEL_WARNING) { lv = TXT_WARNING; color = LCD_YELLOW; }
    else if (max == ALARM_LEVEL_ALARM) { lv = TXT_ALARM_LV; color = LCD_RED; }

    ST7735_DrawCnStr(48, ALARM_LV_Y, lv, color, LCD_BLACK);        /* 级别大字居中 */
    if (max == ALARM_LEVEL_NORMAL) {
        ST7735_DrawCnStr(40, ALARM_NONE_Y, TXT_NONE_ALARM, LCD_GREEN, LCD_BLACK);
    }
}

static void draw_alarm(void)
{
    ST7735_Clear(LCD_BLACK);
    draw_title(TXT_ALARM);
    ST7735_DrawCnStr(0, 40, TXT_CURRENT, LCD_WHITE, LCD_BLACK);   /* 当前状态 */
    draw_alarm_values();
    draw_time_bottom();
}

/* ============ 网络状态页 ============ */
static void draw_net(void)
{
    ST7735_Clear(LCD_BLACK);
    draw_title(TXT_NET);

    ST7735_DrawCnStr(0, 32, TXT_DEVICE, LCD_WHITE, LCD_BLACK);
    ST7735_DrawString(48, 36, DEVICE_ID, LCD_GREEN, LCD_BLACK);

    ST7735_DrawCnStr(0, 56, TXT_NET, LCD_WHITE, LCD_BLACK);
    const char *s = g_ctx->net_ok ? TXT_CONNECTED : TXT_DISCONNECTED;
    uint16_t c = g_ctx->net_ok ? LCD_GREEN : LCD_RED;
    ST7735_DrawCnStr(48, 56, s, c, LCD_BLACK);

    /* 4G 状态机当前状态（调试定位卡点用） */
    char statebuf[20];
    snprintf(statebuf, sizeof(statebuf), "4G:%s", LTE_GetStateStr());
    ST7735_DrawString(0, 80, statebuf, LCD_YELLOW, LCD_BLACK);

    draw_time_bottom();
}

/* ============ 系统信息页 ============ */
static void draw_sysinfo(void)
{
    ST7735_Clear(LCD_BLACK);
    draw_title(TXT_SYSINFO);

    /* 项目名 */
    ST7735_DrawCnStr(0, 28, TXT_PROJECT, LCD_WHITE, LCD_BLACK);

    /* 固件版本 */
    ST7735_DrawCnStr(0, 48, TXT_FW_VER, LCD_WHITE, LCD_BLACK);
    ST7735_DrawString(64, 52, "V2.0", LCD_GREEN, LCD_BLACK);

    /* 设备编号 */
    ST7735_DrawCnStr(0, 68, TXT_DEV_NO, LCD_WHITE, LCD_BLACK);
    ST7735_DrawString(64, 72, DEVICE_ID, LCD_GREEN, LCD_BLACK);

    draw_time_bottom();
}

/* ============ 执行器页（蜂鸣器 / 继电器 + 灯带 开关/颜色/跟随） ============ */

/* 开关模式文案：自动 → 一直打开 → 关闭 */
static const char *act_mode_str(ActuatorMode m)
{
    switch (m) {
    case ACT_MODE_AUTO: return TXT_ACT_AUTO;
    case ACT_MODE_ON:   return TXT_ACT_ON;
    default:            return TXT_ACT_OFF;
    }
}

/* 各行的当前值文案 */
static const char *act_value_str(uint8_t i)
{
    switch (i) {
    case 0: return act_mode_str(Actuator_GetBuzzerMode());
    case 1: return act_mode_str(Actuator_GetRelayMode());
    case 2: return Led_GetOn() ? TXT_LED_ON : TXT_ACT_OFF;
    case 3:
        switch (Led_GetColor()) {
        case LED_COLOR_AUTO:    return TXT_ACT_AUTO;
        case LED_COLOR_RED:     return TXT_LED_RED;
        case LED_COLOR_ORANGE:  return TXT_LED_ORANGE;
        case LED_COLOR_YELLOW:  return TXT_LED_YELLOW;
        case LED_COLOR_GREEN:   return TXT_LED_GREEN;
        case LED_COLOR_CYAN:    return TXT_LED_CYAN;
        case LED_COLOR_BLUE:    return TXT_LED_BLUE;
        case LED_COLOR_PURPLE:  return TXT_LED_PURPLE;
        case LED_COLOR_WHITE:   return TXT_LED_WHITE;
        default:                return TXT_LED_RAINBOW;
        }
    case 4:
        switch (Led_GetEffect()) {
        case LED_FX_BREATH: return TXT_LED_BREATH;
        case LED_FX_FLOW:   return TXT_LED_FLOW;
        default:            return TXT_LED_STEADY;
        }
    default:
        switch (Led_GetFollow()) {
        case LED_FOLLOW_DOT:      return TXT_FOLLOW_DOT;
        case LED_FOLLOW_PROGRESS: return TXT_FOLLOW_PRG;
        case LED_FOLLOW_TAIL:     return TXT_FOLLOW_TAIL;
        case LED_FOLLOW_FAR_COLOR:return TXT_FOLLOW_FAR;
        default:                  return TXT_FOLLOW_OFF;
        }
    }
}

/* 各行的值颜色：
 *   开关模式 自动=绿（跟随告警）/ 一直打开=黄（人为常开）/ 关闭=灰
 *   灯开关   开=绿 / 关=灰
 *   灯颜色   自动=绿，彩虹=紫代表多彩，其余按实际灯色显示
 *   跟随     关=灰，四种跟随效果=绿 */
static uint16_t act_value_color(uint8_t i)
{
    if (i == 0 || i == 1) {
        ActuatorMode m = (i == 0) ? Actuator_GetBuzzerMode() : Actuator_GetRelayMode();
        switch (m) {
        case ACT_MODE_AUTO: return LCD_GREEN;
        case ACT_MODE_ON:   return LCD_YELLOW;
        default:            return LCD_GRAY;
        }
    }
    if (i == 2) {
        return Led_GetOn() ? LCD_GREEN : LCD_GRAY;
    }
    if (i == 3) {
        switch (Led_GetColor()) {
        case LED_COLOR_AUTO:   return LCD_GREEN;
        case LED_COLOR_RED:    return LCD_RED;
        case LED_COLOR_ORANGE: return 0xFC00;        /* 橙 */
        case LED_COLOR_YELLOW: return LCD_YELLOW;
        case LED_COLOR_GREEN:  return LCD_GREEN;
        case LED_COLOR_CYAN:   return LCD_CYAN;
        case LED_COLOR_BLUE:   return LCD_BLUE;
        case LED_COLOR_PURPLE: return LCD_MAGENTA;
        case LED_COLOR_WHITE:  return LCD_WHITE;
        default:               return LCD_MAGENTA;   /* 彩虹：用紫代表多彩 */
        }
    }
    if (i == 4) {
        return (Led_GetEffect() == LED_FX_STEADY) ? LCD_GRAY : LCD_GREEN;
    }
    return (Led_GetFollow() == LED_FOLLOW_OFF) ? LCD_GRAY : LCD_GREEN;
}

static void draw_actuator(void)
{
    ST7735_Clear(LCD_BLACK);
    draw_title(TXT_ACTUATOR);

    for (int i = 0; i < ACT_ROW_COUNT; i++) {
        int y = CONTENT_Y + i * ROW_H;
        int sel = (i == g_act_cursor);
        uint16_t bg = sel ? LCD_BLUE : LCD_BLACK;
        if (sel) {
            ST7735_FillRect(0, y, LCD_WIDTH, ROW_H, LCD_BLUE);
        }
        draw_name(y, ACT_NAMES[i], LCD_WHITE, bg);
        /* 值文字从 x=64 起（行名都是 2~3 字，留足空隙） */
        ST7735_DrawCnStr(64, y, act_value_str(i), act_value_color(i), bg);
    }
    /* ⚠️ 本页**不画时间行**：6 行内容占 y24~119，而时间行在 y112，会重叠 8px。
     * 屏只有 128px 高，装不下两者，故执行器页舍弃时间（其他页面照常显示）。 */
}

/* ============ 页面切换 ============ */
static void enter_item(uint8_t idx)
{
    g_sub_page = 0;
    switch (idx) {
        case 0: g_page = PAGE_REALTIME;  draw_realtime();  break;
        case 1: g_page = PAGE_THRESHOLD; draw_threshold(); break;
        case 2: g_page = PAGE_ALARM;     draw_alarm();     break;
        case 3: g_page = PAGE_NET;       draw_net();       break;
        case 4: g_page = PAGE_SYSINFO;   draw_sysinfo();   break;
        case 5: g_page = PAGE_ACTUATOR;  draw_actuator();  break;
        default: break;
    }
    g_last_refresh = HAL_GetTick();
}

static void back_to_main(void)
{
    g_page = PAGE_MAIN;
    /* 保留离开时的光标位置：从哪一项进去的，返回就停在哪一项。
     * 原来这里是 g_cursor = 0，每次返回都跳回第一项 —— 想重复进入同一项
     * （比如反复看执行器）就得从头推一遍，很别扭。 */
    draw_main_menu();
}

/* ============ 对外接口 ============ */
void Menu_Init(void)
{
    Key_Init();
    draw_main_menu();
}

void Menu_Process(const MenuCtx *ctx)
{
    g_ctx = ctx;
    KeyEvent k = Key_Scan();

    switch (g_page) {
    case PAGE_MAIN:
        if (k == KEY_UP) {
            if (g_cursor > 0) { move_cursor(g_cursor, g_cursor - 1); }
        } else if (k == KEY_DOWN) {
            if (g_cursor < MAIN_ITEM_COUNT - 1) { move_cursor(g_cursor, g_cursor + 1); }
        } else if (k == KEY_OK) {
            slide_finish();              /* 进页面前先落位，避免残影带过去 */
            enter_item(g_cursor);
        } else {
            slide_tick();                /* 无按键时推进滑动动画 */
        }
        break;

    case PAGE_REALTIME:
        if ((k == KEY_UP || k == KEY_DOWN) && ENABLED_COUNT > PER_PAGE) {
            /* 翻页：必须按总页数取模。本板 11 项 ÷ 每页 5 = 3 页，
             * 原来写 `^= 1` 只能在 0/1 之间硬翻转，永远到不了第 3 页。 */
            g_sub_page = (uint8_t)((g_sub_page + 1) % PAGE_COUNT);
            draw_realtime_rows();       /* 只换内容区，不整屏清（防闪） */
        } else if (k == KEY_BACK) {
            back_to_main();
        } else if (HAL_GetTick() - g_last_refresh >= REFRESH_MS) {
            g_last_refresh = HAL_GetTick();
            draw_realtime_values();     /* 定时局部刷新数值 */
            update_time_if_needed();
        }
        break;

    case PAGE_THRESHOLD:
        if (g_th_edit) {
            /* 编辑态：上下调值，右推保存，左推取消。
             * 调值只影响光标那一行 → 只重画该行（不整屏重绘，避免闪屏）。 */
            SensorType t = ENABLED_SENSORS[g_th_cursor];
            int row = g_th_cursor - g_sub_page * PER_PAGE;
            if (k == KEY_OK) {
                /* ⚠️ 顺序很重要：**先**落盘 + 刷新界面，**最后**才发网络。
                 * DataReport_PublishThreshold → MQTT_Publish → LTE_Send 是
                 * 阻塞式串口操作（at_command 3s + 串口发送 3s + 读确认 2s，
                 * 最坏阻塞 8 秒）。若放在前面，用户按完保存会以为"卡死了"。
                 * 先给界面反馈，再去做慢的网络发送。 */
                EdgeAlarm_SaveFlash();             /* 落盘，断电不丢 */
                g_th_edit = 0;                     /* 退出编辑：数值由黄转绿 */
                g_slide.active = 0;
                g_slide.bar_y  = CONTENT_Y + row * ROW_H;
                draw_th_row_split(row, g_th_cursor, t);   /* 先刷新显示 */
                /* 再同步到服务器（让后端报警判定用新阈值）——这一步可能慢 */
                DataReport_PublishThreshold(SENSOR_TYPE_STR[t],
                                            EdgeAlarm_GetThreshold(t));
                break;                             /* 本轮已处理完，跳过下面统一重绘 */
            } else if (k == KEY_BACK) {
                EdgeAlarm_SetThreshold(t, g_th_old);   /* 取消恢复旧值 */
                g_th_edit = 0;
            } else if (k == KEY_UP) {
                EdgeAlarm_SetThreshold(t, EdgeAlarm_GetThreshold(t) + th_step(t));
            } else if (k == KEY_DOWN) {
                float v = EdgeAlarm_GetThreshold(t) - th_step(t);
                if (v < 0.0f) v = 0.0f;
                EdgeAlarm_SetThreshold(t, v);
            }
            if (k == KEY_BACK || k == KEY_UP || k == KEY_DOWN) {
                /* 用滑动渲染路径画这一行（分界参数会自动退化成整行同色） */
                g_slide.active = 0;
                g_slide.bar_y  = CONTENT_Y + row * ROW_H;
                draw_th_row_split(row, g_th_cursor, t);
            }
        } else {
            /* 选择态：上下移光标（自动翻页），右推进编辑，左推返回 */
            if (k == KEY_UP) {
                if (g_th_cursor > 0) {
                    int old = g_th_cursor;
                    g_th_cursor--;
                    if (g_th_cursor < g_sub_page * PER_PAGE) {
                        g_sub_page--;              /* 翻页：整页都要换 */
                        draw_threshold();
                    } else {
                        slide_start(old - g_sub_page * PER_PAGE,
                                    g_th_cursor - g_sub_page * PER_PAGE);
                    }
                }
            } else if (k == KEY_DOWN) {
                if (g_th_cursor < ENABLED_COUNT - 1) {
                    int old = g_th_cursor;
                    g_th_cursor++;
                    if (g_th_cursor >= (g_sub_page + 1) * PER_PAGE) {
                        g_sub_page++;              /* 翻页 */
                        draw_threshold();
                    } else {
                        slide_start(old - g_sub_page * PER_PAGE,
                                    g_th_cursor - g_sub_page * PER_PAGE);
                    }
                }
            } else if (k == KEY_OK) {
                g_th_old = EdgeAlarm_GetThreshold(ENABLED_SENSORS[g_th_cursor]);
                g_th_edit = 1;
                g_slide.active = 0;
                g_slide.bar_y  = CONTENT_Y
                               + (g_th_cursor - g_sub_page * PER_PAGE) * ROW_H;
                draw_th_row_split(g_th_cursor - g_sub_page * PER_PAGE,
                                  g_th_cursor, ENABLED_SENSORS[g_th_cursor]);
            } else if (k == KEY_BACK) {
                back_to_main();
            } else {
                slide_tick();
                update_time_if_needed();
            }
        }
        break;

    case PAGE_ACTUATOR:
        if (k == KEY_UP) {
            if (g_act_cursor > 0) {
                int old = g_act_cursor;
                g_act_cursor--;
                slide_start(old, g_act_cursor);       /* 滑动动画 */
            }
        } else if (k == KEY_DOWN) {
            if (g_act_cursor < ACT_ROW_COUNT - 1) {
                int old = g_act_cursor;
                g_act_cursor++;
                slide_start(old, g_act_cursor);
            }
        } else if (k == KEY_OK) {
            /* 右推=切换本行的值：开关模式循环 自动→一直打开→关闭，
             * 灯开关在 开/关 之间切换，灯颜色循环 自动→红→…→白→彩虹，
             * 灯效循环 常亮→呼吸→流水，跟随循环 关→单点跟随→进度条→拖尾→距离变色。
             * 只影响光标那一行的文字 → 只重画该行，不整屏重绘。 */
            switch (g_act_cursor) {
            case 0:  Actuator_CycleBuzzerMode(); break;
            case 1:  Actuator_CycleRelayMode();  break;
            case 2:  Led_Toggle();               break;
            case 3:  Led_CycleColor();           break;
            case 4:  Led_CycleEffect();          break;
            default: Led_CycleFollow();          break;
            }
            g_slide.active = 0;
            g_slide.bar_y  = CONTENT_Y + g_act_cursor * ROW_H;
            draw_act_row_split(g_act_cursor, ACT_NAMES[g_act_cursor]);
        } else if (k == KEY_BACK) {
            back_to_main();
        } else {
            slide_tick();
            /* ⚠️ 本页不调 update_time_if_needed()：执行器页 6 行内容占 y24~119，
             * 时间行在 y112，画上去会压在第 6 行上。 */
        }
        break;

    case PAGE_ALARM:
        /* 告警级别随采集实时变化，必须定时刷新，否则进页面时无告警就永远显示无告警。
         * 只局部重画级别文字区（不整页重绘，避免闪烁） */
        if (k == KEY_BACK) {
            back_to_main();
        } else if (HAL_GetTick() - g_last_refresh >= REFRESH_MS) {
            g_last_refresh = HAL_GetTick();
            draw_alarm_values();
            update_time_if_needed();
        }
        break;

    case PAGE_NET:
    case PAGE_SYSINFO:
    default:
        if (k == KEY_BACK) {
            back_to_main();
        } else {
            update_time_if_needed();
        }
        break;
    }
}
