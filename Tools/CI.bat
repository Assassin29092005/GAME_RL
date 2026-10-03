@echo off
rem HellwalkerRL - the GitHub Actions CI (.github\workflows\ci.yml) on a dev machine: the engine-free core, the training
rem environment and the network parity, without Unreal. Builds into separate folders so a training run that holds
rem RL\native\out\hwrl.dll is never disturbed. Exit 0 = every step passed.
setlocal
set ROOT=%~dp0..
set PY=%ROOT%\RL\.venv\Scripts\python.exe
if not exist "%PY%" set PY=python
set TMP=%ROOT%\RL\.pip-tmp
set TEMP=%ROOT%\RL\.pip-tmp
set HWRL_OUT=%ROOT%\RL\.pip-tmp\ci_out
set HWRL_DLL=%HWRL_OUT%\hwrl.dll

echo === [1/7] ThesisSim build
call "%ROOT%\Sim\build.bat" || goto :fail
echo === [2/7] core tests
"%ROOT%\Sim\out\ThesisSim.exe" --tests || goto :fail
echo === [3/7] hwrl.dll + envbench (into %HWRL_OUT%)
call "%ROOT%\RL\native\build.bat" || goto :fail
echo === [4/7] environment benchmark
"%HWRL_OUT%\envbench.exe" 1024 300 "%ROOT%\Content\HellwalkerRL\RL\hellwalker_rl.hwrl" || goto :fail
echo === [5/7] policy harness
call "%ROOT%\RL\native\build_harness.bat" || goto :fail
echo === [6/7] network parity
"%PY%" "%ROOT%\RL\tests\test_parity.py" || goto :fail
echo === [7/7] trainer self-tests
"%PY%" "%ROOT%\RL\ppo_rnn.py" --selftest --device cpu || goto :fail
"%PY%" "%ROOT%\RL\env.py" || goto :fail
"%PY%" "%ROOT%\RL\players.py" || goto :fail
echo CI: PASS
exit /b 0
:fail
echo CI: FAIL
exit /b 1
