#ifndef FONT1_H
#define FONT1_H

/* =========================================================
   FONT1.H — Moteur de texte bitmap multi-tailles
   =========================================================
   API unifiée : toutes les fonctions de rendu acceptent
   un pointeur Font1*, quelle que soit la source de la police
   (ROM BIOS ou Font1Bank personnelle).

   Utilisation :
     font1DrawChar(x, y, 'A', 255, &FONT1_BIOS);
     font1DrawChar(x, y, 'A', 255, &FONT1_BANK_8X8);
     font1DrawChar(x, y, 'A', 255, &FONT1_BANK_8X16);
     font1DrawChar(x, y, 'A', 255, &FONT1_BANK_16X16);

   Pour ajouter une nouvelle police :
     1. Générer un fichier de glyphes dans font1/ (OUTILS/fonts/ :
        psf2c.py, png2c.py ou ttf2c.py), qui appelle
        font1DefineChar*() pour chaque caractère.
     2. Dans font1.c, ajouter une fonction font1InitBank*() qui
        alloue la Font1Bank, la prépare avec _initFont1Bank()
        (fonction static, interne à font1.c) puis appelle le
        chargeur de glyphes.
     3. Créer une Font1 pointant vers cette Font1Bank.

   Fonctionnement d'un glyphe :
   Chaque ligne est encodée en binaire : bit 1 = pixel allumé,
   bit 0 = transparent. Le MSB correspond au pixel le plus
   à gauche de la ligne.
   ========================================================= */

/* ---------------------------------------------------------
   Constantes
   --------------------------------------------------------- */

/* Octets par glyphe selon la taille :
   8x8   :  8 lignes × 1 octet  =   8 octets
   8x16  : 16 lignes × 1 octet  =  16 octets
   16x16 : 16 lignes × 2 octets =  32 octets */
#define FONT1_BANK_8X8_GLYPH_BYTES   8
#define FONT1_BANK_8X16_GLYPH_BYTES  16
#define FONT1_BANK_16X16_GLYPH_BYTES  32

/* ---------------------------------------------------------
   Macros CP850 — accents et caractères spéciaux français
   --------------------------------------------------------- */

/* Usage : font1DrawText(x, y, "caract" eGRAVE "re", 255, &FONT1_BANK_8X8);
   La concaténation de littéraux adjacents est résolue à la
   compilation, sans coût à l'exécution.

   Attention avec \xNN : si le caractère suivant est un chiffre
   hexadécimal (0-9, a-f, A-F), le compilateur l'incorpore dans
   l'échappement. Dans ce cas, couper la chaîne :
     "\x82" "e"  et non  "\x82e"  (lu comme un seul code 0x82E) */

/* Minuscules accentuées */
#define eAIGU   "\x82"   /* é   CP850 : 0x82   Unicode : 0xE9 */
#define eGRAVE  "\x8A"   /* è   CP850 : 0x8A   Unicode : 0xE8 */
#define eCIRC   "\x88"   /* ê   CP850 : 0x88   Unicode : 0xEA */
#define eTREMA  "\x89"   /* ë   CP850 : 0x89   Unicode : 0xEB */
#define aGRAVE  "\x85"   /* à   CP850 : 0x85   Unicode : 0xE0 */
#define aCIRC   "\x83"   /* â   CP850 : 0x83   Unicode : 0xE2 */
#define aTREMA  "\x84"   /* ä   CP850 : 0x84   Unicode : 0xE4 */
#define uGRAVE  "\x97"   /* ù   CP850 : 0x97   Unicode : 0xF9 */
#define uCIRC   "\x96"   /* û   CP850 : 0x96   Unicode : 0xFB */
#define uTREMA  "\x81"   /* ü   CP850 : 0x81   Unicode : 0xFC */
#define iCIRC   "\x8C"   /* î   CP850 : 0x8C   Unicode : 0xEE */
#define iTREMA  "\x8B"   /* ï   CP850 : 0x8B   Unicode : 0xEF */
#define oCIRC   "\x93"   /* ô   CP850 : 0x93   Unicode : 0xF4 */
#define oTREMA  "\x94"   /* ö   CP850 : 0x94   Unicode : 0xF6 */
#define cCED    "\x87"   /* ç   CP850 : 0x87   Unicode : 0xE7 */
/* Majuscules accentuées */
#define EAIGU   "\x90"   /* É   CP850 : 0x90   Unicode : 0xC9 */
#define ATREMA  "\x8E"   /* Ä   CP850 : 0x8E   Unicode : 0xC4 */
#define OTREMA  "\x99"   /* Ö   CP850 : 0x99   Unicode : 0xD6 */
#define UTREMA  "\x9A"   /* Ü   CP850 : 0x9A   Unicode : 0xDC */
#define CCED    "\x80"   /* Ç   CP850 : 0x80   Unicode : 0xC7 */

