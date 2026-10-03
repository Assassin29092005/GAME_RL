@echo off
rem HellwalkerRL - build the policy parity harness (tools only): RL\native\out_harness\policy_harness.exe.
rem It links the game's own forward pass (HWCore\HWRLPolicy.cpp and the few core files it needs) so RL\tests\test_parity.py
rem compares PyTorch against the exact code FRLBrain runs. Same flags as Sim\build.bat: the warnings Unreal treats as
rem errors are errors here too.
setlocal
set VCVARS=C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat
if not exist "%VCVARS%" set VCVARS=D:\prism\visual studio\VC\Auxiliary\Build\vcvars64.bat
if not exist "%VCVARS%" for /f "usebackq delims=" %%I in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VCVARS=%%I\VC\Auxiliary\Build\vcvars64.bat"
if not exist "%VCVARS%" (echo cannot find vcvars64.bat - install the Visual Studio C++ build tools & exit /b 1)
call "%VCVARS%" >nul || exit /b 1
set ROOT=%~dp0..\..
set CORE=%ROOT%\Source\HellwalkerRL\Private\HWCore
set OUT=%~dp0out_harness
if not exist "%OUT%" mkdir "%OUT%"
cl /nologo /std:c++20 /O2 /W4 /EHsc /fp:precise /permissive- ^
   /we4456 /we4458 /we4459 /we4668 /we4702 /we4018 /we4101 /we4146 /we4703 /we4706 /we4715 /we4800 ^
   /I "%ROOT%\Source\HellwalkerRL\Public" ^
   "%~dp0policy_harness.cpp" "%CORE%\HWRLPolicy.cpp" "%CORE%\HWRLTypes.cpp" "%CORE%\HWMoves.cpp" "%CORE%\HWTypes.cpp" ^
   /Fo"%OUT%\\" /Fe"%OUT%\policy_harness.exe"
exit /b %ERRORLEVEL%
