# MSADPCM.ACM — reconstructed source

C reconstruction of `MSADPCM.ACM` (Microsoft ADPCM ACM codec 2.01, build 175, from Video for
Windows 1.1 on Windows 3.11, MD5 `b999e47f84ac7a342102f8f0875339ad`). It was made from the binary
with Ghidra 12.1.4 (headless), ndisasm and manual analysis. The hand-written assembly segment is
rendered as C. The driver follows the layout of Microsoft's ACM sample codecs of the same period
(`CODECINST`, the `acmd*` message handlers), and names are taken from that template where the code
matches it.

## Binary layout → source files

| NE segment | Size | Contents | File |
|---|---|---|---|
| 1 CODE PRELOAD | 0x0092 | stub, SDK LibEntry (the NE entry point), LibMain, WEP | `init.c` |
| 2 CODE PRELOAD | 0x166E | `0000-000F` 16 zero bytes | — |
| | | `0010-0F45` format helpers, `acmd*` message handlers, `DriverProc` | `codec.c` |
| | | `0F46-166D` Microsoft C runtime (not reconstructed, see below) | — |
| 3 CODE | 0x099F | `adpcmEncode4Bit_FirstDelta`, full-search encoder `adpcmEncode4Bit` | `msadpcm.c` |
| 4 CODE, no relocations | 0x12AF | hand-written 386 assembly: decoder, real-time encoder, an unreferenced helper | `adpcm386.c` |
| 5 DATA (DGROUP) | 0x01B4 | format and coefficient tables, `ghinst`, C runtime data; local heap 0x1000 | see `msadpcm.h` |

Also: `msadpcm.h` (types, DGROUP map, prototypes), `msadpcm.def` (from the NE header),
`msadpcm.rc` (string table, version block), and `watcom/` (Open Watcom build and two test
programs, see below).

Every function has a header comment with its `seg:offset` range, role and calling convention.

## How it works

* **Messages.** `DriverProc` handles:
  * `DRV_LOAD`, `DRV_FREE`, `DRV_OPEN`, `DRV_CLOSE`, `DRV_INSTALL`, `DRV_REMOVE`, and
    `DRV_CONFIGURE`/`DRV_QUERYCONFIGURE` (always 0);
  * `ACMDM_DRIVER_DETAILS`, `ACMDM_DRIVER_ABOUT` (always not supported), `ACMDM_FORMATTAG_DETAILS`,
    `ACMDM_FORMAT_DETAILS`, `ACMDM_FORMAT_SUGGEST`, and `ACMDM_STREAM_OPEN`/`CLOSE`/`SIZE`/`CONVERT`.

  Other messages from `ACMDM_USER` up return `MMSYSERR_NOTSUPPORTED`; lower ones go to
  `DefDriverProc`. The 0x12-byte `CODECINST` is written by `DRV_OPEN` and never read. `DRV_OPEN`
  without an open description returns 1, which `DriverProc` maps back to "no instance".
* **Formats.**
  * PCM: 16 standard formats (8000, 11025, 22050, 44100 Hz × mono/stereo × 8/16 bits).
  * ADPCM: 8 standard formats (rates × channels). The block is 256 bytes per channel, multiplied
    by `rate / 11000` above 11025 Hz. Samples per block are `(block - 7·channels)·2/channels + 2`.
  * Only formats with the 7 standard coefficient sets are accepted (`adpcmIsMagicFormat`).
* **Conversion.** `acmdStreamOpen` stores the converter in `ACMDRVSTREAMINSTANCE.dwDriver`:
  * **Decoder** (seg4): standard MS ADPCM. Its output is bit-exact with a plain reference decoder
    that uses `>> 8` for the prediction.
  * **Full-search encoder** (seg3), used with `ACM_STREAMOPENF_NONREALTIME`:
    * each block of PCM is copied into a `GlobalAlloc`'d buffer and encoded with all 7
      coefficient sets;
    * the set with the smallest `Σ (error² >> 7)` is picked per channel, and the block is encoded
      again with it;
    * the first step size is the mean absolute error of three predictions over the first five
      samples, divided by 4, at least 16.
  * **Real-time encoder** (seg4), used otherwise: coefficient set 1 (512, −256) and a step size of
    128 for every block, one pass.

## Notable findings

* **The C runtime is linked but never initialised.** The NE entry point is an SDK-style LibEntry in
  seg1. The runtime's own LibEntry (seg2:0F8A, which would initialise the runtime) is never reached, and
  nothing in the driver calls into the runtime. `___EXPORTEDSTUB` is exported anyway (ordinal 3).
