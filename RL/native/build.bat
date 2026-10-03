@echo off
rem HellwalkerRL - build the RL tools (no Unreal): RL\native\out\hwrl.dll (for Python, ctypes) and RL\native\out\envbench.exe.
rem Same compiler flags as Sim\build.bat, from the same engine-free core the game compiles, plus the classic reference
rem brain (Sim\Classic, the evaluation benchmark). Nothing here is compiled into the game.
setlocal
set VCVARS=C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat
if not exist "%VCVARS%" set VCVARS=D:\prism\visual studio\VC\Auxiliary\Build\vcvars64.bat
if not exist "%VCVARS%" for /f "usebackq delims=" %%I in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VCVARS=%%I\VC\Auxiliary\Build\vcvars64.bat"
if not exist "%VCVARS%" (echo cannot find vcvars64.bat - install the Visual Studio C++ build tools & exit /b 1)
call "%VCVARS%" >nul || exit /b 1
set ROOT=%~dp0..\..
set OUT=%~dp0out
if not "%HWRL_OUT%"=="" set OUT=%HWRL_OUT%
if not exist "%OUT%\obj" mkdir "%OUT%\obj"
set FLAGS=/nologo /std:c++20 /O2 /W4 /EHsc /fp:precise /permissive- /DHWRL_EXPORTS ^
   /we4456 /we4458 /we4459 /we4668 /we4702 /we4018 /we4101 /we4146 /we4703 /we4706 /we4715 /we4800 ^
   /I "%ROOT%\Source\HellwalkerRL\Public" /I "%ROOT%\Sim" /I "%~dp0."
cl %FLAGS% /c "%ROOT%\Source\HellwalkerRL\Private\HWCore\*.cpp" "%ROOT%\Sim\Classic\*.cpp" "%~dp0HWRLEnv.cpp" "%~dp0HWRLPlayer.cpp" "%~dp0hwrl_capi.cpp" "%~dp0envbench.cpp" /Fo"%OUT%\obj\\" || exit /b 1
pushd "%OUT%\obj"
set OBJS=
for %%F in (*.obj) do if /I not "%%F"=="envbench.obj" call set OBJS=%%OBJS%% "%%F"
link /nologo /DLL /OUT:"%OUT%\hwrl.dll" %OBJS% || (popd & exit /b 1)
link /nologo /OUT:"%OUT%\envbench.exe" %OBJS% envbench.obj || (popd & exit /b 1)
popd
echo built %OUT%\hwrl.dll and %OUT%\envbench.exe
exit /b 0
