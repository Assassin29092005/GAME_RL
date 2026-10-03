@echo off
rem HellwalkerRL - build the B0 thesis simulator (no Unreal). Compiles the game module's engine-free core, the
rem classic reference brain (Sim\Classic, tools only) and the simulator. Warnings Unreal treats as errors are errors
rem here too, so the core cannot drift into code UE rejects.
setlocal
set VCVARS=C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat
if not exist "%VCVARS%" set VCVARS=D:\prism\visual studio\VC\Auxiliary\Build\vcvars64.bat
if not exist "%VCVARS%" for /f "usebackq delims=" %%I in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VCVARS=%%I\VC\Auxiliary\Build\vcvars64.bat"
if not exist "%VCVARS%" (echo cannot find vcvars64.bat - install the Visual Studio C++ build tools & exit /b 1)
call "%VCVARS%" >nul || exit /b 1
set ROOT=%~dp0..
if not exist "%~dp0out" mkdir "%~dp0out"
cl /nologo /std:c++20 /O2 /W4 /EHsc /fp:precise /permissive- ^
   /we4456 /we4458 /we4459 /we4668 /we4702 /we4018 /we4101 /we4146 /we4703 /we4706 /we4715 /we4800 ^
   /I "%ROOT%\Source\HellwalkerRL\Public" /I "%~dp0." ^
   "%~dp0ThesisSim.cpp" "%~dp0SimArms.cpp" "%~dp0Classic\*.cpp" "%ROOT%\Source\HellwalkerRL\Private\HWCore\*.cpp" ^
   /Fo"%~dp0out\\" /Fe"%~dp0out\ThesisSim.exe"
exit /b %ERRORLEVEL%
