/* =========================================================
   SCENE8.C — Scène de test : cycle de vie complet du moteur
              audio S3M (playMusic / fadeMusicIn / fadeMusicOut
              / stopMusic), avec musique.s3m jouée DEUX FOIS
   =========================================================
   Enchaîne les phases du cycle de vie audio sur musique.s3m
   (Gv=64, Mv=48 : un morceau qui n'est PAS censé sortir à 100 %
   du numérique, bon test pour vérifier que Gv/Mv sont bien
   respectés, voir la section VOLUME de audio.h) :

     0 .. 6 s          : FADE IN   — fadeMusicIn(6000)
     6 s .. fin-6 s    : LECTURE   — plein volume "normal" du
                                     morceau, pendant MUSIC_PASSES
                                     lectures COMPLÈTES (2)
     dernières 6 s     : FADE OUT  — fadeMusicOut(6000), calé pour
                                     se terminer avec la dernière
                                     lecture
     puis 4 s          : ARRET     — stopMusic(), mémoire libérée

   DURÉE : elle dépend du morceau, elle n'est donc PAS une constante.
   Le moteur signale chaque bouclage (hasMusicLooped) : le premier
   bouclage donne la durée d'UNE lecture (mesurée en ticks depuis le
   démarrage), d'où l'on déduit l'instant où lancer le fondu de
   sortie pour qu'il s'achève à la fin de la dernière lecture. Pour
   ce morceau (~2 min 49 s par lecture), la scène dure donc environ
   5 min 40 s. SCENE_MS n'est qu'une borne de sécurité.

   L'écran affiche en direct la phase courante, le numéro de la
   lecture en cours, le statut isMusicPlaying() et le nombre de
   bouclages détectés (compteur cumulé : le drapeau de
   hasMusicLooped() est consommé à chaque appel, voir audio.h).

   Si le chargement échoue (fichier absent, pas de carte son...) ou
   si la musique ne joue pas, la scène saute directement à la phase
   ARRET et se termine après 4 s au lieu d'attendre des bouclages
   qui ne viendront pas.

   AFFICHAGE — police FONT1_BIOS (8x8), écran 320 px : toutes
   les chaînes affichées sont volontairement tenues sous ~36
   caractères pour ne jamais déborder à l'écran.

   NOTE C89 (Open Watcom)
   ----------------------
   Toutes les déclarations en tête de bloc.

   TIMER — scène basée sur scene_template.c (voir ce fichier pour
   les règles) : tout le minutage est en ticks (70 Hz). Deux écarts
   au gabarit, car la durée dépend de la musique :
     - la fin de scène est aussi déclenchée par scene8Over (une
       ligne ajoutée dans le point d'entrée) ;
     - scene8Init() repose sceneStart après le chargement de
       musique.s3m, pour que le temps de chargement ne soit pas
       décompté et que la durée d'une lecture parte du démarrage
       réel de la musique.
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
#define MUSIC_PASSES    2         /* nombre de lectures COMPLÈTES du morceau */
#define MUSIC_FADE_MS   6000UL    /* durée du fondu entrant ET du fondu sortant */
#define STOPPED_MS      4000UL    /* palier "arrêté" après stopMusic()         */

/* Borne de sécurité (10 min) : la vraie fin de scène vient de la musique
   (voir scene8Render). Elle ne joue que si le bouclage n'était jamais
   détecté ; Cleanup coupe alors la musique. */
#define SCENE_MS     600000UL
#define FADE_IN_MS      0UL        /* pas d'intro/outro visuelle : les phases   */
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

#define FADE_TICKS      MS_TO_TICKS(MUSIC_FADE_MS)
#define STOPPED_TICKS   MS_TO_TICKS(STOPPED_MS)

/* ticks -> ms, pour l'affichage uniquement */
#define TICKS_TO_MS(t)  ((t) * 1000UL / TARGET_HZ)

/* Phases, dans l'ordre chronologique (on ne revient jamais en arrière). */
#define PHASE_FADE_IN   0
#define PHASE_PLAYING   1
#define PHASE_FADE_OUT  2
#define PHASE_STOPPED   3





/* ---------------------------------------------------------
   Variables propres à la scène
   --------------------------------------------------------- */
