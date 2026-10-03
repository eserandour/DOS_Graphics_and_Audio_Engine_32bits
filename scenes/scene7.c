/* =========================================================
   SCENE7.C — Scène : tunnel, spirale, plasma et polygones
   =========================================================
   Mise en oeuvre de TOUTES les fonctions de graphics.h :
     putPixel, getPixel, clearScreen
     drawLine, drawRect, drawRectFill
     drawPolygon, drawPolygonFill
     drawCircle, drawCircleFill

   Structure en 6 phases de 3 s chacune (18 s au total) :
     Phase 1 — Tunnel en anneaux (drawCircleFill + drawCircle)
     Phase 2 — Spirale de lignes (drawLine depuis le centre)
     Phase 3 — Plasma en damier morphant (drawRectFill + drawRect)
     Phase 4 — Rosace de polygones (drawPolygonFill + drawPolygon)
     Phase 5 — Polygones en mouvement (drawPolygonFill) et bande
                sinusoïdale de couleurs complémentées
                (getPixel + putPixel)
     Phase 6 — Composition : spirale + tunnel + ornements

   Fade in 1 s / fade out 1 s sur la durée totale.
   Aucune gestion clavier (sauf Échap global via INT 09h).
   Cible : Open Watcom 1.9, DOS, mode 13h (320x200).

   TIMER — scène basée sur scene_template.c (voir ce fichier pour
   les règles) : tout le minutage est en ticks (70 Hz).
   6 sous-scènes de PHASE_TICKS ; elapsed donne la sous-scène et le
   temps dedans. Fondus entrant/sortant = intro/outro du gabarit.
   ========================================================= */





#include <math.h>       /* sin, cos, fabs, sqrt            */
#include <stdlib.h>     /* abs                             */
#include "timer.h"
#include "video.h"
#include "palette.h"
#include "graphics.h"
#include "scene.h"





/* ---------------------------------------------------------
   Réglages de la scène
   --------------------------------------------------------- */
#define PHASE_MS    3000UL      /* durée d'une phase (ms)   */
#define NB_PHASES   6

#define SCENE_MS     (NB_PHASES * PHASE_MS)   /* 18 s */
#define FADE_IN_MS   1000UL     /* fondu entrant  */
#define FADE_OUT_MS  1000UL     /* fondu sortant  */

/* Cadence : 2 ticks = 35 Hz (l'original visait 33 ms, soit ~30 fps).
   Si une image coûte plus cher, le gabarit abandonne le retard. */
#define FRAME_TICKS     2UL

/* ms -> ticks, arrondi ; minimum 1 tick, sauf 0 ms qui reste 0 tick
   (= phase supprimée, ex. FADE_IN_MS 0UL). Ne pas modifier. */
#define MS_TO_TICKS(ms) \
    ((ms) == 0UL ? 0UL : \
     ((((ms) * TARGET_HZ + 500UL) / 1000UL) ? (((ms) * TARGET_HZ + 500UL) / 1000UL) : 1UL))

#define PHASE_TICKS  MS_TO_TICKS(PHASE_MS)

/* Constantes graphiques */
#define PI          3.14159265f
#define TWO_PI      6.28318530f

#define CX          160         /* centre écran X            */
#define CY          100         /* centre écran Y            */





/* ---------------------------------------------------------
   Variables propres à la scène
   --------------------------------------------------------- */
static int lastPhase = -1;   /* dernière phase (sous-scène) dessinée */





/* =========================================================
   UTILITAIRES
   ========================================================= */

/* Retourne un index de couleur entre 1 et 255 (jamais 0). */
static unsigned char hueColor(int offset)
{
    return (unsigned char)(((unsigned int)(offset & 0xFF) % 255) + 1);
}

/* Calcule les sommets d'un polygone régulier à n côtés. */
static void regularPolygon(int cx, int cy, int r, float angle,
                            int n, int *pts)
{
    int i;
    float a;
    for (i = 0; i < n; i++)
    {
        a         = angle + (float)i * (TWO_PI / (float)n);
        pts[i*2]  = cx + (int)((float)cos(a) * (float)r);
        pts[i*2+1]= cy + (int)((float)sin(a) * (float)r);
    }
}

