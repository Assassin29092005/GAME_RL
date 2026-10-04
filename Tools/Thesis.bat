@echo off
rem HellwalkerRL - B0: build and run the headless thesis simulator (no Unreal): every core test, then the B0 sweep.
rem The adaptive arm is the RL keeper (the shipped model) when it exists, else the classic reference brain. Extra
rem arguments pass through, e.g.  Thesis.bat --sessions 16   |   Thesis.bat --brain classic   |   --script 1 (the Sage)
rem   |   --keeper-damage 0.75 (the difficulty's damage scale, both arms).
rem Thesis.bat --arc [...]: the Adaptive AI learning arc instead of B0 (FRLInsight against habit players, switchers and
rem near-random players; 64 sessions of 6 lethal fights per player): --arc-lo / --arc-hi / --keeper-damage / --identity.
setlocal
set ROOT=%~dp0..
set POLICY=%ROOT%\Content\HellwalkerRL\RL\hellwalker_rl.hwrl
call "%ROOT%\Sim\build.bat" || exit /b 1
"%ROOT%\Sim\out\ThesisSim.exe" --tests || exit /b 1
if "%~1"=="--arc" (
	"%ROOT%\Sim\out\ThesisSim.exe" --policy "%POLICY%" %*
) else if exist "%POLICY%" (
	"%ROOT%\Sim\out\ThesisSim.exe" --brain rl --policy "%POLICY%" --sessions 16 %*
) else (
	echo no shipped RL model yet - running B0 on the classic reference brain
	"%ROOT%\Sim\out\ThesisSim.exe" --sessions 16 %*
)
exit /b %ERRORLEVEL%
