/*
 * compress.c - code segment 12 of VIDEDIT.EXE (0x1BBE bytes):
 * Video/Compression Options.
 *
 * The dialog (IDD_COMPRESSION) sets the options used when the movie is
 * saved: a target (preset flags, data rate and interleave), the video
 * compressor, data rate, key frame and interleave settings and the
 * quality.  "Details>>" enlarges it to show those settings and
 * "Preview>>" further to show the current frame compressed with the
 * chosen compressor and quality.  The dialog sizes are the bottom right
 * corners of the black rectangles 151, 152 and 150; controls outside the
 * current size are disabled.
 *
 * The compressors are the three built in ones ("none": no recompression,
 * "RLE ": Microsoft RLE through MEDDIBS's RleCompress, "DIB ": full
 * frames) followed by every installed ICM video compressor.  Defaults
 * are kept in [VidEdit] of MMTOOLS.INI.
 *
 * Data: DGROUP module at DS:0750-0B6F (initialised) and DS:1D44-21C9,
 * DS:3076, DS:3262.  The INI key names are in the code segment.
 * Functions are in the order of the binary.
 */

#include "videdit.h"
#include <string.h>
#include "compress.h"

/* dialog controls */
#define IDC_DATARATECHECK       101
#define IDC_DATARATEARROW       102
#define IDC_KEYFRAMEARROW       103
#define IDC_INTERLEAVECHECK     105
#define IDC_KEYFRAMECHECK       110
#define IDC_DATARATE            112
#define IDC_KEYFRAMES           113
#define IDC_INTERLEAVE          116
#define IDC_PAD                 130
#define IDC_USEDEFAULT          131
#define IDC_SAVEDEFAULT         132
#define IDC_COMPRESSOR          133
#define IDC_CONFIGURE           134
#define IDC_ABOUT               135
#define IDC_QUALITY             140
#define IDC_QUALITYTEXT         141
#define IDC_DETAILS             142
#define IDC_TARGET              145
#define IDC_PREVIEW             146
#define IDC_SIZEPREVIEW         150     /* black rectangles: the three sizes */
#define IDC_SIZESMALL           151
#define IDC_SIZEDETAILS         152
#define IDC_PREVIEWRECT         155
#define IDC_COMPRESSEDTEXT      180
#define IDC_FULLSIZETEXT        181
#define IDC_FRAMETEXT           182
#define IDC_RATIOTEXT           183
#define IDC_FRAMESCROLL         185
#define IDC_REPAINTPREVIEW      104     /* posted to itself on WM_PAINT */
#define IDC_RECOMPRESS          186     /* not a control */

/* string table */
#define IDS_NORECOMPRESSION     490
#define IDS_FULLFRAMES          491
#define IDS_TARGET0             492     /* "Hard Disk" .. 499 "Custom" */
#define IDS_FRAMEN              500     /* "Frame %d" */
#define IDS_FULLSIZE            501     /* "Full Size: %ld bytes" */
#define IDS_COMPRESSEDSIZE      502     /* "Compressed: %ld bytes" */
#define IDS_RATIO               503     /* "Ratio: %d %%" */
#define IDS_MSRLE               504     /* "Microsoft RLE" */
#define IDS_PCM                 505     /* "PCM (Uncompressed)" */
#define IDS_UNKNOWN             506     /* "Unknown" */

#define FCC_NONE    mmioFOURCC('n', 'o', 'n', 'e')
#define FCC_RLE     mmioFOURCC('R', 'L', 'E', ' ')
#define FCC_RLE0    mmioFOURCC('R', 'L', 'E', '0')
#define FCC_DIB     mmioFOURCC('D', 'I', 'B', ' ')
#define FCC_PCM     mmioFOURCC('p', 'c', 'm', ' ')

/* a DIB is the selector of a locked block, the header at offset 0 */
#define DIBHDR(sel)     ((LPBITMAPINFOHEADER)MAKELP(sel, 0))
#define DIBBITS(sel)    MAKELP(sel, (WORD)DIBHDR(sel)->biSize + (WORD)DIBHDR(sel)->biClrUsed * 4)

typedef struct {
    FOURCC  fccHandler;                 /* 00 */
    PSTR    pszName;                    /* 04 */
    FARPROC lpfnConfigure;              /* 06 never set */
    WORD    fOK;                        /* 0A usable for the current format */
    DWORD   dwDefQuality;               /* 0C */
    DWORD   dwDefKeyFrameRate;          /* 10 */
} COMPRESSOR;                           /* 0x14 bytes */

typedef struct {
    PSTR    pszName;                    /* 00 */
    DWORD   dwFlags;                    /* 02 COMPF_* */
    DWORD   dwDataRate;                 /* 06 */
    DWORD   dwInterleave;               /* 0A */
} TARGET;                               /* 0x0E bytes */

typedef BOOL (FAR PASCAL *CONFIGUREPROC)(HWND hwnd, LPVOID FAR *lplpState, DWORD FAR *lpcbState);
typedef BOOL (FAR PASCAL *RLECOMPRESSPROC)(WORD w1, WORD selOut, WORD selPrev, WORD selIn,
                                           HPALETTE hpal, LONG lMaxSize, LONG lTolerance);

BOOL   FAR PASCAL GetMovieFormat(LPBITMAPINFOHEADER lpbi);      /* frames.c */
BOOL   FAR        IsUnchangedAvi(void);                         /* frames.c */
UINT   FAR        GetFrameCount(void);                          /* frames.c */
WORD   FAR PASCAL GetFullFrame(UINT iFrame, HPALETTE NEAR *phpal, WORD wFlags); /* fullfrm.c */
LONG   FAR PASCAL muldiv32(LONG a, LONG b, LONG c);             /* muldiv32.c */

/* INI keys, in the code segment */
char _based(_segname("_CODE")) szDefaultAudioCompression[] = "DefaultAudioCompression"; /* s12:002C, not used */
static char _based(_segname("_CODE")) szDefaultVideoCompression[] = "DefaultVideoCompression";
static char _based(_segname("_CODE")) szDefaultDataRate[]         = "DefaultDataRate";
char _based(_segname("_CODE")) szDefaultQualityTemporal[]  = "DefaultQualityTemporal";  /* not used */
static char _based(_segname("_CODE")) szDefaultQuality[]          = "DefaultQuality";
static char _based(_segname("_CODE")) szDefaultAudFrameRate[]     = "DefaultAudFrameRate";
static char _based(_segname("_CODE")) szDefaultKeyFrameRate[]     = "DefaultKeyFrameRate";
static char _based(_segname("_CODE")) szDefaultFrameRate[]        = "DefaultFrameRate";
static char _based(_segname("_CODE")) szDefaultFlags[]            = "DefaultFlags";
char _based(_segname("_CODE")) szSystemIni[]               = "SYSTEM.INI";              /* not used */

/* initialised data, DS:0750-0B6F (after the module's "AnimSTrackBar",
   "GrayBlob" header strings) */