/* Nombre maximum de glyphes par Font1Bank.
   256 = ensemble complet des caractères (0 à 255). */
#define FONT1_BANK_CAPACITY  256

/* ---------------------------------------------------------
   Font1Size — tailles de glyphes supportées
   --------------------------------------------------------- */

/* Font1Size — identifiant de la taille des glyphes.
   N'encode PAS l'espacement horizontal : ce rôle
   est porté exclusivement par Font1.size, renseigné
   par l'initialisation statique des Font1 dans font1.c. */
typedef enum {
    FONT1_SIZE_8X8   = 0,
    FONT1_SIZE_8X16  = 1,
    FONT1_SIZE_16X16 = 2
} Font1Size;

/* ---------------------------------------------------------
   Font1Bank — stockage des glyphes personnels
   --------------------------------------------------------- */

/* Contient tous les glyphes d'une police bitmap personnelle.
   - size            : taille des glyphes (Font1Size :
                       8x8, 8x16 ou 16x16)
   - count           : nombre de glyphes définis
   - capacity        : nombre max de glyphes (256)
   - bytes_per_glyph : taille en octets d'un glyphe
   - lut[256]        : lut[c] = index du glyphe pour le
                       caractère c, ou -1 si non défini
   - data            : pointeur vers le tableau compact des glyphes.
                       Alloué séparément par malloc(capacity * bytes_per_glyph)
                       pour ne réserver que l'espace utile. */
typedef struct {
    Font1Size         size;
    int               count;
    int               capacity;
    int               bytes_per_glyph;
    int               lut[256];
    unsigned char *data;   /* alloué au juste nécessaire */
} Font1Bank;

/* ---------------------------------------------------------
   Font1Type — type de source de la police
   --------------------------------------------------------- */

typedef enum {
    FONT1_TYPE_BIOS = 0,   /* police ROM BIOS 8x8            */
    FONT1_TYPE_BANK = 1    /* police personnelle dans une Font1Bank */
} Font1Type;

/* ---------------------------------------------------------
   Font1 — structure unifiée (BIOS ou Font1Bank)
   --------------------------------------------------------- */

/* Structure passée à toutes les fonctions de rendu.
   type  : indique si la police est BIOS ou personnelle.
   bank  : pointeur vers la Font1Bank (si FONT1_TYPE_BANK).
           NULL si FONT1_TYPE_BIOS.
   size  : taille en pixels d'un glyphe (espacement). */
typedef struct {
    Font1Type      type;
    Font1Bank *bank;
    int            size;
} Font1;

/* ---------------------------------------------------------
   Polices globales prêtes à l'emploi
   --------------------------------------------------------- */

/* Police BIOS ROM 8x8 (128 caractères IBM/ASCII). */
extern Font1 FONT1_BIOS;

/* Polices personnelles initialisées par font1InitBank*(). */
extern Font1 FONT1_BANK_8X8;    /* font1Bank8x8 */
extern Font1 FONT1_BANK_8X16;   /* font1Bank8x16 */
extern Font1 FONT1_BANK_16X16;  /* font1Bank16x16 */

/* Font1Bank sous-jacentes allouées dynamiquement.
   Initialisées par font1InitBank8x8/8x16/16x16(), NULL avant. */
