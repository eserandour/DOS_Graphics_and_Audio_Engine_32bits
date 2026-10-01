/* =========================================================
   SCENE5.C — Scrolling horizontal puis scrolling vertical
   =========================================================
   Environnement : Open Watcom 1.9, DOS
   Mode video    : 13h (320x200, 256 couleurs)

   La scène se déroule en deux parties enchaînées :

   PARTIE A — Scroller horizontal (credits)
   -----------------------------------------
   Texte long qui défile de droite à gauche, centré
   verticalement, encadré de deux barres horizontales.
   Algorithme colonne par colonne :
     posX      = scrollX + c
     charIdx   = (posX / char_w) % textLen
     pixInChar = posX % char_w
     -> blit de la colonne pixInChar du glyphe charIdx
   Fin : après un passage complet du ruban.

   PARTIE B — Scroller vertical (Star Wars / générique)
   ------------------------------------------------------
   Plusieurs lignes de texte défilent du bas vers le haut.
   La fenêtre de rendu est centrée horizontalement.
   Algorithme ligne par ligne :
     posY      = scrollY + r             (r = ligne écran)
     lineIdx   = posY / char_h           (index de ligne)
     pixInLine = posY % char_h           (pixel dans le glyphe)
     -> blit de la ligne pixInLine de la chaîne lineIdx
   scrollY avance d'un pixel à la fois (VSCROLL_SPEED_MS).
   Fin : quand la dernière ligne a entièrement quitté l'écran.

   TRANSITION A→B
   --------------
   Fondu sortant (fade-out) en FADE_MS ms, puis clearScreen
   et démarrage de la partie B.

   NOTE C89 (Open Watcom)
   ----------------------
   Toutes les déclarations en tête de bloc ou au niveau fichier.

   TIMER — scène basée sur scene_template.c (voir ce fichier pour
   les règles) : tout le minutage est en ticks (70 Hz).
   Durée : déduite des constantes (PART_A + TRANSITION + PART_B
   ticks). Les positions des scrollers dépendent de elapsed.
   ========================================================= */





#include "video.h"
#include "palette.h"
#include "timer.h"
#include "graphics.h"
#include "font2.h"
#include "scene.h"
#include "app.h"
#include <conio.h>   /* outp */





/* =========================================================
   TEXTES
   ========================================================= */

/* Scroller horizontal */
#define HSCROLL_TEXT \
    "     DEMO DOS     MODE 13H     OPEN WATCOM 1.9     "

/* Lignes du générique vertical.
   Chaîne unique, lignes séparées par '\n'.
   Une chaîne vide ("") insère une ligne blanche. */
static const char *vlines[] = {
    "",
    "DEMO DOS",
    "",
    "MODE 13H",
    "",
    "OPEN WATCOM 1.9",
    "",
};
#define VLINES_COUNT  ((int)(sizeof(vlines) / sizeof(vlines[0])))





/* ---------------------------------------------------------
   Réglages de la scène
   --------------------------------------------------------- */
/* Police : FONT2_DESC_16X16_F2 (cellules 16x16). Ces deux valeurs
   servent à calculer la durée de la scène à la compilation ; Init
   vérifie qu'elles correspondent à la police réellement chargée. */
#define FONT_W   16UL
#define FONT_H   16UL

/* Débit des scrollers : 1 pixel tous les N ticks (débit constant,
   sans division entière sur des ms).
   2 ticks = 35 px/s (lent, lisible) ; 1 tick = 70 px/s (rapide).
   VSCROLL_TICKS remplace l'ancien VSCROLL_SPEED_MS = 30 ms/pixel
   (2,1 ticks, que la division entière ramenait de fait à 2). */
#define HSCROLL_TICKS    2UL
#define VSCROLL_TICKS    2UL
#define SCROLL_BG_COLOR   0
#define SCROLL_BAR_COLOR  8
#define SCROLL_BAR_H      2

/* Transition A -> B : fondu sortant */
#define TRANSITION_MS    600UL
#define TRANSITION_TICKS MS_TO_TICKS(TRANSITION_MS)

