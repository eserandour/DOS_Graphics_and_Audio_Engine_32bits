/* =========================================================
   GRAPHICS.C — Primitives de dessin 2D en mode 13h
   ========================================================= */

#include <stdlib.h>   /* abs                              */
#include <string.h>   /* memset, memcpy                   */
#include "video.h"    /* backbuffer, OFFSET, SCREEN_*     */
#include "graphics.h"

/* =========================================================
   MACROS INTERNES
   ========================================================= */

/* Écriture directe d'un pixel — aucune vérification.
   À n'utiliser que lorsque (x,y) est garanti dans les bornes
   [0..SCREEN_WIDTH-1] × [0..SCREEN_HEIGHT-1]. */
#define PUT_PIXEL_UNSAFE(x, y, c) \
    (backbuffer[OFFSET((x),(y))] = (unsigned char)(c))

/* Vérification d'un pixel dans les bornes de l'écran. */
#define IN_BOUNDS(x, y) \
    ((x) >= 0 && (x) < SCREEN_WIDTH && (y) >= 0 && (y) < SCREEN_HEIGHT)

/* Ligne horizontale directe dans le backbuffer.
   xs : x de départ (déjà clampé), len : nombre de pixels > 0.
   Appelle memset directement, sans passer par drawLine. */
#define PUT_HLINE(xs, y, len, c) \
    memset(backbuffer + OFFSET((xs),(y)), (c), (size_t)(len))

/* =========================================================
   EFFACEMENT
   ========================================================= */

/* Remplit le backbuffer entier avec une seule couleur.
   memset est utilisable directement : backbuffer est un
   pointeur plat classique en modèle flat 32 bits. */
void clearScreen(unsigned char color)
{
    memset(backbuffer, color, BACKBUFFER_SIZE);
}

/* =========================================================
   PIXEL (API publique — vérification de bornes conservée)
   ========================================================= */

void putPixel(int x, int y, unsigned char color)
{
    if (IN_BOUNDS(x, y))
        PUT_PIXEL_UNSAFE(x, y, color);
}

unsigned char getPixel(int x, int y)
{
    /* Lecture hors bornes : on ne lit pas hors du backbuffer,
       on retourne 0 par convention. */
    if (!IN_BOUNDS(x, y)) return 0;
    return backbuffer[OFFSET(x, y)];
}

/* =========================================================
   LIGNE — Algorithme de Bresenham + Clipping Cohen-Sutherland
   =========================================================
   Le clipping Cohen-Sutherland divise le plan en 9 zones
   à l'aide de 4 bits (OUT_LEFT, OUT_RIGHT, OUT_BOTTOM,
   OUT_TOP). On classe chaque extrémité dans sa zone, puis :
   - si les deux codes = 0 → segment entièrement visible.
   - si (code1 & code2) ≠ 0 → segment entièrement dehors.
   - sinon → on calcule l'intersection avec la frontière
     correspondante et on recommence.
   ========================================================= */

#define CS_OUT_LEFT   1
#define CS_OUT_RIGHT  2
#define CS_OUT_BOTTOM 4
#define CS_OUT_TOP    8

static int cs_code(int x, int y)
{
    int code = 0;
    if (x < 0)                code |= CS_OUT_LEFT;
    if (x >= SCREEN_WIDTH)    code |= CS_OUT_RIGHT;
    if (y < 0)                code |= CS_OUT_TOP;
    if (y >= SCREEN_HEIGHT)   code |= CS_OUT_BOTTOM;
    return code;
}

