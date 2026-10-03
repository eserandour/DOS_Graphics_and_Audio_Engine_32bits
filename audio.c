/* =========================================================
   AUDIO.C — Moteur audio (musique S3M + effets WAV)
   =========================================================
   Voir audio.h pour la documentation complète du principe de
   fonctionnement (double buffer DMA, pourquoi le mixage est
   hors de l'ISR, volumes, robustesse). Ce fichier implémente
   ce principe : l'ISR se contente de lever un drapeau par
   moitié de buffer libérée, et c'est audioUpdate() — appelée
   depuis la boucle principale de main.c — qui fait le vrai
   travail de mixage.
   ========================================================= */

#include <string.h>
#include "sblaster.h"
#include "s3m.h"
#include "wav.h"
#include "audio.h"

/* Taille (en octets = en échantillons 8 bits) d'UNE moitié du
   buffer DMA circulaire. Le buffer complet fait 2*MIX_BUFFER_
   SAMPLES octets (bufA + bufB, voir audioInit). Une valeur
   large donne plus de marge de temps à audioUpdate() avant
   qu'une moitié ne soit rejouée alors qu'elle contient encore
   du contenu périmé (voir audio.h, section PRINCIPE). */
#define MIX_BUFFER_SAMPLES  4096U

/* 1 dès qu'audioInit() a réussi jusqu'à audioShutdown() : toutes
   les fonctions publiques de ce fichier vérifient ce drapeau et
   deviennent des no-op silencieux si le moteur n'est pas actif
   (carte absente ou initialisation échouée — voir audio.h,
   section ROBUSTESSE). */
static int audioReady = 0;

/* bufA/bufB pointent chacun sur une moitié du buffer DMA
   circulaire alloué par sbAllocDmaBuffer() ; bufRaw et physBase
   sont les informations nécessaires à sa libération et à sa
   reprogrammation matérielle (voir sblaster.h). Le DSP lit ces
   deux moitiés en boucle sans intervention du CPU (mode
   auto-init, voir sbStartOutputLoop). */
static unsigned char *bufA = NULL;
static unsigned char *bufB = NULL;
static unsigned char *bufRaw = NULL;
static unsigned long physBase = 0;

/* needFillA/needFillB : levés par l'ISR quand le DSP vient de
   TERMINER de jouer la moitié correspondante — c'est donc le
   moment où le CPU peut la remplir de contenu frais sans risquer
   d'écraser des échantillons encore en cours de lecture.
   currentHalf suit quelle moitié le DSP est en train de jouer
   (0 = A, 1 = B), pour savoir laquelle vient de se libérer au
   prochain IRQ. Les trois sont volatile : modifiées dans l'ISR,
   lues dans audioUpdate() qui tourne hors interruption. */
static volatile int needFillA = 0;
static volatile int needFillB = 0;
static volatile int currentHalf = 0;

/* ISR installée sur l'IRQ Sound Blaster (voir sbInstallIRQ dans
   audioInit). Volontairement minimale : elle ne fait AUCUN
   mixage (voir audio.h, "Le mixage a délibérément été laissé
   HORS de l'ISR"), juste acquitter l'interruption côté DSP/PIC
   (sbAckIRQ) et basculer le drapeau de la moitié qui vient de
   se libérer. Le vrai travail est fait plus tard par
   audioUpdate(), en dehors de tout contexte d'interruption. */
static void interrupt audioISR(void)
{
    sbAckIRQ();

    if (currentHalf == 0)
    {
        /* Le DSP jouait A, il vient de basculer sur B :
           A est donc libre à remplir, B est en cours de lecture. */
        needFillA = 1;
        currentHalf = 1;
    }
    else
    {
        /* Symétrique : B vient de se libérer, le DSP joue A. */
        needFillB = 1;
        currentHalf = 0;
    }
}

