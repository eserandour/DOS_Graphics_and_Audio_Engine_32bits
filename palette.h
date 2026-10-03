#ifndef PALETTE_H
#define PALETTE_H

/* =========================================================
   PALETTE.H — Gestion de la palette VGA 256 couleurs
   =========================================================
   En mode 13h, chaque pixel est un index (0-255) dans une
   table de 256 couleurs appelée palette. La palette est
   stockée dans le DAC (Digital-to-Analog Converter) VGA.

   Chaque couleur du DAC est définie par trois composantes
   R, G, B sur 6 bits (valeurs 0 à 63, et non 0 à 255
   comme en RGB moderne). Pour convertir : val_6bits * 4.

   Changer la palette ne nécessite pas de redessiner l'écran
   ce qui permet des effets (fade, cycle) très efficaces.
   ========================================================= */

/* ---------------------------------------------------------
   Structure Color
   --------------------------------------------------------- */

/* Représente une couleur VGA.
   Valeurs sur 6 bits : 0 (noir) à 63 (intensité maximale).
   Ne pas dépasser 63 : le DAC ignore les bits supérieurs. */
typedef struct {
    unsigned char r;   /* composante rouge  (0-63) */
    unsigned char g;   /* composante verte  (0-63) */
    unsigned char b;   /* composante bleue  (0-63) */
} Color;

/* ---------------------------------------------------------
   Palettes globales
   --------------------------------------------------------- */

/* Palette lue au démarrage : état initial du DAC VGA.
   Sert de référence pour restaurer les couleurs d'origine. */
extern Color defaultPalette[256];

/* Palette de travail : celle qu'on manipule (fade, cycle).
   C'est cette palette qu'on envoie au DAC avec setPalette. */
extern Color workingPalette[256];

/* Palettes temporaires pour les interpolations (lerp).
   paletteA = palette de départ, paletteB = palette d'arrivée. */
extern Color paletteA[256];
extern Color paletteB[256];

/* Palette dégradé noir → blanc (niveaux de gris). */
extern Color grayPalette[256];

/* Palette rouge : index 0 = noir, index 1-255 = rouge max
   avec vert et bleu croissants (rouge → blanc). */
extern Color redPalette[256];

/* Palette bleue : index 0 = noir, index 1-255 = bleu max
   avec rouge et vert croissants (bleu → blanc). */
extern Color bluePalette[256];

/* Palette verte : index 0 = noir, index 1-255 = vert max
   avec rouge et bleu croissants (vert → blanc). */
extern Color greenPalette[256];

/* Palette cercle chromatique HSV :
   index 0 = noir, index 1-255 = teinte tournante sur 360°
   (rouge → jaune → vert → cyan → bleu → magenta → rouge).
   Utile pour les effets de cycle de palette continus. */
extern Color rainbowPalette[256];

/* ---------------------------------------------------------
   Codes de retour de loadPalette
   --------------------------------------------------------- */

#define PAL_OK        0   /* succès                        */
#define PAL_ERR_FILE  1   /* impossible d'ouvrir le .pal   */
#define PAL_ERR_READ  2   /* lecture incomplète            */

/* ---------------------------------------------------------
   Fonctions — Fichier .pal
   --------------------------------------------------------- */

/* Charge un fichier .pal (768 octets : 256 × R/G/B sur 6 bits)
   dans workingPalette et envoie immédiatement la palette
   au DAC VGA.
   Ne touche pas au backbuffer.
   Retourne PAL_OK, PAL_ERR_FILE ou PAL_ERR_READ. */
int loadPalette(const char *palFile);

/* Sauvegarde une palette dans un fichier .pal (768 octets bruts :
   256 triplets R/G/B sur 6 bits, même format que les .pal du projet).
   Retourne 1 si succès, 0 si échec (fopen ou fwrite). */
int savePalette(const Color *pal, const char *filename);

/* ---------------------------------------------------------
   Fonctions — Accès matériel DAC
   --------------------------------------------------------- */

/* Définit une seule couleur dans le DAC VGA.
   index : numéro de la couleur (0-255)
   r, g, b : composantes sur 6 bits (0-63). */