* **"CDD" stub.** The same 66 bytes precede both LibEntry routines (seg1:0000, seg2:0F48). The
  stub checks `__WINFLAGS` bit 15 (WLO), calls `InitTask` minus 5 through an additive fixup, and
  has a patchable path through `GetModuleUsage`. It ends with `'CDD'` and three offsets into
  itself. Nothing references it (see `init.c`).
* **The assembly needs a 386 and enhanced mode.** It steps through the buffers with 32-bit offsets
  from the first selector, which relies on that selector's limit covering the whole huge block.
  `acmdStreamOpen` selects these routines without checking the CPU.
* **Step size wrap in the assembly.** The decoder and the real-time encoder keep bits 8..23 of
  `P4 × delta` and test `delta − 16` for sign in 16-bit arithmetic. A step size from −32768 to
  −32753 therefore passes the "at least 16" clamp and stays negative. The C encoder in seg3 clamps
  properly, so the two encoders differ there.
* **Decoder off-by-one.** A block's predictor index is rejected only if it is *greater than*
  `wNumCoef`. Index 7 reads one uninitialised stack word from each coefficient array.
* **Decoder details:**
  * The coefficients come from the driver's own table, not from the format. That is equivalent,
    because only the standard set is accepted.
  * The output length is computed before decoding. For a stereo partial block of odd length, the
    last frame is counted but never written.
  * After a block the decoder does not skip ahead to the next `nBlockAlign` boundary.
  * The block count is a 16-bit `DIV`, which faults at 65536 or more blocks per call.
  * A `wSamplesPerBlock` below 4 makes the per-block loop run 65536 times.
* **`acmdStreamConvert` rounds the encoder's input to an even frame count** (when there are more
  than 2). The real-time mono loop takes two samples per pass and stops only at zero, so an odd
  count would run on through memory. No converter or handler checks `cbDstLength`.
* **Full-search encoder details:**
  * The first-delta estimate always reads 5 frames of the work buffer. A block of 1 to 4 frames
    picks up leftover data from earlier use of that memory; in the tests a 1-frame block's header
    carries a sample (13630) that is not in the input.
  * If a block has an odd number of nibbles after its header, the last nibble is dropped.
  * The work buffer is freed with `GlobalFree(SELECTOROF(p))` and no `GlobalUnlock`. This works
    on Windows 3.1: a ToolHelp count shows no leaked blocks.
* **`acmdDriverDetails` copies its whole stack structure out**, so the bytes after the end of each
  string are leftovers from the stack.
* **Other quirks:**
  * `acmdFormatDetails` also validates the formats it builds itself, because the INDEX case falls
    through into the FORMAT check.
  * `acmdStreamSize` computes bytes per block as a 16-bit product.
  * Stream opens don't check that `nBlockAlign` and `wSamplesPerBlock` agree, so non-standard
    ADPCM formats are accepted and handled inconsistently by the encoders and the decoder.
  * On the ADPCM → PCM path, `acmdFormatSuggest` writes the destination's tag, rate and channels
    before checking the requested bit depth, so that rejection leaves it half filled.
* **Dead code:** seg4:1260 is an unreferenced, broken first-delta routine for coefficient set 1.
  Its second load overwrites the base register that the third load uses (shown as assembly in
  `adpcm386.c`).

## C runtime (seg2:0F46-166D), not reconstructed

This is the Microsoft C Windows DLL runtime:

* the stub copy at 0F48 and the runtime's LibEntry at 0F8A, which stores the startup registers,
  calls `LocalInit`, gets the Windows and DOS versions (`DOS3Call` or INT 21h), calls two
  initialisation routines (11F6, 12FE) and then LibMain through 11CA;
* environment setup (`GetDOSEnvironment`) and heap helpers (`LocalReAlloc`, `GlobalReAlloc`,
  `LockSegment`, ...);
* run-time error reporting (`R6000`-`R6018` texts at DS:00CA, `FatalAppExit`/`FatalExit`);
* `___EXPORTEDSTUB` at 165A.

The names describe behaviour; they were not matched against the actual library.

## Building with Open Watcom

```sh
WATCOM=~/src/open-watcom-v2/rel ./watcom/build.sh
```

This writes `watcom/out/msadpcm.acm` (13,008 bytes; the build is reproducible) and the two test
programs. Watcom's tools can't handle the spaces in this path, so the script works with relative
paths. The build is a medium-model Win16 DLL with SS≠DS (`-zu`), 386 code, the code segments laid
out as in the original, `HEAPSIZE 4096`, and the NE expected Windows version (3.10) and PROTMODE
flag of the original. It differs from the original in these ways:

* **Startup.** It uses Watcom's DLL startup (`libentry.obj`), which calls a FAR PASCAL `LibMain`
  with the same arguments as the SDK one, so `init.c` needs no adapter. There is no stub, no MSC
  runtime, and no `___EXPORTEDSTUB`.
