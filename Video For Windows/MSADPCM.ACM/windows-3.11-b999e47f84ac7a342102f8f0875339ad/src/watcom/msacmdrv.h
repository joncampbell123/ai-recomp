/*
 * msacmdrv.h - stand-in for the ACM driver interface header of the
 * Video for Windows 1.1 DK, written for the Open Watcom build.
 *
 * Only what MSADPCM and its tests use.  Win16 layouts; the field offsets
 * agree with the accesses in the original MSADPCM.ACM binary.
 */

#ifndef _INC_ACMDRV
#define _INC_ACMDRV

#define MAKE_ACM_VERSION(mjr, mnr, bld) (((long)(mjr) << 24) | \
                                         ((long)(mnr) << 16) | \
                                         ((long)(bld)))

/* driver messages */
#define ACMDM_DRIVER_NOTIFY             (ACMDM_BASE + 1)
#define ACMDM_DRIVER_DETAILS            (ACMDM_BASE + 10)
#define ACMDM_HARDWARE_WAVE_CAPS_INPUT  (ACMDM_BASE + 20)
#define ACMDM_HARDWARE_WAVE_CAPS_OUTPUT (ACMDM_BASE + 21)
#define ACMDM_FORMATTAG_DETAILS         (ACMDM_BASE + 25)
#define ACMDM_FORMAT_DETAILS            (ACMDM_BASE + 26)
#define ACMDM_FORMAT_SUGGEST            (ACMDM_BASE + 27)
#define ACMDM_FILTERTAG_DETAILS         (ACMDM_BASE + 50)
#define ACMDM_FILTER_DETAILS            (ACMDM_BASE + 51)
#define ACMDM_STREAM_OPEN               (ACMDM_BASE + 76)
#define ACMDM_STREAM_CLOSE              (ACMDM_BASE + 77)
#define ACMDM_STREAM_SIZE               (ACMDM_BASE + 78)
#define ACMDM_STREAM_CONVERT            (ACMDM_BASE + 79)
#define ACMDM_STREAM_RESET              (ACMDM_BASE + 80)
#define ACMDM_STREAM_PREPARE            (ACMDM_BASE + 81)
#define ACMDM_STREAM_UNPREPARE          (ACMDM_BASE + 82)

#ifndef RC_INVOKED

#pragma pack(push, 1)

typedef struct tACMDRVOPENDESC {
    DWORD   cbStruct;               /* +00 */
    FOURCC  fccType;                /* +04 'audc' */
    FOURCC  fccComp;                /* +08 */
    DWORD   dwVersion;              /* +0C version of the ACM opening the driver */
    DWORD   dwFlags;                /* +10 */
    DWORD   dwError;                /* +14 result of DRV_OPEN */
    LPCSTR  pszSectionName;         /* +18 */
    LPCSTR  pszAliasName;           /* +1C */
} ACMDRVOPENDESC, *PACMDRVOPENDESC, FAR *LPACMDRVOPENDESC;

typedef struct tACMDRVSTREAMINSTANCE {
    DWORD           cbStruct;       /* +00 */
    LPWAVEFORMATEX  pwfxSrc;        /* +04 */
    LPWAVEFORMATEX  pwfxDst;        /* +08 */
    LPWAVEFILTER    pwfltr;         /* +0C */
    DWORD           dwCallback;     /* +10 */
    DWORD           dwInstance;     /* +14 */
    DWORD           fdwOpen;        /* +18 */
    DWORD           fdwDriver;      /* +1C */
    DWORD           dwDriver;       /* +20 MSADPCM: the CONVERTPROC */
    HACMSTREAM      has;            /* +24 */
} ACMDRVSTREAMINSTANCE, *PACMDRVSTREAMINSTANCE, FAR *LPACMDRVSTREAMINSTANCE;

typedef struct tACMDRVSTREAMHEADER FAR *LPACMDRVSTREAMHEADER;
typedef struct tACMDRVSTREAMHEADER {
    DWORD                   cbStruct;           /* +00 */
    DWORD                   fdwStatus;          /* +04 */
    DWORD                   dwUser;             /* +08 */
    LPBYTE                  pbSrc;              /* +0C */
    DWORD                   cbSrcLength;        /* +10 */
    DWORD                   cbSrcLengthUsed;    /* +14 */
    DWORD                   dwSrcUser;          /* +18 */
    LPBYTE                  pbDst;              /* +1C */
    DWORD                   cbDstLength;        /* +20 */
    DWORD                   cbDstLengthUsed;    /* +24 */
    DWORD                   dwDstUser;          /* +28 */
    DWORD                   fdwConvert;         /* +2C */
    LPACMDRVSTREAMHEADER    padshNext;          /* +30 */
    DWORD                   fdwDriver;          /* +34 */
    DWORD                   dwDriver;           /* +38 */
    DWORD                   fdwPrepared;        /* +3C ACM bookkeeping from here on */
    DWORD                   dwPrepared;
    LPBYTE                  pbPreparedSrc;
    DWORD                   cbPreparedSrcLength;
    LPBYTE                  pbPreparedDst;
    DWORD                   cbPreparedDstLength;
} ACMDRVSTREAMHEADER, *PACMDRVSTREAMHEADER;

typedef struct tACMDRVSTREAMSIZE {
    DWORD   cbStruct;               /* +00 */
    DWORD   fdwSize;                /* +04 */
    DWORD   cbSrcLength;            /* +08 */
    DWORD   cbDstLength;            /* +0C */
} ACMDRVSTREAMSIZE, *PACMDRVSTREAMSIZE, FAR *LPACMDRVSTREAMSIZE;

typedef struct tACMDRVFORMATSUGGEST {
    DWORD           cbStruct;       /* +00 */
    DWORD           fdwSuggest;     /* +04 */
    LPWAVEFORMATEX  pwfxSrc;        /* +08 */
    DWORD           cbwfxSrc;       /* +0C */
    LPWAVEFORMATEX  pwfxDst;        /* +10 */
    DWORD           cbwfxDst;       /* +14 */
} ACMDRVFORMATSUGGEST, *PACMDRVFORMATSUGGEST, FAR *LPACMDRVFORMATSUGGEST;

#pragma pack(pop)

#endif /* RC_INVOKED */

#endif /* _INC_ACMDRV */