/* Partie B — vertical */
#define VSCROLL_BG_COLOR  0
#define VSCROLL_WIN_W   240      /* largeur de la fenêtre texte (px) */

/* Durée de la scène : déduite des constantes ci-dessus.
   A : un passage complet du ruban ; B : de "sous l'écran" jusqu'à
   ce que la dernière ligne ait quitté le haut. */
#define HSCROLL_LEN   ((unsigned long)(sizeof(HSCROLL_TEXT) - 1))
#define PART_A_TICKS  (HSCROLL_LEN * FONT_W * HSCROLL_TICKS)
#define PART_B_TICKS  (((unsigned long)SCREEN_HEIGHT + \
                        (unsigned long)VLINES_COUNT * FONT_H) * VSCROLL_TICKS)
#define SCENE_TICKS   (PART_A_TICKS + TRANSITION_TICKS + PART_B_TICKS)

/* Le gabarit raisonne en ms : conversion aller-retour sans perte
   (l'erreur de troncature est < 1 ms, soit < 0,07 tick). */
#define SCENE_MS      ((SCENE_TICKS) * 1000UL / TARGET_HZ)
#define FADE_IN_MS      0UL   /* intro/outro : voir PART_A, TRANSITION, PART_B */
#define FADE_OUT_MS     0UL

/* 1 tick : le fondu sort au rythme du timer ; le scrolling, lui, ne
   redessine que lorsque sa position change (tous les N ticks). */
#define FRAME_TICKS     1UL

/* ms -> ticks, arrondi ; minimum 1 tick, sauf 0 ms qui reste 0 tick
   (= phase supprimée, ex. FADE_IN_MS 0UL). Ne pas modifier. */
#define MS_TO_TICKS(ms) \
    ((ms) == 0UL ? 0UL : \
     ((((ms) * TARGET_HZ + 500UL) / 1000UL) ? (((ms) * TARGET_HZ + 500UL) / 1000UL) : 1UL))





/* ---------------------------------------------------------
   Variables propres à la scène
   --------------------------------------------------------- */
static int           partB       = 0;      /* 0 = partie A (+ fondu), 1 = partie B */

/* --- Partie A --- */
static Font2Desc     hFont       = FONT2_DESC_16X16_F2;
static long          scrollX     = 0;
static long          lastScrollX = -1L;    /* dernière position dessinée */
static long          rubanW      = 0;
static int           hTextLen    = 0;
static int           textY       = 0;      /* Y de la ligne de texte H */

/* --- Partie B --- */
static Font2Desc     vFont       = FONT2_DESC_16X16_F2;
static long          scrollY     = 0;      /* pixels déjà remontés */
static long          lastScrollY = 0;      /* dernière position dessinée */
static long          rubanH      = 0;      /* hauteur totale du ruban */
static int           vWinX       = 0;      /* X gauche de la fenêtre  */





/* =========================================================
   UTILITAIRES
   ========================================================= */
static int f2len(const char *s) { int n = 0; while (s[n]) n++; return n; }

/* =========================================================
   FADE-OUT EN VIRGULE FIXE
   =========================================================
   facteur : 64 = pleine luminosité, 0 = noir.
   Directement sur le DAC, sans modifier workingPalette.
   ========================================================= */
static void fadePaletteInt5(Color *pal, unsigned int facteur)
{
    int i;
    waitVRetrace();
    outp(0x3C8, 0);
    for (i = 0; i < 256; i++)
    {
        outp(0x3C9, (unsigned char)((pal[i].r * facteur) >> 6));
        outp(0x3C9, (unsigned char)((pal[i].g * facteur) >> 6));
        outp(0x3C9, (unsigned char)((pal[i].b * facteur) >> 6));
    }
}

/* =========================================================
   PARTIE A — BLIT D'UNE COLONNE (scroll horizontal)
   ========================================================= */
