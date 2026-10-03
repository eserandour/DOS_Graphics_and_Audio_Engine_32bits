/* =========================================================
   SCENE4.C — Affiche HELLO et WORLD avec font2
   =========================================================
   Charge la palette font.pal puis affiche, centres
   horizontalement avec font2DrawTextCentered :
     - "HELLO" a y=45
     - "WORLD" a y=122
   Duree : 3 secondes
   Aucune gestion clavier (sauf Échap global via INT 09h).

   TIMER — scène basée sur scene_template.c (voir ce fichier pour
   les règles) : tout le minutage est en ticks (70 Hz).
   Écran statique : tout est dessiné dans scene4Init.
   ========================================================= */





#include "timer.h"
#include "video.h"
#include "graphics.h"
#include "palette.h"
#include "font2.h"
#include "scene.h"
#include "app.h"





/* ---------------------------------------------------------
   Réglages de la scène
   --------------------------------------------------------- */
#define SCENE_MS     3000UL   /* durée totale                      */
#define FADE_IN_MS      0UL   /* pas de fondu                      */
#define FADE_OUT_MS     0UL

/* Scène statique : tout est dessiné dans Init, rien à rafraîchir. */
#define FRAME_TICKS     1UL

/* ms -> ticks, arrondi ; minimum 1 tick, sauf 0 ms qui reste 0 tick
   (= phase supprimée, ex. FADE_IN_MS 0UL). Ne pas modifier. */
#define MS_TO_TICKS(ms) \
    ((ms) == 0UL ? 0UL : \
     ((((ms) * TARGET_HZ + 500UL) / 1000UL) ? (((ms) * TARGET_HZ + 500UL) / 1000UL) : 1UL))





/* ---------------------------------------------------------
   Variables propres à la scène
   --------------------------------------------------------- */
/* Descripteur global (init statique au niveau fichier = OK pour Watcom) */
static Font2Desc s4_font = FONT2_DESC_DEFAULT;





/* =========================================================
   INIT — appelée UNE fois au lancement de la scène
   ========================================================= */
static void scene4Init(void)
{
    int err;

    err = loadPalette("font2\\font.pal");
    if (err != PAL_OK) { quitRequested = 1; return; }
    if (!font2Load(&s4_font)) { quitRequested = 1; return; }

    clearScreen(0);
    font2DrawTextCentered(&s4_font, "HELLO", 45);
    font2DrawTextCentered(&s4_font, "WORLD", 122);
    flip();
}





/* =========================================================
   RENDER — appelée à chaque image (cadence FRAME_TICKS)
   ---------------------------------------------------------
   phase    : 0 = intro, 1 = corps, 2 = outro   (phases du gabarit)
   progress : avancement DANS cette phase, de 0 à 1000
   elapsed  : ticks écoulés depuis le début de la scène
   ========================================================= */
static void scene4Render(unsigned long elapsed, int phase, unsigned long progress)
{
    (void)elapsed; (void)phase; (void)progress;

    /* Rien à faire : l'écran est fixe, dessiné dans scene4Init. */
}





/* =========================================================
   CLEANUP — appelée UNE fois à la fin de la scène
   ========================================================= */
static void scene4Cleanup(void)
{
    font2Free(&s4_font);
}





/* =========================================================
   POINT D'ENTRÉE — gestion du timer (NE PAS MODIFIER)
   ========================================================= */
void scene4(void)
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
        scene4Init();
        return;                     /* le 1er rendu se fera au tour suivant */
    }

    elapsed = elapsedTime(sceneStart, now);

    /* 2. Fin de scène : test AVANT le rendu */
    if (elapsed >= sceneTicks)
    {
        scene4Cleanup();
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
    scene4Render(elapsed, phase, progress);
}
