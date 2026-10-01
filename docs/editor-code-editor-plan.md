# Plan: turning the CodeEditor widget into a usable Win98 editor

Where we are: `editor/edit_lex.h` (Pascal/Lua/Markdown lexer), `editor/edit_code.h`
(model layer + `CodeEditor` widget), `editor/codetest.cpp` + `c98.bat` (probe).
One file, opened from the command line, no menus.

Where this goes: tabs per file, File / Open / Save / Save As / Exit, Edit / Find /
Replace / Go to line. Running on the Pentium II 350.

## Headline: the work is mostly NOT the editor

The UI above is routine FLTK. What actually drives the estimate is that FLTK 1.3
reaches for Win32 Unicode entry points in several more places, and Windows 98
implements those as stubs that fail. Three of those have already bitten us (window
creation, file open, line-number colours). **Two more sit directly on this
roadmap**, and both must be cleared before the corresponding menu item can work at
all:

| Blocker | Where | Effect on Win98 |
|---|---|---|
| `FindFirstFileW` / `FindNextFileW` | `src/scandir_win32.c` | **no directory listing** → `fl_file_chooser()` shows empty dirs → File / Open unusable |
| `OPENFILENAMEW`, `GetOpenFileNameW`, `SHBrowseForFolderW` | `src/Fl_Native_File_Chooser_WIN32.cxx` | the *native* chooser is unusable outright — don't plan on it |

`scandir_win32.c` is patched first regardless — a working `fl_filename_list()`
matters beyond the chooser. Its other dependencies (`filename_isdir` → `fl_stat`,
plus `fl_getcwd` / `fl_access`) already go through the nine functions patched in
`fl_utf8.cxx`, which is why that patch covered all nine rather than just
`fl_fopen`.

**For the dialogs themselves we call comdlg32 directly** — see
`editor/edit_filedlg.h`. The ANSI entry points (`GetOpenFileNameA` /
`GetSaveFileNameA`) work fine on 98 and give the genuine period dialog, which
looks right in a way FLTK's drawn chooser does not. That is smaller than patching
`Fl_Native_File_Chooser_WIN32.cxx` (which would mean converting `OPENFILENAMEW`,
both `Get*FileNameW` calls *and* `SHBrowseForFolderW`) and it is our code rather
than a sixth vendored patch to re-apply on every FLTK upgrade. Off Windows it
falls back to `fl_file_chooser()`, so the Linux build is unaffected.

## Phase 0 — unblock the file chooser  ✅ IMPLEMENTED (awaiting PII verification)

Patch `src/scandir_win32.c` the same way `fl_utf8.cxx` was patched: a file-local
`fl_win98_is_9x()`, and an ANSI `FindFirstFileA` / `FindNextFileA` path alongside
the wide one. The `WIN32_FIND_DATAA` fields are the same names, so the body is
near-identical; the difference is skipping the UTF-16 → UTF-8 conversion of
`cFileName`.

Done: `src/scandir_win32.c` now branches on a file-local `fl_win98_is_9x()` and
uses `FindFirstFileA` / `FindNextFileA` there.

`editor/chooser98.cpp` + `f98.bat` is the probe, and it checks the layers
separately so a failure localises: stage 1 `fl_filename_list()` (no GUI at all),
stage 2 the `fl_stat`/`fl_access`/`fl_getcwd` group, stage 3 the full
`fl_file_chooser()`. **Run it on the PII before building any menu on top.**

Written up as *Local FLTK patch: ANSI directory listing on Windows 9x* in
`docs/editor-fltk-win98.md`.

## Phase 1 — tabs per file  ✅ IMPLEMENTED (awaiting PII verification)

`Fl_Text_Display` derives from `Fl_Group`, so a `CodeEditor` can be a **direct
child of `Fl_Tabs`** — no wrapper group needed. Its `label()` is the tab caption.

- `struct CodeDoc { CodeEditor *ed; char *path; int dirty; }`, a fixed array
  (say 16 tabs) in keeping with the engine's no-STL style.
- Use **`copy_label()`**, not `label()` — `Fl_Widget::label()` stores the pointer
  without copying, and the tab caption is built from the basename plus a `*` when
  dirty, so it must be owned.
- Dirty flag: `CodeEditor` already installs `staticModifyCb`; set `mDirty` there
  and expose `dirty()` / `clearDirty()`. Small change to `edit_code.h`.
- `Fl_Tabs` wants all children sized to the tab body area (it reserves ~25px at
  the top); size them once and in the window's resize path.
- Tab close via File / Close (Fl_Tabs 1.3 has no close buttons).

