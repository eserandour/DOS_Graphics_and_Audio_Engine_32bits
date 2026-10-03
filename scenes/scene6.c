/* =========================================================
   SCENE6.C — Rotozoom sur l'image freedos (256x256)
   =========================================================
   Environnement : Open Watcom 1.9, DOS
   Mode video    : 13h (320x200, 256 couleurs)

   PRINCIPE
   --------
   Pour chaque pixel (px, py) de l'ecran, on calcule sa
   position dans la texture source apres rotation et zoom :

     u = cx + (cos(a)/zoom)*(px-SCR_CX) - (sin(a)/zoom)*(py-SCR_CY)
     v = cy + (sin(a)/zoom)*(px-SCR_CX) + (cos(a)/zoom)*(py-SCR_CY)

   La texture 256x256 est tilable par masquage (& 0xFF).

   ARITHMETIQUE VIRGULE FIXE 16.16
   --------------------------------
   Virgule fixe pour la boucle par pixel : sur un 386 (sans
   80387, puce en option jusqu'au 486DX) ou un 486SX, les calculs
   flottants seraient émulés, donc très lents à cette échelle.
   D'autres scènes (scene2, scene7) et palette.c utilisent des
   float hors des boucles critiques. 1.0 = 65536L (FP_ONE).

   DÉCOUPAGE DE LA TEXTURE EN DEUX BLOCS
   ------------------------------------
   La texture 256x256 (65536 octets) est stockée en deux blocs
   de 128 lignes, tex0 (lignes 0..127) et tex1 (lignes 128..255),
   accédés via la macro TEX_PIXEL(tx,ty) qui choisit le bon bloc
   selon ty. Deux blocs de 32768 octets plutôt qu'un seul de
   65536 : héritage de la version 16 bits (blocs < 64 Ko). En
   modèle flat 32 bits, un bloc unique de 65536 octets
   fonctionnerait tout aussi bien.

   ANIMATION
   ---------
   Phase 1 (0..8 s)  : rotation continue + zoom oscillant
   Phase 2 (8..10 s) : fade-out palette vers le noir

   NOTE C89 (Open Watcom)
   ----------------------
   Toutes les declarations sont en tete de fonction ou au
   niveau fichier. Aucune declaration dans un bloc imbrique
   ou apres une instruction.

   TIMER — scène basée sur scene_template.c (voir ce fichier pour
   les règles) : tout le minutage est en ticks (70 Hz).
   Phase 1 = corps (0..8 s) ; phase 2 = outro (8..10 s).
   ========================================================= */





#include <stdio.h>    /* FILE, fopen, fread, fclose */
#include <malloc.h>   /* malloc, free               */
#include "timer.h"
#include "video.h"
#include "palette.h"
#include "scene.h"
#include "app.h"





/* ---------------------------------------------------------
   Réglages de la scène
   --------------------------------------------------------- */
#define SCENE_MS     10000UL   /* durée totale : 8 s de rotozoom + 2 s de fondu */
#define FADE_IN_MS       0UL   /* pas d'intro                                   */
#define FADE_OUT_MS   2000UL   /* outro : fondu de palette vers le noir         */

/* Cadence : 2 ticks = 35 Hz (l'original visait 25 ms, soit 40 fps).
   La rotation avance de 2 pas par image : sa vitesse suit FRAME_TICKS. */
#define FRAME_TICKS      2UL

/* ms -> ticks, arrondi ; minimum 1 tick, sauf 0 ms qui reste 0 tick
   (= phase supprimée, ex. FADE_IN_MS 0UL). Ne pas modifier. */
#define MS_TO_TICKS(ms) \
    ((ms) == 0UL ? 0UL : \
     ((((ms) * TARGET_HZ + 500UL) / 1000UL) ? (((ms) * TARGET_HZ + 500UL) / 1000UL) : 1UL))

/* Période d'oscillation du zoom : 4 s */
#define ZOOM_PERIOD_TICKS  MS_TO_TICKS(4000UL)

/* Texture et écran */
#define TEX_W       256
#define TEX_H       256
#define TEX_MASK    0xFF

#define SCR_CX      160
#define SCR_CY      100
#define TEX_CX      128
#define TEX_CY      128

