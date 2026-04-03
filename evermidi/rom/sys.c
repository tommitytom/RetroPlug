
#include "main.h"

#define JOY_PORT *((u8 *)0x4016)
#define PPU_CTRL *((u8 *)0x2000)
#define PPU_MASK *((u8 *)0x2001)
#define PPU_STAT *((u8 *)0x2002)
#define PPU_ADDR *((u8 *)0x2006)
#define PPU_DATA *((u8 *)0x2007)
#define PPU_SCROLL *((u8 *)0x2005)

void ppuSetAddr(u16 addr);

u8 g_buff[G_SCREEN_W * G_SCREEN_H];
u16 g_addr;

void sysInit() {

    u16 i;
    PPU_CTRL = 0;
    PPU_MASK = 0;

    //init pal
    ppuSetAddr(0x3F00);
    PPU_DATA = 0x0D; //bg color
    PPU_DATA = 0x30; //bg text color

    ppuSetAddr(0x2000);
    for (i = 0; i < 2048; i++) {
        PPU_DATA = 0;
    }

    ppuOn();
}

u8 sysJoyRead() {

    u8 joy = 0;
    u8 i;

    JOY_PORT = 0x01;
    JOY_PORT = 0x00;

    for (i = 0; i < 8; i++) {
        joy <<= 1;
        if (JOY_PORT & 3)joy |= 1;
    }

    return joy;
}

void ppuSetAddr(u16 addr) {
    PPU_ADDR = addr >> 8;
    PPU_ADDR = addr & 0xff;
}

void ppuOff() {
    gVsync();
    PPU_MASK = 0;
    //PPU_CTRL = 0;
}

void ppuOn() {
    gVsync();
    //PPU_CTRL = 0x80;
    PPU_MASK = 0x0A;
}

void gVsync() {

    volatile u8 tmp = PPU_STAT;
    while (PPU_STAT < 128);
}

void gSetScroll(u8 x, u8 y) {

    PPU_ADDR = 0;
    PPU_SCROLL = y;
    PPU_SCROLL = x;
}

void gSetXY(u8 x, u8 y) {

    g_addr = x + y * G_SCREEN_W;
}

void gSetX(u8 x) {

    g_addr = g_addr / G_SCREEN_W * G_SCREEN_W;
    g_addr += x;
}

void gClearScreen() {

    u16 i;
    g_addr = 0;

    for (i = 0; i < sizeof (g_buff); i++) {
        g_buff[i] = ' ';
    }
}

void gRepaint() {

    u16 i;
    u8 *src = g_buff;

    //gVsync();
    ppuOff();
    ppuSetAddr(0x2000);

    for (i = 0; i < sizeof (g_buff); i++) {
        PPU_DATA = *src++;
    }

    ppuOn();
    gSetScroll(0, 0);
}

void gAppendString(u8 *str) {

    while (*str != 0) {
        g_buff[g_addr++] = *str++;
    }
}

void gAppendString_ML(u8 *str, u8 max_len) {

    while (*str != 0 && max_len--) {
        g_buff[g_addr++] = *str++;
    }
}

void gAppendChar(u8 val) {

    g_buff[g_addr++] = val;

}

void gAppendHex4(u8 val) {

    val += (val < 10 ? '0' : '7');
    gAppendChar(val);
}

void gAppendHex8(u8 val) {

    gAppendHex4(val >> 4);
    gAppendHex4(val & 15);
}

void gAppendHex16(u16 val) {

    gAppendHex8(val >> 8);
    gAppendHex8(val & 0xff);
}

void gAppendHex32(u32 val) {

    gAppendHex16(val >> 16);
    gAppendHex16(val & 0xffff);
}

void gAppendNum(u32 num) {

    u16 i;
    u8 buff[11];
    u8 *str = (u8 *) & buff[10];


    *str = 0;
    if (num == 0)*--str = '0';
    for (i = 0; num != 0; i++) {

        *--str = num % 10 + '0';
        num /= 10;
    }

    gAppendString(str);
}

void gAppendHex(void *src, u16 len) {

    u8 *src8 = (u8 *) src;

    u16 i;
    for (i = 0; i < len; i++) {
        gAppendHex8(*src8++);
    }
}

void gConsPrint(u8 *str) {

    g_addr = g_addr / G_SCREEN_W * G_SCREEN_W + G_SCREEN_W;
    gAppendString(str);
}

void gConsPrintCX(u8 *str) {

    u8 maxlen = G_SCREEN_W;

    u8 str_len = 0;
    while (str[str_len])str_len++;
    if (str_len > maxlen)str_len = maxlen;
    gSetX((G_SCREEN_W - str_len) / 2);
    gAppendString(str);
}