/* =========================================================
   PHASE 1 — Tunnel en anneaux
   =========================================================
   Des anneaux de cercles remplis, du plus grand au plus
   petit, créent l'illusion d'un tunnel qui avance.
   Des cercles de contour intercalés renforcent la profondeur.
   Deux centres secondaires hors-écran valident le clipping. */

static void phase1(unsigned long t_ticks)
{
    float progress = (float)t_ticks / (float)PHASE_TICKS;
    float offset   = progress * 30.0f;   /* défilement du tunnel */
    int   i, r;
    int   nRings  = 14;
    int   maxR    = 220;
    unsigned char col;

    clearScreen(0);

    /* Anneaux du tunnel : du plus grand vers le plus petit
       pour que les petits (proches) soient au-dessus. */
    for (i = nRings; i >= 0; i--)
    {
        /* Le rayon oscille légèrement pour donner un effet
           de pulsation. */
        r = (int)((float)(i + 1) * (float)maxR / (float)(nRings + 1)
            + (float)sin(progress * TWO_PI + (float)i * 0.4f) * 4.0f
            - offset);
        if (r <= 0) continue;

        col = hueColor(i * 18 + (int)(progress * 80.0f));
        drawCircleFill(CX, CY, r, col);

        /* Contour sombre entre chaque anneau. */
        if (r > 4)
            drawCircle(CX, CY, r - 2, 1);
    }

    /* Deux sources secondaires partiellement hors écran,
       clippées par drawCircle / drawCircleFill. */
    for (i = 0; i < 5; i++)
    {
        r   = 15 + i * 20 + (int)(progress * 30.0f);
        col = hueColor(i * 47 + 130 + (int)(progress * 60.0f));
        drawCircle(-30, -20, r, col);
        drawCircle(SCREEN_WIDTH + 20, SCREEN_HEIGHT + 10, r, col);
    }
}

/* =========================================================
   PHASE 2 — Spirale de lignes
   =========================================================
   Des lignes partent du centre vers des points disposés
   sur une spirale d'Archimède qui tourne dans le temps.
   Une seconde spirale en sens inverse crée un entrelacement.
   Le clipping de drawLine gère les extrémités hors-écran. */

static void phase2(unsigned long t_ticks)
{
    float progress  = (float)t_ticks / (float)PHASE_TICKS;
    float baseAngle = progress * TWO_PI * 2.0f;
    int   i, x2, y2;
    int   nSpokes   = 48;
    float rStep     = 160.0f / (float)nSpokes;
    unsigned char col;
    float a, r;

    clearScreen(0);

    /* Spirale principale : le rayon augmente avec l'index. */
    for (i = 0; i < nSpokes; i++)
    {
        a   = baseAngle + (float)i * (TWO_PI / 12.0f);
        r   = (float)(i + 1) * rStep;
        x2  = CX + (int)((float)cos(a) * r);
        y2  = CY + (int)((float)sin(a) * r);
        col = hueColor(i * 5 + (int)(progress * 100.0f));
        drawLine(CX, CY, x2, y2, col);
    }

    /* Spirale inverse, rayons plus courts, teinte décalée. */
    for (i = 0; i < nSpokes / 2; i++)
    {
        a   = -baseAngle * 0.6f + (float)i * (TWO_PI / 8.0f);
        r   = 20.0f + (float)i * (90.0f / (float)(nSpokes / 2));
        x2  = CX + (int)((float)cos(a) * r);
        y2  = CY + (int)((float)sin(a) * r);
        col = hueColor(i * 11 + 128 + (int)(progress * 70.0f));
        drawLine(CX, CY, x2, y2, col);
    }

    /* Petit disque central pour cacher le fouillis au centre. */
    col = hueColor((int)(progress * 255.0f));
    drawCircleFill(CX, CY, 8, col);
}