#define FP_ONE      65536L
#define ANGLE_STEPS 512





/* ---------------------------------------------------------
   Variables propres à la scène
   --------------------------------------------------------- */
/* Table sinus et texture. La texture est découpée en deux blocs
   (voir note en tête de fichier) :
     tex0 = lignes   0..127
     tex1 = lignes 128..255 */
static long          *sin_tab = NULL;
static unsigned char *tex0    = NULL;
static unsigned char *tex1    = NULL;

static int            angle   = 0;

#define TEX_HALF (TEX_W * (TEX_H / 2))   /* 32768 */

/* Acces unifie a la texture split :
   si ty < 128 => dans tex0, sinon => dans tex1 (ligne ty-128) */
#define TEX_PIXEL(tx, ty) \
    (((ty) < 128) \
        ? tex0[(unsigned int)(ty)  * TEX_W + (unsigned int)(tx)] \
        : tex1[(unsigned int)((ty) - 128) * TEX_W + (unsigned int)(tx)])

#define SIN_FP(a) sin_tab[(a) & 511]
#define COS_FP(a) sin_tab[((a) + 128) & 511]





/* =========================================================
   GENERATION DE LA TABLE SINUS
   =========================================================
   Recurrence de Bresenham avec reseed aux quarts de tour
   pour limiter la derive numerique.
   dtheta = 2*PI/512 en 16.16 ~ 804
   ========================================================= */
static void buildSinTable(void)
{
    const long DTHETA_DIV = FP_ONE / 804L;
    long s, c, ds;
    int  i;

    s = 0L;
    c = FP_ONE;

    for (i = 0; i < ANGLE_STEPS; i++)
    {
        sin_tab[i] = s;

        if      (i == 128) { s =  FP_ONE; c = 0L;      }
        else if (i == 256) { s = 0L;      c = -FP_ONE; }
        else if (i == 384) { s = -FP_ONE; c = 0L;      }
        else
        {
            ds = c / DTHETA_DIV;
            c -= s / DTHETA_DIV;
            s += ds;
        }
    }
}

/* =========================================================
   CALCUL DU ZOOM INVERSE
   =========================================================
   Retourne 1/zoom_reel en 16.16.
   zoom_reel oscille entre 0.6 et 2.4 (période 4 s).
   elapsed_ticks : ticks depuis le début de la scène.
   ========================================================= */
static long computeZoomInv(unsigned long elapsed_ticks)
{
    int  phase;
    long sval;
    long zoom_fp16;
    long inv;

    phase     = (int)((elapsed_ticks * ANGLE_STEPS) / ZOOM_PERIOD_TICKS)
                & (ANGLE_STEPS - 1);
    sval      = SIN_FP(phase);
    zoom_fp16 = 98304L + (sval * 9L) / 10L;
    if (zoom_fp16 < 26214L) zoom_fp16 = 26214L;
    inv       = (FP_ONE / (zoom_fp16 >> 8)) << 8;
    return inv;
}

/* =========================================================
   RENDU D'UNE FRAME
   ========================================================= */
static void renderFrame(unsigned long elapsed_ticks)
{
    long inv_zoom;
    long cos_a, sin_a;
    long row_u0, row_v0;
    long du_dx, dv_dx;
    long u, v;
    long dy;
    int  px, py;
    int  tx, ty;
    unsigned char *dst;

    inv_zoom = computeZoomInv(elapsed_ticks);

    cos_a = (COS_FP(angle) * (inv_zoom >> 8)) >> 8;
    sin_a = (SIN_FP(angle) * (inv_zoom >> 8)) >> 8;

    du_dx = cos_a;
    dv_dx = sin_a;

    dst = backbuffer;

    for (py = 0; py < SCREEN_HEIGHT; py++)
    {
        dy = (long)(py - SCR_CY);

        row_u0 = ((long)TEX_CX << 16)
                 + cos_a * (long)(0 - SCR_CX)
                 - sin_a * dy;
        row_v0 = ((long)TEX_CY << 16)
                 + sin_a * (long)(0 - SCR_CX)
                 + cos_a * dy;

        u = row_u0;
        v = row_v0;

        for (px = 0; px < SCREEN_WIDTH; px++)
        {
            tx = (int)((u >> 16) & TEX_MASK);
            ty = (int)((v >> 16) & TEX_MASK);
            dst[px] = TEX_PIXEL(tx, ty);
            u += du_dx;
            v += dv_dx;
        }
        dst += SCREEN_WIDTH;
    }
}