WORD        gfCompListed    = 0;        /* DS:0768 installed compressors added */
WORD        gnCompressors   = 3;        /* DS:076A */
WORD        gwCompress076c  = 1;        /* DS:076C not used */
COMPRESSOR  gaCompressors[40] = {       /* DS:0772, names set by the dialog */
    { FCC_NONE, "", NULL, 0, 0L,    7L },
    { FCC_RLE,  "", NULL, 0, 8500L, 7L },
    { FCC_DIB,  "", NULL, 0, 0L,    7L },
};
COMPRESSOR  gaAudioCompressors[1] = {   /* DS:0A94 */
    { FCC_PCM,  "" },
};
TARGET      gaTargets[8] = {            /* DS:0AB0, names set by the dialog */
    { "", COMPF_NOPAD | COMPF_KEYFRAMES,                                     0L,           0L },
    { "", COMPF_NOPAD | COMPF_INTERLEAVE | COMPF_KEYFRAMES,                  0L,           1L },
    { "", COMPF_NOPAD | COMPF_INTERLEAVE | COMPF_DATARATE | COMPF_KEYFRAMES, 300L * 1024, 1L },
    { "", COMPF_NOPAD | COMPF_INTERLEAVE | COMPF_DATARATE | COMPF_KEYFRAMES, 150L * 1024, 1L },
    { "", COMPF_NOPAD | COMPF_INTERLEAVE | COMPF_DATARATE | COMPF_KEYFRAMES, 100L * 1024, 1L },
    { "", COMPF_INTERLEAVE | COMPF_DATARATE | COMPF_KEYFRAMES,               150L * 1024, 1L },
    { "", COMPF_INTERLEAVE | COMPF_DATARATE | COMPF_KEYFRAMES,               80L * 1024,  1L },
    { "", COMPF_NOPAD | COMPF_INTERLEAVE | COMPF_DATARATE | COMPF_KEYFRAMES, 150L * 1024, 1L },
};
/* DS:0B20 "Changing Video Compressor type to something valid!\n": a debug
   message whose string was kept although nothing refers to it */

/* uninitialised data */
DWORD       gdwICValue;                 /* DS:1D44 */
char        gszCompFormat[128];         /* DS:1D48 format strings loaded for wsprintf */
HGLOBAL     ghCompress1dc8;             /* DS:1DC8 freed, never set */
WORD        gselPreviewIn;              /* DS:1DCA the frame as a DIB */
WORD        gselPreviewOut;             /* DS:1DCC compressed */
HPALETTE    ghpalPreview;               /* DS:1DCE palette of the frame */
HDRAWDIB    ghddPreview;                /* DS:1DD2 */
char        gszNoRecompression[40];     /* DS:1DD4 */
char        gszMSRLE[40];               /* DS:1DFC */
char        gszFullFrames[40];          /* DS:1E24 */
char        gszPCM[40];                 /* DS:1E4C */
char        gszTarget0[40];             /* DS:1E74 */
char        gszTarget1[60];             /* DS:1E9C */
char        gszTarget2[60];             /* DS:1ED8 */
char        gszTarget3[60];             /* DS:1F14 */
char        gszTarget4[60];             /* DS:1F50 */
char        gszTarget5[56];             /* DS:1F8C */
char        gszTarget6[56];             /* DS:1FC4 */
char        gszTarget7[40];             /* DS:1FFC */
char        gszUnknown[40];             /* DS:2024 */
BITMAPINFOHEADER gbihLast;              /* DS:204C format the list was checked for */
COMPOPTIONS gCompOptionsDlg;            /* DS:2074 the dialog's working copy */
HIC         ghicPreview;                /* DS:2098 */
FOURCC      gfccPreview;                /* DS:209A */
ICINFO      gicinfo;                    /* DS:209E */
DWORD       gdwUSecPerFrameOld;         /* DS:21C6 for undo */
WORD        gidCompressSize;            /* DS:3076 IDC_SIZE*: current dialog size */
RLECOMPRESSPROC glpfnRleCompress;       /* DS:3262 MEDDIBS!RleCompress */

static void FAR _cdecl CompressPreviewStart(HWND hDlg);
static void FAR _cdecl CompressPreviewPaint(HWND hDlg, BOOL fErase);
static void FAR _cdecl CompressPreviewFrame(HWND hDlg);

/*
 * s12:0000-002B  hmemset  (FAR _cdecl)
 */
void FAR _cdecl hmemset(BYTE _huge *hp, BYTE b, LONG cb)
{
    while (cb-- > 0)
        *hp++ = b;
}

/*
 * s12:00E4-014F  ListCompressors  (NEAR)
 *
 * Once: appends the installed video compressors to the table (no check
 * against its 40 entries).
 */
static void NEAR ListCompressors(void)
{
    ICINFO  icinfo;
    int     i;

    if (gfCompListed)
        return;
    gfCompListed = 1;
    for (i = 0; ICInfo(ICTYPE_VIDEO, (DWORD)i, &icinfo); i++) {
        gaCompressors[gnCompressors].fccHandler = icinfo.fccHandler;
        gaCompressors[gnCompressors].lpfnConfigure = NULL;
        gnCompressors++;
    }
}

/*
 * s12:0150-02D9  CheckCompressors  (NEAR)
 *
 * When the movie's format changed since the last check, asks every
 * compressor whether it can compress it, and gets the names of the
 * installed ones and their default quality and key frame rate.  RLE is
 * always allowed for 8 bit video, full frames always, and no
 * recompression when segment 6 says so.  Without a movie every
 * compressor is allowed.
 */
static void NEAR CheckCompressors(void)
{
    BITMAPINFOHEADER bih;               /* [bp-2C] */
    FOURCC      fcc;                    /* [bp-30] */
    BOOL        fChanged = FALSE;       /* [bp-04] */
    int         i;                      /* [bp-02] */
    COMPRESSOR *pc;
    HIC         hic;

    ListCompressors();
    if (!GetMovieFormat(&bih)) {
        _fmemcpy(&bih, &gbihLast, sizeof(bih));
        bih.biBitCount = (WORD)-1;
    }
    if (_fmemcmp(&bih, &gbihLast, sizeof(bih)))
        fChanged = TRUE;
    _fmemcpy(&gbihLast, &bih, sizeof(bih));

    for (i = 0, pc = gaCompressors; i < gnCompressors; i++, pc++) {
        if (fChanged) {
            hic = ICOpen(ICTYPE_VIDEO, pc->fccHandler, ICMODE_COMPRESS);
            if (hic) {
                if (pc->pszName == NULL) {
                    ICGetInfo(hic, &gicinfo, sizeof(ICINFO));
                    pc->pszName = LocalStrDup(gicinfo.szDescription);
                }
                if (bih.biBitCount == (WORD)-1)
                    pc->fOK = TRUE;
                else
                    pc->fOK = (ICCompressQuery(hic, (LPVOID)&bih, NULL) == ICERR_OK);
                ICSendMessage(hic, ICM_GETDEFAULTQUALITY, (DWORD)(LPVOID)&gdwICValue, sizeof(DWORD));
                pc->dwDefQuality = gdwICValue;
                ICSendMessage(hic, ICM_GETDEFAULTKEYFRAMERATE, (DWORD)(LPVOID)&gdwICValue, sizeof(DWORD));
                pc->dwDefKeyFrameRate = gdwICValue;
                ICClose(hic);
            } else {
                pc->fOK = FALSE;
            }
        }
        if (gbihLast.biBitCount == 8) {
            fcc = pc->fccHandler;
            if (fcc == FCC_RLE || fcc == FCC_RLE0)
                pc->fOK = TRUE;
        }
        if (pc->fccHandler == FCC_NONE)
            pc->fOK = IsUnchangedAvi();
        if (pc->fccHandler == FCC_DIB)
            pc->fOK = TRUE;
        if (!fChanged && bih.biBitCount == (WORD)-1)
            pc->fOK = TRUE;
    }
}