/* =========================================================
   PHASE 3 — Plasma en damier morphant
   =========================================================
   Une grille de rectangles pleins dont la couleur est
   modulée par une combinaison de sinus (effet plasma).
   La taille des cellules pulse légèrement.
   Quelques contours drawRect animent la surface. */

static void phase3(unsigned long t_ticks)
{
    float progress = (float)t_ticks / (float)PHASE_TICKS;
    float t        = progress * TWO_PI;
    int   i, j;
    int   cols = 16, rows = 10;
    int   cw, ch;   /* taille d'une cellule */
    int   x, y;
    float fx, fy, val;
    unsigned char col;

    /* Taille de cellule qui pulse (14 à 22 px de large). */
    cw = 18 + (int)((float)sin(t * 1.5f) * 4.0f);
    ch = 18 + (int)((float)cos(t * 1.3f) * 4.0f);

    clearScreen(0);

    for (j = 0; j < rows + 2; j++)
        for (i = 0; i < cols + 2; i++)
        {
            x  = i * cw - cw / 2;
            y  = j * ch - ch / 2;
            fx = (float)i / (float)cols;
            fy = (float)j / (float)rows;

            /* Plasma : somme de 4 ondes sinusoïdales. */
            val = (float)sin(fx * 6.0f + t)
                + (float)sin(fy * 5.0f - t * 1.2f)
                + (float)sin((fx + fy) * 4.0f + t * 0.8f)
                + (float)sin((float)sqrt((double)
                    ((fx - 0.5f) * (fx - 0.5f)
                   + (fy - 0.5f) * (fy - 0.5f))) * 10.0f - t);

            /* val ∈ [-4, 4] → color ∈ [1, 255] */
            col = hueColor((int)((val + 4.0f) * 31.0f));
            drawRectFill(x, y, x + cw - 2, y + ch - 2, col);
        }

    /* Contours animés par-dessus le plasma. */
    for (i = 0; i < 5; i++)
    {
        int margin = 8 + i * 14 + (int)((float)sin(t + (float)i) * 6.0f);
        col = hueColor(i * 50 + (int)(progress * 80.0f));
        drawRect(margin, margin,
                 SCREEN_WIDTH  - 1 - margin,
                 SCREEN_HEIGHT - 1 - margin, col);
    }
}

/* =========================================================
   PHASE 4 — Rosace de polygones
   =========================================================
   Des polygones réguliers de 3 à 8 côtés, disposés en
   couronne autour du centre, tournent en sens alternés.
   Au centre, un polygone plus grand rempli bat comme un
   cœur. Une seconde couronne, partiellement hors écran,
   exerce le clipping de drawPolygon. */

static void phase4(unsigned long t_ticks)
{
    float progress = (float)t_ticks / (float)PHASE_TICKS;
    float angle    = progress * TWO_PI;
    int   i, n;
    int   pts[16]; /* max 8 sommets × 2 coords */
    int   cx2, cy2;
    float a;
    unsigned char col;

    /* Rayon pulsant du polygone central (3 battements/scène). */
    int   rCenter = 35 + (int)((float)sin(angle * 3.0f) * 12.0f);

    clearScreen(0);

    /* Polygone central rempli (hexagone pulsant). */
    regularPolygon(CX, CY, rCenter, angle, 6, pts);
    col = hueColor((int)(progress * 180.0f));
    drawPolygonFill(pts, 6, col);
    col = hueColor((int)(progress * 180.0f) + 128);
    drawPolygon(pts, 6, col);

    /* Couronne de 6 polygones de 3 à 8 côtés. */
    for (i = 0; i < 6; i++)
    {
        n   = 3 + i;           /* triangle, carré, … octogone */
        a   = angle + (float)i * (TWO_PI / 6.0f);
        cx2 = CX + (int)((float)cos(a) * 75.0f);
        cy2 = CY + (int)((float)sin(a) * 75.0f);

        /* Rotation locale alternée. */
        regularPolygon(cx2, cy2, 22,
                       (i % 2 == 0) ? angle * 1.5f : -angle * 1.5f,
                       n, pts);
        col = hueColor(i * 40 + (int)(progress * 120.0f));
        drawPolygonFill(pts, n, col);
        col = hueColor(i * 40 + (int)(progress * 120.0f) + 80);
        drawPolygon(pts, n, col);
    }

    /* Deuxième couronne externe, polygones plus petits,
       partiellement hors écran → clipping activé. */
    for (i = 0; i < 8; i++)
    {
        n   = 3 + (i % 4);
        a   = -angle * 0.8f + (float)i * (TWO_PI / 8.0f);
        cx2 = CX + (int)((float)cos(a) * 140.0f);
        cy2 = CY + (int)((float)sin(a) * 140.0f);
        regularPolygon(cx2, cy2, 14, angle * 2.0f, n, pts);
        col = hueColor(i * 31 + 60 + (int)(progress * 90.0f));
        drawPolygon(pts, n, col);
    }
}

