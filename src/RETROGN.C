/*
 * RETROGN.C  -  RetroGun: MS-DOS Top-Down Pixel-Art Shooter
 *
 * Target platform : MS-DOS 6.2+, 386+ CPU, VGA graphics card
 * Compiler        : DJGPP 2.x (GCC for DOS)
 *
 * Build on Linux (cross-compile):
 *   i586-pc-msdosdjgpp-gcc -O2 -Wall -o RETROGN.EXE RETROGN.C
 *
 * Build on DOS with DJGPP:
 *   gcc -O2 -Wall -o RETROGN.EXE RETROGN.C
 *
 * Run:
 *   RETROGN.EXE   (requires CWSDPMI.EXE in the same directory)
 *
 * Controls:
 *   Arrow keys    Move ship
 *   Space         Fire
 *   ESC           Quit
 */

#include <conio.h>
#include <dpmi.h>
#include <go32.h>
#include <pc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/nearptr.h>
#include <time.h>

/* =========================================================================
 * Constants
 * ========================================================================= */

#define SCREEN_W    320
#define SCREEN_H    200
#define VGA_BASE    0xA0000UL

/* Play-field height (the HUD occupies the bottom strip) */
#define HUD_Y       184
#define PLAY_H      HUD_Y

/* Object limits */
#define MAX_BULLETS     24
#define MAX_ENEMIES     20
#define MAX_PARTICLES   64
#define NUM_STARS       90

/* Player tunables */
#define PLAYER_SPEED        2
#define PLAYER_SHOOT_DELAY  8   /* frames between shots */
#define PLAYER_INVINCIBLE   60  /* frames of invincibility after a hit */
#define PLAYER_START_LIVES  3

/* Bullet tunables */
#define PLAYER_BULLET_SPEED  6
#define ENEMY_BULLET_SPEED   3

/* Sprite sizes */
#define PLAYER_W   13
#define PLAYER_H   11
#define ENEMY_W    11
#define ENEMY_H    11
#define PBULLET_W   2
#define PBULLET_H   6
#define EBULLET_W   2
#define EBULLET_H   5

/* Keyboard scan codes */
#define SC_ESC    0x01
#define SC_SPACE  0x39
#define SC_UP     0x48
#define SC_DOWN   0x50
#define SC_LEFT   0x4B
#define SC_RIGHT  0x4D
#define SC_ENTER  0x1C

/* VGA palette indices used by sprites and HUD */
#define C_BLACK     0
#define C_BLUE      1
#define C_GREEN     2
#define C_CYAN      3
#define C_RED       4
#define C_MAGENTA   5
#define C_BROWN     6
#define C_LGRAY     7
#define C_DGRAY     8
#define C_LBLUE     9
#define C_LGREEN   10
#define C_LCYAN    11
#define C_LRED     12
#define C_LMAG     13
#define C_YELLOW   14
#define C_WHITE    15

/* =========================================================================
 * Data structures
 * ========================================================================= */

typedef enum { ENEMY_SCOUT = 0, ENEMY_FIGHTER, ENEMY_BOMBER } EnemyType;

typedef struct {
    int x, y;
    int vx, vy;
    int active;
    int enemy;   /* 1 = fired by enemy */
} Bullet;

typedef struct {
    int x, y;
    int vx, vy;
    int active;
    int hp;
    int shoot_cd;   /* cooldown counter */
    int phase;      /* movement phase */
    EnemyType type;
} Enemy;

typedef struct {
    int x, y;
    int vx, vy;
    int life;       /* remaining frames */
    unsigned char color;
} Particle;

typedef struct {
    int x, y;
    int speed;      /* parallax layer: 1=slow, 2=medium, 3=fast */
} Star;

/* =========================================================================
 * Global state
 * ========================================================================= */

/* Display */
static unsigned char *vga_mem;
static unsigned char back_buf[SCREEN_W * SCREEN_H];

/* Keyboard */
static volatile unsigned char keys[128];
static _go32_dpmi_seginfo kbd_old_handler;
static _go32_dpmi_seginfo kbd_new_handler;

/* Player */
static int px, py;
static int p_lives;
static long p_score;
static long p_hiscore;
static int p_shoot_cd;
static int p_inv;          /* invincibility frames */
static int p_dead;         /* death animation counter */

/* Objects */
static Bullet   bullets[MAX_BULLETS];
static Enemy    enemies[MAX_ENEMIES];
static Particle particles[MAX_PARTICLES];
static Star     stars[NUM_STARS];

/* Wave / spawn */
static int wave;
static int enemies_left;   /* enemies still to spawn this wave */
static int spawn_timer;
static int spawn_interval; /* frames between spawns */

/* =========================================================================
 * Pixel-art sprites  (0 = transparent)
 * ========================================================================= */

