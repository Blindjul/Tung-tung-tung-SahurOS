/*
 * TungOS Framebuffer GUI - Real Graphical Desktop
 * Draws directly to /dev/fb0 with pixel graphics
 * Windows-like desktop with mouse, windows, taskbar
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <linux/fb.h>
#include <linux/input.h>
#include <time.h>
#include <signal.h>
#include <errno.h>
#include <termios.h>
#include <math.h>

/* Logo data */
#include "logo_splash.h"
#include "logo_icon.h"

/* ═══════════════════════════════════════════════════════════════════
 * Framebuffer globals
 * ═══════════════════════════════════════════════════════════════════ */
static int fb_fd = -1;
static struct fb_var_screeninfo vinfo;
static struct fb_fix_screeninfo finfo;
static unsigned char *fb_ptr = NULL;
static int screen_w = 0, screen_h = 0;
static int fb_bpp = 0;     /* bits per pixel */
static int fb_bytes_pp = 0; /* bytes per pixel */
static int fb_stride = 0;

/* Double buffer */
static unsigned char *back_buf = NULL;

/* Mouse state */
static int mouse_fd = -1;
static int mouse_x = 400, mouse_y = 300;
static int mouse_lbtn = 0, mouse_rbtn = 0;
static int prev_mouse_x = 400, prev_mouse_y = 300;
static int mouse_moved = 0;

/* Desktop state */
static int running = 1;
static int active_window = 0; /* 0=none, 1=terminal, 2=sysinfo, 3=power */

/* ═══════════════════════════════════════════════════════════════════
 * 8x16 Bitmap Font (PC-style, 256 chars)
 * ═══════════════════════════════════════════════════════════════════ */