static void blitColumn(int screenCol)
{
    long posX;
    int  charIdx, pixInChar;
    unsigned char c;
    int  glyphIdx, glyphCol, glyphRow, srcX, srcY, row;
    unsigned char *dst;
    unsigned char pix;
    unsigned char ck;

    posX      = scrollX + (long)screenCol;
    charIdx   = (int)((posX / hFont.char_w) % hTextLen);
    pixInChar = (int)(posX % hFont.char_w);

    c   = (unsigned char)HSCROLL_TEXT[charIdx];
    dst = backbuffer + OFFSET(screenCol, textY);
    ck  = (unsigned char)hFont.colorKey;

    if (c < (unsigned char)hFont.first_char ||
        c > (unsigned char)hFont.last_char)
    {
        for (row = 0; row < hFont.char_h; row++)
        {
            *dst = SCROLL_BG_COLOR;
            dst += SCREEN_WIDTH;
        }
        return;
    }

    glyphIdx = c - (unsigned char)hFont.first_char;
    glyphCol = glyphIdx % hFont.cols;
    glyphRow = glyphIdx / hFont.cols;
    srcX     = glyphCol * hFont.char_w + pixInChar;
    srcY     = glyphRow * hFont.char_h;

    for (row = 0; row < hFont.char_h; row++)
    {
        pix  = font2GetPixel(&hFont, srcX, srcY + row);
        *dst = (pix == ck) ? SCROLL_BG_COLOR : pix;
        dst += SCREEN_WIDTH;
    }
}

/* =========================================================
   PARTIE B — BLIT D'UNE LIGNE (scroll vertical)
   =========================================================
   Pour chaque ligne écran r (0..SCREEN_HEIGHT-1) :
     posY      = scrollY + r         (position dans le ruban)
     lineIdx   = posY / char_h       (indice de ligne de texte)
     pixInLine = posY % char_h       (pixel vertical dans le glyphe)

   Pour chaque colonne c de la fenêtre (vWinX..vWinX+VSCROLL_WIN_W-1) :
     - identifier le glyphe à la position c dans la ligne lineIdx
     - lire le pixel (pixInCol, pixInLine) de ce glyphe
     - écrire dans le backbuffer ou fond si transparent
   ========================================================= */
static void blitVLine(int screenRow)
{
    long posY;
    int  lineIdx, pixInLine;
    const char *line;
    int  lineLen;
    int  col;
    unsigned char ck;
    unsigned char *dst;

    posY      = scrollY + (long)screenRow;
    lineIdx   = (int)(posY / vFont.char_h);
    pixInLine = (int)(posY % vFont.char_h);

    if (lineIdx < 0 || lineIdx >= VLINES_COUNT)
    {
        /* Hors ruban : ligne de fond. */
        dst = backbuffer + OFFSET(vWinX, screenRow);
        {
            int c;
            for (c = 0; c < VSCROLL_WIN_W; c++)
            {
                *dst = VSCROLL_BG_COLOR;
                dst++;
            }
        }
        return;
    }

    line    = vlines[lineIdx];
    lineLen = f2len(line);
    ck      = (unsigned char)vFont.colorKey;

    /* Centrage de la chaîne dans la fenêtre. */
    {
        int textPxW  = lineLen * vFont.char_w;
        int textXoff = (VSCROLL_WIN_W - textPxW) / 2;  /* peut être négatif */
        int winCol;

        dst = backbuffer + OFFSET(vWinX, screenRow);

        for (winCol = 0; winCol < VSCROLL_WIN_W; winCol++)
        {
            int charPx  = winCol - textXoff;  /* position dans le texte (px) */
            unsigned char pix = VSCROLL_BG_COLOR;

            if (charPx >= 0 && charPx < textPxW)
            {
                int charIdx  = charPx / vFont.char_w;
                int pixInChr = charPx % vFont.char_w;
                unsigned char c = (unsigned char)line[charIdx];

                if (c >= (unsigned char)vFont.first_char &&
                    c <= (unsigned char)vFont.last_char)
                {
                    int gi   = c - (unsigned char)vFont.first_char;
                    int gCol = gi % vFont.cols;
                    int gRow = gi / vFont.cols;
                    int sx   = gCol * vFont.char_w + pixInChr;
                    int sy   = gRow * vFont.char_h + pixInLine;
                    unsigned char raw = font2GetPixel(&vFont, sx, sy);
                    if (raw != ck)
                        pix = raw;
                }
            }

            *dst = pix;
            dst++;
        }
    }
}