/*
 * s12:02DA-0335  FillCompressorList  (NEAR PASCAL)
 *
 * Adds the usable compressors that have a name to the combo box, with
 * their table index as item data.
 */
static void NEAR PASCAL FillCompressorList(HWND hDlg)
{
    int         i;
    COMPRESSOR *pc;

    CheckCompressors();
    for (i = 0, pc = gaCompressors; i < gnCompressors; i++, pc++) {
        if (pc->fOK && pc->pszName && *pc->pszName)
            SendDlgItemMessage(hDlg, IDC_COMPRESSOR, CB_SETITEMDATA,
                (WPARAM)SendDlgItemMessage(hDlg, IDC_COMPRESSOR, CB_ADDSTRING, 0,
                                           (LPARAM)(LPSTR)pc->pszName),
                (LPARAM)i);
    }
}

/*
 * s12:0336-0411  GetCompressorName  (FAR PASCAL)
 *
 * Name of the compressor of the current options (fCurrent) or of the
 * copy at DS:2D2A.  For the current options a compressor that can't
 * compress the format is replaced by the first one that can (and the
 * options are changed to it).
 *
 * Quirk: the names of the built in compressors are only loaded by the
 * Compression Options dialog; before it has been opened they are "".
 */
LPSTR FAR PASCAL GetCompressorName(BOOL fCurrent)
{
    FOURCC      fcc;                    /* [bp-4] */
    FOURCC      fccT;                   /* [bp-8] */
    UINT        i;
    COMPRESSOR *pc;

    CheckCompressors();
    fcc = fCurrent ? gCompOptions.fccHandler : gCompOptionsSaved.fccHandler;
    if (fcc == FCC_RLE0)
        fcc = FCC_RLE;
    AnsiLowerBuff((LPSTR)&fcc, 4);

restart:
    for (i = 0; i < (UINT)gnCompressors; i++) {
        pc = &gaCompressors[i];
        fccT = pc->fccHandler;
        AnsiLowerBuff((LPSTR)&fccT, 4);
        if (fccT != fcc && fcc != 0)
            continue;
        if (fCurrent && !pc->fOK) {
            if (fcc != 0) {
                fcc = 0;
                goto restart;
            }
            continue;
        }
        if (fCurrent && fcc == 0)
            gCompOptions.fccHandler = gaCompressors[i].fccHandler;
        return (LPSTR)gaCompressors[i].pszName;
    }

    LoadString(ghInst, IDS_UNKNOWN, gszUnknown, sizeof(gszUnknown));
    return gszUnknown;
}

/*
 * s12:0412-0543  GetCompressDefaults  (FAR PASCAL)
 *
 * The default options from MMTOOLS.INI: flags 14 (interleave, data rate,
 * key frames), 15 fps (only if fFrameRate), 150 KB/s, MSVC, quality
 * 8500, a key frame every 15 frames, audio every frame.
 */
void FAR PASCAL GetCompressDefaults(LPCOMPOPTIONS lpopt, BOOL fFrameRate)
{
    char ach[20];

    lpopt->dwFlags = GetPrivateProfileInt(gszAppName, szDefaultFlags,
                                          COMPF_INTERLEAVE | COMPF_DATARATE | COMPF_KEYFRAMES,
                                          gszIniFile);
    if (fFrameRate) {
        GetPrivateProfileString(gszAppName, szDefaultFrameRate, "15", ach, sizeof(ach), gszIniFile);
        lpopt->dwUSecPerFrame = FrameRateToUSec(ach);
    }
    lpopt->dwDataRate = (DWORD)GetPrivateProfileInt(gszAppName, szDefaultDataRate, 150, gszIniFile) << 10;
    lpopt->dwKeyFrameEvery = GetPrivateProfileInt(gszAppName, szDefaultKeyFrameRate, 15, gszIniFile);
    lpopt->dwInterleave = GetPrivateProfileInt(gszAppName, szDefaultAudFrameRate, 1, gszIniFile);
    lpopt->dwQuality = GetPrivateProfileInt(gszAppName, szDefaultQuality, 8500, gszIniFile);
    GetPrivateProfileString(gszAppName, szDefaultVideoCompression, "MSVC", ach, sizeof(ach), gszIniFile);
    lpopt->fccHandler = mmioStringToFOURCC(ach, MMIO_TOUPPER);
    lpopt->cbState = 0;
    lpopt->lpState = NULL;
}

/*
 * s12:0544-0589  DoCompressionOptions  (FAR PASCAL)
 *
 * Video/Compression Options.
 */
void FAR PASCAL DoCompressionOptions(HWND hwndParent)
{
    FARPROC lpfn;
    int     fOK;

    lpfn = MakeProcInstance((FARPROC)CompressDlgProc, ghInst);
    fOK = DoDialog(IDD_COMPRESSION, hwndParent, lpfn);
    FreeProcInstance(lpfn);
    if (fOK) {
        UpdateLength();
        SetDirty();
    }
}

/*
 * s12:058A-0637  SaveCompressDefaults  (NEAR PASCAL)
 *
 * "Save as Default".  WriteIniInt is given the default of each key.
 */
static void NEAR PASCAL SaveCompressDefaults(LPCOMPOPTIONS lpopt)
{
    char ach[20];

    WriteIniInt(gszAppName, szDefaultDataRate, 150, (int)(lpopt->dwDataRate >> 10));
    WriteIniInt(gszAppName, szDefaultKeyFrameRate, 15, (int)lpopt->dwKeyFrameEvery);
    WriteIniInt(gszAppName, szDefaultAudFrameRate, 1, (int)lpopt->dwInterleave);
    WriteIniInt(gszAppName, szDefaultQuality, 8500, (int)lpopt->dwQuality);
    WriteIniInt(gszAppName, szDefaultFlags, COMPF_INTERLEAVE | COMPF_DATARATE | COMPF_KEYFRAMES,
                (int)lpopt->dwFlags);
    *(FOURCC *)ach = lpopt->fccHandler;
    ach[4] = '\0';
    WritePrivateProfileString(gszAppName, szDefaultVideoCompression, ach, gszIniFile);
}

/*
 * s12:0638-0751  GetDlgOptions  (NEAR PASCAL)
 *
 * Reads the dialog into *lpopt (not the frame rate or the state).
 */
