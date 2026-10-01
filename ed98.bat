@echo off
echo === SOOB Level Editor - Win98/Dev-C++ Build ===
echo.

REM ----------------------------------------------------------------
REM  COMMAND.COM port of build_editor.bat, for Windows 98 SE.
REM  Win98 has no cmd.exe, so setlocal, quoted "set" and parenthesised
REM  if/else blocks are all out -- flow is plain goto, and directories
REM  are tested for with \nul rather than by their bare name.
REM
REM  Run fltk98.bat in SOOB-Core FIRST, then this from the repo root:
REM      X:
REM      cd \Projects\SOOB-Engine
REM      ed98
REM
REM  Editor sources are in editor\; SoobEditor.exe is written to the
REM  repo root so it sits beside assets\, which are relative-pathed
REM  exactly like the game -- build and run from the same folder.
REM ----------------------------------------------------------------

REM ---- Current-directory check. main.cpp is a valid 8.3 name, so this
REM ---- test raises no long-filename question.
if not exist main.cpp goto nocwd

set ENGINE=..\SOOB-Core
set FLTK=%ENGINE%\vendor\fltk-1.3\FL
set OBJDIR=raw\obj
set DC=C:\Dev-Cpp\bin

REM ---- FLTK must be built (fltk98.bat in SOOB-Core). Not tested here:
REM ---- COMMAND.COM's builtins resolve 8.3 names only and "SOOB-Core" is
REM ---- not one, so "if exist" through it is unreliable. Without FLTK the
REM ---- link stops with "cannot find -lfltk".

if not exist raw\nul mkdir raw
if not exist %OBJDIR%\nul mkdir %OBJDIR%

REM ---- Bullet: shared with the game build, so b98.bat will normally have
REM ---- built these already. The editor never simulates, but struct Game
REM ---- embeds PhysWorld/NavGraph by value, so it still links Bullet.
REM ---- Delete raw\obj\bl.o bc.o bd.o to force a rebuild.
if exist %OBJDIR%\bl.o goto skipbl
echo Compiling Bullet Linear Math...
%DC%\g++.exe -Ivendor\bullet3-3.25\src -O2 -c vendor\bullet3-3.25\src\btLinearMathAll.cpp -o %OBJDIR%\bl.o
if errorlevel 1 goto error
goto donebl
:skipbl
echo Skipping Bullet Linear Math - bl.o cached
:donebl

if exist %OBJDIR%\bc.o goto skipbc
echo Compiling Bullet Collision...
%DC%\g++.exe -Ivendor\bullet3-3.25\src -O2 -c vendor\bullet3-3.25\src\btBulletCollisionAll.cpp -o %OBJDIR%\bc.o
if errorlevel 1 goto error
goto donebc
:skipbc
echo Skipping Bullet Collision - bc.o cached
:donebc

if exist %OBJDIR%\bd.o goto skipbd
echo Compiling Bullet Dynamics...
%DC%\g++.exe -Ivendor\bullet3-3.25\src -O2 -c vendor\bullet3-3.25\src\btBulletDynamicsAll.cpp -o %OBJDIR%\bd.o
if errorlevel 1 goto error
goto donebd
:skipbd
echo Skipping Bullet Dynamics - bd.o cached
:donebd

REM ---- Lua 5.1.5 unity build. edit_assets.h runs assets.lua in a bare Lua
REM ---- state to register models/textures. Same object the game build makes.
if exist %OBJDIR%\lua.o goto skiplua
echo Compiling Lua...
%DC%\gcc.exe -I%ENGINE%\vendor\lua-5.1.5\src -Dluaall_c -DLUA_ANSI -O2 -c %ENGINE%\vendor\lua-5.1.5\src\lua_all.c -o %OBJDIR%\lua.o
if errorlevel 1 goto error
goto donelua
:skiplua
echo Skipping Lua - lua.o cached
:donelua

REM ---- Compile the editor. editor.cpp lives in editor\; its edit_*.h resolve
REM ---- next to it. -I. adds the engine-root headers it includes by bare name
REM ---- (obj_loader.h, game.h, render_level.h, ...); -I%ENGINE% resolves
REM ---- SOOB-Core headers (texture.h, asset_registry.h); -I%FLTK% resolves
REM ---- <FL/...>; the lua src path resolves edit_assets.h's <lua.h>.
echo Compiling editor...
%DC%\g++.exe -DWIN32 -DWINVER=0x0500 -D_WIN32_WINNT=0x0500 -I. -I%ENGINE% -I%FLTK% -Ivendor\bullet3-3.25\src -I%ENGINE%\vendor\lua-5.1.5\src -O2 -c editor\editor.cpp -o %OBJDIR%\editor.o
if errorlevel 1 goto error

REM ---- Link: FLTK (GL + core) + Bullet + Win32 GL/GDI. No -mwindows so the
REM ---- conLogf stdout stays visible. Output to the repo root, beside assets\.
echo Linking...
%DC%\g++.exe %OBJDIR%\editor.o %OBJDIR%\bl.o %OBJDIR%\bc.o %OBJDIR%\bd.o %OBJDIR%\lua.o -o SoobEditor.exe -L%FLTK%\lib -lfltk_gl -lfltk -lopengl32 -lglu32 -lole32 -luuid -lcomctl32 -lcomdlg32 -lgdi32 -lwsock32
if errorlevel 1 goto error

echo.
echo === Build successful! Run SoobEditor.exe from the repo root ===
goto end

:nofltk
echo ERROR: FLTK libraries not built.
echo Run fltk98.bat in SOOB-Core first.
goto error

:nocwd
echo ERROR: wrong current directory - main.cpp is not here.
echo Run this from the repo root.
goto error

:error
echo.
echo === Build FAILED ===
pause

:end
