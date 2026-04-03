/* 
 * File:   sys.h
 * Author: Igor
 *
 * Created on December 14, 2024, 9:12 PM
 */

#ifndef SYS_H
#define	SYS_H

#define JOY_UP          0x08
#define JOY_DOWN        0x04
#define JOY_LEFT        0x02
#define JOY_RIGHT       0x01
#define JOY_SEL         0x20
#define JOY_START       0x10
#define JOY_B           0x40
#define JOY_A           0x80

#define G_SCREEN_W      32
#define G_SCREEN_H      28

void sysInit();
u8 sysJoyRead();
void ppuOff();
void ppuOn();

void gVsync();
void gSetScroll(u8 x, u8 y);
void gSetXY(u8 x, u8 y);
void gSetX(u8 x);
void gClearScreen();
void gRepaint();
void gAppendString(u8 *str);
void gAppendString_ML(u8 *str, u8 max_len);
void gAppendChar(u8 val);
void gAppendHex4(u8 val);
void gAppendHex8(u8 val);
void gAppendHex16(u16 val);
void gAppendHex32(u32 val);
void gAppendNum(u32 num);
void gAppendHex(void *src, u16 len);
void gConsPrint(u8 *str);
void gConsPrintCX(u8 *str);

#endif	/* SYS_H */

