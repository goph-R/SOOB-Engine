@echo off
echo === SOOB-Engine Win98/Dev-C++ Build ===
echo.

REM ----------------------------------------------------------------
REM  COMMAND.COM port of build.bat, for Windows 98 SE.
REM
REM  The ONE thing that actually broke: build.bat drifted into
REM  cmd.exe-only syntax during Linux/Win10 work - setlocal, quoted
REM  "set", parenthesised if/else blocks. COMMAND.COM has none of
REM  them, and setlocal on line 2 is the "Bad command or filename".
REM  The first build.bat (commit 29d1c96) was flat goto-style and ran
REM  fine on 98 SE; this restores that style and nothing more.
REM
REM  RUN IT WITH THE CURRENT DIRECTORY ON THE REPO ROOT. Do not type
REM  "c:" first - that moves the current drive and every relative path
REM  below then resolves against C:\ instead of the tree:
REM
REM      X:
REM      cd \Projects\SOOB-Engine
REM      C:\B98.BAT
REM
REM  Keeping the script itself on C: means COMMAND.COM reads it from a
REM  local drive. It reopens the batch file for every line, which is
REM  what made build.bat die with "Batch file missing" over the share.
REM ----------------------------------------------------------------

REM ---- Current-directory check. main.cpp is a valid 8.3 name, so this
REM ---- test raises no long-filename question.
if not exist main.cpp goto nocwd

set ENGINE=..\SOOB-Core
set DC=C:\Dev-Cpp\bin
set OBJDIR=raw\obj

REM ---- DOS mkdir does one level at a time, and a directory is tested
REM ---- for with \nul rather than by its bare name.
if not exist raw\nul mkdir raw
if not exist %OBJDIR%\nul mkdir %OBJDIR%

REM ---- Bullet: cached. Delete raw\obj\bl.o bc.o bd.o to force a
REM ---- rebuild. If your existing Win98 objects are in the repo root
REM ---- from the original build.bat, move them into raw\obj to reuse
REM ---- them instead of recompiling.
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

REM ---- Lua 5.1.5: always rebuilt. The mapped drive between Win98 and
REM ---- the Linux host makes "if exist lua.o" unreliable - a stale
REM ---- object from the other toolchain gets picked up. Lua is small.
echo Compiling Lua...
%DC%\gcc.exe -I%ENGINE%\vendor\lua-5.1.5\src -Dluaall_c -DLUA_ANSI -O2 -c %ENGINE%\vendor\lua-5.1.5\src\lua_all.c -o %OBJDIR%\lua.o
if errorlevel 1 goto error

REM ---- stb_vorbis: cached. Delete raw\obj\vorbis.o to force a rebuild.
if exist %OBJDIR%\vorbis.o goto skipvor
echo Compiling stb_vorbis...
%DC%\gcc.exe -O2 -c %ENGINE%\vendor\stb\stb_vorbis.c -o %OBJDIR%\vorbis.o
if errorlevel 1 goto error
goto donevor
:skipvor
echo Skipping stb_vorbis - vorbis.o cached
:donevor

echo Compiling main...
%DC%\g++.exe -I%ENGINE% -I%ENGINE%\vendor\include -Ivendor\bullet3-3.25\src -I%ENGINE%\vendor\lua-5.1.5\src -O2 -c main.cpp -o %OBJDIR%\main.o
if errorlevel 1 goto error

echo Linking...
%DC%\g++.exe %OBJDIR%\main.o %OBJDIR%\bl.o %OBJDIR%\bc.o %OBJDIR%\bd.o %OBJDIR%\lua.o %OBJDIR%\vorbis.o -o SOOB.exe -L%ENGINE%\vendor\lib -lmingw32 -lSDLmain -lSDL -lopengl32 -lOpenAL32
if errorlevel 1 goto error

REM ---- Mirror SOOB-Core's Lua modules next to the exe. DOS copy has no
REM ---- /Y switch; it overwrites without prompting anyway.
if not exist scripts\nul mkdir scripts
if not exist scripts\engine\nul mkdir scripts\engine
copy %ENGINE%\scripts\engine\*.lua scripts\engine\ >nul

echo.
echo === Build successful! Run SOOB.exe ===
echo Make sure OpenAL32.dll is next to the exe or in the system directory.
goto end

:nocwd
echo ERROR: wrong current directory - main.cpp is not here.
echo.
echo Do NOT type "c:" first. Keep the current drive on the share and
echo call the script by its full path:
echo.
echo     X:
echo     cd \Projects\SOOB-Engine
echo     C:\B98.BAT
goto error

:error
echo.
echo === Build FAILED ===
pause

:end
