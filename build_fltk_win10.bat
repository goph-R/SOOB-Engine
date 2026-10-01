@echo off
setlocal

echo === FLTK 1.3.11 static libs for the SOOB editor (Win10 / WinLibs MinGW) ===
echo.

REM ----------------------------------------------------------------
REM  Win10 twin of build_fltk.bat. Same source lists, same layout --
REM  only the toolchain differs: the portable WinLibs i686 MinGW under
REM  ..\SOOB-Core\vendor_win10\ instead of C:\Dev-Cpp.
REM
REM  Objects go to raw\obj\fltk_w10 and libs to %FLROOT%\lib_w10, so a
REM  Win98 (Dev-C++) build and a Win10 build can coexist in one checkout
REM  without clobbering each other's ABI-incompatible archives.
REM  Delete raw\obj\fltk_w10 to force a full rebuild.
REM ----------------------------------------------------------------

set "ENGINE=..\SOOB-Core"
set "GXX=%ENGINE%\vendor_win10\mingw32\bin\g++.exe"
set "GCC=%ENGINE%\vendor_win10\mingw32\bin\gcc.exe"
set "AR=%ENGINE%\vendor_win10\mingw32\bin\ar.exe"
set "FLROOT=vendor\fltk-1.3\FL"
set "LISTDIR=vendor\fltk-1.3"
set "OBJ=raw\obj\fltk_w10"
set "LIBDIR=%FLROOT%\lib_w10"

if not exist "%GXX%" (
    echo ERROR: MinGW not found at %GXX%
    echo Download WinLibs i686 and extract to %ENGINE%\vendor_win10\mingw32\
    echo https://github.com/brechtsanders/winlibs_mingw/releases
    goto error
)
if not exist "%FLROOT%\FL\Fl.H" (
    echo ERROR: FLTK source not found. Expected %FLROOT%\FL\Fl.H
    goto error
)
if not exist "%FLROOT%\config.h" (
    echo ERROR: %FLROOT%\config.h missing ^(should ship with the editor^).
    goto error
)

if exist "%LIBDIR%\libfltk_gl.a" (
    echo Libraries already built ^(delete raw\obj\fltk_w10 to force a rebuild^).
    goto done
)

if not exist "%OBJ%\c" mkdir "%OBJ%\c"
if not exist "%OBJ%\g" mkdir "%OBJ%\g"
if not exist "%LIBDIR%" mkdir "%LIBDIR%"

REM  WINVER/_WIN32_WINNT stay at 0x0500 to match the Win98 build exactly --
REM  see the long comment in build_fltk.bat for why 0x0400 does not compile.
REM  -std=gnu++98: FLTK 1.3.x is C++98 and uses constructs (notably `register`)
REM  that modern GCC rejects at its default -std=gnu++17.
REM  -fpermissive: 1.3.x has a few narrowing/const-cast sites that were legal
REM  under GCC 3.4's front end and are hard errors on GCC 15.
set "CXXFLAGS=-O2 -w -std=gnu++98 -fpermissive -DWIN32 -DWINVER=0x0500 -D_WIN32_WINNT=0x0500 -DFL_LIBRARY -I%FLROOT% -I%FLROOT%\src"
set "CFLAGS=-O2 -w -std=gnu89 -DWIN32 -DWINVER=0x0500 -D_WIN32_WINNT=0x0500 -DFL_LIBRARY -I%FLROOT% -I%FLROOT%\src"

REM  .c files must be compiled as C (gcc), not C++ (g++): FLTK's UTF-16 code
REM  passes `unsigned short*` to the ...W() APIs, valid in C, invalid in C++.
echo Compiling FLTK core (this takes a while)...
for /f %%f in (%LISTDIR%\fltk_core.list) do (
    if /I "%%~xf"==".c" (
        %GCC% %CFLAGS% -c "%FLROOT%/src/%%f" -o "%OBJ%\c\%%~nf.o"
    ) else (
        %GXX% %CXXFLAGS% -c "%FLROOT%/src/%%f" -o "%OBJ%\c\%%~nf.o"
    )
    if errorlevel 1 (echo   FAILED: %%f & goto error)
)

echo Compiling FLTK GL...
for /f %%f in (%LISTDIR%\fltk_gl.list) do (
    %GXX% %CXXFLAGS% -c "%FLROOT%/src/%%f" -o "%OBJ%\g\%%~nf.o"
    if errorlevel 1 (echo   FAILED: %%f & goto error)
)

echo Archiving libfltk.a ...
if exist "%LIBDIR%\libfltk.a" del "%LIBDIR%\libfltk.a"
for %%o in (%OBJ%\c\*.o) do %AR% rcs "%LIBDIR%\libfltk.a" "%%o"

echo Archiving libfltk_gl.a ...
if exist "%LIBDIR%\libfltk_gl.a" del "%LIBDIR%\libfltk_gl.a"
for %%o in (%OBJ%\g\*.o) do %AR% rcs "%LIBDIR%\libfltk_gl.a" "%%o"

:done
echo.
echo === FLTK ready: %LIBDIR%\libfltk.a + libfltk_gl.a ===
echo Now run build_editor_win10.bat.
goto end

:error
echo.
echo === FLTK BUILD FAILED ===
pause

:end
endlocal
