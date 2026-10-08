@echo off
call "C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvars64.bat" >nul
cd /d "%~dp0"
cl /std:c++20 /EHsc /utf-8 /W4 /nologo forward_demo.cpp /Fe:forward_demo.exe
if errorlevel 1 exit /b 1
forward_demo.exe