void setPaletteColor(unsigned char index,
                     unsigned char r, unsigned char g, unsigned char b);

/* Envoie les 256 couleurs d'une palette vers le DAC VGA.
   Attend le retrace vertical avant d'écrire pour éviter
   le palette tearing (bandes de couleurs parasites). */
void setPalette(Color *pal);

/* Lit les 256 couleurs actuelles du DAC dans un tableau.
   Utile pour sauvegarder la palette par défaut au démarrage. */
void getPalette(Color *pal);

/* ---------------------------------------------------------
   Fonctions — Manipulation de palette
   --------------------------------------------------------- */

/* Copie src vers dest (256 * sizeof(Color) octets). */
void copyPalette(Color *dest, Color *src);

/* Interpolation linéaire entre palA et palB.
   t = 0.0 → résultat = palA
   t = 1.0 → résultat = palB
   t = 0.5 → mélange à 50%
   Formule : dest[i] = palA[i] + t * (palB[i] - palA[i]) */
void lerpPalette(Color *dest, Color *palA, Color *palB, float t);

/* Applique un facteur de luminosité à une palette et
   l'envoie directement au DAC (sans modifier pal en RAM).
   t = 0.0 → tout noir (fade out complet)
   t = 1.0 → palette originale (fade in complet)
   Utile pour les effets de fondu enchaîné. */
void fadePalette(Color *pal, float t);

/* Fait tourner les couleurs d'un cran vers la gauche dans
   l'intervalle [start, end] : chaque entrée reçoit la valeur
   de son voisin de droite, et end reçoit l'ancienne valeur de
   start (rotation : aucune couleur n'est perdue).
   Envoie ensuite la palette au DAC via setPalette(), qui attend
   le retrace vertical (jusqu'à ~14 ms).
   Crée un effet de défilement des couleurs vers la gauche. */
void cyclePaletteLeft(Color *pal, int start, int end);

/* Décale toutes les couleurs d'un cran vers la droite dans
   l'intervalle [start, end]. La couleur end est sauvegardée,
   chaque entrée reçoit la valeur de son voisin de gauche, puis
   la sauvegarde est replacée en start (rotation).
   Envoie ensuite la palette au DAC via setPalette(), qui attend
   le retrace vertical (jusqu'à ~14 ms).
   Crée un effet de défilement des couleurs vers la droite. */
void cyclePaletteRight(Color *pal, int start, int end);

/* ---------------------------------------------------------
   Fonctions — Générateurs de palette
   --------------------------------------------------------- */

/* Génère un dégradé linéaire du noir au blanc.
   index 0 → r=g=b=0 (noir)
   index 255 → r=g=b=63 (blanc)
   Conversion : val_6bits = index / 4 (décalage de 2 bits). */
void buildGrayPalette(Color *pal);

/* Génère une palette rouge :
   index 0   → noir (r=g=b=0)
   index 1-255 → rouge fixe à 63, vert et bleu croissants
   de 0 à 63. L'effet va du rouge pur vers le blanc. */
void buildRedPalette(Color *pal);

/* Génère une palette bleue :
   index 0   → noir (r=g=b=0)
   index 1-255 → bleu fixe à 63, rouge et vert croissants
   de 0 à 63. L'effet va du bleu pur vers le blanc. */
void buildBluePalette(Color *pal);

/* Génère une palette verte :
   index 0   → noir (r=g=b=0)
   index 1-255 → vert fixe à 63, rouge et bleu croissants
   de 0 à 63. L'effet va du vert pur vers le blanc. */
void buildGreenPalette(Color *pal);

/* Génère un cercle chromatique HSV complet :
   index 0   → noir (r=g=b=0)
   index 1-255 → teinte sur 360° à saturation et valeur max.
   Un décalage de 128 dans l'index donne une teinte décalée
   d'environ 180° (128/255 x 360 = 180,7°), soit quasiment la
   complémentaire, utile pour les effets de contraste. */
void buildRainbowPalette(Color *pal);

#endif /* PALETTE_H */
