/*
 * analemma: screensaver of the analemma in the Temple of Orithena, from "Anathem".
 *
 * A hole in the dome throws the sun on the floor at noon. The screensaver shows the
 * temple now, by the real date and time, then today's noon, then a year of noons
 * going round the figure of eight inlaid in the floor.
 *
 *   analemma            full screen on every display; any key or mouse movement quits
 *   analemma --window   in a window; Esc or Q quits
 *   Analemma.scr /s     Windows screensaver conventions
 */
#include <SDL.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* The pictures (drawn with Gemini, prepared by tools/make_sprites.py) are built
 * into the program as PNG and JPEG files and decoded at start-up. */
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wunused-but-set-variable"
#pragma GCC diagnostic ignored "-Wmisleading-indentation"
#endif
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_NO_STDIO
#define STBI_NO_THREAD_LOCALS /* else Windows builds need libwinpthread-1.dll */
#include "stb_image.h"
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

#ifdef _WIN32
#define ART_SECTION ".section .rdata,\"dr\"\n"
#else
#define ART_SECTION ".section .rodata\n"
#endif
#define ART(name, file) \
    __asm__(ART_SECTION ".global art_" #name "\nart_" #name ":\n.incbin \"assets/art/" file "\"\n" \
            ".global art_" #name "_end\nart_" #name "_end:\n.text\n"); \
    extern const unsigned char art_##name[], art_##name##_end[];
ART(temple, "temple.jpg")
ART(banner, "banner.png")
ART(pool, "pool.jpg")
ART(shaft, "shaft.jpg")
ART(lead_wait, "lead_wait.png")
ART(lead_up, "lead_up.png")
ART(sing1, "sing1.png")
ART(sing2, "sing2.png")
ART(sing3, "sing3.png")
ART(sing4, "sing4.png")

enum { SP_TEMPLE, SP_BANNER, SP_POOL, SP_SHAFT, SP_LEAD_WAIT, SP_LEAD_UP, SP_SING1, SP_SING2, SP_SING3, SP_SING4, NSPRITE };
static struct { const unsigned char *file, *end; int w, h; unsigned char *px; } sprite[NSPRITE] = {
    {art_temple, art_temple_end},
    {art_banner, art_banner_end},
    {art_pool, art_pool_end},
    {art_shaft, art_shaft_end},
    {art_lead_wait, art_lead_wait_end},
    {art_lead_up, art_lead_up_end},
    {art_sing1, art_sing1_end},
    {art_sing2, art_sing2_end},
    {art_sing3, art_sing3_end},
    {art_sing4, art_sing4_end},
};
static SDL_Texture **cur_tex; /* the pictures, for the renderer being drawn to */

static void load_sprites(void)
{
    for (int i = 0; i < NSPRITE; i++) {
        int n;
        sprite[i].px = stbi_load_from_memory(sprite[i].file, (int)(sprite[i].end - sprite[i].file), &sprite[i].w, &sprite[i].h, &n, 4);
        if (!sprite[i].px) { sprite[i].w = sprite[i].h = 1; }
    }
}
static void make_textures(SDL_Renderer *r, SDL_Texture **tex)
{
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "linear");
    for (int i = 0; i < NSPRITE; i++) {
        tex[i] = NULL;
        if (!sprite[i].px) continue;
        tex[i] = SDL_CreateTexture(r, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, sprite[i].w, sprite[i].h);
        if (!tex[i]) continue;
        SDL_UpdateTexture(tex[i], NULL, sprite[i].px, sprite[i].w * 4);
        /* the pool and the shaft are light painted on black, and are added to what is behind them */
        SDL_SetTextureBlendMode(tex[i], i == SP_POOL || i == SP_SHAFT ? SDL_BLENDMODE_ADD : SDL_BLENDMODE_BLEND);
    }
}

/* ---- options ---- */
static int opt_window = 0;
static double opt_speed = 1.0;
static uint64_t opt_seed = 0;
static const char *opt_snapshot = NULL;
static double opt_time = 30;
static int opt_w = 1920, opt_h = 1200;

/* ---- colours ---- */
typedef struct { Uint8 r, g, b; } Rgb;
static const Rgb BG = {14, 12, 10}, INK = {46, 32, 22}, CHORD = {130, 44, 30}, PAPER = {253, 246, 226};
static const Rgb BRASS = {214, 170, 84};

/* ---- geometry buffer ---- */
static SDL_Vertex *vb;
static int *ib;
static int nv, ni, vcap, icap;

