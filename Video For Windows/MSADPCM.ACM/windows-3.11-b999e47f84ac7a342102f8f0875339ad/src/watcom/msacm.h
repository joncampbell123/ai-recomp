/*
 * msacm.h - stand-in for the Audio Compression Manager header of the
 * Video for Windows 1.1 DK, written for the Open Watcom build.
 *
 * Only what MSADPCM and its tests use.  Win16 layouts; the field offsets
 * agree with the accesses in the original MSADPCM.ACM binary.
 */

#ifndef _INC_ACM
#define _INC_ACM

#ifndef mmioFOURCC
#define mmioFOURCC(ch0, ch1, ch2, ch3) \
    ((DWORD)(BYTE)(ch0) | ((DWORD)(BYTE)(ch1) << 8) | \
     ((DWORD)(BYTE)(ch2) << 16) | ((DWORD)(BYTE)(ch3) << 24))
#endif

/* error returns */
#define ACMERR_BASE                         512
#define ACMERR_NOTPOSSIBLE                  (ACMERR_BASE + 0)
#define ACMERR_BUSY                         (ACMERR_BASE + 1)
#define ACMERR_UNPREPARED                   (ACMERR_BASE + 2)
#define ACMERR_CANCELED                     (ACMERR_BASE + 3)

/* driver messages in the ACM range */
#define ACMDM_USER                          (DRV_USER + 0x0000)
#define ACMDM_RESERVED_LOW                  (DRV_USER + 0x2000)
#define ACMDM_RESERVED_HIGH                 (DRV_USER + 0x2FFF)
#define ACMDM_BASE                          ACMDM_RESERVED_LOW
#define ACMDM_DRIVER_ABOUT                  (ACMDM_BASE + 11)

/* ACMDRIVERDETAILS */
#define ACMDRIVERDETAILS_SHORTNAME_CHARS    32
#define ACMDRIVERDETAILS_LONGNAME_CHARS     128
#define ACMDRIVERDETAILS_COPYRIGHT_CHARS    80
#define ACMDRIVERDETAILS_LICENSING_CHARS    128
#define ACMDRIVERDETAILS_FEATURES_CHARS     512

#define ACMDRIVERDETAILS_FCCTYPE_AUDIOCODEC mmioFOURCC('a', 'u', 'd', 'c')
#define ACMDRIVERDETAILS_FCCCOMP_UNDEFINED  mmioFOURCC('\0', '\0', '\0', '\0')

#define ACMDRIVERDETAILS_SUPPORTF_CODEC     0x00000001L
#define ACMDRIVERDETAILS_SUPPORTF_CONVERTER 0x00000002L
#define ACMDRIVERDETAILS_SUPPORTF_FILTER    0x00000004L
#define ACMDRIVERDETAILS_SUPPORTF_HARDWARE  0x00000008L
#define ACMDRIVERDETAILS_SUPPORTF_ASYNC     0x00000010L

/* ACMFORMATTAGDETAILS */
#define ACMFORMATTAGDETAILS_FORMATTAG_CHARS 48
#define ACM_FORMATTAGDETAILSF_INDEX         0x00000000L
#define ACM_FORMATTAGDETAILSF_FORMATTAG     0x00000001L
#define ACM_FORMATTAGDETAILSF_LARGESTSIZE   0x00000002L
#define ACM_FORMATTAGDETAILSF_QUERYMASK     0x0000000FL

/* ACMFORMATDETAILS */
#define ACMFORMATDETAILS_FORMAT_CHARS       128
#define ACM_FORMATDETAILSF_INDEX            0x00000000L
#define ACM_FORMATDETAILSF_FORMAT           0x00000001L
#define ACM_FORMATDETAILSF_QUERYMASK        0x0000000FL

/* acmFormatEnum */
#define ACM_FORMATENUMF_WFORMATTAG          0x00010000L
#define ACM_FORMATENUMF_NCHANNELS           0x00020000L
#define ACM_FORMATENUMF_NSAMPLESPERSEC      0x00040000L
#define ACM_FORMATENUMF_WBITSPERSAMPLE      0x00080000L

