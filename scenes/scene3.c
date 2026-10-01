/* =========================================================
   SCENE3.C — Scène : Démonstration des polices font1
   =========================================================
   6 sous-écrans affichés automatiquement, 3 s chacun :
     0 — font1Bios  8x8    (0..127)
     1 — font1Bank  8x8    (0..255)
     2 — font1Bank  8x16   (0..127)
     3 — font1Bank  8x16   (128..255)
     4 — font1Bank  16x16  (0..127)
     5 — font1Bank  16x16  (128..255)
   Aucune gestion clavier (sauf Échap global via INT 09h).

   TIMER — scène basée sur scene_template.c (voir ce fichier pour
   les règles) : tout le minutage est en ticks (70 Hz).
   Chaque sous-écran dure SCREEN_MS = 6 s (36 s au total) ; fondu
   entrant 2 s, fondu sortant 1 s.
   ========================================================= */





#include "timer.h"
#include "video.h"
#include "palette.h"
#include "graphics.h"
#include "font1.h"
#include "scene.h"





/* ---------------------------------------------------------
   Réglages de la scène
   --------------------------------------------------------- */
#define NB_SCREENS   6
#define SCREEN_MS    6000UL   /* durée d'un sous-écran */

#define SCENE_MS     (NB_SCREENS * SCREEN_MS)   /* 36 s */
#define FADE_IN_MS   2000UL   /* durée du fondu entrant  */
#define FADE_OUT_MS  1000UL   /* durée du fondu sortant  */

/* Cadence : 1 tick = 70 Hz, pour un fondu de palette fluide.
   Les images sont légères : le sous-écran n'est redessiné que lorsqu'il change. */
#define FRAME_TICKS     1UL

/* ms -> ticks, arrondi ; minimum 1 tick, sauf 0 ms qui reste 0 tick
   (= phase supprimée, ex. FADE_IN_MS 0UL). Ne pas modifier. */
#define MS_TO_TICKS(ms) \
    ((ms) == 0UL ? 0UL : \
     ((((ms) * TARGET_HZ + 500UL) / 1000UL) ? (((ms) * TARGET_HZ + 500UL) / 1000UL) : 1UL))

#define SCREEN_TICKS  MS_TO_TICKS(SCREEN_MS)





/* ---------------------------------------------------------
   Variables propres à la scène
   --------------------------------------------------------- */
static int lastScreen = -1;   /* dernier sous-écran dessiné */





/* =========================================================
   FONCTION LOCALE : dessin d'un sous-écran
   ========================================================= */