static void vb_need(int v, int i)
{
    if (nv + v > vcap) { vcap = (nv + v) * 2; vb = realloc(vb, sizeof *vb * vcap); }
    if (ni + i > icap) { icap = (ni + i) * 2; ib = realloc(ib, sizeof *ib * icap); }
}
static inline SDL_Color col(Rgb c, double a)
{
    SDL_Color o = {c.r, c.g, c.b, (Uint8)(a < 0 ? 0 : a > 1 ? 255 : a * 255)};
    return o;
}
static inline void vert(double x, double y, SDL_Color c)
{
    vb[nv].position.x = (float)x; vb[nv].position.y = (float)y;
    vb[nv].color = c; vb[nv].tex_coord.x = vb[nv].tex_coord.y = 0;
    nv++;
}
static void quad4(double x0, double y0, double x1, double y1, double x2, double y2, double x3, double y3,
                  SDL_Color c0, SDL_Color c1, SDL_Color c2, SDL_Color c3)
{
    vb_need(4, 6);
    int b = nv;
    vert(x0, y0, c0); vert(x1, y1, c1); vert(x2, y2, c2); vert(x3, y3, c3);
    ib[ni++] = b; ib[ni++] = b + 1; ib[ni++] = b + 2; ib[ni++] = b; ib[ni++] = b + 2; ib[ni++] = b + 3;
}
static void quad(double x0, double y0, double x1, double y1, double x2, double y2, double x3, double y3, SDL_Color c)
{
    quad4(x0, y0, x1, y1, x2, y2, x3, y3, c, c, c, c);
}
static void rect(double x, double y, double w, double h, SDL_Color c) { quad(x, y, x + w, y, x + w, y + h, x, y + h, c); }
static void disc(double cx, double cy, double r, SDL_Color centre, SDL_Color rim)
{
    int n = r < 4 ? 10 : r < 30 ? 28 : 56;
    vb_need(n + 1, 3 * n);
    int b = nv;
    vert(cx, cy, centre);
    for (int i = 0; i < n; i++) vert(cx + r * cos(i * 2 * M_PI / n), cy + r * sin(i * 2 * M_PI / n), rim);
    for (int i = 0; i < n; i++) { ib[ni++] = b; ib[ni++] = b + 1 + i; ib[ni++] = b + 1 + (i + 1) % n; }
}
/* a stroke with round ends */
static void line(double x0, double y0, double x1, double y1, double w, SDL_Color c)
{
    double dx = x1 - x0, dy = y1 - y0, l = hypot(dx, dy);
    if (l > 1e-9) {
        double nx = -dy / l * w / 2, ny = dx / l * w / 2;
        quad(x0 + nx, y0 + ny, x1 + nx, y1 + ny, x1 - nx, y1 - ny, x0 - nx, y0 - ny, c);
    }
    if (w > 2.5) { disc(x0, y0, w / 2, c, c); disc(x1, y1, w / 2, c, c); }
}
__attribute__((unused)) static void ring(double cx, double cy, double r, double w, SDL_Color c)
{
    for (int i = 0; i < 64; i++) {
        double a = i * M_PI / 32, b = (i + 1) * M_PI / 32;
        line(cx + cos(a) * r, cy + sin(a) * r, cx + cos(b) * r, cy + sin(b) * r, w, c);
    }
}
static void flush(SDL_Renderer *r)
{
    if (ni) SDL_RenderGeometry(r, NULL, vb, nv, ib, ni);
    nv = ni = 0;
}

static SDL_Renderer *ren; /* where the current frame is being drawn */

/* a picture stretched over a rectangle; sx0..sx1 picks a slice of it across (0..1) */
static void pic_part(int which, double x, double y, double w, double h, double sx0, double sx1, double tilt, int flip)
{
    flush(ren);
    if (!cur_tex || !cur_tex[which]) return;
    SDL_Rect src = {(int)(sx0 * sprite[which].w), 0, (int)((sx1 - sx0) * sprite[which].w), sprite[which].h};
    SDL_FRect dst = {(float)x, (float)y, (float)w, (float)h};
    SDL_FPoint pivot = {(float)(w / 2), (float)h};
    SDL_RenderCopyExF(ren, cur_tex[which], &src, &dst, tilt, &pivot, flip ? SDL_FLIP_HORIZONTAL : SDL_FLIP_NONE);
}
/* a picture `tall` high, standing with the point (ax, ay) of it (0..1 across and down) at (x, y) */
static void pic(int which, double x, double y, double tall, double ax, double ay)
{
    double w = tall * sprite[which].w / sprite[which].h;
    pic_part(which, x - w * ax, y - tall * ay, w, tall, 0, 1, 0, 0);
}

