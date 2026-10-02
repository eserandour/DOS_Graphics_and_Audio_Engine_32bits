/* =========================================================
   SCENE0.C — Scène : écran noir
   =========================================================
   Durée totale : 3 secondes.
   Version réduite de scene_template.c : pas de fondu, pas de
   rendu, pas de cadence (l'écran est fixe, dessiné dans Init).
   Aucune gestion clavier (sauf Échap global via INT 09h).
   ========================================================= */

#include "timer.h"
#include "video.h"
#include "graphics.h"
#include "scene.h"

/* ---------------------------------------------------------
   Réglages de la scène
   --------------------------------------------------------- */
#define SCENE_MS    3000UL  /* durée totale */





/* =========================================================
   INIT — appelée UNE fois au lancement de la scène
   ========================================================= */
static void scene0Init(void)
{
    clearScreen(0);  /* écran noir */
    flip();
}





/* =========================================================
   POINT D'ENTRÉE — gestion du timer
   ========================================================= */
void scene0(void)
{
    static int initialized = 0;

    unsigned long now = readTimer();

    /* 1. Initialisation : une seule fois par lancement */
    if (!initialized)
    {
        initialized = 1;
        sceneStart  = now;
        scene0Init();
        return;
    }

    /* 2. Fin de scène */
    if (elapsedTimeMs(sceneStart, now) >= SCENE_MS)
    {
        initialized = 0;   /* état remis à zéro d'abord... */
        sceneSignalEnd();  /* ...puis on rend la main      */
    }
}
