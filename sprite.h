#ifndef SPRITE_H
#define SPRITE_H

/* =========================================================
   SPRITE.H — Sprites préchargés en mémoire, mode 13h
   =========================================================
   Environnement : Open Watcom 1.9, DOS (modèle flat 32 bits, DOS/32A)
   Mode vidéo    : 13h (320x200, 256 couleurs, 1 octet/pixel)

   PRINCIPE
   --------
   image.c charge les pixels depuis le disque à chaque blit.
   Sous DOS, avec un accès disque lent (disquette, FAT12),
   un fread() par frame crée un goulot d'étranglement visible.

   Ce module charge le .raw UNE SEULE FOIS en mémoire au
   démarrage (spriteLoad), puis blit depuis la RAM à chaque
   frame (spriteBlit / spriteBlitKey). Le disque n'est plus
   touché pendant le rendu.

   TAILLE MAXIMALE (spriteLoad vs spriteLoadSplit)
   --------------------------------------------------
   En modèle flat 32 bits, malloc() n'impose aucune limite de
   taille de bloc : spriteLoad() charge n'importe quelle
   hauteur h en un seul bloc, jusqu'à la mémoire disponible.
   Seule la largeur reste bornée à 320 pixels (rowBuf[320] en
   interne, cohérent avec la largeur de l'écran) : elle n'est pas
   vérifiée, w > 320 déborderait ce buffer.

   spriteLoadSplit()/SpriteSplit offrent une alternative qui
   répartit les données en blocs fixes de 32 768 octets
   (capacité totale SPR_SPLIT_MAX) : utile si vous préférez
   allouer par blocs de taille bornée plutôt qu'un seul bloc
   contigu. Pour un usage courant, spriteLoad() est plus simple
   et suffit dans l'immense majorité des cas.

   TRANSPARENCE (colorKey)
   -----------------------
   (image.c, lui, ne gère pas la transparence.)
     colorKey <  0  : blit opaque, tous les pixels copiés.
     colorKey >= 0  : les pixels d'index == colorKey ne sont
                      pas écrits (fond laissé intact).

   FEUILLE DE SPRITES (sprite sheet)
   -----------------------------------
   Un Sprite peut représenter une feuille entière (ex. une
   feuille de police comme celles de font2/ ; font2.c utilise
   toutefois son propre chargeur, pas ce module). spriteBlitZone / spriteBlitZoneKey
   extraient un rectangle (srcX, srcY, zoneW, zoneH) depuis
   la feuille et le copient dans le backbuffer, sans aucun
   accès disque.

   CODES DE RETOUR
   ---------------
   SPR_OK           0   succès
   SPR_ERR_MEM      1   malloc a retourné NULL
   SPR_ERR_FILE     2   fopen a échoué
   SPR_ERR_READ     3   fread incomplet (fichier trop court)
   SPR_ERR_SIZE     4   spriteLoadSplit uniquement :
                        w*h > SPR_SPLIT_MAX

   FLUX TYPIQUES
   -------------
   Sprite simple, opaque :
       Sprite spr;
       if (spriteLoad(&spr, "bullet.raw", 8, 8) != SPR_OK) ...
       ...
       spriteBlit(&spr, x, y);          // chaque frame
       ...
       spriteFree(&spr);                // en fin de scène

   Sprite avec transparence (index 0 = fond) :
       spriteBlitKey(&spr, x, y, 0);

   Feuille de sprites, glyphe à (col*16, row*16) taille 16x16 :
       spriteBlitZoneKey(&sheet, col*16, row*16, 16, 16,
                         dstX, dstY, colorKey);

   Grande feuille via l'API par blocs (alternative à spriteLoad()
   quand vous préférez allouer par blocs de taille bornée) :
       SpriteSplit big;
       if (spriteLoadSplit(&big, "bg.raw", 320, 256) != SPR_OK) ...
       spriteBlitSplit(&big, dstX, dstY);
       spriteFreeSplit(&big);
   ========================================================= */


/* =========================================================
   CODES DE RETOUR
   ========================================================= */

#define SPR_OK        0   /* succès                              */
#define SPR_ERR_MEM   1   /* malloc a retourné NULL               */
#define SPR_ERR_FILE  2   /* fopen a échoué                       */
#define SPR_ERR_READ  3   /* fread incomplet                      */
#define SPR_ERR_SIZE  4   /* spriteLoadSplit : w*h > SPR_SPLIT_MAX */


/* =========================================================
   STRUCTURE Sprite — hauteur libre, largeur <= 320 pixels
   ========================================================= */

typedef struct {
    unsigned char *data;  /* pixels en mémoire              */
    int w;                    /* largeur en pixels             */
    int h;                    /* hauteur en pixels             */
} Sprite;