void drawLine(int x1, int y1, int x2, int y2, unsigned char color)
{
    int code1 = cs_code(x1, y1);
    int code2 = cs_code(x2, y2);
    int code, dx, dy, sx, sy, err, e2;

    while (1)
    {
        if (!(code1 | code2)) break;
        if (code1 & code2)    return;

        code = code1 ? code1 : code2;
        dx = x2 - x1;
        dy = y2 - y1;

        if (code & CS_OUT_BOTTOM)
        {
            int nx = x1 + dx * (SCREEN_HEIGHT - 1 - y1) / dy;
            int ny = SCREEN_HEIGHT - 1;
            if (code == code1) { x1 = nx; y1 = ny; code1 = cs_code(x1, y1); }
            else               { x2 = nx; y2 = ny; code2 = cs_code(x2, y2); }
        }
        else if (code & CS_OUT_TOP)
        {
            int nx = x1 + dx * (-y1) / dy;
            int ny = 0;
            if (code == code1) { x1 = nx; y1 = ny; code1 = cs_code(x1, y1); }
            else               { x2 = nx; y2 = ny; code2 = cs_code(x2, y2); }
        }
        else if (code & CS_OUT_RIGHT)
        {
            int ny = y1 + dy * (SCREEN_WIDTH - 1 - x1) / dx;
            int nx = SCREEN_WIDTH - 1;
            if (code == code1) { x1 = nx; y1 = ny; code1 = cs_code(x1, y1); }
            else               { x2 = nx; y2 = ny; code2 = cs_code(x2, y2); }
        }
        else
        {
            int ny = y1 + dy * (-x1) / dx;
            int nx = 0;
            if (code == code1) { x1 = nx; y1 = ny; code1 = cs_code(x1, y1); }
            else               { x2 = nx; y2 = ny; code2 = cs_code(x2, y2); }
        }
    }

    /* Bresenham avec écriture directe.
       Après le clipping, toutes les coordonnées sont dans les
       bornes → PUT_PIXEL_UNSAFE est sûr, pas de check ni d'appel. */
    dx  = abs(x2 - x1);
    dy  = abs(y2 - y1);
    sx  = (x1 < x2) ? 1 : -1;
    sy  = (y1 < y2) ? 1 : -1;
    err = dx - dy;

    while (1)
    {
        PUT_PIXEL_UNSAFE(x1, y1, color);
        if (x1 == x2 && y1 == y2) break;
        e2 = err << 1;                     /* 2*err sans mul */
        if (e2 > -dy) { err -= dy; x1 += sx; }
        if (e2 <  dx) { err += dx; y1 += sy; }
    }
}

/* =========================================================
   RECTANGLE
   ========================================================= */

void drawRect(int x1, int y1, int x2, int y2, unsigned char color)
{
    drawLine(x1, y1, x2, y1, color);
    drawLine(x2, y1, x2, y2, color);
    drawLine(x2, y2, x1, y2, color);
    drawLine(x1, y2, x1, y1, color);
}

/* Rectangle plein : memset ligne par ligne après clamp.
   Cas w==1 traité séparément pour éviter memset de 1 octet   
   en boucle (remplacé par PUT_PIXEL_UNSAFE). */
void drawRectFill(int x1, int y1, int x2, int y2, unsigned char color)
{
    int y, len;

    if (x1 > x2) { int t = x1; x1 = x2; x2 = t; }
    if (y1 > y2) { int t = y1; y1 = y2; y2 = t; }

    if (x1 < 0)              x1 = 0;
    if (x2 >= SCREEN_WIDTH)  x2 = SCREEN_WIDTH  - 1;
    if (y1 < 0)              y1 = 0;
    if (y2 >= SCREEN_HEIGHT) y2 = SCREEN_HEIGHT - 1;

    if (x1 > x2 || y1 > y2) return;

    len = x2 - x1 + 1;

    if (len == 1)
    {
        /* Colonne verticale : PUT_PIXEL_UNSAFE évite memset(…,1). */
        for (y = y1; y <= y2; y++)
            PUT_PIXEL_UNSAFE(x1, y, color);
    }
    else
    {
        for (y = y1; y <= y2; y++)
            PUT_HLINE(x1, y, len, color);
    }
}

/* =========================================================
   POLYGONE
   ========================================================= */

void drawPolygon(int *pts, int n, unsigned char color)
{
    int i;
    if (n < 2) return;
    for (i = 0; i < n - 1; i++)
        drawLine(pts[i*2], pts[i*2+1], pts[i*2+2], pts[i*2+3], color);
    drawLine(pts[(n-1)*2], pts[(n-1)*2+1], pts[0], pts[1], color);
}

