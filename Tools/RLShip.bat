@echo off
rem HellwalkerRL - ship a trained policy into the game: copies <policy.hwrl> to Content\HellwalkerRL\RL\hellwalker_rl.hwrl
rem (the file the game loads, staged into packaged builds as a loose file). The game and ThesisSim read that copy.
rem   RLShip.bat RL\checkpoints\reader\policy_best.hwrl
setlocal
set ROOT=%~dp0..
if "%~1"=="" (echo usage: RLShip.bat ^<policy.hwrl^> & exit /b 1)
if not exist "%~1" (echo no such file: %~1 & exit /b 1)
if not exist "%ROOT%\Content\HellwalkerRL\RL" mkdir "%ROOT%\Content\HellwalkerRL\RL"
copy /Y "%~1" "%ROOT%\Content\HellwalkerRL\RL\hellwalker_rl.hwrl" >nul || exit /b 1
echo shipped %~1 -^> Content\HellwalkerRL\RL\hellwalker_rl.hwrl
exit /b 0
