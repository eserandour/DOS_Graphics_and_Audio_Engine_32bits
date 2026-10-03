#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
s3m_audit.py — compare les effets de s3m.c à ceux de libopenmpt (la référence de
fait pour Scream Tracker 3), module de test par module de test.

Principe : pour chaque effet, on fabrique un petit .s3m (une voie, un échantillon
sinusoïdal), on le fait jouer par libopenmpt (via ffmpeg) ET par ton moteur
(s3m.c compilé avec gcc), puis on compare, tick par tick, la HAUTEUR (en cents)
et le VOLUME. Un effet est "OK" si au plus 3 ticks s'écartent au-delà de la
tolérance (les transitions de ligne ne sont pas alignées au dixième de tick près).

Prérequis : python3, gcc, ffmpeg compilé avec libopenmpt.
Usage     : python s3m_audit.py [chemin/vers/s3m.c]        (défaut : ./s3m.c)
"""
import math, os, struct, subprocess, sys, tempfile, wave

SRC = os.path.abspath(sys.argv[1] if len(sys.argv) > 1 else 's3m.c')
INC = os.path.dirname(SRC)
TMP = tempfile.mkdtemp(prefix='s3m_audit_')
RATE = 11025
TOL_CENTS, TOL_VOL, MAX_BAD = 20.0, 0.20, 3

DUMP_C = r'''
#include <stdio.h>
#include <stdlib.h>
#include "s3m.h"
int main(int argc, char **argv)
{
    unsigned char b; unsigned long i, n = strtoul(argv[3], 0, 10);
    FILE *o = fopen(argv[2], "wb");
    s3mInit(11025UL);
    if (s3mLoad(argv[1]) != S3M_OK) return 1;
    for (i = 0; i < n; i++) { s3mMix(&b, 1); fwrite(&b, 1, 1, o); }
    fclose(o); return 0;
}
'''
ENGINE = os.path.join(TMP, 'dump')
open(os.path.join(TMP, 'dump.c'), 'w').write(DUMP_C)
subprocess.run(['gcc', '-std=c89', '-O1', '-I' + INC, os.path.join(TMP, 'dump.c'), SRC, '-o', ENGINE], check=True)

# ----------------------------------------------------------------------------- modules de test
CMD = {c: i + 1 for i, c in enumerate('ABCDEFGHIJKLMNOPQRSTUV')}
C4, E4, G4, C5 = 0x40, 0x44, 0x47, 0x50

def pack_pattern(rows):
    out = bytearray()
    for r in range(64):
        for ch, (note, inst, vol, cmd, info) in sorted(rows.get(r, {}).items()):
            what = ch; body = bytearray()
            if note is not None: what |= 0x20; body += bytes([note, inst or 0])
            if vol is not None:  what |= 0x40; body += bytes([vol])
            if cmd is not None:  what |= 0x80; body += bytes([cmd, info])
            out += bytes([what]) + body
        out.append(0)
    return bytes(out)

def make(path, rows, speed=6, tempo=125, c2spd=8363, cwt=0x1320, flags=0):
    data = bytes(int(round(128 + 100 * math.sin(2 * math.pi * i / 16))) for i in range(16))
    hdr = bytearray(96)
    hdr[0:5] = b'AUDIT'; hdr[28] = 0x1A; hdr[29] = 16
    struct.pack_into('<HHH', hdr, 32, 2, 1, 1)
    struct.pack_into('<HHH', hdr, 38, flags, cwt, 2)
    hdr[44:48] = b'SCRM'; hdr[48] = 64; hdr[49] = speed; hdr[50] = tempo; hdr[51] = 127; hdr[53] = 0xFC
    for i in range(32): hdr[64 + i] = i if i < 8 else 255
    body = bytearray(hdr) + bytes([0, 255])
    ip = len(body); body += bytes(2); pp = len(body); body += bytes(2); body += bytes(32)
    while len(body) % 16: body.append(0)
    io = len(body); body += bytes(80)
    while len(body) % 16: body.append(0)
    pat = pack_pattern(rows); po = len(body); body += struct.pack('<H', len(pat) + 2) + pat
    while len(body) % 16: body.append(0)
    so = len(body); body += data
    while len(body) % 16: body.append(0)
    h = bytearray(80); h[0] = 1; h[1:5] = b'TEST'
    h[13] = (so // 16) >> 16; struct.pack_into('<H', h, 14, (so // 16) & 0xFFFF)
    struct.pack_into('<III', h, 16, 16, 0, 16); h[28] = 64; h[31] = 1
    struct.pack_into('<I', h, 32, c2spd); h[76:80] = b'SCRS'
    body[io:io + 80] = h
    struct.pack_into('<H', body, ip, io // 16); struct.pack_into('<H', body, pp, po // 16)
    open(path, 'wb').write(body)

def cell(note=None, inst=None, vol=None, cmd=None, info=None):
    return (note, inst, vol, CMD[cmd] if cmd else None, info)

def R(*cells):
    return {i: {0: c} for i, c in enumerate(cells) if c is not None}

# ----------------------------------------------------------------------------- rendus et mesures
def render_ref(mod, seconds):
    out = os.path.join(TMP, 'ref.wav')
    r = subprocess.run(['ffmpeg', '-y', '-hide_banner', '-loglevel', 'error', '-f', 'libopenmpt', '-sample_rate',
                        str(RATE), '-layout', 'mono', '-i', mod, '-t', str(seconds), '-ac', '1', '-ar', str(RATE),
                        '-f', 'wav', out], capture_output=True, text=True)
    if r.returncode: raise SystemExit('ffmpeg/libopenmpt indisponible : ' + r.stderr[:200])
    w = wave.open(out); n = w.getnframes()
    return [v / 32768.0 for v in struct.unpack('<%dh' % n, w.readframes(n))]

def render_eng(mod, n):
    out = os.path.join(TMP, 'eng.raw')
    subprocess.run([ENGINE, mod, out, str(n)], check=True)
    return [(b - 128) / 128.0 for b in open(out, 'rb').read()]

F0 = 8363 / 16.0
def freq_cents(x, a, b):
    cr = [i - 1 + (-x[i - 1]) / (x[i] - x[i - 1]) for i in range(a + 1, b) if x[i - 1] < 0 <= x[i]]
    if len(cr) < 3: return None
    return 1200 * math.log2(RATE * (len(cr) - 1) / (cr[-1] - cr[0]) / F0)

def amp(x, a, b):
    seg = x[a:b]; m = sum(seg) / len(seg)
    return math.sqrt(sum((v - m) ** 2 for v in seg) / len(seg)) * math.sqrt(2)

MOD = os.path.join(TMP, 't.s3m')
def baseline():
    make(MOD, R(cell(C4, 1)))
    r, e = render_ref(MOD, 0.5), render_eng(MOD, 5512)
    return amp(r, 440, 661), amp(e, 440, 661)
BASE = baseline()

def measure(rows, ticks, tempo=125, **kw):
    make(MOD, rows, tempo=tempo, **kw)
    sec = ticks * 2.5 / tempo + 0.05
    r, e = render_ref(MOD, sec), render_eng(MOD, int(sec * RATE))
    res = []
    for k in range(ticks):
        a, b = int(round(k * RATE * 2.5 / tempo)), int(round((k + 1) * RATE * 2.5 / tempo))
        res.append((freq_cents(r, a, b), freq_cents(e, a, b), amp(r, a, b) / BASE[0], amp(e, a, b) / BASE[1]))
    return res

RESULTS = []
def test(name, rows, ticks, pitch=True, vol=True, known=None, **kw):
    res = measure(rows, ticks, **kw)
    bad = 0
    for cr, ce, ar, ae in res:
        b = False
        if pitch and cr is not None and ce is not None and abs(cr - ce) > TOL_CENTS: b = True
        if vol and abs(ar - ae) > TOL_VOL: b = True
        bad += b
    ok = bad <= MAX_BAD
    status = 'OK' if ok else ('DIFF CONNUE' if known else 'ÉCART')
    RESULTS.append(status)
    print('%-12s %-48s %d tick(s) hors tolérance%s' % (status, name, bad, ('  — ' + known) if (known and not ok) else ''))

# ----------------------------------------------------------------------------- scénarios
n32, n40, n0 = cell(C4, 1, 32), cell(C4, 1, 40), cell(C4, 1)
print('Référence : libopenmpt via ffmpeg | tolérances : %g cents, %g de volume, %d ticks hors tolérance max.\n' % (TOL_CENTS, TOL_VOL, MAX_BAD))
print('--- hauteur : portamentos, glissando ---')
test('E04  portamento vers le grave', R(n0, cell(cmd='E', info=0x04)), 14)
test('F04  portamento vers l\'aigu', R(n0, cell(cmd='F', info=0x04)), 14)
for nm, cmd, info in (('EF4 : fin (x*4) vers le grave', 'E', 0xF4), ('EE4 : extra-fin (x*1) vers le grave', 'E', 0xE4),
                      ('FF4 : fin (x*4) vers l\'aigu', 'F', 0xF4), ('FE4 : extra-fin (x*1) vers l\'aigu', 'F', 0xE4)):
    test(nm + ', 14 lignes', {**{0: {0: n0}}, **{r: {0: cell(cmd=cmd, info=info)} for r in range(1, 15)}}, 14 * 6)
test('G08 vers E-4', R(n0, cell(E4, 1, cmd='G', info=0x08)), 14)
test('G00 (mémoire de G)', R(n0, cell(E4, 1, cmd='G', info=0x04), cell(cmd='G', info=0x00)), 20)
test('F00 après E04 (mémoire commune D/E/F/I/J/K/L/Q/R/S)', R(n0, cell(cmd='E', info=0x04), cell(cmd='F', info=0x00)), 20)
test('S11 glissando + G08', R(n0, cell(cmd='S', info=0x11), cell(E4, 1, cmd='G', info=0x08)), 24)
test('S2F finetune', R(n0, cell(cmd='S', info=0x2F)), 14)
print('--- hauteur : vibrato, arpège ---')
test('H44 (vitesse 4, profondeur 4)', R(n0, cell(cmd='H', info=0x44), cell(cmd='H', info=0x44)), 72, speed=24)
test('H4F (profondeur max)', R(n0, cell(cmd='H', info=0x4F)), 48, speed=24)
test('U44 (vibrato fin)', R(n0, cell(cmd='U', info=0x44)), 48, speed=24)
test('S32 + H44 (onde carrée)', R(n0, cell(cmd='S', info=0x32), cell(cmd='H', info=0x44)), 48, speed=24)
test('J47 (arpège)', R(n0, cell(cmd='J', info=0x47)), 18)
test('J00 après J47 (mémoire commune)', R(n0, cell(cmd='J', info=0x47), cell(cmd='J', info=0x00)), 24)
test('K04 après H44 (vibrato + volume)', R(n0, cell(cmd='H', info=0x44), cell(cmd='K', info=0x04)), 36, speed=12)
test('L04 après G08 (tone portamento + volume)', R(n0, cell(E4, 1, cmd='G', info=0x08), cell(cmd='L', info=0x04)), 24)
print('--- volume : D, Q, R, I ---')
for p in (0x04, 0x40, 0x44, 0x0F, 0xF0, 0x4F, 0xF4, 0xFF):
    test('D%02X' % p, R(n32, cell(cmd='D', info=p)), 12, pitch=False)
test('D00 après E04 (mémoire commune)', R(n32, cell(cmd='E', info=0x04), cell(cmd='D', info=0x00)), 18, pitch=False)
test('D04 avec glissements "ST3.00" (cwt 0x1300)', R(n32, cell(cmd='D', info=0x04)), 12, pitch=False, cwt=0x1300)
test('Q33 (-4 toutes les 3 ticks)', R(n40, cell(cmd='Q', info=0x33)), 18, pitch=False)
test('Q34 sur 2 lignes (compteur persistant)', R(n40, cell(cmd='Q', info=0x34), cell(cmd='Q', info=0x34)), 24, pitch=False)
test('Q63 (x=6 : table 2/3 de ST3)', R(n40, cell(cmd='Q', info=0x63)), 14, pitch=False)
test('R44 (tremolo)', R(n40, cell(cmd='R', info=0x44)), 48, pitch=False, speed=24)
test('R4F (tremolo profond)', R(n40, cell(cmd='R', info=0x4F)), 48, pitch=False, speed=24)
test('I22 (tremor : x+1 ticks de son, y+1 de silence)', R(n40, cell(cmd='I', info=0x22)), 18, pitch=False)
test('I00 après I22 (mémoire + compteurs persistants)', R(n40, cell(cmd='I', info=0x22), cell(cmd='I', info=0x00)), 24, pitch=False)
print('--- commandes S, notes, volumes, en-tête ---')
test('SC3 (coupure au tick 3)', R(n40, cell(cmd='S', info=0xC3)), 14, pitch=False)
test('SC0 (ignoré)', R(n40, cell(cmd='S', info=0xC0)), 14, pitch=False)
test('SD3 (note retardée)', R(cell(C4, 1, 40), cell(E4, 1, 40, cmd='S', info=0xD3)), 18)
test('SE2 (retard de ligne)', R(cell(C4, 1, 40), cell(E4, 1, 40, cmd='S', info=0xE2), cell(G4, 1, 40)), 36)
test('SB0 / SB2 (boucle de motif)', R(cell(C4, 1, 40, cmd='S', info=0xB0), cell(E4, 1, 40), cell(G4, 1, 40, cmd='S', info=0xB2), cell(C5, 1, 40)), 66)
test('note SANS instrument : le volume est conservé', R(cell(C4, 1, 20), cell(C4, None, None)), 14, pitch=False)
test('instrument SANS note : volume remis au défaut', R(cell(C4, 1, 20), cell(None, 1, None)), 14, pitch=False)
test('V50 (> 0x40 : ignoré)', R(n40, cell(cmd='V', info=0x50)), 14, pitch=False)
test('A00 (ignoré)', R(cell(C4, 1, 40), cell(E4, 1, 40, cmd='A', info=0x00), cell(G4, 1, 40)), 30)
test('T33', R(cell(C4, 1, 40), cell(E4, 1, 40, cmd='T', info=0x21), cell(G4, 1, 40)), 60)
test('T32 (la spec ST3 l\'ignore : T < 33)', R(cell(C4, 1, 40), cell(E4, 1, 40, cmd='T', info=0x20), cell(G4, 1, 40)), 30,
     known='libopenmpt accepte 32, la spécification ST3 l\'ignore (s3m.c suit la spécification)')
test('en-tête : vitesse initiale 255 (ignorée)', R(cell(C4, 1, 40), cell(E4, 1, 40)), 18, speed=255)
test('en-tête : tempo initial 32 (ignoré)', R(cell(C4, 1, 40), cell(E4, 1, 40)), 18, tempo=32)

nb = len(RESULTS)
print('\n%d OK, %d différence(s) connue(s), %d écart(s) sur %d tests.' % (RESULTS.count('OK'), RESULTS.count('DIFF CONNUE'), RESULTS.count('ÉCART'), nb))
sys.exit(1 if 'ÉCART' in RESULTS else 0)
