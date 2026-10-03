#ifndef S3M_H
#define S3M_H

/* =========================================================
   S3M.H — Chargeur et moteur de lecture de modules S3M
   =========================================================
   Environnement : Open Watcom 1.9, DOS

   SOUS-ENSEMBLE SUPPORTÉ (volontairement limité — voir
   les notes de audio.c pour le détail complet) :
     - Échantillons PCM non compressés, 8 ou 16 bits, mono
       ou stéréo (convertis au chargement en 8 bits non
       signés mono, format natif du moteur de mixage).
     - Jusqu'à S3M_MAX_CHANNELS voies mixées simultanément
       (les voies au-delà sont ignorées si le module en
       utilise davantage — rare en pratique).
     - Volume global initial (Gv) et volume maître (Mv) lus
       depuis l'en-tête, fidèles au fichier plutôt que forcés
       à un maximum arbitraire (voir application dans s3m.c).
     - Commandes :
         Axx vitesse, Txx tempo, Bxx saut de position,
         Cxx rupture de motif (paramètre décodé en BCD,
             conformément au format S3M réel),
         Dxx glissement de volume (mémoire d'effet, glissements
             fins DxF/DFx appliqués une seule fois),
         Vxx volume global, colonne de volume,
         Exx/Fxx portamento par pas — descend/monte (mémoire
             partagée entre les deux, variantes fines ExE/EEx
             et extra-fines ExF/EFx appliquées une seule fois
             au déclenchement de la ligne),
         Gxx tone portamento — glissando vers une note cible
             sans retrigger du sample,
         Hxy vibrato — sinusoïdal, phase remise à zéro à
             chaque nouvelle attaque de note,
         Jxy arpège — cycle base/note+x/note+y sur 3 ticks,
         Oxx offset — démarre la lecture à l'échantillon
             xx*256 au lieu de 0 (uniquement combiné à une
             note qui déclenche réellement le sample).
         Ixy tremor — x ticks de son, y ticks de silence, en cycle
             (mémoire d'effet),
         Kxy vibrato + glissement de volume (le paramètre est celui
             du glissement de volume, le vibrato continue avec les
             derniers réglages de Hxy/Uxy),
         Lxy tone portamento + glissement de volume (idem, avec la
             dernière vitesse de Gxx),
         Qxy retrigger — toutes les y ticks, avec variation de
             volume x (0-F, voir retriggerChannel dans s3m.c),
         Rxy tremolo — oscillation du volume (x vitesse, y profondeur),
         Uxy vibrato fin — comme Hxy avec une profondeur divisée par 4,
         Sxy effets spéciaux :
             S1x glissando (tone portamento par demi-tons),
             S2x finetune (c2spd de la voie, table de Scream Tracker 3),
             S3x / S4x forme d'onde du vibrato / tremolo (0 sinus,
                 1 rampe, 2 carré, 3 aléatoire ; +4 = phase non remise
                 à zéro à chaque note),
             SBx boucle de motif (SB0 = début, SBx = x répétitions),
             SCx coupure de note au tick x (SC0 = immédiate),
             SDx note retardée au tick x,
             SEx retard de ligne (la ligne est rejouée x fois).
     - Mémoire d'effet (paramètre 00 = dernier paramètre non nul)
       pour D, E/F, G, H, I, Q, R, S et O.
     - Les effets "de ligne" (D, E/F, G, H, J, I, Q, R, SC...) ne
       durent que la ligne où ils sont écrits : une voie sans cellule
       sur une ligne ne poursuit pas l'effet de la ligne précédente.
     - Ignorés (sans objet pour ce moteur mono sans filtre) : S0x
       (filtre), S8x/SAx (panoramique), SFx (funk repeat), ainsi que
       les extensions non standard (W, X, Y, Z...). La note se
       déclenche quand même, seul l'effet fin est absent.
     - Bouclage automatique : à la fin de la table d'ordres,
       la lecture reprend au premier ordre valide — adapté
       à une musique de fond de démo qui tourne en boucle.
       Chaque bouclage peut être détecté depuis l'extérieur
       (voir s3mConsumeLoopFlag ci-dessous).

   FIDÉLITÉ DU VIBRATO (Hxy) : la table sinus utilisée (32 pas,
   voir vibratoSineTable dans s3m.c) et l'échelle de profondeur
   sont une approximation raisonnable, pas une reproduction
   bit-exacte de Scream Tracker 3 — largement suffisant pour un
   usage de démo, mais à garder en tête si vous comparez à la
   sortie d'un autre lecteur S3M sur le même fichier.
   ========================================================= */

/* ---------------------------------------------------------
   Codes de retour
   --------------------------------------------------------- */
#define S3M_OK          0
#define S3M_ERR_FILE    1   /* impossible d'ouvrir le fichier      */
#define S3M_ERR_READ    2   /* fichier tronqué / lecture incomplète */
#define S3M_ERR_FORMAT  3   /* signature 'SCRM' absente             */
#define S3M_ERR_MEM     4   /* mémoire insuffisante                 */

/* Nombre maximal de voies réellement mixées (le format S3M
   autorise jusqu'à 32 canaux, mais en mixer autant en logiciel
   sur cette cible est hors de portée — voir note ci-dessus). */
#define S3M_MAX_CHANNELS  16

/* ---------------------------------------------------------
   API
   --------------------------------------------------------- */

/* À appeler une seule fois avant tout usage, avec la fréquence
   de mixage du moteur audio (voir audio.h : MIX_RATE). */
void s3mInit(unsigned long mixRate);

/* Charge et démarre la lecture de 'filename'. Le module
   précédemment chargé (s'il y en a un) est libéré d'abord.
   En cas d'échec, l'état "aucune musique" est conservé
   (silence), rien ne plante. */
int s3mLoad(const char *filename);

/* Libère le module en cours et repasse en silence. */
void s3mUnload(void);

/* Génère 'n' octets de musique (0..255, silence = 128) dans
   buf. Écrit TOUJOURS n octets, y compris du silence         
   si aucun module n'est chargé — le buffer peut donc servir
   de base pour un mixage additif ultérieur (voir wavMix). */
void s3mMix(unsigned char *buf, unsigned int n);

/* Retourne 1 si un module est chargé ET en cours de lecture,
   0 sinon (rien chargé, ou fin de lecture faute de motif
   jouable — voir advanceRow). */
int s3mIsPlaying(void);

/* Retourne 1 si la table d'ordres a rebouclé sur son point de
   départ depuis le dernier appel à cette fonction, puis remet
   le drapeau à 0 (consommé) ; retourne 0 sinon. Un bouclage
   entre deux appels n'est jamais perdu, mais deux bouclages
   entre deux appels ne comptent que pour un seul 1 — pour
   compter précisément les tours, interroger plus souvent
   (typiquement une fois par tour de boucle principale). */
int s3mConsumeLoopFlag(void);

/* Lance un fondu (fade-in ou fade-out) vers 'targetPercent' (0-100,
   clampé) du volume normal, étalé sur 'durationMs' millisecondes de
   lecture réelle (indépendant du tempo/vitesse du morceau — voir
   doTick). Un fondu déjà en cours est repris depuis son niveau actuel,
   pas depuis 0/100 : appeler s3mFadeTo() en plein fondu ne produit
   aucun saut audible. durationMs=0 applique le niveau cible
   immédiatement. N'affecte QUE l'enveloppe de fondu : le volume
   "normal" (Gv/Mv/Vxx/volume par note) continue d'être respecté
   par-dessous, voir doTick(). */
void s3mFadeTo(int targetPercent, unsigned long durationMs);

#endif /* S3M_H */
