/*
 * TungOS Desktop Environment
 * Pure ANSI/VT100 TUI - no ncurses needed
 * Windows/Ubuntu style desktop in text mode
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <termios.h>
#include <signal.h>
#include <dirent.h>
#include <sys/ioctl.h>

/* Colors */
#define COLOR_BLACK   0
#define COLOR_RED     1
#define COLOR_GREEN   2
#define COLOR_YELLOW  3
#define COLOR_BLUE    4
#define COLOR_MAGENTA 5
#define COLOR_CYAN    6
#define COLOR_WHITE   7
#define COLOR_BRIGHT_BLACK   8
#define COLOR_BRIGHT_RED     9
#define COLOR_BRIGHT_GREEN   10
#define COLOR_BRIGHT_YELLOW  11
#define COLOR_BRIGHT_BLUE    12
#define COLOR_BRIGHT_MAGENTA 13
#define COLOR_BRIGHT_CYAN    14
#define COLOR_BRIGHT_WHITE   15

/* TungOS Theme */
#define BG_PRIMARY     COLOR_BLUE
#define BG_DARK        COLOR_BLACK
#define BG_TASKBAR     COLOR_CYAN
#define BG_WINDOW      COLOR_WHITE
#define FG_TITLE       COLOR_WHITE
#define FG_TASKBAR     COLOR_BLACK
#define FG_ACCENT      COLOR_YELLOW
#define FG_DESKTOP     COLOR_WHITE

/* Box drawing */
#define TL "\xe2\x94\x8c"  /* ┌ */
#define TR "\xe2\x94\x90"  /* ┐ */
#define BL "\xe2\x94\x94"  /* └ */
#define BR "\xe2\x94\x98"  /* ┘ */
#define HZ "\xe2\x94\x80"  /* ─ */
#define VT "\xe2\x94\x82"  /* │ */
#define T_TEE "\xe2\x94\xac" /* ┬ */
#define B_TEE "\xe2\x94\xb4" /* ┴ */
#define L_TEE "\xe2\x94\x9c" /* ├ */
#define R_TEE "\xe2\x94\xa4" /* ┤ */
#define CROSS "\xe2\x94\xbc" /* ┼ */

/* ANSI helpers */
#define ESC "\033"
#define CSI "\033["
#define RESET "\033[0m"
#define BOLD "\033[1m"
#define DIM "\033[2m"
#define REVERSE "\033[7m"

static struct termios orig_term;
static int term_rows = 25;
static int term_cols = 80;

void set_color(int fg, int bg) {
    if (fg >= 8)
        printf(CSI"1;%dm", 90 + fg - 8);
    else
        printf(CSI"%dm", 30 + fg);
    if (bg >= 8)
        printf(CSI"%dm", 100 + bg - 8);
    else
        printf(CSI"%dm", 40 + bg);
}

void set_fg(int fg) {
    if (fg >= 8)
        printf(CSI"1;%dm", 90 + fg - 8);
    else
        printf(CSI"%dm", 30 + fg);
}

void set_bg(int bg) {
    if (bg >= 8)
        printf(CSI"%dm", 100 + bg - 8);
    else
        printf(CSI"%dm", 40 + bg);
}

void clear_screen(void) {
    printf(CSI"2J"CSI"H");
}

void move_cursor(int row, int col) {
    printf(CSI"%d;%dH", row, col);
}

void hide_cursor(void) {
    printf(CSI"?25l");
}

void show_cursor(void) {
    printf(CSI"?25h");
}

void get_term_size(void) {
    int fd = open("/dev/tty", O_RDONLY);
    if (fd < 0) return;
    struct winsize {
        unsigned short ws_row, ws_col, ws_xpixel, ws_ypixel;
    } ws;
    if (ioctl(fd, 0x5413, &ws) == 0) {  /* TIOCGWINSZ */
        term_rows = ws.ws_row;
        term_cols = ws.ws_col;
    }
    close(fd);
}

void restore_term(void) {
    tcsetattr(0, TCSANOW, &orig_term);
    show_cursor();
    printf(RESET CSI"?1000l");
    clear_screen();
    fflush(stdout);
}

void init_term(void) {
    tcgetattr(0, &orig_term);
    atexit(restore_term);
    struct termios new_term = orig_term;
    new_term.c_lflag &= ~(ICANON | ECHO | ISIG);
    new_term.c_cc[VMIN] = 1;
    new_term.c_cc[VTIME] = 0;
    tcsetattr(0, TCSANOW, &new_term);
    get_term_size();
    hide_cursor();
    /* Enable mouse tracking */
    printf(CSI"?1000h");
}

