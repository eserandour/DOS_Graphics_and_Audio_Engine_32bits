#ifndef AUDIO_H
#define AUDIO_H

/* =========================================================
   AUDIO.H — Moteur audio (musique S3M + effets WAV)
   =========================================================
   Environnement : Open Watcom 1.9, DOS
   Carte         : Sound Blaster 16 (DSP >= 4.00) ou compatible,
                   détectée via la variable d'environnement BLASTER.

   PRINCIPE
   --------
   Un double-buffer DMA (deux moitiés A/B) est joué en boucle
   par le DSP Sound Blaster, en mode auto-init (programmé une
   seule fois, voir sblaster.h — aucun clic entre les moitiés).
   L'IRQ Sound Blaster se contente de lever un drapeau ; le
   mixage logiciel (musique S3M + sons WAV) est fait par
   audioUpdate(), appelée depuis la boucle principale de main.c
   (voir "while (!quitRequested) { ...; audioUpdate(); }").

   Le mixage a délibérément été laissé HORS de l'ISR : du code
   qui s'exécute dans une interruption bloque TOUTES les autres
   interruptions pendant sa durée (y compris le timer 70 Hz qui
   cadence l'animation), donc y placer un travail aussi long que
   du mixage audio revient à geler périodiquement tout le
   programme — bien pire que le problème que ça prétendait
   résoudre. Deux leviers purement côté audio compensent à la
   place, sans toucher aux scènes :
     - MIX_BUFFER_SAMPLES large (voir audio.c) : marge de temps
       généreuse avant qu'une frame de scène lente ne fasse
       rejouer du contenu périmé.
     - MIX_RATE (voir plus bas) : détermine directement combien
       d'échantillons sont à mixer par seconde, donc le CPU total
       consommé par le mixage — à ajuster ici en premier lieu si
       la charge audio doit être revue.

   VOLUME
   ------
   Le mixage logiciel applique fidèlement le volume tel qu'écrit
   dans le fichier S3M : volume global initial (Gv) et volume
   maître (Mv) lus depuis l'en-tête, volume par note/instrument,
   glissements Dxx et commande Vxx — le morceau sort donc au
   niveau voulu par son compositeur dans Scream Tracker, pas
   systématiquement à 100 % du numérique (voir s3m.c). Le mixer
   matériel de la carte (s'il existe) reste réglé au maximum à
   l'initialisation : c'est un gain analogique indépendant du
   contenu du fichier — aucun champ du format S3M ne décrit le
   niveau de sortie ligne d'une carte son, seul le signal
   numérique envoyé est concerné par Gv/Mv/Vxx ci-dessus.

   ROBUSTESSE
   ----------
   Si aucune carte compatible n'est détectée (BLASTER absent,
   DSP ne répondant pas, ou DSP antérieur à 4.00 — voir
   AUD_ERR_DSPVER), audioInit() échoue proprement :
   playMusic()/playSound() deviennent alors des no-op sans
   jamais planter, et le reste de la démo (vidéo, timer,
   clavier) continue de fonctionner normalement.

   SORTIE PROPRE (Échap)
   ----------------------
   audioShutdown() coupe le DMA, restaure le vecteur IRQ
   d’origine et libère toute la mémoire allouée. Elle
   est appelée automatiquement par shutdown() dans main.c,
   qui est lui-même exécuté dès que quitRequested passe à 1
   (touche Échap, voir keyboard.c) — aucune scène n'a besoin
   de s'en préoccuper.
   ========================================================= */

#define AUD_OK          0
#define AUD_ERR_NOCARD  1   /* pas de carte son détectée/répondant */
#define AUD_ERR_MEM     2   /* mémoire insuffisante                */
#define AUD_ERR_FILE    3   /* fichier introuvable/inaccessible    */
#define AUD_ERR_FORMAT  4   /* fichier présent mais invalide/tronqué */
#define AUD_ERR_DSPVER  5   /* carte trop ancienne : DSP < 4.00, pas
                               de sortie à 22050 Hz exacts         */