/* ---- lettering: pen strokes on a 4 x 6 grid, drawn with a round nib ---- */
static const char *GLYPH[] = {
    "A06 02 20 42 46|04 44", "B00 06 36 45 44 33 03|33 42 41 30 00", "C41 30 10 01 05 16 36 45",
    "D00 06 26 44 42 20 00", "E40 00 06 46|03 33", "F40 00 06|03 33", "G41 30 10 01 05 16 36 45 43 23",
    "H00 06|40 46|03 43", "I20 26|10 30|16 36", "J40 45 36 16 05", "K00 06|40 03 46", "L00 06 46",
    "M06 00 23 40 46", "N06 00 46 40", "O10 30 41 45 36 16 05 01 10", "P06 00 30 41 42 33 03",
    "Q10 30 41 45 36 16 05 01 10|24 46", "R06 00 30 41 42 33 03|23 46", "S41 30 10 01 02 13 33 44 45 36 16 05",
    "T00 40|20 26", "U00 05 16 36 45 40", "V00 26 40", "W00 16 23 36 40", "X00 46|40 06", "Y00 23 40|23 26",
    "Z00 40 06 46", "010 30 41 45 36 16 05 01 10", "111 20 26|16 36", "201 10 30 41 42 06 46",
    "301 10 30 41 42 33 23|33 44 45 36 16 05", "436 30 04 44", "540 00 03 33 44 45 36 16 05",
    "630 10 01 05 16 36 45 44 33 03", "700 40 16", "813 02 01 10 30 41 42 33 13 04 05 16 36 45 44 33",
    "943 13 02 01 10 30 41 45 36 16", ",25 26 17", ".26 26", ":22 22|25 25", "'20 21", "?01 10 30 41 42 24|26 26",
    "-13 33", "!20 24|26 26", "+03 43|21 25", "=02 42|04 44",
};
static const char *glyph(char ch)
{
    for (unsigned i = 0; i < sizeof GLYPH / sizeof *GLYPH; i++)
        if (GLYPH[i][0] == ch) return GLYPH[i] + 1;
    return NULL;
}
static double text_width(const char *str, double s) { return strlen(str) * 6.2 * s - 2.2 * s; }
/* text centred on cx, top at y; s is one grid step, so letters are 6s tall */
static void text(const char *str, double cx, double y, double s, double weight, SDL_Color c)
{
    double x = cx - text_width(str, s) / 2;
    for (; *str; str++, x += 6.2 * s) {
        const char *g = glyph(*str);
        if (!g) continue;
        int have = 0; double lx = 0, ly = 0;
        for (; *g; g++) {
            if (*g == '|') { have = 0; continue; }
            if (*g == ' ') continue;
            double gx = x + (g[0] - '0') * s, gy = y + (g[1] - '0') * s;
            g++;
            if (have) line(lx, ly, gx, gy, s * weight, c);
            disc(gx, gy, s * weight * (have && lx == gx && ly == gy ? 0.62 : 0.5), c, c); /* the nib is round */
            lx = gx; ly = gy; have = 1;
        }
    }
}
/* ---- the temple ----
 *
 * The Temple of Orithena is a camera obscura: a small hole in the dome throws an
 * image of the sun on the floor. At noon each day the spot falls somewhere on the
 * analemma, the long figure of eight inlaid in the floor, and over a year it goes
 * all the way round it.
 *
 * The floor is a circle of radius 1 with x to the east and y to the north. The hole
 * is to the south, which in the picture of the temple is on the far side, so the
 * analemma runs across the floor from there towards us. The sun is the real one: its height from the time
 * of year, and its running fast or slow against the clock from the equation of
 * time, which is what gives the figure its two loops. */
#define LATITUDE 35.0       /* degrees north; the book does not give one for Ecba */
#define HOLE_Y (-0.90)      /* where the hole is above the floor, and how high: chosen so that */
#define HOLE_H 0.85         /* the figure runs nearly the whole way across the floor */
/* where the floor and the hole are in the picture of the temple, as fractions of it */
static const double FLOOR_CX = 0.625, FLOOR_CY = 0.833, FLOOR_RX = 0.325, FLOOR_RY = 0.246, HOLE_SX = 0.6385, HOLE_SY = 0.417;
static const double SOUTH_U = 0.04, SOUTH_V = 0.9992; /* which way south is on the floor: across the picture, and away from us */

static double T, anim_t;
static uint64_t seed_state;
static double now_day, now_hours;  /* today as a day of the year (0 = 1 January), and the time by the clock */
static int opt_day = -1;           /* --date: show this day instead of today */
static double opt_hours = -1;      /* --at: show this time instead of now */
#define T_DAY 11.0   /* noon today */
#define T_YEAR 36.0  /* a year of noons */
#define T_END 106.0
#define ROUND 116.0
#define NSING 12
static struct { double x, y; int who; } singer[NSING];

static uint64_t next_rand(void)
{
    seed_state ^= seed_state << 13; seed_state ^= seed_state >> 7; seed_state ^= seed_state << 17;
    return seed_state;
}
static double frand(void) { return (next_rand() >> 11) * (1.0 / 9007199254740992.0); }
static double ramp(double t, double a, double b) { return t <= a ? 0 : t >= b ? 1 : (t - a) / (b - a); }
__attribute__((unused)) static double ease(double x) { return x * x * (3 - 2 * x); }

