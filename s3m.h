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
         Axx vitesse (A00 ignoré), Txx tempo (T < 33 ignoré),
         Bxx saut de position,
         Cxx rupture de motif (paramètre décodé en BCD,
             conformément au format S3M réel),
         Dxy glissement de volume : D0y/Dx0 aux ticks non nuls,
             DxF/DFy fins (tick 0 seul), D0F/DF0 de 15 à tous les
             ticks ; Dxy avec x et y non nuls descend de y (ST3) ;
             glissements "ST3.00" (indicateur 0x40 ou version 0x1300 :
             les glissements normaux agissent aussi au tick 0),
         Vxx volume global, colonne de volume,
         Exx/Fxx portamento par pas — descend/monte, xx*4 unités
             de période par tick ; EFx/FFx = FIN (x*4) et EEx/FEx =
             EXTRA-FIN (x*1), appliqués une seule fois au tick 0,
         Gxx tone portamento — glissando vers une note cible
             sans retrigger du sample,
         Hxy vibrato — sinusoïdal, phase remise à zéro à
             chaque nouvelle attaque de note,
         Jxy arpège — cycle base/note+x/note+y sur 3 ticks,
         Oxx offset — démarre la lecture à l'échantillon
             xx*256 au lieu de 0 (uniquement combiné à une
             note qui déclenche réellement le sample).
         Ixy tremor — x+1 ticks de son puis y+1 ticks de silence
             (deux compteurs persistants, comme ST3 3.21),
         Kxy vibrato + glissement de volume (le paramètre est celui
             du glissement de volume, le vibrato continue avec les
             derniers réglages de Hxy/Uxy),
         Lxy tone portamento + glissement de volume (idem, avec la
             dernière vitesse de Gxx),
         Qxy retrigger — toutes les y ticks, avec variation de
             volume x (0-F, voir retriggerChannel dans s3m.c),
         Rxy tremolo — oscillation du volume (x vitesse, y profondeur,
             crête = 2*y), comme le vibrato : démarre au tick 1,
         Uxy vibrato fin — comme Hxy avec une profondeur divisée par 4,
         Sxy effets spéciaux :
             S1x glissando (tone portamento par demi-tons),
             S2x finetune (c2spd de la voie, table de Scream Tracker 3),
             S3x / S4x forme d'onde du vibrato / tremolo (0 sinus,
                 1 rampe, 2 carré, 3 aléatoire ; +4 = phase non remise
                 à zéro à chaque note),
             SBx boucle de motif (SB0 = début, SBx = x répétitions),
             SCx coupure de note au tick x (SC0 ignoré),
             SDx note retardée au tick x,
             SEx retard de ligne (la ligne est rejouée x fois).
     - Mémoire d'effet de ST3 : D, E, F, I, J, K, L, Q, R et S partagent
       UNE SEULE mémoire par voie (paramètre 00 = dernier paramètre non nul
       apparu sur la voie, quel que soit l'effet : D00 reprend donc le
       paramètre d'un E04 précédent) ; G, H/U et O ont chacun la leur.
     - Une note SANS numéro d'instrument garde le volume courant de la
       voie ; un numéro d'instrument SANS note remet le volume par défaut.
     - Un vibrato ou un tremolo n'est ni calculé ni avancé au tick 0.
     - La cible d'un Gxx sans note est la dernière note jouée sur la voie.
     - Vitesse initiale 0 ou 255 et tempo initial < 33 : ignorés (6 / 125).
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

   LIMITES : jusqu'à 256 ordres, 99 instruments, 100 motifs (maximum du
   format) et S3M_MAX_CHANNELS voies mixées. Un échantillon peut avoir une
   longueur quelconque : sa position de lecture est un index entier plus
   une fraction de 16 bits (l'ancienne position 16.16 tenait en un seul mot
   de 32 bits et ne pouvait pas dépasser 65535 échantillons).

   DURÉE DU TICK : 2,5 / tempo secondes, en nombre ENTIER d'échantillons
   (tronqué), comme Scream Tracker 3 et libopenmpt. Définir S3M_EXACT_TICKS
   à 1 avant de compiler s3m.c pour reporter la fraction d'échantillon
   (durée exacte en secondes, mais morceau 0,1 à 0,2 % plus long que dans
   ST3 : sur la musique de démonstration, le rendu s'éloigne alors de celui
   de libopenmpt).

   ANTI-CLIC : un saut brutal du signal d'une voie (note lancée, coupée
   ou relancée, fin d'échantillon, gros changement de volume) est lissé par
   un fondu enchaîné de ~3 ms (S3M_XF_LEN échantillons, voir s3m.c). Les
   petits pas de volume (glissements, fondus) ne sont pas modifiés.

   ÉCHELLE DES PÉRIODES ET FIDÉLITÉ DES EFFETS DE HAUTEUR : les périodes
   sont celles de Scream Tracker 3 (do-4 à 8363 Hz = 1712) et dépendent
   du c2spd de l'échantillon, de sorte que les paramètres de Exx/Fxx/Gxx
   (xx*4 unités par tick), des variantes fines, du vibrato et du glissando
   sont interprétés comme dans ST3. Le vibrato/tremolo utilise une table
   sinus de 64 pas (un cycle dure 64/x ticks) ; la profondeur du vibrato
   (sinus * y / 16 unités de période, soit environ +/-7 % pour y = 15,
   Uxy = un quart) est calée sur ProTracker/ST3 mais n'est pas une
   reproduction bit-exacte : largement suffisant pour une démo, à garder
   en tête si vous comparez à un autre lecteur S3M sur le même fichier.
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
