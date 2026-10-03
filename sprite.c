/* =========================================================
   SPRITE.C — Sprites préchargés en mémoire, mode 13h
   =========================================================
   Environnement : Open Watcom 1.9, DOS
   Mode vidéo    : 13h (320x200, 256 couleurs, 1 octet/pixel)

   Voir sprite.h pour la documentation complète de l'API,
   les conventions colorKey, et les flux d'utilisation.
   ========================================================= */

#include <stdio.h>    /* FILE, fopen, fread, fclose          */
#include <malloc.h>   /* malloc, free                        */
#include <string.h>   /* memcpy                              */
#include "video.h"    /* backbuffer, OFFSET, SCREEN_*        */
#include "sprite.h"

/* =========================================================
   MACROS INTERNES
   ========================================================= */

/* Copie len octets de src vers dst (memcpy standard, plus besoin
   d'une macro dédiée aux pointeurs far en modèle flat). */
#define FCOPY(dst, src, len)  memcpy((dst), (src), (size_t)(len))

/* Adresse d'un pixel (px, py) dans le backbuffer. */
#define BB_PTR(px, py)  (backbuffer + OFFSET((px),(py)))

/* Adresse d'un pixel (px, py) dans les données d'un Sprite.
   Le stride source est spr->w (pas SCREEN_WIDTH). */
#define SPR_PTR(spr, px, py)  ((spr)->data + (long)(py) * (spr)->w + (px))


/* =========================================================
   SPRITE SIMPLE  (hauteur libre, largeur <= 320 pixels)
   ========================================================= */

int spriteLoad(Sprite *spr, const char *rawFile, int w, int h)
{
    FILE *f;
    unsigned char rowBuf[320];
    long size = (long)w * h;
    int row;

    spr->data = NULL;
    spr->w    = w;
    spr->h    = h;

    /* malloc() en modèle flat 32 bits n'impose aucune limite de
       taille de bloc : un sprite ou une feuille, quelle que soit
       sa taille, tient en un seul bloc contigu. */

    spr->data = (unsigned char *)malloc((size_t)size);
    if (!spr->data) return SPR_ERR_MEM;

    f = fopen(rawFile, "rb");
    if (!f) { free(spr->data); spr->data = NULL; return SPR_ERR_FILE; }

    for (row = 0; row < h; row++)
    {
        if (fread(rowBuf, 1, (size_t)w, f) != (size_t)w)
        {
            fclose(f);
            free(spr->data);
            spr->data = NULL;
            return SPR_ERR_READ;
        }
        FCOPY(spr->data + (long)row * w, rowBuf, w);
    }

    fclose(f);
    return SPR_OK;
}

void spriteFree(Sprite *spr)
{
    if (spr->data)
    {
        free(spr->data);
        spr->data = NULL;
    }
}


/* =========================================================
   spriteBlit — blit opaque du sprite entier
   =========================================================
   Copie spr->h lignes de spr->w octets depuis la mémoire
   vers le backbuffer, avec clipping sur les quatre bords.

   Clipping :
     srcX0 / srcY0 : premier pixel source visible (quand le
                     sprite dépasse le bord gauche / haut).
     dstX0 / dstY0 : premier pixel destination dans le bb.
     blitW / blitH : dimensions de la zone effectivement
                     copiée après clipping.
   ========================================================= */