static void read_time(void)
{
    time_t tt = time(NULL);
    struct tm *tm = localtime(&tt);
    now_day = tm ? tm->tm_yday : 0;
    now_hours = tm ? tm->tm_hour + tm->tm_min / 60.0 + tm->tm_sec / 3600.0 : 12;
    if (opt_day >= 0) now_day = opt_day;
    if (opt_hours >= 0) now_hours = opt_hours;
}

/* Where the spot of light falls on day `day` at `hours` by the clock. Returns 0 if
 * the sun is down or too low for its light to reach the floor. Only noons are ever
 * shown, so the spot is only ever seen on the floor and on the figure. *up gets the sun's height (sine). */
static int spot(double day, double hours, double *x, double *y, double *up)
{
    double b = 2 * M_PI * (day - 81) / 364;
    double eot = 9.87 * sin(2 * b) - 7.53 * cos(b) - 1.5 * sin(b);   /* minutes the sun is ahead of the clock */
    double decl = 23.45 * M_PI / 180 * sin(2 * M_PI * (284 + day + 1) / 365);
    double ha = (15 * (hours - 12) + eot / 4) * M_PI / 180, lat = LATITUDE * M_PI / 180;
    double u = sin(lat) * sin(decl) + cos(lat) * cos(decl) * cos(ha);
    double north = cos(lat) * sin(decl) - sin(lat) * cos(decl) * cos(ha), east = -cos(decl) * sin(ha);
    if (up) *up = u;
    if (u <= 0.02) return 0;
    *x = -HOLE_H * east / u; *y = HOLE_Y - HOLE_H * north / u;
    return hypot(*x, *y) < 0.9; /* the whole pool of light on the floor, clear of the columns */
}

static void new_round(void)
{
    T = 0;
    read_time();
    /* the theors stand in two curved rows, one each side of the analemma */
    for (int i = 0; i < NSING; i++) {
        int side = i % 2 ? 1 : -1, k = i / 2;
        singer[i].x = side * (0.3 + 0.08 * (k % 2) + 0.03 * frand());
        singer[i].y = -0.66 + k * 0.2 + 0.04 * frand();
        singer[i].who = (int)(frand() * 4) % 4;
    }
}

static void update(double dt)
{
    anim_t += dt;
    T += dt * opt_speed;
    if (T > ROUND) new_round();
}

/* ---- drawing ---- */
static double unit;
static int sw, sh;
static double fsx(double x, double y) { return sw * (FLOOR_CX + (-x * SOUTH_V - y * SOUTH_U) * FLOOR_RX); }
static double fsy(double x, double y) { return sh * (FLOOR_CY - (x * SOUTH_U - y * SOUTH_V) * FLOOR_RY); }