/* Draw a filled rectangle */
void draw_rect(int row, int col, int height, int width, int fg, int bg) {
    set_color(fg, bg);
    for (int r = row; r < row + height; r++) {
        move_cursor(r, col);
        for (int c = 0; c < width; c++) {
            putchar(' ');
        }
    }
    fflush(stdout);
}

/* Draw a box with border */
void draw_box(int row, int col, int height, int width, int border_fg, int border_bg, int fill_bg, const char *title) {
    /* Title bar */
    set_color(border_fg, border_bg);
    move_cursor(row, col);
    printf(TL);
    for (int i = 0; i < width - 2; i++) printf(HZ);
    printf(TR);

    /* Title text */
    if (title) {
        int tlen = strlen(title);
        int tstart = col + (width - tlen) / 2;
        move_cursor(row, tstart);
        set_color(border_fg, border_bg);
        printf(" %s ", title);
    }

    /* Body */
    for (int r = row + 1; r < row + height - 1; r++) {
        set_color(border_fg, fill_bg);
        move_cursor(r, col);
        printf(VT);
        set_bg(fill_bg);
        for (int i = 0; i < width - 2; i++) putchar(' ');
        set_color(border_fg, fill_bg);
        printf(VT);
    }

    /* Bottom */
    set_color(border_fg, fill_bg);
    move_cursor(row + height - 1, col);
    printf(BL);
    for (int i = 0; i < width - 2; i++) printf(HZ);
    printf(BR);
    fflush(stdout);
}

/* Draw the taskbar */
void draw_taskbar(void) {
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    char time_str[32];
    strftime(time_str, sizeof(time_str), "%H:%M", t);

    char date_str[32];
    strftime(date_str, sizeof(date_str), "%a %b %d", t);

    /* Taskbar background */
    draw_rect(term_rows, 1, 1, term_cols, FG_TASKBAR, BG_TASKBAR);

    /* TungOS button */
    move_cursor(term_rows, 2);
    set_color(COLOR_BLACK, COLOR_YELLOW);
    printf(" TungOS ");
    
    /* Separator */
    set_color(FG_TASKBAR, BG_TASKBAR);
    printf(" | ");

    /* Status */
    set_fg(COLOR_BLACK);
    
    /* Uptime */
    FILE *fp = fopen("/proc/uptime", "r");
    if (fp) {
        double up;
        fscanf(fp, "%lf", &up);
        fclose(fp);
        int hours = (int)(up / 3600);
        int mins = ((int)up % 3600) / 60;
        printf("Up: %dh %dm", hours, mins);
    }

    /* Memory */
    fp = fopen("/proc/meminfo", "r");
    if (fp) {
        char line[256];
        int memtotal = 0, memfree = 0;
        if (fgets(line, sizeof(line), fp)) sscanf(line, "MemTotal: %d", &memtotal);
        if (fgets(line, sizeof(line), fp)) sscanf(line, "MemFree: %d", &memfree);
        fclose(fp);
        int used = (memtotal - memfree) / 1024;
        int total = memtotal / 1024;
        printf("  Mem: %d/%dMB", used, total);
    }

    /* Clock - right aligned */
    int clock_len = strlen(date_str) + strlen(time_str) + 5;
    move_cursor(term_rows, term_cols - clock_len);
    set_color(COLOR_BLACK, BG_TASKBAR);
    printf("%s  %s ", date_str, time_str);

    fflush(stdout);
}

