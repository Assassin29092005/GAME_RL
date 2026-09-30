@echo off
rem HellwalkerRL - train the RL keeper (RL.md). Arguments pass through to RL\train.py, e.g.
rem   RLTrain.bat --stage rl1 --run sanity                    feed-forward vs one habitual bot (minutes)
rem   RLTrain.bat --stage rl2 --run reader                    the recurrent reader vs the population (hours)
rem   RLTrain.bat --stage rl2 --run reader --resume           continue a run
rem Logs: RL\runs\<run> (TensorBoard: RL\.venv\Scripts\tensorboard --logdir RL\runs). Checkpoints: RL\checkpoints\<run>.
setlocal
set ROOT=%~dp0..
rem The C: drive is nearly full: keep Python's scratch on D:.
set TMP=%ROOT%\RL\.pip-tmp
set TEMP=%ROOT%\RL\.pip-tmp
if not exist "%ROOT%\RL\native\out\hwrl.dll" call "%~dp0RLBuild.bat" || exit /b 1
"%ROOT%\RL\.venv\Scripts\python.exe" "%ROOT%\RL\train.py" %*
exit /b %ERRORLEVEL%
