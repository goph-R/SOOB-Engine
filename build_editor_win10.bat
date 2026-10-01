@echo off
setlocal

echo === SOOB Level Editor - Win10 MinGW Build ===
echo.

REM ----------------------------------------------------------------
REM  Win10 twin of build_editor.bat. Same sources and layout -- only the
REM  toolchain differs: the portable WinLibs i686 MinGW under
REM  ..\SOOB-Core\vendor_win10\ instead of C:\Dev-Cpp.
REM
REM  Run from the REPO ROOT. Editor sources are in editor\;
REM  SoobEditor_w10.exe is written here so it sits beside assets\
REM  (assets are relative-pathed, exactly like the game).
REM
REM  Prerequisite: run build_fltk_win10.bat FIRST -- it produces the
REM  static libs in %FLTK%\lib_w10 (a separate dir from the Dev-C++
REM  build's lib\, since the two toolchains' archives are not ABI
REM  compatible).
REM ----------------------------------------------------------------

set "ENGINE=..\SOOB-Core"
set "GPP=%ENGINE%\vendor_win10\mingw32\bin\g++.exe"
set "GCC=%ENGINE%\vendor_win10\mingw32\bin\gcc.exe"
set "FLTK=vendor\fltk-1.3\FL"
set "OBJDIR=raw\obj"

if not exist "%GPP%" (
    echo ERROR: MinGW not found at %GPP%
    echo Download WinLibs i686 and extract to %ENGINE%\vendor_win10\mingw32\
    echo https://github.com/brechtsanders/winlibs_mingw/releases
    goto error
)
if not exist "%FLTK%\FL\Fl.H" (
    echo ERROR: FLTK headers not found at %FLTK%\FL\.
    echo Run this .bat from the repo root, with FLTK at
    echo   vendor\fltk-1.3\FL  ^(or edit FLTK at the top^).
    goto error
)
if not exist "%FLTK%\lib_w10\libfltk_gl.a" (
    echo ERROR: FLTK libraries not built. Run build_fltk_win10.bat first.
    goto error
)

if not exist "%OBJDIR%" mkdir "%OBJDIR%"

REM ----------------------------------------------------------------
REM  Bullet Physics (shared with the game build; delete raw\obj\bl_w10.o
REM  bc_w10.o bd_w10.o to force a rebuild). The editor never simulates, but
REM  struct Game embeds PhysWorld/NavGraph by value, so it links Bullet.
REM ----------------------------------------------------------------
set "BOPTS=-O2 -Ivendor\bullet3-3.25\src"

if exist %OBJDIR%\bl_w10.o (
    echo Skipping Bullet Linear Math ^(bl_w10.o cached^)
) else (
    echo Compiling Bullet Linear Math...
    %GPP% %BOPTS% -c vendor\bullet3-3.25\src\btLinearMathAll.cpp -o %OBJDIR%\bl_w10.o
    if errorlevel 1 goto error
)
if exist %OBJDIR%\bc_w10.o (
    echo Skipping Bullet Collision ^(bc_w10.o cached^)
) else (
    echo Compiling Bullet Collision...
    %GPP% %BOPTS% -c vendor\bullet3-3.25\src\btBulletCollisionAll.cpp -o %OBJDIR%\bc_w10.o
    if errorlevel 1 goto error
)
if exist %OBJDIR%\bd_w10.o (
    echo Skipping Bullet Dynamics ^(bd_w10.o cached^)
) else (
    echo Compiling Bullet Dynamics...
    %GPP% %BOPTS% -c vendor\bullet3-3.25\src\btBulletDynamicsAll.cpp -o %OBJDIR%\bd_w10.o
    if errorlevel 1 goto error
)

REM ----------------------------------------------------------------
REM  Lua 5.1.5 (unity build) -- edit_assets.h runs assets.lua in a bare
REM  Lua state to register models/textures. Same object the game build
REM  produces; delete raw\obj\lua_w10.o to force a rebuild.
REM ----------------------------------------------------------------
if exist %OBJDIR%\lua_w10.o (
    echo Skipping Lua ^(lua_w10.o cached^)
) else (
    echo Compiling Lua...
    %GCC% -I%ENGINE%\vendor\lua-5.1.5\src -Dluaall_c -O2 -c %ENGINE%\vendor\lua-5.1.5\src\lua_all.c -o %OBJDIR%\lua_w10.o
    if errorlevel 1 goto error
)

REM ----------------------------------------------------------------
REM  Compile the editor. editor.cpp lives in editor\; its edit_*.h resolve
REM  next to it. -I. adds the engine-root headers it includes by bare name
REM  (obj_loader.h, game.h, render_level.h, ...); -I%ENGINE% resolves
REM  SOOB-Core headers (texture.h, asset_registry.h); -I%FLTK% resolves
REM  <FL/...>; the lua src path resolves edit_assets.h's <lua.h>.
REM ----------------------------------------------------------------
echo Compiling editor...
%GPP% -DWIN32 -DWINVER=0x0500 -D_WIN32_WINNT=0x0500 -I. -I%ENGINE% -I%FLTK% -Ivendor\bullet3-3.25\src -I%ENGINE%\vendor\lua-5.1.5\src -O2 -c editor\editor.cpp -o %OBJDIR%\editor_w10.o
if errorlevel 1 goto error

REM ----------------------------------------------------------------
REM  Link: FLTK (GL + core) + Bullet + Win32 GL/GDI. No -mwindows so the
REM  conLogf stdout stays visible. -static-libgcc/-static-libstdc++ keeps
REM  the exe free of MinGW runtime DLLs, same as the game's Win10 build.
REM ----------------------------------------------------------------
echo Linking...
%GPP% %OBJDIR%\editor_w10.o %OBJDIR%\bl_w10.o %OBJDIR%\bc_w10.o %OBJDIR%\bd_w10.o %OBJDIR%\lua_w10.o -o SoobEditor_w10.exe -L%FLTK%\lib_w10 -lfltk_gl -lfltk -lopengl32 -lglu32 -lole32 -luuid -lcomctl32 -lcomdlg32 -lgdi32 -lwinspool -lwsock32 -static-libgcc -static-libstdc++
if errorlevel 1 goto error

echo.
echo === Build successful! Run SoobEditor_w10.exe from the repo root ===
goto end

:error
echo.
echo === Build FAILED ===
pause

:end
endlocal