/* Draw desktop background with logo */
void draw_desktop(void) {
    /* Blue background */
    set_color(FG_DESKTOP, BG_PRIMARY);
    clear_screen();

    /* Draw TUNGOS text logo centered */
    int logo_start_row = 3;
    int center_col = (term_cols - 50) / 2;

    set_color(COLOR_BRIGHT_YELLOW, BG_PRIMARY);
    move_cursor(logo_start_row, center_col);
    printf("  ████████╗██╗   ██╗ ██████╗ ██╗  ██╗  ");
    move_cursor(logo_start_row+1, center_col);
    printf("  ╚══██╔══╝██║   ██║██╔═══██╗╚██╗██╔╝  ");
    move_cursor(logo_start_row+2, center_col);
    printf("     ██║   ██║   ██║██║   ██║ ╚███╔╝   ");
    move_cursor(logo_start_row+3, center_col);
    printf("     ██║   ╚██╗ ██╔╝██║   ██║ ██╔██╗   ");
    move_cursor(logo_start_row+4, center_col);
    printf("     ██║    ╚████╔╝ ╚██████╔╝██╔╝ ██╗  ");
    move_cursor(logo_start_row+5, center_col);
    printf("     ╚═╝     ╚═══╝   ╚═════╝ ╚═╝  ╚═╝  ");

    /* Subtitle */
    set_color(COLOR_WHITE, BG_PRIMARY);
    move_cursor(logo_start_row + 7, center_col + 5);
    printf("Ultra-Lightweight Linux  v0.1 Zephyr");

    /* Desktop icons - vertical list on the left */
    int icon_row = logo_start_row + 10;
    int icon_col = 4;

    /* Icon: Terminal */
    set_color(COLOR_BRIGHT_GREEN, BG_PRIMARY);
    move_cursor(icon_row, icon_col);
    printf("┌─────────┐");
    move_cursor(icon_row+1, icon_col);
    printf("│ >_      │");
    move_cursor(icon_row+2, icon_col);
    printf("│  $ help │");
    move_cursor(icon_row+3, icon_col);
    printf("└─────────┘");
    set_color(COLOR_WHITE, BG_PRIMARY);
    move_cursor(icon_row+4, icon_col+1);
    printf("Terminal");

    /* Icon: System Info */
    icon_row += 7;
    set_color(COLOR_BRIGHT_CYAN, BG_PRIMARY);
    move_cursor(icon_row, icon_col);
    printf("┌─────────┐");
    move_cursor(icon_row+1, icon_col);
    printf("│ CPU: OK │");
    move_cursor(icon_row+2, icon_col);
    printf("│ RAM: OK │");
    move_cursor(icon_row+3, icon_col);
    printf("└─────────┘");
    set_color(COLOR_WHITE, BG_PRIMARY);
    move_cursor(icon_row+4, icon_col+1);
    printf("SysInfo");

    /* Icon: Power */
    icon_row += 7;
    set_color(COLOR_BRIGHT_RED, BG_PRIMARY);
    move_cursor(icon_row, icon_col);
    printf("┌─────────┐");
    move_cursor(icon_row+1, icon_col);
    printf("│  ⏻      │");
    move_cursor(icon_row+2, icon_col);
    printf("│  Power  │");
    move_cursor(icon_row+3, icon_col);
    printf("└─────────┘");
    set_color(COLOR_WHITE, BG_PRIMARY);
    move_cursor(icon_row+4, icon_col+1);
    printf("Power");

    /* Right side info panel */
    int info_col = term_cols - 30;
    int info_row = logo_start_row + 10;

    draw_box(info_row, info_col, 12, 26, COLOR_WHITE, COLOR_BLUE, COLOR_BLUE, "System");

    set_color(COLOR_BRIGHT_WHITE, BG_PRIMARY);
    move_cursor(info_row + 2, info_col + 2);
    printf("OS:     TungOS v0.1");
    move_cursor(info_row + 3, info_col + 2);
    printf("Kernel: Linux 6.6.87");
    move_cursor(info_row + 4, info_col + 2);
    printf("Arch:   x86_64");
    move_cursor(info_row + 5, info_col + 2);
    printf("Host:   tungos");

    {
    FILE *fp2 = fopen("/proc/meminfo", "r");
    if (fp2) {
        char line[256];
        int memtotal = 0;
        if (fgets(line, sizeof(line), fp2)) sscanf(line, "MemTotal: %d", &memtotal);
        fclose(fp2);
        move_cursor(info_row + 7, info_col + 2);
        printf("RAM:    %d MB", memtotal / 1024);
    }
    }

    {
    FILE *fp3 = fopen("/proc/cpuinfo", "r");
    if (fp3) {
        char line[256];
        while (fgets(line, sizeof(line), fp3)) {
            if (strncmp(line, "model name", 10) == 0) {
                char *p = strchr(line, ':');
                if (p) {
                    p++;
                    while (*p == ' ') p++;
                    char *nl = strchr(p, '\n');
                    if (nl) *nl = 0;
                    move_cursor(info_row + 8, info_col + 2);
                    printf("CPU:    %.20s", p);
                }
                break;
            }
        }
        fclose(fp3);
    }
    }

    /* Bottom hint */
    set_color(COLOR_BRIGHT_WHITE, BG_PRIMARY);
    move_cursor(term_rows - 2, (term_cols - 40) / 2);
    printf("Press [1] Terminal  [2] SysInfo  [3] Power");

    draw_taskbar();
    fflush(stdout);
}

