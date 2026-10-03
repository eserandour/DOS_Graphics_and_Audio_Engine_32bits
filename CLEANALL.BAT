@echo off
del *.obj
del *.out
del *.err

if exist DOS32A.EXE ren DOS32A.EXE DOS32A.TMP
if exist STUB32A.EXE ren STUB32A.EXE STUB32A.TMP

del *.exe

if exist DOS32A.TMP ren DOS32A.TMP DOS32A.EXE
if exist STUB32A.TMP ren STUB32A.TMP STUB32A.EXE