static void NEAR PASCAL GetDlgOptions(HWND hDlg, LPCOMPOPTIONS lpopt)
{
    BOOL f;

    lpopt->dwDataRate = (DWORD)GetDlgItemInt(hDlg, IDC_DATARATE, &f, FALSE) << 10;
    lpopt->dwKeyFrameEvery = GetDlgItemInt(hDlg, IDC_KEYFRAMES, &f, FALSE);
    lpopt->dwInterleave = GetDlgItemInt(hDlg, IDC_INTERLEAVE, &f, FALSE);
    lpopt->dwQuality = (LONG)(GetScrollPos(GetDlgItem(hDlg, IDC_QUALITY), SB_CTL) * 100);
    lpopt->dwFlags = 0;
    if (!IsDlgButtonChecked(hDlg, IDC_PAD))
        lpopt->dwFlags |= COMPF_NOPAD;
    if (IsDlgButtonChecked(hDlg, IDC_INTERLEAVECHECK))
        lpopt->dwFlags |= COMPF_INTERLEAVE;
    if (IsDlgButtonChecked(hDlg, IDC_DATARATECHECK))
        lpopt->dwFlags |= COMPF_DATARATE;
    if (IsDlgButtonChecked(hDlg, IDC_KEYFRAMECHECK))
        lpopt->dwFlags |= COMPF_KEYFRAMES;
    lpopt->fccHandler = gaCompressors[(int)SendDlgItemMessage(hDlg, IDC_COMPRESSOR, CB_GETITEMDATA,
        (WPARAM)SendDlgItemMessage(hDlg, IDC_COMPRESSOR, CB_GETCURSEL, 0, 0L), 0L)].fccHandler;
}

/*
 * s12:0752-077F  FourCCEqual  (FAR _cdecl)
 */
static BOOL FAR _cdecl FourCCEqual(FOURCC fcc1, FOURCC fcc2)
{
    AnsiLowerBuff((LPSTR)&fcc1, 4);
    AnsiLowerBuff((LPSTR)&fcc2, 4);
    return fcc2 == fcc1;
}

/*
 * s12:0780-0A0F  SetDlgOptions  (NEAR PASCAL)
 *
 * Shows *lpopt in the dialog; with fSelect also selects its compressor
 * (sending CBN_SELCHANGE) and the matching target ("Custom" if none).
 * Turns off the flags whose value is 0, and makes "RLE0" "RLE ".
 *
 * Quirk: the compressor search tries the item indexes count .. 1, so it
 * starts one past the last item and never tests item 0, which is what
 * is selected when nothing matches.
 */
static void NEAR PASCAL SetDlgOptions(HWND hDlg, LPCOMPOPTIONS lpopt, BOOL fSelect)
{
    int     i;
    int     idx;
    BOOL    fEnabled;
    TARGET *pt;

    if (fSelect) {
        for (i = (int)SendDlgItemMessage(hDlg, IDC_COMPRESSOR, CB_GETCOUNT, 0, 0L); i > 0; i--) {
            idx = (int)SendDlgItemMessage(hDlg, IDC_COMPRESSOR, CB_GETITEMDATA, i, 0L);
            if (FourCCEqual(lpopt->fccHandler, gaCompressors[idx].fccHandler))
                break;
        }
        SendDlgItemMessage(hDlg, IDC_COMPRESSOR, CB_SETCURSEL, i, 0L);
        SendMessage(hDlg, WM_COMMAND, IDC_COMPRESSOR,
                    MAKELONG(GetDlgItem(hDlg, IDC_COMPRESSOR), CBN_SELCHANGE));
    }

    if (lpopt->dwKeyFrameEvery == 0)
        lpopt->dwFlags &= ~COMPF_KEYFRAMES;
    if (lpopt->dwInterleave == 0)
        lpopt->dwFlags &= ~COMPF_INTERLEAVE;
    if (lpopt->dwDataRate == 0)
        lpopt->dwFlags &= ~COMPF_DATARATE;
    if (lpopt->fccHandler == FCC_RLE0)
        lpopt->fccHandler = FCC_RLE;

    if (fSelect) {
        for (i = 0, pt = gaTargets; pt < &gaTargets[7]; i++, pt++) {
            if (!((lpopt->dwFlags ^ pt->dwFlags) &
                  (COMPF_NOPAD | COMPF_INTERLEAVE | COMPF_DATARATE)) &&
                (!(lpopt->dwFlags & COMPF_DATARATE) || pt->dwDataRate == lpopt->dwDataRate) &&
                (!(lpopt->dwFlags & COMPF_INTERLEAVE) || pt->dwInterleave == lpopt->dwInterleave))
                break;
        }
        SendDlgItemMessage(hDlg, IDC_TARGET, CB_SETCURSEL, i, 0L);
    }

    SetDlgItemInt(hDlg, IDC_DATARATE, (UINT)(lpopt->dwDataRate >> 10), FALSE);
    SetDlgItemInt(hDlg, IDC_KEYFRAMES, (UINT)lpopt->dwKeyFrameEvery, FALSE);
    SetDlgItemInt(hDlg, IDC_INTERLEAVE, (UINT)lpopt->dwInterleave, FALSE);

    fEnabled = IsWindowEnabled(GetDlgItem(hDlg, IDC_QUALITY));
    if (lpopt->dwQuality > ICQUALITY_HIGH)
        lpopt->dwQuality = 8500;
    SetDlgItemInt(hDlg, IDC_QUALITYTEXT, (UINT)(lpopt->dwQuality / 100), FALSE);
    SetScrollRange(GetDlgItem(hDlg, IDC_QUALITY), SB_CTL, 0, 100, FALSE);
    SetScrollPos(GetDlgItem(hDlg, IDC_QUALITY), SB_CTL, (int)(lpopt->dwQuality / 100), fEnabled);
    SetScrollRange(GetDlgItem(hDlg, IDC_FRAMESCROLL), SB_CTL, 0, GetFrameCount(), FALSE);
    SetScrollPos(GetDlgItem(hDlg, IDC_FRAMESCROLL), SB_CTL, gwCurFrame, TRUE);

    CheckDlgButton(hDlg, IDC_PAD, !(lpopt->dwFlags & COMPF_NOPAD));
    CheckDlgButton(hDlg, IDC_INTERLEAVECHECK, (lpopt->dwFlags & COMPF_INTERLEAVE) != 0);
    CheckDlgButton(hDlg, IDC_DATARATECHECK, (lpopt->dwFlags & COMPF_DATARATE) != 0);
    CheckDlgButton(hDlg, IDC_KEYFRAMECHECK, (lpopt->dwFlags & COMPF_KEYFRAMES) != 0);
}

/*
 * s12:0A10-0AD9  CompressSetSize  (FAR _cdecl)
 *
 * Sizes the dialog so that its client area ends at the bottom right
 * corner of the control idRect, and enables only the controls whose top
 * left corner is inside.
 */