/* =========================================================
   CHARGEMENT DE LA TEXTURE
   =========================================================
   Lecture ligne par ligne (TEX_W octets par appel), copiée
   directement dans tex0 puis tex1 selon la ligne en cours.
   Premiere moitie -> tex0, deuxieme -> tex1.
   Retourne 1 si succes, 0 si echec.
   ========================================================= */
static int loadTexture(void)
{
    FILE              *f;
    unsigned int       row;
    unsigned char *dst;

    f = fopen("images\\freedos.raw", "rb");
    if (!f) return 0;

    dst = tex0;
    for (row = 0; row < TEX_H / 2; row++)
    {
        if (fread(dst, 1, TEX_W, f) != TEX_W)
            { fclose(f); return 0; }
        dst += TEX_W;
    }

    dst = tex1;
    for (row = 0; row < TEX_H / 2; row++)
    {
        if (fread(dst, 1, TEX_W, f) != TEX_W)
            { fclose(f); return 0; }
        dst += TEX_W;
    }

    fclose(f);
    return 1;
}

/* =========================================================
   LIBERATION MEMOIRE
   ========================================================= */
static void freeScene6(void)
{
    if (tex0)    { free(tex0);    tex0    = NULL; }
    if (tex1)    { free(tex1);    tex1    = NULL; }
    if (sin_tab) { free(sin_tab); sin_tab = NULL; }
}





/* =========================================================
   INIT — appelée UNE fois au lancement de la scène
   ========================================================= */
static void scene6Init(void)
{
    int err;

    /* Table sinus : 512 * sizeof(long) = 2048 octets */
    sin_tab = (long *)malloc(ANGLE_STEPS * sizeof(long));
    if (!sin_tab) { quitRequested = 1; return; }
    buildSinTable();

    /* Texture split en deux blocs de 32768 octets */
    tex0 = (unsigned char *)malloc(TEX_HALF);
    if (!tex0) { freeScene6(); quitRequested = 1; return; }

    tex1 = (unsigned char *)malloc(TEX_HALF);
    if (!tex1) { freeScene6(); quitRequested = 1; return; }

    err = loadPalette("images\\freedos.pal");
    if (err != PAL_OK) { freeScene6(); quitRequested = 1; return; }

    if (!loadTexture()) { freeScene6(); quitRequested = 1; return; }

    angle = 0;
}





/* =========================================================
   RENDER — appelée à chaque image (cadence FRAME_TICKS)
   ---------------------------------------------------------
   phase    : 0 = intro, 1 = corps, 2 = outro   (phases du gabarit)
   progress : avancement DANS cette phase, de 0 à 1000
   elapsed  : ticks écoulés depuis le début de la scène
   ========================================================= */
static void scene6Render(unsigned long elapsed, int phase, unsigned long progress)
{
    float t;

    if (phase == 2)
    {
        /* OUTRO : fondu de palette vers le noir (la rotation est figée,
           le zoom continue d'évoluer avec elapsed). */
        t = 1.0f - (float)progress / 1000.0f;
        fadePalette(workingPalette, t);
    }
    else
    {
        /* CORPS : rotozoom. */
        angle = (angle + 2) & (ANGLE_STEPS - 1);
    }

    renderFrame(elapsed);
    flip();
}





/* =========================================================
   CLEANUP — appelée UNE fois à la fin de la scène
   ========================================================= */
static void scene6Cleanup(void)
{
    freeScene6();
}





/* =========================================================
   POINT D'ENTRÉE — gestion du timer (NE PAS MODIFIER)
   ========================================================= */
void scene6(void)
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
        scene6Init();
        return;                     /* le 1er rendu se fera au tour suivant */
    }

    elapsed = elapsedTime(sceneStart, now);

    /* 2. Fin de scène : test AVANT le rendu */
    if (elapsed >= sceneTicks)
    {
        scene6Cleanup();
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
    scene6Render(elapsed, phase, progress);
}
