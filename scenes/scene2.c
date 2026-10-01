/* =========================================================
   SCENE2.C — Scène : démonstration palette VGA
   =========================================================
   Phases et durées :
   1.  1 s : affichage statique (palette par défaut)
   2.  5 s : cycle de palette vers la droite
   3.  5 s : cycle de palette vers la gauche
   4.  3 s : lerp vers redPalette
   Aucune gestion clavier (sauf Échap global via INT 09h).

   TIMER — scène basée sur scene_template.c (voir ce fichier pour
   les règles) : tout le minutage est en ticks (70 Hz).
   Phases : 1 s statique, 5 s cycle droite, 5 s cycle gauche, 3 s lerp
   (14 s au total, sans fondu entrant/sortant).
   ========================================================= */





#include "timer.h"
#include "video.h"
#include "palette.h"
#include "graphics.h"
#include "scene.h"





/* ---------------------------------------------------------
   Réglages de la scène
   --------------------------------------------------------- */
#define D1_MS   1000UL   /* 1. statique       */
#define D2_MS   5000UL   /* 2. cycle droite   */
#define D3_MS   5000UL   /* 3. cycle gauche   */
#define D4_MS   3000UL   /* 4. lerp -> rouge  */

#define SCENE_MS     (D1_MS + D2_MS + D3_MS + D4_MS)   /* 14 s */
#define FADE_IN_MS      0UL   /* pas d'intro : les 4 phases sont gérées */
#define FADE_OUT_MS     0UL   /* dans scene2Render, à partir de elapsed */

/* Cadence : 2 ticks = 35 Hz (l'original visait 25 ms, soit 1,75 tick). */
#define FRAME_TICKS     2UL

/* ms -> ticks, arrondi ; minimum 1 tick, sauf 0 ms qui reste 0 tick
   (= phase supprimée, ex. FADE_IN_MS 0UL). Ne pas modifier. */
#define MS_TO_TICKS(ms) \
    ((ms) == 0UL ? 0UL : \
     ((((ms) * TARGET_HZ + 500UL) / 1000UL) ? (((ms) * TARGET_HZ + 500UL) / 1000UL) : 1UL))

/* Fin de chaque phase, en ticks depuis sceneStart. Les durées sont
   cumulées en ms AVANT la conversion : pas d'arrondis qui s'additionnent. */
#define T1_TICKS  MS_TO_TICKS(D1_MS)
#define T2_TICKS  MS_TO_TICKS(D1_MS + D2_MS)
#define T3_TICKS  MS_TO_TICKS(D1_MS + D2_MS + D3_MS)
#define T4_TICKS  MS_TO_TICKS(SCENE_MS)





/* ---------------------------------------------------------
   Variables propres à la scène
   --------------------------------------------------------- */
static int lerpReady = 0;   /* paletteA/paletteB déjà initialisées pour la phase 4 ? */





/* =========================================================
   FONCTION LOCALE : grille de palette
   ========================================================= */

/* Dessine la palette courante sous forme de grille 16x16.
   Chaque cellule est un carré de 10x10 pixels de couleur
   uniforme, avec 2 pixels d'espacement entre les cellules.
   La grille est centrée sur l'écran.
   static = visible uniquement dans ce fichier. */
static void drawPaletteGrid(void)
{
    const int gridSize = 16;  /* 16 colonnes et 16 lignes  */
    const int cellSize = 10;  /* taille d'une cellule (px) */
    const int spacing  = 2;   /* espace entre cellules (px)*/
    const int step     = cellSize + spacing;  /* pas entre deux cellules */

    /* Taille totale de la grille en pixels. */
    const int gridW = gridSize * cellSize + (gridSize - 1) * spacing;
    const int gridH = gridSize * cellSize + (gridSize - 1) * spacing;
    
    /* Offset de centrage. */
    const int offsetX = (SCREEN_WIDTH  - gridW) / 2;
    const int offsetY = (SCREEN_HEIGHT - gridH) / 2;

    int x, y;
    int i = 0;  /* index de couleur (0 à 255) */

    /* Parcourir les 16*16 = 256 cellules.
       i s'incrémente de gauche à droite, de haut en bas. */
    for (y = 0; y < gridSize; y++)
        for (x = 0; x < gridSize; x++)
        {
            int px = offsetX + x * step;  /* coin gauche de la cellule */
            int py = offsetY + y * step;  /* coin haut de la cellule   */
            /* Dessiner un carré plein de la couleur i.
               La couleur réelle dépend de la palette active
               dans le DAC VGA, pas des composantes RGB
               stockées dans workingPalette. */
            drawRectFill(px, py, px + cellSize - 1, py + cellSize - 1,
                         (unsigned char)i++);
        }
}





/* =========================================================
   INIT — appelée UNE fois au lancement de la scène
   ========================================================= */
static void scene2Init(void)
{
    lerpReady = 0;                          /* toujours réinitialiser au démarrage */

    copyPalette(workingPalette, defaultPalette);
    setPalette(workingPalette);
    buildRedPalette(redPalette);

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
static void scene2Render(unsigned long elapsed, int phase, unsigned long progress)
{
    float t;

    (void)phase; (void)progress;            /* phases gérées ci-dessous */

    if (elapsed < T1_TICKS)
    {
        /* Phase 1 : statique, palette par défaut. */
    }
    else if (elapsed < T2_TICKS)
    {
        /* Phase 2 : cycle vers la droite. */
        cyclePaletteRight(workingPalette, 0, 255);
    }
    else if (elapsed < T3_TICKS)
    {
        /* Phase 3 : cycle vers la gauche. */
        cyclePaletteLeft(workingPalette, 0, 255);
    }
    else
    {
        /* -------------------------------------------------------
           Phase 4 : lerp vers redPalette (T3 -> T4).
           On sauvegarde paletteA une seule fois au début de la
           phase pour que l'interpolation parte toujours du même
           point (et non de la palette en cours de lerp).
           ------------------------------------------------------- */
        if (!lerpReady)
        {
            copyPalette(paletteA, workingPalette);   /* état actuel -> source */
            copyPalette(paletteB, redPalette);       /* cible                 */
            lerpReady = 1;
        }

        t = (float)(elapsed - T3_TICKS) / (float)(T4_TICKS - T3_TICKS);   /* 0.0 -> 1.0 */
        lerpPalette(workingPalette, paletteA, paletteB, t);
        setPalette(workingPalette);
    }

    drawPaletteGrid();
    flip();
}





/* =========================================================
   CLEANUP — appelée UNE fois à la fin de la scène
   ========================================================= */
static void scene2Cleanup(void)
{
    /* Rien à libérer. */
}





/* =========================================================
   POINT D'ENTRÉE — gestion du timer (NE PAS MODIFIER)
   ========================================================= */
void scene2(void)
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
        scene2Init();
        return;                     /* le 1er rendu se fera au tour suivant */
    }

    elapsed = elapsedTime(sceneStart, now);

    /* 2. Fin de scène : test AVANT le rendu */
    if (elapsed >= sceneTicks)
    {
        scene2Cleanup();
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
    scene2Render(elapsed, phase, progress);
}