static void FAR _cdecl CompressSetSize(HWND hDlg, WORD idRect)
{
    RECT    rc;                         /* [bp-0E] */
    RECT    rcChild;                    /* [bp-16] */
    POINT   pt;                         /* [bp-06] */
    HWND    hwnd;

    gidCompressSize = idRect;
    GetWindowRect(GetDlgItem(hDlg, idRect), &rc);
    pt.x = rc.right;
    pt.y = rc.bottom;
    for (hwnd = GetWindow(hDlg, GW_CHILD); hwnd; hwnd = GetWindow(hwnd, GW_HWNDNEXT)) {
        GetWindowRect(hwnd, &rcChild);
        EnableWindow(hwnd, rcChild.left < pt.x && rcChild.top < pt.y);
    }
    ScreenToClient(hDlg, (LPPOINT)&rc.right);
    rc.left = rc.top = 0;
    AdjustWindowRect(&rc, GetWindowLong(hDlg, GWL_STYLE), GetMenu(hDlg) != NULL);
    SetWindowPos(hDlg, NULL, 0, 0, rc.right - rc.left, rc.bottom - rc.top,
                 SWP_NOZORDER | SWP_NOMOVE | SWP_NOACTIVATE);
}

/*
 * s12:0ADA-15F3  CompressDlgProc  (FAR PASCAL, loads DS from SS, not
 * exported, returns a LONG)
 *
 * Quirks of the original:
 *  - Configure keeps the compressor's state only when ICM_CONFIGURE
 *    returns non-zero (not ICERR_OK).
 *  - The "configure" function of the built in compressors is never set,
 *    so for them Configure is always disabled.
 *  - A state allocated for the dialog copy is not freed on Cancel.
 */
