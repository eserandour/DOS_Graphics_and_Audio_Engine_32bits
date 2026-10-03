/* =========================================================
   SBLASTER.C — Pilote bas niveau Sound Blaster (DSP + DMA)
   =========================================================
   Voir sblaster.h pour la documentation complète de l'API.
   ========================================================= */

#include <stdlib.h>   /* getenv                              */
#include <string.h>   /* strlen                              */
#include <conio.h>    /* outp, inp                            */
#include <dos.h>      /* _disable, _enable, _dos_getvect,
                         _dos_setvect, union REGS              */
#include <i86.h>      /* int386 (appels DPMI)                  */
#include "sblaster.h"

/* ---------------------------------------------------------
   Registres DSP relatifs au port de base
   --------------------------------------------------------- */
#define DSP_RESET        0x6
#define DSP_READ         0xA
#define DSP_WRITE        0xC
#define DSP_READ_STATUS  0xE   /* bit7 = donnée dispo ; le lire
                                   acquitte aussi l'IRQ 8 bits */

/* ---------------------------------------------------------
   Registres du contrôleur DMA 8237 n°1 (canaux 0-3)
   --------------------------------------------------------- */
#define DMA1_MASK        0x0A
#define DMA1_MODE        0x0B
#define DMA1_FLIPFLOP    0x0C

static const unsigned int dmaAddrPort[4] = { 0x00, 0x02, 0x04, 0x06 };
static const unsigned int dmaCountPort[4] = { 0x01, 0x03, 0x05, 0x07 };
static const unsigned int dmaPagePort[4]  = { 0x87, 0x83, 0x81, 0x82 };

/* ---------------------------------------------------------
   État détecté / installé
   --------------------------------------------------------- */
unsigned int  sbBase         = 0;
int           sbIrq          = -1;
int           sbDma8         = -1;
unsigned char sbVersionMajor = 0;
unsigned char sbVersionMinor = 0;

static void interrupt (*old_sb_isr)(void);
static int  sbIrqInstalled = 0;
static int  sbHighIrq      = 0;   /* 1 si sbIrq >= 8 (second PIC) */

/* ---------------------------------------------------------
   Attente courte non bloquante à l'infini : quelques dizaines
   de lectures de port suffisent très largement pour laisser
   le DSP répondre (bien plus rapide que le budget réel du
   matériel). On borne quand même par un compteur pour ne
   jamais boucler indéfiniment si la carte ne répond pas.
   --------------------------------------------------------- */
#define DSP_TIMEOUT  0xFFFFU

/* ---------------------------------------------------------
   sbDetect — parse la variable d'environnement BLASTER
   ---------------------------------------------------------
   Format typique : "A220 I5 D1 H5 P330 T6"
     A = adresse de base, en HEXADÉCIMAL
     I = IRQ, en décimal
     D = canal DMA 8 bits, en décimal
     (les autres lettres sont ignorées : H, P, T...)
   --------------------------------------------------------- */
static unsigned int parseHex(const char *s, int *len)
{
    unsigned int v = 0;
    int n = 0;
    while (((*s >= '0' && *s <= '9') ||
            (*s >= 'A' && *s <= 'F') ||
            (*s >= 'a' && *s <= 'f')) && n < 4)
    {
        char c = *s;
        v <<= 4;
        if (c >= '0' && c <= '9') v |= (unsigned int)(c - '0');
        else if (c >= 'A' && c <= 'F') v |= (unsigned int)(c - 'A' + 10);
        else v |= (unsigned int)(c - 'a' + 10);
        s++; n++;
    }
    *len = n;
    return v;
}

static unsigned int parseDec(const char *s, int *len)
{
    unsigned int v = 0;
    int n = 0;
    while (*s >= '0' && *s <= '9' && n < 3)
    {
        v = v * 10 + (unsigned int)(*s - '0');
        s++; n++;
    }
    *len = n;
    return v;
}