/* =========================================================
   PHASE 5 — Polygones en mouvement + bande complémentée
   =========================================================
   Trois polygones (triangle, carré, pentagone) suivent des
   trajectoires sinusoïdales indépendantes en tournant.
   getPixel + putPixel : le long d'une sinusoïde horizontale,
   chaque pixel non noir est relu et remplacé par l'index
   décalé de 127 (couleur « complémentaire » dans la palette). */

static void phase5(unsigned long t_ticks)
{
    float progress = (float)t_ticks / (float)PHASE_TICKS;
    float t        = progress * TWO_PI;
    int   i, pts[10];
    unsigned char col, existing;
    int   x, y;

    /* Positions des trois polygones (trajectoires sinusoïdales
       indépendantes pour éviter la synchronisation). */
    int cx1 = 60  + (int)((float)cos(t * 1.1f)         * 55.0f);
    int cy1 = 60  + (int)((float)sin(t * 0.9f)         * 40.0f);
    int cx2 = CX  + (int)((float)cos(t * 0.7f + 1.0f)  * 90.0f);
    int cy2 = CY  + (int)((float)sin(t * 1.3f + 0.5f)  * 60.0f);
    int cx3 = 240 + (int)((float)cos(t * 1.5f + 2.0f)  * 50.0f);
    int cy3 = 140 + (int)((float)sin(t * 0.8f + 1.5f)  * 40.0f);

    clearScreen(0);

    /* Triangle rempli. */
    regularPolygon(cx1, cy1, 38, t * 1.2f, 3, pts);
    col = hueColor((int)(progress * 160.0f));
    drawPolygonFill(pts, 3, col);
    drawPolygon(pts, 3, 255);

    /* Carré rempli. */
    regularPolygon(cx2, cy2, 32, t * 0.8f, 4, pts);
    col = hueColor((int)(progress * 160.0f) + 85);
    drawPolygonFill(pts, 4, col);
    drawPolygon(pts, 4, 255);

    /* Pentagone rempli. */
    regularPolygon(cx3, cy3, 28, -t * 1.4f, 5, pts);
    col = hueColor((int)(progress * 160.0f) + 170);
    drawPolygonFill(pts, 5, col);
    drawPolygon(pts, 5, 255);

    /* Effet getPixel / putPixel sur une sinusoïde horizontale
       médiane : complémentation des couleurs présentes. */
    for (x = 0; x < SCREEN_WIDTH; x++)
    {
        y        = CY + (int)((float)sin((float)x * 0.08f + t) * 20.0f);
        if (y < 0 || y >= SCREEN_HEIGHT) continue;
        existing = getPixel(x, y);
        if (existing != 0)
        {
            col = (unsigned char)(((int)existing + 127) % 255 + 1);
            putPixel(x, y, col);
        }
    }
}