LONG FAR PASCAL _loadds CompressDlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
    COMPOPTIONS opts;                   /* [bp-3E] */
    COMPRESSOR *pc;                     /* [bp-40] */
    char        ach[24];                /* [bp-1A] */
    TARGET     *pt;
    DWORD       cb;
    WORD        sel;
    int         i;
    int         id;
    int         pos;
    BOOL        f;
    BOOL        fConfig;
    BOOL        fAbout;
    WORD        w;

    switch (msg) {
    case WM_PAINT:
        PostMessage(hDlg, WM_COMMAND, IDC_REPAINTPREVIEW, 0L);
        return 0L;

    case WM_INITDIALOG:
        LoadString(ghInst, IDS_NORECOMPRESSION, gszNoRecompression, sizeof(gszNoRecompression));
        LoadString(ghInst, IDS_MSRLE, gszMSRLE, sizeof(gszMSRLE));
        LoadString(ghInst, IDS_FULLFRAMES, gszFullFrames, sizeof(gszFullFrames));
        LoadString(ghInst, IDS_PCM, gszPCM, sizeof(gszPCM));
        LoadString(ghInst, IDS_TARGET0 + 0, gszTarget0, 40);
        LoadString(ghInst, IDS_TARGET0 + 1, gszTarget1, 60);
        LoadString(ghInst, IDS_TARGET0 + 2, gszTarget2, 60);
        LoadString(ghInst, IDS_TARGET0 + 3, gszTarget3, 60);
        LoadString(ghInst, IDS_TARGET0 + 4, gszTarget4, 60);
        LoadString(ghInst, IDS_TARGET0 + 5, gszTarget5, 55);
        LoadString(ghInst, IDS_TARGET0 + 6, gszTarget6, 55);
        LoadString(ghInst, IDS_TARGET0 + 7, gszTarget7, 40);
        gaCompressors[0].pszName = gszNoRecompression;
        gaCompressors[1].pszName = gszMSRLE;
        gaCompressors[2].pszName = gszFullFrames;
        gaAudioCompressors[0].pszName = gszPCM;
        gaTargets[0].pszName = gszTarget0;
        gaTargets[1].pszName = gszTarget1;
        gaTargets[2].pszName = gszTarget2;
        gaTargets[3].pszName = gszTarget3;
        gaTargets[4].pszName = gszTarget4;
        gaTargets[5].pszName = gszTarget5;
        gaTargets[6].pszName = gszTarget6;
        gaTargets[7].pszName = gszTarget7;
        for (pt = gaTargets; pt < &gaTargets[8]; pt++)
            SendDlgItemMessage(hDlg, IDC_TARGET, CB_ADDSTRING, 0, (LPARAM)(LPSTR)pt->pszName);
        FillCompressorList(hDlg);
        gCompOptionsDlg = gCompOptions;
        SetDlgOptions(hDlg, &gCompOptionsDlg, TRUE);
        CompressSetSize(hDlg, IDC_SIZESMALL);
        EnableWindow(GetDlgItem(hDlg, IDC_PREVIEW), FALSE);
        EnableWindow(GetDlgItem(hDlg, IDC_DETAILS), TRUE);
        ShowWindow(GetDlgItem(hDlg, IDC_PREVIEW), SW_HIDE);
        break;

    case WM_COMMAND:
        switch (wParam) {
        case IDOK:
            if (gCompOptions.lpState && gCompOptions.lpState != gCompOptionsSaved.lpState &&
                gCompOptionsDlg.lpState != gCompOptions.lpState) {
                GlobalUnlock((HGLOBAL)SELECTOROF(gCompOptions.lpState));
                GlobalFree((HGLOBAL)SELECTOROF(gCompOptions.lpState));
            }
            gCompOptions = gCompOptionsDlg;
            GetDlgOptions(hDlg, &gCompOptions);
            /* fall through */
        case IDCANCEL:
            if (ghicPreview) {
                if (gselPreviewOut)
                    ICCompressEnd(ghicPreview);
                ICClose(ghicPreview);
                ghicPreview = NULL;
                gfccPreview = 0;
            }
            CompressPreviewStart(NULL);
            EndDialog(hDlg, wParam == IDOK);
            break;

        case IDC_REPAINTPREVIEW:
            CompressPreviewPaint(hDlg, FALSE);
            break;

        case IDC_DATARATE:
        case IDC_KEYFRAMES:
        case IDC_INTERLEAVE:
            if (HIWORD(lParam) == EN_CHANGE) {
                w = GetDlgItemInt(hDlg, wParam, &f, FALSE);
                if (!f || w > 10000) {
                    SetDlgItemInt(hDlg, wParam, w > 10000 ? 10000 : 1, FALSE);
                    MessageBeep(0);
                    SendDlgItemMessage(hDlg, wParam, EM_SETSEL, 0, MAKELONG(0, 0x7FFF));
                }
            }
            break;

        case IDC_USEDEFAULT:
            GetCompressDefaults(&gCompOptionsDlg, FALSE);
            SetDlgOptions(hDlg, &gCompOptionsDlg, TRUE);
            break;

        case IDC_SAVEDEFAULT:
            GetDlgOptions(hDlg, &gCompOptionsDlg);
            SaveCompressDefaults(&gCompOptionsDlg);
            break;

        case IDC_COMPRESSOR:
            if (HIWORD(lParam) != CBN_SELCHANGE)
                break;
            i = (int)SendDlgItemMessage(hDlg, IDC_COMPRESSOR, CB_GETITEMDATA,
                    (WPARAM)SendDlgItemMessage(hDlg, IDC_COMPRESSOR, CB_GETCURSEL, 0, 0L), 0L);
            pc = &gaCompressors[i];
            if (pc->fccHandler != gCompOptionsDlg.fccHandler) {
                if (gCompOptionsDlg.lpState && gCompOptionsDlg.lpState != gCompOptions.lpState) {
                    GlobalUnlock((HGLOBAL)SELECTOROF(gCompOptionsDlg.lpState));
                    GlobalFree((HGLOBAL)SELECTOROF(gCompOptionsDlg.lpState));
                }
                gCompOptionsDlg.cbState = 0;
                gCompOptionsDlg.lpState = NULL;
                GetDlgOptions(hDlg, &opts);
                if (pc->fccHandler != gCompOptions.fccHandler) {
                    opts.dwQuality = pc->dwDefQuality;
                    opts.dwKeyFrameEvery = pc->dwDefKeyFrameRate;
                } else {
                    opts.dwQuality = gCompOptions.dwQuality;
                    opts.dwKeyFrameEvery = gCompOptions.dwKeyFrameEvery;
                    gCompOptionsDlg.cbState = gCompOptions.cbState;
                    gCompOptionsDlg.lpState = gCompOptions.lpState;
                }
                SetDlgOptions(hDlg, &opts, FALSE);
            }

            if (gidCompressSize == IDC_SIZESMALL)
                break;

            if (!ghicPreview || pc->fccHandler != gfccPreview) {
                if (ghicPreview) {
                    if (gselPreviewOut)
                        ICCompressEnd(ghicPreview);
                    ICClose(ghicPreview);
                    ghicPreview = NULL;
                    gfccPreview = 0;
                }
                gfccPreview = pc->fccHandler;
                ghicPreview = ICOpen(ICTYPE_VIDEO, gfccPreview, ICMODE_COMPRESS);
            }
            if (ghicPreview) {
                ICGetInfo(ghicPreview, &gicinfo, sizeof(ICINFO));
                fConfig = ICQueryConfigure(ghicPreview);
                fAbout = ICQueryAbout(ghicPreview);
            } else {
                if (gfccPreview == FCC_RLE || gfccPreview == FCC_RLE0)
                    gicinfo.dwFlags = VIDCF_QUALITY | VIDCF_CRUNCH | VIDCF_TEMPORAL;
                else
                    gicinfo.dwFlags = 0;
                fConfig = (pc->lpfnConfigure != NULL);
                fAbout = FALSE;
            }
            EnableWindow(GetDlgItem(hDlg, IDC_CONFIGURE), fConfig);
            EnableWindow(GetDlgItem(hDlg, IDC_ABOUT), fAbout);
            ShowWindow(GetDlgItem(hDlg, IDC_ABOUT), (fAbout && !fConfig) ? SW_SHOW : SW_HIDE);
            ShowWindow(GetDlgItem(hDlg, IDC_CONFIGURE), (fAbout && !fConfig) ? SW_HIDE : SW_SHOW);

            f = ((BYTE)gicinfo.dwFlags & VIDCF_QUALITY) != 0;
            EnableWindow(GetDlgItem(hDlg, IDC_QUALITY), f);
            ShowWindow(GetDlgItem(hDlg, IDC_QUALITYTEXT), f);
            f = ((BYTE)gicinfo.dwFlags & (VIDCF_QUALITY | VIDCF_CRUNCH)) != 0;
            EnableWindow(GetDlgItem(hDlg, IDC_DATARATECHECK), f);
            EnableWindow(GetDlgItem(hDlg, IDC_DATARATE), f);
            EnableWindow(GetDlgItem(hDlg, IDC_DATARATEARROW), f);
            f = ((BYTE)gicinfo.dwFlags & VIDCF_TEMPORAL) != 0;
            EnableWindow(GetDlgItem(hDlg, IDC_KEYFRAMECHECK), f);
            EnableWindow(GetDlgItem(hDlg, IDC_KEYFRAMES), f);
            EnableWindow(GetDlgItem(hDlg, IDC_KEYFRAMEARROW), f);

            if (gidCompressSize == IDC_SIZEPREVIEW)
                CompressPreviewStart(hDlg);
            break;

        case IDC_CONFIGURE:
            if (ghicPreview) {
                if (GetKeyState(VK_CONTROL) < 0) {
                    ICAbout(ghicPreview, hDlg);
                    break;
                }
                ICSetState(ghicPreview, gCompOptionsDlg.lpState, gCompOptionsDlg.cbState);
                if (ICConfigure(ghicPreview, hDlg) != 0) {
                    cb = ICGetState(ghicPreview, NULL, 0);
                    if (cb != gCompOptionsDlg.cbState || !gCompOptionsDlg.lpState ||
                        gCompOptionsDlg.lpState == gCompOptions.lpState) {
                        if (gCompOptionsDlg.lpState && gCompOptionsDlg.lpState != gCompOptions.lpState) {
                            GlobalUnlock((HGLOBAL)SELECTOROF(gCompOptionsDlg.lpState));
                            GlobalFree((HGLOBAL)SELECTOROF(gCompOptionsDlg.lpState));
                        }
                        ghMemTemp = GlobalAlloc(GMEM_MOVEABLE, cb);
                        sel = ghMemTemp ? SELECTOROF(GlobalLock(ghMemTemp)) : 0;
                        gCompOptionsDlg.lpState = MAKELP(sel, 0);
                        gCompOptionsDlg.cbState = cb;
                    }
                    ICGetState(ghicPreview, gCompOptionsDlg.lpState, cb);
                }
            } else {
                i = (int)SendDlgItemMessage(hDlg, IDC_COMPRESSOR, CB_GETITEMDATA,
                        (WPARAM)SendDlgItemMessage(hDlg, IDC_COMPRESSOR, CB_GETCURSEL, 0, 0L), 0L);
                (*(CONFIGUREPROC)gaCompressors[i].lpfnConfigure)(hDlg, &gCompOptionsDlg.lpState,
                                                                 &gCompOptionsDlg.cbState);
            }
            CompressPreviewFrame(hDlg);
            break;

        case IDC_ABOUT:
            if (ghicPreview)
                ICAbout(ghicPreview, hDlg);
            break;

        case IDC_DETAILS:
            CompressSetSize(hDlg, IDC_SIZEDETAILS);
            EnableWindow(GetDlgItem(hDlg, IDC_DETAILS), FALSE);
            if (gwLengthShown) {
                EnableWindow(GetDlgItem(hDlg, IDC_PREVIEW), TRUE);
                ShowWindow(GetDlgItem(hDlg, IDC_PREVIEW), SW_SHOW);
                ShowWindow(GetDlgItem(hDlg, IDC_DETAILS), SW_HIDE);
            }
            PostMessage(hDlg, WM_COMMAND, IDC_COMPRESSOR,
                        MAKELONG(GetDlgItem(hDlg, IDC_COMPRESSOR), CBN_SELCHANGE));
            PostMessage(hDlg, WM_NEXTDLGCTL, (WPARAM)GetDlgItem(hDlg, IDC_TARGET), 1L);
            break;

        case IDC_TARGET:
            if (HIWORD(lParam) != CBN_SELCHANGE)
                break;
            i = (int)SendDlgItemMessage(hDlg, IDC_TARGET, CB_GETCURSEL, 0, 0L);
            if (i == 7 && gidCompressSize == IDC_SIZESMALL) {
                PostMessage(hDlg, WM_COMMAND, IDC_DETAILS, 0L);
                break;
            }
            GetDlgOptions(hDlg, &gCompOptionsDlg);
            gCompOptionsDlg.dwFlags = gaTargets[i].dwFlags;
            gCompOptionsDlg.dwDataRate = gaTargets[i].dwDataRate;
            gCompOptionsDlg.dwInterleave = gaTargets[i].dwInterleave;
            SetDlgOptions(hDlg, &gCompOptionsDlg, FALSE);
            break;

        case IDC_PREVIEW:
            if (!gwLengthShown)
                break;
            CompressSetSize(hDlg, IDC_SIZEPREVIEW);
            EnableWindow(GetDlgItem(hDlg, IDC_PREVIEW), FALSE);
            EnableWindow(GetDlgItem(hDlg, IDC_DETAILS), FALSE);
            ShowWindow(GetDlgItem(hDlg, IDC_DETAILS), SW_HIDE);
            PostMessage(hDlg, WM_COMMAND, IDC_COMPRESSOR,
                        MAKELONG(GetDlgItem(hDlg, IDC_COMPRESSOR), CBN_SELCHANGE));
            SendMessage(hDlg, WM_NEXTDLGCTL, (WPARAM)GetDlgItem(hDlg, IDC_TARGET), 1L);
            break;

        case IDC_RECOMPRESS:
            CompressPreviewFrame(hDlg);
            break;
        }
        break;

    case WM_HSCROLL:
        id = GetWindowWord((HWND)HIWORD(lParam), GWW_ID);
        pos = GetScrollPos((HWND)HIWORD(lParam), SB_CTL);
        switch (wParam) {
        case SB_LINEUP:
            pos--;
            break;
        case SB_LINEDOWN:
            pos++;
            break;
        case SB_PAGEUP:
            pos -= 10;
            break;
        case SB_PAGEDOWN:
            pos += 10;
            break;
        case SB_THUMBPOSITION:
        case SB_THUMBTRACK:
            pos = LOWORD(lParam);
            break;
        case SB_ENDSCROLL:
            if (id == IDC_QUALITY) {
                CompressPreviewFrame(hDlg);
            } else if (id == IDC_FRAMESCROLL) {
                SeekTo(pos);
                CompressPreviewStart(hDlg);
            }
            return 1L;
        default:                        /* SB_TOP, SB_BOTTOM and others */
            return 1L;
        }
        if (id == IDC_QUALITY) {
            if (pos < 0)
                pos = 0;
            if (pos > 100)
                pos = 100;
            SetDlgItemInt(hDlg, IDC_QUALITYTEXT, pos, FALSE);
        } else if (id == IDC_FRAMESCROLL) {
            if (pos < 0)
                pos = 0;
            if ((int)GetFrameCount() < pos)
                pos = GetFrameCount();
            LoadString(ghInst, IDS_FRAMEN, gszCompFormat, sizeof(gszCompFormat));
            wsprintf(ach, gszCompFormat, pos);
            SetDlgItemText(hDlg, IDC_FRAMETEXT, ach);
        }
        SetScrollPos((HWND)HIWORD(lParam), SB_CTL, pos, TRUE);
        break;

    case WM_VSCROLL:
        id = GetWindowWord((HWND)HIWORD(lParam), GWW_ID) + 10;
        ArrowEditStep(GetDlgItem(hDlg, id), wParam, 0L, 10000L, 1);
        break;

    case WM_QUERYNEWPALETTE:
    case WM_PALETTECHANGED:
        if (gselPreviewIn)
            InvalidateRect(hDlg, NULL, FALSE);
        return 0L;

    case WM_TIMER:
        break;

    default:
        return 0L;
    }
    return 1L;
}

