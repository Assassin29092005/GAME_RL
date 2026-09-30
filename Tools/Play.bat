@echo off
rem Hellwalker - the open world (title screen: new game / continue). Extra arguments pass through, e.g.
rem   Play.bat -HWWorldMode=Hellwalker      skip the title (pathbreaker | hellwalker | 66)
rem   Play.bat -HWContinue                  continue the saved walk
rem   Play.bat -HWDebug                     A2 overlay + hit volumes (F3 toggles it in game)
rem   Play.bat -HWBoss=Wukong               the Warden as Paragon Wukong (default: Sevarog)
rem   Play.bat -HWGreybox                   the greybox fighters even when the Fab packs are present
setlocal
set UE=D:\Shadow\Epic Games\UE_5.8
start "" "%UE%\Engine\Binaries\Win64\UnrealEditor.exe" "%~dp0..\HellwalkerRL.uproject" -game -windowed -ResX=1600 -ResY=900 %*