/* Player ship  13w x 11h  pointing upward */
static const unsigned char spr_player[PLAYER_H][PLAYER_W] = {
    { 0, 0, 0, 0, 0, 0,15, 0, 0, 0, 0, 0, 0 },
    { 0, 0, 0, 0, 0,15,15,15, 0, 0, 0, 0, 0 },
    { 0, 0, 0, 0,15,11,11,11,15, 0, 0, 0, 0 },
    { 0, 0, 0,15, 0,11,15,11, 0,15, 0, 0, 0 },
    { 0, 0,15,15,15, 0,15, 0,15,15,15, 0, 0 },
    { 0,15,11,11,11,11,11,11,11,11,11,15, 0 },
    { 0, 0,15,15, 0,15, 0,15, 0,15,15, 0, 0 },
    { 0, 0, 0,15, 0,11,15,11, 0,15, 0, 0, 0 },
    { 0, 0, 0,15,15, 0, 0, 0,15,15, 0, 0, 0 },
    { 0, 0, 0,15, 0, 0, 0, 0, 0,15, 0, 0, 0 },
    { 0, 0,15,15, 0, 0, 0, 0, 0,15,15, 0, 0 }
};

/* Scout enemy  11w x 11h  pointing downward (toward player) */
static const unsigned char spr_scout[ENEMY_H][ENEMY_W] = {
    { 0, 0,12, 0, 0, 0, 0, 0,12, 0, 0 },
    { 0, 0,12,12, 0, 0, 0,12,12, 0, 0 },
    { 0,12, 4,12,12, 0,12,12, 4,12, 0 },
    { 0,12, 4, 4,12,12,12, 4, 4,12, 0 },
    { 12, 4, 4, 4, 4,12, 4, 4, 4, 4,12 },
    { 12, 4,12, 4, 4, 4, 4, 4,12, 4,12 },
    { 12, 4, 4, 4, 4,12, 4, 4, 4, 4,12 },
    { 0,12, 4, 4, 0, 0, 0, 4, 4,12, 0 },
    { 0, 0,12, 0, 0, 4, 0, 0,12, 0, 0 },
    { 0, 0, 0, 0,12, 4,12, 0, 0, 0, 0 },
    { 0, 0, 0, 0, 0,12, 0, 0, 0, 0, 0 }
};

/* Fighter enemy  11w x 11h */
static const unsigned char spr_fighter[ENEMY_H][ENEMY_W] = {
    { 0, 0, 0, 0, 0,14, 0, 0, 0, 0, 0 },
    { 0, 0, 0, 0,14,14,14, 0, 0, 0, 0 },
    { 0, 0, 6,14,14,14,14,14, 6, 0, 0 },
    { 0, 6, 6,14, 6,14, 6,14, 6, 6, 0 },
    { 6, 6, 6, 6,14,14,14, 6, 6, 6, 6 },
    { 6,14,14,14,14,14,14,14,14,14, 6 },
    { 6, 6, 6, 6,14,14,14, 6, 6, 6, 6 },
    { 0, 6, 6,14, 6,14, 6,14, 6, 6, 0 },
    { 0, 0, 6,14, 0, 6, 0,14, 6, 0, 0 },
    { 0, 0, 0, 6, 0,14, 0, 6, 0, 0, 0 },
    { 0, 0, 0, 0, 0,14, 0, 0, 0, 0, 0 }
};

/* Bomber enemy  11w x 11h  (slow but tough) */
static const unsigned char spr_bomber[ENEMY_H][ENEMY_W] = {
    { 0, 0, 0, 5, 5, 5, 5, 5, 0, 0, 0 },
    { 0, 0, 5,13, 5, 5, 5,13, 5, 0, 0 },
    { 0, 5,13,13,13, 5,13,13,13, 5, 0 },
    { 5,13,13, 5,13,13,13, 5,13,13, 5 },
    { 5,13, 5,13,13,13,13,13, 5,13, 5 },
    { 5,13,13,13,13,13,13,13,13,13, 5 },
    { 5,13, 5,13,13,13,13,13, 5,13, 5 },
    { 5,13,13, 5, 5,13, 5, 5,13,13, 5 },
    { 0, 5,13,13, 0, 5, 0,13,13, 5, 0 },
    { 0, 0, 5, 5, 0,13, 0, 5, 5, 0, 0 },
    { 0, 0, 0, 5, 0, 5, 0, 5, 0, 0, 0 }
};

/* =========================================================================
 * Minimal 8x8 bitmap font  (ASCII 32-90: space through 'Z')
 * Standard IBM PC ROM font subset
 * ========================================================================= */
