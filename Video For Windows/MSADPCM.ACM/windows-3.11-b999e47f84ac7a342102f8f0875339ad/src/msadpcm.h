/*
 * msadpcm.h - shared declarations for the reconstructed MSADPCM.ACM
 * (Microsoft ADPCM ACM codec 2.01, Video for Windows 1.1 / Windows 3.11,
 * Win16 NE DLL, MD5 b999e47f84ac7a342102f8f0875339ad).
 *
 * Reconstructed from the binary.  Offsets in comments are:
 *   segN:xxxx  code segment N, offset xxxx  (seg1..seg4)
 *   DS:xxxx    automatic data segment (seg5) offset
 *
 * The C parts were built with a Microsoft C 7 / Visual C++ 1.x class
 * compiler (LINK 5.60; 386 code generation: ENTER/LEAVE, MOVSX, o32 PUSH
 * and POP around 32-bit MUL/DIV), medium model with SS != DS.  The driver
 * follows the layout of Microsoft's ACM sample codecs of the same period
 * (CODECINST, acmd* message handlers, FNLOCAL/FNGLOBAL/FNEXPORT), and the
 * names used here come from that template where the code matches it.
 */

#ifndef _MSADPCM_H_
#define _MSADPCM_H_

#include <windows.h>
#include <mmsystem.h>
#include <mmreg.h>              /* WAVEFORMATEX, ADPCMWAVEFORMAT */
#include <msacm.h>
#include <msacmdrv.h>           /* ACMDM_*, ACMDRV* structures */

#define FNLOCAL     NEAR PASCAL
#define FNGLOBAL    FAR PASCAL
#define FNEXPORT    FAR PASCAL _loadds

#ifndef FIELD_OFFSET
#define FIELD_OFFSET(type, field)   ((LONG)(UINT)&(((type NEAR *)0)->field))
#endif

#define SIZEOF_ARRAY(ar)            (sizeof(ar) / sizeof((ar)[0]))

/* ACMDRIVERDETAILS.vdwACM and .vdwDriver: both 0x02010000 */
#define VERSION_MSACM               MAKE_ACM_VERSION(2, 1, 0)
#define VERSION_CODEC               MAKE_ACM_VERSION(2, 1, 0)

/* string table (block 1) */
#define IDS_ACM_DRIVER_SHORTNAME    1       /* "MS-ADPCM" */
#define IDS_ACM_DRIVER_LONGNAME     2       /* "Microsoft ADPCM CODEC" */
#define IDS_ACM_DRIVER_COPYRIGHT    3
#define IDS_ACM_DRIVER_LICENSING    4       /* not in the string table */
#define IDS_ACM_DRIVER_FEATURES     5
#define IDS_ACM_DRIVER_TAG_NAME     10      /* "Microsoft ADPCM" */

/*
 * DRV_OPEN without an ACMDRVOPENDESC (for instance from the Drivers control
 * panel) returns 1, and DriverProc maps a dwId of 1 back to "no instance".
 */
#define BOGUS_DRIVER_ID             1L

/* MS ADPCM parameters */
#define MSADPCM_MAX_CHANNELS        2
#define MSADPCM_MAX_COEFFICIENTS    7       /* the standard coefficient sets */
#define MSADPCM_BITS_PER_SAMPLE     4
#define MSADPCM_HEADER_LENGTH       7       /* block header bytes per channel */
#define MSADPCM_WFX_EXTRA_BYTES     32      /* cbSize: 2 + 2 + 7 * sizeof(ADPCMCOEFSET) */
#define MSADPCM_CSCALE              8       /* coefficients are 8.8 fixed point */
#define MSADPCM_PSCALE              8       /* gaiP4[] is 8.8 fixed point */
#define MSADPCM_DELTA4_MIN          16
#define MSADPCM_OUTPUT4MASK         0x0F
#define MSADPCM_OUTPUT4MAX          7
#define MSADPCM_OUTPUT4MIN          (-8)
#define ENCODE_DELTA_LOOKAHEAD      5       /* samples used to pick the first delta */

/* format enumeration */
#define CODEC_MAX_FORMAT_TAGS               2
#define CODEC_MAX_FILTER_TAGS               0
#define CODEC_MAX_SAMPLE_RATES              4
#define CODEC_MAX_CHANNELS                  MSADPCM_MAX_CHANNELS
#define CODEC_MAX_BITSPERSAMPLE_PCM         2
#define CODEC_MAX_STANDARD_FORMATS_PCM      (CODEC_MAX_SAMPLE_RATES * CODEC_MAX_CHANNELS * \
                                             CODEC_MAX_BITSPERSAMPLE_PCM)   /* 16 */
#define CODEC_MAX_STANDARD_FORMATS_ADPCM    (CODEC_MAX_SAMPLE_RATES * CODEC_MAX_CHANNELS) /* 8 */

