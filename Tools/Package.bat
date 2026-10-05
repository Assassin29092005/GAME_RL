@echo off
rem HellwalkerRL - package the standalone game: Build\Packaged\Windows\HellwalkerRL.exe (Win64).
rem   Package.bat                 Shipping (the final product)
rem   Package.bat Development     with logs and the console (hw.* commands, F3 overlay debugging)
rem Close the editor first. The RL keeper's weights (Content\HellwalkerRL\RL\hellwalker_rl.hwrl) are staged as a loose
rem file next to the paks (DirectoriesToAlwaysStageAsNonUFS); the pack folders the game loads by path are listed in
rem Config\DefaultGame.ini (DirectoriesToAlwaysCook), because nothing references them from a map.
rem -prereqs stages the Visual C++ runtime installer (Engine\Extras\Redist): HellwalkerRL.exe runs it on a PC without the
rem runtime, which otherwise cannot start the game. The old Build\Packaged\Windows is removed first: -archive copies over
rem it, so a Development exe, PDBs and a test run's Saved folder would end up in the release (Tools\MakeRelease.bat).
setlocal
set UE=D:\Shadow\Epic Games\UE_5.8
set CONFIG=%~1
if "%CONFIG%"=="" set CONFIG=Shipping
if exist "%~dp0..\Build\Packaged\Windows" rmdir /s /q "%~dp0..\Build\Packaged\Windows"
call "%UE%\Engine\Build\BatchFiles\RunUAT.bat" BuildCookRun -project="%~dp0..\HellwalkerRL.uproject" -noP4 -platform=Win64 ^
   -clientconfig=%CONFIG% -build -cook -stage -pak -iostore -prereqs -archive -archivedirectory="%~dp0..\Build\Packaged" -utf8output -unattended
exit /b %ERRORLEVEL%