void spriteBlit(const Sprite *spr, int dstX, int dstY)
{
    int srcX0 = 0, srcY0 = 0;
    int dstX0 = dstX, dstY0 = dstY;
    int blitW = spr->w, blitH = spr->h;
    int row;
    unsigned char *src;
    unsigned char *dst;

    /* Clipping gauche. */
    if (dstX0 < 0) { srcX0 -= dstX0; blitW += dstX0; dstX0 = 0; }
    /* Clipping haut. */
    if (dstY0 < 0) { srcY0 -= dstY0; blitH += dstY0; dstY0 = 0; }
    /* Clipping droit. */
    if (dstX0 + blitW > SCREEN_WIDTH)  blitW = SCREEN_WIDTH  - dstX0;
    /* Clipping bas. */
    if (dstY0 + blitH > SCREEN_HEIGHT) blitH = SCREEN_HEIGHT - dstY0;

    /* Sprite entièrement hors écran. */
    if (blitW <= 0 || blitH <= 0) return;

    src = SPR_PTR(spr, srcX0, srcY0);
    dst = BB_PTR(dstX0, dstY0);

    for (row = 0; row < blitH; row++)
    {
        FCOPY(dst, src, blitW);
        src += spr->w;      /* ligne suivante dans la feuille  */
        dst += SCREEN_WIDTH; /* ligne suivante dans le backbuffer */
    }
}


/* =========================================================
   spriteBlitKey — blit avec colorKey
   =========================================================
   Même logique que spriteBlit mais les pixels d'index
   == colorKey ne sont pas écrits. Chaque ligne visible est
   parcourue pixel par pixel (pas de memcpy possible ici).
   colorKey < 0 → bascule en blit opaque (memcpy).       
   ========================================================= */

void spriteBlitKey(const Sprite *spr, int dstX, int dstY,
                   int colorKey)
{
    int srcX0 = 0, srcY0 = 0;
    int dstX0 = dstX, dstY0 = dstY;
    int blitW = spr->w, blitH = spr->h;
    int row, col;
    unsigned char *src;
    unsigned char *dst;
    unsigned char *s;
    unsigned char *d;
    unsigned char ck;

    /* Blit opaque si pas de colorKey. */
    if (colorKey < 0) { spriteBlit(spr, dstX, dstY); return; }

    /* Clipping. */
    if (dstX0 < 0) { srcX0 -= dstX0; blitW += dstX0; dstX0 = 0; }
    if (dstY0 < 0) { srcY0 -= dstY0; blitH += dstY0; dstY0 = 0; }
    if (dstX0 + blitW > SCREEN_WIDTH)  blitW = SCREEN_WIDTH  - dstX0;
    if (dstY0 + blitH > SCREEN_HEIGHT) blitH = SCREEN_HEIGHT - dstY0;
    if (blitW <= 0 || blitH <= 0) return;

    src = SPR_PTR(spr, srcX0, srcY0);
    dst = BB_PTR(dstX0, dstY0);
    ck  = (unsigned char)colorKey;

    for (row = 0; row < blitH; row++)
    {
        s = src;
        d = dst;
        for (col = 0; col < blitW; col++)
        {
            if (*s != ck) *d = *s;
            s++; d++;
        }
        src += spr->w;
        dst += SCREEN_WIDTH;
    }
}


/* =========================================================
   spriteBlitZone — blit d'une zone de la feuille, opaque
   =========================================================
   Extrait le rectangle (srcX, srcY, zoneW, zoneH) depuis
   la feuille et le copie dans le backbuffer en (dstX, dstY).
   Clipping sur les quatre bords du backbuffer.
   Aucun accès disque : tout est déjà en mémoire.
   ========================================================= */

void spriteBlitZone(const Sprite *spr,
                    int srcX, int srcY, int zoneW, int zoneH,
                    int dstX, int dstY)
{
    int dstX0 = dstX, dstY0 = dstY;
    int sx0 = srcX, sy0 = srcY;
    int blitW = zoneW, blitH = zoneH;
    int row;
    unsigned char *src;
    unsigned char *dst;

    /* Clipping gauche. */
    if (dstX0 < 0) { sx0 -= dstX0; blitW += dstX0; dstX0 = 0; }
    /* Clipping haut. */
    if (dstY0 < 0) { sy0 -= dstY0; blitH += dstY0; dstY0 = 0; }
    /* Clipping droit. */
    if (dstX0 + blitW > SCREEN_WIDTH)  blitW = SCREEN_WIDTH  - dstX0;
    /* Clipping bas. */
    if (dstY0 + blitH > SCREEN_HEIGHT) blitH = SCREEN_HEIGHT - dstY0;

    if (blitW <= 0 || blitH <= 0) return;

    /* Pointeur source : pixel (sx0, sy0) dans la feuille.
       Stride source = spr->w (largeur totale de la feuille). */
    src = spr->data + (long)sy0 * spr->w + sx0;
    dst = BB_PTR(dstX0, dstY0);

    for (row = 0; row < blitH; row++)
    {
        FCOPY(dst, src, blitW);
        src += spr->w;
        dst += SCREEN_WIDTH;
    }
}


