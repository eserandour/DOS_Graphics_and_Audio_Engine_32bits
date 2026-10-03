#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
s3m_extract.py — extrait les samples d'un fichier S3M (ScreamTracker 3) en WAV.

Usage :  python3 s3m_extract.py musique.s3m [dossier_de_sortie]

Format S3M utilisé (offsets en octets) :
  En-tête (96 octets)
     +32 OrdNum (word)   +34 InsNum (word)   +36 PatNum (word)
     +42 Ffv    (word)   1 = samples signés, 2 = non signés (le plus courant)
     +44 "SCRM"
  +96            : OrdNum octets (table d'ordre)
  +96 + OrdNum   : InsNum parapointers (word) vers les en-têtes d'instruments
  Parapointer    : adresse réelle = valeur * 16

  En-tête d'instrument (80 octets)
      +0  type (1 = sample PCM)
      +1  nom de fichier DOS (12 octets)
     +13  MemSeg : 1 octet haut + 1 word bas, adresse réelle = valeur * 16
     +16  longueur (dword, en échantillons)
     +20  début de boucle (dword)   +24 fin de boucle (dword)
     +28  volume (0-64)
     +30  pack (0 = non compressé)
     +31  flags : bit0 = boucle, bit1 = stéréo, bit2 = 16 bits
     +32  C2Spd (dword) : fréquence de la note C-4, sert de fréquence du WAV
     +48  nom du sample (28 octets)
     +76  "SCRS"
"""
import os
import re
import struct
import sys
import wave


def lire_s3m(chemin, sortie):
    with open(chemin, 'rb') as f:
        d = f.read()

    if d[44:48] != b'SCRM':
        raise ValueError("Ce fichier n'est pas un S3M (signature SCRM absente).")

    ord_num, ins_num = struct.unpack_from('<HH', d, 32)
    ffv = struct.unpack_from('<H', d, 42)[0]
    signe = (ffv == 1)

    para = struct.unpack_from('<%dH' % ins_num, d, 96 + ord_num)

    os.makedirs(sortie, exist_ok=True)
    nb = 0

    for n, p in enumerate(para, start=1):
        o = p * 16
        if o == 0 or o + 80 > len(d) or d[o] != 1:
            continue                                  # vide ou instrument OPL (AdLib)

        hi, lo = struct.unpack_from('<BH', d, o + 13)
        data_off = ((hi << 16) | lo) * 16
        longueur, boucle_deb, boucle_fin = struct.unpack_from('<III', d, o + 16)
        pack, flags = struct.unpack_from('<BB', d, o + 30)
        c2spd = struct.unpack_from('<I', d, o + 32)[0]
        nom = d[o + 48:o + 76].split(b'\0')[0].decode('cp437', 'replace').strip()

        if longueur == 0 or pack != 0:
            continue                                  # sample vide ou compressé (non géré)

        stereo = bool(flags & 2)
        bits16 = bool(flags & 4)
        canaux = 2 if stereo else 1
        larg   = 2 if bits16 else 1
        octets = longueur * canaux * larg
        brut   = d[data_off:data_off + octets]
        if len(brut) < octets:
            print('  sample %02d tronqué dans le fichier, ignoré' % n)
            continue

        # S3M stéréo : tout le canal gauche, puis tout le canal droit -> à entrelacer.
        if stereo:
            moitie = longueur * larg
            g, dr = brut[:moitie], brut[moitie:]
            out = bytearray()
            for i in range(longueur):
                out += g[i * larg:(i + 1) * larg] + dr[i * larg:(i + 1) * larg]
            brut = bytes(out)

        # WAV 8 bits = non signé ; WAV 16 bits = signé little-endian.
        if bits16:
            vals = list(struct.unpack('<%dH' % (len(brut) // 2), brut))
            if not signe:
                vals = [v - 32768 for v in vals]
            else:
                vals = [v - 65536 if v >= 32768 else v for v in vals]
            brut = struct.pack('<%dh' % len(vals), *vals)
        elif signe:
            brut = bytes((b + 128) & 0xFF for b in brut)

        base = re.sub(r'[^\w\- ]+', '_', nom).strip() or 'sample'
        fichier = os.path.join(sortie, '%02d_%s.wav' % (n, base))
        with wave.open(fichier, 'wb') as w:
            w.setnchannels(canaux)
            w.setsampwidth(larg)
            w.setframerate(c2spd or 8363)
            w.writeframes(brut)

        boucle = ' boucle %d-%d' % (boucle_deb, boucle_fin) if flags & 1 else ''
        print('  %02d  %-28s %6d éch.  %5d Hz  %2d bits  %s%s' %
              (n, nom, longueur, c2spd, 16 if bits16 else 8,
               'stéréo' if stereo else 'mono', boucle))
        nb += 1

    print('%d sample(s) extrait(s) dans %s' % (nb, sortie))


if __name__ == '__main__':
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(1)
    src = sys.argv[1]
    dst = sys.argv[2] if len(sys.argv) > 2 else os.path.splitext(src)[0] + '_samples'
    lire_s3m(src, dst)
