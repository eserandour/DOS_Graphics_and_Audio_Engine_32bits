#ifndef SCENE_H
#define SCENE_H

/* =========================================================
   SCENE.H — Gestionnaire de scenes
   ========================================================= */

/* Nombre maximum de scenes gerees (SCENE_0 a SCENE_99). */
#define MAX_SCENES 100

typedef enum {
    SCENE_0  = 0,  SCENE_1  = 1,  SCENE_2  = 2,  SCENE_3  = 3,  SCENE_4  = 4,
    SCENE_5  = 5,  SCENE_6  = 6,  SCENE_7  = 7,  SCENE_8  = 8,  SCENE_9  = 9,
    SCENE_10 = 10, SCENE_11 = 11, SCENE_12 = 12, SCENE_13 = 13, SCENE_14 = 14,
    SCENE_15 = 15, SCENE_16 = 16, SCENE_17 = 17, SCENE_18 = 18, SCENE_19 = 19,
    SCENE_20 = 20, SCENE_21 = 21, SCENE_22 = 22, SCENE_23 = 23, SCENE_24 = 24,
    SCENE_25 = 25, SCENE_26 = 26, SCENE_27 = 27, SCENE_28 = 28, SCENE_29 = 29,
    SCENE_30 = 30, SCENE_31 = 31, SCENE_32 = 32, SCENE_33 = 33, SCENE_34 = 34,
    SCENE_35 = 35, SCENE_36 = 36, SCENE_37 = 37, SCENE_38 = 38, SCENE_39 = 39,
    SCENE_40 = 40, SCENE_41 = 41, SCENE_42 = 42, SCENE_43 = 43, SCENE_44 = 44,
    SCENE_45 = 45, SCENE_46 = 46, SCENE_47 = 47, SCENE_48 = 48, SCENE_49 = 49,
    SCENE_50 = 50, SCENE_51 = 51, SCENE_52 = 52, SCENE_53 = 53, SCENE_54 = 54,
    SCENE_55 = 55, SCENE_56 = 56, SCENE_57 = 57, SCENE_58 = 58, SCENE_59 = 59,
    SCENE_60 = 60, SCENE_61 = 61, SCENE_62 = 62, SCENE_63 = 63, SCENE_64 = 64,
    SCENE_65 = 65, SCENE_66 = 66, SCENE_67 = 67, SCENE_68 = 68, SCENE_69 = 69,
    SCENE_70 = 70, SCENE_71 = 71, SCENE_72 = 72, SCENE_73 = 73, SCENE_74 = 74,
    SCENE_75 = 75, SCENE_76 = 76, SCENE_77 = 77, SCENE_78 = 78, SCENE_79 = 79,
    SCENE_80 = 80, SCENE_81 = 81, SCENE_82 = 82, SCENE_83 = 83, SCENE_84 = 84,
    SCENE_85 = 85, SCENE_86 = 86, SCENE_87 = 87, SCENE_88 = 88, SCENE_89 = 89,
    SCENE_90 = 90, SCENE_91 = 91, SCENE_92 = 92, SCENE_93 = 93, SCENE_94 = 94,
    SCENE_95 = 95, SCENE_96 = 96, SCENE_97 = 97, SCENE_98 = 98, SCENE_99 = 99
} Scene;

extern Scene currentScene;
extern unsigned long sceneStart;

/* Callback appelé par sceneSignalEnd() quand une scène se déclare
   terminée. L'implémentation (ex: main.c) décide quelle scène vient
   ensuite.
   Signature : void handler(Scene sceneQuiVientDeFinir); */
typedef void (*SceneEndHandler)(Scene);
extern SceneEndHandler onSceneEnd;

void setScene(Scene s);
void runCurrentScene(void);

/* Appelé par une scène pour signaler qu'elle est terminée.
   La scène ne choisit PAS la suivante : c'est onSceneEnd qui décide. */
void sceneSignalEnd(void);

#endif /* SCENE_H */
