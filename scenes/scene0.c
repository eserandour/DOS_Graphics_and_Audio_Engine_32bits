/* =========================================================
   SCENE0.C — Scène : écran noir
   =========================================================
   Durée totale : 3 secondes.
   Version minimale du gabarit scene_template.c : pas de fondu,
   pas de rendu, pas de cadence (l'écran est fixe).
   Aucune gestion clavier (sauf Échap global via INT 09h).
   ========================================================= */

#include "timer.h"
#include "video.h"
#include "graphics.h"
#include "scene.h"

#define SCENE_MS     3000UL   /* durée totale */

void scene0(void)
{
    static int initialized = 0;
    unsigned long now = readTimer();

    if (!initialized)
    {
        initialized = 1;
        sceneStart  = now;          /* démarre le chrono */
        clearScreen(0);             /* écran noir        */
        flip();
        return;
    }

    if (elapsedTimeMs(sceneStart, now) >= SCENE_MS)
    {
        initialized = 0;            /* état remis à zéro AVANT de rendre la main */
        sceneSignalEnd();
    }
}
