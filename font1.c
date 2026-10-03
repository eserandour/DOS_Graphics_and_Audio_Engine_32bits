/* =========================================================
   FONT1.C — Moteur de rendu texte bitmap
   =========================================================
   API unifiée : font1DrawChar, font1DrawText, font1DrawTextCentered
   acceptent un Font1* qui encapsule indifféremment la police
   BIOS ROM ou une Font1Bank personnelle.

   Les données brutes des glyphes sont dans font1/.
   ========================================================= */

#include <stdlib.h>    /* exit    */
#include <string.h>    /* memset  */
#include <malloc.h>    /* malloc, free */
#include "video.h"     /* SCREEN_WIDTH */
#include "graphics.h"  /* putPixel */
#include "font1.h"

/* =========================================================
   VARIABLES GLOBALES
   ========================================================= */

/* Pointeur plat vers la table de glyphes 8x8 en ROM BIOS,
   adresse physique fixe 0xFFA6E (F000:FA6E en notation
   segment:offset historique) sur tous les PC IBM compatibles. */
static unsigned char *font1Bios = NULL;

/* Font1Bank sous-jacentes, allouées dynamiquement par font1InitBank*(). */
Font1Bank *font1Bank8x8   = NULL;
Font1Bank *font1Bank8x16  = NULL;
Font1Bank *font1Bank16x16  = NULL;

/* Structures Font1 globales prêtes à l'emploi.
   Initialisées statiquement ci-dessous ; font1InitBank*()
   renseigne ensuite le champ .bank.
   FONT1_BIOS.bank = NULL : la police vient directement de la ROM,
   pas d'une Font1Bank allouée en mémoire.

   Troisième champ : Font1.size = espacement horizontal en pixels
   entre deux caractères consécutifs dans font1DrawText().
   Pour 8x16, la largeur utile du glyphe est 8 px (pas 16) :
   le pas horizontal est donc 8. */
Font1 FONT1_BIOS       = { FONT1_TYPE_BIOS, NULL,  8 };
Font1 FONT1_BANK_8X8   = { FONT1_TYPE_BANK, NULL,  8 };
Font1 FONT1_BANK_8X16  = { FONT1_TYPE_BANK, NULL,  8 };
Font1 FONT1_BANK_16X16 = { FONT1_TYPE_BANK, NULL, 16 };

/* =========================================================
   INITIALISATION INTERNE
   ========================================================= */

/* Prépare une Font1Bank vide pour la taille donnée.
   Alloue data au juste nécessaire, met la LUT à -1. */
static void _initFont1Bank(Font1Bank *fb, Font1Size size)
{
    int i, bpg;

    switch (size)
    {
        case FONT1_SIZE_8X8:   bpg = FONT1_BANK_8X8_GLYPH_BYTES;   break;
        case FONT1_SIZE_8X16:  bpg = FONT1_BANK_8X16_GLYPH_BYTES;  break;
        case FONT1_SIZE_16X16: bpg = FONT1_BANK_16X16_GLYPH_BYTES; break;
    }

    fb->size            = size;
    fb->count           = 0;
    fb->capacity        = FONT1_BANK_CAPACITY;
    fb->bytes_per_glyph = bpg;

    fb->data = (unsigned char *)malloc(
                   (unsigned long)FONT1_BANK_CAPACITY * bpg);
    if (!fb->data) { setVideoMode(0x03); exit(1); }

    for (i = 0; i < 256; i++) fb->lut[i] = -1;
    memset(fb->data, 0, (unsigned long)FONT1_BANK_CAPACITY * bpg);
}

/* =========================================================
   DÉFINITION DE GLYPHES
   ========================================================= */

/* Ajoute ou remplace un glyphe 8x8.
   Alloue un nouveau slot si le caractère n'existe pas encore
   dans la Font1Bank, sinon écrase l'existant. */
void font1DefineChar8x8(Font1Bank *fb, unsigned char c,
                 unsigned char b0, unsigned char b1,
                 unsigned char b2, unsigned char b3,
                 unsigned char b4, unsigned char b5,
                 unsigned char b6, unsigned char b7)
{
    int slot;
    unsigned char *g;

    if (fb->lut[c] == -1)
    {
        if (fb->count >= fb->capacity) return;
        fb->lut[c] = fb->count++;
    }
    slot = fb->lut[c];
    g    = fb->data + slot * fb->bytes_per_glyph;
    g[0]=b0; g[1]=b1; g[2]=b2; g[3]=b3;
    g[4]=b4; g[5]=b5; g[6]=b6; g[7]=b7;
}

/* Ajoute ou remplace un glyphe 8x16.
   Stockage direct : 16 octets, 1 par ligne, MSB = pixel gauche. */