int sbDetect(void)
{
    const char *env;
    int haveA = 0, haveI = 0, haveD = 0;
    int n;

    env = getenv("BLASTER");
    if (!env || !*env)
        return SB_ERR_NOENV;

    while (*env)
    {
        while (*env == ' ') env++;
        if (!*env) break;

        switch (*env)
        {
        case 'A': case 'a':
            sbBase = parseHex(env + 1, &n);
            if (n > 0) haveA = 1;
            env += 1 + n;
            break;
        case 'I': case 'i':
            sbIrq = (int)parseDec(env + 1, &n);
            if (n > 0) haveI = 1;
            env += 1 + n;
            break;
        case 'D': case 'd':
            sbDma8 = (int)parseDec(env + 1, &n);
            if (n > 0) haveD = 1;
            env += 1 + n;
            break;
        default:
            /* Lettre non gérée (H, P, T...) : sauter le jeton */
            env++;
            while (*env && *env != ' ') env++;
            break;
        }
    }

    if (!haveA || !haveI || !haveD)
        return SB_ERR_NOENV;

    if (sbIrq < 0 || sbIrq > 15 || sbDma8 < 0 || sbDma8 > 3)
        return SB_ERR_NOENV;

    sbHighIrq = (sbIrq >= 8) ? 1 : 0;
    return SB_OK;
}

/* ---------------------------------------------------------
   Primitives DSP
   --------------------------------------------------------- */

static void dspWrite(unsigned char val)
{
    unsigned int timeout = DSP_TIMEOUT;
    while (timeout-- && (inp(sbBase + DSP_WRITE) & 0x80))
        ;
    outp(sbBase + DSP_WRITE, val);
}

static int dspReadReady(void)
{
    unsigned int timeout = DSP_TIMEOUT;
    while (timeout-- && !(inp(sbBase + DSP_READ_STATUS) & 0x80))
        ;
    return (inp(sbBase + DSP_READ_STATUS) & 0x80) ? 1 : 0;
}

static unsigned char dspRead(void)
{
    return (unsigned char)inp(sbBase + DSP_READ);
}

int sbReset(void)
{
    unsigned int i;

    outp(sbBase + DSP_RESET, 1);
    /* Impulsion d'au moins ~3 microsecondes : quelques dizaines
       d'accès port suffisent très largement à cette échelle. */
    for (i = 0; i < 32U; i++)
        inp(sbBase + DSP_RESET);
    outp(sbBase + DSP_RESET, 0);

    if (!dspReadReady())
        return SB_ERR_NORESET;
    if (dspRead() != 0xAA)
        return SB_ERR_NORESET;

    /* Version du DSP (commande 0xE1, renvoie 2 octets). */
    dspWrite(0xE1);
    sbVersionMajor = dspReadReady() ? dspRead() : 0;
    sbVersionMinor = dspReadReady() ? dspRead() : 0;

    return SB_OK;
}

int sbIsDsp4(void)
{
    return (sbVersionMajor >= SB_DSP_MIN_MAJOR) ? 1 : 0;
}

/* ---------------------------------------------------------
   sbSetOutputRate — fréquence de sortie exacte (SB16)
   ---------------------------------------------------------
   La commande 0x41 (DSP >= 4.00) prend la fréquence en Hz sur
   16 bits, octet FORT d'abord (contrairement aux longueurs,
   envoyées octet faible d'abord).
   --------------------------------------------------------- */
void sbSetOutputRate(unsigned int hz)
{
    dspWrite(0x41);
    dspWrite((unsigned char)((hz >> 8) & 0xFF));   /* octet fort   */
    dspWrite((unsigned char)(hz & 0xFF));          /* octet faible */
}

void sbSetMixerVolumeMax(void)
{
    /* Port mixer = base + 0x4 (index), base + 0x5 (donnée).
       Registre 0x22 = volume maître (SB Pro : 4 bits/canal,
       SB16 : 5 bits/canal — 0xFF couvre les deux formats en
       mettant tous les bits utiles à 1, soit le maximum quel
       que soit le modèle réellement présent).
       Sans puce mixer (SB 1.0/2.0), ces ports ne répondent
       simplement pas : écrire dedans est inoffensif. */
    outp(sbBase + 0x4, 0x22);
    outp(sbBase + 0x5, 0xFF);

    /* Registre 0x04 = volume voix DAC sur les mixers CT1345
       (SB Pro). Même logique de compatibilité inoffensive. */
    outp(sbBase + 0x4, 0x04);
    outp(sbBase + 0x5, 0xFF);
}

