#ifndef FONT2_H
#define FONT2_H

/* =========================================================
   FONT2.H — Affichage de texte par feuille de sprites
   =========================================================
   Environnement : Open Watcom 1.9, DOS
   Mode video    : 13h (320x200, 256 couleurs, 1 octet/pixel)

   PRINCIPE
   --------
   Les glyphes sont stockes dans une feuille de sprites .raw,
   organisee en grille reguliere. La police est decrite par
   une structure Font2Desc, ce qui permet d'utiliser plusieurs
   polices differentes selon les besoins de l'appelant.

   UTILISATION
   -----------
   1) Declarer un descripteur (ou utiliser un predefini) :

        Font2Desc myFont = FONT2_DESC_DEFAULT;

   2) Charger la feuille en RAM :

        if (!font2Load(&myFont)) { ... erreur ... }

   3) Dessiner :

        font2DrawText(&myFont, "SCORE 1000", 10, 84);
        font2DrawChar(&myFont, 'A', 160, 84);

   4) Liberer quand la police n'est plus utile :

        font2Free(&myFont);

   TRANSPARENCE
   ------------
   colorKey < 0  : copie opaque (fond du glyphe ecrase le BB).
   colorKey >= 0 : pixels d'index colorKey non ecrits.
   Chaque descripteur porte son propre colorKey.
   ========================================================= */


/* =========================================================
   DESCRIPTEUR DE POLICE
   ========================================================= */

typedef struct
{
    const char *path;       /* chemin vers le .raw          */
    int sheet_w;            /* largeur totale (px)          */
    int sheet_h;            /* hauteur totale (px)          */
    int char_w;             /* largeur d'un glyphe (px)     */
    int char_h;             /* hauteur d'un glyphe (px)     */
    int cols;               /* colonnes dans la grille      */
    int first_char;         /* premier caractere (ASCII)    */
    int last_char;          /* dernier caractere (ASCII)    */
    int colorKey;           /* index transparent, ou -1     */

    /* --- champ interne, ne pas modifier manuellement --- */
    unsigned char *sheet; /* buffer RAM (NULL si vide)  */

} Font2Desc;


/* =========================================================
   DESCRIPTEURS PREDEFINIS
   =========================================================
   Utilisables directement :
     Font2Desc f = FONT2_DESC_DEFAULT;
     Font2Desc f = FONT2_DESC_16X16_F2;
   ========================================================= */

/* font.raw : grille 320x192, glyphes 32x32, 0x20..0x5B, bg=127 */
#define FONT2_DESC_DEFAULT \
    { "font2\\font.raw", 320, 192, 32, 32, 10, 0x20, 0x5B, 127, 0 }

/* 16X16_F2.raw : grille 320x48, glyphes 16x16, 0x20..0x5B, bg=5 */
#define FONT2_DESC_16X16_F2 \
    { "font2\\16X16_F2.raw", 320, 48, 16, 16, 20, 0x20, 0x5B, 5, 0 }


/* =========================================================
   API PUBLIQUE
   ========================================================= */

/* Charge la feuille en mémoire (malloc).
   Retourne 1 si succes, 0 si echec (memoire ou fichier). */
int font2Load(Font2Desc *fd);

/* Libere la RAM. Met fd->sheet a NULL. */
void font2Free(Font2Desc *fd);

/* Retourne 1 si la feuille est chargee en RAM, 0 sinon. */
int font2IsLoaded(const Font2Desc *fd);

/* Dessine le caractere c a (x, y) dans le backbuffer.
   Caractere hors plage : rien n'est dessine.            */
void font2DrawChar(Font2Desc *fd, unsigned char c,
                   int x, int y);

/* Dessine text a partir de (x, y) dans le backbuffer.
   Les caracteres hors plage sont ignores.               */
void font2DrawText(Font2Desc *fd, const char *text,
                   int x, int y);

/* Dessine text centre horizontalement sur SCREEN_WIDTH.
   Si la chaine depasse 320 px, calee a gauche (x=0).  */
void font2DrawTextCentered(Font2Desc *fd, const char *text,
                           int y);

/* Retourne l'octet a (sx, sy) dans la feuille RAM.
   Precondition : font2IsLoaded(fd).
   Usage : acces colonne par colonne pour le scroller.   */
unsigned char font2GetPixel(const Font2Desc *fd,
                             int sx, int sy);

#endif /* FONT2_H */