static void drawScreen(int screen)
{
    int c, col, row;

    clearScreen(0);

    switch (screen)
    {
        case 0:
        {
            int startX = (SCREEN_WIDTH  - 16 * 10) / 2;
            int startY = (SCREEN_HEIGHT -  8 * 10) / 2 + 9;
            font1DrawTextCentered(4, "font1Bios 8x8 - 0..127", 255, &FONT1_BIOS);
            drawLine(4, 15, 315, 15, 100);
            for (c = 0; c < 128; c++)
            {
                col = c % 16; row = c / 16;
                font1DrawChar(startX + col * 10, startY + row * 10,
                              (unsigned char)c, 255, &FONT1_BIOS);
            }
            break;
        }
        case 1:
        {
            int startX = (SCREEN_WIDTH  - 16 * 10) / 2;
            int startY = (SCREEN_HEIGHT - 16 * 10) / 2 + 9;
            font1DrawTextCentered(4, "font1Bank 8x8 - 0..255", 255, &FONT1_BIOS);
            drawLine(4, 15, 315, 15, 100);
            for (c = 0; c < 256; c++)
            {
                col = c % 16; row = c / 16;
                font1DrawChar(startX + col * 10, startY + row * 10,
                              (unsigned char)c, 255, &FONT1_BANK_8X8);
            }
            break;
        }
        case 2:
        case 3:
        {
            int base   = (screen - 2) * 128;
            int startX = (SCREEN_WIDTH  - 16 * 10) / 2;
            int startY = (SCREEN_HEIGHT -  8 * 18) / 2 + 9;
            font1DrawTextCentered(4, screen == 2
                ? "font1Bank 8x16 - 0..127"
                : "font1Bank 8x16 - 128..255",
                255, &FONT1_BIOS);
            drawLine(4, 15, 315, 15, 100);
            for (c = 0; c < 128; c++)
            {
                col = c % 16; row = c / 16;
                font1DrawChar(startX + col * 10, startY + row * 18,
                              (unsigned char)(base + c), 255, &FONT1_BANK_8X16);
            }
            break;
        }
        case 4:
        case 5:
        {
            int base   = (screen - 4) * 128;
            int startX = (SCREEN_WIDTH  - 16 * 18) / 2;
            int startY = (SCREEN_HEIGHT -  8 * 18) / 2 + 9;
            font1DrawTextCentered(4, screen == 4
                ? "font1Bank 16x16 - 0..127"
                : "font1Bank 16x16 - 128..255",
                255, &FONT1_BIOS);
            drawLine(4, 15, 315, 15, 100);
            for (c = 0; c < 128; c++)
            {
                col = c % 16; row = c / 16;
                font1DrawChar(startX + col * 18, startY + row * 18,
                              (unsigned char)(base + c), 255, &FONT1_BANK_16X16);
            }
            break;
        }
    }

    flip();
}





/* =========================================================
   INIT — appelée UNE fois au lancement de la scène
   ========================================================= */
static void scene3Init(void)
{
    lastScreen = 0;

    font1InitBios();
    font1InitBank8x8();
    font1InitBank8x16();
    font1InitBank16x16();

    buildRedPalette(redPalette);
    copyPalette(workingPalette, redPalette);
    fadePalette(workingPalette, 0.0f);      /* on part du noir (DAC seulement) */

    drawScreen(0);
}





/* =========================================================
   RENDER — appelée à chaque image (cadence FRAME_TICKS)
   ---------------------------------------------------------
   phase    : 0 = intro, 1 = corps, 2 = outro   (phases du gabarit)
   progress : avancement DANS cette phase, de 0 à 1000
   elapsed  : ticks écoulés depuis le début de la scène
   ========================================================= */
static void scene3Render(unsigned long elapsed, int phase, unsigned long progress)
{
    int   screen;
    float t;

    /* Sous-écran courant : calculé depuis elapsed (et non par un
       screenStart relancé à chaque changement, qui faisait dériver). */
    screen = (int)(elapsed / SCREEN_TICKS);
    if (screen >= NB_SCREENS) screen = NB_SCREENS - 1;

    if (screen != lastScreen)
    {
        lastScreen = screen;
        drawScreen(screen);
    }

    /* -------------------------------------------------------
       Facteur de fondu (non bloquant), indépendant du
       sous-écran courant.
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
static void scene3Cleanup(void)
{
    /* Libère les Font1Bank avant scene6 pour éviter
       la fragmentation du tas :
       font2 (15 Ko) + font1 banks (~14 Ko) libérés
       en séquence avant les malloc(32768) x2 (tex0/tex1)
       et le malloc(2048) (sin_tab) de scene6. */
    font1FreeBank(&font1Bank8x8);
    font1FreeBank(&font1Bank8x16);
    font1FreeBank(&font1Bank16x16);
}





/* =========================================================
   POINT D'ENTRÉE — gestion du timer (NE PAS MODIFIER)
   ========================================================= */
void scene3(void)
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
        scene3Init();
        return;                     /* le 1er rendu se fera au tour suivant */
    }

    elapsed = elapsedTime(sceneStart, now);

    /* 2. Fin de scène : test AVANT le rendu */
    if (elapsed >= sceneTicks)
    {
        scene3Cleanup();
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
    scene3Render(elapsed, phase, progress);
}
