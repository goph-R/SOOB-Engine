@echo off
setlocal

echo === SOOB-Engine Win98/Dev-C++ Build ===
echo.

REM ----------------------------------------------------------------
REM  Compile main
REM ----------------------------------------------------------------
echo Compiling main...
C:\Dev-Cpp\bin\g++.exe -Ivendor\include -Ivendor\bullet3-3.25\src -Ivendor\lua-5.1.5\src -O2 -c main.cpp -o %OBJDIR%\main.o
if errorlevel 1 goto error

REM ----------------------------------------------------------------
REM  Link
REM ----------------------------------------------------------------
echo Linking...
C:\Dev-Cpp\bin\g++.exe %OBJDIR%\main.o %OBJDIR%\bl.o %OBJDIR%\bc.o %OBJDIR%\bd.o %OBJDIR%\lua.o %OBJDIR%\vorbis.o -o SOOB.exe -Lvendor\lib -lmingw32 -lSDLmain -lSDL -lopengl32 -lOpenAL32
if errorlevel 1 goto error

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