/* =========================================================
   PHASE 6 — Composition finale
   =========================================================
   Toutes les primitives ensemble :
   - Tunnel d'anneaux en arrière-plan (drawCircleFill)
   - Spirale de lignes par-dessus (drawLine)
   - Polygone central pulsant (drawPolygonFill)
   - Couronne de cercles de contour (drawCircle)
   - Cadre de rectangles imbriqués (drawRect)
   - Ornements putPixel sur une ellipse
   ========================================================= */

static void phase6(unsigned long t_ticks)
{
    float progress = (float)t_ticks / (float)PHASE_TICKS;
    float angle    = progress * TWO_PI;
    int   i, x2, y2, r, pts[12];
    float a;
    unsigned char col;

    clearScreen(0);

    /* --- Fond : tunnel simplifié (5 anneaux) --- */
    for (i = 5; i >= 0; i--)
    {
        r   = 30 + i * 32 + (int)((float)sin(angle + (float)i) * 5.0f);
        col = hueColor(i * 22 + (int)(progress * 60.0f));
        drawCircleFill(CX, CY, r, col);
    }

    /* --- Spirale de 30 rayons --- */
    for (i = 0; i < 30; i++)
    {
        a   = angle * 1.5f + (float)i * (TWO_PI / 10.0f);
        r   = 40 + i * 5;
        x2  = CX + (int)((float)cos(a) * (float)r);
        y2  = CY + (int)((float)sin(a) * (float)r);
        col = hueColor(i * 8 + (int)(progress * 80.0f));
        drawLine(CX, CY, x2, y2, col);
    }

    /* --- Polygone central rempli (5 côtés pulsant) --- */
    r = 28 + (int)((float)sin(angle * 4.0f) * 8.0f);
    regularPolygon(CX, CY, r, angle * 2.0f, 5, pts);
    col = hueColor(200 + (int)(progress * 55.0f));
    drawPolygonFill(pts, 5, col);
    drawPolygon(pts, 5, 255);

    /* --- Couronne de 6 cercles de contour orbitaux --- */
    for (i = 0; i < 6; i++)
    {
        a   = angle * 1.2f + (float)i * (TWO_PI / 6.0f);
        x2  = CX + (int)((float)cos(a) * 65.0f);
        y2  = CY + (int)((float)sin(a) * 65.0f);
        col = hueColor(i * 42 + (int)(progress * 90.0f));
        drawCircle(x2, y2, 14, col);
    }

    /* --- Cadres de rectangles imbriqués animés --- */
    for (i = 0; i < 4; i++)
    {
        int m = 4 + i * 8 + (int)((float)sin(angle + (float)i * 0.8f) * 5.0f);
        col   = hueColor(i * 60 + (int)(progress * 50.0f));
        drawRect(m, m, SCREEN_WIDTH - 1 - m, SCREEN_HEIGHT - 1 - m, col);
    }

    /* --- Ornements putPixel : ellipse de points --- */
    for (i = 0; i < 80; i++)
    {
        a   = angle * 3.0f + (float)i * (TWO_PI / 80.0f);
        x2  = CX + (int)((float)cos(a) * 90.0f);
        y2  = CY + (int)((float)sin(a) * 45.0f);
        col = hueColor(i * 3 + (int)(progress * 120.0f));
        putPixel(x2, y2, col);
    }
}





/* =========================================================
   INIT — appelée UNE fois au lancement de la scène
   ========================================================= */
static void scene7Init(void)
{
    lastPhase = -1;

    buildRainbowPalette(rainbowPalette);
    copyPalette(workingPalette, rainbowPalette);
    fadePalette(workingPalette, 0.0f);      /* on part du noir (DAC seulement) */

    clearScreen(0);
    flip();
}





/* =========================================================
   RENDER — appelée à chaque image (cadence FRAME_TICKS)
   ---------------------------------------------------------
   phase    : 0 = intro, 1 = corps, 2 = outro   (phases du gabarit)
   progress : avancement DANS cette phase, de 0 à 1000
   elapsed  : ticks écoulés depuis le début de la scène
   ========================================================= */
