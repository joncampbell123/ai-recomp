/*
 * msacm.h - stand-in for the Audio Compression Manager header of the
 * Video for Windows 1.1 DK, written for the Open Watcom build (Open
 * Watcom ships no ACM headers).
 *
 * Only what the VfW 1.1 tools use.  Win16 layouts.  The functions are
 * imported from MSACM.DLL by ordinal in the linker directives.
 */

#ifndef _INC_ACM
#define _INC_ACM

#ifndef mmioFOURCC
#define mmioFOURCC(ch0, ch1, ch2, ch3) \
    ((DWORD)(BYTE)(ch0) | ((DWORD)(BYTE)(ch1) << 8) | \
     ((DWORD)(BYTE)(ch2) << 16) | ((DWORD)(BYTE)(ch3) << 24))
#endif

#define ACMAPI                              FAR PASCAL

#define ACMERR_BASE                         512
#define ACMERR_NOTPOSSIBLE                  (ACMERR_BASE + 0)
#define ACMERR_BUSY                         (ACMERR_BASE + 1)
#define ACMERR_UNPREPARED                   (ACMERR_BASE + 2)
#define ACMERR_CANCELED                     (ACMERR_BASE + 3)

#define ACM_METRIC_MAX_SIZE_FORMAT          50

#define ACMFORMATTAGDETAILS_FORMATTAG_CHARS 48
#define ACMFORMATDETAILS_FORMAT_CHARS       128

#define ACM_FORMATDETAILSF_INDEX            0x00000000L
#define ACM_FORMATDETAILSF_FORMAT           0x00000001L
#define ACM_FORMATDETAILSF_QUERYMASK        0x0000000FL

#define ACM_FORMATENUMF_WFORMATTAG          0x00010000L
#define ACM_FORMATENUMF_NCHANNELS           0x00020000L
#define ACM_FORMATENUMF_NSAMPLESPERSEC      0x00040000L
#define ACM_FORMATENUMF_WBITSPERSAMPLE      0x00080000L
#define ACM_FORMATENUMF_CONVERT             0x00100000L
#define ACM_FORMATENUMF_SUGGEST             0x00200000L
#define ACM_FORMATENUMF_HARDWARE            0x00400000L
#define ACM_FORMATENUMF_INPUT               0x00800000L
#define ACM_FORMATENUMF_OUTPUT              0x01000000L

#define ACMFORMATCHOOSE_STYLEF_SHOWHELP             0x00000004L
#define ACMFORMATCHOOSE_STYLEF_ENABLEHOOK           0x00000008L
#define ACMFORMATCHOOSE_STYLEF_ENABLETEMPLATE       0x00000010L
#define ACMFORMATCHOOSE_STYLEF_ENABLETEMPLATEHANDLE 0x00000020L
#define ACMFORMATCHOOSE_STYLEF_INITTOWFXSTRUCT      0x00000040L

#ifndef RC_INVOKED

DECLARE_HANDLE(HACMOBJ);

#pragma pack(push, 1)

typedef struct tACMFORMATDETAILS {
    DWORD           cbStruct;       /* +00 */
    DWORD           dwFormatIndex;  /* +04 */
    DWORD           dwFormatTag;    /* +08 */
    DWORD           fdwSupport;     /* +0C */
    LPWAVEFORMATEX  pwfx;           /* +10 */
    DWORD           cbwfx;          /* +14 */
    char            szFormat[ACMFORMATDETAILS_FORMAT_CHARS];    /* +18 */
} ACMFORMATDETAILS, *PACMFORMATDETAILS, FAR *LPACMFORMATDETAILS;  /* 0x98 bytes */

typedef UINT (CALLBACK *ACMFORMATCHOOSEHOOKPROC)(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

typedef struct tACMFORMATCHOOSE {
    DWORD           cbStruct;       /* +00 */
    DWORD           fdwStyle;       /* +04 */
    HWND            hwndOwner;      /* +08 */
    LPWAVEFORMATEX  pwfx;           /* +0A */
    DWORD           cbwfx;          /* +0E */
    LPCSTR          pszTitle;       /* +12 */
    char            szFormatTag[ACMFORMATTAGDETAILS_FORMATTAG_CHARS];   /* +16 */
    char            szFormat[ACMFORMATDETAILS_FORMAT_CHARS];            /* +46 */
    LPSTR           pszName;        /* +C6 */
    DWORD           cchName;        /* +CA */
    DWORD           fdwEnum;        /* +CE */
    LPWAVEFORMATEX  pwfxEnum;       /* +D2 */
    HINSTANCE       hInstance;      /* +D6 */
    LPCSTR          pszTemplateName;/* +D8 */
    LPARAM          lCustData;      /* +DC */
    ACMFORMATCHOOSEHOOKPROC pfnHook;/* +E0 */
} ACMFORMATCHOOSE, *PACMFORMATCHOOSE, FAR *LPACMFORMATCHOOSE;     /* 0xE4 bytes */

#pragma pack(pop)

DWORD ACMAPI acmGetVersion(void);                                           /* MSACM.7 */
UINT  ACMAPI acmMetrics(HACMOBJ hao, UINT uMetric, LPVOID pMetric);         /* MSACM.8 */
UINT  ACMAPI acmFormatChoose(LPACMFORMATCHOOSE pafmtc);                     /* MSACM.40 */
UINT  ACMAPI acmFormatDetails(HACMOBJ had, LPACMFORMATDETAILS pafd, DWORD fdwDetails); /* MSACM.41 */

#endif /* RC_INVOKED */

#endif /* _INC_ACM */