static int           lastPhase    = -1;          /* dernière phase audio traitée */
static int           musicPhase   = PHASE_FADE_IN;
static int           loopCount    = 0;           /* bouclages détectés (cumul)   */
static int           loadStatus   = AUD_OK;
static unsigned long nextDrawAt   = 0UL;         /* prochain affichage (ticks depuis sceneStart) */

static unsigned long songTicks    = 0UL;         /* durée d'UNE lecture, mesurée au 1er bouclage */
static int           fadeOutKnown = 0;           /* 1 dès que fadeOutAt est calculé */
static unsigned long fadeOutAt    = 0UL;         /* instant du fondu de sortie (ticks) */
static unsigned long fadeOutStart = 0UL;         /* instant où il a réellement démarré */
static unsigned long stoppedAt    = 0UL;         /* instant du stopMusic()            */
static int           scene8Over   = 0;           /* 1 : la scène peut se terminer     */





static const char *statusText(int r)
{
    switch (r)
    {
    case AUD_OK:         return "OK";
    case AUD_ERR_NOCARD: return "ERREUR: pas de carte son (BLASTER)";
    case AUD_ERR_DSPVER: return "ERREUR: Sound Blaster 16 requise";
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
    lastPhase    = -1;
    musicPhase   = PHASE_FADE_IN;
    loopCount    = 0;
    nextDrawAt   = 0UL;
    songTicks    = 0UL;
    fadeOutKnown = 0;
    fadeOutAt    = 0UL;
    fadeOutStart = 0UL;
    stoppedAt    = 0UL;
    scene8Over   = 0;

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

    /* Un éventuel drapeau de bouclage périmé (autre passage dans la
       playlist) ne doit pas être pris pour le premier bouclage de
       CETTE lecture : on le consomme ici. */
    (void)hasMusicLooped();

    /* La musique vient de démarrer à plein volume "normal" (Gv/Mv
       du fichier : s3mLoad(), appelée par playMusic() dans audio.c,
       remet le fondu à 127/127 au chargement) : pour vraiment
       démarrer de RIEN, on la coupe instantanément (durée 0 =
       application immédiate, voir s3mFadeTo) avant de lancer la
       vraie montée progressive juste après. Sans effet si le
       chargement a échoué (fadeMusicOut/fadeMusicIn ne plantent
       jamais, voir audio.c). */
    fadeMusicOut(0);
    fadeMusicIn(MUSIC_FADE_MS);

    /* Exception au gabarit : le chrono repart ICI, après le chargement
       de musique.s3m (plusieurs centaines de Ko), pour que ce temps de
       chargement ne soit pas décompté de la durée de la scène — et
       surtout pour que la durée d'une lecture, mesurée au premier
       bouclage, parte bien du démarrage de la musique. */
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
    int  phaseChanged;
    int  pass;
    char line[64];                           /* texte affiché : < 36 caractères */
    char elapsedStr[16];
    char songStr[16];

    (void)phase; (void)progress;            /* phases audio gérées ci-dessous */

    /* Compteur cumulé de bouclages : hasMusicLooped() consomme son
       drapeau à chaque appel (voir audio.h), donc on l'appelle une
       fois par image et on accumule nous-mêmes plutôt que de risquer
       d'en rater un entre deux images. */
    loopCount += hasMusicLooped();

    /* Premier bouclage = fin de la première lecture : le temps écoulé
       depuis le démarrage est la durée d'UNE lecture. La dernière
       lecture se terminera donc à MUSIC_PASSES * songTicks ; on lance
       le fondu de sortie FADE_TICKS avant, pour qu'il s'achève avec
       elle (le drapeau de bouclage précède de peu la fin audible, de
       l'ordre d'une ligne de motif + la latence du tampon audio). */
    if (loopCount >= 1 && songTicks == 0UL)
    {
        songTicks = elapsed;
        if ((unsigned long)MUSIC_PASSES * songTicks > FADE_TICKS)
            fadeOutAt = (unsigned long)MUSIC_PASSES * songTicks - FADE_TICKS;
        else
            fadeOutAt = 0UL;
        fadeOutKnown = 1;
    }

    /* Machine à états : on n'avance que dans un sens. */
    if (musicPhase < PHASE_STOPPED && (loadStatus != AUD_OK || !isMusicPlaying()))
    {
        musicPhase = PHASE_STOPPED;         /* échec ou pas de son : on abrège */
    }
    else if (musicPhase < PHASE_FADE_OUT)
    {
        if (musicPhase == PHASE_FADE_IN && elapsed >= FADE_TICKS)
            musicPhase = PHASE_PLAYING;

        /* Fondu de sortie : à l'instant calculé, ou, au pire, dès que le
           nombre de lectures voulu est atteint. */
        if ((fadeOutKnown && elapsed >= fadeOutAt) || loopCount >= MUSIC_PASSES)
            musicPhase = PHASE_FADE_OUT;
    }
    else if (musicPhase == PHASE_FADE_OUT)
    {
        if (elapsed >= fadeOutStart + FADE_TICKS)
            musicPhase = PHASE_STOPPED;
    }

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
            fadeOutStart = elapsed;
            fadeMusicOut(MUSIC_FADE_MS);
        }
        else if (musicPhase == PHASE_STOPPED)
        {
            stoppedAt = elapsed;
            stopMusic();   /* coupe le son ET libère la mémoire de
                              musique.s3m — voir audio.c */
        }
        lastPhase = musicPhase;
    }

    /* Fin de scène : STOPPED_MS après l'arrêt. Le point d'entrée teste
       scene8Over au prochain appel. */
    if (musicPhase == PHASE_STOPPED && elapsed >= stoppedAt + STOPPED_TICKS)
        scene8Over = 1;

    /* -------------------------------------------------------
       Affichage — redessiné au changement de phase, ou au plus
       toutes les DRAW_TICKS sinon (largement suffisant pour un
       compteur lisible par un humain). Redessiner à CHAQUE image
       (clearScreen + lignes de texte + flip, potentiellement
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

        font1DrawTextCentered( 40, "TEST AUDIO S3M", 15, &FONT1_BIOS);
        font1DrawTextCentered( 52, "playMusic + fadeIn/fadeOut + stop", 8, &FONT1_BIOS);

        font1DrawTextCentered( 72, "Fichier : musique.s3m", 7, &FONT1_BIOS);
        font1DrawTextCentered( 84, statusText(loadStatus),
                               (loadStatus == AUD_OK) ? 15 : 4, &FONT1_BIOS);

        font1DrawTextCentered(108, phaseText(musicPhase), 14, &FONT1_BIOS);

        pass = loopCount + 1;
        if (pass > MUSIC_PASSES) pass = MUSIC_PASSES;
        sprintf(line, "lecture %d / %d", pass, MUSIC_PASSES);
        font1DrawTextCentered(124, line, 15, &FONT1_BIOS);

        formatSeconds(elapsedStr, TICKS_TO_MS(elapsed));
        if (songTicks != 0UL)
        {
            formatSeconds(songStr, TICKS_TO_MS(songTicks));
            sprintf(line, "t = %s  (1 lecture = %s)", elapsedStr, songStr);
        }
        else
        {
            sprintf(line, "t = %s", elapsedStr);
        }
        font1DrawTextCentered(144, line, 7, &FONT1_BIOS);

        sprintf(line, "isMusicPlaying() = %s", isMusicPlaying() ? "OUI" : "NON");
        font1DrawTextCentered(160, line, 7, &FONT1_BIOS);

        sprintf(line, "bouclages detectes = %d", loopCount);
        font1DrawTextCentered(176, line, 7, &FONT1_BIOS);

        flip();

        nextDrawAt = elapsed + DRAW_TICKS;
    }
}





/* =========================================================
   CLEANUP — appelée UNE fois à la fin de la scène
   ========================================================= */
static void scene8Cleanup(void)
{
    /* Normalement stopMusic() a déjà été appelé au passage en phase
       ARRET ; sinon (borne de sécurité SCENE_MS atteinte) on coupe ici. */
    if (musicPhase != PHASE_STOPPED)
        stopMusic();
}





/* =========================================================
   POINT D'ENTRÉE — gestion du timer (gabarit : une seule ligne
   modifiée, le test de fin, qui tient aussi compte de scene8Over)
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

    /* 2. Fin de scène : test AVANT le rendu (borne de sécurité, ou fin
          anticipée décidée par la scène elle-même : scene8Over) */
    if (elapsed >= sceneTicks || scene8Over)
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
