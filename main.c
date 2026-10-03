/* =========================================================
   MAIN.C — Point d'entrée et arrêt propre
   =========================================================
   Environnement : Open Watcom 1.9, DOS
   Mode vidéo    : 13h (320x200, 256 couleurs)

   Échap est détecté par un handler INT 09h installé dans
   keyboard.c, qui lève quitRequested sans consommer la touche.
   main ne lit jamais le buffer clavier.
   ========================================================= */

#include <stdlib.h>
#include <time.h>
#include "timer.h"
#include "keyboard.h"
#include "app.h"
#include "video.h"
#include "palette.h"
#include "font1.h"
#include "scene.h"
#include "audio.h"

void shutdown(void)
{
    /* Couper le DMA/IRQ audio en tout premier, avant de toucher aux
       autres vecteurs — voir audio.h ("SORTIE PROPRE (Échap)"). */
    audioShutdown();
    restoreKeyboard();
    restoreTimer();
    setVideoMode(0x03);
    freeBackbuffer();
}

/* =========================================================
   ORCHESTRATEUR — playlist de scènes
   =========================================================
   Pour modifier l'ordre ou répéter une scène, il suffit
   d'éditer ce tableau. Les scènes appellent sceneSignalEnd()
   sans savoir ce qui les suit.
   ========================================================= */

static const Scene playlist[] = {
    SCENE_0,  /* 3 secondes d'écran noir (pour la capture vidéo) */
    SCENE_1,  /* pixels aléatoires (LCG)                        */
    SCENE_2,  /* démonstration palette VGA                      */
    SCENE_3,  /* démonstration des polices font1                */
    SCENE_4,  /* affichage "HELLO" / "WORLD" avec font2         */
    SCENE_5,  /* scrolling de texte                             */
    SCENE_6,  /* rotozoom freedos                               */
    SCENE_7,  /* démonstration primitives graphics              */
    SCENE_8,  /* démonstration moteur audio                     */
    SCENE_0,  /* 3 secondes d'écran noir (pour la capture vidéo) */
};
#define PLAYLIST_LEN (sizeof(playlist) / sizeof(playlist[0]))

static int playlistIdx = 0;

/* Mettre à 0 pour que la démo s'arrête après la dernière scène. */
#define DEMO_LOOP 1

static void handleSceneEnd(Scene finished)
{
    (void)finished;   /* non utilisé : on suit le curseur, pas la scène */

    if (playlistIdx + 1 >= (int)PLAYLIST_LEN)
    {
        if (!DEMO_LOOP) { quitRequested = 1; return; }
        playlistIdx = 0;
    }
    else
    {
        playlistIdx++;
    }

    setScene(playlist[playlistIdx]);
}

int main(void)
{
    srand((unsigned int)time(NULL));

    if (!initBackbuffer())
        return 1;

    setVideoMode(0x13);
    getPalette(defaultPalette);
    installTimer();
    installKeyboard();

    /* Si aucune carte son n'est détectée (BLASTER absent, DSP muet),
       audioInit() échoue proprement : playMusic()/playSound() restent
       de simples no-op et la démo continue normalement en silence. */
    audioInit();

    /* Brancher l'orchestrateur avant la première scène. */
    onSceneEnd = handleSceneEnd;

    playlistIdx = 0;
    setScene(playlist[0]);

    while (!quitRequested)
    {
        runCurrentScene();
        audioUpdate();   /* remplit la moitié de buffer audio libérée */
    }

    shutdown();
    return 0;
}
