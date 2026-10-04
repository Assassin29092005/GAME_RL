@echo off
rem HellwalkerRL - the GitHub Releases download: zips Build\Packaged\Windows (run Tools\Package.bat first) into parts under
rem GitHub's 2 GiB asset limit + Join-and-Extract.bat + SHA256SUMS.txt in Build\Packaged\Release. Upload every file there to a
rem release (gh release create v<version> Build\Packaged\Release\* --notes-file ...).
setlocal
"%~dp0..\RL\.venv\Scripts\python.exe" "%~dp0MakeRelease.py" || exit /b 1
