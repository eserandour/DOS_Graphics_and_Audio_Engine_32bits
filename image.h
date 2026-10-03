#ifndef IMAGE_H
#define IMAGE_H

/* =========================================================
   IMAGE.H -- Chargement one-shot d'images RAW/PAL en mode 13h
   =========================================================
   Environnement : Open Watcom 1.9, DOS
   Mode video    : 13h (320x200, 256 couleurs, 1 octet/pixel)

   PRINCIPE
   --------
   Ce module charge une image depuis le disque et l'affiche
   en une seule passe, sans conserver les donnees en RAM.

   Chaque appel ouvre le fichier, lit les pixels, les copie
   dans le backbuffer, puis ferme le fichier. C'est donc un
   acces disque a chaque appel -- a reserver aux images
   affichees une seule fois (arriere-plan fixe, ecran titre).

   Pour les assets affiches frame apres frame (sprites,
   polices bitmap), utiliser sprite.h qui precharge les
   donnees en memoire et blit sans acces disque.

   FORMATS
   -------
   .pal  768 octets = 256 entrees x 3 composantes R, G, B.
         Chaque composante sur 6 bits (0-63), format natif
         du DAC VGA. Produit par OUTILS/vgatool.py.

   .raw  Pixels bruts, 1 octet par pixel = index palette.
         Stockage ligne par ligne, gauche->droite, haut->bas.
         Produit par OUTILS/vgatool.py.

   CODES DE RETOUR
   ---------------
   IMG_OK       0   succes
   IMG_ERR_PAL  1   impossible d'ouvrir le .pal
   IMG_ERR_RAW  2   impossible d'ouvrir le .raw
   IMG_ERR_READ 3   lecture incomplete (fichier trop court)

   FLUX TYPIQUE
   ------------
   Fond plein ecran 320x200 :
       drawScreen("bg.pal", "bg.raw");
       flip();

   Image plus petite positionnee a (x, y) :
       drawImage("spr.pal", "spr.raw", 64, 64, 128, 68);
       flip();
   ========================================================= */


/* ---------------------------------------------------------
   Codes de retour
   --------------------------------------------------------- */

#define IMG_OK       0   /* succes                          */
#define IMG_ERR_PAL  1   /* impossible d'ouvrir le .pal     */
#define IMG_ERR_RAW  2   /* impossible d'ouvrir le .raw     */
#define IMG_ERR_READ 3   /* lecture incomplete              */


/* ---------------------------------------------------------
   Fonctions
   --------------------------------------------------------- */

/* Charge la palette depuis palFile (-> DAC VGA), puis copie
   le .raw de dimensions srcW x srcH dans le backbuffer,
   coin superieur gauche en (dstX, dstY).
   Le reste du backbuffer n'est pas modifie.
   AUCUN clipping : l'image doit tenir entierement a l'ecran
   (dstX >= 0, dstY >= 0, dstX + srcW <= 320, dstY + srcH <= 200),
   et srcW ne doit pas depasser 320 (buffer de ligne interne).
   Pour un blit clippe, utiliser sprite.h.
   Retourne IMG_OK ou un code IMG_ERR_*. */
int drawImage(const char *palFile, const char *rawFile,
              int srcW, int srcH, int dstX, int dstY);

/* Raccourci pour un fond plein ecran 320x200 en (0, 0).
   Equivalent a drawImage(palFile, rawFile, 320, 200, 0, 0).
   Retourne IMG_OK ou un code IMG_ERR_*. */
int drawScreen(const char *palFile, const char *rawFile);


#endif /* IMAGE_H */
