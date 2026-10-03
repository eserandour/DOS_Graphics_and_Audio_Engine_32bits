# DOS Graphics & Audio Engine (32 bits)

*Dernière version : 03/10/2026 à 21h44*

Moteur graphique et audio pour **DOS**, écrit en C ANSI avec **Open Watcom 1.9**, mode VGA **13h** (320×200, 256 couleurs) et carte **Sound Blaster** (ou compatible), en modèle mémoire **flat 32 bits** (DOS/32A).

Accès direct au matériel PC (VRAM, PIT, clavier, DMA/DSP), sans dépendance à une bibliothèque graphique ou audio tierce. Une playlist de 9 scènes de démonstration (`scenes/`) illustre l'ensemble des modules : palette, polices bitmap, primitives 2D, rotozoom, musique tracker S3M... Un gabarit (`scenes/scene_template.c`) donne la structure à suivre pour écrire ses propres scènes en gérant correctement le timer.

<p align="center">
  <img src="CAPTURES/demo_009.png" width="45%" alt="Texte font2 (feuille de sprites) : HELLO WORLD">
  <img src="CAPTURES/demo_007.png" width="45%" alt="Police bitmap font1 16x16">
</p>
<p align="center">
  <img src="CAPTURES/demo_012.png" width="45%" alt="Tunnel de cercles concentriques">
  <img src="CAPTURES/demo_015.png" width="45%" alt="Rosace de polygones remplis">
</p>

---

## Sommaire