/* Minimal font data - ASCII 32-126, 8 pixels wide, 16 pixels tall */
static const unsigned char font8x16[96][16] = {
    /* 32 ' ' */ {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    /* 33 '!' */ {0x00,0x00,0x18,0x18,0x00,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x00,0x00,0x18,0x18},
    /* 34 '"' */ {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    /* 35 '#' */ {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    /* 36 '$' */ {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    /* 37 '%' */ {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    /* 38 '&' */ {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    /* 39 ''' */ {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    /* 40 '(' */ {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    /* 41 ')' */ {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    /* 42 '*' */ {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    /* 43 '+' */ {0x00,0x00,0x00,0x00,0x00,0x18,0x18,0x7e,0x7e,0x18,0x18,0x00,0x00,0x00,0x00,0x00},
    /* 44 ',' */ {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x18,0x18,0x30},
    /* 45 '-' */ {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xfe,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    /* 46 '.' */ {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x18,0x18,0x00},
    /* 47 '/' */ {0x00,0x00,0x02,0x06,0x0c,0x18,0x30,0x60,0xc0,0x80,0x00,0x00,0x00,0x00,0x00,0x00},
    /* 48 '0' */ {0x00,0x00,0x38,0x6c,0xc6,0xc6,0xd6,0xd6,0xc6,0xc6,0x6c,0x38,0x00,0x00,0x00,0x00},
    /* 49 '1' */ {0x00,0x00,0x18,0x38,0x78,0x18,0x18,0x18,0x18,0x18,0x18,0x7e,0x00,0x00,0x00,0x00},
    /* 50 '2' */ {0x00,0x00,0x7c,0xc6,0x06,0x0c,0x18,0x30,0x60,0xc0,0xc6,0xfe,0x00,0x00,0x00,0x00},
    /* 51 '3' */ {0x00,0x00,0x7c,0xc6,0x06,0x06,0x3c,0x06,0x06,0x06,0xc6,0x7c,0x00,0x00,0x00,0x00},
    /* 52 '4' */ {0x00,0x00,0x0c,0x1c,0x3c,0x6c,0xcc,0xfe,0x0c,0x0c,0x0c,0x1e,0x00,0x00,0x00,0x00},
    /* 53 '5' */ {0x00,0x00,0xfe,0xc0,0xc0,0xc0,0xfc,0x06,0x06,0x06,0xc6,0x7c,0x00,0x00,0x00,0x00},
    /* 54 '6' */ {0x00,0x00,0x38,0x60,0xc0,0xc0,0xfc,0xc6,0xc6,0xc6,0xc6,0x7c,0x00,0x00,0x00,0x00},
    /* 55 '7' */ {0x00,0x00,0xfe,0xc6,0x06,0x06,0x0c,0x18,0x30,0x30,0x30,0x30,0x00,0x00,0x00,0x00},
    /* 56 '8' */ {0x00,0x00,0x7c,0xc6,0xc6,0xc6,0x7c,0xc6,0xc6,0xc6,0xc6,0x7c,0x00,0x00,0x00,0x00},
    /* 57 '9' */ {0x00,0x00,0x7c,0xc6,0xc6,0xc6,0x7e,0x06,0x06,0x06,0x0c,0x78,0x00,0x00,0x00,0x00},
    /* 58 ':' */ {0x00,0x00,0x00,0x00,0x18,0x18,0x00,0x00,0x00,0x18,0x18,0x00,0x00,0x00,0x00,0x00},
    /* 59 ';' */ {0x00,0x00,0x00,0x00,0x18,0x18,0x00,0x00,0x00,0x18,0x18,0x30,0x00,0x00,0x00,0x00},
    /* 60 '<' */ {0x00,0x00,0x00,0x06,0x0c,0x18,0x30,0x60,0x30,0x18,0x0c,0x06,0x00,0x00,0x00,0x00},
    /* 61 '=' */ {0x00,0x00,0x00,0x00,0x00,0x7e,0x00,0x00,0x7e,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    /* 62 '>' */ {0x00,0x00,0x00,0x60,0x30,0x18,0x0c,0x06,0x0c,0x18,0x30,0x60,0x00,0x00,0x00,0x00},
    /* 63 '?' */ {0x00,0x00,0x7c,0xc6,0xc6,0x0c,0x18,0x18,0x18,0x00,0x18,0x18,0x00,0x00,0x00,0x00},
    /* 64 '@' */ {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    /* 65 'A' */ {0x00,0x00,0x10,0x38,0x6c,0xc6,0xc6,0xfe,0xc6,0xc6,0xc6,0xc6,0x00,0x00,0x00,0x00},
    /* 66 'B' */ {0x00,0x00,0xfc,0x66,0x66,0x66,0x7c,0x66,0x66,0x66,0x66,0xfc,0x00,0x00,0x00,0x00},
    /* 67 'C' */ {0x00,0x00,0x3c,0x66,0xc2,0xc0,0xc0,0xc0,0xc0,0xc2,0x66,0x3c,0x00,0x00,0x00,0x00},
    /* 68 'D' */ {0x00,0x00,0xf8,0x6c,0x66,0x66,0x66,0x66,0x66,0x66,0x6c,0xf8,0x00,0x00,0x00,0x00},
    /* 69 'E' */ {0x00,0x00,0xfe,0x66,0x62,0x68,0x78,0x68,0x60,0x62,0x66,0xfe,0x00,0x00,0x00,0x00},
    /* 70 'F' */ {0x00,0x00,0xfe,0x66,0x62,0x68,0x78,0x68,0x60,0x60,0x60,0xf0,0x00,0x00,0x00,0x00},
    /* 71 'G' */ {0x00,0x00,0x3c,0x66,0xc2,0xc0,0xc0,0xde,0xc6,0xc6,0x66,0x3a,0x00,0x00,0x00,0x00},
    /* 72 'H' */ {0x00,0x00,0xc6,0xc6,0xc6,0xc6,0xfe,0xc6,0xc6,0xc6,0xc6,0xc6,0x00,0x00,0x00,0x00},
    /* 73 'I' */ {0x00,0x00,0x3c,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x3c,0x00,0x00,0x00,0x00},
    /* 74 'J' */ {0x00,0x00,0x1e,0x0c,0x0c,0x0c,0x0c,0x0c,0xcc,0xcc,0xcc,0x78,0x00,0x00,0x00,0x00},
    /* 75 'K' */ {0x00,0x00,0xe6,0x66,0x66,0x6c,0x78,0x78,0x6c,0x66,0x66,0xe6,0x00,0x00,0x00,0x00},
    /* 76 'L' */ {0x00,0x00,0xf0,0x60,0x60,0x60,0x60,0x60,0x60,0x62,0x66,0xfe,0x00,0x00,0x00,0x00},
    /* 77 'M' */ {0x00,0x00,0xc6,0xee,0xfe,0xfe,0xd6,0xc6,0xc6,0xc6,0xc6,0xc6,0x00,0x00,0x00,0x00},
    /* 78 'N' */ {0x00,0x00,0xc6,0xe6,0xf6,0xfe,0xde,0xce,0xc6,0xc6,0xc6,0xc6,0x00,0x00,0x00,0x00},
    /* 79 'O' */ {0x00,0x00,0x7c,0xc6,0xc6,0xc6,0xc6,0xc6,0xc6,0xc6,0xc6,0x7c,0x00,0x00,0x00,0x00},
    /* 80 'P' */ {0x00,0x00,0xfc,0x66,0x66,0x66,0x7c,0x60,0x60,0x60,0x60,0xf0,0x00,0x00,0x00,0x00},
    /* 81 'Q' */ {0x00,0x00,0x7c,0xc6,0xc6,0xc6,0xc6,0xc6,0xc6,0xd6,0xde,0x7c,0x06,0x00,0x00,0x00},
    /* 82 'R' */ {0x00,0x00,0xfc,0x66,0x66,0x66,0x7c,0x6c,0x66,0x66,0x66,0xe6,0x00,0x00,0x00,0x00},
    /* 83 'S' */ {0x00,0x00,0x7c,0xc6,0xc6,0x60,0x38,0x0c,0x06,0xc6,0xc6,0x7c,0x00,0x00,0x00,0x00},
    /* 84 'T' */ {0x00,0x00,0x7e,0x7e,0x5a,0x18,0x18,0x18,0x18,0x18,0x18,0x3c,0x00,0x00,0x00,0x00},
    /* 85 'U' */ {0x00,0x00,0xc6,0xc6,0xc6,0xc6,0xc6,0xc6,0xc6,0xc6,0xc6,0x7c,0x00,0x00,0x00,0x00},
    /* 86 'V' */ {0x00,0x00,0xc6,0xc6,0xc6,0xc6,0xc6,0xc6,0x6c,0x6c,0x38,0x10,0x00,0x00,0x00,0x00},
    /* 87 'W' */ {0x00,0x00,0xc6,0xc6,0xc6,0xc6,0xd6,0xd6,0xd6,0xfe,0xee,0x6c,0x00,0x00,0x00,0x00},
    /* 88 'X' */ {0x00,0x00,0xc6,0xc6,0x6c,0x6c,0x38,0x38,0x6c,0x6c,0xc6,0xc6,0x00,0x00,0x00,0x00},
    /* 89 'Y' */ {0x00,0x00,0x66,0x66,0x66,0x66,0x3c,0x18,0x18,0x18,0x18,0x3c,0x00,0x00,0x00,0x00},
    /* 90 'Z' */ {0x00,0x00,0xfe,0xc6,0x86,0x0c,0x18,0x30,0x60,0xc2,0xc6,0xfe,0x00,0x00,0x00,0x00},
    /* 91 '[' */ {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    /* 92 '\' */ {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    /* 93 ']' */ {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    /* 94 '^' */ {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    /* 95 '_' */ {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xff,0x00,0x00},
    /* 96 '`' */ {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    /* 97 'a' */ {0x00,0x00,0x00,0x00,0x00,0x00,0x7c,0x06,0x7e,0xc6,0xc6,0x7e,0x00,0x00,0x00,0x00},
    /* 98 'b' */ {0x00,0x00,0xc0,0xc0,0xc0,0xc0,0xfc,0xc6,0xc6,0xc6,0xc6,0xfc,0x00,0x00,0x00,0x00},
    /* 99 'c' */ {0x00,0x00,0x00,0x00,0x00,0x00,0x7c,0xc6,0xc0,0xc0,0xc6,0x7c,0x00,0x00,0x00,0x00},
    /* 100 'd' */ {0x00,0x00,0x06,0x06,0x06,0x06,0x7e,0xc6,0xc6,0xc6,0xc6,0x7e,0x00,0x00,0x00,0x00},
    /* 101 'e' */ {0x00,0x00,0x00,0x00,0x00,0x00,0x7c,0xc6,0xfe,0xc0,0xc0,0x7c,0x00,0x00,0x00,0x00},
    /* 102 'f' */ {0x00,0x00,0x1c,0x36,0x30,0x30,0x78,0x30,0x30,0x30,0x30,0x78,0x00,0x00,0x00,0x00},
    /* 103 'g' */ {0x00,0x00,0x00,0x00,0x00,0x00,0x7e,0xc6,0xc6,0xc6,0x7e,0x06,0xc6,0x7c,0x00,0x00},
    /* 104 'h' */ {0x00,0x00,0xc0,0xc0,0xc0,0xc0,0xfc,0xc6,0xc6,0xc6,0xc6,0xc6,0x00,0x00,0x00,0x00},
    /* 105 'i' */ {0x00,0x00,0x18,0x18,0x00,0x38,0x18,0x18,0x18,0x18,0x18,0x3c,0x00,0x00,0x00,0x00},
    /* 106 'j' */ {0x00,0x00,0x06,0x06,0x00,0x0e,0x06,0x06,0x06,0x06,0x06,0xc6,0xc6,0x7c,0x00,0x00},
    /* 107 'k' */ {0x00,0x00,0xc0,0xc0,0xc0,0xc0,0xc6,0xcc,0xd8,0xf0,0xd8,0xcc,0x00,0x00,0x00,0x00},
    /* 108 'l' */ {0x00,0x00,0x38,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x3c,0x00,0x00,0x00,0x00},
    /* 109 'm' */ {0x00,0x00,0x00,0x00,0x00,0x00,0xec,0xfe,0xd6,0xd6,0xc6,0xc6,0x00,0x00,0x00,0x00},
    /* 110 'n' */ {0x00,0x00,0x00,0x00,0x00,0x00,0xfc,0xc6,0xc6,0xc6,0xc6,0xc6,0x00,0x00,0x00,0x00},
    /* 111 'o' */ {0x00,0x00,0x00,0x00,0x00,0x00,0x7c,0xc6,0xc6,0xc6,0xc6,0x7c,0x00,0x00,0x00,0x00},
    /* 112 'p' */ {0x00,0x00,0x00,0x00,0x00,0x00,0xfc,0xc6,0xc6,0xc6,0xfc,0xc0,0xc0,0xc0,0x00,0x00},
    /* 113 'q' */ {0x00,0x00,0x00,0x00,0x00,0x00,0x7e,0xc6,0xc6,0xc6,0x7e,0x06,0x06,0x06,0x00,0x00},
    /* 114 'r' */ {0x00,0x00,0x00,0x00,0x00,0x00,0xdc,0xe6,0xc0,0xc0,0xc0,0xc0,0x00,0x00,0x00,0x00},
    /* 115 's' */ {0x00,0x00,0x00,0x00,0x00,0x00,0x7e,0xc0,0x7c,0x06,0x06,0xfc,0x00,0x00,0x00,0x00},
    /* 116 't' */ {0x00,0x00,0x30,0x30,0x30,0x78,0x30,0x30,0x30,0x30,0x36,0x1c,0x00,0x00,0x00,0x00},
    /* 117 'u' */ {0x00,0x00,0x00,0x00,0x00,0x00,0xc6,0xc6,0xc6,0xc6,0xc6,0x7e,0x00,0x00,0x00,0x00},
    /* 118 'v' */ {0x00,0x00,0x00,0x00,0x00,0x00,0xc6,0xc6,0xc6,0xc6,0x6c,0x38,0x00,0x00,0x00,0x00},
    /* 119 'w' */ {0x00,0x00,0x00,0x00,0x00,0x00,0xc6,0xc6,0xd6,0xd6,0xfe,0x6c,0x00,0x00,0x00,0x00},
    /* 120 'x' */ {0x00,0x00,0x00,0x00,0x00,0x00,0xc6,0x6c,0x38,0x38,0x6c,0xc6,0x00,0x00,0x00,0x00},
    /* 121 'y' */ {0x00,0x00,0x00,0x00,0x00,0x00,0xc6,0xc6,0xc6,0xc6,0x7e,0x06,0x0c,0xf8,0x00,0x00},
    /* 122 'z' */ {0x00,0x00,0x00,0x00,0x00,0x00,0xfe,0x8c,0x18,0x30,0x62,0xfe,0x00,0x00,0x00,0x00},
    /* 123-126 - simplified */ 
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
};

/* ═══════════════════════════════════════════════════════════════════
 * Color definitions (0xRRGGBB format)
 * ═══════════════════════════════════════════════════════════════════ */
#define COLOR_TRANSPARENT 0xFFFFFFFF

/* TungOS Windows-like theme */
#define COL_DESKTOP_BG    0x005A9E    /* Windows blue */
#define COL_TASKBAR_BG    0xC0C0C0    /* Silver/gray */
#define COL_TASKBAR_DARK  0x808080    /* Dark border */
#define COL_WINDOW_BG     0xFFFFFF    /* White window */
#define COL_WINDOW_TITLE  0x000080    /* Navy title bar */
#define COL_WINDOW_TITLE_INACTIVE  0x808080
#define COL_WINDOW_BORDER 0x000000    /* Black border */
#define COL_WINDOW_BORDER_LIGHT 0xDFDFDF
#define COL_TEXT_WHITE     0xFFFFFF
#define COL_TEXT_BLACK     0x000000
#define COL_TEXT_DARK      0x404040
#define COL_BTN_FACE       0xC0C0C0
#define COL_BTN_HIGHLIGHT  0xFFFFFF
#define COL_BTN_SHADOW     0x808080
#define COL_BTN_DARK       0x000000
#define COL_CLOSE_RED      0xFF0000
#define COL_LOADING_BAR    0xFFD700    /* Gold/Yellow */
#define COL_LOADING_BG     0x404040
#define COL_GREEN          0x00CC00
#define COL_ICON_TERMINAL  0x003366
#define COL_ICON_SYSINFO   0x006633
#define COL_ICON_POWER     0xCC0000

/* ═══════════════════════════════════════════════════════════════════
 * Framebuffer primitives
 * ═══════════════════════════════════════════════════════════════════ */
static inline void fb_put_pixel(int x, int y, unsigned int color) {
    if (x < 0 || x >= screen_w || y < 0 || y >= screen_h) return;
    unsigned char *pixel = back_buf + y * fb_stride + x * fb_bytes_pp;
    if (fb_bpp == 32) {
        pixel[0] = (color >> 0) & 0xFF;   /* B */
        pixel[1] = (color >> 8) & 0xFF;   /* G */
        pixel[2] = (color >> 16) & 0xFF;  /* R */
        pixel[3] = 0xFF;                   /* A */
    } else if (fb_bpp == 16) {
        unsigned short c = ((color >> 19) << 11) | 
                          (((color >> 10) & 0x3F) << 5) | 
                          ((color >> 3) & 0x1F);
        *(unsigned short*)pixel = c;
    }
}

static inline unsigned int fb_get_pixel(int x, int y) {
    if (x < 0 || x >= screen_w || y < 0 || y >= screen_h) return 0;
    unsigned char *pixel = back_buf + y * fb_stride + x * fb_bytes_pp;
    if (fb_bpp == 32) {
        return pixel[2] << 16 | pixel[1] << 8 | pixel[0];
    } else if (fb_bpp == 16) {
        unsigned short c = *(unsigned short*)pixel;
        int r = (c >> 11) << 3;
        int g = ((c >> 5) & 0x3F) << 2;
        int b = (c & 0x1F) << 3;
        return (r << 16) | (g << 8) | b;
    }
    return 0;
}

/* Fill a rectangle */
static void fill_rect(int x, int y, int w, int h, unsigned int color) {
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > screen_w) w = screen_w - x;
    if (y + h > screen_h) h = screen_h - y;
    if (w <= 0 || h <= 0) return;
    
    for (int j = y; j < y + h; j++) {
        unsigned char *pixel = back_buf + j * fb_stride + x * fb_bytes_pp;
        if (fb_bpp == 32) {
            unsigned char rb = (color >> 16) & 0xFF;
            unsigned char gb = (color >> 8) & 0xFF;
            unsigned char bb = (color >> 0) & 0xFF;
            for (int i = 0; i < w; i++) {
                pixel[0] = bb; pixel[1] = gb; pixel[2] = rb; pixel[3] = 0xFF;
                pixel += 4;
            }
        } else if (fb_bpp == 16) {
            unsigned short c = ((color >> 19) << 11) | 
                              (((color >> 10) & 0x3F) << 5) | 
                              ((color >> 3) & 0x1F);
            for (int i = 0; i < w; i++) {
                *(unsigned short*)pixel = c;
                pixel += 2;
            }
        }
    }
}

/* Draw rectangle outline */
static void draw_rect(int x, int y, int w, int h, unsigned int color) {
    /* Top */
    fill_rect(x, y, w, 1, color);
    /* Bottom */
    fill_rect(x, y + h - 1, w, 1, color);
    /* Left */
    fill_rect(x, y, 1, h, color);
    /* Right */
    fill_rect(x + w - 1, y, 1, h, color);
}

/* Draw a line */
static void draw_line(int x0, int y0, int x1, int y1, unsigned int color) {
    int dx = abs(x1 - x0);
    int dy = -abs(y1 - y0);
    int sx = x0 < x1 ? 1 : -1;
    int sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    
    while (1) {
        fb_put_pixel(x0, y0, color);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

/* Draw a character at (x, y) */
static void draw_char(int x, int y, char c, unsigned int fg, unsigned int bg) {
    if (c < 32 || c > 126) return;
    int idx = c - 32;
    for (int row = 0; row < 16; row++) {
        unsigned char bits = font8x16[idx][row];
        for (int col = 0; col < 8; col++) {
            if (bits & (0x80 >> col)) {
                fb_put_pixel(x + col, y + row, fg);
            } else if (bg != COLOR_TRANSPARENT) {
                fb_put_pixel(x + col, y + row, bg);
            }
        }
    }
}

/* Draw a string */
static void draw_text(int x, int y, const char *str, unsigned int fg, unsigned int bg) {
    int cx = x;
    while (*str) {
        draw_char(cx, y, *str, fg, bg);
        cx += 8;
        str++;
    }
}

/* Draw text centered in a rectangle */
static void draw_text_centered(int x, int y, int w, const char *str, unsigned int fg, unsigned int bg) {
    int len = strlen(str) * 8;
    int start_x = x + (w - len) / 2;
    draw_text(start_x, y, str, fg, bg);
}

/* Draw RGBA image with alpha blending */
static void draw_rgba_image(int x, int y, int img_w, int img_h, 
                           const unsigned char *data, int scale) {
    for (int j = 0; j < img_h * scale; j++) {
        int src_y = j / scale;
        for (int i = 0; i < img_w * scale; i++) {
            int src_x = i / scale;
            int src_idx = (src_y * img_w + src_x) * 4;
            unsigned char r = data[src_idx];
            unsigned char g = data[src_idx + 1];
            unsigned char b = data[src_idx + 2];
            unsigned char a = data[src_idx + 3];
            
            if (a == 0) continue; /* Skip transparent */
            
            unsigned int color = (r << 16) | (g << 8) | b;
            
            if (a == 255) {
                fb_put_pixel(x + i, y + j, color);
            } else {
                /* Alpha blend */
                unsigned int bg = fb_get_pixel(x + i, y + j);
                int bg_r = (bg >> 16) & 0xFF;
                int bg_g = (bg >> 8) & 0xFF;
                int bg_b = bg & 0xFF;
                int nr = (r * a + bg_r * (255 - a)) / 255;
                int ng = (g * a + bg_g * (255 - a)) / 255;
                int nb = (b * a + bg_b * (255 - a)) / 255;
                fb_put_pixel(x + i, y + j, (nr << 16) | (ng << 8) | nb);
            }
        }
    }
}

/* Flip back buffer to screen */
static void fb_flip(void) {
    memcpy(fb_ptr, back_buf, finfo.smem_len);
}

/* ═══════════════════════════════════════════════════════════════════
 * Windows-style UI components
 * ═══════════════════════════════════════════════════════════════════ */

/* 3D raised border (like Win95) */
static void draw_raised_border(int x, int y, int w, int h) {
    /* Outer highlight (top-left) */
    fill_rect(x, y, w - 1, 1, COL_BTN_HIGHLIGHT);
    fill_rect(x, y, 1, h - 1, COL_BTN_HIGHLIGHT);
    /* Outer shadow (bottom-right) */
    fill_rect(x + w - 1, y, 1, h, COL_BTN_SHADOW);
    fill_rect(x, y + h - 1, w, 1, COL_BTN_SHADOW);
    /* Inner highlight */
    fill_rect(x + 1, y + 1, w - 3, 1, COL_BTN_FACE);
    fill_rect(x + 1, y + 1, 1, h - 3, COL_BTN_FACE);
    /* Inner shadow */
    fill_rect(x + w - 2, y + 1, 1, h - 2, COL_BTN_DARK);
    fill_rect(x + 1, y + h - 2, w - 2, 1, COL_BTN_DARK);
}

/* 3D sunken border */
static void draw_sunken_border(int x, int y, int w, int h) {
    fill_rect(x, y, w, 1, COL_BTN_DARK);
    fill_rect(x, y, 1, h, COL_BTN_DARK);
    fill_rect(x + w - 1, y + 1, 1, h - 1, COL_BTN_HIGHLIGHT);
    fill_rect(x + 1, y + h - 1, w - 1, 1, COL_BTN_HIGHLIGHT);
}

/* Draw a button */
static void draw_button(int x, int y, int w, int h, const char *text, int pressed) {
    fill_rect(x, y, w, h, COL_BTN_FACE);
    if (pressed) {
        draw_sunken_border(x, y, w, h);
        draw_text(x + 12, y + (h - 16) / 2 + 1, text, COL_TEXT_BLACK, COLOR_TRANSPARENT);
    } else {
        draw_raised_border(x, y, w, h);
        draw_text(x + 11, y + (h - 16) / 2, text, COL_TEXT_BLACK, COLOR_TRANSPARENT);
    }
}

/* Draw a window */
typedef struct {
    int x, y, w, h;
    const char *title;
    int active;
} Window;

static void draw_window(Window *win) {
    /* Shadow */
    fill_rect(win->x + 4, win->y + 4, win->w, win->h, 0x404040);
    
    /* Window body */
    fill_rect(win->x, win->y, win->w, win->h, COL_WINDOW_BG);
    
    /* Title bar */
    unsigned int title_color = win->active ? COL_WINDOW_TITLE : COL_WINDOW_TITLE_INACTIVE;
    fill_rect(win->x + 2, win->y + 2, win->w - 4, 24, title_color);
    
    /* Title text */
    draw_text(win->x + 28, win->y + 6, win->title, COL_TEXT_WHITE, COLOR_TRANSPARENT);
    
    /* Close button [X] */
    fill_rect(win->x + win->w - 22, win->y + 4, 18, 18, COL_BTN_FACE);
    draw_raised_border(win->x + win->w - 22, win->y + 4, 18, 18);
    draw_text(win->x + win->w - 17, win->y + 7, "X", COL_TEXT_BLACK, COLOR_TRANSPARENT);
    
    /* Window borders */
    draw_raised_border(win->x, win->y, win->w, win->h);
    
    /* Menu bar area */
    fill_rect(win->x + 2, win->y + 26, win->w - 4, 2, COL_BTN_SHADOW);
}

/* ═══════════════════════════════════════════════════════════════════
 * Mouse cursor
 * ═══════════════════════════════════════════════════════════════════ */
static unsigned char cursor_saved[32*32*4];
static int cursor_saved_x = -100, cursor_saved_y = -100;
static int cursor_visible = 0;

/* Simple arrow cursor shape (16x16) */
static const unsigned char cursor_shape[16][16] = {
    {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
    {1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
    {1,2,1,0,0,0,0,0,0,0,0,0,0,0,0,0},
    {1,2,2,1,0,0,0,0,0,0,0,0,0,0,0,0},
    {1,2,2,2,1,0,0,0,0,0,0,0,0,0,0,0},
    {1,2,2,2,2,1,0,0,0,0,0,0,0,0,0,0},
    {1,2,2,2,2,2,1,0,0,0,0,0,0,0,0,0},
    {1,2,2,2,2,2,2,1,0,0,0,0,0,0,0,0},
    {1,2,2,2,2,2,2,2,1,0,0,0,0,0,0,0},
    {1,2,2,2,2,2,1,1,1,1,0,0,0,0,0,0},
    {1,2,2,1,2,2,1,0,0,0,0,0,0,0,0,0},
    {1,2,1,0,1,2,2,1,0,0,0,0,0,0,0,0},
    {1,1,0,0,1,2,2,1,0,0,0,0,0,0,0,0},
    {1,0,0,0,0,1,2,2,1,0,0,0,0,0,0,0},
    {0,0,0,0,0,1,2,2,1,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,1,1,0,0,0,0,0,0,0,0},
};

static void cursor_save_bg(void) {
    for (int j = 0; j < 16; j++) {
        for (int i = 0; i < 16; i++) {
            int px = cursor_saved_x + i;
            int py = cursor_saved_y + j;
            if (px >= 0 && px < screen_w && py >= 0 && py < screen_h) {
                unsigned char *pixel = back_buf + py * fb_stride + px * fb_bytes_pp;
                int idx = (j * 16 + i) * 4;
                cursor_saved[idx]   = pixel[0];
                cursor_saved[idx+1] = pixel[1];
                cursor_saved[idx+2] = pixel[2];
                cursor_saved[idx+3] = pixel[3];
            }
        }
    }
    cursor_visible = 1;
}

static void cursor_restore_bg(void) {
    if (!cursor_visible) return;
    for (int j = 0; j < 16; j++) {
        for (int i = 0; i < 16; i++) {
            int px = cursor_saved_x + i;
            int py = cursor_saved_y + j;
            if (px >= 0 && px < screen_w && py >= 0 && py < screen_h) {
                unsigned char *pixel = back_buf + py * fb_stride + px * fb_bytes_pp;
                int idx = (j * 16 + i) * 4;
                pixel[0] = cursor_saved[idx];
                pixel[1] = cursor_saved[idx+1];
                pixel[2] = cursor_saved[idx+2];
                pixel[3] = cursor_saved[idx+3];
            }
        }
    }
    cursor_visible = 0;
}

static void cursor_draw(void) {
    cursor_save_bg();
    for (int j = 0; j < 16; j++) {
        for (int i = 0; i < 16; i++) {
            int px = mouse_x + i;
            int py = mouse_y + j;
            unsigned char c = cursor_shape[j][i];
            if (c == 1) {
                fb_put_pixel(px, py, 0xFFFFFF); /* White outline */
            } else if (c == 2) {
                fb_put_pixel(px, py, 0x000000); /* Black fill */
            }
        }
    }
}

/* ═══════════════════════════════════════════════════════════════════
 * Desktop icons
 * ═══════════════════════════════════════════════════════════════════ */
typedef struct {
    int x, y;
    int w, h;
    const char *label;
    unsigned int color;
} DesktopIcon;

#define ICON_SIZE 48
#define ICON_SPACING 80

static DesktopIcon desktop_icons[] = {
    { 30, 40, ICON_SIZE, ICON_SIZE, "Terminal",  COL_ICON_TERMINAL },
    { 30, 130, ICON_SIZE, ICON_SIZE, "SysInfo",  COL_ICON_SYSINFO },
    { 30, 220, ICON_SIZE, ICON_SIZE, "Power",    COL_ICON_POWER },
};
#define NUM_ICONS 3

static void draw_desktop_icon(DesktopIcon *icon) {
    /* Icon box */
    int x = icon->x, y = icon->y, w = icon->w, h = icon->h;
    
    /* Draw a raised box with icon color */
    fill_rect(x, y, w, h, icon->color);
    draw_raised_border(x, y, w, h);
    
    /* Icon interior - draw a simple symbol */
    if (strcmp(icon->label, "Terminal") == 0) {
        /* Terminal icon: >_ prompt */
        fill_rect(x + 6, y + 8, w - 12, h - 16, 0x001030);
        draw_text(x + 10, y + 14, ">_", COL_GREEN, COLOR_TRANSPARENT);
    } else if (strcmp(icon->label, "SysInfo") == 0) {
        /* SysInfo icon: monitor shape */
        fill_rect(x + 8, y + 6, w - 16, h - 20, 0xD0D0D0);
        draw_sunken_border(x + 8, y + 6, w - 16, h - 20);
        draw_line(x + w/2 - 6, y + h - 16, x + w/2 + 6, y + h - 16, 0x000000);
        draw_line(x + w/2, y + h - 16, x + w/2, y + h - 10, 0x000000);
    } else if (strcmp(icon->label, "Power") == 0) {
        /* Power icon: circle with line */
        int cx = x + w/2, cy = y + h/2;
        int r = 14;
        for (int a = 0; a < 360; a += 5) {
            int px = cx + r * cos(a * 3.14159 / 180);
            int py = cy + r * sin(a * 3.14159 / 180);
            fb_put_pixel(px, py, 0xFF0000);
        }
        draw_line(cx, cy - 16, cx, cy - 2, 0xFF0000);
    }
    
    /* Label below icon */
    int label_x = x + (w - strlen(icon->label) * 8) / 2;
    draw_text(label_x, y + h + 4, icon->label, COL_TEXT_WHITE, COLOR_TRANSPARENT);
}

/* ═══════════════════════════════════════════════════════════════════
 * Taskbar
 * ═══════════════════════════════════════════════════════════════════ */
#define TASKBAR_H 36

static void draw_taskbar(void) {
    int tb_y = screen_h - TASKBAR_H;
    
    /* Taskbar background */
    fill_rect(0, tb_y, screen_w, TASKBAR_H, COL_TASKBAR_BG);
    
    /* Top border (raised effect) */
    fill_rect(0, tb_y, screen_w, 2, COL_BTN_HIGHLIGHT);
    fill_rect(0, tb_y + 2, screen_w, 1, COL_BTN_DARK);
    
    /* TungOS Start button */
    draw_button(4, tb_y + 4, 80, 28, "TungOS", 0);
    
    /* Draw the tiny logo on the start button */
    draw_rgba_image(8, tb_y + 6, LOGO_ICON_W, LOGO_ICON_IMGH, logo_icon, 1);
    
    /* Separator */
    fill_rect(88, tb_y + 4, 2, 28, COL_BTN_SHADOW);
    
    /* Active window area */
    if (active_window) {
        const char *win_name = "";
        if (active_window == 1) win_name = "Terminal";
        else if (active_window == 2) win_name = "SysInfo";
        else if (active_window == 3) win_name = "Power";
        
        draw_button(96, tb_y + 4, 100, 28, win_name, 1);
    }
    
    /* System tray - right side */
    int tray_x = screen_w - 120;
    fill_rect(tray_x, tb_y + 4, 2, 28, COL_BTN_SHADOW);
    
    /* Clock */
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    char time_str[32];
    strftime(time_str, sizeof(time_str), "%H:%M", t);
    char date_str[32];
    strftime(date_str, sizeof(date_str), "%a %b %d", t);
    
    int clock_x = screen_w - 110;
    draw_text(clock_x, tb_y + 6, date_str, COL_TEXT_DARK, COLOR_TRANSPARENT);
    draw_text(clock_x + 4, tb_y + 20, time_str, COL_TEXT_BLACK, COLOR_TRANSPARENT);
}

/* ═══════════════════════════════════════════════════════════════════
 * Start Menu
 * ═══════════════════════════════════════════════════════════════════ */
static int start_menu_open = 0;

static void draw_start_menu(void) {
    int menu_x = 4;
    int menu_y = screen_h - TASKBAR_H - 180;
    int menu_w = 180;
    int menu_h = 180;
    
    /* Shadow */
    fill_rect(menu_x + 4, menu_y + 4, menu_w, menu_h, 0x404040);
    
    /* Menu background */
    fill_rect(menu_x, menu_y, menu_w, menu_h, COL_BTN_FACE);
    
    /* Side banner */
    fill_rect(menu_x, menu_y, 28, menu_h, COL_WINDOW_TITLE);
    draw_text(menu_x + 2, menu_y + menu_h - 20, "TOS", COL_TEXT_WHITE, COLOR_TRANSPARENT);
    
    /* Menu items */
    const char *items[] = {"Terminal", "System Info", "Reboot", "Power Off"};
    int num_items = 4;
    
    for (int i = 0; i < num_items; i++) {
        int item_y = menu_y + 8 + i * 36;
        int is_hover = (mouse_y >= item_y && mouse_y < item_y + 32 && 
                        mouse_x >= menu_x + 28 && mouse_x < menu_x + menu_w);
        
        if (is_hover) {
            fill_rect(menu_x + 28, item_y, menu_w - 32, 32, 0x000080);
            draw_text(menu_x + 36, item_y + 8, items[i], COL_TEXT_WHITE, COLOR_TRANSPARENT);
        } else {
            fill_rect(menu_x + 28, item_y, menu_w - 32, 32, COLOR_TRANSPARENT);
            draw_text(menu_x + 36, item_y + 8, items[i], COL_TEXT_BLACK, COLOR_TRANSPARENT);
        }
    }
    
    /* Border */
    draw_rect(menu_x, menu_y, menu_w, menu_h, COL_BTN_DARK);
    
    /* Separator */
    fill_rect(menu_x + 28, menu_y + 8 + 2 * 36 - 2, menu_w - 32, 2, COL_BTN_SHADOW);
}

/* ═══════════════════════════════════════════════════════════════════
 * Full desktop rendering
 * ═══════════════════════════════════════════════════════════════════ */
static Window terminal_win = {150, 60, 500, 360, "Terminal", 1};
static Window sysinfo_win = {180, 80, 420, 320, "System Information", 1};
static Window power_win = {250, 150, 300, 200, "Power", 1};

static void draw_desktop(void) {
    /* Desktop background */
    fill_rect(0, 0, screen_w, screen_h, COL_DESKTOP_BG);
    
    /* Draw logo in the center-ish of the desktop */
    int logo_x = (screen_w - LOGO_SPLASH_W * 1) / 2;
    int logo_y = (screen_h - TASKBAR_H) / 2 - LOGO_SPLASH_IMGH / 2;
    draw_rgba_image(logo_x, logo_y, LOGO_SPLASH_W, LOGO_SPLASH_IMGH, logo_splash, 1);
    
    /* Desktop icons */
    for (int i = 0; i < NUM_ICONS; i++) {
        draw_desktop_icon(&desktop_icons[i]);
    }
    
    /* Windows */
    if (active_window == 1) {
        draw_window(&terminal_win);
        /* Terminal content */
        fill_rect(terminal_win.x + 4, terminal_win.y + 30, terminal_win.w - 8, terminal_win.h - 34, 0x001030);
        draw_text(terminal_win.x + 10, terminal_win.y + 36, "TungOS Terminal v0.1", COL_GREEN, COLOR_TRANSPARENT);
        draw_text(terminal_win.x + 10, terminal_win.y + 54, "Type 'exit' to close", COL_TEXT_WHITE, COLOR_TRANSPARENT);
        draw_text(terminal_win.x + 10, terminal_win.y + 72, "$ _", COL_GREEN, COLOR_TRANSPARENT);
    } else if (active_window == 2) {
        draw_window(&sysinfo_win);
        /* System info content */
        int sx = sysinfo_win.x + 12, sy = sysinfo_win.y + 34;
        draw_text(sx, sy,      "OS:       TungOS v0.1 Zephyr", COL_TEXT_BLACK, COLOR_TRANSPARENT);
        draw_text(sx, sy + 18, "Kernel:   Linux 6.6.87-tungos", COL_TEXT_BLACK, COLOR_TRANSPARENT);
        draw_text(sx, sy + 36, "Arch:     x86_64", COL_TEXT_BLACK, COLOR_TRANSPARENT);
        draw_text(sx, sy + 54, "Shell:    ash (BusyBox)", COL_TEXT_BLACK, COLOR_TRANSPARENT);
        
        /* Read some /proc info */
        {
            FILE *fp = fopen("/proc/meminfo", "r");
            if (fp) {
                char line[256];
                int memtotal = 0, memfree = 0;
                if (fgets(line, sizeof(line), fp)) sscanf(line, "MemTotal: %d", &memtotal);
                if (fgets(line, sizeof(line), fp)) sscanf(line, "MemFree: %d", &memfree);
                fclose(fp);
                char buf[64];
                snprintf(buf, sizeof(buf), "Memory:   %d/%d MB", (memtotal - memfree)/1024, memtotal/1024);
                draw_text(sx, sy + 72, buf, COL_TEXT_BLACK, COLOR_TRANSPARENT);
            }
        }
        {
            FILE *fp = fopen("/proc/uptime", "r");
            if (fp) {
                double up;
                fscanf(fp, "%lf", &up);
                fclose(fp);
                char buf[64];
                snprintf(buf, sizeof(buf), "Uptime:   %dh %dm", (int)(up/3600), ((int)up % 3600)/60);
                draw_text(sx, sy + 90, buf, COL_TEXT_BLACK, COLOR_TRANSPARENT);
            }
        }
    } else if (active_window == 3) {
        draw_window(&power_win);
        /* Power menu content */
        int px = power_win.x + 20, py = power_win.y + 36;
        draw_text(px, py, "What do you want to do?", COL_TEXT_BLACK, COLOR_TRANSPARENT);
        draw_button(px, py + 28, 120, 28, "Reboot", 0);
        draw_button(px + 140, py + 28, 120, 28, "Power Off", 0);
        draw_button(px + 70, py + 68, 100, 28, "Cancel", 0);
    }
    
    /* Start menu (on top) */
    if (start_menu_open) {
        draw_start_menu();
    }
    
    /* Taskbar (always on top) */
    draw_taskbar();
}

/* ═══════════════════════════════════════════════════════════════════
 * Boot splash screen
 * ═══════════════════════════════════════════════════════════════════ */
static void show_boot_splash(void) {
    /* Dark blue background */
    fill_rect(0, 0, screen_w, screen_h, 0x002050);
    
    /* Draw logo centered */
    int logo_scale = 1;
    int logo_x = (screen_w - LOGO_SPLASH_W * logo_scale) / 2;
    int logo_y = (screen_h - LOGO_SPLASH_IMGH * logo_scale) / 2 - 60;
    draw_rgba_image(logo_x, logo_y, LOGO_SPLASH_W, LOGO_SPLASH_IMGH, logo_splash, logo_scale);
    
    /* Title text */
    draw_text_centered(0, logo_y + LOGO_SPLASH_IMGH * logo_scale + 20, screen_w, 
                       "TungOS v0.1 Zephyr", COL_TEXT_WHITE, COLOR_TRANSPARENT);
    draw_text_centered(0, logo_y + LOGO_SPLASH_IMGH * logo_scale + 40, screen_w,
                       "Ultra-Lightweight Linux", 0x808080, COLOR_TRANSPARENT);
    
    /* Loading bar background */
    int bar_w = 300;
    int bar_h = 16;
    int bar_x = (screen_w - bar_w) / 2;
    int bar_y = logo_y + LOGO_SPLASH_IMGH * logo_scale + 70;
    
    draw_sunken_border(bar_x - 2, bar_y - 2, bar_w + 4, bar_h + 4);
    fill_rect(bar_x, bar_y, bar_w, bar_h, COL_LOADING_BG);
    
    fb_flip();
    
    /* Animate loading bar */
    for (int i = 0; i < bar_w; i++) {
        /* Draw one pixel column of the progress bar */
        for (int j = 0; j < bar_h; j++) {
            fb_put_pixel(bar_x + i, bar_y + j, COL_LOADING_BAR);
        }
        
        /* Update screen periodically */
        if (i % 4 == 0) {
            fb_flip();
        }
        usleep(8000); /* ~2.4 seconds for full bar */
    }
    
    fb_flip();
    usleep(300000); /* Pause briefly */
}

/* ═══════════════════════════════════════════════════════════════════
 * Mouse handling
 * ═══════════════════════════════════════════════════════════════════ */
static void process_mouse_event(void) {
    /* Read from /dev/input/mice (3 bytes per event) */
    unsigned char buf[3];
    int n = read(mouse_fd, buf, 3);
    if (n < 3) return;
    
    int dx = (signed char)buf[1];
    int dy = (signed char)buf[2];
    
    /* Buttons */
    mouse_lbtn = buf[0] & 0x01;
    mouse_rbtn = buf[0] & 0x02;
    
    /* Apply movement */
    mouse_x += dx;
    mouse_y -= dy; /* Invert Y */
    
    /* Clamp to screen */
    if (mouse_x < 0) mouse_x = 0;
    if (mouse_x >= screen_w - 1) mouse_x = screen_w - 1;
    if (mouse_y < 0) mouse_y = 0;
    if (mouse_y >= screen_h - 1) mouse_y = screen_h - 1;
    
    if (dx != 0 || dy != 0) mouse_moved = 1;
}

static void handle_click(int x, int y) {
    int tb_y = screen_h - TASKBAR_H;
    
    /* Start button click */
    if (x >= 4 && x <= 84 && y >= tb_y + 4 && y <= tb_y + 32) {
        start_menu_open = !start_menu_open;
        if (start_menu_open) active_window = 0;
        return;
    }
    
    /* Start menu items */
    if (start_menu_open) {
        int menu_x = 4;
        int menu_y = screen_h - TASKBAR_H - 180;
        
        if (x >= menu_x + 28 && x < menu_x + 180) {
            if (y >= menu_y + 8 && y < menu_y + 8 + 36) {
                /* Terminal */
                start_menu_open = 0;
                active_window = 1;
                return;
            } else if (y >= menu_y + 8 + 36 && y < menu_y + 8 + 72) {
                /* SysInfo */
                start_menu_open = 0;
                active_window = 2;
                return;
            } else if (y >= menu_y + 8 + 72 && y < menu_y + 8 + 108) {
                /* Reboot */
                start_menu_open = 0;
                /* Reboot the system */
                fb_flip();
                pid_t pid = fork();
                if (pid == 0) {
                    execl("/sbin/reboot", "reboot", NULL);
                    _exit(1);
                }
                return;
            } else if (y >= menu_y + 8 + 108 && y < menu_y + 8 + 144) {
                /* Power Off */
                start_menu_open = 0;
                fb_flip();
                pid_t pid = fork();
                if (pid == 0) {
                    execl("/sbin/poweroff", "poweroff", NULL);
                    _exit(1);
                }
                return;
            }
        }
        /* Click outside menu closes it */
        if (x < menu_x || x > menu_x + 180 || y < menu_y || y > menu_y + 180) {
            start_menu_open = 0;
        }
        return;
    }
    
    /* Desktop icon clicks */
    for (int i = 0; i < NUM_ICONS; i++) {
        DesktopIcon *icon = &desktop_icons[i];
        if (x >= icon->x && x < icon->x + icon->w && 
            y >= icon->y && y < icon->y + icon->h + 20) {
            if (strcmp(icon->label, "Terminal") == 0) {
                active_window = 1;
            } else if (strcmp(icon->label, "SysInfo") == 0) {
                active_window = 2;
            } else if (strcmp(icon->label, "Power") == 0) {
                active_window = 3;
            }
            return;
        }
    }
    
    /* Window close button clicks */
    if (active_window) {
        Window *win = NULL;
        if (active_window == 1) win = &terminal_win;
        else if (active_window == 2) win = &sysinfo_win;
        else if (active_window == 3) win = &power_win;
        
        if (win) {
            /* Close button [X] */
            if (x >= win->x + win->w - 22 && x <= win->x + win->w - 4 &&
                y >= win->y + 4 && y <= win->y + 22) {
                active_window = 0;
                return;
            }
            
            /* Power menu buttons */
            if (active_window == 3) {
                int px = power_win.x + 20, py = power_win.y + 36;
                /* Reboot */
                if (x >= px && x <= px + 120 && y >= py + 28 && y <= py + 56) {
                    pid_t pid = fork();
                    if (pid == 0) { execl("/sbin/reboot", "reboot", NULL); _exit(1); }
                    return;
                }
                /* Power Off */
                if (x >= px + 140 && x <= px + 260 && y >= py + 28 && y <= py + 56) {
                    pid_t pid = fork();
                    if (pid == 0) { execl("/sbin/poweroff", "poweroff", NULL); _exit(1); }
                    return;
                }
                /* Cancel */
                if (x >= px + 70 && x <= px + 170 && y >= py + 68 && y <= py + 96) {
                    active_window = 0;
                    return;
                }
            }
        }
    }
    
    /* Click on taskbar area does nothing special */
}

/* ═══════════════════════════════════════════════════════════════════
 * Input loop with select()
 * ═══════════════════════════════════════════════════════════════════ */
#include <sys/select.h>

static int needs_redraw = 1;

static void input_loop(void) {
    fd_set fds;
    struct timeval tv;
    int max_fd = mouse_fd;
    int click_pending = 0;
    
    while (running) {
        FD_ZERO(&fds);
        if (mouse_fd >= 0) FD_SET(mouse_fd, &fds);
        
        tv.tv_sec = 0;
        tv.tv_usec = 50000; /* 50ms = 20 FPS for idle */
        
        int ret = select(max_fd + 1, &fds, NULL, NULL, &tv);
        
        if (ret > 0 && FD_ISSET(mouse_fd, &fds)) {
            int was_lbtn = mouse_lbtn;
            process_mouse_event();
            
            /* Detect click (release after press) */
            if (was_lbtn && !mouse_lbtn) {
                handle_click(mouse_x, mouse_y);
                needs_redraw = 1;
            }
            if (mouse_moved) {
                needs_redraw = 1;
                mouse_moved = 0;
            }
        }
        
        /* Periodic taskbar clock update */
        static int tick = 0;
        tick++;
        if (tick >= 20) { /* ~1 second */
            tick = 0;
            needs_redraw = 1;
        }
        
        if (needs_redraw) {
            /* Restore old cursor position */
            cursor_restore_bg();
            
            /* Redraw desktop */
            draw_desktop();
            
            /* Draw cursor at new position */
            cursor_saved_x = mouse_x;
            cursor_saved_y = mouse_y;
            cursor_draw();
            
            /* Flip to screen */
            fb_flip();
            
            needs_redraw = 0;
        }
    }
}

/* ═══════════════════════════════════════════════════════════════════
 * Initialization
 * ═══════════════════════════════════════════════════════════════════ */
static int init_framebuffer(void) {
    fb_fd = open("/dev/fb0", O_RDWR);
    if (fb_fd < 0) {
        perror("open /dev/fb0");
        return -1;
    }
    
    if (ioctl(fb_fd, FBIOGET_VSCREENINFO, &vinfo) < 0) {
        perror("ioctl FBIOGET_VSCREENINFO");
        close(fb_fd);
        return -1;
    }
    
    if (ioctl(fb_fd, FBIOGET_FSCREENINFO, &finfo) < 0) {
        perror("ioctl FBIOGET_FSCREENINFO");
        close(fb_fd);
        return -1;
    }
    
    screen_w = vinfo.xres;
    screen_h = vinfo.yres;
    fb_bpp = vinfo.bits_per_pixel;
    fb_bytes_pp = fb_bpp / 8;
    fb_stride = finfo.line_length;
    
    fprintf(stderr, "[TungOS] Framebuffer: %dx%d %dbpp stride=%d\n", 
            screen_w, screen_h, fb_bpp, fb_stride);
    
    /* Map framebuffer */
    size_t fb_size = finfo.smem_len;
    fb_ptr = mmap(NULL, fb_size, PROT_READ | PROT_WRITE, MAP_SHARED, fb_fd, 0);
    if (fb_ptr == MAP_FAILED) {
        perror("mmap fb");
        close(fb_fd);
        return -1;
    }
    
    /* Allocate back buffer */
    back_buf = malloc(fb_size);
    if (!back_buf) {
        perror("malloc back_buf");
        munmap(fb_ptr, fb_size);
        close(fb_fd);
        return -1;
    }
    memset(back_buf, 0, fb_size);
    
    return 0;
}

static int init_mouse(void) {
    /* Try /dev/input/mice first */
    mouse_fd = open("/dev/input/mice", O_RDONLY | O_NONBLOCK);
    if (mouse_fd < 0) {
        /* Try event devices */
        mouse_fd = open("/dev/input/event1", O_RDONLY | O_NONBLOCK);
        if (mouse_fd < 0) {
            mouse_fd = open("/dev/input/event0", O_RDONLY | O_NONBLOCK);
        }
    }
    
    if (mouse_fd >= 0) {
        fprintf(stderr, "[TungOS] Mouse device opened (fd=%d)\n", mouse_fd);
        /* Center mouse */
        mouse_x = screen_w / 2;
        mouse_y = screen_h / 2;
        return 0;
    }
    
    fprintf(stderr, "[TungOS] Warning: No mouse device found\n");
    return -1;
}

static void cleanup(void) {
    if (back_buf) free(back_buf);
    if (fb_ptr) munmap(fb_ptr, finfo.smem_len);
    if (fb_fd >= 0) close(fb_fd);
    if (mouse_fd >= 0) close(mouse_fd);
}

/* ═══════════════════════════════════════════════════════════════════
 * Main
 * ═══════════════════════════════════════════════════════════════════ */
int main(void) {
    fprintf(stderr, "[TungOS] Starting graphical desktop...\n");
    
    /* Initialize framebuffer */
    if (init_framebuffer() < 0) {
        fprintf(stderr, "[TungOS] FATAL: Cannot initialize framebuffer\n");
        fprintf(stderr, "[TungOS] Falling back to shell...\n");
        execl("/bin/sh", "sh", "-l", NULL);
        return 1;
    }
    
    /* Initialize mouse */
    init_mouse();
    
    /* Show boot splash with real logo */
    show_boot_splash();
    
    /* Enter desktop */
    mouse_x = screen_w / 2;
    mouse_y = screen_h / 2;
    input_loop();
    
    /* Cleanup */
    cleanup();
    return 0;
}
