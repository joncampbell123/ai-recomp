# CAPSCRN.EXE — reconstructed source

C reconstruction of `CAPSCRN.EXE` (CapScrn 1.1, "AVI Screen Capture", from Video for Windows 1.1
on Windows 3.11, MD5 `51abd4aaf2881136e3906ff99c1c70fa`, 39,936 bytes). It was made from the
binary with Ghidra 12.1.4 (headless), ndisasm and manual analysis. The original was built with
Microsoft C 7 / Visual C++ 1.x (LINK 5.50) as a medium-model application with 386 code.

## Binary layout → source files

| NE segment | Size | Contents | File |
|---|---|---|---|
| 1 CODE PRELOAD | 0x1A0E | main window, settings, Preferences and Set Capture File dialogs, capture | `capscrn.c` |
| 2 CODE PRELOAD | 0x05A8 | "ComArrow" spin control for the frame rate | `arrow.c` |
| 3 CODE PRELOAD | 0x0870 | start-up box (`CSIntroBox`) | `intro.c` |
| 4 CODE PRELOAD | 0x2193 | Microsoft C runtime, entry point seg4:001A (not reconstructed, see below) | — |
| 5 DATA (DGROUP) | 0x128E | strings, settings, buffers; local heap 0x1000, stack 0x1800 | see `capscrn.h` |

Also: `capscrn.h` (resource IDs, DGROUP map, prototypes), `capscrn.def` (from the NE header),
`capscrn.rc` and `res/` (icon, Preferences dialog, strings, accelerators, the intro bitmap,
version block), and `watcom/` (Open Watcom build and the UI test script).

Every function has a header comment with its `seg:offset` range and calling convention. The
functions are in the order of the binary.

## How it works

* **An icon only.** The main window is created minimised and `WM_QUERYOPEN` refuses to restore it.
  All commands are items appended to its system menu: Set Capture File, Set Capture Window,
  Preferences, Capture, Copy Frame, Edit Capture and Help. A second instance shows "Capscrn is
  already running." and quits.
* **Capture.** CapScrn makes a hidden AVICAP capture window (`capCreateCaptureWindow`) and connects
  it to the screen capture driver. It finds the driver by description ("Screen Capture Driver")
  among the `MSVideo` drivers; if it is missing, it is added to `[Drivers]` of SYSTEM.INI as
  `MSVideo9=Scrncap.drv`. Capture hides the icon and sends `WM_CAP_SEQUENCE` with:
  * the frame rate from Preferences (1-20 fps, or a fraction such as 0.5);
  * the stop key, with `0x8000` for Ctrl and `0x4000` for Shift, as `vKeyAbort`;
  * a time limit of `30000 / fps` seconds, 4 video and 10 audio buffers, no yield, no mouse abort;
  * audio if it is enabled in Preferences.

  The capture loop calls CapScrn's yield callback, which dispatches one message. When the capture
  is done the file name is incremented (if enabled), Edit Capture becomes available, and a
  frame count of 29,900 or more, file errors and set-up errors are reported.
* **Set Capture Window** is the capture driver's video format dialog (`WM_CAP_DLG_VIDEOFORMAT`).
* **Copy Frame** grabs one frame and copies it to the clipboard. **Edit Capture** runs
  `VIDEdit -n <last capture>`.
* **Settings** are in `[CapScrn]` of MMTOOLS.INI: `fps`, `HotKey`, `Ctrl`, `Shift`, `Audio`,
  `File`, `IncFile` and `OnTop`. The audio format comes from AVICAP and is chosen with the ACM
  format chooser (ACM 2.0 or later only).
* **Help.** F1 in a dialog or menu is caught by a `WH_MSGFILTER` hook and opens CAPSCRN.HLP at
  that dialog's topic (the index otherwise). The help file is the module's file name with
  `.hlp`.
* **Always on top.** With OnTop set the icon is `HWND_TOPMOST`. While a dialog is up it is moved
  to `HWND_BOTTOM` (not `HWND_NOTOPMOST`), and back on top afterwards.
* **Capture file names.** `ParseFileNumber` takes the trailing digits of the file name and builds
  a `wsprintf` format for the next one: `CAP0007.AVI` → `CAP0008.AVI`. It gives up when all the
  digits are 9s.
* **Palette changes during a capture** stop it with a message box.

## Notable findings