/* =========================================================
   BARRES HORIZONTALES de la partie A
   ========================================================= */
static void drawScrollBars(void)
{
    drawRectFill(0, textY - SCROLL_BAR_H - 1,
                 SCREEN_WIDTH - 1, textY - 1,
                 SCROLL_BAR_COLOR);
    drawRectFill(0, textY + hFont.char_h,
                 SCREEN_WIDTH - 1,
                 textY + hFont.char_h + SCROLL_BAR_H,
                 SCROLL_BAR_COLOR);
}





/* =========================================================
   INIT — appelée UNE fois au lancement de la scène
   ========================================================= */
static void scene5Init(void)
{
    int err;

    partB       = 0;
    lastScrollX = -1L;

    err = loadPalette("font2\\16X16_F2.pal");
    if (err != PAL_OK) { quitRequested = 1; return; }

    /* Charger la police horizontale. */
    if (!font2Load(&hFont)) { quitRequested = 1; return; }

    /* FONT_W / FONT_H fixent la durée de la scène : s'ils ne
       correspondent pas à la police, on arrête plutôt que de
       jouer une scène trop courte ou trop longue. */
    if ((unsigned long)hFont.char_w != FONT_W ||
        (unsigned long)hFont.char_h != FONT_H)
    {
        font2Free(&hFont);
        quitRequested = 1;
        return;
    }

    hTextLen = f2len(HSCROLL_TEXT);
    textY    = (SCREEN_HEIGHT - hFont.char_h) / 2;
    rubanW   = (long)hTextLen * hFont.char_w;
    scrollX  = 0;

    clearScreen(SCROLL_BG_COLOR);
    drawScrollBars();
    flip();
}





/* =========================================================
   RENDER — appelée à chaque image (cadence FRAME_TICKS)
   ---------------------------------------------------------
   phase    : 0 = intro, 1 = corps, 2 = outro   (phases du gabarit)
   progress : avancement DANS cette phase, de 0 à 1000
   elapsed  : ticks écoulés depuis le début de la scène
   ========================================================= */
static void scene5Render(unsigned long elapsed, int phase, unsigned long progress)
{
    unsigned long fadeElapsed;
    unsigned int  facteur;
    long          pos;
    int           i;

    (void)phase; (void)progress;            /* parties A / B gérées ci-dessous */

    /* =======================================================
       PARTIE A — scroll horizontal
       La position est une fonction du temps écoulé : 1 pixel
       tous les HSCROLL_TICKS ticks, sans accumulation d'erreur
       ni rattrapage plafonné.
       ======================================================= */
    if (elapsed < PART_A_TICKS)
    {
        pos = (long)(elapsed / HSCROLL_TICKS);
        if (pos == lastScrollX) return;     /* rien de nouveau à dessiner */
        lastScrollX = pos;
        scrollX     = pos;

        for (i = 0; i < SCREEN_WIDTH; i++)
            blitColumn(i);

        drawScrollBars();
        flip();
        return;
    }

    /* =======================================================
       TRANSITION A -> B — fondu sortant (facteur 64 -> 0)
       ======================================================= */
    if (elapsed < PART_A_TICKS + TRANSITION_TICKS)
    {
        fadeElapsed = elapsed - PART_A_TICKS;
        facteur = (unsigned int)((TRANSITION_TICKS - fadeElapsed) * 64UL
                                 / TRANSITION_TICKS);
        fadePaletteInt5(workingPalette, facteur);
        return;
    }

    /* =======================================================
       PARTIE B — scroll vertical
       ======================================================= */
    if (!partB)
    {
        /* Premier passage : fondu terminé, on initialise la partie B. */
        fadePaletteInt5(workingPalette, 0);

        font2Free(&hFont);

        /* La police verticale est la même feuille. */
        vFont.sheet = 0;
        if (!font2Load(&vFont)) { quitRequested = 1; return; }

        rubanH  = (long)VLINES_COUNT * vFont.char_h;
        /* scrollY démarre négatif : le texte entre par le bas.
           scrollY = -(SCREEN_HEIGHT) place la première ligne
           juste sous l'écran. */
        scrollY     = -(long)SCREEN_HEIGHT;
        lastScrollY = scrollY - 1L;         /* force le 1er dessin */
        vWinX       = (SCREEN_WIDTH - VSCROLL_WIN_W) / 2;

        /* Écran noir d'abord, puis palette à pleine luminosité
           (évite de rallumer l'ancienne image un instant). */
        clearScreen(VSCROLL_BG_COLOR);
        flip();
        setPalette(workingPalette);

        partB = 1;
    }

    pos = -(long)SCREEN_HEIGHT
          + (long)((elapsed - PART_A_TICKS - TRANSITION_TICKS) / VSCROLL_TICKS);
    if (pos == lastScrollY) return;         /* rien de nouveau à dessiner */
    lastScrollY = pos;
    scrollY     = pos;

    /* Rendu : effacer les marges latérales, puis blit ligne par ligne. */
    if (vWinX > 0)
    {
        drawRectFill(0, 0, vWinX - 1, SCREEN_HEIGHT - 1,
                     VSCROLL_BG_COLOR);
        drawRectFill(vWinX + VSCROLL_WIN_W, 0,
                     SCREEN_WIDTH - 1, SCREEN_HEIGHT - 1,
                     VSCROLL_BG_COLOR);
    }

    for (i = 0; i < SCREEN_HEIGHT; i++)
        blitVLine(i);

    flip();
}





