@echo off
echo === FLTK probe build ===
if not exist main.cpp goto nocwd
set FLTK=..\SOOB-Core\vendor\fltk-1.3\FL
set DC=C:\Dev-Cpp\bin
REM  FLTK lives in ..\SOOB-Core (not 8.3), so no "if exist" check here.
%DC%\g++.exe -DWIN32 -DWINVER=0x0500 -D_WIN32_WINNT=0x0500 -I%FLTK% -O2 -c editor\fltktest.cpp -o raw\obj\t98.o
if errorlevel 1 goto error
%DC%\g++.exe raw\obj\t98.o -o t98.exe -L%FLTK%\lib -lfltk_gl -lfltk -lopengl32 -lglu32 -lole32 -luuid -lcomctl32 -lcomdlg32 -lgdi32 -lwsock32
if errorlevel 1 goto error
echo.
echo === Built t98.exe - run:  t98 ^> probe.txt ===
goto end
:nofltk
echo ERROR: run fltk98.bat in SOOB-Core first.
goto error
:nocwd
echo ERROR: run this from the repo root.
goto error
:error
echo.
echo === PROBE BUILD FAILED ===
pause
:end