static const char *MONTH[12] = {"JANUARY", "FEBRUARY", "MARCH", "APRIL", "MAY", "JUNE", "JULY", "AUGUST", "SEPTEMBER", "OCTOBER", "NOVEMBER", "DECEMBER"};
static const int MDAYS[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
static void date_of(int day, int *d, int *m)
{
    day = ((day % 365) + 365) % 365;
    for (*m = 0; day >= MDAYS[*m]; (*m)++) day -= MDAYS[*m];
    *d = day + 1;
}

/* a picture of light, added to what is there, over the rectangle given */
static void light(int which, double cx, double cy, double w, double h, double alpha)
{
    flush(ren);
    if (!cur_tex || !cur_tex[which] || alpha <= 0) return;
    SDL_FRect dst = {(float)(cx - w / 2), (float)(cy - h / 2), (float)w, (float)h};
    SDL_SetTextureAlphaMod(cur_tex[which], (Uint8)(fmin(alpha, 1) * 255));
    SDL_RenderCopyF(ren, cur_tex[which], NULL, &dst);
    SDL_SetTextureAlphaMod(cur_tex[which], 255);
}
/* the shaft, from the hole down to the spot at (x, y) on the screen */
static void shaft(double x, double y, double wide, double alpha)
{
    flush(ren);
    if (!cur_tex || !cur_tex[SP_SHAFT]) return;
    double hx = sw * HOLE_SX, hy = sh * HOLE_SY, top = wide * 0.22;
    Uint8 a = (Uint8)(fmin(alpha, 1) * 255);
    SDL_Vertex v[4] = {
        {{(float)(hx - top / 2), (float)hy}, {255, 255, 255, a}, {0.15f, 0.02f}}, {{(float)(hx + top / 2), (float)hy}, {255, 255, 255, a}, {0.85f, 0.02f}},
        {{(float)(x + wide / 2), (float)y}, {255, 255, 255, a}, {0.85f, 0.98f}}, {{(float)(x - wide / 2), (float)y}, {255, 255, 255, a}, {0.15f, 0.98f}}};
    int idx[6] = {0, 1, 2, 0, 2, 3};
    SDL_RenderGeometry(ren, cur_tex[SP_SHAFT], v, 4, idx, 6);
}

static const char *say1, *say2;
static char line2[96];

static void draw_caption(int w, int h, const char *l1, const char *l2)
{
    double s1 = unit * 0.46, s2 = unit * 0.3;
    double widest = fmax(text_width(l1, s1), text_width(l2, s2)), room = w * 0.6;
    if (widest > room) { s1 *= room / widest; s2 *= room / widest; }
    double bw = fmax(text_width(l1, s1), text_width(l2, s2)) + unit * 13, bh = unit * 11.5;
    double bx = w / 2.0 - bw / 2, by = unit * 1.2; /* at the top: the floor is at the bottom */
    double cap = 0.135, cw = bh * 1.15 * sprite[SP_BANNER].w * cap / sprite[SP_BANNER].h;
    pic_part(SP_BANNER, bx + cw - 1, by, bw - 2 * cw + 2, bh, cap, 1 - cap, 0, 0);
    pic_part(SP_BANNER, bx, by, cw, bh, 0, cap, 0, 0);
    pic_part(SP_BANNER, bx + bw - cw, by, cw, bh, 1 - cap, 1, 0, 0);
    text(l1, w / 2.0, by + bh * 0.25, s1, 0.95, col(INK, 1));
    text(l2, w / 2.0, by + bh * 0.25 + s1 * 6 + unit * 1.2, s2, 0.9, col(CHORD, 1));
}

static void person(int which, double x, double y, double lit)
{
    double sx = fsx(x, y), sy = fsy(x, y), tall = sh * 0.125 * (0.72 + 0.5 * (sy / sh - 0.58) / 0.42); /* larger towards us */
    disc(sx + tall * 0.05, sy, tall * 0.2, col(INK, 0.3), col(INK, 0));
    pic(which, sx, sy + tall * 0.04, tall, 0.5, 1);
    if (lit > 0) light(SP_POOL, sx, sy - tall * 0.5, tall * 0.9, tall * 1.2, lit * 0.14);
}

static void draw_scene(SDL_Renderer *r, int w, int h)
{
    ren = r; sw = w; sh = h;
    unit = (w < h ? w : h) * 0.0125;
    SDL_SetRenderDrawColor(r, BG.r, BG.g, BG.b, 255);
    SDL_RenderClear(r);
    pic_part(SP_TEMPLE, 0, 0, w, h, 0, 1, 0, 0);

    /* which moment is being shown */
    double day = now_day, hours = now_hours, year = 0;
    int mode = T < T_DAY ? 0 : T < T_YEAR ? 1 : 2, d, m;
    /* The light is only ever shown at noon, which is the only time it is on the
     * figure: now, if it is noon now; then today's noon; then every noon of the year. */
    double shine = 1;
    if (mode == 0) { shine = fabs(now_hours - 12) < 0.12 ? 1 : 0; hours = 12; }
    else if (mode == 1) { hours = 12; shine = ramp(T, T_DAY + 1.5, T_DAY + 5) * (1 - ramp(T, T_YEAR - 2.5, T_YEAR - 0.5)); }
    else {
        year = ramp(T, T_YEAR + 3, T_END);
        day = now_day + 365 * year;
        hours = 12;
        shine = ramp(T, T_YEAR + 0.5, T_YEAR + 2.5);
    }

    /* the analemma, inlaid in brass: the noon spot for every day of the year */
    for (int pass = 0; pass < 2; pass++)
        for (int k = 0; k < 365; k++) {
            double x0, y0, x1, y1;
            spot(k, 12, &x0, &y0, NULL); spot(k + 1, 12, &x1, &y1, NULL);
            line(fsx(x0, y0), fsy(x0, y0), fsx(x1, y1), fsy(x1, y1), unit * (pass ? 0.5 : 0.95), pass ? col(BRASS, 1) : col(INK, 0.8));
        }
    for (int mo = 0, k = 0; mo < 12; k += MDAYS[mo++]) { /* a stud at the first of each month */
        double x, y;
        spot(k, 12, &x, &y, NULL);
        disc(fsx(x, y), fsy(x, y), unit * 0.62, col(INK, 0.85), col(INK, 0.85));
        disc(fsx(x, y), fsy(x, y), unit * 0.42, col(PAPER, 1), col(BRASS, 1));
    }
    /* the noons of the year that have gone by so far */
    if (mode == 2) for (double q = 0; q < 365 * year; q += 2.5) {
        double x, y;
        spot(now_day + q, 12, &x, &y, NULL);
        light(SP_POOL, fsx(x, y), fsy(x, y), w * FLOOR_RX * 0.045, h * FLOOR_RY * 0.045, 0.55);
    }
    flush(r);

    double x = 0, y = 0, up = 0, lx, ly, sun_now = 0;
    int on = spot(day, hours, &x, &y, &up) && shine > 0;
    { double qx, qy; spot(now_day, now_hours, &qx, &qy, &sun_now); }
    spot(mode == 2 ? day : now_day, 12, &lx, &ly, NULL);      /* the leader stands where today's noon falls */
    double near = on ? shine : 0;
    double pw = w * FLOOR_RX * 0.2, ph = h * FLOOR_RY * 0.2, bright = on ? shine : 0;
    if (on) light(SP_POOL, fsx(x, y), fsy(x, y), pw, ph, bright);
    if (mode < 2 && shine < 1) { /* where today's noon falls, marked while the light is away */
        double beat = 0.5 + 0.5 * sin(anim_t * 2.4), rr = 0.055 + 0.012 * beat;
        for (int k = 0; k < 40; k++) {
            double a0 = k * M_PI / 20, a1 = (k + 1) * M_PI / 20;
            line(fsx(lx + cos(a0) * rr, ly + sin(a0) * rr), fsy(lx + cos(a0) * rr, ly + sin(a0) * rr),
                 fsx(lx + cos(a1) * rr, ly + sin(a1) * rr), fsy(lx + cos(a1) * rr, ly + sin(a1) * rr), unit * 0.3, col(PAPER, (1 - shine) * (0.5 + 0.4 * beat)));
        }
    }
    /* people behind the shaft, the shaft, then the people in front of it */
    double spot_sy = on ? fsy(x, y) : -1;
    for (int pass = 0; pass < 2; pass++) {
        for (int j = 0; j < 60; j++) { /* from the far side of the floor to the near */
            double s0 = h * (0.4 + j / 80.0), s1 = s0 + h / 80.0;
            if ((s0 < spot_sy) != (pass == 0) && on) continue;
            if (!on && pass) continue;
            for (int i = 0; i < NSING; i++) {
                double py = fsy(singer[i].x, singer[i].y);
                if (py < s0 || py >= s1) continue;
                person(SP_SING1 + singer[i].who, singer[i].x + near * sin(anim_t * 2.2 + i) * 0.006, singer[i].y, 0);
            }
            double py = fsy(lx, ly);
            if (py >= s0 && py < s1) person(near > 0.5 ? SP_LEAD_UP : SP_LEAD_WAIT, lx, ly, near);
        }
        if (pass == 0 && on) shaft(fsx(x, y), spot_sy, pw * 0.8, 0.5 * bright);
    }
    flush(r);
    if (mode == 0 && sun_now <= 0.02) { rect(0, 0, w, h, (SDL_Color){8, 12, 34, 110}); flush(r); } /* night */

    date_of((int)floor(day + 0.5), &d, &m);
    if (mode == 0) {
        double to_noon = 12 - now_hours;
        int mins = (int)(fabs(to_noon) * 60 + 0.5);
        say1 = "THE TEMPLE OF ORITHENA";
        if (on) snprintf(line2, sizeof line2, "%d %s. IT IS NOON", d, MONTH[m]);
        else snprintf(line2, sizeof line2, "%d %s, %02d:%02d. NOON %s %d H %02d MIN%s", d, MONTH[m], (int)now_hours, (int)(now_hours * 60) % 60,
                      to_noon > 0 ? "IS IN" : "WAS", mins / 60, mins % 60, to_noon > 0 ? "" : " AGO");
        if (T > 5.5) say1 = on ? "THE LIGHT IS ON THE ANALEMMA" : sun_now <= 0.02 ? "THE SUN IS DOWN OVER ECBA" :
                            to_noon > 0 ? "THE LEADER WAITS WHERE TODAY'S NOON WILL FALL" : "THE RING MARKS WHERE TODAY'S NOON FELL";
        say2 = line2;
    } else if (mode == 1) {
        say1 = "NOON TODAY";
        if (shine > 0.5) say2 = "THE LIGHT FALLS ON THE ANALEMMA, AND THE THEORS SING";
        else { snprintf(line2, sizeof line2, "%d %s", d, MONTH[m]); say2 = line2; }
    } else {
        int dd = (int)floor(day + 0.5) % 365;
        say1 = year >= 1 ? "EVERY NOON OF THE YEAR HAS ITS PLACE ON THE FIGURE" : "A YEAR OF NOONS";
        const char *note = abs(dd - 171) < 5 ? ": THE LONGEST DAY" : abs(dd - 354) < 5 ? ": THE SHORTEST DAY" :
                           abs(dd - 79) < 4 || abs(dd - 265) < 4 ? ": DAY AND NIGHT EQUAL" : "";
        snprintf(line2, sizeof line2, "NOON ON %d %s%s", d, MONTH[m], year >= 1 ? ", TODAY" : note);
        say2 = line2;
    }
    draw_caption(w, h, say1, say2);
    flush(r);
    double fade = fmin(ramp(T, 0, 1.4), 1 - ramp(T, ROUND - 1.4, ROUND - 0.1));
    fade = fmin(fade, fmin(1 - 0.7 * (ramp(T, T_DAY - 0.6, T_DAY) - ramp(T, T_DAY, T_DAY + 0.6)), 1 - 0.7 * (ramp(T, T_YEAR - 0.6, T_YEAR) - ramp(T, T_YEAR, T_YEAR + 0.6))));
    if (fade < 1) { rect(0, 0, w, h, col(BG, 1 - fade)); flush(r); }
}

typedef struct { SDL_Window *win; SDL_Renderer *ren; SDL_Texture *scene, *tex[NSPRITE]; int w, h; } View;

static void size_view(View *v)
{
    int ow, oh;
    SDL_RendererInfo info;
    SDL_GetRendererOutputSize(v->ren, &ow, &oh);
    if (ow < 1 || oh < 1) return;
    int ss = 2; /* drawn at twice the size and scaled down, for smooth edges */
    if (SDL_GetRendererInfo(v->ren, &info) == 0) {
        if (!(info.flags & SDL_RENDERER_TARGETTEXTURE) || (info.flags & SDL_RENDERER_SOFTWARE)) ss = 0;
        else if (info.max_texture_width && (ow * 2 > info.max_texture_width || oh * 2 > info.max_texture_height)) ss = 0;
    }
    if (v->w == ow * (ss ? ss : 1) && v->h == oh * (ss ? ss : 1) && (v->scene || !ss)) return;
    if (v->scene) SDL_DestroyTexture(v->scene);
    v->scene = NULL; v->w = ow; v->h = oh;
    if (!ss) return;
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "linear");
    v->scene = SDL_CreateTexture(v->ren, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, ow * ss, oh * ss);
    if (v->scene) { v->w = ow * ss; v->h = oh * ss; }
}
static void render(View *v)
{
    if (v->scene) SDL_SetRenderTarget(v->ren, v->scene);
    cur_tex = v->tex;
    draw_scene(v->ren, v->w, v->h);
    if (v->scene) { SDL_SetRenderTarget(v->ren, NULL); SDL_RenderCopy(v->ren, v->scene, NULL, NULL); }
    SDL_RenderPresent(v->ren);
}
static int make_view(View *v, int display)
{
    Uint32 flags = SDL_WINDOW_ALLOW_HIGHDPI;
    int x = SDL_WINDOWPOS_CENTERED_DISPLAY(display);
    if (opt_window) flags |= SDL_WINDOW_RESIZABLE;
    else flags |= SDL_WINDOW_FULLSCREEN_DESKTOP | SDL_WINDOW_BORDERLESS;
    memset(v, 0, sizeof *v);
    v->win = SDL_CreateWindow("The Analemma", x, x, 1200, 750, flags);
    if (!v->win) return 0;
    v->ren = SDL_CreateRenderer(v->win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC | SDL_RENDERER_TARGETTEXTURE);
    if (!v->ren) v->ren = SDL_CreateRenderer(v->win, -1, SDL_RENDERER_SOFTWARE);
    if (!v->ren) return 0;
    SDL_SetRenderDrawBlendMode(v->ren, SDL_BLENDMODE_BLEND);
    make_textures(v->ren, v->tex);
    return 1;
}