#define PCM_BLOCKALIGNMENT(pwfx)        (UINT)(((pwfx)->wBitsPerSample >> 3) << ((pwfx)->nChannels >> 1))
#define PCM_AVGBYTESPERSEC(pwfx)        (DWORD)((pwfx)->nSamplesPerSec * (pwfx)->nBlockAlign)
#define PCM_BYTESTOSAMPLES(pwfx, cb)    (DWORD)((cb) / PCM_BLOCKALIGNMENT(pwfx))
#define PCM_SAMPLESTOBYTES(pwfx, dw)    (DWORD)((dw) * PCM_BLOCKALIGNMENT(pwfx))

#ifndef RC_INVOKED              /* the rest is C only */

typedef BYTE _huge *HPBYTE;

/*
 * Per-open instance, LocalAlloc(LPTR, sizeof(CODECINST)) == 0x12 bytes.
 * Only written by acmdDriverOpen; no handler reads it back.
 */
typedef struct tCODECINST {
    FOURCC      fccType;            /* +00 'audc' */
    DRIVERPROC  DriverProc;         /* +04 always NULL */
    HDRVR       hdrvr;              /* +08 */
    DWORD       vdwACM;             /* +0A ACMDRVOPENDESC.dwVersion */
    DWORD       dwFlags;            /* +0E ACMDRVOPENDESC.dwFlags */
} CODECINST, NEAR *PCODECINST;

/*
 * Converter selected by acmdStreamOpen and stored in
 * ACMDRVSTREAMINSTANCE.dwDriver.  All three take the stream's source and
 * destination formats and buffers and the number of source bytes, and
 * return the number of destination bytes.  FAR PASCAL, retf 14h.
 */
typedef DWORD (FNGLOBAL *CONVERTPROC)(LPWAVEFORMATEX pwfxSrc, HPBYTE pbSrc,
                                      LPWAVEFORMATEX pwfxDst, HPBYTE pbDst,
                                      DWORD cbSrcLength);

/* ------------------------------------------------------------------ */
/* DGROUP (seg5, 0x01B4 bytes, local heap 0x1000), in address order    */
/* ------------------------------------------------------------------ */
/*
 * DS:0000-000F  reserved header (0,0,5,0,...; Windows' instance data)
 * DS:0010       gauFormatTagIndexToTag[2]        { 1, 2 }               codec.c
 * DS:0014       gauFormatIndexToSampleRate[4]    { 8000 .. 44100 }      codec.c
 * DS:001C       gauFormatIndexToBitsPerSample[2] { 8, 16 }              codec.c
 * DS:0020       gaiCoef1[7]                                             msadpcm.c
 * DS:002E       gaiCoef2[7]                                             msadpcm.c
 * DS:003C       gaiP4[16]                                               msadpcm.c
 * DS:005C-01AF  C runtime data (never initialised, see README)
 * DS:01B0       ghinst                                                  init.c
 * DS:01B2-01B3  unreferenced
 */
extern HINSTANCE    ghinst;                                 /* DS:01B0 */
extern const int    gaiCoef1[MSADPCM_MAX_COEFFICIENTS];     /* DS:0020 */
extern const int    gaiCoef2[MSADPCM_MAX_COEFFICIENTS];     /* DS:002E */
extern const int    gaiP4[16];                              /* DS:003C */

/* ------------------------------------------------------------------ */
/* init.c (seg1)                                                       */
/* ------------------------------------------------------------------ */

int FAR PASCAL LibMain(HINSTANCE hinst, WORD wDataSeg, WORD cbHeapSize, LPSTR pszCmdLine);
int FNEXPORT WEP(int nParam);

/* ------------------------------------------------------------------ */
/* codec.c (seg2:0000-0F45)                                            */
/* ------------------------------------------------------------------ */

LRESULT FNEXPORT DriverProc(DWORD dwId, HDRVR hdrvr, UINT uMsg,
                            LPARAM lParam1, LPARAM lParam2);

/* ------------------------------------------------------------------ */
/* msadpcm.c (seg3), used for ACM_STREAMOPENF_NONREALTIME              */
/* ------------------------------------------------------------------ */

DWORD FNGLOBAL adpcmEncode4Bit(LPWAVEFORMATEX pwfPCM, HPBYTE pbSrc,
                               LPWAVEFORMATEX pwfADPCM, HPBYTE pbDst,
                               DWORD cbSrcLength);              /* seg3:0164 */

/* ------------------------------------------------------------------ */
/* adpcm386.c (seg4, hand-written 386 assembly)                        */
/* ------------------------------------------------------------------ */

DWORD FNGLOBAL adpcmDecode4Bit(LPWAVEFORMATEX pwfADPCM, HPBYTE pbSrc,
                               LPWAVEFORMATEX pwfPCM, HPBYTE pbDst,
                               DWORD cbSrcLength);              /* seg4:0028 */
DWORD FNGLOBAL adpcmEncode4Bit_RealTime(LPWAVEFORMATEX pwfPCM, HPBYTE pbSrc,
                                        LPWAVEFORMATEX pwfADPCM, HPBYTE pbDst,
                                        DWORD cbSrcLength);     /* seg4:0A4A */

#endif /* RC_INVOKED */

#endif /* _MSADPCM_H_ */
