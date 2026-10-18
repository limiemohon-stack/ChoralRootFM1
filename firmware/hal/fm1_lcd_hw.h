/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* FM-1 LCD wiring: SPI1 on PC9 CLK, PC10 DO, PC7 CS,
 * PC8 D/C; PA2, active low: the backlight, and more. The panel protocol is src/lcd.c.
 *
 * PA2 is not the backlight alone. Low = the backlight AND the key matrix / 595 chain alive (the keys, the buttons,
 * the knobs, the LEDs: hal/fm1_input.h); high = both dead (very likely one enable of the supply that feeds the
 * panel, the 595s and the LEDs). Seen on the unit 2026-10-10, twice over: the screen off of 0.14 / 0.15 drove PA2
 * high for the backlight and the unit went deaf (no key sounded or woke the screen; a SysEx from the computer
 * brought the screen back and the keys with it); and before fm1_lcd_hw_init has pulled PA2 low (the boot stub) a
 * held key does not read through the 595 chain, which the same read does after it. So PA2 is driven low once,
 * here, and nothing raises it again while the firmware runs: there is no backlight switch (the screen "off" is a
 * black frame with the backlight on, src/cr_ui.c cr_ui_draw). No other writer of PA's OUT / DIR touches bit 2:
 * hal/fm1_input.h sets and clears its own bits (1, 3, 4, 9, 10) with read-modify-writes.
 *
 *   fm1_lcd_hw_init()          pins, PA2 low (for good: see above), SPI1 master at BAUD 4
 *   fm1_lcd_baud(b)            SPI1 clock = lsb / (b + 1)
 *   fm1_lcd_send_cmd(c)        D/C low, CS low, one byte (then fm1_lcd_wait)
 *   fm1_lcd_send_data(p, n)    D/C high, CS low, DMA n bytes from RAM p
 *   fm1_lcd_wait()             SPI done (or timeout, counted), pending cleared
 *   fm1_lcd_deselect()         CS high
 *
 * SMK-37 (BOARD_SMK37): SPI1 group A (IOMAP_CON1 bit4 = 0): PB6 CLK, PB7 DO; PC8 CS, PB5 D/C; no backlight control
 * (always on), and PA2 is a key-matrix row there: never touched. Group B (PC10) carries the 16 RGB pad LEDs
 * (WS2812 class) instead: fm1_padled_send() switches SPI1 over for one frame while the LCD CS is high, as the stock
 * firmware does (stock v016; AC79 SDK spi.h for the pin groups).
 *   fm1_padled_send(p, n)      LCD deselected and idle: SPI1 to PC10 at BAUD 8, DMA n bytes
 *                              (then fm1_lcd_wait, then fm1_padled_done)
 *   fm1_padled_done()          SPI1 back to the LCD pins at the LCD baud
 */
#pragma once
#include <stdint.h>
#include "fm1_cc.h"

#define FM1_LCD_PC_OUT (*(volatile uint32_t *)0x50080u)
#define FM1_LCD_PC_DIR (*(volatile uint32_t *)0x50088u)
#define FM1_LCD_IOMAP_CON1 (*(volatile uint32_t *)0x51020u)
#define FM1_LCD_SPI_CON (*(volatile uint32_t *)0x11D00u)
#define FM1_LCD_SPI_BAUD (*(volatile uint32_t *)0x11D04u)
#define FM1_LCD_SPI_BUF (*(volatile uint32_t *)0x11D08u)
#define FM1_LCD_SPI_ADR (*(volatile uint32_t *)0x11D0Cu)
#define FM1_LCD_SPI_CNT (*(volatile uint32_t *)0x11D10u)
#if BOARD_SMK37
#define FM1_LCD_PB_OUT (*(volatile uint32_t *)0x50040u)
#define FM1_LCD_PB_DIR (*(volatile uint32_t *)0x50048u)
#define FM1_LCD_GROUP_B 0x10u             /* IOMAP_CON1 bit4: SPI1 on PC8/PC9/PC10 */
#define FM1_LCD_CS (1u << 8)              /* PC8 */
#define FM1_LCD_DC (1u << 5)              /* PB5 */
#define FM1_LCD_CLK (1u << 6)             /* PB6 */
#define FM1_LCD_DO (1u << 7)              /* PB7 */
#define FM1_PADLED_DO (1u << 10)          /* PC10 */
#define FM1_PADLED_BAUD 8u                /* lsb / 9 = 6.67 MHz: one SPI byte per LED bit */

static uint32_t fm1_lcd_timeouts;
static uint8_t fm1_lcd_cur_baud = 4u;             /* (fm1_padled_done restores it) */

FM1_INLINE void fm1_lcd_hw_init(void)
{
    FM1_LCD_IOMAP_CON1 &= ~FM1_LCD_GROUP_B;
    FM1_LCD_PC_OUT |= FM1_LCD_CS;
    FM1_LCD_PC_DIR &= ~FM1_LCD_CS;
    FM1_LCD_PB_OUT &= ~(FM1_LCD_DC | FM1_LCD_CLK | FM1_LCD_DO);
    FM1_LCD_PB_DIR &= ~(FM1_LCD_DC | FM1_LCD_CLK | FM1_LCD_DO);
    FM1_LCD_PC_OUT &= ~FM1_PADLED_DO;     /* the LED line idles low (latch) */
    FM1_LCD_PC_DIR &= ~FM1_PADLED_DO;
    FM1_LCD_SPI_CON = 0x4021u;
    FM1_LCD_SPI_BAUD = 4u;
    fm1_lcd_cur_baud = 4u;
}