/* =========================================================
   STRUCTURE SpriteSplit — allocation par blocs de taille fixe
   =========================================================
   N blocs de 32 768 octets chacun (sauf le dernier,
   qui peut être plus petit). blk[i] contient les octets
   i*32768 .. min((i+1)*32768, w*h) - 1.

   nBlk = nombre de blocs effectivement utilisés.
   SPR_SPLIT_MAX_BLK = capacité maximale du tableau blk[].

   Taille max gérée : SPR_SPLIT_MAX_BLK * 32768 octets.
   Avec 8 blocs → 262 144 octets (largement de quoi couvrir
   une feuille 320x256 = 81 920 o, ou même 320x800).
   ========================================================= */

#define SPR_SPLIT_BLOCK   32768L
#define SPR_SPLIT_MAX_BLK 8
#define SPR_SPLIT_MAX     (SPR_SPLIT_MAX_BLK * SPR_SPLIT_BLOCK)

typedef struct {
    unsigned char *blk[SPR_SPLIT_MAX_BLK]; /* blocs de données */
    int nBlk;                   /* nombre de blocs utilisés      */
    int w;                      /* largeur en pixels             */
    int h;                      /* hauteur en pixels             */
} SpriteSplit;


/* =========================================================
   SPRITE SIMPLE  (hauteur libre, largeur <= 320 pixels)
   ========================================================= */

/* Charge le fichier .raw (largeur w, hauteur h) en mémoire.
   Remplit spr->data, spr->w, spr->h.
   Retourne SPR_OK ou un code SPR_ERR_*. */
int spriteLoad(Sprite *spr, const char *rawFile, int w, int h);

/* Libère la mémoire allouée par spriteLoad.
   Met spr->data à NULL. Sans effet si déjà NULL. */
void spriteFree(Sprite *spr);

/* Blit opaque : copie le sprite entier dans le backbuffer
   avec le coin supérieur gauche en (dstX, dstY).
   Clipping intégré sur les quatre bords. */
void spriteBlit(const Sprite *spr, int dstX, int dstY);

/* Blit avec colorKey : identique à spriteBlit mais les
   pixels dont l'index == colorKey ne sont pas écrits.
   colorKey < 0 → blit opaque (même comportement que spriteBlit). */
void spriteBlitKey(const Sprite *spr, int dstX, int dstY,
                   int colorKey);

/* Blit d'une zone (rectangle) de la feuille de sprites.
   srcX, srcY   : coin supérieur gauche dans la feuille.
   zoneW, zoneH : dimensions de la zone à extraire.
   dstX, dstY   : position dans le backbuffer.
   Opaque (tous les pixels copiés). */
void spriteBlitZone(const Sprite *spr,
                    int srcX, int srcY, int zoneW, int zoneH,
                    int dstX, int dstY);

/* Idem spriteBlitZone avec colorKey. */
void spriteBlitZoneKey(const Sprite *spr,
                       int srcX, int srcY, int zoneW, int zoneH,
                       int dstX, int dstY,
                       int colorKey);

/* Blit d'une frame d'animation depuis une feuille organisée
   en grille régulière (frameW x frameH, framesPerRow colonnes).
   frameIndex commence à 0 (ligne par ligne). Opaque. */
void spriteBlitFrame(const Sprite *spr, int frameIndex,
                     int framesPerRow, int frameW, int frameH,
                     int dstX, int dstY);

/* Idem spriteBlitFrame avec colorKey. */
void spriteBlitFrameKey(const Sprite *spr, int frameIndex,
                        int framesPerRow, int frameW, int frameH,
                        int dstX, int dstY, int colorKey);


/* =========================================================
   SPRITE SPLIT  (allocation par blocs de taille fixe)
   ========================================================= */

/* Charge le .raw en N blocs de 32 768 octets max chacun.
   Retourne SPR_OK ou un code SPR_ERR_* (SPR_ERR_SIZE si
   w*h > SPR_SPLIT_MAX). */
int spriteLoadSplit(SpriteSplit *spr, const char *rawFile,
                    int w, int h);

/* Libère tous les blocs alloués. Met les pointeurs à NULL. */
void spriteFreeSplit(SpriteSplit *spr);

/* Blit opaque d'un SpriteSplit dans le backbuffer. */
void spriteBlitSplit(const SpriteSplit *spr, int dstX, int dstY);

/* Blit avec colorKey d'un SpriteSplit. */
void spriteBlitSplitKey(const SpriteSplit *spr, int dstX, int dstY,
                        int colorKey);


#endif /* SPRITE_H */
