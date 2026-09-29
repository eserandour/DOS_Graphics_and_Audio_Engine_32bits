/* =========================================================
   SCENE.C — Gestionnaire de scenes
   ========================================================= */

#include "timer.h"
#include "scene.h"

/* Prototypes des scenes : fichier genere par BUILD.BAT */
#include "scenedcl.h"

Scene currentScene = SCENE_0;
unsigned long sceneStart = 0;
SceneEndHandler onSceneEnd = 0;   /* NULL par défaut — à brancher dans main.c */

typedef void (*SceneFunc)(void);

/* Table des scenes : entrees generees par BUILD.BAT */
static SceneFunc scenes[] = {
#include "scenetab.h"
};

#define NB_SCENES_LINKED (sizeof(scenes) / sizeof(scenes[0]))

void setScene(Scene s)
{
    currentScene = s;
    sceneStart   = readTimer();
}

void runCurrentScene(void)
{
    if ((unsigned) currentScene < NB_SCENES_LINKED)
        scenes[currentScene]();
}

void sceneSignalEnd(void)
{
    if (onSceneEnd)
        onSceneEnd(currentScene);
}