- [Fonctionnalités](#fonctionnalités)
- [Structure du dépôt](#structure-du-dépôt)
- [Modules](#modules)
- [Prérequis](#prérequis)
- [Compilation](#compilation)
- [Exécution](#exécution)
- [Exemple minimal](#exemple-minimal)
- [Scènes de démonstration](#scènes-de-démonstration)
  - [Écrire une nouvelle scène](#écrire-une-nouvelle-scène)
- [Outils annexes (OUTILS/)](#outils-annexes-outils)
- [Limites connues](#limites-connues)
- [Licence](#licence)
- [Remerciements](#remerciements)
- [Version 16 bits](#version-16-bits)

---

## Fonctionnalités

- **Vidéo** — mode 13h, double buffering (backbuffer RAM + `flip()`), attente du retrace vertical (`waitVRetrace()`, utilisée par les changements de palette).
- **Palette VGA** — chargement/sauvegarde `.pal`, interpolation (fade), cycle de couleurs, générateurs procéduraux (niveaux de gris, rouge/vert/bleu, arc-en-ciel HSV).
- **Primitives 2D** — pixel, ligne (Bresenham + clipping Cohen-Sutherland), rectangle, polygone (contour et remplissage scanline), cercle (Bresenham / mid-point).
- **Images & sprites** — chargement `.raw`/`.pal` en un coup (fonds d'écran, écrans fixes) ou préchargés en RAM pour un blit sans accès disque par frame, transparence par *color key*, feuilles de sprites.
- **Texte bitmap** — `font1` (glyphes ROM BIOS ou personnels, 8×8/8×16/16×16, accents français CP850) et `font2` (rendu par feuille de sprites).
- **Timer haute résolution** — reprogrammation du PIT à 70 Hz, avec chaînage vers l'ISR BIOS d'origine pour ne pas casser l'horloge DOS.
- **Clavier** — détection bas niveau de la touche Échap via l'interruption 09h.
- **Audio** — pilote Sound Blaster bas niveau (détection `BLASTER`, DSP, DMA en boucle auto-init sans clic), lecteur de modules **S3M** (vitesse, tempo, sauts, volumes, glissements, portamento, tone portamento, vibrato, tremolo, tremor, arpège, offset, retrigger, finetune, boucle de motif, note retardée, coupure de note, retard de ligne...) et mixeur d'effets **WAV** (8/16 bits, mono/stéréo, rééchantillonnage à la volée), mixage effectué hors interruption.
- **Gestionnaire de scènes** — chaque scène gère son propre minutage (en ticks du timer, à partir d'un gabarit commun) et signale sa fin ; l'enchaînement (playlist, bouclage) est décidé par `main.c`.

## Structure du dépôt

```
DOS_Graphics_and_Audio_Engine_32bits/
├── main.c              Point d'entrée, boucle principale, arrêt propre
├── app.h                Flags globaux (quitRequested)
│
├── video.c / video.h     Mode 13h, backbuffer, retrace vertical
├── palette.c / palette.h Palette VGA (DAC), fade, cycle, générateurs
├── graphics.c / graphics.h  Primitives 2D (ligne, rectangle, polygone, cercle)
├── image.c / image.h     Chargement one-shot d'images .raw/.pal
├── sprite.c / sprite.h   Sprites préchargés en RAM, feuilles de sprites
├── font1.c / font1.h     Texte bitmap (BIOS ou police personnelle)
├── font2.c / font2.h     Texte par feuille de sprites
│
├── timer.c / timer.h     Timer PIT haute résolution (70 Hz)
├── keyboard.c / keyboard.h  Détection Échap (INT 09h)
│
├── audio.c / audio.h     Orchestrateur audio (musique + effets)
├── sblaster.c / sblaster.h  Pilote bas niveau Sound Blaster (DSP + DMA)
├── s3m.c / s3m.h         Lecteur de modules musicaux .s3m (effets S3M étendus)
├── wav.c / wav.h         Chargement et mixage d'effets .wav
│
├── scene.c / scene.h     Gestionnaire de scènes (table des scènes, signal de fin)
├── scenes/               9 scènes de démonstration (scene0.c … scene8.c)
│   └── scene_template.c  Gabarit pour écrire une scène (gestion du timer)
│
├── font1/                Données des polices bitmap personnelles
├── font2/                Feuilles de sprites de police + palettes
├── images/               Images de démo (.raw/.pal) et palettes de test
├── audios/               Musique de démo (musique.s3m)
├── CAPTURES/             Captures d'écran de la démo
│
├── OUTILS/               Scripts Python (conversion d'assets, audit S3M) + DUMPPAL.C
│
├── BUILD.BAT             Compilation (wcc386 + wlink), génère les 3 fichiers ci-dessous
├── CLEAN.BAT / CLEANALL.BAT  Nettoyage des fichiers générés
└── LICENSE               GNU GPL v3

Fichiers générés par BUILD.BAT (ne pas modifier à la main) :
├── LINK.RSP              Script d'édition de liens (DOS/32A)
├── SCENEDCL.H            Prototypes des scènes présentes (inclus par scene.c)
└── SCENETAB.H            Entrées du tableau des scènes (inclus par scene.c)

```

## Modules

| Module     | Rôle                                     | Dépend de                |
| ---------- | ---------------------------------------- | ------------------------ |
| `video`    | Backbuffer, mode vidéo, retrace vertical | —                        |
| `palette`  | Palette VGA (DAC), fade, cycle           | `video`                  |
| `graphics` | Primitives de dessin 2D                  | `video`                  |
| `image`    | Chargement one-shot d'images             | `video`, `palette`       |
| `sprite`   | Sprites préchargés, feuilles de sprites  | `video`                  |
| `font1`    | Texte bitmap multi-tailles               | `video`, `graphics`      |
| `font2`    | Texte par feuille de sprites             | `video`                  |
| `timer`    | Timer PIT 70 Hz                          | —                        |
| `keyboard` | Détection Échap                          | `app`                    |
| `sblaster` | Pilote DSP + DMA Sound Blaster           | —                        |
| `s3m`      | Lecteur de modules musicaux              | —                        |
| `wav`      | Mixeur d'effets sonores                  | —                        |
| `audio`    | Orchestrateur audio                      | `sblaster`, `s3m`, `wav` |
| `scene`    | Enchaînement des scènes                  | `timer`                  |

Chaque `.h` documente en tête de fichier le format de données et les conventions d'usage du module correspondant.

## Prérequis

- **[Open Watcom 1.9](http://www.openwatcom.org/)** (`wcc386` + `wlink`), seule chaîne de compilation testée.
- **[DOS/32A](http://sourceforge.net/projects/dos32a/)** : `STUB32A.EXE` à l'édition de liens (référencé par `LINK.RSP`), `DOS32A.EXE` à côté de `demo.exe` (ou dans le `PATH`) au lancement.
- Un PC réel (386 ou plus) avec carte VGA, ou un émulateur DOS : [DOSBox](https://www.dosbox.com/), [DOSBox-X](https://dosbox-x.com/), [86Box](https://86box.net/).
- Pour le son : carte **Sound Blaster 16** (DSP ≥ 4.00) ou compatible, configurée via la variable d'environnement `BLASTER` (ex. `SET BLASTER=A220 I5 D1 H5 P330 T6`). C'est la carte émulée par défaut par DOSBox et DOSBox-X (`sbtype = sb16`). En son absence, ou avec une carte plus ancienne, le moteur audio se désactive proprement et la démo continue en silence.
- Python 3, uniquement pour les scripts de `OUTILS/` (facultatif pour compiler/exécuter la démo) : Pillow pour `vgatool.py`, `gen_palettes.py`, `png2c.py`, `ttf2c.py` et les aperçus de `psf2c.py` ; `freetype-py` en plus pour `ttf2c.py` ; un compilateur C natif (gcc) pour `gen_palettes.py` et `s3m_audit.py` (plus `ffmpeg` avec libopenmpt pour ce dernier) ; `s3m_extract.py` n'utilise que la bibliothèque standard.

## Compilation

```
BUILD.BAT
```

Compile chaque module avec `wcc386 -3s -mf -os -I.` (instructions 386, modèle flat, optimisation taille), puis lie via `wlink @LINK.RSP` pour produire `demo.exe`.

```
CLEAN.BAT       REM supprime .obj / .out / .err
CLEANALL.BAT    REM idem + supprime aussi demo.exe (DOS32A.EXE / STUB32A.EXE sont conservés)
```

> `LINK.RSP` explicite les directives DOS/32A (format `OS2 LE`, stub `stub32a.exe`). Si votre installation Watcom reconnaît déjà le système `STUB32A`, `LINK.RSP` peut être réduit à la ligne `SYSTEM STUB32A`.

## Exécution

```
demo.exe
```

À lancer depuis la racine du projet : les scènes chargent leurs ressources par des chemins relatifs (`font2\`, `images\`, `audios\`).

Boucle jusqu'à **Échap**.

## Exemple minimal

```
#include "video.h"
#include "palette.h"
#include "graphics.h"
#include "timer.h"
#include "keyboard.h"
#include "app.h"

int main(void)
{
    initBackbuffer();
    setVideoMode(0x13);
    installTimer();
    installKeyboard();

    while (!quitRequested)
    {
        clearScreen(0);
        drawCircleFill(160, 100, 40, 12);
        drawRect(10, 10, 309, 189, 15);
        flip();
    }

    restoreKeyboard();
    restoreTimer();
    setVideoMode(0x03);
    freeBackbuffer();
    return 0;
}
```

## Scènes de démonstration

| # | Scène      | Contenu                                                                                   |
| - | ---------- | ----------------------------------------------------------------------------------------- |
| 0 | `scene0.c` | Écran noir (3 s, calage des captures vidéo)                                               |
| 1 | `scene1.c` | Pixels aléatoires (LCG) avec fondu d'entrée/sortie                                        |
| 2 | `scene2.c` | Palette VGA : cycle de couleurs, interpolation (lerp)                                     |
| 3 | `scene3.c` | Polices `font1` : BIOS et personnelles, 8×8/8×16/16×16                                    |
| 4 | `scene4.c` | Texte via `font2` (feuille de sprites)                                                    |
| 5 | `scene5.c` | Scrolling de texte, horizontal puis vertical                                              |
| 6 | `scene6.c` | Rotozoom (rotation + zoom) sur une image 256×256                                          |
| 7 | `scene7.c` | Primitives 2D en 6 phases : tunnel, spirale de lignes, plasma en damier, rosace de polygones, polygones en mouvement, composition |
| 8 | `scene8.c` | Cycle de vie audio : `playMusic` → `fadeMusicIn` → lecture (morceau joué **deux fois**) → `fadeMusicOut` → `stopMusic` ; durée déduite de la musique |

Ordre et bouclage définis par le tableau `playlist[]` dans `main.c`.

Toutes les scènes suivent la même structure, celle de `scenes/scene_template.c` (`scene0.c` en est une version réduite, sans rendu ni fondu, qui compare directement des millisecondes). Leur minutage repose sur les **ticks** du timer (70 Hz) plutôt que sur des millisecondes.

### Écrire une nouvelle scène

`scenes/scene_template.c` est le point de départ : il fournit la gestion du timer, il ne reste qu'à remplir les zones repérées par des bandeaux `VOTRE CODE ICI`.

**Structure d'une scène**

| Zone                  | Rôle                                                                                 |
| --------------------- | ------------------------------------------------------------------------------------ |
| Réglages              | `SCENE_MS`, `FADE_IN_MS`, `FADE_OUT_MS` (durées en ms), `FRAME_TICKS` (cadence)       |
| Variables             | Données propres à la scène (`static`, niveau fichier)                                 |
| `sceneXInit()`        | Appelée **une fois** au lancement : chargement des ressources, état initial           |
| `sceneXRender()`      | Appelée à chaque image : `phase` (0 intro, 1 corps, 2 outro), `progress` (0 à 1000 dans la phase), `elapsed` (ticks écoulés) |
| `sceneXCleanup()`     | Appelée **une fois** à la fin : libération de ce qu'`Init` a alloué                   |
| `sceneX()`            | Point d'entrée, gestion du timer : **à ne pas modifier** (seul le nom change)         |

**Règles du timer**

- On raisonne en **ticks** (70 Hz, 1 tick ≈ 14,3 ms). Les durées longues s'écrivent en ms et sont converties une fois par `MS_TO_TICKS` (arrondi au plus proche, minimum 1 tick ; `0` reste `0`, ce qui supprime la phase). La cadence s'écrit directement en ticks avec `FRAME_TICKS` : 1 = 70 Hz, 2 = 35 Hz, 3 = 23 Hz, 4 = 17,5 Hz.
- `sceneStart` est posé une seule fois, au premier appel, juste avant `Init` : c'est l'unique référence de temps de la scène (`setScene()` le pose aussi, mais il est reposé à ce premier appel).
- La fonction ne **bloque jamais** (ni `pause()` ni boucle d'attente) : `main.c` appelle `audioUpdate()` entre deux appels.
- La cadence avance par **pas fixes** (`lastFrame += FRAME_TICKS`), sans dérive ; en cas de retard important, le retard est abandonné au lieu d'être rattrapé en rafale.
- La fin de scène est testée **avant** le rendu. L'état est remis à zéro **avant** `sceneSignalEnd()`, pour qu'une scène relancée aussitôt (bouclage de la playlist) reparte propre.

**Scènes à plusieurs étapes.** Pour une scène composée de plusieurs sous-écrans (`scene2`, `scene3`, `scene7`), on met `FADE_IN_MS`/`FADE_OUT_MS` à `0UL` si besoin et on déduit l'étape courante de `elapsed` (`elapsed / DUREE_ETAPE_TICKS`) au lieu de relancer un chronomètre à chaque étape, qui ferait dériver la durée totale.

**Scènes dont la durée dépend du contenu.** `scene8` joue `musique.s3m` deux fois : sa durée vient du morceau, pas d'une constante. Le premier bouclage (`hasMusicLooped()`) donne la durée d'une lecture, d'où l'on déduit l'instant du fondu de sortie ; la scène se termine d'elle-même via un indicateur (`scene8Over`) testé dans le point d'entrée, `SCENE_MS` ne servant que de borne de sécurité. Ce sont les deux seuls écarts au gabarit : cette ligne dans le point d'entrée, et `sceneStart` reposé à la fin de `scene8Init()`, après le chargement du module, pour que la durée d'une lecture parte du démarrage réel de la musique.

**Créer une scène.** Copier `scene_template.c` en `scenes/sceneN.c`, renommer `sceneX` / `sceneXInit` / `sceneXRender` / `sceneXCleanup` en `sceneN…`, régler les durées et remplir les zones, puis ajouter `SCENE_N` à `playlist[]` dans `main.c`. Rien d'autre à déclarer : `SCENE_0` à `SCENE_99` existent déjà dans `scene.h`, et `BUILD.BAT` détecte automatiquement les fichiers `scenes\scene0.c` à `scenes\scene99.c`, les compile, les ajoute à `LINK.RSP` et génère `SCENEDCL.H` / `SCENETAB.H` (prototypes et tableau `scenes[]` inclus par `scene.c`). `scene_template.c`, qui ne porte pas un nom de ce type, n'est jamais compilé.

> ⚠️ Les scènes doivent être **numérotées sans trou** (`scene0.c`, `scene1.c`, `scene2.c`…). Le tableau `scenes[]` est indexé par position : s'il manquait par exemple `scene5.c`, `SCENE_6` appellerait `scene7()`.

## Outils annexes (OUTILS/)

- **`vgatool.py`** — convertit une image en `.raw` + `.pal`, ou visualise une palette existante.
- **`gen_palettes.py`** — régénère les fichiers `.pal` procéduraux du projet et leurs aperçus PNG en compilant (gcc) et en exécutant les fonctions `build...Palette()` de `palette.c`.
- **`fonts/`** — conversion de polices TrueType, PNG ou PSF vers le format `Font1Bank` (`ttf2c.py`, `png2c.py`, `psf2c.py`).
- **`s3m_extract.py`** — extrait les samples d'un fichier `.s3m` en `.wav` (un fichier par sample, fréquence = `C2Spd`). Python 3 seul, sans dépendance :

  ```
  python s3m_extract.py audios/musique.s3m dossier_sortie
  ```

  Sont ignorés les instruments AdLib et les samples compressés ; les points de boucle sont affichés mais pas écrits dans les WAV. Utile pour réutiliser les sons d'un module comme effets (`wav.c`) ou pour les retravailler dans un tracker.
- **`s3m_audit.py`** — vérifie les effets de `s3m.c` en les comparant à **libopenmpt** (via `ffmpeg`), tick par tick : une dizaine de familles d'effets (portamentos, vibrato, arpège, volume, tremor, retrigger, commandes S, mémoire d'effet, en-tête...) sur de petits modules de test. Prérequis : `gcc` et `ffmpeg` compilé avec libopenmpt ; usage `python3 s3m_audit.py` depuis `OUTILS/` (le script trouve `../s3m.c` tout seul ; un autre chemin peut être passé en argument).
- **`DUMPPAL.C`** — programme DOS qui passe en mode 13h, lit la palette par défaut du DAC VGA et l'écrit dans `DEFAULT.PAL` (origine de `images/default.pal`).
- **`fonts/ascii_cp850.py`** — génère `ascii_cp850.txt`, table des 256 codes CP850 avec leurs glyphes.

## Limites connues

- Mode 13h uniquement (320×200, 256 couleurs).
- Lecteur S3M : échantillons PCM non compressés uniquement, les voies sont mixées en mono, tick de durée entière comme ST3/libopenmpt (option `S3M_EXACT_TICKS` pour une durée exacte). Les effets sont vérifiés contre libopenmpt par `OUTILS/s3m_audit.py`. Tous les effets standard de Scream Tracker 3 sont pris en charge (A, B, C, D, E, F, G, H, I, J, K, L, O, Q, R, T, U, V, et S1x, S2x, S3x, S4x, SBx, SCx, SDx, SEx), avec mémoire d'effet. Sont ignorés (la note se déclenche quand même) : le filtre (S0x), le panoramique (S8x, SAx), le funk repeat (SFx) et les extensions non standard ; ne sont pas non plus reproduits certains comportements très particuliers de ST3 (volume global appliqué seulement aux notes dont le volume change, note coupée par SCx « figée » puis reprise par un E/F/G/H...) ; l'en-tête de `s3m.h` donne le détail exact. La fidélité du vibrato et du tremolo est une approximation de Scream Tracker 3, suffisante pour la démo mais pas bit-exacte.
- Jusqu'à `S3M_MAX_CHANNELS` (16) voies mixées et `WAV_MAX_VOICES` (4) effets simultanés.
- Sound Blaster 16 (DSP ≥ 4.00) requise : sortie DMA 8 bits mono, auto-init, à 22 050 Hz exacts (commande `0x41`). Les cartes plus anciennes (SB 2.0, SB Pro) ne savent produire que 1 000 000 / n Hz, soit 22 222 Hz au plus près : le moteur les refuse plutôt que de jouer à une autre fréquence.
- Testé uniquement avec Open Watcom 1.9 + DOS/32A.

## Licence

GNU GPL v3 — voir [`LICENSE`](LICENSE).

## Remerciements

`audios/musique.s3m` (*Vampyra 4: "Najade"*, fichier d'origine `vampyra_4-najade.s3m`) est emprunté à **Gnosis** (Gisle Martens Meyer) — voir `audios/readme.txt`.

## Version 16 bits

Pour ceux qui souhaitent retrouver le modèle mémoire DOS classique, une **version 16 bits** du moteur est également disponible.

Cette version utilise des **pointeurs FAR** ainsi que le modèle mémoire segmenté traditionnel du DOS 16 bits.

👉 **[DOS Graphics and Audio Engine – 16 bits](https://github.com/eserandour/DOS_Graphics_and_Audio_Engine_16bits)**

Les versions 32 bits et 16 bits sont maintenues dans deux projets distincts, afin de permettre l'utilisation du moteur aussi bien dans un **environnement 32 bits en mode protégé** que dans l'**environnement DOS classique 16 bits**.