void font1DefineChar8x16(Font1Bank *fb, unsigned char c,
                 unsigned char b0,  unsigned char b1,
                 unsigned char b2,  unsigned char b3,
                 unsigned char b4,  unsigned char b5,
                 unsigned char b6,  unsigned char b7,
                 unsigned char b8,  unsigned char b9,
                 unsigned char b10, unsigned char b11,
                 unsigned char b12, unsigned char b13,
                 unsigned char b14, unsigned char b15)
{
    int slot;
    unsigned char *g;

    if (fb->lut[c] == -1)
    {
        if (fb->count >= fb->capacity) return;
        fb->lut[c] = fb->count++;
    }
    slot = fb->lut[c];
    g    = fb->data + slot * fb->bytes_per_glyph;
    g[ 0]=b0;  g[ 1]=b1;  g[ 2]=b2;  g[ 3]=b3;
    g[ 4]=b4;  g[ 5]=b5;  g[ 6]=b6;  g[ 7]=b7;
    g[ 8]=b8;  g[ 9]=b9;  g[10]=b10; g[11]=b11;
    g[12]=b12; g[13]=b13; g[14]=b14; g[15]=b15;
}

/* Ajoute ou remplace un glyphe 16x16.
   Stockage big-endian : octet haut en premier pour que
   le rendu lise les bits de gauche à droite. */
void font1DefineChar16x16(Font1Bank *fb, unsigned char c,
                  unsigned int rows[16])
{
    int slot, i;
    unsigned char *g;

    if (fb->lut[c] == -1)
    {
        if (fb->count >= fb->capacity) return;
        fb->lut[c] = fb->count++;
    }
    slot = fb->lut[c];
    g    = fb->data + slot * fb->bytes_per_glyph;
    for (i = 0; i < 16; i++)
    {
        g[i*2]   = (unsigned char)(rows[i] >> 8);
        g[i*2+1] = (unsigned char)(rows[i] & 0xFF);
    }
}

/* =========================================================
   RENDU INTERNE
   ========================================================= */

/* Rend un glyphe 8x8.
   Pour chaque ligne, lit l'octet de bits et teste chaque
   bit avec un masque décalé de 0x80 (gauche) à 0x01 (droite). */
static void _renderGlyph8(int x, int y, unsigned char color,
                          unsigned char *glyph)
{
    int row, col;
    unsigned char bits;

    for (row = 0; row < 8; row++)
    {
        bits = glyph[row];
        for (col = 0; col < 8; col++)
            if (bits & (0x80 >> col))
                putPixel(x+col, y+row, color);
    }
}

/* Rend un glyphe 8x16.
   Largeur 8 px (1 octet/ligne), hauteur 16 lignes. */
static void _renderGlyph8x16(int x, int y, unsigned char color,
                             unsigned char *glyph)
{
    int row, col;
    unsigned char bits;

    for (row = 0; row < 16; row++)
    {
        bits = glyph[row];
        for (col = 0; col < 8; col++)
            if (bits & (0x80 >> col))
                putPixel(x+col, y+row, color);
    }
}

/* Rend un glyphe 16x16.
   Reconstitue chaque ligne en unsigned int depuis 2 octets
   big-endian, puis teste les 16 bits avec masque 0x8000→0x0001. */
static void _renderGlyph16(int x, int y, unsigned char color,
                           unsigned char *glyph)
{
    int row, col;
    unsigned int bits;

    for (row = 0; row < 16; row++)
    {
        bits = ((unsigned int)glyph[row*2] << 8) | glyph[row*2+1];
        for (col = 0; col < 16; col++)
            if (bits & (0x8000 >> col))
                putPixel(x+col, y+row, color);
    }
}

/* =========================================================
   RENDU PUBLIC — API UNIFIÉE
   ========================================================= */

/* Dessine un caractère avec la police f.
   c est un code direct (0x00–0xFF).
   Pour FONT1_TYPE_BIOS, le code est passé tel quel à la ROM.
   Pour FONT1_TYPE_BANK, le code indexe directement la LUT. */
void font1DrawChar(int x, int y, unsigned char c,
              unsigned char color, Font1 *f)
{
    unsigned char *glyph;
    unsigned char *g;

    if (f->type == FONT1_TYPE_BIOS)
    {
        /* Police BIOS : table ROM à F000:FA6E, 128 glyphes (0–127).
           Les codes >= 128 sont hors table : on les remplace par '?'. */
        if (c >= 128) c = '?';
        glyph = font1Bios + ((unsigned int)c * 8);
        _renderGlyph8(x, y, color, glyph);
    }
    else
    {
        /* Police personnelle : chercher le glyphe dans la Font1Bank. */
        int slot = f->bank->lut[(unsigned int)c];
        if (slot < 0) return;   /* caractère non défini */

        g = f->bank->data + slot * f->bank->bytes_per_glyph;

        switch (f->bank->size)
        {
            case FONT1_SIZE_8X8:
                _renderGlyph8(x, y, color, g);
                break;
            case FONT1_SIZE_8X16:
                _renderGlyph8x16(x, y, color, g);
                break;
            case FONT1_SIZE_16X16:
                _renderGlyph16(x, y, color, g);
                break;
        }
    }
}

