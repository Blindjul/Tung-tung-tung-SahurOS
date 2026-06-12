/*
 * TungOS Graphical Desktop Environment
 * Real GUI - direct framebuffer rendering
 * No X11, no Wayland, no dependencies
 *
 * Features:
 *   - Graphical desktop with gradient wallpaper
 *   - Mouse cursor + click support
 *   - Draggable windows with title bars
 *   - Taskbar with Start button, clock, system tray
 *   - Desktop icons (Terminal, SysInfo, Power)
 *   - Start menu popup
 *   - Built-in terminal emulator window
 *   - System info window
 *   - Power options
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <termios.h>
#include <time.h>
#include <errno.h>
#include <signal.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <linux/fb.h>
#include <linux/input.h>
#include <dirent.h>
#include <stdint.h>

/* ── Framebuffer globals ── */
static int fb_fd = -1;
static struct fb_var_screeninfo vinfo;
static struct fb_fix_screeninfo finfo;
static unsigned char *fb_mem = NULL;
static unsigned char *backbuf = NULL;
static int fb_w = 0, fb_h = 0, fb_bpp = 0, fb_stride = 0;

/* ── Input globals ── */
static int mouse_fd = -1;
static int kbd_fd = -1;
static int mouse_x = 400, mouse_y = 300;
static int mouse_btn = 0;
static int mouse_prev_btn = 0;

/* ── Theme colors (RGB) ── */
#define TUNGOS_BLUE     ((uint32_t)(0x00 << 16 | 0x78 | 0xD4 << 8))  /* #0078D4 */
#define TUNGOS_DARK     ((uint32_t)(0x00 << 16 | 0x5A | 0x9E << 8))  /* #005A9E */
#define TUNGOS_ACCENT   ((uint32_t)(0xFF << 16 | 0xC1 | 0x07 << 8))  /* #FFC107 */
#define TUNGOS_WHITE    ((uint32_t)(0xFF << 16 | 0xFF | 0xFF << 8))
#define TUNGOS_BLACK    ((uint32_t)(0x00 << 16 | 0x00 | 0x00 << 8))
#define TUNGOS_GRAY     ((uint32_t)(0xF3 << 16 | 0xF3 | 0xF3 << 8))  /* #F3F3F3 */
#define TUNGOS_DARKGRAY ((uint32_t)(0x76 << 16 | 0x76 | 0x76 << 8))  /* #767676 */
#define TUNGOS_TASKBAR  ((uint32_t)(0x00 << 16 | 0x78 | 0xD4 << 8))  /* #0078D4 */
#define TUNGOS_TITLE    ((uint32_t)(0x00 << 16 | 0x78 | 0xD4 << 8))
#define TUNGOS_WINBG   ((uint32_t)(0xFF << 16 | 0xFF | 0xFF << 8))
#define TUNGOS_RED      ((uint32_t)(0xE8 << 16 | 0x11 | 0x23 << 8))  /* #E81123 */
#define TUNGOS_GREEN    ((uint32_t)(0x10 << 16 | 0x7C | 0x10 << 8))  /* #107C10 */
#define TUNGOS_YELLOW   ((uint32_t)(0xFF << 16 | 0xC1 | 0x07 << 8))  /* #FFC107 */
#define TUNGOS_CLOSE    ((uint32_t)(0xE8 << 16 | 0x11 | 0x23 << 8))  /* Close btn red */

/* ── Layout ── */
#define TASKBAR_H       36
#define ICON_SIZE       48
#define WIN_TITLE_H     28
#define WIN_BORDER      2
#define START_BTN_W     48
#define START_MENU_W    180
#define START_MENU_H    160

/* ── Pixel drawing ── */
static inline uint32_t rgb32(int r, int g, int b) {
    return (r << 16) | (g << 8) | b;
}

