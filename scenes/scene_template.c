/* =========================================================
   SCENE_TEMPLATE.C — Structure minimale d'une scène
   =========================================================
   Les zones où ajouter son code sont encadrées ainsi :

   >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
   >>>  VOTRE CODE ICI : ...                            <<<
   >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>

   Tout le reste (gestion du timer) est à laisser tel quel.

   Règles du timer (PIT à TARGET_HZ = 70 Hz) :

   1. On raisonne en TICKS. Les durées longues s'écrivent en
      ms et sont converties une fois avec MS_TO_TICKS (arrondi
      au plus proche, minimum 1 tick ; 0 ms reste 0 tick, ce
      qui supprime la phase) ; la cadence s'écrit directement
      en ticks (FRAME_TICKS).
   2. sceneStart est posé une seule fois, au premier appel du
      point d'entrée, juste avant sceneXInit() : c'est la seule
      référence de temps de la scène. (setScene() le pose aussi,
      mais le point d'entrée le repose au premier appel.)
   3. La fonction ne bloque JAMAIS (pas de pause(), pas de
      boucle d'attente) : main appelle audioUpdate() entre
      deux appels.
   4. Cadence : lastFrame avance par pas fixes (pas de
      lastFrame = now, qui fait dériver). Si on a pris plus
      d'une image de retard, on abandonne le retard au lieu
      de rattraper en rafale.
   5. Fin de scène : on remet l'état à zéro AVANT d'appeler
      sceneSignalEnd(), puis return. sceneSignalEnd() peut
      relancer tout de suite la même scène (ex. SCENE_0 en
      début et fin de playlist) ; elle doit repartir propre.
   ========================================================= */

#include "timer.h"
#include "video.h"
#include "palette.h"
#include "graphics.h"
#include "scene.h"

/* >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
   >>>  VOTRE CODE ICI : #include supplémentaires       <<<
   >>>  (font1.h, font2.h, image.h, audio.h, app.h...)  <<<
   >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>> */

/* ---------------------------------------------------------
   Réglages de la scène
   Durées en ms (lisibles, l'arrondi au tick est négligeable).
   Cadence en ticks (1 tick = 1000/70 = 14,29 ms : exprimer
   un intervalle court en ms serait trompeur).
   --------------------------------------------------------- */

/* >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
   >>>  VOTRE CODE ICI : durées et cadence de la scène  <<<
   >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>> */

#define SCENE_MS     5000UL   /* durée totale                 */
#define FADE_IN_MS    500UL   /* phase intro (0UL = pas d'intro)  */
#define FADE_OUT_MS   500UL   /* phase outro (0UL = pas d'outro)  */
#define FRAME_TICKS     2UL   /* ticks entre 2 rendus : 1=70 Hz, 2=35 Hz, 3=23,3 Hz, 4=17,5 Hz */

/* ms -> ticks, arrondi ; minimum 1 tick, sauf 0 ms qui reste 0 tick
   (= phase supprimée, ex. FADE_IN_MS 0UL). Ne pas modifier. */
#define MS_TO_TICKS(ms) \
    ((ms) == 0UL ? 0UL : \
     ((((ms) * TARGET_HZ + 500UL) / 1000UL) ? (((ms) * TARGET_HZ + 500UL) / 1000UL) : 1UL))





/* >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
   >>>  VOTRE CODE ICI : variables et données propres   <<<
   >>>  à la scène (static, niveau fichier)             <<<
   >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>> */





/* =========================================================
   INIT — appelée UNE fois au lancement de la scène
   ========================================================= */
static void sceneXInit(void)
{
    /* >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
       >>>  VOTRE CODE ICI : chargement palette, images, <<<
       >>>  polices, démarrage musique, état initial...  <<<
       >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>> */
    clearScreen(0);
    flip();
}





/* =========================================================
   RENDER — appelée à chaque image (cadence FRAME_TICKS)
   ---------------------------------------------------------
   phase    : 0 = intro, 1 = corps, 2 = outro
   progress : avancement DANS la phase, de 0 à 1000
              (entier, millièmes : pas de float nécessaire)
   elapsed  : ticks écoulés depuis le début de la scène
   ========================================================= */
static void sceneXRender(unsigned long elapsed, int phase, unsigned long progress)
{
    (void)elapsed;

    switch (phase)
    {
    case 0: /* ---- INTRO ---- */
        /* >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
           >>>  VOTRE CODE ICI : apparition (fondu in...)  <<<
           >>>  progress : 0 -> 1000                       <<<
           >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>> */
        (void)progress;
        break;

    case 1: /* ---- CORPS ---- */
        /* >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
           >>>  VOTRE CODE ICI : animation principale     <<<
           >>>  progress : 0 -> 1000                       <<<
           >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>> */
        break;

    case 2: /* ---- OUTRO ---- */
        /* >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
           >>>  VOTRE CODE ICI : disparition (fondu out)  <<<
           >>>  progress : 0 -> 1000                       <<<
           >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>> */
        break;
    }

    /* >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
       >>>  VOTRE CODE ICI : dessin commun aux 3 phases,  <<<
       >>>  puis flip() (une seule fois par image)        <<<
       >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>> */
    flip();
}





/* =========================================================
   CLEANUP — appelée UNE fois à la fin de la scène
   ========================================================= */
static void sceneXCleanup(void)
{
    /* >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
       >>>  VOTRE CODE ICI : libérer ce qu'Init a alloué  <<<
       >>>  (font2Free, stopMusic, fadeMusicOut...)       <<<
       >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>> */
}





/* =========================================================
   POINT D'ENTRÉE — gestion du timer (NE PAS MODIFIER,
   sauf le nom de la fonction : sceneX -> sceneN)
   ========================================================= */
void sceneX(void)
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
        sceneXInit();
        return;                     /* le 1er rendu se fera au tour suivant */
    }

    elapsed = elapsedTime(sceneStart, now);

    /* 2. Fin de scène : test AVANT le rendu */
    if (elapsed >= sceneTicks)
    {
        sceneXCleanup();
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
    sceneXRender(elapsed, phase, progress);
}
