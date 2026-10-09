/*
 * mmreg.h - stand-in for the multimedia registration header of the
 * Video for Windows 1.1 DK, written for the Open Watcom build (Open
 * Watcom ships no ACM headers).
 *
 * Only what the VfW 1.1 tools use.  Win16 layouts.
 */

#ifndef _INC_MMREG
#define _INC_MMREG

#ifndef MM_MSFT_ACM_MSADPCM
#define MM_MSFT_ACM_MSADPCM     33
#endif

#ifndef WAVE_FORMAT_UNKNOWN
#define WAVE_FORMAT_UNKNOWN     0x0000
#endif
#ifndef WAVE_FORMAT_ADPCM
#define WAVE_FORMAT_ADPCM       0x0002
#endif

#ifndef RC_INVOKED

#pragma pack(push, 1)

#ifndef _WAVEFORMATEX_
#define _WAVEFORMATEX_
typedef struct tWAVEFORMATEX {
    WORD    wFormatTag;             /* +00 */
    WORD    nChannels;              /* +02 */
    DWORD   nSamplesPerSec;         /* +04 */
    DWORD   nAvgBytesPerSec;        /* +08 */
    WORD    nBlockAlign;            /* +0C */
    WORD    wBitsPerSample;         /* +0E */
    WORD    cbSize;                 /* +10 bytes of format data after this */
} WAVEFORMATEX, *PWAVEFORMATEX, NEAR *NPWAVEFORMATEX, FAR *LPWAVEFORMATEX;
#endif

typedef struct adpcmcoef_tag {
    short   iCoef1;
    short   iCoef2;
} ADPCMCOEFSET, *PADPCMCOEFSET, NEAR *NPADPCMCOEFSET, FAR *LPADPCMCOEFSET;

typedef struct adpcmwaveformat_tag {
    WAVEFORMATEX    wfx;            /* +00 */
    WORD            wSamplesPerBlock; /* +12 */
    WORD            wNumCoef;       /* +14 */
    ADPCMCOEFSET    aCoef[1];       /* +16, wNumCoef entries */
} ADPCMWAVEFORMAT, *PADPCMWAVEFORMAT, NEAR *NPADPCMWAVEFORMAT, FAR *LPADPCMWAVEFORMAT;

#pragma pack(pop)

#endif /* RC_INVOKED */

#endif /* _INC_MMREG */
