# Video for Windows 1.1 tools — reconstructed source

C reconstructions of the three applications in the `WINVIDEO` directory of Video for Windows
1.1 (as installed on Windows 3.11), with Open Watcom builds and A/B UI tests that compare each
rebuild with the original under Windows 3.11 in DOSBox-X.

| Program | What it is | Original | Directory |
|---|---|---|---|
| `CAPSCRN.EXE` | CapScrn 1.1: captures the screen to an AVI file through AVICAP and the screen capture driver | 39,936 bytes, MD5 `51abd4aaf2881136e3906ff99c1c70fa` | `capscrn/` |
| `VIDCAP.EXE` | VidCap 1.1: the video capture tool, with its own capture engine on the MSVIDEO capture driver interface | 138,816 bytes, MD5 `0ad5d7ad8e4832d321af388ea670718f` | `vidcap/` |
| `VIDEDIT.EXE` | VidEdit 1.1: the AVI editor, built on the Media Manager and Workbench framework | 237,888 bytes, MD5 `840b3657148e73ca12662d40fd4b61e1` | `videdit/` |

The original binaries are not included. Each directory has its own README with the binary layout,
how the program works, notable findings (including the bugs of the original that were kept), the
build, and what was and wasn't verified.

## Layout

```
capscrn/, vidcap/, videdit/   source, .rc/.def, res/, README.md
    watcom/build.sh           Open Watcom build (output in watcom/out)
    watcom/test/              uitest scripts (and test data generators)
common/watcom/                stand-in headers for what Open Watcom lacks: AVICAP, MSACM,
                              MMREG, MSVIDEO (capture drivers, DrawDib, ICM), AVIFMT, and the
                              MediaMan, Workbench and WINCOM interfaces VidEdit uses
common/watcom/uitest/         uitest.c, a scripted Win16 UI test driver, and uidiff.py
```

## Building

Open Watcom 2.0 (`WATCOM` pointing at its `rel` directory); its tools can't handle the spaces in
this path, so the scripts use relative paths:

```sh
WATCOM=~/src/open-watcom-v2/rel capscrn/watcom/build.sh
WATCOM=~/src/open-watcom-v2/rel vidcap/watcom/build.sh
WATCOM=~/src/open-watcom-v2/rel videdit/watcom/build.sh
WATCOM=~/src/open-watcom-v2/rel common/watcom/uitest/build.sh
```

All builds are reproducible.

## How the programs were tested

`uitest.exe` runs inside Windows and follows a script: it starts the program, sends it menu
commands, fills in and clicks dialogs, and logs window trees, control contents, menus, INI
settings, screenshots, the clipboard, and the size, CRC and RIFF structure of the files the
program writes. Each script runs twice, once with the original and once with the rebuild, each in
a fresh copy of the same Windows 3.11 + VfW 1.1 installation in headless DOSBox-X (S3 at 800×600,
256 colours), and `uidiff.py` compares the two results. Capture works headless through the screen
capture driver (`SCRNCAP.DRV`). There is no wave input or output in that setup, so audio
capture and playback are untested.

```
win C:\TEST\UITEST.EXE C:\TEST\SCRIPT.UIT C:\TEST\UI.LOG C:\TEST\IMG
python3 common/watcom/uitest/uidiff.py RUN_A/TEST RUN_B/TEST
```

## Results

| Program | Rebuild | A/B result |
|---|---|---|
| CapScrn | 33,314 bytes | 313 log lines and 7 screenshots identical, including a capture |
| VidCap | 140,848 bytes | 853 of 857 log lines and 14 of 15 screenshots identical; the 4 differences are data the original leaves uninitialised (bytes after an RLE frame's end marker, also in the clipboard copy; stale bytes after the RIFF data in the preallocated capture file plus its capture time) and a hidden scroll range in MSVIDEO's compression dialog that varies between runs of the original |
| VidEdit | 260,424 bytes | 1,614 log lines and 27 screenshots identical in three scripts; saved files identical except four reserved AVI header DWORDs the MediaMan AVI writer leaves uninitialised (masked by `uidiff.py`) |

Details, including what wasn't tested, are in each program's README.