/* =========================================================
   POLYGONE PLEIN — Scanline filling (even-odd rule)
   =========================================================
   intersections[] déclaré static : plus d'allocation
   sur la pile à chaque appel. DOS est mono-tâche, la fonction
   n'est pas réentrante → le tableau static est sûr.
   ========================================================= */

void drawPolygonFill(int *pts, int n, unsigned char color)
{
    /* Static : un seul tableau en mémoire, jamais sur la pile. */
    static int intersections[SCREEN_WIDTH];

    int i, j;
    int y, ymin, ymax;
    int x1, y1, x2, y2;
    int count, tmp, dx, dy;

    if (n < 3) return;

    ymin = pts[1]; ymax = pts[1];
    for (i = 1; i < n; i++)
    {
        if (pts[i*2+1] < ymin) ymin = pts[i*2+1];
        if (pts[i*2+1] > ymax) ymax = pts[i*2+1];
    }

    if (ymin < 0)              ymin = 0;
    if (ymax >= SCREEN_HEIGHT) ymax = SCREEN_HEIGHT - 1;

    for (y = ymin; y <= ymax; y++)
    {
        count = 0;

        for (i = 0; i < n; i++)
        {
            j  = (i + 1) % n;
            x1 = pts[i*2];   y1 = pts[i*2+1];
            x2 = pts[j*2];   y2 = pts[j*2+1];

            if ((y1 <= y && y2 > y) || (y2 <= y && y1 > y))
            {
                dy = y2 - y1;
                dx = x2 - x1;
                intersections[count++] = x1 + (dx * (y - y1)) / dy;
            }
        }

        /* Tri par insertion : efficace pour count petit (2 ou 4 en pratique). */
        for (i = 1; i < count; i++)
        {
            tmp = intersections[i];
            j   = i - 1;
            while (j >= 0 && intersections[j] > tmp)
            {
                intersections[j+1] = intersections[j];
                j--;
            }
            intersections[j+1] = tmp;
        }

        /* Remplissage par paires d'intersections (règle pair-impair). */
        for (i = 0; i + 1 < count; i += 2)
        {
            int xstart = intersections[i];
            int xend   = intersections[i+1];
            int len;

            if (xstart < 0)             xstart = 0;
            if (xend   >= SCREEN_WIDTH) xend   = SCREEN_WIDTH - 1;

            len = xend - xstart;
            if (len > 0)
                PUT_HLINE(xstart, y, len, color);
        }
    }
}

/* =========================================================
   CERCLE — Algorithme de Bresenham (mid-point circle)
   =========================================================
   Le cercle possède 8 axes de symétrie. On calcule les
   points d'un seul octant (x de 0 à r/√2) et on en déduit
   les 7 autres par symétrie.

   Les 8 points symétriques sont vérifiés une fois
   par dimension (xc±x en X, yc±y en Y) puis écrits avec
   PUT_PIXEL_UNSAFE. Cela regroupe les 16 vérifications
   initiales en 8 et élimine 8 appels de fonction putPixel.
   ========================================================= */