/* =========================================================
   CLEANUP — appelée UNE fois à la fin de la scène
   ========================================================= */
static void scene5Cleanup(void)
{
    if (partB) font2Free(&vFont);
    else       font2Free(&hFont);
}





/* =========================================================
   POINT D'ENTRÉE — gestion du timer (NE PAS MODIFIER)
   ========================================================= */
void scene5(void)
{
    static int           initialized = 0;
    static unsigned long lastFrame   = 0UL;

    const unsigned long sceneTicks = MS_TO_TICKS(SCENE_MS);
    const unsigned long inTicks    = MS_TO_TICKS(FADE_IN_MS);
    const unsigned long outTicks   = MS_TO_TICKS(FADE_OUT_MS);

    unsigned long now, elapsed, progress;
    int phase;

    now = readTimer();

    /* 1. Initialisation : une seule fois par lancement */
    if (!initialized)
    {
        initialized = 1;
        sceneStart  = now;
        lastFrame   = now;
        scene5Init();
        return;                     /* le 1er rendu se fera au tour suivant */
    }

    elapsed = elapsedTime(sceneStart, now);

    /* 2. Fin de scène : test AVANT le rendu */
    if (elapsed >= sceneTicks)
    {
        scene5Cleanup();
        initialized = 0;            /* état remis à zéro d'abord... */
        sceneSignalEnd();           /* ...puis on rend la main      */
        return;                     /* et on ne touche plus à rien  */
    }

    /* 3. Limitation de cadence */
    if (elapsedTime(lastFrame, now) < FRAME_TICKS)
        return;

    lastFrame += FRAME_TICKS;       /* pas fixe : pas de dérive */
    if (elapsedTime(lastFrame, now) >= FRAME_TICKS)
        lastFrame = now;            /* trop de retard : on abandonne le rattrapage */

    /* 4. Phase courante et progression dans la phase (0..1000) */
    if (inTicks > 0UL && elapsed < inTicks)
    {
        phase    = 0;
        progress = elapsed * 1000UL / inTicks;
    }
    else if (outTicks > 0UL && elapsed >= sceneTicks - outTicks)
    {
        phase    = 2;
        progress = (elapsed - (sceneTicks - outTicks)) * 1000UL / outTicks;
    }
    else
    {
        phase    = 1;
        progress = (elapsed - inTicks) * 1000UL / (sceneTicks - inTicks - outTicks);
    }

    /* 5. Rendu */
    scene5Render(elapsed, phase, progress);
}