FM1_INLINE void fm1_lcd_baud(uint32_t b)
{
    FM1_LCD_SPI_BAUD = b;
    fm1_lcd_cur_baud = (uint8_t)b;
}

static void fm1_lcd_wait(void)
{
    uint32_t n;
    for (n = 0; n < 4000000u && !(FM1_LCD_SPI_CON & 0x8000u); n++)
        ;
    if (n == 4000000u)
        fm1_lcd_timeouts++;
    FM1_LCD_SPI_CON |= 0x4000u;
}

FM1_INLINE void fm1_lcd_deselect(void) { FM1_LCD_PC_OUT |= FM1_LCD_CS; }

FM1_INLINE void fm1_lcd_send_cmd(uint8_t c)
{
    FM1_LCD_PB_OUT &= ~FM1_LCD_DC;
    FM1_LCD_PC_OUT &= ~FM1_LCD_CS;
    FM1_LCD_SPI_CON |= 0x4000u;
    FM1_LCD_SPI_BUF = c;
}

FM1_INLINE void fm1_lcd_send_data(const void *p, uint32_t n)
{
    FM1_LCD_PB_OUT |= FM1_LCD_DC;
    FM1_LCD_PC_OUT &= ~FM1_LCD_CS;
    FM1_LCD_SPI_CON |= 0x4000u;
    FM1_LCD_SPI_ADR = (uint32_t)(uintptr_t)p;
    FM1_LCD_SPI_CNT = n;
}

FM1_INLINE void fm1_padled_send(const void *p, uint32_t n)
{
    FM1_LCD_PC_OUT |= FM1_LCD_CS;         /* the panel ignores the bus */
    FM1_LCD_SPI_BAUD = FM1_PADLED_BAUD;
    FM1_LCD_IOMAP_CON1 |= FM1_LCD_GROUP_B;
    FM1_LCD_SPI_CON |= 0x4000u;
    FM1_LCD_SPI_ADR = (uint32_t)(uintptr_t)p;
    FM1_LCD_SPI_CNT = n;
}

FM1_INLINE void fm1_padled_done(void)
{
    FM1_LCD_IOMAP_CON1 &= ~FM1_LCD_GROUP_B;
    FM1_LCD_SPI_BAUD = fm1_lcd_cur_baud;
}
#else /* FM-1 */
#define FM1_LCD_PA_OUT (*(volatile uint32_t *)0x50000u)
#define FM1_LCD_PA_DIR (*(volatile uint32_t *)0x50008u)
#define FM1_LCD_PA2 (1u << 2)      /* low: the backlight and the key matrix alive (above). Never set it */
#define FM1_LCD_CS (1u << 7)
#define FM1_LCD_DC (1u << 8)
#define FM1_LCD_CLK (1u << 9)
#define FM1_LCD_DO (1u << 10)

static uint32_t fm1_lcd_timeouts;

FM1_INLINE void fm1_lcd_hw_init(void)
{
    FM1_LCD_PA_OUT &= ~FM1_LCD_PA2;                 /* PA2 low, an output: the only write of it there is */
    FM1_LCD_PA_DIR &= ~FM1_LCD_PA2;
    FM1_LCD_IOMAP_CON1 |= 0x10u;
    FM1_LCD_PC_OUT |= FM1_LCD_CS;
    FM1_LCD_PC_OUT &= ~(FM1_LCD_DC | FM1_LCD_CLK | FM1_LCD_DO);
    FM1_LCD_PC_DIR &= ~(FM1_LCD_CS | FM1_LCD_DC | FM1_LCD_CLK | FM1_LCD_DO);
    FM1_LCD_SPI_CON = 0x4021u;
    FM1_LCD_SPI_BAUD = 4u;
}

FM1_INLINE void fm1_lcd_baud(uint32_t b) { FM1_LCD_SPI_BAUD = b; }

static void fm1_lcd_wait(void)
{
    uint32_t n;
    for (n = 0; n < 4000000u && !(FM1_LCD_SPI_CON & 0x8000u); n++)
        ;
    if (n == 4000000u)
        fm1_lcd_timeouts++;
    FM1_LCD_SPI_CON |= 0x4000u;
}

FM1_INLINE void fm1_lcd_deselect(void) { FM1_LCD_PC_OUT |= FM1_LCD_CS; }

FM1_INLINE void fm1_lcd_send_cmd(uint8_t c)
{
    FM1_LCD_PC_OUT &= ~FM1_LCD_DC;
    FM1_LCD_PC_OUT &= ~FM1_LCD_CS;
    FM1_LCD_SPI_CON |= 0x4000u;
    FM1_LCD_SPI_BUF = c;
}

FM1_INLINE void fm1_lcd_send_data(const void *p, uint32_t n)
{
    FM1_LCD_PC_OUT |= FM1_LCD_DC;
    FM1_LCD_PC_OUT &= ~FM1_LCD_CS;
    FM1_LCD_SPI_CON |= 0x4000u;
    FM1_LCD_SPI_ADR = (uint32_t)(uintptr_t)p;
    FM1_LCD_SPI_CNT = n;
}
#endif /* BOARD_SMK37 */
