@echo off
setlocal

echo === SDLFun Win10 MinGW Build ===
echo.

REM ----------------------------------------------------------------
REM  Shared engine + portable MinGW live under ..\SOOB-Core\.
REM  Bullet stays here -- 3D-only, FPS-demo-specific.
REM ----------------------------------------------------------------
set "ENGINE=..\SOOB-Core"
set "GPP=%ENGINE%\vendor_win10\mingw32\bin\g++.exe"
set "GCC=%ENGINE%\vendor_win10\mingw32\bin\gcc.exe"

if not exist "%GPP%" (
    echo ERROR: MinGW not found at %GPP%
    echo Download WinLibs i686 and extract to %ENGINE%\vendor_win10\mingw32\
    echo https://github.com/brechtsanders/winlibs_mingw/releases
    goto error
)

REM ----------------------------------------------------------------
REM  Check for OpenAL headers and import library
REM ----------------------------------------------------------------
if not exist "%ENGINE%\vendor_win10\include\AL\al.h" (
    echo ERROR: OpenAL headers missing.
    echo Download OpenAL Soft 1.23.x prebuilt binaries and place:
    echo   AL\al.h, AL\alc.h -^> %ENGINE%\vendor_win10\include\AL\
    echo   libOpenAL32.dll.a or libopenal.dll.a -^> %ENGINE%\vendor_win10\lib\
    echo   OpenAL32.dll -^> next to the exe ^(or in PATH^)
    echo From: https://www.openal-soft.org/#download
    goto error
)

for /f "tokens=*" %%V in ('%GPP% -dumpversion') do set "GCC_VER=%%V"
echo Found GCC %GCC_VER%
echo.

set "OPTS=-O2 -I%ENGINE% -I%ENGINE%\vendor_win10\include -Ivendor\bullet3-3.25\src -I%ENGINE%\vendor\lua-5.1.5\src"

REM ----------------------------------------------------------------
REM  Object output directory (gitignored)
REM ----------------------------------------------------------------
set "OBJDIR=raw\obj"
if not exist "%OBJDIR%" mkdir "%OBJDIR%"

REM ----------------------------------------------------------------
REM  Compile Bullet Physics (cached — delete raw\obj\bl_w10.o/bc_w10.o/bd_w10.o to force rebuild)
REM ----------------------------------------------------------------
if exist %OBJDIR%\bl_w10.o (
    echo Skipping Bullet Linear Math ^(bl_w10.o cached^)
) else (
    echo Compiling Bullet Linear Math...
    %GPP% %OPTS% -c vendor\bullet3-3.25\src\btLinearMathAll.cpp -o %OBJDIR%\bl_w10.o
    if errorlevel 1 goto error
)

if exist %OBJDIR%\bc_w10.o (
    echo Skipping Bullet Collision ^(bc_w10.o cached^)
) else (
    echo Compiling Bullet Collision...
    %GPP% %OPTS% -c vendor\bullet3-3.25\src\btBulletCollisionAll.cpp -o %OBJDIR%\bc_w10.o
    if errorlevel 1 goto error
)

if exist %OBJDIR%\bd_w10.o (
    echo Skipping Bullet Dynamics ^(bd_w10.o cached^)
) else (
    echo Compiling Bullet Dynamics...
    %GPP% %OPTS% -c vendor\bullet3-3.25\src\btBulletDynamicsAll.cpp -o %OBJDIR%\bd_w10.o
    if errorlevel 1 goto error
)

REM ----------------------------------------------------------------
REM  Lua 5.1.5 (cached — delete raw\obj\lua_w10.o to force rebuild)
REM ----------------------------------------------------------------
if exist %OBJDIR%\lua_w10.o (
    echo Skipping Lua ^(lua_w10.o cached^)
) else (
    echo Compiling Lua...
    %GCC% -I%ENGINE%\vendor\lua-5.1.5\src -Dluaall_c -O2 -c %ENGINE%\vendor\lua-5.1.5\src\lua_all.c -o %OBJDIR%\lua_w10.o
    if errorlevel 1 goto error
)

REM ----------------------------------------------------------------
REM  stb_vorbis (cached — delete raw\obj\vorbis_w10.o to force rebuild)
REM ----------------------------------------------------------------
if exist %OBJDIR%\vorbis_w10.o (
    echo Skipping stb_vorbis ^(vorbis_w10.o cached^)
) else (
    echo Compiling stb_vorbis...
    %GCC% -O2 -c %ENGINE%\vendor\stb\stb_vorbis.c -o %OBJDIR%\vorbis_w10.o
    if errorlevel 1 goto error
)

REM ----------------------------------------------------------------
REM  Compile main
REM ----------------------------------------------------------------
echo Compiling SDL main stub...
%GCC% -c %ENGINE%\vendor_win10\sdl_main.c -o %OBJDIR%\sdl_main.o
if errorlevel 1 goto error

echo Compiling main...
%GPP% %OPTS% -c main.cpp -o %OBJDIR%\main.o
if errorlevel 1 goto error

REM ----------------------------------------------------------------
REM  Link
REM ----------------------------------------------------------------
echo Linking...
%GPP% %OBJDIR%\main.o %OBJDIR%\sdl_main.o %OBJDIR%\bl_w10.o %OBJDIR%\bc_w10.o %OBJDIR%\bd_w10.o %OBJDIR%\lua_w10.o %OBJDIR%\vorbis_w10.o -o SDLFun_w10.exe -L%ENGINE%\vendor_win10\lib -lmingw32 -lSDL -lopengl32 -lOpenAL32 -static-libgcc -static-libstdc++
if errorlevel 1 goto error

REM ----------------------------------------------------------------
REM  Copy DLLs next to exe (if not already there)
REM ----------------------------------------------------------------
if not exist "libwinpthread-1.dll" (
    copy %ENGINE%\vendor_win10\mingw32\bin\libwinpthread-1.dll . >nul
)
if not exist "OpenAL32.dll" (
    if exist "%ENGINE%\vendor_win10\OpenAL32.dll" (
        copy %ENGINE%\vendor_win10\OpenAL32.dll . >nul
    ) else (
        echo NOTE: OpenAL32.dll not found next to exe. Place the OpenAL Soft
        echo       runtime DLL ^(renamed from soft_oal.dll if needed^) here.
    )
)

REM ----------------------------------------------------------------
REM  Mirror SOOB-Core's Lua engine modules next to the exe so
REM  require "engine.scene" resolves via ./scripts/?.lua in shipped
REM  builds without needing the SOOB-Core repo on the player's machine.
REM ----------------------------------------------------------------
if not exist scripts\engine mkdir scripts\engine
copy /Y %ENGINE%\scripts\engine\*.lua scripts\engine\ >nul

echo.
echo === Build successful! Run SDLFun_w10.exe ===
goto end

:error
echo.
echo === Build FAILED ===
pause

:end
endlocal