/* Remplit 'buf' (n octets) avec le mixage de la musique S3M puis
   des effets WAV. s3mMix() écrit d'abord n octets de musique (ou
   de silence si rien n'est chargé — voir s3m.h), puis wavMix()
   ajoute les voies d'effets par-dessus sans écraser ce qui est
   déjà là (voir wav.h). L'ordre est important : wavMix() suppose
   que buf contient déjà la musique. */
static void mixBuffer(unsigned char *buf, unsigned int n)
{
    s3mMix(buf, n);
    wavMix(buf, n);
}

/* ---------------------------------------------------------
   Cycle de vie
   --------------------------------------------------------- */

int audioInit(void)
{
    unsigned char tc;

    audioReady = 0;
    needFillA = 0;
    needFillB = 0;

    /* Détection (variable BLASTER) puis reset matériel du DSP.
       Si l'une des deux étapes échoue, aucune carte compatible
       n'est disponible : le moteur reste inactif, sans planter
       (voir audio.h, section ROBUSTESSE). */
    if (sbDetect() != SB_OK) return AUD_ERR_NOCARD;
    if (sbReset()  != SB_OK) return AUD_ERR_NOCARD;

    /* Mixer matériel au maximum (gain analogique) : c'est Gv/Mv/
       Vxx côté S3M qui décideront ensuite du niveau réel du
       signal numérique envoyé (voir audio.h, section VOLUME). */
    sbSetMixerVolumeMax();

    /* Un seul appel à sbAllocDmaBuffer() pour les DEUX moitiés :
       on demande 2*MIX_BUFFER_SAMPLES octets d'un coup pour
       garantir que bufA et bufB soient contigus en mémoire ET en
       adresse physique, condition nécessaire au mode auto-init
       du DMA (voir sbStartOutputLoop dans sblaster.h). bufB
       pointe simplement après bufA dans ce même bloc. */
    bufA = sbAllocDmaBuffer(MIX_BUFFER_SAMPLES * 2U, &physBase, &bufRaw);
    if (!bufA)
    {
        bufA = bufB = NULL;
        bufRaw = NULL;
        return AUD_ERR_MEM;
    }
    bufB = bufA + MIX_BUFFER_SAMPLES;

    /* Pré-remplissage en silence (128 = zéro au format 8 bits non
       signé, voir s3m.h/wav.h) : évite un bruit parasite au tout
       premier IRQ, avant qu'audioUpdate() n'ait eu l'occasion de
       mixer du contenu réel. */
    memset(bufA, 128, MIX_BUFFER_SAMPLES);
    memset(bufB, 128, MIX_BUFFER_SAMPLES);

    /* Les moteurs S3M et WAV ont besoin de connaître la fréquence
       de mixage pour convertir leurs pas de lecture (note -> step,
       rééchantillonnage) — voir s3mInit/wavInit. */
    s3mInit(MIX_RATE);
    wavInit(MIX_RATE);

    /* Constante de temps DSP : formule standard Sound Blaster,
       tc = 256 - 1000000/fréquence(Hz). La commande 0x40 est
       comprise par toutes les versions de DSP (voir sblaster.c) ;
       la sortie auto-init lancée plus bas exige en revanche un
       DSP >= 2.00. Le diviseur étant entier, tc = 211 donne en
       réalité 1000000/45 = 22222 Hz pour MIX_RATE = 22050. */
    tc = (unsigned char)(256U - (unsigned int)(1000000UL / MIX_RATE));
    sbSetTimeConstant(tc);

    /* Branche notre ISR sur l'IRQ détectée et la démasque au PIC. */
    sbInstallIRQ(audioISR);

    /* currentHalf=0 : on considère que le DSP va commencer par
       jouer A (voir sbStartOutputLoop juste après, qui programme
       le DMA sur le buffer bufA/bufB en entier). Programmé UNE
       SEULE FOIS en mode auto-init : le DSP bouclera ensuite tout
       seul sur les deux moitiés sans réarmement du CPU (voir
       sblaster.h pour le détail matériel). */
    currentHalf = 0;
    sbStartOutputLoop(physBase, MIX_BUFFER_SAMPLES);

    audioReady = 1;
    return AUD_OK;
}