static void start_show(void)
{
    seed_state = opt_seed ? opt_seed * 0x9E3779B97F4A7C15ULL : ((uint64_t)time(NULL) << 20) ^ SDL_GetPerformanceCounter();
    if (!seed_state) seed_state = 1;
    for (int i = 0; i < 6; i++) next_rand();
    anim_t = 0;
    new_round();
}

static int snapshot(void)
{
    SDL_Surface *surf = SDL_CreateRGBSurfaceWithFormat(0, opt_w, opt_h, 32, SDL_PIXELFORMAT_ARGB8888);
    SDL_Renderer *r = surf ? SDL_CreateSoftwareRenderer(surf) : NULL;
    if (!r) { fprintf(stderr, "analemma: %s\n", SDL_GetError()); return 1; }
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_Texture *tex[NSPRITE];
    make_textures(r, tex);
    cur_tex = tex;
    start_show();
    for (double t = 0; t < opt_time; t += 1.0 / 60) update(1.0 / 60);
    draw_scene(r, opt_w, opt_h);
    SDL_RenderPresent(r);
    if (SDL_SaveBMP(surf, opt_snapshot) != 0) { fprintf(stderr, "analemma: %s\n", SDL_GetError()); return 1; }
    return 0;
}

static void usage(void)
{
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_INFORMATION, "The Analemma",
        "The Analemma screensaver\n\n"
        "  --window        run in a window (Esc or Q quits)\n"
        "  --speed X       animation speed, 1 = normal\n"
        "  --seed N        repeatable sequence\n"
        "  --date MM-DD    show this day instead of today\n"
        "  --at HH:MM      show this time instead of now\n"
        "  --snapshot FILE.bmp [--time SECONDS] [--size WxH]\n"
        "                  write a picture of the screensaver after SECONDS and exit\n", NULL);
}