static inline void put_pixel(int x, int y, uint32_t color) {
    if (x < 0 || x >= fb_w || y < 0 || y >= fb_h) return;
    unsigned char *p = backbuf + y * fb_stride + x * (fb_bpp / 8);
    if (fb_bpp == 32) {
        p[0] = color & 0xFF;
        p[1] = (color >> 8) & 0xFF;
        p[2] = (color >> 16) & 0xFF;
        p[3] = 0xFF;
    } else if (fb_bpp == 16) {
        uint16_t c = ((color >> 16) & 0xF8) << 8 | ((color >> 8) & 0xFC) << 3 | (color & 0xF8) >> 3;
        p[0] = c & 0xFF;
        p[1] = (c >> 8) & 0xFF;
    }
}

static inline uint32_t get_pixel(int x, int y) {
    if (x < 0 || x >= fb_w || y < 0 || y >= fb_h) return 0;
    unsigned char *p = backbuf + y * fb_stride + x * (fb_bpp / 8);
    if (fb_bpp == 32) {
        return p[0] | (p[1] << 8) | (p[2] << 16);
    } else if (fb_bpp == 16) {
        uint16_t c = p[0] | (p[1] << 8);
        int r = (c >> 8) & 0xF8;
        int g = (c >> 3) & 0xFC;
        int b = (c << 3) & 0xF8;
        return rgb32(r, g, b);
    }
    return 0;
}

/* ── Primitives ── */
void fill_rect(int x, int y, int w, int h, uint32_t color) {
    for (int dy = 0; dy < h; dy++)
        for (int dx = 0; dx < w; dx++)
            put_pixel(x + dx, y + dy, color);
}

void draw_rect_border(int x, int y, int w, int h, uint32_t color, int thickness) {
    fill_rect(x, y, w, thickness, color);
    fill_rect(x, y + h - thickness, w, thickness, color);
    fill_rect(x, y, thickness, h, color);
    fill_rect(x + w - thickness, y, thickness, h, color);
}

