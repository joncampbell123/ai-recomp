# VIDEDIT.EXE — reconstructed source

C reconstruction of `VIDEDIT.EXE` (VidEdit 1.1, the AVI editor of Video for Windows 1.1, on
Windows 3.11; MD5 `840b3657148e73ca12662d40fd4b61e1`, 237,888 bytes). It was made from the binary
with Ghidra 12.1.4 (headless), ndisasm and manual analysis. The original was built with Microsoft
C 7 / Visual C++ 1.x (LINK 5.50) as a medium-model application with 386 code, from about 38 C
modules and two assembly modules.

VidEdit is a client of the Multimedia Development Kit framework: the Media Manager
(`MEDIAMAN.DLL`, media elements with type handlers; AVI files are read and written by the
`MEDDIBS` handler), the Workbench (`WRKBENCH.DLL`: tool registration, file dialogs, the About box,
progress bars) and `WINCOM.DLL`. Video for Windows supplies DrawDib and the compressors
(`MSVIDEO.DLL`).

## Binary layout → source files

| NE segment | Size | Contents | Files |
|---|---|---|---|
| 1 PRELOAD | 0x5ED1 | `0000-000F` 16 zero bytes | — |
| | | `0010-2FEB` WinMain and commands, undo, `hmemmove` and `muldiv32` (assembly modules, as C), files | `videdit.c` `undo.c` `hmem.c` `file.c` `muldiv32.c` |
| | | `2FEC-5ED0` Microsoft C runtime (entry point 2FF6; not reconstructed) | — |
| 2 PRELOAD | 0x1356 | initialisation (functions of several modules, the init segment) | `init.c` |
| 3 PRELOAD | 0x016E | termination | `term.c` |
| 4 PRELOAD | 0x4B2A | the view window, the frame window and zoom, Edit commands, palette dialogs, Synchronize, the SDK's DIB.C | `view.c` `framewnd.c` `edit.c` `dib.c` |
| 5 PRELOAD | 0x805E | main window and status bar, dialog helpers, intro box, `SplitPath`, `comarrow`, `GrayBlob` panel, tool bars, control bars and the selection, `aviframebox`, the `AnimSTrackBar` trackbar | `vedwnd.c` `dlgutil.c` `intro.c` `splitpth.c` `arrow.c` `grayblob.c` `toolbar.c` `ctrlbars.c` `framebox.c` `trackbar.c` |
| 6 | 0x70AC | the frame table, palettes and remap tables, drawing, Video Format, full frames from key frame + deltas | `frames.c` `framerct.c` `paltable.c` `remap.c` `framedrw.c` `fullfrm.c` |
| 7 | 0x1ACA | the audio track and the wave clipboard | `wave.c` |
| 8 | 0x008F | MIDI track stubs (compiled out) | `midistub.c` |
| 9 | 0x024F | Preferences | `prefs.c` |
| 10 | 0x0059 | unreferenced `ComArrow` registration | `arrowini.c` |
| 11 | 0x1481 | Insert/Extract/Save As dialogs; saving through MediaMan | `filedlg.c` `savefile.c` |
| 12 | 0x1BBE | Compression Options | `compress.c` |
| 13 | 0x047E | RLE8 decompression | `rle.c` |
| 14 | 0x01E0 | Go To | `goto.c` |
| 15 | 0x0065 | unreferenced: play with Media Player | `mplayer.c` |
| 16 | 0x3452 | histograms and the optimal palette (the PALMAP sample's DIBMAP), inverse colour map, bit depth conversion | `dibmap.c` `invcmap.c` `dibconv.c` |
| 17 | 0x06C9 | Statistics | `stats.c` |
| 18 | 0x0BFA | the crop rectangle tracker | `croprect.c` |
| 19 | 0x0898 | Play Preview (MCIAVI) | `preview.c` |
| 20 | 0x05ED | Crop | `crop.c` |
| 21 DATA | 0x327A | DGROUP; local heap 0x1400, stack 0x4000 | `shared.c` and each file's own data |

Headers: `videdit.h` includes everything; `resource.h` (menu commands, dialogs), `shared.h`
(globals used by more than one segment, with `shared.c`), and one header per part:
`vemain.h` (segments 1-3, 8-10, 13-15), `vedwnd.h` (5), `frames.h` (6), `edit.h` (4, 7),
`compress.h`, `filedlg.h`, `savefile.h`, `preview.h`, `crop.h`, `stats.h`, `croprect.h`,
`dibmap.h`. Stand-in headers for MediaMan, the Workbench, WINCOM and MSVIDEO are in
`../common/watcom`.

Every function has a header comment with its `sNN:start-end` range, calling convention and the
quirks of the original. Globals carry their `DS:` offset; initialised ones have the original's
initial values. Each C module's data starts with its own copies of the strings
"AnimSTrackBar" and "GrayBlob" (from a shared header), which shows where the modules' data begins
in DGROUP; the files follow those module boundaries where they fall inside a segment.

## How it works

* **The movie.** The video track is a huge array of 0x20-byte `FRAME`s (`frames.c`), each naming a
  MediaMan video element and a frame in it (or a DIB in memory), with source and destination
  rectangles, a palette and a colour remap table. Editing moves `FRAME`s around; nothing is
  decompressed until it's shown or saved. The audio track is a MediaMan WAVE element edited with
  the `MEDWAVE.MMH` messages (`wave.c`); the MIDI track is compiled out.
* **Files.** Open and Insert go through the Workbench file dialogs and `medLocate`/`medAccess`; the
  AVI handler (`MEDDIBS`) answers the info, stream, format and frame messages. Save writes a
  temporary file through a temporary physical type ('AVIE' or the next free code) whose handler
  (`SaveHandler`) serves the edited movie, recompressing with the chosen compressor or passing
  frames through as stored; then it deletes the original and renames.
* **Frames on screen.** 8-bit frames are drawn with `StretchDIBits` and `DIB_PAL_COLORS` against
  an identity palette; other depths go through DrawDib. RLE deltas are drawn with their skipped
  pixels transparent, and full frames are built from the last key frame (`fullfrm.c`, using
  DIB.DRV when it's installed). "Fast Frame Update" draws only the current frame's data.
* **Undo** keeps one level: edits are undone by running them in reverse with the insert mode
  swapped.
* **Palettes.** Create Palette builds a median-cut palette from a 15-bit histogram of the chosen
  frames (`dibmap.c`), Paste Palette applies the clipboard palette with remap tables built from an
  inverse colour map (`invcmap.c`), and Video Format converts every frame's bit depth
  (`dibconv.c`).
* **Settings** are in `[VidEdit]` of MMTOOLS.INI; compression defaults also in SYSTEM.INI.

## Notable findings

* **Window procedures** are not exported. Most have no DS-loading code and rely on DS being DGROUP;
  those in segment 5 use MSC's smart-callback entry (`mov ax,ss … mov ds,ax`).
* **Undoing Crop, Resize, Synchronize or a format change** tests an uninitialised local, so whether
  an error box appears depends on what is on the stack.
* **Bugs kept as in the original** (each is commented in the code):
  * `PalTableFree` and `RemapTableFree` free entry [0] repeatedly instead of each entry.
  * `ReplaceLostMedid` clears `wFrame` in the movie's frame at the clipboard entry's index.
  * `TransparentStretchBlt` swaps source and destination coordinates without DIB.DRV, so it only
    works at (0,0); `CropFrames` mixes destination and source coordinates.
  * `toolbarRemoveTool` copies one entry past the end; the trackbar passes the SB_ code to
    `DefWindowProc` in place of the key, `WM_MOUSEMOVE` falls into the `WM_KEYUP` code, and
    `WM_DESTROY` deletes the thumb bitmaps all trackbars share.
  * `WaveRenderClipboard` tests a byte count before setting it, so rendering CF_WAVE depends on
    stack contents.
  * The compressor search in Compression Options never looks at item 0; Statistics loads the wait
    cursor from VidEdit's own instance (which has none) and retries an unreadable frame forever.
  * `DibReduce` computes 24-bit row sizes in 16 bits (wrong above 2730 pixels); several DIB
    converters use a global handle as a selector without locking it and read 8-bit pixels as
    WORDs; `DibConvertBitCount` sends 1-bit DIBs down the 4-bit paths.
  * The crop rectangle's width collapses at 0 but its height only when negative.
  * A lone "-" or "/" at the end of the command line hangs `AppInit`; unknown options become part
    of the file name; `RunVidCap` runs "VidCap -n " without the file name.
* **Dead code:** the `ComArrow` registration (s10), Play with Media Player (s15), the MIDI stubs,
  the About box variant of the intro box, `muldivru32`, `RleHasSkip`, `HasPaletteChanges`,
  `FindPaletteChange`, `CropGetRect`, and much of DIB.C.
* **The Workbench open functions** (`WrkOpenFileName`, `WrkOpenDialog`) fill in an 8-byte
  `MedReturn`, not a MEDID.

## C runtime (s01:2FEC-5ED0), not reconstructed

Startup (`__astart` at 2FF6), heap, environment, the 8087 emulator hooks, run-time error messages,
`___EXPORTEDSTUB` (4C60), and the library routines the application calls: 30A0/30A4 `atoi`/`atol`,
30A8 `atof`, 30F4 `sprintf`, 3152 `strchr`, 317C `strrchr`, 31A4 `_dos_getfileattr`, 326A long
multiplication, 3366 `floor`, 33C0 double → long conversion. The names describe behaviour; they
were not matched against the actual library.

## Building with Open Watcom

```sh
WATCOM=~/src/open-watcom-v2/rel ./watcom/build.sh
```

This writes `watcom/out/videdit.exe` (260,424 bytes; the build is reproducible).
`watcom/sources.txt` lists the files with their code segments in the original order (packcode is
off, so the 20 code segments stay separate), `watcom/imports.lnk` the MEDIAMAN, WRKBENCH, WINCOM
and MSVIDEO imports by ordinal. Differences from the original:

* **Runtime.** Watcom's startup and C library replace Microsoft's (an extra code segment); there is
  no `___EXPORTEDSTUB`.
* **Callbacks.** `-zWs` gives every far function Watcom's smart-callback entry (DS loaded from SS).
* **Assembly as C.** `hmemmove`, `muldiv32` and the RLE decoders are C; the histogram code uses a
  huge pointer where the original used FS and 32-bit offsets.
* **Headers.** The MediaMan declarations follow MEDIAMAN.H of the 1990 Multimedia Extensions beta
  SDK (argument sizes checked against the VfW 1.1 DLL); the Workbench and WINCOM declarations were
  worked out from the DLLs' RETF sizes and VidEdit's call sites.
* **Version resource.** `build.sh` zeroes the build time `wrc` writes into `dwFileDateLS`.

## Status and verification

Verified with A/B UI tests under Windows 3.11 with Video for Windows 1.1 in DOSBox-X (headless, S3
at 800×600, 256 colours), each run in a fresh copy of the same Windows installation, driven by
`../common/watcom/uitest` and compared with `uidiff.py`:

* `watcom/test/videdit_a.uit` and `videdit_b.uit` use `TEST8.AVI` made by `watcom/test/mkavi.py`
  (30 uncompressed 8-bit frames with PCM audio, all key frames, so every frame is deterministic).
  They cover the main window, menus and tool bars, Preferences (title, time format, background),
  Go To, Set Selection, Copy/Cut/Paste (the clipboard DIB), zoom, Convert Frame Rate, Statistics,
  Resize, Crop, Video Format (8 → 24 bit), Create Palette, Paste Palette, Synchronize, Compression
  Options, Save As (uncompressed), Extract, New, Insert, Load into Memory, About, Help and the save
  prompt on exit.
* `watcom/test/videdit_s.uit` uses `SAMPLE.AVI` from the VfW disks (Cinepak with audio): Go To,
  selection, Copy/Delete/Undo, every dialog of the Video menu and the file dialogs, and Save As.

Result: all log lines and screenshots are identical (part A 333 lines and 9 screenshots, part B 445
and 4, the SAMPLE.AVI script 836 and 14), and the saved and extracted AVI files are
byte-identical except for the four reserved DWORDs at the end of the AVI main header, which the
MediaMan AVI writer leaves uninitialised (they hold leftover memory; `uidiff.py` masks them).

To repeat the tests, build `../common/watcom/uitest`, make `TEST8.AVI` with
`python3 watcom/test/mkavi.py TEST8.AVI`, and copy `UITEST.EXE`, a script (as `SCRIPT.UIT`),
`VIDEDIT.EXE`, `VIDEDIT.HLP`, `TEST8.AVI` and `SAMPLE.AVI` to `C:\TEST` of a Windows 3.11 + VfW 1.1
installation; run `win C:\TEST\UITEST.EXE C:\TEST\SCRIPT.UIT C:\TEST\UI.LOG C:\TEST\IMG` once for
each binary, each in a fresh copy, and compare the two `TEST` directories with `uidiff.py`.

Pitfalls found while testing: what a Cinepak delta frame looks like depends on which frames the
decompressor saw before (and, with Fast Frame Update, on timing), so the tests only capture key
frames of SAMPLE.AVI; and `uitest` translates 8-bit screen pixels through the system palette
itself, because `GetDIBits` through the screen DC depends on palette history.

Not tested: playback and Play Preview with sound (no wave output headless), the MCI preview
window, Capture Video (runs VidCap), undo of Crop/Resize/format changes (uninitialised test in the
original), drag and drop, RLE and palette-change AVIs, damaged files, and
the compressor configuration dialogs.