/* Show the start menu */
void show_start_menu(void) {
    int menu_row = term_rows - 8;
    int menu_col = 1;
    int menu_w = 22;
    int menu_h = 8;

    draw_box(menu_row, menu_col, menu_h, menu_w, COLOR_WHITE, COLOR_BLUE, COLOR_BLUE, " TungOS ");

    set_color(COLOR_BRIGHT_WHITE, BG_PRIMARY);
    move_cursor(menu_row + 2, menu_col + 2);
    printf("1. Terminal");
    move_cursor(menu_row + 3, menu_col + 2);
    printf("2. System Info");
    move_cursor(menu_row + 4, menu_col + 2);
    printf("3. Reboot");
    move_cursor(menu_row + 5, menu_col + 2);
    printf("4. Power Off");
    move_cursor(menu_row + 6, menu_col + 2);
    printf("5. Back to Desktop");

    fflush(stdout);
}

/* Run an interactive shell in a "window" */
void open_terminal(void) {
    restore_term();
    clear_screen();

    printf(CSI"1;37;44m");
    printf("  TungOS Terminal - type 'exit' to return to desktop  \n");
    printf(RESET);
    printf("\n");

    /* Start a shell */
    pid_t pid = fork();
    if (pid == 0) {
        execl("/bin/sh", "sh", "-l", NULL);
        exit(1);
    }
    int status;
    waitpid(pid, &status, 0);

    /* Return to desktop */
    init_term();
    draw_desktop();
}

/* Show system info window */
void show_sysinfo(void) {
    int win_row = 4;
    int win_col = (term_cols - 50) / 2;
    int win_w = 50;
    int win_h = 16;

    draw_box(win_row, win_col, win_h, win_w, COLOR_WHITE, COLOR_BLUE, COLOR_BLUE, " System Information ");

    set_color(COLOR_BRIGHT_WHITE, BG_PRIMARY);
    int r = win_row + 2;
    move_cursor(r, win_col + 3); printf("OS:        TungOS v0.1 Zephyr");
    move_cursor(++r, win_col + 3); printf("Kernel:    Linux 6.6.87-tungos");
    move_cursor(++r, win_col + 3); printf("Arch:      x86_64");
    move_cursor(++r, win_col + 3); printf("Hostname:  tungos");

    FILE *fp = fopen("/proc/meminfo", "r");
    if (fp) {
        char line[256];
        int memtotal = 0, memfree = 0;
        if (fgets(line, sizeof(line), fp)) sscanf(line, "MemTotal: %d", &memtotal);
        if (fgets(line, sizeof(line), fp)) sscanf(line, "MemFree: %d", &memfree);
        fclose(fp);
        move_cursor(++r, win_col + 3);
        printf("Memory:    %d/%d MB used", (memtotal - memfree)/1024, memtotal/1024);
    }

    fp = fopen("/proc/uptime", "r");
    if (fp) {
        double up;
        fscanf(fp, "%lf", &up);
        fclose(fp);
        int h = (int)(up / 3600);
        int m = ((int)up % 3600) / 60;
        int s = (int)up % 60;
        move_cursor(++r, win_col + 3);
        printf("Uptime:    %dh %dm %ds", h, m, s);
    }

    fp = fopen("/proc/cpuinfo", "r");
    if (fp) {
        char line[256];
        while (fgets(line, sizeof(line), fp)) {
            if (strncmp(line, "model name", 10) == 0) {
                char *p = strchr(line, ':');
                if (p) { p++; while(*p==' ')p++; char *nl=strchr(p,'\n'); if(nl)*nl=0;
                move_cursor(++r, win_col + 3); printf("CPU:       %s", p); }
                break;
            }
        }
        fclose(fp);
    }

    move_cursor(++r, win_col + 3); printf("Shell:     ash (BusyBox)");
    move_cursor(++r, win_col + 3); printf("Packages:  50+ applets");

    set_color(COLOR_YELLOW, BG_PRIMARY);
    move_cursor(win_row + win_h - 2, win_col + (win_w - 20) / 2);
    printf("Press any key to close");

    fflush(stdout);

    /* Wait for key */
    char c;
    read(0, &c, 1);
}

