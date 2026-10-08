# MSVIDC.DRV — reconstructed source

C reconstruction of `MSVIDC.DRV` (Microsoft Video 1 / CRAM codec, Video for Windows 1.1,
MD5 `76a8179b20041ed5fa2ce2303f3b49c8`), made from the binary with Ghidra 12.1.4 (headless),
ndisasm and manual analysis. The two hand-written assembly segments are rendered as C.

## Binary layout → source files

| NE segment | Size | Contents | File |
|---|---|---|---|
| 1 CODE PRELOAD | 0x0CA4 | LibEntry, LibMain, WEP, `DriverProc`, open/close/state/info, decompress query/begin/end, dither table builder | `drvproc.c` |
| 2 CODE **USE32** (flag 0x2000) | 0x1E84 | hand-written 386 decompressors, each behind a DPMI stub that sets the D bit on CS | `dec386.c` |
| 3 CODE | 0x4BC9 | `0000-06B7` ICM compress handlers, Configure dialog | `compress.c` |
| | | `06B8-1095` huge-pointer C decompressors | `decomp.c` |
| | | `1096-1D2B` inverse colormap (Spencer Thomas `inv_cmap`), quality→threshold, compress setup | `invcmap.c` |
| | | `1D2C-34B9` CRAM-8 and CRAM-16 frame encoders | `encode.c` |
| | | `34BA-4BC8` Microsoft C runtime (not reconstructed, see below) | — |
| 4 CODE | 0x051C | hand-written 16-bit decompressors (the "286" path) | `dec286.c` |
| 5 DATA (DGROUP) | 0x182B | tables and globals; full map in `msvidc.h` | `data.c` and the module that owns each variable |

Also: `msvidc.h` (types, DGROUP map, prototypes), `msvidc.def` (from the NE header),
`msvidc.rc` (dialog `CONFIGURE`, string table, version block), and `watcom/` (Open Watcom build
and the A/B test program, see below).

Every function has a header comment with its `seg:offset` range, role and calling convention.

## How it works

* **Instance**: `INSTINFO` (0x1C bytes, `LocalAlloc`), state = temporal quality ratio (percent,
  default 75). `ICM_GETDEFAULTQUALITY` = 7500. Flags reported: `VIDCF_QUALITY | VIDCF_TEMPORAL`.
* **Decompression**: `DecompressQuery` picks a routine from `aDecompress386[src][2x][dst]` or,
  if `GetWinFlags() & WF_CPU286`, from `aDecompress286`. Supported: CRAM8→8, CRAM8→8 at 2x,
  CRAM16→8 (ordered dither through a 0x1FA00-byte table built on first use), CRAM16→16,
  CRAM16→16 at 2x. Only whole frames; output origin x/y is honoured.
* **Compression**: quality Q → badness `10000-Q` (2500 for default) → threshold
  `32768*(badness/10000)^4`. The temporal threshold uses `badness*100/ratio`.
  For each 4x4 block the encoders try in order: skip (matches the previous frame), solid,
  2-colour (split on luma), 8-colour (per-quadrant split, always accepted). 8-bit output maps
  colours through a 32K RGB555→index table built with Thomas' incremental inverse colormap.
  A frame is flagged as a key frame when there is no previous frame or no block was skipped.

## Notable findings

* **USE32 segment in a Win16 DLL.** Segment 2 has the NE 32-bit flag. Each entry runs
  `xor eax,eax / mov ah,80h / add eax,eax / jc stub`, which sets CF only when decoded as 16-bit
  code. The stub then calls DPMI 0Bh/0Ch to set the D bit, and the IRET reloads CS as USE32.
* **The 286 path cannot run on a 286.** Segments 1 and 3 use 386 instructions (`push dword`,
  `movsx`, 32-bit registers), including in `DriverProc` and `Decompress`.
* **DS≠SS bug** in `decomp.c`: the block decoders store the skip count through a near pointer
  to the caller's stack variable, which lands in DGROUP. Skip codes therefore advance only one
  block in `Decompress8To8Huge` and `Decompress16To24Huge`.
