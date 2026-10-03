@echo off
rem HellwalkerRL - the Unreal-vs-simulator gap (RL\parity.py): the same autoplay players against the same keeper in the
rem real game (the arena, -HWParity, one launch per session) and in the simulator it was trained in, compared metric by
rem metric with bootstrap intervals. Build the game first (Tools\Build.bat) and close the editor.
rem   Parity.bat                                                   RL keeper (shipped) + script vs rhythm 0.7 / habitual 0.7 / varied 0.5
rem   Parity.bat --bots turtle:0.7 --arms rl --sessions 8          one cell, more sessions
rem   Parity.bat --ue-arg=-HWNoHitstop --name parity_nohitstop      an experiment (also -HWMoveAccel= -HWMoveBraking=)
rem   Parity.bat --compare-only                                    re-analyse the saved records (RL\reports\parity_raw)
rem Report: RL\reports\parity.md / .json
setlocal
set ROOT=%~dp0..
set TMP=%ROOT%\RL\.pip-tmp
set TEMP=%ROOT%\RL\.pip-tmp
if not exist "%ROOT%\RL\native\out\hwrl.dll" call "%~dp0RLBuild.bat" || exit /b 1
"%ROOT%\RL\.venv\Scripts\python.exe" "%ROOT%\RL\parity.py" %*
exit /b %ERRORLEVEL%
