@echo off
rem Hellwalker - watch a simulated player (the B0 instrument) fight the Warden, 5 encounters back to back
rem (the Warden keeps what it learns across them; F3 shows its reasoning live).
rem   Demo.bat [masher|turtle|habitual|varied|dodger|rhythm] [skill 0-1] [pathbreaker|hellwalker]
setlocal
set KIND=%1
if "%KIND%"=="" set KIND=habitual
set SKILL=%2
if "%SKILL%"=="" set SKILL=0.8
set TIER=%3
if "%TIER%"=="" set TIER=Hellwalker
set UE=D:\Shadow\Epic Games\UE_5.8
start "" "%UE%\Engine\Binaries\Win64\UnrealEditor.exe" "%~dp0..\HellwalkerRL.uproject" /Game/HellwalkerRL/Maps/L_Arena -game -windowed -ResX=1600 -ResY=900 -HWAutoStart -HWTier=%TIER% -HWAutoplay=%KIND% -HWAutoplaySkill=%SKILL% -HWEncounters=5 -HWDebug