/* ---------------------------------------------------------
   Buffer DMA "safe" (ne franchit pas une frontière de 64 Ko)
   ===========================================================
   Le DMA ISA a besoin de mémoire DOS conventionnelle (< 1 Mo),
   physiquement contiguë. On l'obtient via DPMI 0x0100 "Allocate
   DOS Memory Block" (voir sblaster.h). Cette fonction ne peut
   PAS être remplacée par malloc() classique : le tas C sous
   DOS/32A est en mémoire étendue gérée par l'extendeur, qui
   n'offre aucune garantie de contiguïté physique nécessaire au
   contrôleur DMA 8237.
   --------------------------------------------------------- */

/* Sélecteur DPMI du dernier bloc DOS alloué par
   sbAllocDmaBuffer(), nécessaire pour le libérer proprement
   avec sbFreeDmaBuffer() (DPMI 0x0101). 0 = aucun bloc alloué. */
static unsigned int dmaDosSelector = 0;

unsigned char *sbAllocDmaBuffer(unsigned int size,
                                 unsigned long *physAddr,
                                 unsigned char **rawBlock)
{
    union REGS r;
    unsigned int paragraphs;
    unsigned long segBase, phys1, phys1End;
    unsigned char *p;

    if (size == 0 || size > 32768U)
        return NULL;

    /* On demande 2*size octets (arrondis au paragraphe de 16
       octets supérieur) pour pouvoir choisir la moitié qui ne
       franchit pas une frontière physique de 64 Ko, comme avant. */
    paragraphs = (unsigned int)(((unsigned long)size * 2UL + 15UL) / 16UL);

    memset(&r, 0, sizeof(r));
    r.x.eax = 0x0100;          /* DPMI : Allocate DOS Memory Block   */
    r.x.ebx = paragraphs;      /* nombre de paragraphes demandés     */
    int386(0x31, &r, &r);

    if (r.x.cflag)             /* CF=1 : échec (mémoire conv. pleine) */
        return NULL;

    /* AX = segment réel du bloc alloué (mémoire conventionnelle).
       DX = sélecteur DPMI du bloc, à conserver pour la libération.
       Sous un extendeur flat "0-based" (DOS/32A, DOS/4GW...), la
       mémoire conventionnelle est mappée 1:1 dans l'espace linéaire
       du programme : adresse linéaire == adresse physique pour tout
       ce qui est < 1 Mo. segment*16 donne donc directement un
       pointeur plat utilisable normalement, en lecture ET écriture. */
    segBase = (unsigned long)(r.x.eax & 0xFFFFUL);
    dmaDosSelector = (unsigned int)(r.x.edx & 0xFFFFUL);

    p = (unsigned char *)(segBase << 4);

    phys1    = segBase << 4;
    phys1End = phys1 + (unsigned long)size - 1UL;

    *rawBlock = p;

    if ((phys1 >> 16) == (phys1End >> 16))
    {
        /* La première moitié ne franchit pas de frontière 64 Ko. */
        *physAddr = phys1;
        return p;
    }

    /* La première moitié franchit une frontière : la seconde
       moitié est alors garantie de ne pas la franchir (elle
       occupe l'autre côté de l'unique frontière possible dans
       une plage de 2*size <= 65536 octets). */
    *physAddr = phys1 + (unsigned long)size;
    return p + size;
}

/* Libère le bloc DOS alloué par sbAllocDmaBuffer() via DPMI 0x0101
   "Free DOS Memory Block". Ne fait rien si aucun bloc n'est alloué
   (dmaDosSelector == 0), pour rester sans effet si appelée deux
   fois ou sans allocation préalable réussie. */
