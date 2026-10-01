@echo off
echo === SOOB Code Editor build ===
if not exist main.cpp goto nocwd
set FLTK=vendor\fltk-1.3\FL
set DC=C:\Dev-Cpp\bin
if not exist %FLTK%\lib\fltkok.tag goto nofltk
%DC%\g++.exe -DWIN32 -DWINVER=0x0500 -D_WIN32_WINNT=0x0500 -I. -Ieditor -I%FLTK% -O2 -c editor\codeedit.cpp -o raw\obj\e98.o
if errorlevel 1 goto error
%DC%\g++.exe raw\obj\e98.o -o codeedit.exe -L%FLTK%\lib -lfltk -lole32 -luuid -lcomctl32 -lcomdlg32 -lgdi32 -lwsock32
if errorlevel 1 goto error
echo.
echo === Built codeedit.exe - run:  codeedit                      ===
echo ===                            codeedit scripts\main.lua     ===
goto end
:nofltk
echo ERROR: run fltk98.bat first.
goto error
:nocwd
echo ERROR: run this from the repo root.
goto error
:error
echo.
echo === BUILD FAILED ===
pause
:end