extern Font1Bank *font1Bank8x8;
extern Font1Bank *font1Bank8x16;
extern Font1Bank *font1Bank16x16;

/* ---------------------------------------------------------
   Initialisation
   --------------------------------------------------------- */

/* Charge font1Bios depuis la ROM BIOS (adresse physique 0xFFA6E,
   soit F000:FA6E en notation segment:offset historique).
   À appeler avant tout font1DrawChar avec FONT1_BIOS. */
void font1InitBios(void);

/* Initialise font1Bank8x8 et charge ses glyphes depuis font1/.
   Met à jour FONT1_BANK_8X8 pour qu'elle pointe sur font1Bank8x8. */
void font1InitBank8x8(void);

/* Initialise font1Bank8x16 et charge ses glyphes depuis font1/. */
void font1InitBank8x16(void);

/* Initialise font1Bank16x16 et charge ses glyphes. */
void font1InitBank16x16(void);

/* Libère les deux allocations d'une Font1Bank
   (la struct elle-même et le tableau data).
   Met le pointeur passé à NULL, ainsi que le champ .bank
   du Font1 correspondant (FONT1_BANK_8X8/_8X16/_16X16),
   pour éviter tout pointeur pendant après libération. */
void font1FreeBank(Font1Bank **fb);

/* ---------------------------------------------------------
   Définition de glyphes
   --------------------------------------------------------- */

/* Ajoute ou remplace un glyphe 8x8 dans une Font1Bank.
   b0 = ligne du haut, b7 = ligne du bas.
   Bit 7 de chaque octet = pixel le plus à gauche. */
void font1DefineChar8x8(Font1Bank *fb, unsigned char c,
                 unsigned char b0, unsigned char b1,
                 unsigned char b2, unsigned char b3,
                 unsigned char b4, unsigned char b5,
                 unsigned char b6, unsigned char b7);

/* Ajoute ou remplace un glyphe 8x16.
   b0 = ligne du haut, b15 = ligne du bas.
   Bit 7 de chaque octet = pixel le plus a gauche. */
void font1DefineChar8x16(Font1Bank *fb, unsigned char c,
                 unsigned char b0,  unsigned char b1,
                 unsigned char b2,  unsigned char b3,
                 unsigned char b4,  unsigned char b5,
                 unsigned char b6,  unsigned char b7,
                 unsigned char b8,  unsigned char b9,
                 unsigned char b10, unsigned char b11,
                 unsigned char b12, unsigned char b13,
                 unsigned char b14, unsigned char b15);

/* Ajoute ou remplace un glyphe 16x16.
   rows[16] : 16 unsigned int, bit 15 = pixel gauche. */
void font1DefineChar16x16(Font1Bank *fb, unsigned char c,
                  unsigned int rows[16]);

/* ---------------------------------------------------------
   Rendu — API unifiée
   --------------------------------------------------------- */

/* Dessine un caractère avec la police f.
   Fonctionne avec FONT1_BIOS, FONT1_BANK_8X8, FONT1_BANK_8X16,
   FONT1_BANK_16X16 ou toute autre Font1 correctement initialisée. */
void font1DrawChar(int x, int y, unsigned char c,
              unsigned char color, Font1 *f);

/* Dessine une chaîne de caractères avec la police f.
   L'espacement entre caractères = f->size pixels. */
void font1DrawText(int x, int y, const char *str,
              unsigned char color, Font1 *f);

/* Dessine une chaîne centrée horizontalement avec la
   police f. */
void font1DrawTextCentered(int y, const char *str,
                      unsigned char color, Font1 *f);


/* ---------------------------------------------------------
   Chargeurs de glyphes — usage interne uniquement
   Appelées par font1InitBank8x8/8x16/16x16 dans font1.c.
   Ne pas appeler directement depuis les scènes ou main.c.
   --------------------------------------------------------- */
void _initFont1_8x8(void);
void _initFont1_8x16(void);
void _initFont1_16x16(void);

#endif /* FONT1_H */