void sbFreeDmaBuffer(void)
{
    union REGS r;

    if (dmaDosSelector == 0)
        return;

    memset(&r, 0, sizeof(r));
    r.x.eax = 0x0101;              /* DPMI : Free DOS Memory Block */
    r.x.edx = dmaDosSelector;
    int386(0x31, &r, &r);

    dmaDosSelector = 0;
}

/* ---------------------------------------------------------
   IRQ
   --------------------------------------------------------- */
void sbInstallIRQ(void interrupt (*handler)(void))
{
    unsigned int vector;
    unsigned int maskPort;
    unsigned char mask;

    vector = sbHighIrq ? (0x70 + (sbIrq - 8)) : (0x08 + sbIrq);

    _disable();
    old_sb_isr = _dos_getvect(vector);
    _dos_setvect(vector, handler);

    /* Démasquer l'IRQ concernée au PIC. */
    maskPort = sbHighIrq ? 0xA1 : 0x21;
    mask = (unsigned char)inp(maskPort);
    mask &= (unsigned char)~(1 << (sbHighIrq ? (sbIrq - 8) : sbIrq));
    outp(maskPort, mask);

    /* Si l'IRQ est sur le second PIC, la cascade (IRQ2 sur le
       PIC primaire) doit aussi être démasquée. Elle l'est déjà
       normalement par le BIOS, mais on s'en assure. */
    if (sbHighIrq)
    {
        mask = (unsigned char)inp(0x21);
        mask &= (unsigned char)~(1 << 2);
        outp(0x21, mask);
    }

    _enable();
    sbIrqInstalled = 1;
}

void sbRestoreIRQ(void)
{
    unsigned int vector;
    unsigned int maskPort;
    unsigned char mask;

    if (!sbIrqInstalled)
        return;

    vector = sbHighIrq ? (0x70 + (sbIrq - 8)) : (0x08 + sbIrq);

    _disable();

    /* Remasquer l'IRQ avant de rendre le vecteur, pour être sûr
       qu'aucune interruption ne puisse plus jamais retomber sur
       notre ISR une fois le vecteur restauré. */
    maskPort = sbHighIrq ? 0xA1 : 0x21;
    mask = (unsigned char)inp(maskPort);
    mask |= (unsigned char)(1 << (sbHighIrq ? (sbIrq - 8) : sbIrq));
    outp(maskPort, mask);

    _dos_setvect(vector, old_sb_isr);
    _enable();

    sbIrqInstalled = 0;
}

void sbAckIRQ(void)
{
    /* Acquitte le DSP côté 8 bits : la simple lecture de ce port
       lève le signal d'IRQ matériel. */
    inp(sbBase + DSP_READ_STATUS);

    /* EOI au(x) PIC(s). */
    if (sbHighIrq)
        outp(0xA0, 0x20);   /* PIC esclave d'abord */
    outp(0x20, 0x20);       /* PIC maître toujours */
}

/* ---------------------------------------------------------
   Sortie DMA 8 bits en boucle (auto-init), SANS réarmement
   =========================================================
   Programme le contrôleur DMA UNE SEULE FOIS en mode "auto-
   initialize" sur un buffer circulaire de 2*halfLen octets
   (les deux moitiés A+B, contiguës en mémoire ET en adresse
   physique — voir sbAllocDmaBuffer), puis démarre le DSP en
   sortie DAC 8 bits auto-init par la commande SB16 0xC6
   (8 bits, auto-init, FIFO activée), mode 0x00 (mono, non
   signé), suivie de la taille de bloc. Fréquence réglée au
   préalable par sbSetOutputRate() (commande 0x41).

   Le DSP lève ensuite une IRQ automatiquement tous les
   halfLen octets (taille de bloc), en bouclant indéfiniment
   sur le buffer sans AUCUNE
   intervention du CPU entre deux moitiés : contrairement au
   mode simple-cycle (ancienne commande 0x14, réarmée à la
   main à chaque IRQ), il n'y a ici aucun micro-trou entre
   les blocs, donc aucun clic audible, même en silence total.
   --------------------------------------------------------- */
