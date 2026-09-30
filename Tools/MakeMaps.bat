@echo off
rem Hellwalker - (re)write the project's two maps: /Game/HellwalkerRL/Maps/L_Hellwalker (the open world, default)
rem and L_Arena (the duel alone). They hold only a game-mode override; everything else is built in C++ at play.
setlocal
set UE=D:\Shadow\Epic Games\UE_5.8
"%UE%\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "%~dp0..\HellwalkerRL.uproject" -run=HWMakeMaps -unattended -nosplash -nopause
exit /b %ERRORLEVEL%
