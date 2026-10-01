@echo off
echo === CodeEditor probe build ===
if not exist main.cpp goto nocwd
set FLTK=vendor\fltk-1.3\FL
set DC=C:\Dev-Cpp\bin
if not exist %FLTK%\lib\fltkok.tag goto nofltk
%DC%\g++.exe -DWIN32 -DWINVER=0x0500 -D_WIN32_WINNT=0x0500 -I. -Ieditor -I%FLTK% -O2 -c editor\codetest.cpp -o raw\obj\c98.o
if errorlevel 1 goto error
%DC%\g++.exe raw\obj\c98.o -o c98.exe -L%FLTK%\lib -lfltk -lole32 -luuid -lcomctl32 -lcomdlg32 -lgdi32 -lwsock32
if errorlevel 1 goto error
echo.
echo === Built c98.exe - run:  c98            (built-in demo) ===
echo ===                       c98 file.lua   (open a file)   ===
goto end
:nofltk
echo ERROR: run fltk98.bat first.
goto error
:nocwd
echo ERROR: run this from the repo root.
goto error
:error
echo.
echo === PROBE BUILD FAILED ===
pause
:end