void sbStartOutputLoop(unsigned long physAddr, unsigned int halfLen)
{
    unsigned int addrPort  = dmaAddrPort[sbDma8];
    unsigned int countPort = dmaCountPort[sbDma8];
    unsigned int pagePort  = dmaPagePort[sbDma8];
    unsigned int totalLen  = halfLen * 2U;
    unsigned int count     = totalLen - 1U;
    unsigned int blockCnt  = halfLen - 1U;

    outp(DMA1_MASK, 0x04 | sbDma8);          /* masquer le canal   */
    outp(DMA1_FLIPFLOP, 0);                  /* reset flip-flop    */
    outp(DMA1_MODE, 0x58 | sbDma8);          /* lecture, auto-init */

    outp(addrPort, (unsigned char)(physAddr & 0xFF));
    outp(addrPort, (unsigned char)((physAddr >> 8) & 0xFF));
    outp(pagePort, (unsigned char)((physAddr >> 16) & 0xFF));

    outp(countPort, (unsigned char)(count & 0xFF));
    outp(countPort, (unsigned char)((count >> 8) & 0xFF));

    outp(DMA1_MASK, sbDma8);                 /* démasquer le canal */

    /* Taille de bloc = une moitié : une IRQ tous les halfLen
       octets, exactement à la frontière entre A et B. */
    /* 0xC6 = sortie 8 bits, auto-init, FIFO.
       Mode 0x00 = mono, échantillons non signés (bit 4 : signé,
       bit 5 : stéréo). Puis taille de bloc - 1, octet faible
       d'abord. */
    dspWrite(0xC6);
    dspWrite(0x00);
    dspWrite((unsigned char)(blockCnt & 0xFF));
    dspWrite((unsigned char)((blockCnt >> 8) & 0xFF));
}

/* ---------------------------------------------------------
   Sortie DMA 8 bits simple-cycle (un seul bloc, non bouclé)
   =========================================================
   Conservée pour compatibilité/tests ponctuels ; le moteur
   audio (audio.c) utilise désormais sbStartOutputLoop() pour
   la lecture continue, qui évite les clics entre blocs.
   --------------------------------------------------------- */
void sbStartOutput(unsigned long physAddr, unsigned int len)
{
    unsigned int addrPort  = dmaAddrPort[sbDma8];
    unsigned int countPort = dmaCountPort[sbDma8];
    unsigned int pagePort  = dmaPagePort[sbDma8];
    unsigned int count     = len - 1U;

    outp(DMA1_MASK, 0x04 | sbDma8);          /* masquer le canal   */
    outp(DMA1_FLIPFLOP, 0);                  /* reset flip-flop    */
    outp(DMA1_MODE, 0x48 | sbDma8);          /* lecture, simple-cycle */

    outp(addrPort, (unsigned char)(physAddr & 0xFF));
    outp(addrPort, (unsigned char)((physAddr >> 8) & 0xFF));
    outp(pagePort, (unsigned char)((physAddr >> 16) & 0xFF));

    outp(countPort, (unsigned char)(count & 0xFF));
    outp(countPort, (unsigned char)((count >> 8) & 0xFF));

    outp(DMA1_MASK, sbDma8);                 /* démasquer le canal */

    /* Commande DSP 0x14 : sortie DAC 8 bits, simple-cycle,
       longueur len-1 (16 bits, octet faible puis fort). */
    dspWrite(0x14);
    dspWrite((unsigned char)(count & 0xFF));
    dspWrite((unsigned char)((count >> 8) & 0xFF));
}

void sbStopOutput(void)
{
    /* 0xD0 = pause DMA 8 bits. Un reset complet du DSP juste
       après garantit un silence propre même si le DSP était
       au milieu d'un transfert. */
    dspWrite(0xD0);
    sbReset();
    outp(DMA1_MASK, 0x04 | sbDma8);   /* masquer le canal DMA */
}
