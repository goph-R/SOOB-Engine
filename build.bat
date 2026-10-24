@echo off
setlocal

echo === SOOB-Engine Win98/Dev-C++ Build ===
echo.

REM ----------------------------------------------------------------
REM  Shared engine (2D / audio / scripting) lives at ..\SOOB-Core\.
REM  Bullet stays here -- 3D-only, FPS-demo-specific.
REM ----------------------------------------------------------------
set "ENGINE=..\SOOB-Core"

REM ----------------------------------------------------------------
REM  Check for OpenAL headers and import library in the shared engine.
REM    %ENGINE%\vendor\include\AL\al.h, alc.h     ^(headers^)
REM    %ENGINE%\vendor\lib\libOpenAL32.a OR       ^(MinGW import library^)
REM    %ENGINE%\vendor\lib\libOpenAL32.dll.a      ^(OpenAL Soft MinGW variant^)
REM    OpenAL32.dll                               ^(next to the exe^)
REM ----------------------------------------------------------------
if not exist "%ENGINE%\vendor\include\AL\al.h" (
    echo ERROR: OpenAL headers missing.
    echo Place al.h and alc.h in %ENGINE%\vendor\include\AL\.
    goto error
)
if not exist "%ENGINE%\vendor\lib\libOpenAL32.a" (
    if not exist "%ENGINE%\vendor\lib\libOpenAL32.dll.a" (
        echo ERROR: OpenAL MinGW import library missing.
        echo Expected %ENGINE%\vendor\lib\libOpenAL32.a or libOpenAL32.dll.a.
        goto error
    )
)

REM ----------------------------------------------------------------
REM  Object output directory (gitignored)
REM ----------------------------------------------------------------
set "OBJDIR=raw\obj"
if not exist "%OBJDIR%" mkdir "%OBJDIR%"

REM ----------------------------------------------------------------
REM  Bullet Physics (cached — delete raw\obj\bl.o/bc.o/bd.o to force rebuild)
REM ----------------------------------------------------------------
if exist %OBJDIR%\bl.o (
    echo Skipping Bullet Linear Math ^(bl.o cached^)
) else (
    echo Compiling Bullet Linear Math...
    C:\Dev-Cpp\bin\g++.exe -Ivendor\bullet3-3.25\src -O2 -c vendor\bullet3-3.25\src\btLinearMathAll.cpp -o %OBJDIR%\bl.o
    if errorlevel 1 goto error
)
if exist %OBJDIR%\bc.o (
    echo Skipping Bullet Collision ^(bc.o cached^)
) else (
    echo Compiling Bullet Collision...
    C:\Dev-Cpp\bin\g++.exe -Ivendor\bullet3-3.25\src -O2 -c vendor\bullet3-3.25\src\btBulletCollisionAll.cpp -o %OBJDIR%\bc.o
    if errorlevel 1 goto error
)
if exist %OBJDIR%\bd.o (
    echo Skipping Bullet Dynamics ^(bd.o cached^)
) else (
    echo Compiling Bullet Dynamics...
    C:\Dev-Cpp\bin\g++.exe -Ivendor\bullet3-3.25\src -O2 -c vendor\bullet3-3.25\src\btBulletDynamicsAll.cpp -o %OBJDIR%\bd.o
    if errorlevel 1 goto error
)

REM ----------------------------------------------------------------
REM  Lua 5.1.5 — always rebuilt. The mapped drive between Win98 and
REM  the Linux dev host makes `if exist lua.o` unreliable (stale object
REM  from the other toolchain gets picked up). Lua is small; recompile
REM  every build is cheap.
REM  -DLUA_ANSI disables loadlib dlopen / Win32 branches that Dev-C++ 3.4
REM  can't satisfy. -Dluaall_c is Lua's own unity-build switch.
REM ----------------------------------------------------------------
echo Compiling Lua...
C:\Dev-Cpp\bin\gcc.exe -I%ENGINE%\vendor\lua-5.1.5\src -Dluaall_c -DLUA_ANSI -O2 -c %ENGINE%\vendor\lua-5.1.5\src\lua_all.c -o %OBJDIR%\lua.o
if errorlevel 1 goto error

REM ----------------------------------------------------------------
REM  stb_vorbis (cached — delete raw\obj\vorbis.o to force rebuild)
REM ----------------------------------------------------------------
if exist %OBJDIR%\vorbis.o (
    echo Skipping stb_vorbis ^(vorbis.o cached^)
) else (
    echo Compiling stb_vorbis...
    C:\Dev-Cpp\bin\gcc.exe -O2 -c %ENGINE%\vendor\stb\stb_vorbis.c -o %OBJDIR%\vorbis.o
    if errorlevel 1 goto error
)

REM ----------------------------------------------------------------
REM  Compile main.  -I%ENGINE% lets #include "script.h" etc. resolve
REM  to the shared headers.
REM ----------------------------------------------------------------
echo Compiling main...
C:\Dev-Cpp\bin\g++.exe -I%ENGINE% -I%ENGINE%\vendor\include -Ivendor\bullet3-3.25\src -I%ENGINE%\vendor\lua-5.1.5\src -O2 -c main.cpp -o %OBJDIR%\main.o
if errorlevel 1 goto error

REM ----------------------------------------------------------------
REM  Link
REM ----------------------------------------------------------------
echo Linking...
C:\Dev-Cpp\bin\g++.exe %OBJDIR%\main.o %OBJDIR%\bl.o %OBJDIR%\bc.o %OBJDIR%\bd.o %OBJDIR%\lua.o %OBJDIR%\vorbis.o -o SOOB.exe -L%ENGINE%\vendor\lib -lmingw32 -lSDLmain -lSDL -lopengl32 -lOpenAL32
if errorlevel 1 goto error

REM ----------------------------------------------------------------
REM  Mirror SOOB-Core's Lua engine modules next to the exe so
REM  require "engine.scene" resolves via ./scripts/?.lua in shipped
REM  builds without needing the SOOB-Core repo on the player's machine.
REM ----------------------------------------------------------------
if not exist scripts\engine mkdir scripts\engine
copy /Y %ENGINE%\scripts\engine\*.lua scripts\engine\ >nul

echo.
echo === Build successful! Run SOOB.exe ===
echo Make sure OpenAL32.dll is next to the exe or in the system directory.
goto end

:error
echo.
echo === Build FAILED ===
pause

:end
endlocal
