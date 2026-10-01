/* =========================================================
   SCENE8.C — Scène de test : cycle de vie complet du moteur
              audio S3M (playMusic / fadeMusicIn / fadeMusicOut
              / stopMusic)
   =========================================================
   Enchaîne les quatre phases du cycle de vie audio sur
   musique.s3m ("Starshine - PM", Gv=48 Mv=48 — un morceau qui
   n'est PAS censé sortir à 100 % du numérique, bon test pour
   vérifier que Gv/Mv sont bien respectés, voir la section
   VOLUME de audio.h) :

     0.0s -  6.0s : FADE IN   — fadeMusicIn(6000)
     6.0s - 22.0s : LECTURE   — plein volume "normal" du morceau
    22.0s - 28.0s : FADE OUT  — fadeMusicOut(6000)
    28.0s - 32.0s : ARRET     — stopMusic(), mémoire libérée      

   L'écran affiche en direct la phase courante, le statut
   isMusicPlaying(), et le nombre de bouclages détectés via
   hasMusicLooped() (compteur cumulé — voir audio.h : le drapeau
   est consommé à chaque appel, donc on l'accumule nous-mêmes).
   Ce morceau dure plusieurs minutes (40 ordres/motifs à
   speed=6/tempo=125) : ne pas s'étonner que ce compteur reste à
   0 sur les 32 secondes de la scène, c'est attendu — le
   mécanisme est démontré, pas un bouclage complet.

   AFFICHAGE — police FONT1_BIOS (8x8), écran 320 px : toutes
   les chaînes affichées sont volontairement tenues sous ~36
   caractères pour ne jamais déborder à l'écran.

   NOTE C89 (Open Watcom)
   ----------------------
   Toutes les déclarations en tête de bloc.

   TIMER — scène basée sur scene_template.c (voir ce fichier pour
   les règles) : tout le minutage est en ticks (70 Hz).
   Durée : 32 s (6 s fade in, 16 s lecture, 6 s fade out, 4 s arrêt).
   ========================================================= */





#include <stdio.h>
#include "timer.h"
#include "video.h"
#include "graphics.h"
#include "font1.h"
#include "scene.h"
#include "audio.h"
#include "palette.h"





/* ---------------------------------------------------------
   Réglages de la scène
   --------------------------------------------------------- */
#define MUSIC_FADE_MS   6000UL    /* durée de chaque fondu (3000 à l'origine) */
#define PLAY_MS        16000UL    /* palier plein volume entre les deux fondus */
#define STOPPED_MS      4000UL    /* palier "arrêté" après stopMusic()         */

#define T_FADE_IN_END   (MUSIC_FADE_MS)
#define T_PLAY_END      (T_FADE_IN_END + PLAY_MS)
#define T_FADE_OUT_END  (T_PLAY_END + MUSIC_FADE_MS)
#define T_SCENE_END     (T_FADE_OUT_END + STOPPED_MS)

#define SCENE_MS     T_SCENE_END   /* 32 s */
#define FADE_IN_MS      0UL        /* pas d'intro/outro visuelle : les 4 phases */
#define FADE_OUT_MS     0UL        /* audio sont gérées dans scene8Render       */

/* Cadence : 4 ticks (~17 Hz). Léger exprès : voir le commentaire sur
   l'affichage, dans scene8Render. */
#define FRAME_TICKS     4UL

/* Affichage rafraîchi au plus toutes les 18 ticks (~250 ms, ~4 fois/s). */
#define DRAW_TICKS     18UL

/* ms -> ticks, arrondi ; minimum 1 tick, sauf 0 ms qui reste 0 tick
   (= phase supprimée, ex. FADE_IN_MS 0UL). Ne pas modifier. */
#define MS_TO_TICKS(ms) \
    ((ms) == 0UL ? 0UL : \
     ((((ms) * TARGET_HZ + 500UL) / 1000UL) ? (((ms) * TARGET_HZ + 500UL) / 1000UL) : 1UL))

/* Fins de phase, en ticks depuis sceneStart */
#define T_FADE_IN_END_TICKS   MS_TO_TICKS(T_FADE_IN_END)
#define T_PLAY_END_TICKS      MS_TO_TICKS(T_PLAY_END)
#define T_FADE_OUT_END_TICKS  MS_TO_TICKS(T_FADE_OUT_END)

