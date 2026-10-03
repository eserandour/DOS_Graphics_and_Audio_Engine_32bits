/* =========================================================
   VIDEO.C — Mode 13h, backbuffer, curseur
   ========================================================= */

#include <malloc.h>   /* malloc, free                */
#include <string.h>   /* memset, memcpy               */
#include <conio.h>    /* inp (lecture port 0x3DA)    */
#include "video.h"

/* ---------------------------------------------------------
   Accès direct à la VRAM en modèle flat 32 bits
   ---------------------------------------------------------
   Sous DOS/32A (comme sous DOS/4GW, PMODE/W...), le premier
   méga-octet d'espace linéaire du programme est mappé 1:1 sur
   la mémoire physique basse : l'adresse linéaire 0xA0000
   correspond exactement à l'adresse physique 0xA0000, qui est
   la VRAM VGA en mode 13h. On peut donc la traiter comme un
   pointeur plat ordinaire, sans FP_SEG/FP_OFF ni movedata().
   C'est la technique standard de tous les programmes DOS en
   mode protégé flat (Doom, Quake, etc. l'utilisent). */
#define VGA_FLAT_PTR  ((unsigned char *)0xA0000UL)

/* ---------------------------------------------------------
   Backbuffer global
   --------------------------------------------------------- */

/* Définition du pointeur backbuffer (déclaré extern dans
   video.h, défini ici une seule fois dans tout le projet).
   Initialisé à NULL : indique que le buffer n'est pas
   encore alloué. */
unsigned char *backbuffer = NULL;

/* =========================================================
   BACKBUFFER
   ========================================================= */

/* Alloue le backbuffer avec malloc() standard : en modèle flat
   32 bits il n'y a plus de limite de 64 Ko ni de segment de
   données séparé, malloc() peut allouer n'importe où dans les
   plusieurs Mo gérés par l'extendeur DOS/32A.
   Retourne 1 si succès, 0 si échec. */
int initBackbuffer(void)
{
    backbuffer = (unsigned char *)malloc(BACKBUFFER_SIZE);
    if (!backbuffer)
        return 0;   /* allocation échouée : mémoire insuffisante */

    /* Initialiser tous les pixels à 0 (noir) pour éviter
       d'afficher des données aléatoires de la mémoire. */
    memset(backbuffer, 0, BACKBUFFER_SIZE);
    return 1;
}

/* Libère le backbuffer et invalide le pointeur.
   Vérification préalable : évite un double free si
   freeBackbuffer() est appelé deux fois par erreur. */
void freeBackbuffer(void)
{
    if (backbuffer)
    {
        free(backbuffer);
        backbuffer = NULL;   /* évite un pointeur invalide */
    }
}

/* Copie le contenu du backbuffer vers la VRAM VGA.
   En modèle flat, backbuffer et VGA_FLAT_PTR sont deux
   pointeurs plats ordinaires dans le même espace d'adressage :
   un simple memcpy() suffit, plus besoin de movedata(). */
void flip(void)
{
    memcpy(VGA_FLAT_PTR, backbuffer, BACKBUFFER_SIZE);
}

/* =========================================================
   MODE VIDÉO ET CURSEUR
   ========================================================= */

/* Change le mode vidéo via l'interruption BIOS 10h.
   AH = 00h : fonction "Set Video Mode"
   AL = mode : 0x13 pour 320x200x256, 0x03 pour texte 80x25 */
void setVideoMode(unsigned char mode)
{
    _asm {
        mov ah, 00h   /* fonction BIOS : changement de mode */
        mov al, mode  /* numéro du mode vidéo               */
        int 10h       /* appel BIOS vidéo                   */
    }
}

/* Cache le curseur texte via l'interruption BIOS 10h.
   AH = 01h : fonction "Set Cursor Shape"
   CX = 1400h : ligne de début du curseur = 0x14 (20), au-delà
   de la hauteur de la cellule de caractère : le curseur n'est
   plus dessiné. (La méthode documentée par le BIOS consiste à
   mettre le bit 5 de CH à 1, ex. CX = 2000h ; 0x14 n'a pas ce
   bit à 1, il masque le curseur par débordement.) */
void cursorOff(void)
{
    _asm {
        mov ah, 01h     /* fonction BIOS : forme du curseur  */
        mov cx, 1400h   /* début 0x14 > hauteur cellule      */
        int 10h
    }
}

/* Restaure le curseur texte standard via BIOS 10h.
   CX = 0607h : CH=06 (ligne de début), CL=07 (ligne de fin)
   correspond à un curseur underline standard en mode texte. */
void cursorOn(void)
{
    _asm {
        mov ah, 01h     /* fonction BIOS : forme du curseur  */
        mov cx, 0607h   /* curseur underline standard        */
        int 10h
    }
}

/* =========================================================
   SYNCHRONISATION VERTICALE
   ========================================================= */

/* Attend le prochain retrace vertical du moniteur.
   Le registre d'état VGA (port 0x3DA) indique :
     bit 3 = 1 : retrace vertical en cours
     bit 3 = 0 : affichage actif (pas de retrace)

   Algorithme en deux temps :
   1. Attendre la FIN du retrace en cours (si on est
      déjà dedans, évite de rater le prochain).
   2. Attendre le DÉBUT du prochain retrace.

   On peut alors faire flip() ou changer la palette en
   toute sécurité sans provoquer de tearing visible. */
void waitVRetrace(void)
{
    while  (inp(0x3DA) & 0x08);    /* attendre fin du retrace en cours  */
    while (!(inp(0x3DA) & 0x08));  /* attendre début du prochain retrace */
}
