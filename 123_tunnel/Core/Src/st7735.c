/**
 * @file    st7735.c
 * @brief   ST7735 IPS LCD 驱动实现（软件 SPI）
 *
 * 软件 SPI：GPIO 翻转模拟 SCK/MOSI 时序，写命令/数据。
 * 颜色 RGB565（16 位，先高字节后低字节）。
 */
#include "st7735.h"
#include "st7735_font.h"
#include "st7735_font_cn.h"

/* ============ GPIO 初始化 ============ */
static void ST7735_GPIO_Init(void)
{
    __HAL_RCC_GPIOB_CLK_ENABLE();
    GPIO_InitTypeDef gpio = {0};
    gpio.Pin   = LCD_SCK_PIN | LCD_MOSI_PIN | LCD_DC_PIN | LCD_RES_PIN
               | LCD_CS_PIN | LCD_BLK_PIN;
    gpio.Mode  = GPIO_MODE_OUTPUT_PP;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(LCD_PORT, &gpio);

    /* CS 拉低常使能，BLK 拉高背光亮 */
    HAL_GPIO_WritePin(LCD_PORT, LCD_CS_PIN, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(LCD_PORT, LCD_BLK_PIN, GPIO_PIN_SET);
}

/* ============ GPIO 快速写入（BSRR 直写） ============
 * 原先逐位调用 HAL_GPIO_WritePin：全屏 128x128 要 16384 像素 x 2 字节 x 8 位
 * x 3 次调用 ≈ 78 万次函数调用，一次全屏刷新约 190ms，动画/过渡根本跑不动。
 * 改为直接写 BSRR：一条 store 指令代替一次函数调用，全屏刷新降到约 30ms。
 * BSRR 低 16 位置位、高 16 位复位，写操作原子，无需读改写，比 HAL 更安全。
 * 前提：SCK/MOSI/DC 必须同属 LCD_PORT（当前均为 GPIOB），才能共用同一 BSRR。 */
#define LCD_SET(pin)    (LCD_PORT->BSRR = (uint32_t)(pin))
#define LCD_CLR(pin)    (LCD_PORT->BSRR = (uint32_t)(pin) << 16)
#define LCD_DC_CMD()    LCD_CLR(LCD_DC_PIN)
#define LCD_DC_DATA()   LCD_SET(LCD_DC_PIN)

/* ============ 软件 SPI ============ */
static void spi_write_byte(uint8_t b)
{
    for (int i = 0; i < 8; i++) {
        LCD_CLR(LCD_SCK_PIN);                       /* SCK 拉低 */
        if (b & 0x80) { LCD_SET(LCD_MOSI_PIN); }    /* 先建立 MOSI 电平 */
        else          { LCD_CLR(LCD_MOSI_PIN); }
        b <<= 1;
        LCD_SET(LCD_SCK_PIN);                       /* SCK 拉高，从机在此采样 */
    }
}

/* ============ 写命令 / 写数据 ============ */
static void ST7735_Cmd(uint8_t cmd)
{
    LCD_DC_CMD();                                   /* DC=0 命令 */
    spi_write_byte(cmd);
}

static void ST7735_Data(uint8_t data)
{
    LCD_DC_DATA();                                  /* DC=1 数据 */
    spi_write_byte(data);
}

/* ============ 复位 ============ */
static void ST7735_Reset(void)
{
    HAL_GPIO_WritePin(LCD_PORT, LCD_RES_PIN, GPIO_PIN_RESET);
    HAL_Delay(20);
    HAL_GPIO_WritePin(LCD_PORT, LCD_RES_PIN, GPIO_PIN_SET);
    HAL_Delay(120);
}

/* ============ 设置地址窗口 ============ */
static void ST7735_SetWindow(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1)
{
    uint16_t xs = x0 + LCD_X_OFFSET, xe = x1 + LCD_X_OFFSET;
    uint16_t ys = y0 + LCD_Y_OFFSET, ye = y1 + LCD_Y_OFFSET;

    ST7735_Cmd(0x2A);                       /* CASET 列地址 */
    ST7735_Data(xs >> 8); ST7735_Data(xs & 0xFF);
    ST7735_Data(xe >> 8); ST7735_Data(xe & 0xFF);

    ST7735_Cmd(0x2B);                       /* RASET 行地址 */
    ST7735_Data(ys >> 8); ST7735_Data(ys & 0xFF);
    ST7735_Data(ye >> 8); ST7735_Data(ye & 0xFF);

    ST7735_Cmd(0x2C);                       /* RAMWR 写显存 */
}

/* ============ 填充矩形 ============ */
void ST7735_FillRect(int x, int y, int w, int h, uint16_t color)
{
    ST7735_SetWindow(x, y, x + w - 1, y + h - 1);
    uint8_t hi = (uint8_t)(color >> 8), lo = (uint8_t)(color & 0xFF);
    uint32_t n = (uint32_t)w * h;

    LCD_DC_DATA();                      /* DC 整块只设一次（原来每字节设一次） */
    for (uint32_t i = 0; i < n; i++) {
        spi_write_byte(hi);
        spi_write_byte(lo);
    }
}

/* ============ 初始化 ============ */
void ST7735_Init(void)
{
    ST7735_GPIO_Init();
    ST7735_Reset();

    /* 初始化命令序列（ST7735 128x160，竖屏） */
    ST7735_Cmd(0x01);                       /* SWRESET 软复位 */
    HAL_Delay(120);
    ST7735_Cmd(0x11);                       /* SLPOUT 退出睡眠 */
    HAL_Delay(120);

    ST7735_Cmd(0xB1);                       /* FRMCTR1 帧率 */
    ST7735_Data(0x01); ST7735_Data(0x2C); ST7735_Data(0x2D);
    ST7735_Cmd(0xB2);                       /* FRMCTR2 */
    ST7735_Data(0x01); ST7735_Data(0x2C); ST7735_Data(0x2D);
    ST7735_Cmd(0xB3);                       /* FRMCTR3 */
    ST7735_Data(0x01); ST7735_Data(0x2C); ST7735_Data(0x2D);
    ST7735_Data(0x01); ST7735_Data(0x2C); ST7735_Data(0x2D);
    ST7735_Cmd(0xB4);                       /* INVCTR 反相控制 */
    ST7735_Data(0x07);
    ST7735_Cmd(0xC0);                       /* PWCTR1 电源控制 */
    ST7735_Data(0xA2); ST7735_Data(0x02); ST7735_Data(0x84);
    ST7735_Cmd(0xC1);                       /* PWCTR2 */
    ST7735_Data(0xC5);
    ST7735_Cmd(0xC2);                       /* PWCTR3 */
    ST7735_Data(0x0A); ST7735_Data(0x00);
    ST7735_Cmd(0xC3);                       /* PWCTR4 */
    ST7735_Data(0x8A); ST7735_Data(0x2A);
    ST7735_Cmd(0xC4);                       /* PWCTR5 */
    ST7735_Data(0x8A); ST7735_Data(0xEE);
    ST7735_Cmd(0xC5);                       /* VMCTR1 VCOM */
    ST7735_Data(0x0E);
    ST7735_Cmd(0x20);                       /* INVOFF 不反相 */
    ST7735_Cmd(0x36);                       /* MADCTL 方向 */
    ST7735_Data(0xC8);                      /* 竖屏 */
    ST7735_Cmd(0x3A);                       /* COLMOD 颜色格式 */
    ST7735_Data(0x05);                      /* 16 位 RGB565 */
    ST7735_Cmd(0x13);                       /* NORON 正常显示 */
    ST7735_Cmd(0x29);                       /* DISPON 显示开 */
    HAL_Delay(100);

    ST7735_Clear(LCD_BLACK);
}

/* ============ 清屏 ============ */
void ST7735_Clear(uint16_t color)
{
    ST7735_FillRect(0, 0, LCD_WIDTH, LCD_HEIGHT, color);
}

/* ============ 显示字符（8x16，带背景色） ============ */
void ST7735_DrawChar(int x, int y, char c, uint16_t color, uint16_t bg)
{
    if (c < 0x20 || c > 0x7E) {
        c = ' ';                            /* 非 ASCII 用空格替代 */
    }
    const unsigned char *glyph = FONT8x8[c - 0x20];
    /* 预先拆好高低字节，避免逐像素做移位/掩码 */
    uint8_t ch = (uint8_t)(color >> 8), cl = (uint8_t)color;
    uint8_t bh = (uint8_t)(bg >> 8),    bl = (uint8_t)bg;

    ST7735_SetWindow(x, y, x + 7, y + 7);
    LCD_DC_DATA();
    for (int row = 0; row < 8; row++) {
        unsigned char bits = glyph[row];
        for (int col = 0; col < 8; col++) {
            if (bits & (0x80 >> col)) { spi_write_byte(ch); spi_write_byte(cl); }
            else                      { spi_write_byte(bh); spi_write_byte(bl); }
        }
    }
}

/* ============ 显示字符串 ============ */
void ST7735_DrawString(int x, int y, const char *str, uint16_t color, uint16_t bg)
{
    int cx = x, cy = y;
    while (*str) {
        if (*str == '\n') {
            cx = x;
            cy += 8;
            str++;
            continue;
        }
        if (cx + 8 > LCD_WIDTH) {           /* 超宽自动换行 */
            cx = x;
            cy += 8;
        }
        if (cy + 8 > LCD_HEIGHT) {
            break;                          /* 超出屏幕停止 */
        }
        ST7735_DrawChar(cx, cy, *str, color, bg);
        cx += 8;
        str++;
    }
}

/* ============ 显示单个中文（GBK 码，16x16） ============ */
static void ST7735_DrawCnChar(int x, int y, uint8_t hi, uint8_t lo,
                              uint16_t color, uint16_t bg)
{
    const CnGlyph16 *g = NULL;
    for (int i = 0; i < CN_FONT16_COUNT; i++) {
        if (CN_FONT16[i].idx[0] == hi && CN_FONT16[i].idx[1] == lo) {
            g = &CN_FONT16[i];
            break;
        }
    }
    if (g == NULL) {                        /* 字库缺失：画背景色方块占位 */
        ST7735_FillRect(x, y, 16, 16, bg);
        return;
    }

    uint8_t ch = (uint8_t)(color >> 8), cl = (uint8_t)color;
    uint8_t bh = (uint8_t)(bg >> 8),    bl = (uint8_t)bg;

    ST7735_SetWindow(x, y, x + 15, y + 15);
    LCD_DC_DATA();
    for (int i = 0; i < 32; i++) {          /* 16 行 x 2 字节 */
        uint8_t bits = g->mask[i];
        for (int j = 0; j < 8; j++) {
            if (bits & (0x80 >> j)) { spi_write_byte(ch); spi_write_byte(cl); }
            else                    { spi_write_byte(bh); spi_write_byte(bl); }
        }
    }
}

/* ============ 显示中文 GBK 字符串（每 2 字节一个汉字） ============ */
void ST7735_DrawCnStr(int x, int y, const char *gbk, uint16_t color, uint16_t bg)
{
    while (*gbk) {
        ST7735_DrawCnChar(x, y, (uint8_t)gbk[0], (uint8_t)gbk[1], color, bg);
        x += 16;
        gbk += 2;
    }
}

/* ============ 分界底色绘制 ============ */

/* 汉字（16x16），按 split_y 分界底色 */
static void draw_cn_char_split(int x, int y, uint8_t hi, uint8_t lo,
                               uint16_t color, int split_y,
                               uint16_t bg_above, uint16_t bg_below)
{
    const CnGlyph16 *g = NULL;
    for (int i = 0; i < CN_FONT16_COUNT; i++) {
        if (CN_FONT16[i].idx[0] == hi && CN_FONT16[i].idx[1] == lo) {
            g = &CN_FONT16[i];
            break;
        }
    }
    if (g == NULL) {                        /* 缺字：用分界底色填满占位 */
        ST7735_FillRect(x, y, 16, 16, (y >= split_y) ? bg_below : bg_above);
        return;
    }

    uint8_t ch = (uint8_t)(color >> 8), cl = (uint8_t)color;
    uint8_t ah = (uint8_t)(bg_above >> 8), al = (uint8_t)bg_above;
    uint8_t bh = (uint8_t)(bg_below >> 8), bl = (uint8_t)bg_below;

    /* 整个字都在分界线上方或下方时，底色单一，走快路径 */
    int uniform = (y + 16 <= split_y) ? 1 : ((y >= split_y) ? 2 : 0);

    ST7735_SetWindow(x, y, x + 15, y + 15);
    LCD_DC_DATA();
    for (int r = 0; r < 16; r++) {          /* 16 行，每行 2 字节 = 16 列 */
        uint8_t b0 = g->mask[r * 2];
        uint8_t b1 = g->mask[r * 2 + 1];
        uint8_t dh, dl;
        if (uniform == 1)      { dh = ah; dl = al; }
        else if (uniform == 2) { dh = bh; dl = bl; }
        else if (y + r < split_y) { dh = ah; dl = al; }
        else                   { dh = bh; dl = bl; }

        for (int j = 0; j < 8; j++) {
            if (b0 & (0x80 >> j)) { spi_write_byte(ch); spi_write_byte(cl); }
            else                  { spi_write_byte(dh); spi_write_byte(dl); }
        }
        for (int j = 0; j < 8; j++) {
            if (b1 & (0x80 >> j)) { spi_write_byte(ch); spi_write_byte(cl); }
            else                  { spi_write_byte(dh); spi_write_byte(dl); }
        }
    }
}

void ST7735_DrawCnStrSplit(int x, int y, const char *gbk, uint16_t color,
                           int split_y, uint16_t bg_above, uint16_t bg_below)
{
    while (*gbk) {
        draw_cn_char_split(x, y, (uint8_t)gbk[0], (uint8_t)gbk[1],
                           color, split_y, bg_above, bg_below);
        x += 16;
        gbk += 2;
    }
}

/* ASCII（8x8 字形，画在 y+4；上下各 4px 补底色撑满 16px 行高） */
static void draw_char_split(int x, int y, char c, uint16_t color,
                            int split_y, uint16_t bg_above, uint16_t bg_below)
{
    if (c < 0x20 || c > 0x7E) { c = ' '; }
    const unsigned char *g = FONT8x8[c - 0x20];

    uint8_t ch = (uint8_t)(color >> 8), cl = (uint8_t)color;
    uint8_t ah = (uint8_t)(bg_above >> 8), al = (uint8_t)bg_above;
    uint8_t bh = (uint8_t)(bg_below >> 8), bl = (uint8_t)bg_below;

    /* 开整行 16 高的窗口：前 4 行与后 4 行只填底色 */
    ST7735_SetWindow(x, y, x + 7, y + 15);
    LCD_DC_DATA();
    for (int r = 0; r < 16; r++) {
        int gi = r - 4;
        uint8_t bits = (gi >= 0 && gi < 8) ? g[gi] : 0;
        uint8_t dh = (y + r >= split_y) ? bh : ah;
        uint8_t dl = (y + r >= split_y) ? bl : al;
        for (int j = 0; j < 8; j++) {
            if (bits & (0x80 >> j)) { spi_write_byte(ch); spi_write_byte(cl); }
            else                    { spi_write_byte(dh); spi_write_byte(dl); }
        }
    }
}

void ST7735_DrawNameSplit(int x, int y, const char *s, uint16_t color,
                          int split_y, uint16_t bg_above, uint16_t bg_below)
{
    while (*s) {
        if ((uint8_t)*s < 0x80) {
            draw_char_split(x, y, *s, color, split_y, bg_above, bg_below);
            x += 8; s++;
        } else {
            draw_cn_char_split(x, y, (uint8_t)s[0], (uint8_t)s[1],
                               color, split_y, bg_above, bg_below);
            x += 16; s += 2;
        }
    }
}

void ST7735_DrawAsciiSplit(int x, int y, const char *s, uint16_t color,
                           int split_y, uint16_t bg_above, uint16_t bg_below)
{
    while (*s) {
        draw_char_split(x, y, *s, color, split_y, bg_above, bg_below);
        x += 8; s++;
    }
}