static const unsigned char font8x8[59][8] = {
    /* 32 ' ' */ {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    /* 33 '!' */ {0x18,0x18,0x18,0x18,0x00,0x18,0x00,0x00},
    /* 34 '"' */ {0x66,0x66,0x00,0x00,0x00,0x00,0x00,0x00},
    /* 35 '#' */ {0x66,0xFF,0x66,0x66,0xFF,0x66,0x00,0x00},
    /* 36 '$' */ {0x18,0x3E,0x60,0x3C,0x06,0x7C,0x18,0x00},
    /* 37 '%' */ {0x62,0x66,0x0C,0x18,0x30,0x66,0x46,0x00},
    /* 38 '&' */ {0x3C,0x66,0x3C,0x38,0x67,0x66,0x3F,0x00},
    /* 39 '\''*/ {0x06,0x0C,0x18,0x00,0x00,0x00,0x00,0x00},
    /* 40 '(' */ {0x0C,0x18,0x30,0x30,0x30,0x18,0x0C,0x00},
    /* 41 ')' */ {0x30,0x18,0x0C,0x0C,0x0C,0x18,0x30,0x00},
    /* 42 '*' */ {0x00,0x66,0x3C,0xFF,0x3C,0x66,0x00,0x00},
    /* 43 '+' */ {0x00,0x18,0x18,0x7E,0x18,0x18,0x00,0x00},
    /* 44 ',' */ {0x00,0x00,0x00,0x00,0x00,0x18,0x18,0x30},
    /* 45 '-' */ {0x00,0x00,0x00,0x7E,0x00,0x00,0x00,0x00},
    /* 46 '.' */ {0x00,0x00,0x00,0x00,0x00,0x18,0x00,0x00},
    /* 47 '/' */ {0x03,0x06,0x0C,0x18,0x30,0x60,0x00,0x00},
    /* 48 '0' */ {0x3C,0x66,0x6E,0x76,0x66,0x66,0x3C,0x00},
    /* 49 '1' */ {0x18,0x38,0x18,0x18,0x18,0x18,0x7E,0x00},
    /* 50 '2' */ {0x3C,0x66,0x06,0x1C,0x30,0x66,0x7E,0x00},
    /* 51 '3' */ {0x3C,0x66,0x06,0x1C,0x06,0x66,0x3C,0x00},
    /* 52 '4' */ {0x06,0x1E,0x36,0x66,0x7F,0x06,0x06,0x00},
    /* 53 '5' */ {0x7E,0x60,0x7C,0x06,0x06,0x66,0x3C,0x00},
    /* 54 '6' */ {0x3C,0x66,0x60,0x7C,0x66,0x66,0x3C,0x00},
    /* 55 '7' */ {0x7E,0x66,0x0C,0x18,0x18,0x18,0x18,0x00},
    /* 56 '8' */ {0x3C,0x66,0x66,0x3C,0x66,0x66,0x3C,0x00},
    /* 57 '9' */ {0x3C,0x66,0x66,0x3E,0x06,0x66,0x3C,0x00},
    /* 58 ':' */ {0x00,0x18,0x00,0x00,0x00,0x18,0x00,0x00},
    /* 59 ';' */ {0x00,0x18,0x00,0x00,0x00,0x18,0x18,0x30},
    /* 60 '<' */ {0x0E,0x18,0x30,0x60,0x30,0x18,0x0E,0x00},
    /* 61 '=' */ {0x00,0x00,0x7E,0x00,0x7E,0x00,0x00,0x00},
    /* 62 '>' */ {0x70,0x18,0x0C,0x06,0x0C,0x18,0x70,0x00},
    /* 63 '?' */ {0x3C,0x66,0x06,0x1C,0x18,0x00,0x18,0x00},
    /* 64 '@' */ {0x3E,0x63,0x6F,0x69,0x6F,0x60,0x3E,0x00},
    /* 65 'A' */ {0x18,0x3C,0x66,0x7E,0x66,0x66,0x66,0x00},
    /* 66 'B' */ {0x7C,0x66,0x66,0x7C,0x66,0x66,0x7C,0x00},
    /* 67 'C' */ {0x3C,0x66,0x60,0x60,0x60,0x66,0x3C,0x00},
    /* 68 'D' */ {0x78,0x6C,0x66,0x66,0x66,0x6C,0x78,0x00},
    /* 69 'E' */ {0x7E,0x60,0x60,0x78,0x60,0x60,0x7E,0x00},
    /* 70 'F' */ {0x7E,0x60,0x60,0x78,0x60,0x60,0x60,0x00},
    /* 71 'G' */ {0x3C,0x66,0x60,0x6E,0x66,0x66,0x3C,0x00},
    /* 72 'H' */ {0x66,0x66,0x66,0x7E,0x66,0x66,0x66,0x00},
    /* 73 'I' */ {0x3C,0x18,0x18,0x18,0x18,0x18,0x3C,0x00},
    /* 74 'J' */ {0x1E,0x0C,0x0C,0x0C,0x0C,0x6C,0x38,0x00},
    /* 75 'K' */ {0x66,0x6C,0x78,0x70,0x78,0x6C,0x66,0x00},
    /* 76 'L' */ {0x60,0x60,0x60,0x60,0x60,0x60,0x7E,0x00},
    /* 77 'M' */ {0x63,0x77,0x7F,0x6B,0x63,0x63,0x63,0x00},
    /* 78 'N' */ {0x66,0x76,0x7E,0x7E,0x6E,0x66,0x66,0x00},
    /* 79 'O' */ {0x3C,0x66,0x66,0x66,0x66,0x66,0x3C,0x00},
    /* 80 'P' */ {0x7C,0x66,0x66,0x7C,0x60,0x60,0x60,0x00},
    /* 81 'Q' */ {0x3C,0x66,0x66,0x66,0x66,0x3C,0x0E,0x00},
    /* 82 'R' */ {0x7C,0x66,0x66,0x7C,0x78,0x6C,0x66,0x00},
    /* 83 'S' */ {0x3C,0x66,0x60,0x3C,0x06,0x66,0x3C,0x00},
    /* 84 'T' */ {0x7E,0x18,0x18,0x18,0x18,0x18,0x18,0x00},
    /* 85 'U' */ {0x66,0x66,0x66,0x66,0x66,0x66,0x3C,0x00},
    /* 86 'V' */ {0x66,0x66,0x66,0x66,0x66,0x3C,0x18,0x00},
    /* 87 'W' */ {0x63,0x63,0x63,0x6B,0x7F,0x77,0x63,0x00},
    /* 88 'X' */ {0x66,0x66,0x3C,0x18,0x3C,0x66,0x66,0x00},
    /* 89 'Y' */ {0x66,0x66,0x66,0x3C,0x18,0x18,0x18,0x00},
    /* 90 'Z' */ {0x7E,0x06,0x0C,0x18,0x30,0x60,0x7E,0x00}
};

/* =========================================================================
 * Keyboard interrupt handler
 * ========================================================================= */

static void kbd_isr(void)
{
    unsigned char sc = inportb(0x60);
    if (sc < 0x80)
        keys[sc] = 1;
    else
        keys[sc & 0x7F] = 0;
    outportb(0x20, 0x20); /* send EOI to PIC */
}

static void kbd_install(void)
{
    _go32_dpmi_get_protected_mode_interrupt_vector(9, &kbd_old_handler);
    /* pm_offset stores the ISR address. On 32-bit DOS, int and pointer are both
     * 4 bytes, so the double cast (via size_t) avoids a compiler warning when
     * building on a 64-bit host while remaining correct on the 32-bit target. */
    kbd_new_handler.pm_offset   = (int)(size_t)kbd_isr;
    kbd_new_handler.pm_selector = _go32_my_cs();
    _go32_dpmi_allocate_iret_wrapper(&kbd_new_handler);
    _go32_dpmi_set_protected_mode_interrupt_vector(9, &kbd_new_handler);
}

static void kbd_remove(void)
{
    _go32_dpmi_set_protected_mode_interrupt_vector(9, &kbd_old_handler);
    _go32_dpmi_free_iret_wrapper(&kbd_new_handler);
}

/* =========================================================================
 * BIOS timer tick counter (runs at ~18.2 Hz)
 * ========================================================================= */

static unsigned long get_bios_ticks(void)
{
    __dpmi_regs r;
    r.x.ax = 0x0000;
    __dpmi_int(0x1A, &r);
    return ((unsigned long)r.x.cx << 16) | r.x.dx;
}

/* =========================================================================
 * VGA / display
 * ========================================================================= */

static void set_video_mode(int mode)
{
    __dpmi_regs r;
    memset(&r, 0, sizeof(r));
    r.x.ax = mode;
    __dpmi_int(0x10, &r);
}

static void display_init(void)
{
    __djgpp_nearptr_enable();
    vga_mem = (unsigned char *)(VGA_BASE + __djgpp_conventional_base);
    set_video_mode(0x13); /* 320x200 256-color */
}

static void display_shutdown(void)
{
    set_video_mode(0x03); /* 80x25 text mode */
    __djgpp_nearptr_disable();
}

/* Blit back-buffer to VGA memory */
static void flip(void)
{
    memcpy(vga_mem, back_buf, SCREEN_W * SCREEN_H);
}

static void cls(unsigned char color)
{
    memset(back_buf, color, SCREEN_W * SCREEN_H);
}

static void put_pixel(int x, int y, unsigned char c)
{
    if (x >= 0 && x < SCREEN_W && y >= 0 && y < SCREEN_H)
        back_buf[y * SCREEN_W + x] = c;
}

/* Draw a filled rectangle */
static void fill_rect(int x, int y, int w, int h, unsigned char c)
{
    int i, j;
    for (j = y; j < y + h; j++)
        for (i = x; i < x + w; i++)
            put_pixel(i, j, c);
}

/* Draw a horizontal line */
static void hline(int x, int y, int w, unsigned char c)
{
    int i;
    for (i = x; i < x + w; i++)
        put_pixel(i, y, c);
}

/* Draw a single 8x8 character (col = foreground, 0=transparent bg) */
static void draw_char(int x, int y, char ch, unsigned char col)
{
    int row, bit;
    int idx = (int)(unsigned char)ch - 32;
    if (idx < 0 || idx >= 59) return;
    for (row = 0; row < 8; row++) {
        unsigned char bits = font8x8[idx][row];
        for (bit = 7; bit >= 0; bit--) {
            if (bits & (1 << bit))
                put_pixel(x + (7 - bit), y + row, col);
        }
    }
}

/* Draw a null-terminated string */
static void draw_str(int x, int y, const char *s, unsigned char col)
{
    while (*s) {
        draw_char(x, y, *s, col);
        x += 8;
        s++;
    }
}

/* Draw a non-negative integer */
static void draw_int(int x, int y, long n, unsigned char col)
{
    char buf[16];
    int i = 14;
    buf[15] = '\0';
    if (n == 0) { buf[i--] = '0'; }
    while (n > 0) { buf[i--] = (char)('0' + n % 10); n /= 10; }
    draw_str(x, y, buf + i + 1, col);
}

/* =========================================================================
 * Sprite rendering
 * ========================================================================= */

/* Draw a sprite (2-D byte array) with color 0 treated as transparent */
#define DRAW_SPRITE(sx, sy, spr, sw, sh) \
do { \
    int _r, _c; \
    for (_r = 0; _r < (sh); _r++) \
        for (_c = 0; _c < (sw); _c++) { \
            unsigned char _px = (spr)[_r][_c]; \
            if (_px) put_pixel((sx)+_c, (sy)+_r, _px); \
        } \
} while(0)

/* =========================================================================
 * Random helpers
 * ========================================================================= */

static int rnd(int n) { return n > 0 ? rand() % n : 0; }

/* =========================================================================
 * Particles
 * ========================================================================= */

static void spawn_explosion(int cx, int cy, int count,
                            unsigned char c1, unsigned char c2)
{
    int i;
    for (i = 0; i < MAX_PARTICLES; i++) {
        if (!particles[i].life && count > 0) {
            int vx = rnd(7) - 3;
            int vy = rnd(7) - 3;
            particles[i].x    = cx;
            particles[i].y    = cy;
            particles[i].vx   = vx;
            particles[i].vy   = vy;
            particles[i].life = 8 + rnd(12);
            particles[i].color = (rnd(2) == 0) ? c1 : c2;
            count--;
        }
    }
}

static void update_particles(void)
{
    int i;
    for (i = 0; i < MAX_PARTICLES; i++) {
        if (particles[i].life > 0) {
            particles[i].x += particles[i].vx;
            particles[i].y += particles[i].vy;
            particles[i].life--;
        }
    }
}

static void draw_particles(void)
{
    int i;
    for (i = 0; i < MAX_PARTICLES; i++) {
        if (particles[i].life > 0)
            put_pixel(particles[i].x, particles[i].y, particles[i].color);
    }
}

/* =========================================================================
 * Stars (parallax scrolling background)
 * ========================================================================= */

static void init_stars(void)
{
    int i;
    for (i = 0; i < NUM_STARS; i++) {
        stars[i].x     = rnd(SCREEN_W);
        stars[i].y     = rnd(PLAY_H);
        stars[i].speed = 1 + rnd(3);
    }
}

static void update_stars(void)
{
    int i;
    for (i = 0; i < NUM_STARS; i++) {
        stars[i].y += stars[i].speed;
        if (stars[i].y >= PLAY_H) {
            stars[i].y = 0;
            stars[i].x = rnd(SCREEN_W);
        }
    }
}

static void draw_stars(void)
{
    int i;
    static const unsigned char star_colors[3] = { C_DGRAY, C_LGRAY, C_WHITE };
    for (i = 0; i < NUM_STARS; i++)
        put_pixel(stars[i].x, stars[i].y,
                  star_colors[stars[i].speed - 1]);
}

/* =========================================================================
 * Bullets
 * ========================================================================= */

static void fire_player_bullet(void)
{
    int i;
    for (i = 0; i < MAX_BULLETS; i++) {
        if (!bullets[i].active) {
            bullets[i].x      = px + PLAYER_W / 2;
            bullets[i].y      = py - PBULLET_H;
            bullets[i].vx     = 0;
            bullets[i].vy     = -PLAYER_BULLET_SPEED;
            bullets[i].active = 1;
            bullets[i].enemy  = 0;
            return;
        }
    }
}

static void fire_enemy_bullet(int ex, int ey)
{
    int i;
    for (i = 0; i < MAX_BULLETS; i++) {
        if (!bullets[i].active) {
            bullets[i].x      = ex + ENEMY_W / 2;
            bullets[i].y      = ey + ENEMY_H;
            bullets[i].vx     = 0;
            bullets[i].vy     = ENEMY_BULLET_SPEED;
            bullets[i].active = 1;
            bullets[i].enemy  = 1;
            return;
        }
    }
}

static void update_bullets(void)
{
    int i;
    for (i = 0; i < MAX_BULLETS; i++) {
        if (!bullets[i].active) continue;
        bullets[i].x += bullets[i].vx;
        bullets[i].y += bullets[i].vy;
        if (bullets[i].y < -PBULLET_H || bullets[i].y > SCREEN_H
         || bullets[i].x < 0 || bullets[i].x > SCREEN_W)
            bullets[i].active = 0;
    }
}

static void draw_bullets(void)
{
    int i;
    for (i = 0; i < MAX_BULLETS; i++) {
        if (!bullets[i].active) continue;
        if (!bullets[i].enemy) {
            /* Player bullet: bright cyan */
            fill_rect(bullets[i].x, bullets[i].y,
                      PBULLET_W, PBULLET_H, C_LCYAN);
            put_pixel(bullets[i].x, bullets[i].y, C_WHITE);
        } else {
            /* Enemy bullet: red */
            fill_rect(bullets[i].x, bullets[i].y,
                      EBULLET_W, EBULLET_H, C_LRED);
            put_pixel(bullets[i].x, bullets[i].y, C_YELLOW);
        }
    }
}

/* =========================================================================
 * Enemies
 * ========================================================================= */

static void spawn_enemy(void)
{
    int i;
    EnemyType t;
    int hp, shoot_cd;
    for (i = 0; i < MAX_ENEMIES; i++) {
        if (enemies[i].active) continue;

        /* Determine type based on wave */
        if (wave <= 2)
            t = ENEMY_SCOUT;
        else if (wave <= 4)
            t = (rnd(3) == 0) ? ENEMY_FIGHTER : ENEMY_SCOUT;
        else
            t = (EnemyType)rnd(3);

        switch (t) {
        case ENEMY_SCOUT:   hp = 1; shoot_cd = 40 + rnd(20); break;
        case ENEMY_FIGHTER: hp = 2; shoot_cd = 30 + rnd(20); break;
        default:            hp = 4; shoot_cd = 20 + rnd(10); break;
        }

        enemies[i].x        = rnd(SCREEN_W - ENEMY_W - 4) + 2;
        enemies[i].y        = -(ENEMY_H + rnd(40));
        enemies[i].vx       = (rnd(3) - 1);
        enemies[i].vy       = 1 + rnd(wave > 3 ? 2 : 1);
        enemies[i].active   = 1;
        enemies[i].hp       = hp;
        enemies[i].shoot_cd = shoot_cd;
        enemies[i].phase    = rnd(120);
        enemies[i].type     = t;
        enemies_left--;
        return;
    }
}

static void update_enemies(void)
{
    int i;
    for (i = 0; i < MAX_ENEMIES; i++) {
        if (!enemies[i].active) continue;

        enemies[i].phase++;

        /* Side-to-side sinusoidal drift */
        if (enemies[i].phase % 4 == 0) {
            int dir = ((enemies[i].phase / 4) % 60 < 30) ? 1 : -1;
            enemies[i].x += dir * (enemies[i].type == ENEMY_SCOUT ? 2 : 1);
        }

        enemies[i].y += enemies[i].vy;

        /* Clamp horizontal to screen */
        if (enemies[i].x < 0)              enemies[i].x = 0;
        if (enemies[i].x > SCREEN_W - ENEMY_W) enemies[i].x = SCREEN_W - ENEMY_W;

        /* Enemy shooting */
        if (enemies[i].y > 0) {
            enemies[i].shoot_cd--;
            if (enemies[i].shoot_cd <= 0) {
                fire_enemy_bullet(enemies[i].x, enemies[i].y);
                enemies[i].shoot_cd = 30 + rnd(30);
            }
        }

        /* Flew off bottom */
        if (enemies[i].y > PLAY_H + ENEMY_H)
            enemies[i].active = 0;
    }
}

static void draw_enemies(void)
{
    int i;
    for (i = 0; i < MAX_ENEMIES; i++) {
        if (!enemies[i].active) continue;
        switch (enemies[i].type) {
        case ENEMY_SCOUT:
            DRAW_SPRITE(enemies[i].x, enemies[i].y, spr_scout,
                        ENEMY_W, ENEMY_H);
            break;
        case ENEMY_FIGHTER:
            DRAW_SPRITE(enemies[i].x, enemies[i].y, spr_fighter,
                        ENEMY_W, ENEMY_H);
            break;
        default:
            DRAW_SPRITE(enemies[i].x, enemies[i].y, spr_bomber,
                        ENEMY_W, ENEMY_H);
            break;
        }
        /* Health bar for bombers */
        if (enemies[i].type == ENEMY_BOMBER) {
            hline(enemies[i].x, enemies[i].y - 2,
                  enemies[i].hp * 2, C_LGREEN);
        }
    }
}

/* =========================================================================
 * Collision detection
 * ========================================================================= */

/* Simple AABB overlap test */
static int rect_hit(int ax, int ay, int aw, int ah,
                    int bx, int by, int bw, int bh)
{
    return (ax < bx + bw && ax + aw > bx &&
            ay < by + bh && ay + ah > by);
}

static int check_collisions(void)
{
    int i, j;
    int player_hit = 0;

    for (i = 0; i < MAX_BULLETS; i++) {
        if (!bullets[i].active) continue;

        if (!bullets[i].enemy) {
            /* Player bullet vs enemies */
            for (j = 0; j < MAX_ENEMIES; j++) {
                if (!enemies[j].active) continue;
                if (rect_hit(bullets[i].x, bullets[i].y, PBULLET_W, PBULLET_H,
                             enemies[j].x, enemies[j].y, ENEMY_W, ENEMY_H)) {
                    bullets[i].active = 0;
                    enemies[j].hp--;
                    if (enemies[j].hp <= 0) {
                        /* Score by type */
                        switch (enemies[j].type) {
                        case ENEMY_SCOUT:   p_score += 10; break;
                        case ENEMY_FIGHTER: p_score += 25; break;
                        default:            p_score += 50; break;
                        }
                        spawn_explosion(enemies[j].x + ENEMY_W / 2,
                                        enemies[j].y + ENEMY_H / 2,
                                        12, C_YELLOW, C_LRED);
                        enemies[j].active = 0;
                    } else {
                        spawn_explosion(enemies[j].x + ENEMY_W / 2,
                                        enemies[j].y + ENEMY_H / 2,
                                        4, C_WHITE, C_YELLOW);
                    }
                    break;
                }
            }
        } else {
            /* Enemy bullet vs player */
            if (p_inv == 0 && !p_dead &&
                rect_hit(bullets[i].x, bullets[i].y, EBULLET_W, EBULLET_H,
                         px, py, PLAYER_W, PLAYER_H)) {
                bullets[i].active = 0;
                player_hit = 1;
            }
        }
    }

    /* Enemy body vs player */
    if (p_inv == 0 && !p_dead) {
        for (j = 0; j < MAX_ENEMIES; j++) {
            if (!enemies[j].active) continue;
            if (rect_hit(px, py, PLAYER_W, PLAYER_H,
                         enemies[j].x, enemies[j].y, ENEMY_W, ENEMY_H)) {
                player_hit = 1;
                spawn_explosion(enemies[j].x + ENEMY_W / 2,
                                enemies[j].y + ENEMY_H / 2,
                                10, C_LRED, C_YELLOW);
                enemies[j].active = 0;
            }
        }
    }

    return player_hit;
}

/* =========================================================================
 * Player
 * ========================================================================= */

static void player_init(void)
{
    px = SCREEN_W / 2 - PLAYER_W / 2;
    py = PLAY_H - PLAYER_H - 10;
    p_shoot_cd = 0;
    p_inv      = 0;
    p_dead     = 0;
}

static void update_player(void)
{
    if (p_dead) {
        p_dead--;
        return;
    }

    /* Movement */
    if (keys[SC_LEFT]  && px > 0)           px -= PLAYER_SPEED;
    if (keys[SC_RIGHT] && px < SCREEN_W - PLAYER_W) px += PLAYER_SPEED;
    if (keys[SC_UP]    && py > 0)           py -= PLAYER_SPEED;
    if (keys[SC_DOWN]  && py < PLAY_H - PLAYER_H)   py += PLAYER_SPEED;

    /* Shoot */
    if (p_shoot_cd > 0) p_shoot_cd--;
    if (keys[SC_SPACE] && p_shoot_cd == 0) {
        fire_player_bullet();
        p_shoot_cd = PLAYER_SHOOT_DELAY;
    }

    /* Invincibility countdown */
    if (p_inv > 0) p_inv--;
}

static void draw_player(void)
{
    if (p_dead) return;
    /* Blink during invincibility */
    if (p_inv > 0 && (p_inv / 4) % 2 == 0) return;
    DRAW_SPRITE(px, py, spr_player, PLAYER_W, PLAYER_H);
}

/* =========================================================================
 * HUD
 * ========================================================================= */

static void draw_hud(void)
{
    int i;
    /* HUD background bar */
    fill_rect(0, HUD_Y, SCREEN_W, SCREEN_H - HUD_Y, C_DGRAY);
    hline(0, HUD_Y, SCREEN_W, C_LBLUE);

    /* Score */
    draw_str(2, HUD_Y + 4, "SCORE:", C_CYAN);
    draw_int(50, HUD_Y + 4, p_score, C_WHITE);

    /* Hi-score */
    draw_str(110, HUD_Y + 4, "BEST:", C_CYAN);
    draw_int(150, HUD_Y + 4, p_hiscore, C_YELLOW);

    /* Wave */
    draw_str(210, HUD_Y + 4, "WAVE:", C_CYAN);
    draw_int(250, HUD_Y + 4, (long)wave, C_LGREEN);

    /* Lives */
    draw_str(270, HUD_Y + 4, "LIVES:", C_CYAN);
    for (i = 0; i < p_lives; i++) {
        /* Small ship icon for each life */
        int lx = 314 - i * 10;
        put_pixel(lx + 2, HUD_Y + 4, C_LCYAN);
        put_pixel(lx + 1, HUD_Y + 5, C_WHITE);
        put_pixel(lx + 2, HUD_Y + 5, C_WHITE);
        put_pixel(lx + 3, HUD_Y + 5, C_WHITE);
        put_pixel(lx + 1, HUD_Y + 6, C_LGRAY);
        put_pixel(lx + 3, HUD_Y + 6, C_LGRAY);
    }
}

/* =========================================================================
 * Wave management
 * ========================================================================= */

static void start_wave(int w)
{
    wave           = w;
    enemies_left   = 6 + w * 3;    /* more enemies each wave */
    spawn_interval = 60 - w * 6;   /* spawn faster each wave */
    if (spawn_interval < 15) spawn_interval = 15;
    spawn_timer    = 0;
}

static int wave_clear(void)
{
    int i;
    if (enemies_left > 0) return 0;
    for (i = 0; i < MAX_ENEMIES; i++)
        if (enemies[i].active) return 0;
    return 1;
}

/* =========================================================================
 * Overlay messages
 * ========================================================================= */

static void show_message(const char *line1, const char *line2, int ticks)
{
    unsigned long t_end;
    int len1, len2, x1, x2;

    len1 = (int)strlen(line1);
    len2 = (int)strlen(line2);
    x1 = (SCREEN_W - len1 * 8) / 2;
    x2 = (SCREEN_W - len2 * 8) / 2;

    t_end = get_bios_ticks() + (unsigned long)ticks;
    while (get_bios_ticks() < t_end && !keys[SC_ESC]) {
        cls(C_BLACK);
        draw_stars();
        draw_str(x1, 90, line1, C_YELLOW);
        if (line2[0]) draw_str(x2, 102, line2, C_WHITE);
        flip();
    }
}

/* =========================================================================
 * Title screen
 * ========================================================================= */

static void title_screen(void)
{
    int frame = 0;
    keys[SC_SPACE] = 0;
    keys[SC_ESC]   = 0;

    while (!keys[SC_SPACE] && !keys[SC_ESC]) {
        int i;
        cls(C_BLACK);
        update_stars();
        draw_stars();

        /* Blinking stars decoration */
        for (i = 0; i < 20; i++)
            put_pixel(rnd(SCREEN_W), rnd(PLAY_H), C_WHITE);

        /* Title */
        draw_str(80,  60, "RETRO", C_YELLOW);
        draw_str(80,  72, " GUN",  C_LRED);

        /* Subtitle */
        draw_str(68, 90, "TOP-DOWN SHOOTER", C_LCYAN);

        /* Blinking prompt */
        if ((frame / 18) % 2 == 0)
            draw_str(64, 120, "PRESS SPACE TO PLAY", C_WHITE);

        /* Controls */
        draw_str(44, 145, "ARROWS:MOVE  SPACE:FIRE", C_LGRAY);
        draw_str(76, 155, "ESC:QUIT", C_LGRAY);

        flip();
        frame++;

        /* Wait one BIOS tick (~55ms) */
        {
            unsigned long t = get_bios_ticks();
            while (get_bios_ticks() == t) {}
        }
    }
}

/* =========================================================================
 * Game over / win screen
 * ========================================================================= */

static void end_screen(int won)
{
    unsigned long t_end = get_bios_ticks() + 110; /* ~6 seconds */
    keys[SC_SPACE] = 0;

    while (get_bios_ticks() < t_end && !keys[SC_SPACE] && !keys[SC_ESC]) {
        cls(C_BLACK);
        draw_stars();
        if (won) {
            draw_str(96,  80, "YOU WIN!", C_YELLOW);
        } else {
            draw_str(88,  80, "GAME OVER", C_LRED);
        }
        draw_str(60, 96, "SCORE:", C_CYAN);
        draw_int(108, 96, p_score, C_WHITE);
        if (p_score > p_hiscore) {
            draw_str(76, 110, "NEW BEST!", C_YELLOW);
        }
        draw_str(60, 125, "PRESS SPACE", C_LGRAY);
        flip();
        {
            unsigned long t = get_bios_ticks();
            while (get_bios_ticks() == t) {}
        }
    }
}

/* =========================================================================
 * Main game loop
 * ========================================================================= */

#define TOTAL_WAVES  5

static int run_game(void)
{
    int i;

    /* Reset game state */
    p_lives = PLAYER_START_LIVES;
    p_score = 0;
    player_init();

    memset(bullets,   0, sizeof(bullets));
    memset(enemies,   0, sizeof(enemies));
    memset(particles, 0, sizeof(particles));

    start_wave(1);

    for (;;) {
        /* ------ tick timer (~18 fps) ---- */
        {
            unsigned long t = get_bios_ticks();
            while (get_bios_ticks() == t) {}
        }

        /* ------ ESC check ---- */
        if (keys[SC_ESC]) return 0;

        /* ------ Spawn enemies ---- */
        if (enemies_left > 0) {
            spawn_timer--;
            if (spawn_timer <= 0) {
                spawn_enemy();
                spawn_timer = spawn_interval;
            }
        }

        /* ------ Update ---- */
        update_stars();
        update_player();
        update_bullets();
        update_enemies();
        update_particles();

        /* ------ Collisions ---- */
        if (check_collisions()) {
            /* Player was hit */
            p_lives--;
            p_inv = PLAYER_INVINCIBLE;
            spawn_explosion(px + PLAYER_W / 2, py + PLAYER_H / 2,
                            16, C_LRED, C_YELLOW);
            if (p_lives <= 0) {
                /* Death animation */
                for (i = 0; i < 36; i++) {
                    cls(C_BLACK);
                    draw_stars();
                    draw_enemies();
                    draw_particles();
                    draw_hud();
                    update_particles();
                    flip();
                    {
                        unsigned long t = get_bios_ticks();
                        while (get_bios_ticks() == t) {}
                    }
                }
                if (p_score > p_hiscore) p_hiscore = p_score;
                end_screen(0);
                return 1; /* game over, want to restart */
            }
        }

        /* ------ Wave clear? ---- */
        if (wave_clear()) {
            if (wave >= TOTAL_WAVES) {
                if (p_score > p_hiscore) p_hiscore = p_score;
                end_screen(1);
                return 1;
            } else {
                char msg[20];
                snprintf(msg, sizeof(msg), "WAVE %d CLEAR", wave);
                show_message(msg, "", 36);
                /* Bonus life every 2 waves */
                if (wave % 2 == 0 && p_lives < 5) p_lives++;
                start_wave(wave + 1);
                memset(bullets, 0, sizeof(bullets));
            }
        }

        /* ------ Draw ---- */
        cls(C_BLACK);
        draw_stars();
        draw_enemies();
        draw_bullets();
        draw_player();
        draw_particles();
        draw_hud();
        flip();
    }
}

/* =========================================================================
 * Entry point
 * ========================================================================= */

int main(void)
{
    srand((unsigned)get_bios_ticks());

    /* Install keyboard handler */
    memset((void *)keys, 0, sizeof(keys));
    kbd_install();

    /* Set up display */
    display_init();

    /* Init star field */
    init_stars();

    /* Game loop with title/restart */
    p_hiscore = 0;
    do {
        title_screen();
        if (keys[SC_ESC]) break;
    } while (run_game());

    /* Restore system */
    display_shutdown();
    kbd_remove();

    printf("\nThanks for playing RetroGun!\n");
    printf("Final high score: %ld\n", p_hiscore);
    return 0;
}
