@ECHO OFF
REM BUILD.BAT - Build RetroGun on DOS using DJGPP
REM
REM Requirements:
REM   DJGPP 2.x installed and in PATH
REM   CWSDPMI.EXE in the same directory or PATH

ECHO Building RetroGun...
gcc -O2 -Wall -o RETROGN.EXE src\RETROGN.C
IF ERRORLEVEL 1 (
    ECHO Build FAILED.
    EXIT /B 1
)
ECHO Build successful: RETROGN.EXE
ECHO Run RETROGN.EXE to play.