* **Dither table overlaps the Windows instance data.** The original DGROUP has no 16-byte NULL
  header, so `abDitherIndex` starts at DS:0004. `LocalInit` in LibEntry stores `pLocalHeap` at
  DS:0006, so in the running original the cube cells (0,0,2) and (0,0,3) map to palette entries
  0x50 and 0x18 (the bytes of the heap pointer) instead of 0x0A and 0x04. This shows up as a few
  wrong pixels in dark blue areas of 16→8 dithered output.
* **`MAKE4` bug** in `decomp.c`: the 4-pixel DWORD is built in 16-bit int arithmetic, so pixels
  2 and 3 of solid and 8-colour rows come out 0x00 or 0xFF.
* **Dead code**: `Decompress16To32_386`, `Decompress16To32x2_386` (seg2) and
  `Decompress16To24Huge` (seg3:0A88) are complete but have no table slot. DS:00F9-034D holds two
  unreferenced dither tables.
* **Other quirks**: `Decompress()` without a prior begin does an implicit begin and clears the
  flag again. `DecompressBegin` writes `biSizeImage` into the caller's header, and
  `DecompressGetPalette`/`CompressSetup` write `biClrUsed`. `ICM_DECOMPRESS` drops
  `icd->dwFlags`. `BuildInverseTable` leaks the 32K table if the 128K distance buffer can't be
  allocated. Only one instance at a time may compress with 8-bit input or output (shared
  palette tables, `gpinstCompress`). The encoders ignore partial edge blocks.

## C runtime (seg3:34BA-4BC8), not reconstructed

These names come from behaviour and the usual MSC 7 library layout; they were not matched
against the actual library:

* `_FPInit` 34BA and `_FPTerm` 34EC (WIN87EM `__fpmath` glue, used around compression)
* the FP signal handler 3516 (`PostQuitMessage`)
* `__aFldiv` 3538, `__aFlmul` 35D2
* `_ftol` 3620/3BD8
* float↔text conversion internals (3624-3B9F, 3E50-3E95, 3F2E-4102, 4396-4BC8)
* string and memory helpers (3C12-3E4F, 3EDE)
* runtime error messages 4104/4145 (`R6xxx` texts at DS:174C)
* `___EXPORTEDSTUB` 4382

The driver itself only calls `_FPInit`, `_FPTerm` and `_ftol`.

## Building with Open Watcom

```sh
WATCOM=~/src/open-watcom-v2/rel ./watcom/build.sh
```

This writes `watcom/out/msvidc.drv` (34,722 bytes; the build is reproducible) and
`watcom/icmtest/out/icmtest.exe`. Watcom's tools can't handle the spaces in this path, so the
script works with relative paths. The build is a medium-model Win16 DLL with SS≠DS (`-zu`), 386
code (`-3`) and the code segments laid out as in the original. It differs from the original in
these ways:

* **Startup.** It uses Watcom's DLL startup (`libentry.obj`) instead of the original MSC-style
  LibEntry (documented in `drvproc.c`), because Watcom's runtime helpers need its startup to
  run. `watcom/libmain.c` adapts Watcom's 4-argument `LibMain` to the reconstructed one.
* **Runtime stand-ins.** `watcom/compat.c` replaces the MSC runtime's `_FPInit`/`_FPTerm`. It
  sets 64-bit x87 precision for the compression call. Watcom's default control word is 53-bit,
  and with that the default-quality threshold comes out 128 instead of the original's 127.
* **Floating point.** `invcmap.c` is compiled with `-fpi`, which gives the same WIN87EM OS
  fixups as the original.
* **Source changes for this build.** Two source changes were needed:
  * `invcmap.c` pads the 128K distance buffer by 64K on each side under `#ifdef __WATCOMC__`.
    Thomas' loops form pointers just outside the buffer. MSC keeps huge pointers in memory, but
    Watcom keeps them in ES:BX, and loading the out-of-range selector faults in protected mode.
  * `encode.c` zeroes its stack arrays with `_fmemset` instead of `memset`, because near
    pointers to the stack are wrong when SS≠DS. The original uses individual stores.
