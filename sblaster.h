#ifndef SBLASTER_H
#define SBLASTER_H

/* =========================================================
   SBLASTER.H — Pilote bas niveau Sound Blaster (DSP + DMA)
   =========================================================
   Environnement : Open Watcom 1.9, DOS

   Ce module ne connait ni le S3M ni le WAV : il sait
   uniquement dialoguer avec le DSP Sound Blaster (reset,
   version, fréquence de sortie, volume mixer) et avec le
   contrôleur DMA 8237 (canal 8 bits, mode auto-init en
   boucle, programmé une seule fois, sans réarmement à
   chaque IRQ — voir sbStartOutputLoop() plus bas et audio.c).

   DÉTECTION
   ---------
   La détection se fait via la variable d'environnement
   BLASTER, positionnée par le pilote SB (SET BLASTER=A220
   I5 D1 H5 P330 T6). Si elle est absente ou mal formée,
   sbDetect() échoue et TOUT le reste du moteur audio doit
   rester silencieux sans jamais planter (voir audio.c).

   CARTE REQUISE : DSP >= 4.00 (Sound Blaster 16 ou compatible,
   DOSBox / DOSBox-X par défaut). Seul un DSP 4.xx sait régler la
   fréquence de sortie en Hz exacts (commande 0x41), donc jouer
   à 22050 Hz ; les DSP plus anciens ne savent produire que des
   fréquences 1000000/n (22222 Hz au plus près), que ce moteur
   refuse. sbIsDsp4() permet de tester la version après sbReset().

   BUFFER DMA — PARTICULARITÉ DU MODÈLE FLAT 32 BITS
   ---------------------------------------------------------
   Le contrôleur DMA 8237 ne comprend que des adresses PHYSIQUES
   20 bits ; il ignore totalement la pagination / le mode protégé.
   malloc() sous DOS/32A alloue en mémoire étendue gérée par
   l'extendeur : rien ne garantit qu'un bloc obtenu ainsi soit
   physiquement contigu ni situé sous la barre des 16 Mo, donc
   on NE PEUT PAS l'utiliser directement pour du DMA ISA.

   sbAllocDmaBuffer() utilise donc l'appel DPMI 0x0100 "Allocate
   DOS Memory Block" pour réserver un bloc dans les 640 Ko de
   mémoire conventionnelle bas de carte, garanti physiquement
   contigu. Sous un extendeur flat "0-based" comme DOS/32A ou
   DOS/4GW, cette zone est mappée 1:1 dans l'espace linéaire du
   programme : adresse linéaire == adresse physique. Le segment
   réel renvoyé par le DPMI (AX) donne donc directement, une fois
   multiplié par 16, un pointeur plat utilisable normalement.

   Comme en mode réel, un buffer DMA ne doit jamais franchir une
   frontière physique de 64 Ko (limite du registre de page du
   8237) : on alloue le double de la taille demandée et on choisit
   la moitié qui ne franchit pas cette frontière — même technique
   qu'avant, appliquée à l'adresse linéaire/physique désormais
   identiques. Garanti correct pour toute taille <= 32768 octets.

   Le bloc DOS alloué doit être libéré via sbFreeDmaBuffer() (DPMI
   0x0101), PAS via free() : ce n'est pas un bloc du tas C normal.
   ========================================================= */

/* ---------------------------------------------------------
   Codes de retour
   --------------------------------------------------------- */
#define SB_OK           0
#define SB_ERR_NOENV    1   /* variable BLASTER absente/illisible */
#define SB_ERR_NORESET  2   /* le DSP ne répond pas au reset      */
#define SB_DSP_MIN_MAJOR 4  /* version DSP minimale (SB16)        */

/* ---------------------------------------------------------
   Informations carte détectée (lecture seule pour audio.c)
   --------------------------------------------------------- */
extern unsigned int  sbBase;         /* port I/O de base, ex. 0x220 */
extern int           sbIrq;          /* IRQ matériel, ex. 5 ou 7    */
extern int           sbDma8;         /* canal DMA 8 bits, 0-3       */
extern unsigned char sbVersionMajor;
extern unsigned char sbVersionMinor;

/* ---------------------------------------------------------
   Détection et initialisation du DSP
   --------------------------------------------------------- */

/* Lit et parse la variable d'environnement BLASTER.
   Retourne SB_OK ou SB_ERR_NOENV. */
int sbDetect(void);

/* Reset matériel du DSP (impulsion sur le port +0x6) et
   vérification de la réponse 0xAA. Lit aussi la version.
   Retourne SB_OK ou SB_ERR_NORESET. */
int sbReset(void);

