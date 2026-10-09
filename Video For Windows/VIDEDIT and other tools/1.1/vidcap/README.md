# VIDCAP.EXE — reconstructed source

C reconstruction of `VIDCAP.EXE` (VidCap 1.1, "Video Capture app", from Video for Windows 1.1 on
Windows 3.11, MD5 `0ad5d7ad8e4832d321af388ea670718f`, 138,816 bytes). It was made from the binary
with Ghidra 12.1.4 (headless), ndisasm and manual analysis. The original was built with Microsoft C 7
(LINK 5.50) as a medium-model application with 386 code and inline 8087 code (WIN87EM).

## Binary layout → source files

| NE segment | Size | Contents | Files |
|---|---|---|---|
| 1 CODE PRELOAD | 0x8D6E | helpers, intro/About box, the spin control, WinMain and the menu commands, DOS helpers, capture format and palette, capture file commands, the Microsoft C runtime (5550-8CDB), the muldiv32 module | `misc.c` `intro.c` `path.c` `arrow.c` `vidcap.c` `dosutil.c` `video.c` `file.c` `util.c` (runtime) `muldiv.c` |
| 2 CODE PRELOAD | 0x1614 | settings, help, start-up and shut-down, the bars | `profile.c` `help.c` `init.c` |
| 3 CODE PRELOAD | 0x32CC | the frame window with its scroll bars, the video window, the main window, the toolbar control | `frame.c` `show.c` `ctrl.c` `toolbar.c` |
| 4 CODE | 0x0069 | registering the spin control | `arrowini.c` |
| 5 CODE | 0x162A | dialogs: Set File Size, Capture Video Sequence, Recording Level, Initialization Error, Preferences, Audio Format | `dialogs.c` |
| 6 CODE | 0x1E56 | histogram and median-cut palette | `dibmap.c` |
| 7 CODE | 0x2B86 | streaming capture to AVI; the status bar | `capavi.c` `status.c` |
| 8 CODE | 0x2FF8 | Capture Frames, MCI control and MCI step capture, frame averaging | `capframe.c` `mcicap.c` `average.c` |
| 9 DATA (DGROUP) | 0x3072 | local heap 0x1000, stack 0x2000 | see `vidcap.h`; `data.c` |

Also: `vidcap.h` (resource IDs, the DGROUP map with DS offsets, prototypes), `vidcap.def` (from the
NE header), `vidcap.rc` and `res/` (icon, toolbar bitmap, menu, accelerators, dialogs, strings, file
filters, the About picture, version block), and `watcom/` (Open Watcom build and the UI test).

Every function has a header comment with its `seg:offset` range, calling convention and quirks; the
functions are in the order of the binary. Far functions are `_cdecl` unless marked `PASCAL`; a few
near helpers (`NEAR PASCAL`, `_fastcall`) are noted. Every far function of the original has the
Windows prologue (`mov ax,ds / nop / inc bp ...`). Only 14 functions are exported: the window
procedures, the COMMDLG hook, the message filter hook, Capture Palette's and Capture Frames' dialog
procedures, and `___EXPORTEDSTUB`. The other dialog procedures are not exported.

