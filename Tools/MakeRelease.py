"""HellwalkerRL - the GitHub Releases download (Tools\MakeRelease.bat). Zips Build/Packaged/Windows (Tools\Package.bat) into
HellwalkerRL-<ProjectVersion>-Windows.zip (folder HellwalkerRL/ inside), splits it into parts under GitHub's 2 GiB asset
limit, writes Join-and-Extract.bat (joins the parts with copy /b, unpacks with Windows' tar) and SHA256SUMS.txt.
Output: Build/Packaged/Release/ (gitignored) - upload every file there to a release (README "Play")."""
import hashlib
import os
import sys
import time
import zipfile

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
SRC = os.path.join(ROOT, "Build", "Packaged", "Windows")
OUT = os.path.join(ROOT, "Build", "Packaged", "Release")
VER = "1.0.0"
with open(os.path.join(ROOT, "Config", "DefaultGame.ini"), encoding="utf-8") as _ini:
    for _line in _ini:
        if _line.startswith("ProjectVersion="):
            VER = _line.split("=", 1)[1].strip()
NAME = f"HellwalkerRL-{VER}-Windows.zip"
PART = 2_000_000_000  # bytes; GitHub: each release asset must be under 2 GiB (2,147,483,648)
STORED_EXT = {".pak", ".ucas", ".utoc", ".jpg", ".png", ".ogg", ".mp4", ".bik"}  # already compressed

os.makedirs(OUT, exist_ok=True)
for f in os.listdir(OUT):
    os.remove(os.path.join(OUT, f))
zpath = os.path.join(OUT, NAME)
t0 = time.time()
files = []
for d, _, fs in os.walk(SRC):
    for f in fs:
        files.append(os.path.join(d, f))
total = sum(os.path.getsize(f) for f in files)
print(f"{len(files)} files, {total / 1e9:.2f} GB -> {zpath}", flush=True)
with zipfile.ZipFile(zpath, "w", allowZip64=True) as z:
    for f in files:
        arc = os.path.join("HellwalkerRL", os.path.relpath(f, SRC))
        ext = os.path.splitext(f)[1].lower()
        ct = zipfile.ZIP_STORED if ext in STORED_EXT or os.path.getsize(f) > 200_000_000 else zipfile.ZIP_DEFLATED
        z.write(f, arc, compress_type=ct, compresslevel=6 if ct == zipfile.ZIP_DEFLATED else None)
zsize = os.path.getsize(zpath)
print(f"zip {zsize / 1e9:.2f} GB in {time.time() - t0:.0f} s", flush=True)

# Verify the archive's directory before splitting.
with zipfile.ZipFile(zpath) as z:
    names = z.namelist()
    assert len(names) == len(files), (len(names), len(files))
    assert "HellwalkerRL/HellwalkerRL.exe" in names

# Split, hashing the whole and every part.
whole = hashlib.sha256()
parts = []
with open(zpath, "rb") as src:
    i = 0
    while True:
        i += 1
        pname = f"{NAME}.{i:03d}"
        h = hashlib.sha256()
        n = 0
        with open(os.path.join(OUT, pname), "wb") as dst:
            while n < PART:
                buf = src.read(min(64 * 1024 * 1024, PART - n))
                if not buf:
                    break
                dst.write(buf)
                h.update(buf)
                whole.update(buf)
                n += len(buf)
        if n == 0:
            os.remove(os.path.join(OUT, pname))
            break
        parts.append((pname, n, h.hexdigest()))
        if n < PART:
            break
os.remove(zpath)
assert all(sz < 2 ** 31 for _, sz, _ in parts)

with open(os.path.join(OUT, "SHA256SUMS.txt"), "w", newline="\r\n") as f:
    for pname, _, hx in parts:
        f.write(f"{hx}  {pname}\n")
    f.write(f"{whole.hexdigest()}  {NAME}\n")

joined = "+".join(f'"%NAME%.{i + 1:03d}"' for i in range(len(parts)))
checks = " ".join(f"{i + 1:03d}" for i in range(len(parts)))
bat = f"""@echo off
rem HellwalkerRL {VER} - joins the downloaded parts into one zip and unpacks the game next to this file.
rem Put this file and ALL {len(parts)} parts ({NAME}.001 ... .{len(parts):03d}) in the same folder, then double-click it.
rem Needs about 16 GB of free space while it works (the game itself is 7.4 GB). Uses tar.exe, built into Windows 10 and 11.
setlocal
cd /d "%~dp0"
set NAME={NAME}
for %%P in ({checks}) do if not exist "%NAME%.%%P" (
	echo Missing %NAME%.%%P - download every part into this folder first.
	pause
	exit /b 1
)
echo Joining the {len(parts)} parts...
copy /b {joined} "%NAME%" >nul
if errorlevel 1 (
	echo Joining failed - is there enough free disk space?
	pause
	exit /b 1
)
echo Unpacking the game - this takes a few minutes...
tar -xf "%NAME%"
if errorlevel 1 (
	echo Unpacking failed. You can also open %NAME%.001 with 7-Zip and extract it.
	pause
	exit /b 1
)
del "%NAME%"
echo.
echo Done. The game is in the HellwalkerRL folder: run HellwalkerRL.exe.
echo (Windows may say "Windows protected your PC": choose More info, then Run anyway.)
echo You can delete the downloaded parts now.
start "" "%~dp0HellwalkerRL"
pause
"""
with open(os.path.join(OUT, "Join-and-Extract.bat"), "w", newline="\r\n") as f:
    f.write(bat)

for pname, sz, hx in parts:
    print(f"{pname}  {sz / 1e9:.3f} GB  {hx[:16]}")
print(f"whole {whole.hexdigest()[:16]}  total {time.time() - t0:.0f} s")