* **Version resource.** `wrc` puts the build time in `dwFileDateLS`; `build.sh` zeroes it, as
  in the original.
* **No `___EXPORTEDSTUB`.** That export (ordinal 3) belongs to the MSC runtime and is omitted.

## Status and verification

* **A/B test under Windows 3.1** (DOSBox-X, plain Windows 3.1, 386 enhanced mode):
  * **What it does.** `watcom/icmtest` loads a codec with `LoadLibrary` and drives its
    `DriverProc` directly:
    * driver and info messages;
    * compression of 4-frame synthetic sequences at 3 sizes (64×48, 101×75, 168×124) from 8, 16,
      24 and 32 bpp input to CRAM8 and CRAM16, at 5 quality settings, with and without the
      previous frame;
    * decompression of every frame to every output the codec offers (8/16 bpp, 1× and 2×).
  * **What gets recorded.** Every return code, header, compressed stream and decoded image, about
    8,000 records and 42 MB.
  * **Result.** The Watcom build and the original `MSVIDC.DRV` produce identical results
    everywhere, including every compressed stream, except 477 pixels of 16→8 dithered output.
    All of those are explained by the original's DS:0006 overlap described above. Both drivers
    give the same results on repeated runs.
  * **To rerun.** Run `win icmtest <codec.drv> <out file>` once per driver, then
    `python3 watcom/icmtest/abdiff.py A.OUT B.OUT`. The test program exits Windows when done.
* **Media Player under Windows 3.11 with Video for Windows 1.1** (S3 at 800×600, 256 colours):
  * **Setup.** Two 105-frame clips were played with `mplayer /play /close`, once with the
    original `MSVIDC.DRV` and once with the rebuild, while DOSBox-X recorded the screen:
    * the VfW sample clip transcoded to 16-bit Video 1 by ffmpeg's independent encoder, so the
      codec dithers to 8 bits;
    * an 8-bit Video 1 clip made with `watcom/mptest/mkcram8.py`.
    `watcom/mptest/runexit.c` launches Media Player and exits Windows afterwards.
  * **Comparison.** Every displayed frame was compared with frames built from ffmpeg's decode of
    the same files.
  * **8-bit clip.** Both drivers show all 105 frames exactly.
  * **16-bit clip.** The rebuild shows all 105 frames exactly as the reconstructed dither
    predicts. The original shows all 105 exactly as predicted once the DS:0006 corruption is
    included. That corruption appears in 18 frames as purple specks in dark blue areas.
* **Emulator comparisons.** Most of the code was also differentially tested in unicorn. The
  original machine code ran in 16-bit real mode with relocations patched and DS≠SS, next to the
  C built with Watcom as real 16-bit code (or as host code for `dec386.c`). Full outputs and
  side effects were compared:
  * `dec386.c`: all 7 routines, ~1,000 random CRAM streams, identical.
  * `dec286.c` and `decomp.c`: 3,396 random cases plus directed ones, identical. These tests
    confirm the two `decomp.c` defects above in the original. One Watcom quirk: it compiles
    `(LPBYTE)((BYTE _huge *)p + 2)` as an offset-only add, which differs from MSC when a flag
    word starts at offset 0xFFFE.
  * `encode.c` and `invcmap.c`: ~1,640 frames and 113 palettes, identical (return value,
    output stream, all 9 statistics counters, Status calls, the 32K inverse table, the 128K
    distance buffer).
  * `QualityToThreshold` matches for badness 0..160000. Above that the result depends on MSC's
    `_ftol` (low DWORD of a 64-bit FISTP); see the comment in `invcmap.c`.
* **Not tested:**
  * the 286 path (it can't run on a 286 anyway; see above);
  * `ICM_DECOMPRESSEX*` with sub-rectangles;
  * top-down output DIBs in the Windows A/B run;
  * the Configure and About dialogs (only their query forms);
  * status callbacks in the Windows A/B run;
  * allocation-failure paths;
  * paths that read uninitialised stack in the original (`pPrev` without `lpbiPrev`);
  * 32 bpp widths ≥ 2048 (16-bit stride overflow).