/* Retourne 1 si le DSP détecté par sbReset() est en version
   >= SB_DSP_MIN_MAJOR.00 (Sound Blaster 16), 0 sinon. */
int sbIsDsp4(void);

/* Règle la fréquence de sortie à 'hz' Hz exacts (commande DSP
   0x41, DSP >= 4.00 uniquement : vérifier sbIsDsp4() avant). */
void sbSetOutputRate(unsigned int hz);

/* Positionne le volume du mixer matériel (s'il existe, SB Pro/16)
   au maximum. Sans effet — et sans erreur — sur un DSP sans
   puce mixer (SB 1.0/2.0) : dans ce cas il n'y a de toute façon
   aucune atténuation possible, le volume est donc déjà "max". */
void sbSetMixerVolumeMax(void);

/* ---------------------------------------------------------
   Buffer DMA
   --------------------------------------------------------- */

/* Alloue un buffer de 'size' octets (size <= 32768) garanti de
   ne pas franchir une frontière physique de 64 Ko, en mémoire
   DOS conventionnelle basse (obligatoire pour le DMA ISA — voir
   note ci-dessus). *rawBlock reçoit le pointeur plat de DÉBUT du
   bloc réellement alloué (2*size octets) ; à ne PAS passer à
   free() — la libération se fait via sbFreeDmaBuffer().
   *physAddr reçoit l'adresse physique 20 bits à programmer dans
   le contrôleur DMA.
   Retourne le pointeur plat utilisable pour écrire les données,
   ou NULL si l'allocation échoue. */
unsigned char *sbAllocDmaBuffer(unsigned int size,
                                 unsigned long *physAddr,
                                 unsigned char **rawBlock);

/* Libère le bloc de mémoire DOS conventionnelle alloué par le
   dernier appel réussi à sbAllocDmaBuffer() (DPMI 0x0101).
   Sans effet si aucun buffer n'est actuellement alloué.
   À appeler à l'arrêt du moteur audio, à la place de free(). */
void sbFreeDmaBuffer(void);

/* ---------------------------------------------------------
   IRQ
   --------------------------------------------------------- */

/* Installe 'handler' sur le vecteur IRQ détecté par sbDetect(),
   sauvegarde l'ancien vecteur et démasque l'IRQ au PIC.
   À appeler après sbDetect() / sbReset(). */
void sbInstallIRQ(void interrupt (*handler)(void));

/* Restaure le vecteur IRQ original et remasque l'IRQ au PIC
   dans son état d'avant sbInstallIRQ(). À appeler impérativement
   avant de quitter le programme. */
void sbRestoreIRQ(void);

/* À appeler en tout début de l'ISR audio : acquitte l'IRQ côté
   DSP (lecture du port de statut 8 bits) puis envoie l'EOI au(x)
   PIC(s) concerné(s) (gère le cas IRQ >= 8, second PIC). */
void sbAckIRQ(void);

/* ---------------------------------------------------------
   Lecture DMA (sortie 8 bits)
   --------------------------------------------------------- */

/* Programme le contrôleur DMA UNE SEULE FOIS en mode auto-init
   sur un buffer circulaire de 2*halfLen octets (physAddr =
   adresse physique du DÉBUT de ce buffer, les deux moitiés
   devant être contiguës — voir sbAllocDmaBuffer), puis démarre
   le DSP en sortie 8 bits mono non signée, auto-init, par la
   commande SB16 0xC6, à la fréquence fixée par sbSetOutputRate().
   Nécessite un DSP >= 4.00 (voir sbIsDsp4).

   Le DSP boucle ensuite indéfiniment sur ce buffer et lève une
   IRQ tous les halfLen octets, SANS AUCUN réarmement CPU entre
   les blocs : c'est la méthode à utiliser pour une lecture
   continue sans clic (voir audio.c). À appeler une seule fois
   à l'initialisation, jamais depuis l'ISR. */
void sbStartOutputLoop(unsigned long physAddr, unsigned int halfLen);

/* Programme le contrôleur DMA sur physAddr/len puis déclenche
   la commande DSP 0x14 (sortie 8 bits simple-cycle, un seul
   bloc, pas de bouclage matériel) pour 'len' octets. Conservée
   pour compatibilité/tests ponctuels — pour la lecture continue
   du moteur audio, préférer sbStartOutputLoop() (voir plus haut :
   réarmer ce mode à chaque IRQ produit un micro-trou audible
   entre chaque bloc). */
void sbStartOutput(unsigned long physAddr, unsigned int len);

/* Arrête proprement la sortie DMA en cours (pause DSP) — utilisé
   à la fermeture pour éviter tout bruit résiduel ou DMA qui
   continue de tourner après la sortie du programme. */
void sbStopOutput(void);

#endif /* SBLASTER_H */
