@echo off
rem Hellwalker - the arena duel on its own (the B4 blind test, tuning, quick fights): the Warden only,
rem start screen with tier choice. Extra arguments pass through (-HWTier=, -HWBlind, -HWDebug, -HWBoss=Wukong).
setlocal
set UE=D:\Shadow\Epic Games\UE_5.8
start "" "%UE%\Engine\Binaries\Win64\UnrealEditor.exe" "%~dp0..\HellwalkerRL.uproject" /Game/HellwalkerRL/Maps/L_Arena -game -windowed -ResX=1600 -ResY=900 %*