* **Stop key 'Z' comes back as F1.** The Preferences list is "Esc", 'A'-'Z', F2-F12, but reading
  the selection back counts only 25 letters. F1 is not in the list, so the next time the selection
  is whatever is in an uninitialised variable (the same happens for any key that isn't listed).
* **Fractional frame rates lose the integer part.** On OK the rate is first read as an integer
  (clamped to 1-20); if the text contains a '.', the loop that finds it also clears every
  character before it, so only the fraction is converted: "0.5" works, "7.5" gives 0.5 fps.
* **The intro box shows Ctrl + Shift + F1 as "Ctrl + Shift + p".** Only F2-F12 are formatted as
  "F%d"; any other key is shown as its virtual key code taken as a character (F1 is 0x70, 'p').
* **`WM_CLOSE` sends `WM_CAP_DRIVER_DISCONNECT` to the main window** instead of the capture window.
  The driver is disconnected anyway when the capture window is destroyed with its parent.
* **Not freed:** the proc instances of the three capture callbacks. The intro box deletes its
  font unless it is `SYSTEM_FONT`, but the stock font it falls back to is `ANSI_VAR_FONT`.
* **Overwrite confirmation only with incrementing on.** `ConfirmOverwrite` asks before capturing
  over an existing file only when IncFile is set.
* **Callbacks without a DS prologue.** The exported window procedures have no code to load DS:
  they rely on DS being DGROUP when they are called, which holds for a single-instance application.
  The capture callbacks are `_loadds`.
* **Unreferenced code:** `IntroBoxDone` (seg3:076E). `IncrementFileName` is passed the capture
  file name as a second argument that it does not use.

## C runtime (seg4), not reconstructed

This is the Microsoft C Windows application runtime: the startup code (`InitTask`, `__astart`,
which calls `WinMain`), the 8087 emulator hooks (`WIN87EM`), heap and environment set-up,
run-time error messages, `___EXPORTEDSTUB` (seg4:13D4) and the library routines CapScrn calls:

| Offset | Routine | Called from |
|---|---|---|
| 00C4 | `atol` | `ArrowEditChange` |
| 00C8 | `atof` | `FrameRateToUSec` |
| 0114 | `_splitpath` | `SetCaptureFile` |
| 0268 | `_makepath` | `SetCaptureFile`, `IncrementFileName` |
| 03C4 | `_fstrncmp` | `ConnectDriver` |
| 0400 | `floor` | `FrameRateToUSec` |
| 045A | double → long conversion | `FrameRateToUSec`, `ReadSettings`, `PrefsDlgProc` |

`strlen`, `_fmemset` and `_fmemcpy` were expanded inline. The names describe behaviour; they were
not matched against the actual library.

## Building with Open Watcom

```sh
WATCOM=~/src/open-watcom-v2/rel ./watcom/build.sh
```

This writes `watcom/out/capscrn.exe` (33,314 bytes; the build is reproducible). Watcom's tools
can't handle the spaces in this path, so the script works with relative paths. The build is a
medium-model Win16 application with 386 code, the three code segments kept separate
(`option packcode=0`) in the original order, the same exports by ordinal, imports of AVICAP and
MSACM by ordinal, the original's stack and heap sizes, description, expected Windows version
(3.10) and PROTMODE flag. It differs from the original in these ways:

* **Runtime.** It uses Watcom's startup and C library instead of Microsoft's; there is no
  `___EXPORTEDSTUB`.
* **Callbacks.** `-zWs` gives the exported procedures Watcom's "smart callback" entry code (DS
  loaded from SS). The original's have no entry code.
* **Headers.** Open Watcom ships no AVICAP or ACM headers. `../common/watcom/avicap.h`, `msacm.h`
  and `mmreg.h` are stand-ins with only what these programs use, with structure offsets checked
  against the original's accesses.
* **Version resource.** `wrc` puts the build time in `dwFileDateLS`; `build.sh` zeroes it, as in
  the original.

## Status and verification

Verified with an A/B UI test under Windows 3.11 with Video for Windows 1.1 (DOSBox-X, headless,
S3 Trio64 at 800×600, 256 colours). Each run starts from a fresh copy of the same Windows
installation; `../common/watcom/uitest` drives the program from a script and logs window trees,
dialog contents, menus, INI values, screenshots, the clipboard and the captured AVI's structure.
`watcom/test/capscrn.uit` covers:

* the intro box (default stop key Esc, and Ctrl + Shift + F1), the icon and its system menu;
* Preferences twice: defaults, then every control changed, then what came back (including the
  'Z' → F1 quirk);
* Set Capture File, typing a new name;
* a capture of a few seconds, stopped by posting `WM_CAP_STOP` to the capture window, then the
  AVI's chunk structure (headers, stream formats, index; frame counts vary) and the incremented
  file name in the title;
* Copy Frame (the clipboard DIB), Edit Capture (VidEdit starts with the capture), Set Capture
  Window (the screen capture driver's dialog), Help (WinHelp opens), a second instance;
* the settings in MMTOOLS.INI and SYSTEM.INI after quitting.

Result: all 313 log lines and 7 screenshots are identical for the original and the rebuild
(`uidiff.py`).

Not tested: audio capture and the ACM format chooser (no wave input in the headless setup),
stopping with the hot key, the error paths (driver missing, disk full), and palette changes
during a capture.

To repeat the test, build `../common/watcom/uitest`, copy `UITEST.EXE`, the script (as
`SCRIPT.UIT`), `CAPSCRN.EXE`, `CAPSCRN.HLP` and `VIDEDIT.EXE` to `C:\TEST` of a Windows 3.1
installation with Video for Windows 1.1, and run
`win C:\TEST\UITEST.EXE C:\TEST\SCRIPT.UIT C:\TEST\UI.LOG C:\TEST\IMG`. Then compare two runs with
`uidiff.py A\TEST B\TEST`.