/* =========================================================
   spriteBlitZoneKey — blit d'une zone avec colorKey
   ========================================================= */

void spriteBlitZoneKey(const Sprite *spr,
                       int srcX, int srcY, int zoneW, int zoneH,
                       int dstX, int dstY,
                       int colorKey)
{
    int dstX0 = dstX, dstY0 = dstY;
    int sx0 = srcX, sy0 = srcY;
    int blitW = zoneW, blitH = zoneH;
    int row, col;
    unsigned char *src;
    unsigned char *dst;
    unsigned char *s;
    unsigned char *d;
    unsigned char ck;

    if (colorKey < 0)
    {
        spriteBlitZone(spr, srcX, srcY, zoneW, zoneH, dstX, dstY);
        return;
    }

    /* Clipping. */
    if (dstX0 < 0) { sx0 -= dstX0; blitW += dstX0; dstX0 = 0; }
    if (dstY0 < 0) { sy0 -= dstY0; blitH += dstY0; dstY0 = 0; }
    if (dstX0 + blitW > SCREEN_WIDTH)  blitW = SCREEN_WIDTH  - dstX0;
    if (dstY0 + blitH > SCREEN_HEIGHT) blitH = SCREEN_HEIGHT - dstY0;
    if (blitW <= 0 || blitH <= 0) return;

    src = spr->data + (long)sy0 * spr->w + sx0;
    dst = BB_PTR(dstX0, dstY0);
    ck  = (unsigned char)colorKey;

    for (row = 0; row < blitH; row++)
    {
        s = src;
        d = dst;
        for (col = 0; col < blitW; col++)
        {
            if (*s != ck) *d = *s;
            s++; d++;
        }
        src += spr->w;
        dst += SCREEN_WIDTH;
    }
}


/* =========================================================
   spriteBlitFrame / spriteBlitFrameKey — animation par feuille
   =========================================================
   Pratique pour les feuilles d'animation organisées en
   grille régulière de frameW x frameH pixels, framesPerRow
   colonnes par ligne. frameIndex commence à 0.

   col = frameIndex % framesPerRow
   row = frameIndex / framesPerRow
   srcX = col * frameW, srcY = row * frameH

   Simple enrobage autour de spriteBlitZone(Key), sans
   accès disque ni calcul supplémentaire côté appelant.
   ========================================================= */

void spriteBlitFrame(const Sprite *spr, int frameIndex,
                     int framesPerRow, int frameW, int frameH,
                     int dstX, int dstY)
{
    int col = frameIndex % framesPerRow;
    int row = frameIndex / framesPerRow;

    spriteBlitZone(spr, col * frameW, row * frameH, frameW, frameH,
                   dstX, dstY);
}

void spriteBlitFrameKey(const Sprite *spr, int frameIndex,
                        int framesPerRow, int frameW, int frameH,
                        int dstX, int dstY, int colorKey)
{
    int col = frameIndex % framesPerRow;
    int row = frameIndex / framesPerRow;

    spriteBlitZoneKey(spr, col * frameW, row * frameH, frameW, frameH,
                      dstX, dstY, colorKey);
}