Also in this phase: **replace FLTK's undo with our own** (see *Undo* below). It
has to happen here rather than later, because FLTK's undo state is global and
tabs are exactly what breaks it.

Delivered in `editor/codeedit.cpp` + `e98.bat` (Phases 1 and 2 together — tabs
without File / Open are not much use). Undo lives in `edit_code.h` as a model
layer (`CodeUndo`, `codeUndoRecord/Undo/Redo`) so it is covered headlessly in
`edit_code_model_test.cpp`; `CodeEditor` only delegates.

The app uses **`Fl_Double_Window`**: FLTK's plain `Fl_Window` is single-buffered,
so every partial repaint lands directly on screen and flickers on period
hardware.

## Phase 2 — File menu  ✅ IMPLEMENTED (awaiting PII verification)

`Fl_Menu_Bar` across the top. Open / Save / Save As / Close / Exit.

- Open: `codeFileOpenDialog(buf, sizeof(buf), cwd)` from `editor/edit_filedlg.h`
  — returns 1 and fills a caller-owned buffer. Then `new CodeEditor` +
  `loadFile()` (language already comes from the extension via `lexLangFromPath`).
- Save: `textBuffer()->savefile(path)`, returns 0 on success. Save As:
  `codeFileSaveDialog()` first. Untitled documents route Save → Save As.
- Exit and the window's X button must share one path: set
  `win->callback()`, because the default window callback exits *without* asking
  about unsaved work. Prompt per dirty document with
  `fl_choice("Save changes to %s?", "Cancel", "Save", "Don't save")`.

**Watch:** two comdlg32 details that `edit_filedlg.h` already handles, and which
would each be a baffling failure otherwise:

- **`OFN_NOCHANGEDIR`** — without it the dialog leaves the process in whatever
  directory the user browsed to. The engine and editor both resolve assets
  relative to the working directory, so the first Open would quietly break
  texture and model loading. Probe stage 5 asserts the cwd is unchanged.
- **`lStructSize`** — `OPENFILENAME` gained three members at
  `_WIN32_WINNT >= 0x0500`, which is what these builds compile at. Older
  comdlg32 rejects the larger size with `CDERR_STRUCTSIZE` and never shows a
  dialog at all. We try the compiled size and retry at the legacy 76 bytes if it
  complains.

## Phase 3 — Edit menu  ✅ IMPLEMENTED (awaiting PII verification)

The buffer API already has what is needed:

- **Find**: `search_forward(startPos, str, &foundPos, matchCase)` and
  `search_backward(...)`. Select the hit with `insert_position()` +
  `buffer()->select()`, then `show_insert_position()` to scroll it into view.
- **Replace**: search, then `buffer()->replace(start, end, text)`, then continue
  from the new position. Replace All loops until `search_forward` fails.
- **Go to line**: `buffer()->skip_lines(0, n - 1)` → position →
  `insert_position()` + `show_insert_position()`. Roughly 15 lines including the
  prompt.

Two routes for the dialogs:

| | effort | feel |
|---|---|---|
| `fl_input("Find:")` + F3 for find-next | ~80 lines | serviceable, modal, no match-case toggle |
| modeless `Fl_Window` with input, Match case, Find Next / Replace / Replace All | ~250 lines | what you actually want in a daily editor |

Built the modeless dialog. The search layer lives in `editor/edit_find.h` —
buffer-only, so it is covered headlessly in `edit_code_model_test.cpp` — and
`codeedit.cpp` only puts widgets on top.

Two details worth remembering:

- **Replace All is one undo step.** `CodeUndo` grew a grouping bracket
  (`beginUndoGroup()` / `endUndoGroup()`); records sharing a group id undo and
  redo as a unit. Without it a 50-match sweep needed 50 Ctrl+Z presses.
- **`codeReplaceAll` advances past the REPLACEMENT, not the match**, so
  replacing `a` with `aa` terminates instead of looping forever. There is a test
  for exactly that, and it hangs if the rule is broken.

## Phase 4 — polish  (~200 lines, half a day)

Shortcuts (Ctrl+O/S/W/F/G, F3), a status bar showing line:col and the language,
recent-files list, configurable tab width. `Fl_Text_Editor` already handles
Ctrl+C/V/X and Tab indentation.

## Effort

| Phase | Lines | Time |
|---|---|---|
| 0 · scandir ANSI patch | ~40 | 0.5 day |
| 1 · tabs + own undo stack | ~400 | 1 day |
| 2 · File menu | ~200 | 0.5 day |
| 3 · Edit menu | ~250 | 0.5–1 day |
| 4 · polish | ~200 | 0.5 day |
| **Total** | **~1100** | **3–4 days** |

