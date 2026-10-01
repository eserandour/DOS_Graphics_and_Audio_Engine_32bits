/* =========================================================
   SCENE1.C — Scène : pixels aléatoires
   =========================================================
   Basée sur scene_template.c (voir ce fichier pour les
   règles de gestion du timer).

   Durée totale : 6 secondes.
   Fade in  non bloquant : 0 -> 1 s      (t : 0.0 -> 1.0)
   Corps    pleine luminosité : 1 s -> 3 s
   Fade out non bloquant : 3 s -> 6 s    (t : 1.0 -> 0.0)
   Pixels aléatoires renouvelés toutes les 100 ms.
   Aucune gestion clavier (sauf Échap global via INT 09h).
   ========================================================= */

#include <time.h>
#include "timer.h"
#include "video.h"
#include "palette.h"
#include "graphics.h"
#include "scene.h"





/* ---------------------------------------------------------
   Réglages de la scène
   --------------------------------------------------------- */
#define SCENE_MS     6000UL   /* durée totale                      */
#define FADE_IN_MS   1000UL   /* phase intro                       */
#define FADE_OUT_MS  3000UL   /* phase outro                       */

/* Cadence de rendu : 1 tick (70 Hz), pour que le fondu de palette
   soit aussi fluide que possible. Les pixels, eux, ne sont renouvelés
   que toutes les PIXELS_TICKS (voir scene1Render). */
#define FRAME_TICKS     1UL

/* 100 ms = 7 ticks exactement (100 * 70 / 1000) */
#define PIXELS_TICKS    7UL

/* ms -> ticks, arrondi ; minimum 1 tick, sauf 0 ms qui reste 0 tick
   (= phase supprimée, ex. FADE_IN_MS 0UL). Ne pas modifier. */
#define MS_TO_TICKS(ms) \
    ((ms) == 0UL ? 0UL : \
     ((((ms) * TARGET_HZ + 500UL) / 1000UL) ? (((ms) * TARGET_HZ + 500UL) / 1000UL) : 1UL))





/* ---------------------------------------------------------
   Variables propres à la scène
   --------------------------------------------------------- */
static unsigned long lcg_state     = 0UL;   /* état du générateur LCG            */
static unsigned long nextPixelsAt  = 0UL;   /* prochain renouvellement (ticks
                                               depuis sceneStart)                 */





/* =========================================================
   INIT — appelée UNE fois au lancement de la scène
   ========================================================= */
static void scene1Init(void)
{
    lcg_state    = (unsigned long)time(NULL);
    nextPixelsAt = 0UL;                     /* 1er renouvellement immédiat */

    copyPalette(workingPalette, defaultPalette);
    fadePalette(workingPalette, 0.0f);      /* on part du noir (DAC seulement) */

    clearScreen(0);
    flip();
}





/* =========================================================
   RENDER — appelée à chaque image (cadence FRAME_TICKS)
   ---------------------------------------------------------
   phase    : 0 = intro, 1 = corps, 2 = outro
   progress : avancement DANS la phase, de 0 à 1000
   elapsed  : ticks écoulés depuis le début de la scène
   ========================================================= */
static void scene1Render(unsigned long elapsed, int phase, unsigned long progress)
{
    unsigned long *dst;
    unsigned long i;
    float         t;

    /* -------------------------------------------------------
       Facteur de fondu courant (non bloquant)
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

    /* -------------------------------------------------------
       Renouvellement des pixels toutes les PIXELS_TICKS.
       On écrit des blocs de 4 octets (unsigned long, 32 bits)
       plutôt que de dépendre de la taille de "unsigned int" :
       nombre d'itérations fixe (BACKBUFFER_SIZE / 4) et 32 bits
       d'entropie du LCG à chaque écriture.
       flip() n'est appelé que lorsque le backbuffer a changé.
       ------------------------------------------------------- */
    if (elapsed >= nextPixelsAt)
    {
        dst = (unsigned long *)backbuffer;

        for (i = 0; i < BACKBUFFER_SIZE / 4UL; i++)
        {
            lcg_state = lcg_state * 1664525UL + 1013904223UL;
            dst[i]    = lcg_state;
        }

        flip();

        nextPixelsAt += PIXELS_TICKS;               /* pas fixe : pas de dérive */
        if (nextPixelsAt <= elapsed)
            nextPixelsAt = elapsed + PIXELS_TICKS;  /* trop de retard : on abandonne le rattrapage */
    }

    /* -------------------------------------------------------
       Application du fondu sur la palette.
       fadePalette() applique t à workingPalette et envoie
       directement au DAC, sans modifier workingPalette en RAM.
       ------------------------------------------------------- */
    fadePalette(workingPalette, t);
}





/* =========================================================
   CLEANUP — appelée UNE fois à la fin de la scène
   ========================================================= */
static void scene1Cleanup(void)
{
    /* Rien à libérer. */
}





/* =========================================================
   POINT D'ENTRÉE — gestion du timer (NE PAS MODIFIER)
   ========================================================= */
void scene1(void)
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
        scene1Init();
        return;                     /* le 1er rendu se fera au tour suivant */
    }

    elapsed = elapsedTime(sceneStart, now);

    /* 2. Fin de scène : test AVANT le rendu */
    if (elapsed >= sceneTicks)
    {
        scene1Cleanup();
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
    scene1Render(elapsed, phase, progress);
}