/* =========================================================
   SPRITE SPLIT  (allocation par blocs de taille fixe)
   =========================================================
   Split générique en N blocs de SPR_SPLIT_BLOCK (32768)
   octets max chacun (capacité totale SPR_SPLIT_MAX) : une
   alternative à spriteLoad() pour qui préfère répartir les
   données sur plusieurs allocations bornées plutôt qu'un
   unique bloc contigu. Le dernier bloc peut être plus petit
   (taille restante).

   blk[i] contient les octets i*32768 .. min((i+1)*32768,
   w*h) - 1. nBlk = nombre de blocs réellement alloués.

   Les blits reconstituent chaque ligne en copiant, bloc par
   bloc, les portions qui la recouvrent (une ligne peut être
   à cheval sur deux blocs au maximum puisque w <= 320 <
   32768).
   ========================================================= */

/* Copie 'len' octets de la zone logique [offset, offset+len)
   du SpriteSplit vers dst (rowBuf). Une ligne (w <= 320)
   ne peut chevaucher qu'au plus deux blocs consécutifs,
   puisque chaque bloc fait au moins 320 octets. */
static void splitCopyOut(const SpriteSplit *spr, long offset,
                         int len, unsigned char *dst)
{
    int blk      = (int)(offset / SPR_SPLIT_BLOCK);
    long blkOff  = offset - (long)blk * SPR_SPLIT_BLOCK;
    long avail   = SPR_SPLIT_BLOCK - blkOff;
    int chunk0;

    if ((long)len <= avail)
    {
        /* Entièrement dans blk. */
        FCOPY(dst, spr->blk[blk] + blkOff, len);
    }
    else
    {
        /* À cheval entre blk et blk+1. */
        chunk0 = (int)avail;
        FCOPY(dst,          spr->blk[blk]     + blkOff, chunk0);
        FCOPY(dst + chunk0, spr->blk[blk + 1],           len - chunk0);
    }
}

int spriteLoadSplit(SpriteSplit *spr, const char *rawFile,
                    int w, int h)
{
    FILE *f;
    unsigned char rowBuf[320];
    long size = (long)w * h;
    long remaining;
    long written = 0;
    int i, row;
    int blk;
    long blkOff, avail;
    int chunk0;

    spr->w = w;
    spr->h = h;
    spr->nBlk = 0;
    for (i = 0; i < SPR_SPLIT_MAX_BLK; i++) spr->blk[i] = NULL;

    if (size > SPR_SPLIT_MAX) return SPR_ERR_SIZE;

    /* Calcule le nombre de blocs nécessaires et les alloue. */
    spr->nBlk = (int)((size + SPR_SPLIT_BLOCK - 1) / SPR_SPLIT_BLOCK);
    if (spr->nBlk < 1) spr->nBlk = 1;

    remaining = size;
    for (i = 0; i < spr->nBlk; i++)
    {
        long blkSize = (remaining < SPR_SPLIT_BLOCK) ? remaining : SPR_SPLIT_BLOCK;
        spr->blk[i] = (unsigned char *)malloc((size_t)blkSize);
        if (!spr->blk[i])
        {
            spriteFreeSplit(spr);
            return SPR_ERR_MEM;
        }
        remaining -= blkSize;
    }

    f = fopen(rawFile, "rb");
    if (!f) { spriteFreeSplit(spr); return SPR_ERR_FILE; }

    for (row = 0; row < h; row++)
    {
        if (fread(rowBuf, 1, (size_t)w, f) != (size_t)w)
        {
            fclose(f);
            spriteFreeSplit(spr);
            return SPR_ERR_READ;
        }

        /* Écrit la ligne (w octets) à l'offset logique 'written',
           répartie sur un ou deux blocs consécutifs. */
        blk    = (int)(written / SPR_SPLIT_BLOCK);
        blkOff = written - (long)blk * SPR_SPLIT_BLOCK;
        avail  = SPR_SPLIT_BLOCK - blkOff;

        if ((long)w <= avail)
        {
            FCOPY(spr->blk[blk] + blkOff, rowBuf, w);
        }
        else
        {
            chunk0 = (int)avail;
            FCOPY(spr->blk[blk]     + blkOff, rowBuf,          chunk0);
            FCOPY(spr->blk[blk + 1],          rowBuf + chunk0, w - chunk0);
        }

        written += w;
    }

    fclose(f);
    return SPR_OK;
}