/* Fréquence de sortie et de mixage : 22050 Hz exacts, réglés par
   la commande DSP 0x41 (SB16, DSP >= 4.00). Le moteur ne joue
   jamais à une autre fréquence : sur une carte plus ancienne,
   audioInit() échoue avec AUD_ERR_DSPVER. */
#define MIX_RATE  22050UL

/* ---------------------------------------------------------
   Cycle de vie
   --------------------------------------------------------- */

/* Détecte la carte, initialise le DSP et le DMA, démarre la
   lecture silencieuse. À appeler une seule fois au démarrage,
   après installKeyboard() par exemple. Ne plante jamais : en
   cas d'échec, retourne un code d'erreur et le moteur reste
   inactif (silence permanent, sans effet de bord). */
int audioInit(void);

/* À appeler très régulièrement (voir "PRINCIPE" ci-dessus) :
   effectue le mixage logiciel de la moitié de buffer qui
   vient de se libérer, s'il y en a une. */
void audioUpdate(void);

/* Coupe le DMA, restaure l'IRQ, libère toute la mémoire.
   À appeler impérativement avant de quitter le programme
   (voir shutdown() dans main.c). Ne fait rien si audioInit()
   n'a jamais réussi. */
void audioShutdown(void);

/* ---------------------------------------------------------
   Lecture
   --------------------------------------------------------- */

/* Charge et joue en boucle un module de musique .s3m. Coupe
   la musique en cours s'il y en avait une. Sans effet si le
   moteur audio n'est pas actif (voir AUD_ERR_NOCARD).
   Retourne AUD_OK, ou un code d'erreur précis (AUD_ERR_FILE
   si le fichier est introuvable, AUD_ERR_FORMAT s'il est
   présent mais invalide/tronqué, AUD_ERR_MEM en cas de
   mémoire insuffisante) — utile pour afficher un    
   diagnostic à l'écran plutôt qu'un échec silencieux. */
int playMusic(const char *filename);

/* Arrête la musique en cours et libère intégralement la mémoire
   qu'elle occupait (échantillons + motifs) — pas un simple silence
   en façade. Sans effet si aucune musique n'est chargée, ou si le
   moteur audio n'est pas actif. Les effets WAV en cours (playSound)
   ne sont pas affectés. */
void stopMusic(void);

/* Retourne 1 si une musique est actuellement chargée et en cours de
   lecture, 0 sinon (rien chargé, lecture terminée faute de motif
   jouable, ou moteur audio inactif). */
int isMusicPlaying(void);

/* Retourne 1 si la musique en cours a rebouclé sur son point de
   départ depuis le dernier appel à cette fonction, puis remet le
   drapeau à 0 ; retourne 0 sinon (y compris si aucune musique n'est
   chargée ou si le moteur audio est inactif). Permet à l'appelant
   de synchroniser un événement sur la musique elle-même plutôt
   que sur une durée estimée à la main. */
int hasMusicLooped(void);

/* Fait monter la musique en cours de son niveau actuel (typiquement
   silence après un fadeMusicOut(), ou pleine intensité au premier
   playMusic()) jusqu'au volume normal du morceau (celui écrit dans
   le fichier — voir Mv/Gv ci-dessus), en 'durationMs' millisecondes.
   Sans effet si aucune musique n'est chargée ou si le moteur audio
   n'est pas actif. */
void fadeMusicIn(unsigned long durationMs);

/* Fait descendre la musique en cours de son niveau actuel jusqu'au
   silence total, en 'durationMs' millisecondes — la musique continue
   de jouer (et donc de consommer la mémoire du module), juste  
   inaudible une fois le fondu terminé : combiner avec stopMusic()
   ensuite si vous voulez aussi libérer la mémoire. Sans effet si
   aucune musique n'est chargée ou si le moteur audio n'est pas actif. */
void fadeMusicOut(unsigned long durationMs);

/* Joue un effet sonore .wav sur une voie libre (jusqu'à 4 en
   simultané). Sans effet si le moteur audio n'est pas actif. */
int playSound(const char *filename);

#endif /* AUDIO_H */