Module boundaries come from the order of the initialised data (link order: misc, intro, path, arrow,
vidcap, profile, help, init, dialogs, frame, *data*, dosutil, init, video, capframe, mcicap, file,
toolbar, status, util, average) and the order of code within the segments. DS:01D4-0285 (the capture
settings and driver channels) belongs to a module without code in seg1, probably the capture module;
it is `data.c` here, with the uninitialised variables used everywhere. The names are made up, except
those that survive in the binary (exports, class names, profile keys) and those borrowed from the VfW
SDK and AVICAP where the code is plainly the same (`dibmap.c`, the `capavi.c` routines that became
AVICAP's `capavi.c`, the `muldiv32` routines).

## How it works

* **Start-up.** `AppInit` shows the start-up box (unless `-n`), opens the capture driver
  (`-d<n>` picks driver *n*, default 0: `[drivers] MSVideo`, `MSVideo1`... of SYSTEM.INI) with three
  channels: `VIDEO_IN` (format, frames, streaming), `VIDEO_EXTERNALIN` (Video Source) and
  `VIDEO_EXTERNALOUT` (Video Display and overlay). Without a driver the Initialization Error dialog
  offers to continue or exit. The window is the toolbar (a copy of Media Player's toolbar control),
  the frame window with scroll bars around the video window, and the status bar.
* **Settings** are in `[VidCap]` of MMTOOLS.INI: window position and size, `StatusBar`, `ToolBar`,
  `CenterImage`, `BackgroundColor`, `CapFile`, `MicroSecPerFrame`, `TimeLimit`, `LimitEnabled`,
  `Sound`, `Frequency`, `Channels`, `Bits`, `CaptureToMemory`, `MCIDevice`, `IndexSizeInKFrames`,
  `AverageFrames`, `StepCaptureAt2x`, `Yield`, `LiveWindow`, `OverlayWindow`. Values equal to the
  default are not written.
* **The capture file** is set with Set Capture File (a COMMDLG dialog with a hook and its own template
  with a New Size button). A new file is created at the size given in Set File Size (written to its
  end, so the disk space is allocated before capturing). Edit Captured Video runs `VIDEdit -n
  <file>`; Save Captured Video As copies the file with a progress line in the status bar.
* **Capture/Video** (`capavi.c`) is the streaming engine that later became AVICAP. It leaves 2 KB at
  the start of the file for the header, then writes each frame and audio buffer as a RIFF chunk padded
  with JUNK to a 2 KB boundary, and keeps a DWORD index entry per chunk (size plus audio, key frame,
  dummy and granular flags). Video buffers (up to 1000) come from memory below 1 MB when capturing to
  disk, so DOS writes them without copying, or from global memory with a DOS bounce buffer when
  capturing to memory. Frames late by more than one frame time are made up with up to 248 empty
  "dummy" chunks. With audio, four half-second wave buffers are recorded and the frame rate written
  to the header is recomputed from the amount of audio. SmartDrive caching of the capture drive is
  switched off while capturing. The capture stops on Escape or a mouse click, at the time limit, at
  the end of the MCI segment, when the index (IndexSizeInKFrames) is full, or on an error. The header
  (RIFF AVI, hdrl with the VfW 1.0 main header, video and audio stream headers, the palette, INFO
  chunks with the date and the SMPTE start time) and the idx1 index are written afterwards.
* **Capture/Frames** appends single frames on request; **Capture/Single Frame** grabs one frame into
  the window. **Capture/Palette** grabs RGB555 frames, counts their colours in a 128 KB histogram and
  makes an optimal palette with a median cut (`dibmap.c`), which is then set on the driver with the
  RGB555-to-index table.
* **MCI control** of a videodisc or VCR (the MCI Settings dialog): with "Play Video" the device plays
  while Capture/Video streams; with "Step Video", MCI step capture steps the device frame by frame,
  grabs a frame each time the position passes the time of the next frame (with optional temporal
  averaging of several grabs and 2x spatial averaging from a double-size format), then replays the
  segment to record the audio.
* **Help.** F1 is caught with a `WH_MSGFILTER` hook and opens VIDCAP.HLP at the current dialog's
  context.

## Notable findings

* **SMPTE time at 25 fps.** The ISMT chunk written for MCI captures formats the start time as
  hh:mm:ss:ff with 25 frames a second, from hundredths of a second.
* **The 32-bit 2x averaging reads source pixels 3 bytes apart** (it treats them as RGB triples);
  the averaging add, average and clear loops all leave out the last pixel; the sum buffer is
  `biSizeImage * 6` bytes whatever the depth; a 5-to-8 bit table built for 16-bit frames is unused.
* **Palette colours are scaled by 255/32**, so the median-cut palette never has a component above 247.
* **MCI Settings** keep "use start/stop time" flags that are set when the dialog opens and never
  cleared; pressing OK while Set Start/Set Stop is reading the position posts OK again but also
  carries it out. `MCICountDevices` returns an uninitialised value when `MCI_SYSINFO` fails.
* **`WriteIndex`** returns TRUE without writing anything when the index overflowed, and its last
  `mmioAscend` uses the global file handle rather than its argument; its result is ignored.
* **Capture/Video** leaves the wait cursor, the preview DC and the compressor running when the file
  can't be opened, and never releases the preview DC if `DrawDibDraw` fails during the capture.
* **`SetInfoChunk`** (AVICAP's) only reallocates and returns TRUE when the removed chunk is not the
  last one.
* **Capture Video Sequence**: a time limit over 32767 is replaced and then treated as 20; OK with a
  zero time limit tries to cancel, but a second `EndDialog` makes it OK anyway.
* **Settings**: the capture file name is compared through a 30-character buffer, so a longer name is
  always written again; an unknown background colour writes an uninitialised buffer. A "None" string
  next to the background colour names is unused (MCI control off is written as an empty MCIDevice).
* **Command line**: the digit test of `-d` is `c >= '0' || c <= '9'`, and a command line ending in
  `-` or `/` loops for ever.
* **Toolbar** (from Media Player): the focused pressed look never shows, removing a button copies one
  entry past the end, Space up adds the right-button flag to the focus index.
* **Audio Format / Recording Level**: the audio menu items depend on wave *output* devices, and the
  average bytes per second of the level meter's format is a 16-bit product.
* **`OpenVideo`** turns capture off on a 286. `XmsQueryFree` returns the conventional memory size
  (INT 12h after the XMS call); `GetVolumeLabel` sets only CL as the loop count.
* **`SplitPath`** checks the extension by looking at the start of the name.
* **Copy** has a test for 8 *and* 24 bits per pixel; Load Palette never restores the cursor.
* **Unreferenced code:** `DibReduce` and the `Reduce*` routines, `GetWaveInPosition`,
  `MCIDeviceStop`, `GetShowDC`, `StartAutoScroll` and the four `Scroll*To*` helpers, most of
  `dosutil.c`, and the truncating `muldivt32`.
* **Non-exported dialog procedures** load DS with `mov ax,ds`, so they depend on Windows entering
  them with DS already DGROUP; the COMMDLG hook and the message filter hook are exported and get DS
  from their thunks.

## C runtime (seg1:5550-8CDB), not reconstructed

The Microsoft C Windows application runtime: start-up (entry point seg1:5556, `InitTask`, `WinMain`),
the 8087 emulator hooks, heap and environment set-up, run-time error messages, `___EXPORTEDSTUB`
(seg1:7902) and these routines called by VidCap:

| Offset | Routine | Called from |
|---|---|---|
| 5600 | `atoi` | `ArrowEditChange`, `MCITimeToMs`, settings |
| 5604 | `atol` | spin controls, `StrToLong`, `MCIDeviceGetPosition` |
| 5608 | `atof` | `FrameRateToUSec` |
| 5654 | `sprintf` | `USecToFrameRate` ("%.3f") |
| 56B2 | `ctime` | the IDIT chunk |
| 56DE | `time` | the IDIT chunk |
| 5770 | `strchr` | `MCITimeToMs` |
| 579A | `strrchr` | `MCITimeToMs` |
| 57C2 | long signed divide | many |
| 585C | long multiply | many |
| 588E, 58AE | long divide/multiply assign | `video.c`, `dialogs.c` |
| 58CE | long unsigned divide | many |
| 592E | long unsigned remainder | `dibmap.c`, `capavi.c`, `mcicap.c` |
| 5998 | `floor` | `FrameRateToUSec` |
| 59F2 | double → long | `FrameRateToUSec`, `capavi.c` |

`_fmemset`, `_fmemcpy` and `strlen` were expanded inline. The names describe behaviour; they were not
matched against the actual library.

## Building with Open Watcom

```sh
WATCOM=~/src/open-watcom-v2/rel ./watcom/build.sh
```

This writes `watcom/out/vidcap.exe` (140,848 bytes; the build is reproducible). Watcom's tools
can't handle the spaces in this path, so the script works with relative paths; `watcom/sources.txt`
lists the source files with their code segments. The build is a medium-model Win16 application with
386 code, the eight code segments kept separate (`option packcode=0`) in the original order (1-3
preloaded, all discardable), the same exports by ordinal (but no ordinal 11), MSVIDEO imported by
ordinal, COMMDLG and MMSYSTEM from Watcom's import libraries, the original's stack and heap sizes,
description, expected Windows version (3.10) and PROTMODE flag. It differs from the original in these
ways:

* **Runtime.** Watcom's start-up and C library instead of Microsoft's, in a ninth code segment
  (`_TEXT`); no `___EXPORTEDSTUB`. The DGROUP layout is Watcom's.
* **Callbacks.** Watcom gives the functions no DS-loading prologue. Window and dialog procedures are
  entered with DS = DGROUP, so this only matters for the three callbacks that are entered with another
  DS: the COMMDLG hook (called from COMMDLG's dialog procedure), the `WH_MSGFILTER` hook and the spin
  control's timer procedure, which are `_loadds`. (Without it, Set Capture File crashed.)
* **Inline assembly.** The histogram helpers that use FS and 32-bit offsets are `#pragma aux` inline
  code instead of near `_fastcall` functions, and `dibmap.c` is compiled with `-zfp -zgp` so Watcom
  doesn't use FS itself. `IncHistogram` saturates with add/sbb instead of a compare and jump.
  `dosutil.c` and `muldiv.c` are inline assembly as in the original module.
* **Names.** `GetEnvironment` (a Windows API name) is `DosGetEnvironment`; the truncating multiply-
  divide is `muldivt32`, as `MulDiv32` would link as the same PASCAL name as `muldiv32`.
* **Headers.** `../common/watcom/msvideo.h` and `avifmt.h` are stand-ins for the VfW 1.1 DK headers.
  The main AVI header fields VidCap writes as `dwScale`/`dwRate`/`dwStart`/`dwLength` (the VfW 1.0
  layout) are `dwReserved[0..3]` there.
* **Version resource.** `wrc` puts the build time in `dwFileDateLS`; `build.sh` zeroes it.

## Status and verification

Verified with an A/B UI test under Windows 3.11 with Video for Windows 1.1 (DOSBox-X, headless,
S3 Trio64 at 800×600, 256 colours), each run from a fresh copy of the same Windows installation,
driven by `../common/watcom/uitest` with `watcom/test/vidcap.uit`. The capture source is the screen
capture driver SCRNCAP.DRV, which the script sets up as `[drivers] MSVideo` of SYSTEM.INI. The script
covers:

* the main window (toolbar, status bar, frame and video window), its menus;
* Preferences: defaults, every control changed (status bar off, centring off, black background,
  324,000 frames), the window without its status bar, what came back; F1 in the dialog (the
  `WH_MSGFILTER` hook opens VIDCAP.HLP);
* Audio Format: defaults, Level... (no wave input in this setup: the error message), 16 bit stereo
  22 kHz, what came back;
* Video Format (the driver's Capture Window dialog), Video Source and Video Display (nothing with
  this driver), Video Compression (`ICCompressorChoose`);
* Set Capture File with a new file name and Set File Size (1 MB), the title;
* Capture Frames with two frames, the AVI's chunk structure; Capture Single Frame (the window image);
* Save Single Frame (`FRAME.DIB`), Save Palette (`TEST.PAL`), Copy (the clipboard DIB), Paste Palette,
  Load Palette;
* Capture Palette (fails: the driver has no RGB555 format);
* Capture Video: the dialog, MCI Settings (opened with no MCI devices, defaults 10 and 20 s), a 3
  second capture to disk and one to memory, the AVIs written;
* Save Captured Video As, Preview on and off (status text), About, Help (WinHelp), Edit Captured
  Video (VidEdit starts with the capture file), Exit, and the settings in MMTOOLS.INI.

Result: 857 log lines and 15 images; all are identical for the original and the rebuild except these,
which differ between runs of the original too or come from uninitialised memory:

* a hidden scroll bar's range in MSVIDEO's Video Compression dialog (`500..2247`, `..2263`,
  `..2295` in different runs of the original);
* `FRAME.DIB` and the clipboard DIB (the `COPY` image): identical up to the RLE end-of-bitmap mark;
  VidCap writes the whole 96,000-byte frame buffer, and the rest is whatever was in memory;
* the CRC of `CAPTURE.AVI`: the capture date in the IDIT chunk, and the old contents of the
  preallocated file after the AVI (the frames captured earlier, with the same stale tails).

Two bugs of the rebuild were found this way and fixed: the COMMDLG hook needed `_loadds` (Set Capture
File crashed), and MCI control off is written as an empty `MCIDevice`, not "None".

Not tested: frame capture by streaming — the screen capture driver delivers no frames to VidCap's
streaming capture in this setup (with the original too), so Capture/Video ends with "No frames
captured" after the time limit and the streaming loop only runs empty; audio capture and the
recording level meter (no wave input); MCI control and MCI step capture, and so the frame averaging
(no MCI videodisc or VCR); overlay (the driver has none); the spin controls' auto-repeat; error paths
such as a full disk.

To repeat the test, build `../common/watcom/uitest`, copy `UITEST.EXE`, the script (as
`SCRIPT.UIT`), `VIDCAP.EXE`, `VIDCAP.HLP` and `VIDEDIT.EXE` to `C:\TEST` of a Windows 3.11 + VfW 1.1
installation with SCRNCAP.DRV in `WINDOWS\SYSTEM`, run `win uitest SCRIPT.UIT UI.LOG IMG` there for
each binary, and compare the two directories with `../common/watcom/uitest/uidiff.py`.
