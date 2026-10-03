@echo off
rem HellwalkerRL - upload the packaged game to itch.io with butler (https://itch.io/docs/butler/).
rem   ItchPush.bat <itch user> <game>              e.g. ItchPush.bat yourname hellwalker
rem   ItchPush.bat <itch user> <game> <version>    tag the build (shown on the itch.io page)
rem Build the game first (Tools\Package.bat) and log in once with "butler login". Pushes Build\Packaged\Windows to the
rem "windows" channel; later pushes upload only what changed.
setlocal
if "%~2"=="" (echo usage: ItchPush.bat ^<itch user^> ^<game^> [version] & exit /b 1)
set ROOT=%~dp0..
if not exist "%ROOT%\Build\Packaged\Windows\HellwalkerRL.exe" (echo no packaged game: run Tools\Package.bat first & exit /b 1)
where butler >nul 2>nul || (echo butler not found: install it from https://itch.io/docs/butler/ and add it to PATH & exit /b 1)
if "%~3"=="" (
	butler push "%ROOT%\Build\Packaged\Windows" %~1/%~2:windows
) else (
	butler push "%ROOT%\Build\Packaged\Windows" %~1/%~2:windows --userversion %~3
)
exit /b %ERRORLEVEL%
