@echo off
REM =========================================================
REM BUILD.BAT - Compilation 32 bits (386+, modele flat)
REM Version pur DOS (COMMAND.COM) : pas de set /a, for /L, call
REM Les scenes scenes\scene0.c a scenes\scene99.c qui existent
REM sont compilees et ajoutees automatiquement a LINK.RSP.
REM =========================================================
REM Necessite Open Watcom 1.9 avec wcc386/wlink dans le PATH,
REM et DOS32A.EXE + STUB32A.EXE accessibles depuis LINK.RSP.
REM LINK.RSP est GENERE par ce fichier a chaque execution.
REM
REM -3s  = instructions 386, convention d'appel "stack"
REM        (plus sure a marier avec les gestionnaires
REM        d'interruption ecrits a la main que -3r)
REM -mf  = modele memoire "flat" (obligatoire pour DOS/32A)
REM -os  = optimise pour la taille (comme le build 16 bits)
REM -I.  = cherche les .h dans le repertoire courant
REM =========================================================

REM ---- Generation de LINK.RSP (partie fixe) ----
> LINK.RSP echo # LINK.RSP genere par BUILD.BAT
>> LINK.RSP echo LIBPATH %%WATCOM%%\LIB386
>> LINK.RSP echo LIBPATH %%WATCOM%%\LIB386\DOS
>> LINK.RSP echo LIBPATH %%WATCOM%%\LIB386\L32
>> LINK.RSP echo OPTION STUB=stub32a.exe
>> LINK.RSP echo FORMAT OS2 LE
>> LINK.RSP echo.
>> LINK.RSP echo file main.obj
>> LINK.RSP echo file timer.obj
>> LINK.RSP echo file keyboard.obj
>> LINK.RSP echo file video.obj
>> LINK.RSP echo file palette.obj
>> LINK.RSP echo file graphics.obj
>> LINK.RSP echo file image.obj
>> LINK.RSP echo file sprite.obj
>> LINK.RSP echo file font1.obj
>> LINK.RSP echo file f1_8x8.obj
>> LINK.RSP echo file f1_8x16.obj
>> LINK.RSP echo file f1_16x16.obj
>> LINK.RSP echo file font2.obj
>> LINK.RSP echo file sblaster.obj
>> LINK.RSP echo file s3m.obj
>> LINK.RSP echo file wav.obj
>> LINK.RSP echo file audio.obj
>> LINK.RSP echo file scene.obj

REM ---- Compilation des modules fixes ----
wcc386 -3s -mf -os -I. main.c
if errorlevel 1 goto error
wcc386 -3s -mf -os -I. timer.c
if errorlevel 1 goto error
wcc386 -3s -mf -os -I. keyboard.c
if errorlevel 1 goto error
wcc386 -3s -mf -os -I. video.c
if errorlevel 1 goto error
wcc386 -3s -mf -os -I. palette.c
if errorlevel 1 goto error
wcc386 -3s -mf -os -I. graphics.c
if errorlevel 1 goto error
wcc386 -3s -mf -os -I. image.c
if errorlevel 1 goto error
wcc386 -3s -mf -os -I. sprite.c
if errorlevel 1 goto error
wcc386 -3s -mf -os -I. font1.c
if errorlevel 1 goto error
wcc386 -3s -mf -os -I. font1\f1_8x8.c
if errorlevel 1 goto error
wcc386 -3s -mf -os -I. font1\f1_8x16.c
if errorlevel 1 goto error
wcc386 -3s -mf -os -I. font1\f1_16x16.c
if errorlevel 1 goto error
wcc386 -3s -mf -os -I. font2.c
if errorlevel 1 goto error
wcc386 -3s -mf -os -I. sblaster.c
if errorlevel 1 goto error
wcc386 -3s -mf -os -I. s3m.c
if errorlevel 1 goto error
wcc386 -3s -mf -os -I. wav.c
if errorlevel 1 goto error
wcc386 -3s -mf -os -I. audio.c
if errorlevel 1 goto error
wcc386 -3s -mf -os -I. scene.c
if errorlevel 1 goto error

REM ---- Scenes : compile tout ce qui existe (0 a 99) ----
for %%i in ( 0  1  2  3  4  5  6  7  8  9) do if exist scenes\scene%%i.c wcc386 -3s -mf -os -I. scenes\scene%%i.c
for %%i in (10 11 12 13 14 15 16 17 18 19) do if exist scenes\scene%%i.c wcc386 -3s -mf -os -I. scenes\scene%%i.c
for %%i in (20 21 22 23 24 25 26 27 28 29) do if exist scenes\scene%%i.c wcc386 -3s -mf -os -I. scenes\scene%%i.c
for %%i in (30 31 32 33 34 35 36 37 38 39) do if exist scenes\scene%%i.c wcc386 -3s -mf -os -I. scenes\scene%%i.c
for %%i in (40 41 42 43 44 45 46 47 48 49) do if exist scenes\scene%%i.c wcc386 -3s -mf -os -I. scenes\scene%%i.c
for %%i in (50 51 52 53 54 55 56 57 58 59) do if exist scenes\scene%%i.c wcc386 -3s -mf -os -I. scenes\scene%%i.c
for %%i in (60 61 62 63 64 65 66 67 68 69) do if exist scenes\scene%%i.c wcc386 -3s -mf -os -I. scenes\scene%%i.c
for %%i in (70 71 72 73 74 75 76 77 78 79) do if exist scenes\scene%%i.c wcc386 -3s -mf -os -I. scenes\scene%%i.c
for %%i in (80 81 82 83 84 85 86 87 88 89) do if exist scenes\scene%%i.c wcc386 -3s -mf -os -I. scenes\scene%%i.c
for %%i in (90 91 92 93 94 95 96 97 98 99) do if exist scenes\scene%%i.c wcc386 -3s -mf -os -I. scenes\scene%%i.c

REM ---- Ajout des scenes dans LINK.RSP ----
for %%i in ( 0  1  2  3  4  5  6  7  8  9) do if exist scenes\scene%%i.c >> LINK.RSP echo file scene%%i.obj
for %%i in (10 11 12 13 14 15 16 17 18 19) do if exist scenes\scene%%i.c >> LINK.RSP echo file scene%%i.obj
for %%i in (20 21 22 23 24 25 26 27 28 29) do if exist scenes\scene%%i.c >> LINK.RSP echo file scene%%i.obj
for %%i in (30 31 32 33 34 35 36 37 38 39) do if exist scenes\scene%%i.c >> LINK.RSP echo file scene%%i.obj
for %%i in (40 41 42 43 44 45 46 47 48 49) do if exist scenes\scene%%i.c >> LINK.RSP echo file scene%%i.obj
for %%i in (50 51 52 53 54 55 56 57 58 59) do if exist scenes\scene%%i.c >> LINK.RSP echo file scene%%i.obj
for %%i in (60 61 62 63 64 65 66 67 68 69) do if exist scenes\scene%%i.c >> LINK.RSP echo file scene%%i.obj
for %%i in (70 71 72 73 74 75 76 77 78 79) do if exist scenes\scene%%i.c >> LINK.RSP echo file scene%%i.obj
for %%i in (80 81 82 83 84 85 86 87 88 89) do if exist scenes\scene%%i.c >> LINK.RSP echo file scene%%i.obj
for %%i in (90 91 92 93 94 95 96 97 98 99) do if exist scenes\scene%%i.c >> LINK.RSP echo file scene%%i.obj

>> LINK.RSP echo name demo.exe

REM ---- Edition de liens ----
wlink @LINK.RSP
if errorlevel 1 goto error

echo.
echo Build OK.
goto end

:error
echo.
echo *** ERREUR pendant la compilation ou l'edition de liens ***

:end