/*
 * s12:15F4-160D  SetUSecPerFrame  (FAR PASCAL)
 *
 * New frame rate of the movie, the old one kept for undo.
 */
BOOL FAR PASCAL SetUSecPerFrame(DWORD dwUSecPerFrame)
{
    gdwUSecPerFrameOld = gCompOptions.dwUSecPerFrame;
    gCompOptions.dwUSecPerFrame = dwUSecPerFrame;
    return TRUE;
}

/*
 * s12:160E-162F  SwapUSecPerFrame
 *
 * Undo/redo of SetUSecPerFrame.
 */
BOOL FAR SwapUSecPerFrame(void)
{
    DWORD dw;

    dw = gdwUSecPerFrameOld;
    gdwUSecPerFrameOld = gCompOptions.dwUSecPerFrame;
    gCompOptions.dwUSecPerFrame = dw;
    return TRUE;
}

/*
 * s12:1630-1835  CompressPreviewStart  (FAR _cdecl)
 *
 * Frees the preview; with a dialog (and a movie) gets the current frame
 * as a DIB, makes an output DIB of twice its size, starts the compressor
 * on them and compresses the frame.  Any failure frees everything.
 */
static void FAR _cdecl CompressPreviewStart(HWND hDlg)
{
    char        ach[40];                /* [bp-2C] */
    HCURSOR     hcur;
    LONG        l;                      /* [bp-04] */
    HINSTANCE   hmod;

    if (gselPreviewOut) {
        GlobalUnlock((HGLOBAL)gselPreviewOut);
        GlobalFree((HGLOBAL)gselPreviewOut);
        gselPreviewOut = 0;
    }
    if (gselPreviewIn) {
        GlobalUnlock((HGLOBAL)gselPreviewIn);
        GlobalFree((HGLOBAL)gselPreviewIn);
        gselPreviewIn = 0;
    }
    if (ghCompress1dc8) {
        GlobalUnlock(ghCompress1dc8);
        GlobalFree(ghCompress1dc8);
        ghCompress1dc8 = NULL;
    }
    if (ghddPreview) {
        DrawDibClose(ghddPreview);
        ghddPreview = NULL;
    }
    if (!hDlg || !gwLengthShown)
        return;

    ghddPreview = DrawDibOpen();
    if (!glpfnRleCompress) {
        hmod = GetModuleHandle("MEDDIBS");
        if (hmod)
            glpfnRleCompress = (RLECOMPRESSPROC)GetProcAddress(hmod, "RleCompress");
    }

    gselPreviewIn = GetFullFrame(gwCurFrame, &ghpalPreview, 0x51);
    if (gselPreviewIn == 1)
        gselPreviewIn = 0;
    if (!gselPreviewIn)
        goto fail;
    gselPreviewIn = (WORD)CopyHandle((HANDLE)gselPreviewIn);
    gselPreviewOut = (WORD)CopyHandle((HANDLE)gselPreviewIn);
    if (!gselPreviewIn || !gselPreviewOut)
        goto fail;
    ghMemTemp = GlobalReAlloc((HGLOBAL)gselPreviewOut, GlobalSize((HGLOBAL)gselPreviewOut) * 2, 0);
    gselPreviewOut = ghMemTemp ? SELECTOROF(GlobalLock(ghMemTemp)) : 0;
    if (!gselPreviewOut)
        goto fail;

    if (ghicPreview) {
        if ((LONG)ICCompressGetFormat(ghicPreview, DIBHDR(gselPreviewIn), DIBHDR(gselPreviewOut)) < 0)
            goto fail;
        UpdateWindow(hDlg);
        hcur = SetCursor(LoadCursor(NULL, IDC_WAIT));
        l = ICCompressBegin(ghicPreview, DIBHDR(gselPreviewIn), DIBHDR(gselPreviewOut));
        if (hcur)
            SetCursor(hcur);
        if (l < 0)
            goto fail;
    }

    LoadString(ghInst, IDS_FRAMEN, gszCompFormat, sizeof(gszCompFormat));
    wsprintf(ach, gszCompFormat, gwCurFrame);
    SetDlgItemText(hDlg, IDC_FRAMETEXT, ach);
    SetScrollPos(GetDlgItem(hDlg, IDC_FRAMESCROLL), SB_CTL, gwCurFrame, TRUE);
    CompressPreviewFrame(hDlg);
    return;

fail:
    CompressPreviewStart(NULL);
}

