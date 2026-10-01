@echo off
setlocal

echo === SOOB Code Editor - Win10 MinGW Build ===
echo.

REM ----------------------------------------------------------------
REM  Win10 twin of e98.bat. Same source -- only the toolchain differs:
REM  the portable WinLibs i686 MinGW under ..\SOOB-Core\vendor_win10\
REM  instead of C:\Dev-Cpp.
REM
REM  Run from the REPO ROOT.
REM
REM  Prerequisite: run build_fltk_win10.bat FIRST -- it produces the
REM  static libs in %FLTK%\lib_w10 (a separate dir from the Dev-C++
REM  build's lib\, since the two toolchains' archives are not ABI
REM  compatible).
REM ----------------------------------------------------------------

set "ENGINE=..\SOOB-Core"
set "GPP=%ENGINE%\vendor_win10\mingw32\bin\g++.exe"
set "FLTK=vendor\fltk-1.3\FL"
set "OBJDIR=raw\obj"

if not exist main.cpp (
    echo ERROR: run this from the repo root.
    goto error
)
if not exist "%GPP%" (
    echo ERROR: MinGW not found at %GPP%
    echo Download WinLibs i686 and extract to %ENGINE%\vendor_win10\mingw32\
    echo https://github.com/brechtsanders/winlibs_mingw/releases
    goto error
)
if not exist "%FLTK%\lib_w10\libfltk.a" (
    echo ERROR: FLTK libraries not built. Run build_fltk_win10.bat first.
    goto error
)

if not exist "%OBJDIR%" mkdir "%OBJDIR%"

echo Compiling code editor...
%GPP% -DWIN32 -DWINVER=0x0500 -D_WIN32_WINNT=0x0500 -I. -Ieditor -I%FLTK% -O2 -c editor\codeedit.cpp -o %OBJDIR%\e10.o
if errorlevel 1 goto error

REM ----------------------------------------------------------------
REM  Link: FLTK core + Win32. -static-libgcc/-static-libstdc++ keeps
REM  the exe free of MinGW runtime DLLs, same as the game's Win10 build.
REM ----------------------------------------------------------------
echo Linking...
%GPP% %OBJDIR%\e10.o -o codeedit_w10.exe -L%FLTK%\lib_w10 -lfltk -lole32 -luuid -lcomctl32 -lcomdlg32 -lgdi32 -lwinspool -lwsock32 -static-libgcc -static-libstdc++
if errorlevel 1 goto error

echo.
echo === Built codeedit_w10.exe - run:  codeedit_w10                      ===
echo ===                                codeedit_w10 scripts\main.lua     ===
goto end

:error
echo.
echo === BUILD FAILED ===
pause

:end
endlocal