/* ticks -> ms, pour l'affichage uniquement */
#define TICKS_TO_MS(t)  ((t) * 1000UL / TARGET_HZ)

/* Phases, dans l'ordre chronologique. */
#define PHASE_FADE_IN   0
#define PHASE_PLAYING   1
#define PHASE_FADE_OUT  2
#define PHASE_STOPPED   3





/* ---------------------------------------------------------
   Variables propres à la scène
   --------------------------------------------------------- */
static int           lastPhase  = -1;        /* dernière phase audio traitée */
static int           loopCount  = 0;
static int           loadStatus = AUD_OK;
static unsigned long nextDrawAt = 0UL;       /* prochain affichage (ticks depuis sceneStart) */





static const char *statusText(int r)
{
    switch (r)
    {
    case AUD_OK:         return "OK";
    case AUD_ERR_NOCARD: return "ERREUR: pas de carte son (BLASTER)";
    case AUD_ERR_FILE:   return "ERREUR: musique.s3m introuvable";
    case AUD_ERR_FORMAT: return "ERREUR: fichier .s3m invalide/tronque";
    case AUD_ERR_MEM:    return "ERREUR: memoire insuffisante";
    default:             return "ERREUR: inconnue";
    }
}

static const char *phaseText(int phase)
{
    switch (phase)
    {
    case PHASE_FADE_IN:  return "FADE IN   0% -> 100% (6s)";
    case PHASE_PLAYING:  return "LECTURE - volume normal du fichier";
    case PHASE_FADE_OUT: return "FADE OUT  100% -> 0% (6s)";
    default:              return "ARRET - stopMusic(), memoire liberee";
    }
}

static void formatSeconds(char *buf, unsigned long ms)
{
    unsigned long sec  = ms / 1000UL;
    unsigned long dsec = (ms % 1000UL) / 100UL;
    sprintf(buf, "%lu.%lus", sec, dsec);
}





/* =========================================================
   INIT — appelée UNE fois au lancement de la scène
   ========================================================= */
static void scene8Init(void)
{
    lastPhase  = -1;
    loopCount  = 0;
    nextDrawAt = 0UL;

    font1InitBios();

    /* Cette scène affiche du texte avec des indices de couleur fixes
       (15, 14, 8, 7, 4) en supposant la palette VGA par défaut.
       Sans ce reset, un passage précédent par une scène qui modifie
       la palette (ex : scene7, qui termine sur un fondu au noir de
       rainbowPalette) laisse le DAC dans un état quasi noir : le
       texte serait alors dessiné avec les bons indices, mais ces
       indices pointeraient vers des teintes invisibles. Vu au
       deuxième passage sur scene8 dans la playlist bouclée. */
    copyPalette(workingPalette, defaultPalette);
    setPalette(workingPalette);

    loadStatus = playMusic("audios\\musique.s3m");

    /* La musique vient de démarrer à plein volume "normal" (Gv/Mv
       du fichier, voir stopMusic()/playMusic() dans s3m.c qui
       remettent le fondu à 127/127 au chargement) : pour vraiment
       démarrer de RIEN, on la coupe instantanément (durée 0 =
       application immédiate, voir s3mFadeTo) avant de lancer la
       vraie montée progressive juste après. Sans effet si le
       chargement a échoué (fadeMusicOut/fadeMusicIn ne plantent
       jamais, voir audio.c). */
    fadeMusicOut(0);
    fadeMusicIn(MUSIC_FADE_MS);

    /* Exception au gabarit : le chrono repart ICI, après le chargement
       de musique.s3m (~290 Ko), pour que ce temps de chargement ne soit
       pas décompté de la durée de la scène (comme dans l'original). */
    sceneStart = readTimer();
}





/* =========================================================
   RENDER — appelée à chaque image (cadence FRAME_TICKS)
   ---------------------------------------------------------
   phase    : 0 = intro, 1 = corps, 2 = outro   (phases du gabarit)
   progress : avancement DANS cette phase, de 0 à 1000
   elapsed  : ticks écoulés depuis le début de la scène
   ========================================================= */