/* Dessine une chaîne avec la police f.
   Chaque octet de str est un code direct.
   L'espacement entre caractères = f->size pixels. */
void font1DrawText(int x, int y, const char *str,
              unsigned char color, Font1 *f)
{
    int i    = 0;
    int step = f->size;

    while (str[i] != '\0')
    {
        font1DrawChar(x + i * step, y, (unsigned char)str[i], color, f);
        i++;
    }
}

/* Dessine une chaîne centrée horizontalement.
   Largeur totale = longueur en octets * f->size pixels. */
void font1DrawTextCentered(int y, const char *str,
                      unsigned char color, Font1 *f)
{
    int len = 0, x;
    while (str[len] != '\0') len++;
    x = (SCREEN_WIDTH - len * f->size) / 2;
    if (x < 0) x = 0;
    font1DrawText(x, y, str, color, f);
}

/* =========================================================
   INITIALISATION
   ========================================================= */

/* Charge font1Bios depuis la ROM BIOS.
   F000:FA6E est l'adresse fixe de la table de polices 8x8
   dans le BIOS de tous les PC IBM compatibles, soit l'adresse
   physique 0xFFA6E (0xF000*16 + 0xFA6E).
   En modèle flat sous DOS/32A, le premier méga-octet est mappé
   1:1 en adresses linéaires (comme la VRAM, voir video.c) : la
   ROM BIOS est donc directement lisible via un pointeur plat
   ordinaire, sans MK_FP()/segment:offset. */
void font1InitBios(void)
{
    font1Bios = (unsigned char *)0xFFA6EUL;
    /* FONT1_BIOS est déjà initialisée statiquement,
       pas besoin de modifier ses champs ici. */
}

/* Alloue font1Bank8x8 dynamiquement, initialise la Font1Bank,
   charge ses glyphes depuis font1/,
   et met à jour FONT1_BANK_8X8 pour qu'elle pointe dessus. */
void font1InitBank8x8(void)
{
    font1Bank8x8 = (Font1Bank *)malloc(sizeof(Font1Bank));
    if (!font1Bank8x8) { setVideoMode(0x03); exit(1); }
    _initFont1Bank(font1Bank8x8, FONT1_SIZE_8X8);
    _initFont1_8x8();
    FONT1_BANK_8X8.bank = font1Bank8x8;
}

/* Alloue font1Bank8x16 dynamiquement, initialise la Font1Bank,
   charge ses glyphes depuis font1/,
   et met a jour FONT1_BANK_8X16 pour qu'elle pointe dessus. */
void font1InitBank8x16(void)
{
    font1Bank8x16 = (Font1Bank *)malloc(sizeof(Font1Bank));
    if (!font1Bank8x16) { setVideoMode(0x03); exit(1); }
    _initFont1Bank(font1Bank8x16, FONT1_SIZE_8X16);
    _initFont1_8x16();
    FONT1_BANK_8X16.bank = font1Bank8x16;
}

/* Alloue font1Bank16x16 dynamiquement, initialise la Font1Bank,
   charge ses glyphes depuis font1/,
   et met à jour FONT1_BANK_16X16 pour qu'elle pointe dessus. */
void font1InitBank16x16(void)
{
    font1Bank16x16 = (Font1Bank *)malloc(sizeof(Font1Bank));
    if (!font1Bank16x16) { setVideoMode(0x03); exit(1); }
    _initFont1Bank(font1Bank16x16, FONT1_SIZE_16X16);
    _initFont1_16x16();
    FONT1_BANK_16X16.bank = font1Bank16x16;
}

/* Libère les deux allocations d'une Font1Bank.
   Appel sans effet si *fb == NULL.

   *fb est forcément l'une des trois Font1Bank globales
   (font1Bank8x8, font1Bank8x16 ou font1Bank16x16) : on met
   aussi à jour le champ .bank du Font1 correspondant
   (FONT1_BANK_8X8 / _8X16 / _16X16), pour qu'il ne pointe
   plus jamais vers la mémoire qu'on vient de libérer. */
void font1FreeBank(Font1Bank **fb)
{
    if (!*fb) return;

    if      (*fb == font1Bank8x8)   FONT1_BANK_8X8.bank   = NULL;
    else if (*fb == font1Bank8x16)  FONT1_BANK_8X16.bank  = NULL;
    else if (*fb == font1Bank16x16) FONT1_BANK_16X16.bank = NULL;

    if ((*fb)->data) { free((*fb)->data); (*fb)->data = NULL; }
    free(*fb);
    *fb = NULL;
}