void spriteFreeSplit(SpriteSplit *spr)
{
    int i;
    for (i = 0; i < SPR_SPLIT_MAX_BLK; i++)
    {
        if (spr->blk[i]) { free(spr->blk[i]); spr->blk[i] = NULL; }
    }
    spr->nBlk = 0;
}


/* =========================================================
   spriteBlitSplit — blit opaque d'un SpriteSplit
   =========================================================
   Pour chaque ligne de la zone blittée, splitCopyOut()
   reconstitue la ligne (1 ou 2 blocs) dans rowBuf, qui est
   ensuite copié vers le backbuffer.
   ========================================================= */

void spriteBlitSplit(const SpriteSplit *spr, int dstX, int dstY)
{
    int srcX0 = 0, srcY0 = 0;
    int dstX0 = dstX, dstY0 = dstY;
    int blitW = spr->w, blitH = spr->h;
    unsigned char rowBuf[320];
    unsigned char *dst;
    int row;
    long offset;

    /* Clipping gauche. */
    if (dstX0 < 0) { srcX0 -= dstX0; blitW += dstX0; dstX0 = 0; }
    /* Clipping haut. */
    if (dstY0 < 0) { srcY0 -= dstY0; blitH += dstY0; dstY0 = 0; }
    /* Clipping droit. */
    if (dstX0 + blitW > SCREEN_WIDTH)  blitW = SCREEN_WIDTH  - dstX0;
    /* Clipping bas. */
    if (dstY0 + blitH > SCREEN_HEIGHT) blitH = SCREEN_HEIGHT - dstY0;

    if (blitW <= 0 || blitH <= 0) return;

    dst = BB_PTR(dstX0, dstY0);

    for (row = 0; row < blitH; row++)
    {
        offset = (long)(srcY0 + row) * spr->w + srcX0;
        splitCopyOut(spr, offset, blitW, rowBuf);
        FCOPY(dst, rowBuf, blitW);
        dst += SCREEN_WIDTH;
    }
}


/* =========================================================
   spriteBlitSplitKey — blit avec colorKey d'un SpriteSplit
   ========================================================= */

void spriteBlitSplitKey(const SpriteSplit *spr, int dstX, int dstY,
                        int colorKey)
{
    int srcX0 = 0, srcY0 = 0;
    int dstX0 = dstX, dstY0 = dstY;
    int blitW = spr->w, blitH = spr->h;
    unsigned char rowBuf[320];
    unsigned char *dst;
    unsigned char *d;
    unsigned char *s;
    int row, col;
    long offset;
    unsigned char ck;

    if (colorKey < 0) { spriteBlitSplit(spr, dstX, dstY); return; }

    /* Clipping gauche. */
    if (dstX0 < 0) { srcX0 -= dstX0; blitW += dstX0; dstX0 = 0; }
    /* Clipping haut. */
    if (dstY0 < 0) { srcY0 -= dstY0; blitH += dstY0; dstY0 = 0; }
    /* Clipping droit. */
    if (dstX0 + blitW > SCREEN_WIDTH)  blitW = SCREEN_WIDTH  - dstX0;
    /* Clipping bas. */
    if (dstY0 + blitH > SCREEN_HEIGHT) blitH = SCREEN_HEIGHT - dstY0;
    if (blitW <= 0 || blitH <= 0) return;

    dst = BB_PTR(dstX0, dstY0);
    ck  = (unsigned char)colorKey;

    for (row = 0; row < blitH; row++)
    {
        offset = (long)(srcY0 + row) * spr->w + srcX0;
        splitCopyOut(spr, offset, blitW, rowBuf);

        s = rowBuf;
        d = dst;
        for (col = 0; col < blitW; col++)
        {
            if (*s != ck) *d = *s;
            s++; d++;
        }

        dst += SCREEN_WIDTH;
    }
}