/* acmFormatSuggest */
#define ACM_FORMATSUGGESTF_WFORMATTAG       0x00010000L
#define ACM_FORMATSUGGESTF_NCHANNELS        0x00020000L
#define ACM_FORMATSUGGESTF_NSAMPLESPERSEC   0x00040000L
#define ACM_FORMATSUGGESTF_WBITSPERSAMPLE   0x00080000L
#define ACM_FORMATSUGGESTF_TYPEMASK         0x00FF0000L

/* acmStreamOpen, acmStreamSize, acmStreamConvert */
#define ACM_STREAMOPENF_QUERY               0x00000001L
#define ACM_STREAMOPENF_ASYNC               0x00000002L
#define ACM_STREAMOPENF_NONREALTIME         0x00000004L

#define ACM_STREAMSIZEF_SOURCE              0x00000000L
#define ACM_STREAMSIZEF_DESTINATION         0x00000001L
#define ACM_STREAMSIZEF_QUERYMASK           0x0000000FL

#define ACM_STREAMCONVERTF_BLOCKALIGN       0x00000004L
#define ACM_STREAMCONVERTF_START            0x00000010L
#define ACM_STREAMCONVERTF_END              0x00000020L

#ifndef RC_INVOKED

DECLARE_HANDLE(HACMSTREAM);

#pragma pack(push, 1)

typedef struct tWAVEFILTER {
    DWORD   cbStruct;
    DWORD   dwFilterTag;
    DWORD   fdwFilter;
    DWORD   dwReserved[5];
} WAVEFILTER, *PWAVEFILTER, NEAR *NPWAVEFILTER, FAR *LPWAVEFILTER;

typedef struct tACMDRIVERDETAILS {
    DWORD   cbStruct;               /* +000 */
    FOURCC  fccType;                /* +004 */
    FOURCC  fccComp;                /* +008 */
    WORD    wMid;                   /* +00C */
    WORD    wPid;                   /* +00E */
    DWORD   vdwACM;                 /* +010 */
    DWORD   vdwDriver;              /* +014 */
    DWORD   fdwSupport;             /* +018 */
    DWORD   cFormatTags;            /* +01C */
    DWORD   cFilterTags;            /* +020 */
    HICON   hicon;                  /* +024 */
    char    szShortName[ACMDRIVERDETAILS_SHORTNAME_CHARS];  /* +026 */
    char    szLongName[ACMDRIVERDETAILS_LONGNAME_CHARS];    /* +046 */
    char    szCopyright[ACMDRIVERDETAILS_COPYRIGHT_CHARS];  /* +0C6 */
    char    szLicensing[ACMDRIVERDETAILS_LICENSING_CHARS];  /* +116 */
    char    szFeatures[ACMDRIVERDETAILS_FEATURES_CHARS];    /* +196 */
} ACMDRIVERDETAILS, *PACMDRIVERDETAILS, FAR *LPACMDRIVERDETAILS;   /* 0x396 bytes */

typedef struct tACMFORMATTAGDETAILS {
    DWORD   cbStruct;               /* +00 */
    DWORD   dwFormatTagIndex;       /* +04 */
    DWORD   dwFormatTag;            /* +08 */
    DWORD   cbFormatSize;           /* +0C */
    DWORD   fdwSupport;             /* +10 */
    DWORD   cStandardFormats;       /* +14 */
    char    szFormatTag[ACMFORMATTAGDETAILS_FORMATTAG_CHARS];   /* +18 */
} ACMFORMATTAGDETAILS, *PACMFORMATTAGDETAILS, FAR *LPACMFORMATTAGDETAILS; /* 0x48 bytes */

typedef struct tACMFORMATDETAILS {
    DWORD           cbStruct;       /* +00 */
    DWORD           dwFormatIndex;  /* +04 */
    DWORD           dwFormatTag;    /* +08 */
    DWORD           fdwSupport;     /* +0C */
    LPWAVEFORMATEX  pwfx;           /* +10 */
    DWORD           cbwfx;          /* +14 */
    char            szFormat[ACMFORMATDETAILS_FORMAT_CHARS];    /* +18 */
} ACMFORMATDETAILS, *PACMFORMATDETAILS, FAR *LPACMFORMATDETAILS;  /* 0x98 bytes */

#pragma pack(pop)

#endif /* RC_INVOKED */

#endif /* _INC_ACM */