void drawCircle(int xc, int yc, int r, unsigned char color)
{
    int x = 0, y = r;
    int d = 3 - 2 * r;

    while (x <= y)
    {
        /* Vérification par axe puis écriture directe. */
        int xpx = xc + x, xmx = xc - x;
        int xpy = xc + y, xmy = xc - y;
        int ypy = yc + y, ymy = yc - y;
        int ypx = yc + x, ymx = yc - x;

        /* Octants en X+ */
        if (xpx >= 0 && xpx < SCREEN_WIDTH) {
            if (ypy >= 0 && ypy < SCREEN_HEIGHT) PUT_PIXEL_UNSAFE(xpx, ypy, color);
            if (ymy >= 0 && ymy < SCREEN_HEIGHT) PUT_PIXEL_UNSAFE(xpx, ymy, color);
        }
        /* Octants en X- */
        if (xmx >= 0 && xmx < SCREEN_WIDTH) {
            if (ypy >= 0 && ypy < SCREEN_HEIGHT) PUT_PIXEL_UNSAFE(xmx, ypy, color);
            if (ymy >= 0 && ymy < SCREEN_HEIGHT) PUT_PIXEL_UNSAFE(xmx, ymy, color);
        }
        /* Octants en Y (x et y échangés) */
        if (xpy >= 0 && xpy < SCREEN_WIDTH) {
            if (ypx >= 0 && ypx < SCREEN_HEIGHT) PUT_PIXEL_UNSAFE(xpy, ypx, color);
            if (ymx >= 0 && ymx < SCREEN_HEIGHT) PUT_PIXEL_UNSAFE(xpy, ymx, color);
        }
        if (xmy >= 0 && xmy < SCREEN_WIDTH) {
            if (ypx >= 0 && ypx < SCREEN_HEIGHT) PUT_PIXEL_UNSAFE(xmy, ypx, color);
            if (ymx >= 0 && ymx < SCREEN_HEIGHT) PUT_PIXEL_UNSAFE(xmy, ymx, color);
        }

        if (d < 0) d += 4 * x + 6;
        else      { d += 4 * (x - y) + 10; y--; }
        x++;
    }
}

/* =========================================================
   CERCLE PLEIN
   =========================================================
   Lignes horizontales tracées avec PUT_HLINE (macro
   memset directe). Élimine deux niveaux d’indirection par
   rapport à un éventuel appel de drawLine horizontal.

   Les 4 lignes symétriques par itération couvrent :
     yc-y (ligne haute du cercle extérieur)
     yc+y (ligne basse du cercle extérieur)
     yc-x (ligne haute du cercle intérieur)
     yc+x (ligne basse du cercle intérieur)
   ========================================================= */

void drawCircleFill(int xc, int yc, int r, unsigned char color)
{
    int x = 0, y = r;
    int d = 3 - 2 * r;
    int xs, xl;

    while (x <= y)
    {
        /* Ligne yc-y : de xc-x à xc+x, largeur 2x+1. */
        if (yc-y >= 0 && yc-y < SCREEN_HEIGHT) {
            xs = xc - x; xl = 2*x + 1;
            if (xs < 0)              { xl += xs; xs = 0; }
            if (xs + xl > SCREEN_WIDTH) xl = SCREEN_WIDTH - xs;
            if (xl > 0) PUT_HLINE(xs, yc-y, xl, color);
        }

        /* Ligne yc+y : de xc-x à xc+x, largeur 2x+1. */
        if (yc+y >= 0 && yc+y < SCREEN_HEIGHT) {
            xs = xc - x; xl = 2*x + 1;
            if (xs < 0)              { xl += xs; xs = 0; }
            if (xs + xl > SCREEN_WIDTH) xl = SCREEN_WIDTH - xs;
            if (xl > 0) PUT_HLINE(xs, yc+y, xl, color);
        }

        /* Ligne yc-x : de xc-y à xc+y, largeur 2y+1. */
        if (yc-x >= 0 && yc-x < SCREEN_HEIGHT) {
            xs = xc - y; xl = 2*y + 1;
            if (xs < 0)              { xl += xs; xs = 0; }
            if (xs + xl > SCREEN_WIDTH) xl = SCREEN_WIDTH - xs;
            if (xl > 0) PUT_HLINE(xs, yc-x, xl, color);
        }

        /* Ligne yc+x : de xc-y à xc+y, largeur 2y+1. */
        if (yc+x >= 0 && yc+x < SCREEN_HEIGHT) {
            xs = xc - y; xl = 2*y + 1;
            if (xs < 0)              { xl += xs; xs = 0; }
            if (xs + xl > SCREEN_WIDTH) xl = SCREEN_WIDTH - xs;
            if (xl > 0) PUT_HLINE(xs, yc+x, xl, color);
        }

        if (d < 0) d += 4 * x + 6;
        else      { d += 4 * (x - y) + 10; y--; }
        x++;
    }
}
