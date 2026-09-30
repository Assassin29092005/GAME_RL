@echo off
rem HellwalkerRL - build the RL tools: RL\native\out\hwrl.dll (the training environment + C++ forward pass for Python)
rem and Sim\out\ThesisSim.exe (B0), both from the same engine-free core the game compiles. No Unreal.
setlocal
call "%~dp0..\RL\native\build.bat" || exit /b 1
call "%~dp0..\Sim\build.bat" || exit /b 1
echo RL tools built: RL\native\out\hwrl.dll, Sim\out\ThesisSim.exe
exit /b 0