static void scene7Render(unsigned long elapsed, int phase, unsigned long progress)
{
    int           sub;
    unsigned long sub_t;
    float         t;

    /* -------------------------------------------------------
       Sous-scène courante (1 à 6) et temps dans cette sous-scène,
       en ticks. Ne pas confondre avec 'phase' (intro/corps/outro
       du gabarit) qui ne sert ici qu'au fondu global.
       ------------------------------------------------------- */
    sub = (int)(elapsed / PHASE_TICKS);
    if (sub >= NB_PHASES) sub = NB_PHASES - 1;
    sub_t = elapsed - (unsigned long)sub * PHASE_TICKS;

    if (sub != lastPhase)
    {
        clearScreen(0);
        lastPhase = sub;
    }

    switch (sub)
    {
        case 0: phase1(sub_t); break;
        case 1: phase2(sub_t); break;
        case 2: phase3(sub_t); break;
        case 3: phase4(sub_t); break;
        case 4: phase5(sub_t); break;
        case 5: phase6(sub_t); break;
    }

    flip();

    /* -------------------------------------------------------
       Fondu global (intro / outro du gabarit)
       ------------------------------------------------------- */
    switch (phase)
    {
    case 0: /* ---- INTRO : 0 -> 1 ---- */
        t = (float)progress / 1000.0f;
        break;

    case 1: /* ---- CORPS : pleine luminosité ---- */
        t = 1.0f;
        break;

    default: /* ---- OUTRO : 1 -> 0 ---- */
        t = 1.0f - (float)progress / 1000.0f;
        break;
    }

    fadePalette(workingPalette, t);
}





/* =========================================================
   CLEANUP — appelée UNE fois à la fin de la scène
   ========================================================= */
static void scene7Cleanup(void)
{
    /* Rien à libérer. */
}





/* =========================================================
   POINT D'ENTRÉE — gestion du timer (NE PAS MODIFIER)
   ========================================================= */
void scene7(void)
{
    static int           initialized = 0;
    static unsigned long lastFrame   = 0UL;

    const unsigned long sceneTicks = MS_TO_TICKS(SCENE_MS);
    const unsigned long inTicks    = MS_TO_TICKS(FADE_IN_MS);
    const unsigned long outTicks   = MS_TO_TICKS(FADE_OUT_MS);

    unsigned long now, elapsed, progress;
    int phase;

    now = readTimer();

    /* 1. Initialisation : une seule fois par lancement */
    if (!initialized)
    {
        initialized = 1;
        sceneStart  = now;
        lastFrame   = now;
        scene7Init();
        return;                     /* le 1er rendu se fera au tour suivant */
    }

    elapsed = elapsedTime(sceneStart, now);

    /* 2. Fin de scène : test AVANT le rendu */
    if (elapsed >= sceneTicks)
    {
        scene7Cleanup();
        initialized = 0;            /* état remis à zéro d'abord... */
        sceneSignalEnd();           /* ...puis on rend la main      */
        return;                     /* et on ne touche plus à rien  */
    }

    /* 3. Limitation de cadence */
    if (elapsedTime(lastFrame, now) < FRAME_TICKS)
        return;

    lastFrame += FRAME_TICKS;       /* pas fixe : pas de dérive */
    if (elapsedTime(lastFrame, now) >= FRAME_TICKS)
        lastFrame = now;            /* trop de retard : on abandonne le rattrapage */

    /* 4. Phase courante et progression dans la phase (0..1000) */
    if (inTicks > 0UL && elapsed < inTicks)
    {
        phase    = 0;
        progress = elapsed * 1000UL / inTicks;
    }
    else if (outTicks > 0UL && elapsed >= sceneTicks - outTicks)
    {
        phase    = 2;
        progress = (elapsed - (sceneTicks - outTicks)) * 1000UL / outTicks;
    }
    else
    {
        phase    = 1;
        progress = (elapsed - inTicks) * 1000UL / (sceneTicks - inTicks - outTicks);
    }

    /* 5. Rendu */
    scene7Render(elapsed, phase, progress);
}