static void scene8Render(unsigned long elapsed, int phase, unsigned long progress)
{
    int  musicPhase;
    int  phaseChanged;
    char line[40];
    char elapsedStr[16];
    char totalStr[16];

    (void)phase; (void)progress;            /* phases audio gérées ci-dessous */

    /* Compteur cumulé de bouclages : hasMusicLooped() consomme son
       drapeau à chaque appel (voir audio.h), donc on l'appelle une
       fois par image et on accumule nous-mêmes plutôt que de risquer
       d'en rater un entre deux images. */
    loopCount += hasMusicLooped();

    if      (elapsed < T_FADE_IN_END_TICKS)  musicPhase = PHASE_FADE_IN;
    else if (elapsed < T_PLAY_END_TICKS)     musicPhase = PHASE_PLAYING;
    else if (elapsed < T_FADE_OUT_END_TICKS) musicPhase = PHASE_FADE_OUT;
    else                                     musicPhase = PHASE_STOPPED;

    /* Actions ponctuelles au changement de phase — appelées UNE SEULE
       fois chacune (comparaison à lastPhase), jamais à chaque image :
       rappeler fadeMusicOut()/fadeMusicIn() en boucle referait
       repartir le fondu depuis son niveau courant à chaque image,
       ce qui ne descendrait/monterait jamais réellement (voir
       s3mFadeTo, qui redémarre toujours depuis fadeLevel actuel). */
    phaseChanged = (musicPhase != lastPhase);
    if (phaseChanged)
    {
        if (musicPhase == PHASE_FADE_OUT)
        {
            fadeMusicOut(MUSIC_FADE_MS);
        }
        else if (musicPhase == PHASE_STOPPED)
        {
            stopMusic();   /* coupe le son ET libère les ~290 Ko
                              de musique.s3m — voir audio.c */
        }
        lastPhase = musicPhase;
    }

    /* -------------------------------------------------------
       Affichage — redessiné au changement de phase, ou au plus
       toutes les DRAW_TICKS sinon (largement suffisant pour un
       compteur lisible par un humain). Redessiner à CHAQUE image
       (clearScreen + 8 lignes de texte + flip, potentiellement
       70 fois/seconde) monopolisait assez de CPU pour empêcher
       audioUpdate() de suivre le rythme réel du DMA : l'horloge
       interne du moteur audio prenait alors plusieurs secondes de
       retard sur l'horloge murale (bouclage constaté à l'usage),
       ce qui ressemblait à des saccades/silences pendant les
       fondus alors que le calcul de volume lui-même était correct.
       ------------------------------------------------------- */
    if (phaseChanged || elapsed >= nextDrawAt)
    {
        clearScreen(0);

        font1DrawTextCentered( 48, "TEST AUDIO S3M", 15, &FONT1_BIOS);
        font1DrawTextCentered( 60, "playMusic + fadeIn/fadeOut + stop", 8, &FONT1_BIOS);

        font1DrawTextCentered( 84, "Fichier : musique.s3m", 7, &FONT1_BIOS);
        font1DrawTextCentered( 96, statusText(loadStatus),
                               (loadStatus == AUD_OK) ? 15 : 4, &FONT1_BIOS);

        font1DrawTextCentered(120, phaseText(musicPhase), 14, &FONT1_BIOS);

        formatSeconds(elapsedStr, TICKS_TO_MS(elapsed));
        formatSeconds(totalStr,   T_SCENE_END);
        sprintf(line, "t = %s / %s", elapsedStr, totalStr);
        font1DrawTextCentered(140, line, 7, &FONT1_BIOS);

        sprintf(line, "isMusicPlaying() = %s", isMusicPlaying() ? "OUI" : "NON");
        font1DrawTextCentered(156, line, 7, &FONT1_BIOS);

        sprintf(line, "bouclages detectes = %d", loopCount);
        font1DrawTextCentered(172, line, 7, &FONT1_BIOS);

        flip();

        nextDrawAt = elapsed + DRAW_TICKS;
    }
}





/* =========================================================
   CLEANUP — appelée UNE fois à la fin de la scène
   ========================================================= */
static void scene8Cleanup(void)
{
    /* stopMusic() a déjà été appelé au passage en phase ARRET. */
}





/* =========================================================
   POINT D'ENTRÉE — gestion du timer (NE PAS MODIFIER)
   ========================================================= */
void scene8(void)
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
        scene8Init();
        return;                     /* le 1er rendu se fera au tour suivant */
    }

    elapsed = elapsedTime(sceneStart, now);

    /* 2. Fin de scène : test AVANT le rendu */
    if (elapsed >= sceneTicks)
    {
        scene8Cleanup();
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
    scene8Render(elapsed, phase, progress);
}