/* Power menu */
void show_power_menu(void) {
    int win_row = 8;
    int win_col = (term_cols - 36) / 2;
    int win_w = 36;
    int win_h = 8;

    draw_box(win_row, win_col, win_h, win_w, COLOR_WHITE, COLOR_RED, COLOR_RED, " Power ");

    set_color(COLOR_BRIGHT_WHITE, COLOR_RED);
    move_cursor(win_row + 2, win_col + 3);
    printf("  [R] Reboot");
    move_cursor(win_row + 3, win_col + 3);
    printf("  [P] Power Off");
    move_cursor(win_row + 4, win_col + 3);
    printf("  [C] Cancel");

    set_color(COLOR_YELLOW, COLOR_RED);
    move_cursor(win_row + 6, win_col + (win_w - 18) / 2);
    printf("Choose an option:");

    fflush(stdout);

    char c;
    read(0, &c, 1);

    if (c == 'r' || c == 'R') {
        restore_term();
        execl("/sbin/reboot", "reboot", NULL);
    } else if (c == 'p' || c == 'P') {
        restore_term();
        execl("/sbin/poweroff", "poweroff", NULL);
    }
    /* Cancel - redraw desktop */
    draw_desktop();
}

/* Boot splash screen */
void show_boot_splash(void) {
    clear_screen();
    set_color(COLOR_BRIGHT_YELLOW, BG_PRIMARY);

    int center = (term_cols - 50) / 2;
    int row = term_rows / 2 - 5;

    move_cursor(row, center);
    printf("  ████████╗██╗   ██╗ ██████╗ ██╗  ██╗  ");
    move_cursor(row+1, center);
    printf("  ╚══██╔══╝██║   ██║██╔═══██╗╚██╗██╔╝  ");
    move_cursor(row+2, center);
    printf("     ██║   ██║   ██║██║   ██║ ╚███╔╝   ");
    move_cursor(row+3, center);
    printf("     ██║   ╚██╗ ██╔╝██║   ██║ ██╔██╗   ");
    move_cursor(row+4, center);
    printf("     ██║    ╚████╔╝ ╚██████╔╝██╔╝ ██╗  ");
    move_cursor(row+5, center);
    printf("     ╚═╝     ╚═══╝   ╚═════╝ ╚═╝  ╚═╝  ");

    set_color(COLOR_WHITE, BG_PRIMARY);
    move_cursor(row + 7, center + 8);
    printf("Ultra-Lightweight Linux");

    /* Loading bar */
    set_color(COLOR_BRIGHT_WHITE, BG_PRIMARY);
    move_cursor(row + 10, center + 5);
    printf("[");
    int bar_w = 40;
    for (int i = 0; i < bar_w; i++) {
        printf(" ");
    }
    printf("]");

    fflush(stdout);

    /* Animate loading */
    for (int i = 0; i < bar_w; i++) {
        usleep(30000);
        move_cursor(row + 10, center + 6 + i);
        set_color(COLOR_BRIGHT_YELLOW, BG_PRIMARY);
        printf("=");
        fflush(stdout);
    }

    set_color(COLOR_BRIGHT_GREEN, BG_PRIMARY);
    move_cursor(row + 12, center + 16);
    printf("Ready!");
    fflush(stdout);
    usleep(300000);
}

/* Main desktop loop */
void desktop_loop(void) {
    draw_desktop();

    while (1) {
        /* Update taskbar clock */
        draw_taskbar();

        char c = 0;
        int n = read(0, &c, 1);
        if (n <= 0) continue;

        switch (c) {
            case '1':
                open_terminal();
                break;
            case '2':
                show_sysinfo();
                draw_desktop();
                break;
            case '3':
                show_power_menu();
                break;
            case 's': case 'S':
                show_start_menu();
                /* Wait for sub-choice */
                read(0, &c, 1);
                switch(c) {
                    case '1': open_terminal(); break;
                    case '2': show_sysinfo(); draw_desktop(); break;
                    case '3': restore_term(); execl("/sbin/reboot","reboot",NULL); break;
                    case '4': restore_term(); execl("/sbin/poweroff","poweroff",NULL); break;
                    case '5': draw_desktop(); break;
                    default: draw_desktop(); break;
                }
                break;
            case 'q':
                restore_term();
                return;
            default:
                break;
        }
    }
}

int main(void) {
    init_term();

    /* Boot splash */
    show_boot_splash();

    /* Enter desktop */
    desktop_loop();

    restore_term();
    return 0;
}
