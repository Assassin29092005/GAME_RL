@echo off
rem Hellwalker - build the editor target (close the editor first: new UPROPERTY/UFUNCTION need a full rebuild, PLAN §7).
setlocal
set UE=D:\Shadow\Epic Games\UE_5.8
call "%UE%\Engine\Build\BatchFiles\Build.bat" HellwalkerRLEditor Win64 Development -Project="%~dp0..\HellwalkerRL.uproject" -WaitMutex
exit /b %ERRORLEVEL%