int main(int argc, char **argv)
{
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        if ((a[0] == '/' || a[0] == '-') && (a[1] == 's' || a[1] == 'S') && !a[2]) continue;
        else if ((a[0] == '/' || a[0] == '-') && (a[1] == 'p' || a[1] == 'P') && (!a[2] || a[2] == ':')) return 0;
        else if ((a[0] == '/' || a[0] == '-') && (a[1] == 'c' || a[1] == 'C') && (!a[2] || a[2] == ':')) {
            SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_INFORMATION, "The Analemma", "The Analemma screensaver has no settings.", NULL);
            return 0;
        }
        else if (!strcmp(a, "--window")) opt_window = 1;
        else if (!strcmp(a, "--speed") && i + 1 < argc) opt_speed = SDL_atof(argv[++i]);
        else if (!strcmp(a, "--seed") && i + 1 < argc) opt_seed = SDL_strtoull(argv[++i], NULL, 10);
        else if (!strcmp(a, "--snapshot") && i + 1 < argc) opt_snapshot = argv[++i];
        else if (!strcmp(a, "--date") && i + 1 < argc) { /* MM-DD */
            static const int BEFORE[12] = {0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334};
            const char *dash = SDL_strchr(argv[++i], '-');
            int mo = SDL_atoi(argv[i]), dy = dash ? SDL_atoi(dash + 1) : 0;
            if (mo < 1 || mo > 12 || dy < 1 || dy > 31) { usage(); return 1; }
            opt_day = BEFORE[mo - 1] + dy - 1;
        }
        else if (!strcmp(a, "--at") && i + 1 < argc) { /* HH:MM */
            const char *colon = SDL_strchr(argv[++i], ':');
            opt_hours = SDL_atoi(argv[i]) + (colon ? SDL_atoi(colon + 1) / 60.0 : 0);
        }
        else if (!strcmp(a, "--time") && i + 1 < argc) opt_time = SDL_atof(argv[++i]);
        else if (!strcmp(a, "--size") && i + 1 < argc) {
            const char *x = SDL_strchr(argv[++i], 'x');
            opt_w = SDL_atoi(argv[i]); opt_h = x ? SDL_atoi(x + 1) : 0;
            if (opt_w < 64 || opt_h < 64) { usage(); return 1; }
        }
        else { usage(); return 1; }
    }
    if (opt_speed < 0.05) opt_speed = 0.05;
    load_sprites();

    if (opt_snapshot) return snapshot();
    SDL_SetHint(SDL_HINT_VIDEO_ALLOW_SCREENSAVER, "1");
    if (SDL_Init(SDL_INIT_VIDEO) != 0) { fprintf(stderr, "analemma: %s\n", SDL_GetError()); return 1; }

    View views[8];
    int nviews = 0, ndisp = opt_window ? 1 : SDL_GetNumVideoDisplays();
    if (ndisp < 1) ndisp = 1;
    for (int d = 0; d < ndisp && nviews < 8; d++)
        if (make_view(&views[nviews], d)) nviews++;
    if (!nviews) { fprintf(stderr, "analemma: %s\n", SDL_GetError()); return 1; }
    if (!opt_window) SDL_ShowCursor(SDL_DISABLE);
    start_show();

    Uint64 freqc = SDL_GetPerformanceFrequency(), last = SDL_GetPerformanceCounter(), start = last;
    double moved = 0;
    int running = 1;
    while (running) {
        SDL_Event e;
        double since = (double)(SDL_GetPerformanceCounter() - start) / freqc;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) running = 0;
            else if (e.type == SDL_WINDOWEVENT && e.window.event == SDL_WINDOWEVENT_CLOSE) running = 0;
            else if (e.type == SDL_KEYDOWN) {
                if (!opt_window || e.key.keysym.sym == SDLK_ESCAPE || e.key.keysym.sym == SDLK_q) running = 0;
            } else if (!opt_window && since > 1.0) {
                if (e.type == SDL_MOUSEBUTTONDOWN || e.type == SDL_MOUSEWHEEL || e.type == SDL_FINGERDOWN) running = 0;
                else if (e.type == SDL_MOUSEMOTION) {
                    moved += abs(e.motion.xrel) + abs(e.motion.yrel);
                    if (moved > 24) running = 0;
                }
            }
        }
        Uint64 now = SDL_GetPerformanceCounter();
        double dt = (double)(now - last) / freqc;
        last = now;
        if (dt > 0.1) dt = 0.1;
        update(dt);
        for (int i = 0; i < nviews; i++) { size_view(&views[i]); render(&views[i]); }
        SDL_Delay(views[0].scene ? 1 : 12);
    }
    SDL_Quit();
    return 0;
}


