/*
 * avifmt.h - stand-in for AVIFMT.H of the Video for Windows 1.1 DK,
 * written for the Open Watcom build: the RIFF AVI file layout.
 */

#ifndef _INC_AVIFMT
#define _INC_AVIFMT

#ifndef mmioFOURCC
#define mmioFOURCC(ch0, ch1, ch2, ch3) \
    ((DWORD)(BYTE)(ch0) | ((DWORD)(BYTE)(ch1) << 8) | \
     ((DWORD)(BYTE)(ch2) << 16) | ((DWORD)(BYTE)(ch3) << 24))
#endif
#ifndef aviTWOCC
#define aviTWOCC(ch0, ch1)      ((WORD)(BYTE)(ch0) | ((WORD)(BYTE)(ch1) << 8))
#endif

typedef WORD TWOCC;

#define formtypeAVI             mmioFOURCC('A', 'V', 'I', ' ')
#define listtypeAVIHEADER       mmioFOURCC('h', 'd', 'r', 'l')
#define ckidAVIMAINHDR          mmioFOURCC('a', 'v', 'i', 'h')
#define listtypeSTREAMHEADER    mmioFOURCC('s', 't', 'r', 'l')
#define ckidSTREAMHEADER        mmioFOURCC('s', 't', 'r', 'h')
#define ckidSTREAMFORMAT        mmioFOURCC('s', 't', 'r', 'f')
#define ckidSTREAMHANDLERDATA   mmioFOURCC('s', 't', 'r', 'd')
#define ckidSTREAMNAME          mmioFOURCC('s', 't', 'r', 'n')
#define listtypeAVIMOVIE        mmioFOURCC('m', 'o', 'v', 'i')
#define listtypeAVIRECORD       mmioFOURCC('r', 'e', 'c', ' ')
#define ckidAVINEWINDEX         mmioFOURCC('i', 'd', 'x', '1')
#define ckidAVIPADDING          mmioFOURCC('J', 'U', 'N', 'K')

#define streamtypeVIDEO         mmioFOURCC('v', 'i', 'd', 's')
#define streamtypeAUDIO         mmioFOURCC('a', 'u', 'd', 's')
#define streamtypeTEXT          mmioFOURCC('t', 'x', 't', 's')

#define cktypeDIBbits           aviTWOCC('d', 'b')
#define cktypeDIBcompressed     aviTWOCC('d', 'c')
#define cktypePALchange         aviTWOCC('p', 'c')
#define cktypeWAVEbytes         aviTWOCC('w', 'b')

#define FromHex(n)              (((n) >= 'A') ? ((n) + 10 - 'A') : ((n) - '0'))
#define StreamFromFOURCC(fcc)   ((WORD)((FromHex(LOBYTE(LOWORD(fcc))) << 4) + \
                                        (FromHex(HIBYTE(LOWORD(fcc))))))
#define TWOCCFromFOURCC(fcc)    HIWORD(fcc)
#define ToHex(n)                ((BYTE)(((n) > 9) ? ((n) - 10 + 'A') : ((n) + '0')))
#define MAKEAVICKID(tcc, stream) \
    MAKELONG((ToHex((stream) & 0x0f) << 8) | (ToHex(((stream) & 0xf0) >> 4)), tcc)

#define AVIF_HASINDEX           0x00000010L
#define AVIF_MUSTUSEINDEX       0x00000020L
#define AVIF_ISINTERLEAVED      0x00000100L
#define AVIF_WASCAPTUREFILE     0x00010000L
#define AVIF_COPYRIGHTED        0x00020000L

#define AVI_HEADERSIZE          2048

typedef struct {
    DWORD       dwMicroSecPerFrame;     /* 00 */
    DWORD       dwMaxBytesPerSec;       /* 04 */
    DWORD       dwPaddingGranularity;   /* 08 */
    DWORD       dwFlags;                /* 0C AVIF_* */
    DWORD       dwTotalFrames;          /* 10 */
    DWORD       dwInitialFrames;        /* 14 */
    DWORD       dwStreams;              /* 18 */
    DWORD       dwSuggestedBufferSize;  /* 1C */
    DWORD       dwWidth;                /* 20 */
    DWORD       dwHeight;               /* 24 */
    DWORD       dwReserved[4];          /* 28 */
} MainAVIHeader;                        /* 0x38 bytes */

#define AVISF_DISABLED          0x00000001L
#define AVISF_VIDEO_PALCHANGES  0x00010000L

typedef struct {
    FOURCC      fccType;                /* 00 */
    FOURCC      fccHandler;             /* 04 */
    DWORD       dwFlags;                /* 08 AVISF_* */
    WORD        wPriority;              /* 0C */
    WORD        wLanguage;              /* 0E */
    DWORD       dwInitialFrames;        /* 10 */
    DWORD       dwScale;                /* 14 */
    DWORD       dwRate;                 /* 18 dwRate / dwScale = samples per second */
    DWORD       dwStart;                /* 1C */
    DWORD       dwLength;               /* 20 */
    DWORD       dwSuggestedBufferSize;  /* 24 */
    DWORD       dwQuality;              /* 28 */
    DWORD       dwSampleSize;           /* 2C */
    RECT        rcFrame;                /* 30 */
} AVIStreamHeader;                      /* 0x38 bytes */

#define AVIIF_LIST              0x00000001L
#define AVIIF_KEYFRAME          0x00000010L
#define AVIIF_NOTIME            0x00000100L
#define AVIIF_COMPUSE           0x0FFF0000L

typedef struct {
    DWORD       ckid;
    DWORD       dwFlags;                /* AVIIF_* */
    DWORD       dwChunkOffset;
    DWORD       dwChunkLength;
} AVIINDEXENTRY;

typedef struct {
    BYTE        bFirstEntry;
    BYTE        bNumEntries;            /* 0 means 256 */
    WORD        wFlags;
    PALETTEENTRY peNew[1];
} AVIPALCHANGE;

#endif /* _INC_AVIFMT */