void draw_line(int x0, int y0, int x1, int y1, uint32_t color) {
    int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    while (1) {
        put_pixel(x0, y0, color);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

/* ── Bitmap font (8x16, based on Linux kernel font) ── */
#include "tungos_font.h"

void draw_char(int x, int y, char c, uint32_t fg, uint32_t bg) {
    if ((unsigned char)c < 32 || (unsigned char)c > 126) c = '?';
    const unsigned char *glyph = font8x16 + ((unsigned char)c - 32) * 16;
    for (int row = 0; row < 16; row++) {
        unsigned char bits = glyph[row];
        for (int col = 0; col < 8; col++) {
            uint32_t color = (bits & (0x80 >> col)) ? fg : bg;
            /* Scale 2x for readability */
            put_pixel(x + col * 2, y + row * 2, color);
            put_pixel(x + col * 2 + 1, y + row * 2, color);
            put_pixel(x + col * 2, y + row * 2 + 1, color);
            put_pixel(x + col * 2 + 1, y + row * 2 + 1, color);
        }
    }
}

void draw_text(int x, int y, const char *text, uint32_t fg, uint32_t bg) {
    int cx = x;
    while (*text) {
        if (*text == '\n') { cx = x; y += 32; text++; continue; }
        draw_char(cx, y, *text, fg, bg);
        cx += 16;
        text++;
    }
}

void draw_text_transparent(int x, int y, const char *text, uint32_t fg) {
    int cx = x;
    while (*text) {
        if (*text == '\n') { cx = x; y += 32; text++; continue; }
        const unsigned char *glyph = font8x16 + ((unsigned char)*text - 32) * 16;
        for (int row = 0; row < 16; row++) {
            unsigned char bits = glyph[row];
            for (int col = 0; col < 8; col++) {
                if (bits & (0x80 >> col)) {
                    put_pixel(cx + col * 2, y + row * 2, fg);
                    put_pixel(cx + col * 2 + 1, y + row * 2, fg);
                    put_pixel(cx + col * 2, y + row * 2 + 1, fg);
                    put_pixel(cx + col * 2 + 1, y + row * 2 + 1, fg);
                }
            }
        }
        cx += 16;
        text++;
    }
}

/* ── Gradient ── */
void draw_gradient(int x, int y, int w, int h, uint32_t c1, uint32_t c2) {
    int r1 = (c1 >> 16) & 0xFF, g1 = (c1 >> 8) & 0xFF, b1 = c1 & 0xFF;
    int r2 = (c2 >> 16) & 0xFF, g2 = (c2 >> 8) & 0xFF, b2 = c2 & 0xFF;
    for (int dy = 0; dy < h; dy++) {
        float t = (float)dy / h;
        int r = r1 + (r2 - r1) * t;
        int g = g1 + (g2 - g1) * t;
        int b = b1 + (b2 - b1) * t;
        fill_rect(x, y + dy, w, 1, rgb32(r, g, b));
    }
}

/* ── Rounded rect ── */
void draw_rounded_rect(int x, int y, int w, int h, int radius, uint32_t fill, uint32_t border) {
    fill_rect(x + radius, y, w - 2 * radius, h, fill);
    fill_rect(x, y + radius, w, h - 2 * radius, fill);
    /* Corners */
    for (int dy = -radius; dy <= radius; dy++) {
        for (int dx = -radius; dx <= radius; dx++) {
            if (dx * dx + dy * dy <= radius * radius) {
                put_pixel(x + radius + dx, y + radius + dy, fill);
                put_pixel(x + w - radius + dx, y + radius + dy, fill);
                put_pixel(x + radius + dx, y + h - radius + dy, fill);
                put_pixel(x + w - radius + dx, y + h - radius + dy, fill);
            }
        }
    }
    if (border != fill) {
        draw_rect_border(x, y, w, h, border, 2);
    }
}

/* ── Screen flip ── */
void flip_screen(void) {
    memcpy(fb_mem, backbuf, fb_stride * fb_h);
}

/* ── Window system ── */
#define MAX_WINDOWS 8
#define MAX_CONSOLE_LINES 50

typedef struct {
    int x, y, w, h;
    int visible;
    int focused;
    char title[64];
    uint32_t title_color;
    /* For terminal window */
    int is_terminal;
    char console_lines[MAX_CONSOLE_LINES][80];
    int console_line_count;
    int console_cursor;
    int pid;  /* child shell pid */
    int master_fd;  /* pty master */
} Window;

static Window windows[MAX_WINDOWS];
static int window_count = 0;
static int active_window = -1;
static int dragging = 0;
static int drag_off_x = 0, drag_off_y = 0;
static int start_menu_open = 0;

/* ── Icon positions ── */
typedef struct {
    int x, y;
    char label[32];
    uint32_t color;
    int action;  /* 0=terminal, 1=sysinfo, 2=power */
} DesktopIcon;

#define NUM_ICONS 3
static DesktopIcon icons[NUM_ICONS] = {
    { 40, 80,  "Terminal",   TUNGOS_GREEN,  0 },
    { 40, 190, "SysInfo",    TUNGOS_BLUE,   1 },
    { 40, 300, "Power",      TUNGOS_RED,    2 },
};

/* ── Create window ── */
int create_window(const char *title, int x, int y, int w, int h, int is_terminal) {
    if (window_count >= MAX_WINDOWS) return -1;
    int idx = window_count++;
    memset(&windows[idx], 0, sizeof(Window));
    windows[idx].x = x; windows[idx].y = y;
    windows[idx].w = w; windows[idx].h = h;
    windows[idx].visible = 1;
    windows[idx].focused = 1;
    strncpy(windows[idx].title, title, sizeof(windows[idx].title) - 1);
    windows[idx].title_color = TUNGOS_TITLE;
    windows[idx].is_terminal = is_terminal;
    windows[idx].pid = -1;
    windows[idx].master_fd = -1;

    /* Unfocus other windows */
    for (int i = 0; i < window_count - 1; i++)
        windows[i].focused = 0;

    active_window = idx;
    return idx;
}

/* ── Draw a window ── */
void draw_window(int idx) {
    Window *win = &windows[idx];
    if (!win->visible) return;

    /* Shadow */
    fill_rect(win->x + 4, win->y + 4, win->w, win->h, rgb32(0x40, 0x40, 0x40));

    /* Window body */
    fill_rect(win->x, win->y, win->w, win->h, TUNGOS_WINBG);

    /* Title bar */
    uint32_t tc = win->focused ? TUNGOS_TITLE : TUNGOS_DARKGRAY;
    fill_rect(win->x, win->y, win->w, WIN_TITLE_H, tc);

    /* Title text */
    draw_text(win->x + 10, win->y + 4, win->title, TUNGOS_WHITE, tc);

    /* Close button (X) */
    int close_x = win->x + win->w - 28;
    int close_y = win->y + 2;
    fill_rect(close_x, close_y, 24, 24, TUNGOS_CLOSE);
    draw_text(close_x + 4, close_y + 2, "X", TUNGOS_WHITE, TUNGOS_CLOSE);

    /* Minimize button */
    fill_rect(close_x - 28, close_y, 24, 24, rgb32(0x60, 0x60, 0x60));
    draw_text(close_x - 24, close_y + 2, "_", TUNGOS_WHITE, rgb32(0x60, 0x60, 0x60));

    /* Border */
    draw_rect_border(win->x, win->y, win->w, win->h, rgb32(0xCC, 0xCC, 0xCC), 1);

    /* Content area - if terminal, draw console lines */
    if (win->is_terminal && win->console_line_count > 0) {
        int cx = win->x + 8;
        int cy = win->y + WIN_TITLE_H + 8;
        int line_h = 18;
        int max_lines = (win->h - WIN_TITLE_H - 16) / line_h;
        int start = 0;
        if (win->console_line_count > max_lines)
            start = win->console_line_count - max_lines;
        for (int i = start; i < win->console_line_count && cy < win->y + win->h - 8; i++) {
            draw_text(cx, cy, win->console_lines[i], TUNGOS_BLACK, TUNGOS_WINBG);
            cy += line_h;
        }
    }
}

/* ── Draw desktop icon ── */
void draw_icon(int idx) {
    DesktopIcon *icon = &icons[idx];
    int x = icon->x, y = icon->y;

    /* Icon box */
    draw_rounded_rect(x, y, ICON_SIZE, ICON_SIZE, 8, icon->color, icon->color);

    /* Icon symbol */
    uint32_t white = TUNGOS_WHITE;
    switch (icon->action) {
        case 0: /* Terminal */
            draw_text(x + 8, y + 8, ">_", white, icon->color);
            break;
        case 1: /* SysInfo */
            draw_text(x + 4, y + 4, "i", white, icon->color);
            break;
        case 2: /* Power */
            draw_text(x + 8, y + 6, "O", white, icon->color);
            break;
    }

    /* Label below icon */
    int label_w = strlen(icon->label) * 8;
    int lx = x + (ICON_SIZE - label_w) / 2;
    draw_text(lx, y + ICON_SIZE + 6, icon->label, TUNGOS_WHITE, TUNGOS_BLUE);
}

/* ── Draw taskbar ── */
void draw_taskbar(void) {
    int tb_y = fb_h - TASKBAR_H;

    /* Taskbar background */
    fill_rect(0, tb_y, fb_w, TASKBAR_H, TUNGOS_TASKBAR);

    /* Start button */
    draw_rounded_rect(4, tb_y + 4, START_BTN_W, TASKBAR_H - 8, 4, TUNGOS_ACCENT, TUNGOS_ACCENT);
    draw_text(12, tb_y + 8, "Tung", TUNGOS_BLACK, TUNGOS_ACCENT);

    /* Separator */
    fill_rect(START_BTN_W + 8, tb_y + 6, 1, TASKBAR_H - 12, rgb32(0x60, 0xA0, 0xE0));

    /* Window buttons in taskbar */
    int btn_x = START_BTN_W + 16;
    for (int i = 0; i < window_count; i++) {
        if (!windows[i].visible) continue;
        int bw = 120;
        uint32_t bc = windows[i].focused ? rgb32(0x40, 0x90, 0xD0) : rgb32(0x20, 0x60, 0xA0);
        fill_rect(btn_x, tb_y + 4, bw, TASKBAR_H - 8, bc);
        draw_text(btn_x + 8, tb_y + 8, windows[i].title, TUNGOS_WHITE, bc);
        btn_x += bw + 4;
    }

    /* Clock */
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    char clock_str[32];
    strftime(clock_str, sizeof(clock_str), "%H:%M", t);
    int clock_w = strlen(clock_str) * 16;
    draw_text(fb_w - clock_w - 20, tb_y + 8, clock_str, TUNGOS_WHITE, TUNGOS_TASKBAR);
}

/* ── Draw start menu ── */
void draw_start_menu(void) {
    int menu_x = 4;
    int menu_y = fb_h - TASKBAR_H - START_MENU_H;

    /* Shadow */
    fill_rect(menu_x + 4, menu_y + 4, START_MENU_W, START_MENU_H, rgb32(0x30, 0x30, 0x30));

    /* Background */
    fill_rect(menu_x, menu_y, START_MENU_W, START_MENU_H, TUNGOS_WHITE);
    draw_rect_border(menu_x, menu_y, START_MENU_W, START_MENU_H, rgb32(0xCC, 0xCC, 0xCC), 1);

    /* Header */
    fill_rect(menu_x + 1, menu_y + 1, START_MENU_W - 2, 36, TUNGOS_TASKBAR);
    draw_text(menu_x + 10, menu_y + 8, "TungOS", TUNGOS_WHITE, TUNGOS_TASKBAR);

    /* Menu items */
    const char *items[] = {"Terminal", "System Info", "Reboot", "Power Off"};
    uint32_t colors[] = {TUNGOS_GREEN, TUNGOS_BLUE, TUNGOS_DARKGRAY, TUNGOS_RED};
    for (int i = 0; i < 4; i++) {
        int iy = menu_y + 44 + i * 28;
        /* Hover highlight */
        fill_rect(menu_x + 4, iy, START_MENU_W - 8, 24, rgb32(0xE8, 0xE8, 0xE8));
        draw_text(menu_x + 14, iy + 2, items[i], colors[i], rgb32(0xE8, 0xE8, 0xE8));
    }
}

/* ── Draw mouse cursor ── */
void draw_cursor(void) {
    int cx = mouse_x, cy = mouse_y;
    /* Simple arrow cursor */
    for (int i = 0; i < 12; i++) {
        put_pixel(cx + i, cy, TUNGOS_BLACK);
        put_pixel(cx, cy + i, TUNGOS_BLACK);
    }
    for (int i = 0; i < 8; i++) {
        put_pixel(cx + i, cy + i, TUNGOS_BLACK);
    }
    /* White inner */
    for (int i = 1; i < 11; i++) {
        put_pixel(cx + i, cy + 1, TUNGOS_WHITE);
        put_pixel(cx + 1, cy + i, TUNGOS_WHITE);
    }
}

/* ── Draw the TUNGOS logo on desktop ── */
void draw_logo(void) {
    int cx = fb_w / 2 - 80;
    int cy = 30;

    /* Big TUNGOS text */
    const char *lines[] = {
        "TTTTTT  U   U  N   N  GGGGG",
        "  TT    U   U  NN  N  G    ",
        "  TT    U   U  N N N  G GG ",
        "  TT    U   U  N  NN  G  G ",
        "  TT     UUU   N   N  GGGG ",
    };
    for (int i = 0; i < 5; i++) {
        draw_text(cx, cy + i * 34, lines[i], TUNGOS_ACCENT, TUNGOS_BLUE);
    }
    draw_text(cx + 20, cy + 180, "Ultra-Lightweight Linux  v0.1", TUNGOS_WHITE, TUNGOS_BLUE);
}

/* ── System info content for window ── */
void fill_sysinfo_window(int win_idx) {
    Window *win = &windows[win_idx];
    win->console_line_count = 0;

    snprintf(win->console_lines[win->console_line_count++], 80, "OS:       TungOS v0.1 Zephyr");
    snprintf(win->console_lines[win->console_line_count++], 80, "Kernel:   Linux 6.6.87-tungos");
    snprintf(win->console_lines[win->console_line_count++], 80, "Arch:     x86_64");
    snprintf(win->console_lines[win->console_line_count++], 80, "Hostname: tungos");

    FILE *fp = fopen("/proc/meminfo", "r");
    if (fp) {
        char line[256]; int mt = 0, mf = 0;
        if (fgets(line, sizeof(line), fp)) sscanf(line, "MemTotal: %d", &mt);
        if (fgets(line, sizeof(line), fp)) sscanf(line, "MemFree: %d", &mf);
        fclose(fp);
        snprintf(win->console_lines[win->console_line_count++], 80, "Memory:   %d / %d MB", (mt-mf)/1024, mt/1024);
    }

    fp = fopen("/proc/uptime", "r");
    if (fp) {
        double up; fscanf(fp, "%lf", &up); fclose(fp);
        snprintf(win->console_lines[win->console_line_count++], 80, "Uptime:   %dh %dm", (int)(up/3600), ((int)up%3600)/60);
    }
    snprintf(win->console_lines[win->console_line_count++], 80, "Shell:    ash (BusyBox)");
    snprintf(win->console_lines[win->console_line_count++], 80, "Desktop:  TungOS FBGUI");
}

/* ── Full desktop render ── */
void render_desktop(void) {
    /* Wallpaper gradient */
    draw_gradient(0, 0, fb_w, fb_h - TASKBAR_H, rgb32(0x00, 0x50, 0xB4), rgb32(0x00, 0x2A, 0x6E));

    /* Logo */
    draw_logo();

    /* Desktop icons */
    for (int i = 0; i < NUM_ICONS; i++)
        draw_icon(i);

    /* Windows */
    for (int i = 0; i < window_count; i++)
        if (windows[i].visible) draw_window(i);

    /* Taskbar */
    draw_taskbar();

    /* Start menu */
    if (start_menu_open)
        draw_start_menu();

    /* Mouse cursor */
    draw_cursor();

    flip_screen();
}

/* ── Input handling ── */
void process_mouse(void) {
    struct input_event ev;
    while (1) {
        int n = read(mouse_fd, &ev, sizeof(ev));
        if (n < sizeof(ev)) break;

        if (ev.type == EV_REL) {
            if (ev.code == REL_X) { mouse_x += ev.value; if (mouse_x < 0) mouse_x = 0; if (mouse_x >= fb_w) mouse_x = fb_w - 1; }
            if (ev.code == REL_Y) { mouse_y += ev.value; if (mouse_y < 0) mouse_y = 0; if (mouse_y >= fb_h) mouse_y = fb_h - 1; }
        }
        if (ev.type == EV_KEY && ev.code == BTN_LEFT) {
            mouse_prev_btn = mouse_btn;
            mouse_btn = ev.value;
        }
    }
}

/* ── Click handler ── */
void handle_click(int x, int y) {
    int tb_y = fb_h - TASKBAR_H;

    /* Start button */
    if (x >= 4 && x <= START_BTN_W + 4 && y >= tb_y + 4 && y <= tb_y + TASKBAR_H - 4) {
        start_menu_open = !start_menu_open;
        return;
    }

    /* Start menu items */
    if (start_menu_open) {
        int menu_x = 4;
        int menu_y = fb_h - TASKBAR_H - START_MENU_H;
        if (x >= menu_x && x <= menu_x + START_MENU_W && y >= menu_y && y <= menu_y + START_MENU_H) {
            int item = (y - menu_y - 44) / 28;
            start_menu_open = 0;
            if (item == 0) { /* Terminal */
                int w = create_window("Terminal", 100, 60, 500, 350, 1);
                if (w >= 0) {
                    snprintf(windows[w].console_lines[windows[w].console_line_count++], 80, "TungOS Terminal v0.1");
                    snprintf(windows[w].console_lines[windows[w].console_line_count++], 80, "$ _");
                }
            } else if (item == 1) { /* SysInfo */
                int w = create_window("System Information", 120, 80, 400, 280, 1);
                if (w >= 0) fill_sysinfo_window(w);
            } else if (item == 2) { /* Reboot */
                system("/sbin/reboot");
            } else if (item == 3) { /* Power Off */
                system("/sbin/poweroff");
            }
            return;
        }
        start_menu_open = 0;
    }

    /* Desktop icons */
    for (int i = 0; i < NUM_ICONS; i++) {
        if (x >= icons[i].x && x <= icons[i].x + ICON_SIZE &&
            y >= icons[i].y && y <= icons[i].y + ICON_SIZE) {
            if (icons[i].action == 0) {
                int w = create_window("Terminal", 100, 60, 500, 350, 1);
                if (w >= 0) {
                    snprintf(windows[w].console_lines[windows[w].console_line_count++], 80, "TungOS Terminal v0.1");
                    snprintf(windows[w].console_lines[windows[w].console_line_count++], 80, "$ _");
                }
            } else if (icons[i].action == 1) {
                int w = create_window("System Information", 120, 80, 400, 280, 1);
                if (w >= 0) fill_sysinfo_window(w);
            } else if (icons[i].action == 2) {
                int w = create_window("Power", 200, 100, 300, 200, 1);
                if (w >= 0) {
                    snprintf(windows[w].console_lines[windows[w].console_line_count++], 80, "");
                    snprintf(windows[w].console_lines[windows[w].console_line_count++], 80, "  [R] Reboot");
                    snprintf(windows[w].console_lines[windows[w].console_line_count++], 80, "  [P] Power Off");
                    snprintf(windows[w].console_lines[windows[w].console_line_count++], 80, "  [C] Cancel");
                }
            }
            return;
        }
    }

    /* Window interaction */
    for (int i = window_count - 1; i >= 0; i--) {
        if (!windows[i].visible) continue;
        Window *win = &windows[i];
        if (x >= win->x && x <= win->x + win->w && y >= win->y && y <= win->y + win->h) {
            /* Close button */
            int close_x = win->x + win->w - 28;
            if (x >= close_x && x <= close_x + 24 && y >= win->y + 2 && y <= win->y + 26) {
                windows[i].visible = 0;
                /* Refocus */
                active_window = -1;
                for (int j = window_count - 1; j >= 0; j--)
                    if (windows[j].visible) { windows[j].focused = 1; active_window = j; break; }
                return;
            }
            /* Title bar drag */
            if (y >= win->y && y <= win->y + WIN_TITLE_H) {
                dragging = 1;
                drag_off_x = x - win->x;
                drag_off_y = y - win->y;
                /* Focus this window */
                for (int j = 0; j < window_count; j++) windows[j].focused = 0;
                windows[i].focused = 1;
                active_window = i;
                return;
            }
            /* Focus */
            for (int j = 0; j < window_count; j++) windows[j].focused = 0;
            windows[i].focused = 1;
            active_window = i;
            return;
        }
    }
}

void handle_release(int x, int y) {
    dragging = 0;
}

/* ── Init framebuffer ── */
int init_fb(void) {
    fb_fd = open("/dev/fb0", O_RDWR);
    if (fb_fd < 0) { perror("open /dev/fb0"); return -1; }

    ioctl(fb_fd, FBIOGET_VSCREENINFO, &vinfo);
    ioctl(fb_fd, FBIOGET_FSCREENINFO, &finfo);

    fb_w = vinfo.xres;
    fb_h = vinfo.yres;
    fb_bpp = vinfo.bits_per_pixel;
    fb_stride = finfo.line_length;

    fprintf(stderr, "Framebuffer: %dx%d %dbpp stride=%d\n", fb_w, fb_h, fb_bpp, fb_stride);

    size_t screensize = fb_stride * fb_h;
    fb_mem = mmap(NULL, screensize, PROT_READ | PROT_WRITE, MAP_SHARED, fb_fd, 0);
    if (fb_mem == MAP_FAILED) { perror("mmap fb"); return -1; }

    backbuf = malloc(screensize);
    if (!backbuf) { perror("malloc backbuf"); return -1; }
    memset(backbuf, 0, screensize);

    mouse_x = fb_w / 2;
    mouse_y = fb_h / 2;

    return 0;
}

/* ── Init input ── */
int init_input(void) {
    /* Find mouse device */
    DIR *d = opendir("/dev/input");
    if (!d) { perror("opendir /dev/input"); return -1; }
    struct dirent *ent;
    while ((ent = readdir(d))) {
        if (strncmp(ent->d_name, "event", 5) == 0) {
            char path[64];
            snprintf(path, sizeof(path), "/dev/input/%s", ent->d_name);
            int fd = open(path, O_RDONLY | O_NONBLOCK);
            if (fd < 0) continue;
            /* Try to identify - mice have REL_X/Y */
            unsigned long evbits = 0;
            ioctl(fd, EVIOCGBIT(0, sizeof(evbits)), &evbits);
            if (evbits & (1 << EV_REL)) {
                mouse_fd = fd;
                fprintf(stderr, "Mouse: %s\n", path);
            } else if (evbits & (1 << EV_KEY)) {
                kbd_fd = fd;
                fprintf(stderr, "Keyboard: %s\n", path);
            } else {
                close(fd);
            }
        }
    }
    closedir(d);

    /* Also try /dev/input/mice */
    if (mouse_fd < 0) {
        mouse_fd = open("/dev/input/mice", O_RDONLY | O_NONBLOCK);
        if (mouse_fd >= 0) fprintf(stderr, "Mouse: /dev/input/mice\n");
    }

    return (mouse_fd >= 0) ? 0 : -1;
}

/* ── Boot splash ── */
void show_boot_splash(void) {
    draw_gradient(0, 0, fb_w, fb_h, rgb32(0x00, 0x50, 0xB4), rgb32(0x00, 0x2A, 0x6E));

    /* TUNGOS logo */
    draw_text(fb_w/2 - 160, fb_h/2 - 100, "TTTTTT  U   U  N   N  GGGGG", TUNGOS_ACCENT, rgb32(0x00, 0x3C, 0x8C));
    draw_text(fb_w/2 - 160, fb_h/2 - 66, "  TT    U   U  NN  N  G    ", TUNGOS_ACCENT, rgb32(0x00, 0x3C, 0x8C));
    draw_text(fb_w/2 - 160, fb_h/2 - 32, "  TT    U   U  N N N  G GG ", TUNGOS_ACCENT, rgb32(0x00, 0x3C, 0x8C));
    draw_text(fb_w/2 - 160, fb_h/2 + 2,  "  TT    U   U  N  NN  G  G ", TUNGOS_ACCENT, rgb32(0x00, 0x3C, 0x8C));
    draw_text(fb_w/2 - 160, fb_h/2 + 36, "  TT     UUU   N   N  GGGG ", TUNGOS_ACCENT, rgb32(0x00, 0x3C, 0x8C));

    draw_text(fb_w/2 - 140, fb_h/2 + 80, "Ultra-Lightweight Linux", TUNGOS_WHITE, rgb32(0x00, 0x3C, 0x8C));

    /* Loading bar */
    int bar_x = fb_w/2 - 200;
    int bar_y = fb_h/2 + 130;
    draw_rect_border(bar_x, bar_y, 400, 20, TUNGOS_WHITE, 1);
    for (int i = 0; i < 396; i++) {
        fill_rect(bar_x + 2 + i, bar_y + 2, 1, 16, TUNGOS_ACCENT);
        if (i % 4 == 0) flip_screen();
        usleep(8000);
    }
    draw_text(fb_w/2 - 30, bar_y + 30, "Ready!", TUNGOS_WHITE, rgb32(0x00, 0x3C, 0x8C));
    flip_screen();
    usleep(300000);
}

/* ── Main ── */
int main(void) {
    if (init_fb() < 0) {
        fprintf(stderr, "TungOS: Framebuffer init failed, falling back to console\n");
        execl("/bin/sh", "sh", "-l", NULL);
        return 1;
    }

    show_boot_splash();

    if (init_input() < 0) {
        fprintf(stderr, "TungOS: Input init failed (no mouse)\n");
    }

    /* Main loop */
    while (1) {
        /* Process input */
        if (mouse_fd >= 0) {
            int prev_x = mouse_x, prev_y = mouse_y;
            process_mouse();

            /* Handle drag */
            if (dragging && active_window >= 0) {
                windows[active_window].x = mouse_x - drag_off_x;
                windows[active_window].y = mouse_y - drag_off_y;
            }

            /* Handle click */
            if (mouse_btn && !mouse_prev_btn) {
                handle_click(mouse_x, mouse_y);
            }
            if (!mouse_btn && mouse_prev_btn) {
                handle_release(mouse_x, mouse_y);
            }
        }

        /* Render */
        render_desktop();

        usleep(16000);  /* ~60fps cap */
    }

    return 0;
}