Estimates assume the Win98 round trip stays short. Every phase should be built and
run on the PII before the next one starts — that is where the surprises have all
come from so far, and none of them were visible on Linux.

## Known FLTK limits worth deciding on now

### Undo — we keep our own stack (decided)

**FLTK's undo is single-level AND global.** `Fl_Text_Buffer`'s undo state is a set
of file-scope statics in `Fl_Text_Buffer.cxx` — one `undobuffer`, and a single
`undowidget` naming the buffer it belongs to. So Ctrl+Z undoes only the most
recent edit, and `undo()` opens with `if (undowidget != this ...) return 0`, which
means **switching tabs discards the undo state of the tab you left.** Unusable for
a daily editor, and tabs make it worse.

**Decision: `CodeEditor` keeps its own undo stack.** Sketch (~150 lines, folded
into Phase 1):

- `struct CodeEdit { int pos; char *inserted; char *deleted; }` — a ring or
  growable array per editor, so it is naturally per-tab.
- Record in the modify callback that `edit_code.h` already installs: on insert,
  keep `(pos, len)`; on delete, keep the `deletedText` FLTK hands the callback
  (copy it — FLTK's pointer does not outlive the call).
- Undo replays in reverse: `remove()` what was inserted, `insert()` what was
  deleted. Redo walks forward again.
- **Set a re-entrancy flag while replaying**, so the modify callback does not
  record the undo as a fresh edit.
- Coalesce consecutive single-character inserts into one record, or every
  keystroke becomes an undo step.
- Call `buffer()->canUndo(0)` to switch FLTK's own undo off and avoid two
  mechanisms fighting over Ctrl+Z; rebind `FL_CTRL+'z'` / `FL_CTRL+'y'` through
  `Fl_Text_Editor::add_key_binding`.

This composes cleanly with the incremental re-highlighter: replaying goes back
through the same buffer-modify path, so `codeStyleUpdate()` restyles the affected
range exactly as it does for typing — no special casing.

Most of it is testable headlessly in `edit_code_model_test.cpp`, with the same
differential trick already used there: after any sequence of edits followed by
undo-to-empty, the buffer and its style buffer must match a freshly loaded one.

Rejected: backporting FLTK 1.4's multi-level undo — it is much larger and 1.4
requires C++11, so it would need adapting to GCC 3.4.

**Markdown wrapping stays off.** See the note in `edit_code.h`: with
`mContinuousWrap`, any keystroke that re-flows a paragraph makes FLTK repaint the
whole widget. `wrapLines(1)` is there if a faster machine ever wants it.

## Word wrap notes

**View / Word Wrap** (Alt+Z) wraps to the window; **View / Wrap at Column...**
wraps at a fixed column, with a dotted guide drawn at that column. Both are per
document, and the column is remembered while wrapping is toggled off and on.
Wrapping is off by default — see the repaint cost in `edit_code.h`.

One FLTK trap is worth remembering, because it fails silently rather than
erroring. `wrap_mode(WRAP_AT_COLUMN, n)` converts the column with `col_to_x()` =
`n * mColumnScale`, and `mColumnScale` is a lazily measured font width that
`textfont()`, `textsize()` and `highlight_data()` all reset to 0. If it measures
as 0, the margin becomes 0 — and `wrapped_line_counter()` reads a 0 margin as
"none set" and falls back to `text_area.w`:

```c
if (mWrapMarginPix != 0) wrapMarginPix = mWrapMarginPix;
else                     wrapMarginPix = text_area.w;
```

so the column is quietly ignored and you get wrap-to-window instead.
`CodeEditor::wrapEnable()` therefore measures the character width itself and
passes `WRAP_AT_PIXEL`, which FLTK takes verbatim. Exact for `CODE_FONT`, which
must stay fixed-pitch — the ruler position and the line-number margin assume it.

## Testing

Keep the split that is already working: anything that does not need a display gets
a headless test on Linux.

- Lexer → `editor/edit_code_test.cpp` (no FLTK at all).
- Incremental re-highlighting → `editor/edit_code_model_test.cpp` (Fl_Text_Buffer
  only, no widget, no X server).
- Find / Replace / Go-to-line are also pure `Fl_Text_Buffer` work, so they belong
  in a model test too — write it alongside Phase 3 rather than after.
- Tabs, menus and dialogs need a display and a human; test those on the PII.

## Related

- `docs/editor-fltk-win98.md` — the FLTK build recipe and all local patches.
  Phase 0 adds a fifth.
- `editor/edit_code.h` — the widget; header comment explains the incremental
  re-highlight and the wrapping decision.