/*
 * s12:1836-18D3  CompressPreviewPaint  (FAR _cdecl)
 *
 * Draws the compressed frame in the white rectangle.
 */
static void FAR _cdecl CompressPreviewPaint(HWND hDlg, BOOL fErase)
{
    RECT    rc;                         /* [bp-0A] */
    HDC     hdc;                        /* [bp-02] */
    HWND    hwnd;

    if (!gselPreviewOut)
        return;
    hwnd = GetDlgItem(hDlg, IDC_PREVIEWRECT);
    hdc = GetDC(hwnd);
    GetClientRect(hwnd, &rc);
    IntersectClipRect(hdc, rc.left, rc.top, rc.right, rc.bottom);
    if (fErase)
        FillRect(hdc, &rc, (HBRUSH)DefWindowProc(hwnd, WM_CTLCOLOR, (WPARAM)hdc,
                                                 MAKELONG(hwnd, CTLCOLOR_STATIC)));
    DrawDibDraw(ghddPreview, hdc, 0, 0, -1, -1, DIBHDR(gselPreviewOut), NULL, 0, 0, -1, -1, 0);
    ReleaseDC(hwnd, hdc);
    ValidateRect(hwnd, NULL);
}

/*
 * s12:18D4-1BBD  CompressPreviewFrame  (FAR _cdecl)
 *
 * Compresses the frame with the quality of the scroll bar and, if the
 * data rate is on, a frame size of data rate x frame time minus the
 * sound of one frame.  With ICM; or for "RLE " through MEDDIBS's
 * RleCompress (which is given 10000 - quality); otherwise the frame is
 * shown as it is.  A frame ICM didn't make a key frame is drawn over a
 * cleared DrawDib buffer.  Then shows the sizes and the ratio.
 *
 * Quirk: the ratio, a DWORD, is passed to wsprintf for "%d".
 */
static void FAR _cdecl CompressPreviewFrame(HWND hDlg)
{
    char        ach[40];                /* [bp-70] */
    BITMAPINFOHEADER bih;               /* [bp-48] */
    DWORD       dwCkid;                 /* [bp-20] */
    DWORD       dwFlags;                /* [bp-1C] */
    DWORD       dw;                     /* [bp-18] data rate, then sound per frame */
    BOOL        f;                      /* [bp-14] */
    LONG        lQuality;               /* [bp-12] */
    LPVOID      lpPrev;                 /* [bp-0E] */
    LPBITMAPINFOHEADER lpbiPrev;        /* [bp-0A] */
    LONG        lFrameSize;             /* [bp-06] */
    DWORD       dwOutSize;              /* [bp-04] */
    DWORD       dwInSize;               /* [bp-08] */
    DWORD       dwRatio;                /* [bp-0C] */
    HCURSOR     hcur;                   /* [bp-02] */
    WORD        wPrev;                  /* [bp-02] too */
    LPVOID      lpBuf;

    if (!gselPreviewOut || !gselPreviewIn)
        return;

    lQuality = (LONG)(GetScrollPos(GetDlgItem(hDlg, IDC_QUALITY), SB_CTL) * 100);
    if (IsDlgButtonChecked(hDlg, IDC_DATARATECHECK)) {
        dw = (DWORD)GetDlgItemInt(hDlg, IDC_DATARATE, &f, FALSE) << 10;
        lFrameSize = muldiv32(dw, gCompOptions.dwUSecPerFrame, 1000000L);
        WaveFrameBytes(0, &dw);
        lFrameSize -= dw;
    } else {
        lFrameSize = 0;
    }

    wPrev = 1;
    lpbiPrev = NULL;
    lpPrev = NULL;
    if (ghicPreview) {
        dwFlags = 0;
        UpdateWindow(hDlg);
        hcur = SetCursor(LoadCursor(NULL, IDC_WAIT));
        dw = ICCompress(ghicPreview, 0L,
                        DIBHDR(gselPreviewOut), DIBBITS(gselPreviewOut),
                        DIBHDR(gselPreviewIn), DIBBITS(gselPreviewIn),
                        &dwCkid, &dwFlags, (LONG)gwCurFrame, lFrameSize, lQuality,
                        lpbiPrev, lpPrev);
        if (hcur)
            SetCursor(hcur);
        if (!(dwFlags & AVIIF_KEYFRAME)) {
            lpBuf = DrawDibGetBuffer(ghddPreview, &bih, sizeof(bih), 0L);
            if (lpBuf)
                hmemset(lpBuf, 0, bih.biSizeImage);
        }
    } else if (glpfnRleCompress && (gfccPreview == FCC_RLE || gfccPreview == FCC_RLE0)) {
        wPrev = 0;
        (*glpfnRleCompress)(0, gselPreviewOut, wPrev, gselPreviewIn, ghpalPreview,
                            lFrameSize, 10000L - lQuality);
        DIBHDR(gselPreviewOut)->biCompression = BI_RLE8;
    }

    dwOutSize = DIBHDR(gselPreviewOut)->biSizeImage;
    dwInSize = DIBHDR(gselPreviewIn)->biSizeImage;
    dwRatio = dwOutSize * 100 / dwInSize;

    LoadString(ghInst, IDS_FULLSIZE, gszCompFormat, sizeof(gszCompFormat));
    wsprintf(ach, gszCompFormat, dwInSize);
    SetDlgItemText(hDlg, IDC_FULLSIZETEXT, ach);
    LoadString(ghInst, IDS_COMPRESSEDSIZE, gszCompFormat, sizeof(gszCompFormat));
    wsprintf(ach, gszCompFormat, dwOutSize);
    SetDlgItemText(hDlg, IDC_COMPRESSEDTEXT, ach);
    LoadString(ghInst, IDS_RATIO, gszCompFormat, sizeof(gszCompFormat));
    wsprintf(ach, gszCompFormat, dwRatio);
    SetDlgItemText(hDlg, IDC_RATIOTEXT, ach);
    UpdateWindow(hDlg);
    CompressPreviewPaint(hDlg, FALSE);
}