/* À appeler très régulièrement depuis la boucle principale (voir
   audio.h, section PRINCIPE). Ne fait qu'un travail borné : au
   plus UN mixage de MIX_BUFFER_SAMPLES octets par moitié et par
   appel, donc deux au maximum si needFillA et needFillB sont
   tous les deux levés (cas rare d'une frame très lente ayant
   laissé passer deux IRQ). Les drapeaux sont redescendus juste
   après le mixage correspondant. */
void audioUpdate(void)
{
    if (!audioReady) return;

    if (needFillA) { mixBuffer(bufA, MIX_BUFFER_SAMPLES); needFillA = 0; }
    if (needFillB) { mixBuffer(bufB, MIX_BUFFER_SAMPLES); needFillB = 0; }
}

/* Arrêt propre et complet du moteur audio, dans l'ordre qui
   évite tout état incohérent :
     1. Couper le DMA (sbStopOutput) : plus aucun son ne sort.
     2. Restaurer le vecteur IRQ d'origine (sbRestoreIRQ) : notre
        ISR ne sera plus jamais appelée, avant même de libérer
        les buffers qu'elle référence.
     3. Libérer la musique et les effets en cours.
     4. Libérer le buffer DMA lui-même.
   Sans effet si audioInit() n'a jamais réussi (audioReady == 0),
   pour rester sûr même appelée deux fois ou sans init préalable. */
void audioShutdown(void)
{
    if (!audioReady) return;

    sbStopOutput();
    sbRestoreIRQ();

    s3mUnload();
    wavStopAll();

    if (bufRaw) sbFreeDmaBuffer();
    bufA = bufB = NULL;
    bufRaw = NULL;

    audioReady = 0;
}

/* Charge 'filename' et démarre sa lecture (s3mLoad coupe d'abord
   toute musique déjà chargée, voir s3m.h). Traduit les codes de
   retour internes de s3m.c vers les codes publics AUD_ERR_* pour
   ne pas exposer les détails du lecteur S3M à l'appelant. */
int playMusic(const char *filename)
{
    int r;

    if (!audioReady) return AUD_ERR_NOCARD;

    r = s3mLoad(filename);
    switch (r)
    {
    case S3M_OK:         return AUD_OK;
    case S3M_ERR_FILE:   return AUD_ERR_FILE;
    case S3M_ERR_READ:   return AUD_ERR_FORMAT;
    case S3M_ERR_FORMAT: return AUD_ERR_FORMAT;
    default:              return AUD_ERR_MEM;   /* S3M_ERR_MEM */
    }
}

void stopMusic(void)
{
    if (!audioReady) return;
    s3mUnload();
}

int isMusicPlaying(void)
{
    if (!audioReady) return 0;
    return s3mIsPlaying();
}

int hasMusicLooped(void)
{
    if (!audioReady) return 0;
    return s3mConsumeLoopFlag();
}

/* 100 = volume cible en pourcentage du volume normal du morceau
   (100% = tel qu'écrit dans le fichier, voir Gv/Mv dans s3m.h) :
   un fadeMusicIn() ramène donc TOUJOURS vers le volume normal,
   jamais au-delà. */
void fadeMusicIn(unsigned long durationMs)
{
    if (!audioReady) return;
    s3mFadeTo(100, durationMs);
}

/* 0 = silence total en cible : la musique continue de jouer en
   mémoire, juste inaudible une fois le fondu terminé (voir
   audio.h pour la différence avec stopMusic()). */
void fadeMusicOut(unsigned long durationMs)
{
    if (!audioReady) return;
    s3mFadeTo(0, durationMs);
}

/* Délègue directement à wavPlay() (chargement + assignation à
   une voie libre, voir wav.h) ; seul le code de retour est
   traduit vers les codes publics AUD_ERR_*. */
int playSound(const char *filename)
{
    if (!audioReady) return AUD_ERR_NOCARD;
    return (wavPlay(filename) == WAV_OK) ? AUD_OK : AUD_ERR_MEM;
}
