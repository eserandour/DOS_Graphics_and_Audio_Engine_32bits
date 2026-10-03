/* =========================================================
   DUMPPAL.C — Dump de la palette par défaut du mode 13h
   =========================================================
   Passe en mode 13h, lit les 256 couleurs du DAC VGA
   (palette initialisée par le BIOS pour ce mode) et les
   écrit dans DEFAULT.PAL, puis repasse en mode texte.

   Compilation et exécution (FreeDOS, Open Watcom 1.9, DOS/32A),
   avec les mêmes options que BUILD.BAT :
     Se placer dans le répertoire OUTILS qui contient ce fichier
     wcc386 -3s -mf -os -I.. DUMPPAL.C
     wcc386 -3s -mf -os -I.. ..\palette.c
     wcc386 -3s -mf -os -I.. ..\video.c
     wlink format os2 le option stub=stub32a.exe
           file dumppal.obj,palette.obj,video.obj name dumppal.exe
     DUMPPAL
   (ajouter les LIBPATH de LINK.RSP si wlink ne trouve pas les
   bibliothèques ; ou, si votre installation reconnaît le système
   STUB32A : wcl386 -3s -mf -os -I.. -l=stub32a DUMPPAL.C
   ..\palette.c ..\video.c)

   Le fichier DEFAULT.PAL produit est au format standard du
   projet : 768 octets bruts (256 × R/G/B sur 6 bits). Copié
   dans images/, il devient images/default.pal.
   ========================================================= */

#include <stdio.h>
#include "palette.h"
#include "video.h"

int main(void)
{
    /* Passer en mode 13h : le BIOS charge sa palette par défaut. */
    setVideoMode(0x13);

    /* Lire le DAC immédiatement, avant tout autre changement. */
    getPalette(defaultPalette);

    /* Repasser en mode texte. */
    setVideoMode(0x03);

    if (savePalette(defaultPalette, "DEFAULT.PAL"))
        printf("DEFAULT.PAL ecrit (768 octets).\n");
    else
        printf("ERREUR : impossible d'ecrire DEFAULT.PAL.\n");

    return 0;
}
