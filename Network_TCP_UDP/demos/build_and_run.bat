@echo off
REM Build and run all Windows demos. Run from "x64 Native Tools Command Prompt for VS 2022",
REM or this script will try to call vcvars64.bat automatically.
where cl >nul 2>nul
if errorlevel 1 call "C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvars64.bat"
cd /d "%~dp0"
for %%f in (d1_tcp_udp_behaviors d2_rudp_sim) do (
  cl /nologo /std:c++20 /EHsc /O2 /utf-8 %%f.cpp >nul
  echo ===== %%f =====
  %%f.exe
  echo.
)
del *.obj 2>nul
pause
