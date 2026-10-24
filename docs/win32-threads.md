# Switching the Win10 MinGW to win32-threads

## Why

The default winlibs MinGW we vendor today is the **posix-dwarf** variant,
which pulls in `libwinpthread-1.dll` as a runtime dependency. That DLL
ships next to `SOOB_w10.exe` and is one of the files Windows Defender
most commonly false-positives on MinGW-built distributions (same family
that ate `libintl-8.dll` during dev).

Switching to the **win32-dwarf** winlibs variant drops `libwinpthread-1.dll`
entirely — `std::thread` and friends use the Win32 thread API directly
— leaving just `OpenAL32.dll` + `SDL.dll` beside the exe. One fewer
quarantine surface for shipped copies; zero code changes needed.

## Is the codebase actually safe for win32-threads?

Yes. The only code in the repo that touches `<thread>` is inside
Bullet:

- `vendor/bullet3-3.25/src/LinearMath/btThreads.cpp` — calls
  `std::thread::hardware_concurrency()`. This is a header-only query
  that works on any thread model, and winlibs ships `mcfgthread` so
  even heavier `std::thread` usage would compile.
- `vendor/bullet3-3.25/src/LinearMath/TaskScheduler/btThreadSupport{Win32,Posix}.cpp`
  — both guarded by `#if BT_THREADSAFE`, which we do not define. Dead
  code in our build.

The engine's own modules (`main.cpp` + the header-only files) contain
zero `std::thread` / `std::mutex` / `std::async` usage and are locked
to a pre-C++11 style anyway (Win98 Dev-C++ 3.4 compatibility), so they
are trivially fine with either thread model.

## Steps

1. Open the winlibs releases page:
   https://github.com/brechtsanders/winlibs_mingw/releases

   Pick a release tagged for GCC 15.2.0 (or whatever version you want
   to pin to — keep it matching `g++ -dumpversion` from the current
   toolchain if you want a clean swap).

2. Download the **win32-dwarf** variant, not posix-dwarf. File name
   pattern to look for:

   ```
   winlibs-i686-win32-dwarf-gcc-15.2.0-mingw-w64ucrt-*.zip
   ```

   Pieces:
   - `i686` — 32-bit target, matches what we build today.
   - `win32` — thread model. **This is the bit we're changing.**
   - `dwarf` — exception model, same as current posix-dwarf.
   - `ucrt` — C runtime. We're already UCRT; keep it (don't swap to
     msvcrt unless you also want to target Win7/8 natively, which is
     a separate decision).

3. Back up the current toolchain so a rollback is one `rename` away:

   ```
   rename vendor_win10\mingw32 mingw32.old
   ```

4. Extract the new zip. The archive contains a top-level `mingw32\`
   folder — extract into `vendor_win10\` so you end up with
   `vendor_win10\mingw32\bin\g++.exe` again.

5. Invalidate the Bullet + Lua cache (compiler runtime changed):

   ```
   del raw\obj\bl.o raw\obj\bc.o raw\obj\bd.o raw\obj\lua.o
   ```

6. Run `build_win10.bat`. First build takes the usual ~60–90s for
   Bullet; after that you're back on the cached path.

## Dropping libwinpthread-1.dll

Once the new toolchain builds cleanly:

1. Delete the vendored DLL from the repo root:

   ```
   del libwinpthread-1.dll
   ```

2. Remove the copy-step from `build_win10.bat`. Find the block:

   ```bat
   if not exist "libwinpthread-1.dll" (
       copy vendor_win10\mingw32\bin\libwinpthread-1.dll . >nul
   )
   ```

   …and delete it.

3. Run `SOOB_w10.exe`. If Windows complains about a missing DLL,
   check which one and decide case-by-case (should not happen with
   `-static-libgcc -static-libstdc++` already in the link line plus
   the win32-threads runtime). If it runs, you're done — the shipped
   DLL set is now just `OpenAL32.dll` + `SDL.dll`.

4. Once verified, delete `vendor_win10\mingw32.old\` and commit.

## What can still go wrong

- **EV vs standard code-signing on the installer**: unrelated to this
  change, but reminder — a standard cert still accrues SmartScreen
  reputation gradually. EV is instant trust. See the discussion in
  CLAUDE.md / session notes if this matters at release time.
- **A different MinGW DLL gets flagged** (e.g. `libgcc_s_dw2-1.dll`
  in a future release). With `-static-libgcc` on, we already dodge
  that one, but if a future GCC version changes what it wants
  statically linked, audit the DLL directory next to the built exe
  before shipping.
- **Bullet starts using `BT_THREADSAFE`** in a future vendor bump.
  If we ever define that macro, re-audit this doc — the thread
  support TUs will suddenly be live code.
