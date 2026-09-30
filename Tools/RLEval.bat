@echo off
rem HellwalkerRL - evaluate an RL keeper against RL.md section 7: the B0 checks (ThesisSim --brain rl), the adaptation
rem curve on held-out habit players, not-a-bully and classic vs RL on identical seeded players.
rem   RLEval.bat                                                      the shipped model
rem   RLEval.bat --hwrl RL\checkpoints\rl2_reader\latest.hwrl --ckpt RL\checkpoints\rl2_reader\latest.pt --name reader
rem   RLEval.bat --only b0,arms                                       a subset (b0 | curve | arms)
rem Report: RL\reports\<name>.md / .json
setlocal
set ROOT=%~dp0..
set TMP=%ROOT%\RL\.pip-tmp
set TEMP=%ROOT%\RL\.pip-tmp
if not exist "%ROOT%\RL\native\out\hwrl.dll" call "%~dp0RLBuild.bat" || exit /b 1
set ARGS=%*
if "%~1"=="" set ARGS=--hwrl "%ROOT%\Content\HellwalkerRL\RL\hellwalker_rl.hwrl" --only b0,arms --name shipped
"%ROOT%\RL\.venv\Scripts\python.exe" "%ROOT%\RL\eval.py" %ARGS%
exit /b %ERRORLEVEL%
