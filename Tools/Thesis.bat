@echo off
rem Hellwalker - B0: build and run the headless thesis simulator (no Unreal).
setlocal
call "%~dp0..\Sim\build.bat" || exit /b 1
"%~dp0..\Sim\out\ThesisSim.exe" --tests || exit /b 1
"%~dp0..\Sim\out\ThesisSim.exe" --sessions 16 %*
exit /b %ERRORLEVEL%