* **Assembly as C.** `adpcm386.c` uses huge pointers instead of 32-bit offsets. It is slower: the
  A/B test run takes about twice as long with the rebuild. The two code-segment copies of the step
  table are kept, via `_based(_segname("_CODE"))`.
* **Headers.** Open Watcom ships no ACM headers. `watcom/mmreg.h`, `msacm.h` and `msacmdrv.h` are
  stand-ins with only what the driver and the tests use, with field offsets checked against the
  original's accesses.
* **Version resource.** `wrc` puts the build time in `dwFileDateLS`; `build.sh` zeroes it, as in the
  original.

## Status and verification

* **Direct A/B test under Windows 3.1** (DOSBox-X, plain Windows 3.1, 386 enhanced mode):
  * **What it does.** `watcom/acmtest` loads a codec with `LoadLibrary` and drives its `DriverProc`
    directly:
    * the driver messages, including open with and without an open description, and unhandled
      messages in and below the ACM range;
    * driver details for 10 structure sizes, and format tag details for every query type and
      several tags, indices and sizes;
    * format details by index (5 tags × 12 indices) and by format (48 formats, valid and invalid:
      odd rates up to 2 MHz, bad bit depths and channel counts, wrong coefficients, `cbSize`,
      `wNumCoef`, other tags);
    * format suggestions for all 48 formats × 6 flag combinations × 4 destination presets;
    * stream opens for every pair of formats, and stream sizes for 25 lengths up to `0xFFFFFFFF`;
    * encodes for every standard configuration with both encoders, three of 8 test signals each,
      with and without `BLOCKALIGN`; all signals in two configurations; 0 to 9-frame inputs;
      non-standard block sizes and samples per block; and buffers over 64K;
    * decodes of every encoded result to 8 and 16 bits: whole, cut short, and `BLOCKALIGN`;
    * fuzzed ADPCM: random data with in-range and arbitrary step sizes, a bad predictor, predictor
      7, and step sizes that land in the wrap window;
    * a global heap leak check around 10 full-search encodes.
  * **What gets recorded.** Return codes, structures, and every output buffer plus a 256-byte guard
    area: 27,737 records, 18 MB.
  * **Result.** The Watcom build and the original produce identical results everywhere except:
    * bytes past the end of the strings in `ACMDRIVERDETAILS`, which are stack leftovers (they
      also differ between two runs of the same driver; `abdiff.py` masks them);
    * the decoded data of the two fuzzed streams that use predictor 7. The differences are
      confined to the block with that predictor, as expected from the uninitialised read; the
      return values and lengths match.

    Repeated runs of either driver give the same results.
  * **Wrap window.** None of the ordinary signals reaches a step size in the wrap window. The test
    therefore includes a 13-sample input, found by a search over the encoder's arithmetic, that
    takes the real-time encoder to −32757. It also includes fuzzed headers that take the decoder
    there.
  * **Mutation checks.** These show the test notices the quirks:
    * replacing the 16-bit clamp with a plain `< 16` changes 78 records;
    * rejecting predictor 7 (`>=` instead of `>`) changes 12 length records.
  * **To rerun.** Run `win acmtest <codec.acm> <out file>` once per driver, then
    `python3 watcom/acmtest/abdiff.py A.OUT B.OUT`. The test program exits Windows when done.
* **A/B test through MSACM.DLL under Windows 3.11 with Video for Windows 1.1:**
  * **What it does.** `watcom/msacmtest` uses the ACM API with the codec installed as
    `MSACM.msadpcm`:
    * driver enumeration and details;
    * format tags, and format enumeration through the driver and through the ACM (64 formats,
      with the ACM's names for them);
    * 32 format suggestions;
    * streamed conversions in two-block chunks with prepared headers and the `START`, `END` and
      `BLOCKALIGN` flags: 32 encodes (16 configurations × 2 encoders) and their 64 decodes.
  * **Result.** 1,698 records, identical for the two drivers.
  * **To rerun.** Install each driver in turn as `SYSTEM\MSADPCM.ACM`, run
    `win msacmtst <out file>` for each, and compare with `abdiff.py`.
* **Independent decoder check.** ffmpeg's `adpcm_ms` decodes the real-time encoder's streams
  bit-exactly. The full-search streams differ by ffmpeg's rounding: ffmpeg divides the prediction
  by 64 with quartered coefficients, truncating toward zero. They match a reference decoder that
  uses `>> 8` exactly.
* **Not tested:**
  * playback through the wave mapper; the headless Windows installs at hand had no working sound
    driver;
  * standard mode and 286 machines (the original's assembly can't run there);
  * 65536 or more blocks in one decode call;
  * `wSamplesPerBlock` below 4;
  * allocation failures.
