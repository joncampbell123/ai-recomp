/*
 * filedlg.c - code segment 11 of VIDEDIT.EXE, s11:0000-0A21: the
 * File/Insert, File/Extract and File/Save As file dialogs.
 *
 * The common dialogs' file type lists are built from the Workbench
 * tools (MediaMan handlers) that can load (Insert) or save (Extract,
 * Save As) the wanted media types: each tool gives a title and an
 * extension string; the chosen type is mapped back to its tool through
 * a Workbench tool array.  Extract and Save As use templates with hooks
 * (EXTRACTFILE shows the frames that will be extracted, AVISAVEAS the
 * compression settings and has "Save Copy"), and the hooks put the new
 * type's extension on the file name when the type changes.
 *
 * Data: DGROUP module at DS:0692-06FF (initialised) and DS:1CE2-1D2D.
 * The rest of the segment is savefile.c.  Functions are in the order of
 * the binary.
 */

#include "videdit.h"
#include <stdio.h>
#include "compress.h"
#include "filedlg.h"

/* common dialog controls */
#define IDC_FILETYPE            0x0470  /* cmb1 */
#define IDC_FILENAME            0x0480  /* edt1 */

/* EXTRACTFILE */
#define IDC_EXTRACTSTART        223
#define IDC_EXTRACTEND          224
#define IDC_SETSELECTION        226

/* AVISAVEAS */
#define IDC_COMPRESSIONOPTIONS  152
#define IDC_SAVECOPY            155
#define IDC_COMPRESSORNAME      156
#define IDC_DATARATEVALUE       157
#define IDC_STREAMING           159

/* string table */
#define IDS_CAPTION             126     /* "VidEdit" */
#define IDS_NOMEMORY            299     /* "Not enough memory to complete operation." */
#define IDS_STREAMAUDIO         450     /* "Stream audio" */
#define IDS_NONSTREAMINGAUDIO   451     /* "Non-streaming audio" */

#define ERR_CANCELLED           0x020C  /* medSetExtError when the dialog is cancelled */

#define MEDTYPE_DIBS            medFOURCC('D', 'I', 'B', 'S')
#define MEDTYPE_AVI             medFOURCC('A', 'V', 'I', ' ')

/* initialised data, DS:0692-06FF (after the module's "AnimSTrackBar",
   "GrayBlob" header strings) */
WORD        gwFiledlg06a8   = 0;        /* DS:06A8 not used */
WORD        gwInsertFilter  = 0;        /* DS:06AA file type last chosen for Insert */
/* DS:06B7 "avifileInsert: type is '%.4s'\n": a debug message whose string
   was kept although nothing refers to it */
WORD        gfExtractSetting = 0;       /* DS:06D6 set while the hook fills in the range */
WORD        gwExtractFilter = 0;        /* DS:06D8 file type last chosen for Extract */
WORD        gwFiledlg06e6   = 1;        /* DS:06E6 not used */
WORD        gwFiledlg06e8   = 1;        /* DS:06E8 not used */
WORD        gwFiledlg06ea   = 0;        /* DS:06EA not used */
WORD        gfSaveCopy      = 0;        /* DS:06EC "Save Copy" checked */

/* uninitialised data */
OPENFILENAME gofn;                      /* DS:1CE2 */
WORD        gwExtractStart;             /* DS:1D2A */
WORD        gwExtractEnd;               /* DS:1D2C */

/*
 * s11:0000-0241  InsertFileDialog  (FAR PASCAL)
 *
 * File/Insert: lets the user pick a file of one of the media types in
 * pTypes (a 0-terminated list) and opens it with WrkOpenFileName.
 * Returns WrkOpenFileName's result (1 = OK), or 2 when cancelled (with
 * MediaMan's extended error 0x20C) or out of memory.
 *
 * Quirks: the template name is set but OFN_ENABLETEMPLATE is not, so
 * the standard dialog is shown; if no tool matches the chosen type, an
 * uninitialised media type is passed on.
 */
WORD FAR PASCAL InsertFileDialog(FPMedReturn lpmr, MEDTYPE *pTypes, WORD wFlags, HWND hwnd,
                                 LPSTR lpszTitle)
{
    char        achFile[256];           /* [bp-124] */
    WRKTOOLINFO info;                   /* [bp-24] */
    MEDTYPE    *pt;
    HGLOBAL     h;
    LPSTR       lpFilter;               /* [bp-08] */
    LPSTR       lpEnd;                  /* [bp-04] */
    WORD        hArray;                 /* [bp-10] */
    WORD        wTool;
    BOOL        f;

    lpmr->medid = 0;
    achFile[0] = '\0';
    h = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT | GMEM_DDESHARE, 0x400);
    if (!h)
        goto nomem;
    lpEnd = lpFilter = GlobalLock(h);
    hArray = WrkCreateToolArray(5);
    if (!hArray) {
        GlobalFree(h);
        goto nomem;
    }

    for (pt = pTypes; *pt; pt++) {
        for (wTool = 0; (wTool = WrkIterTools(wTool, 1)) != 0; ) {
            info.wVersion = 0x0100;
            WrkGetToolInfo(wTool, &info);
            if (info.medtypeLogical != *pt || !(info.wFlags & WTF_LOAD))
                continue;
            WrkAddToToolArray(hArray, wTool, 1);
            WrkGetToolTitle(wTool, lpEnd, 0x400 - (OFFSETOF(lpEnd) - OFFSETOF(lpFilter)));
            while (*lpEnd++)
                ;
            WrkGetExtString(info.wExtString, lpEnd, 0x400 - (OFFSETOF(lpEnd) - OFFSETOF(lpFilter)));
            while (*lpEnd++)
                ;
        }
    }

    gofn.lStructSize = sizeof(OPENFILENAME);
    *lpEnd = '\0';
    gofn.hwndOwner = hwnd;
    gofn.hInstance = ghInst;
    gofn.lpstrFilter = lpFilter;
    gofn.nFilterIndex = gwInsertFilter;
    gofn.lpstrCustomFilter = NULL;
    gofn.nMaxCustFilter = 0;
    gofn.lpstrFile = achFile;
    gofn.nMaxFile = sizeof(achFile);
    gofn.lpstrFileTitle = NULL;
    gofn.nMaxFileTitle = 0;
    gofn.lpstrInitialDir = NULL;
    gofn.lpstrTitle = lpszTitle;
    gofn.Flags = OFN_SHAREAWARE | OFN_FILEMUSTEXIST | OFN_HIDEREADONLY;
    gofn.lpstrDefExt = NULL;
    gofn.lCustData = 0;
    gofn.lpfnHook = NULL;
    gofn.lpTemplateName = "INSERTFILE";
    f = GetOpenFileName(&gofn);
    wFlags &= ~0x1000;
    gwInsertFilter = (WORD)gofn.nFilterIndex;
    GlobalUnlock(h);
    GlobalFree(h);

    wTool = WrkGetToolArrayEntry(hArray, (WORD)gofn.nFilterIndex - 1);
    if (wTool) {
        info.wVersion = 0x0100;
        WrkGetToolInfo(wTool, &info);
    }
    WrkDestroyToolArray(hArray);
    if (!f) {
        medSetExtError(ERR_CANCELLED, ghInst);
        return 2;
    }
    return WrkOpenFileName(achFile, lpmr, info.medtypeLogical, wFlags, hwnd, lpszTitle);

nomem:
    MessageBeep(MB_ICONEXCLAMATION);
    ErrorResBox(hwnd, ghInst, MB_ICONEXCLAMATION, IDS_CAPTION, IDS_NOMEMORY);
    return 2;
}

/*
 * s11:0242-03F9  ExtractHookProc  (FAR PASCAL, loads DS from SS, not
 * exported)
 *
 * Hook of the Extract dialog: shows the selection (as times if
 * positions are shown as times), "Set Selection..." changes it, and a
 * new file type puts its extension on the file name.
 */
BOOL FAR PASCAL _loadds ExtractHookProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
    char    achFile[144];               /* [bp-17A] */
    char    achDir[130];                /* [bp-EA] */
    char    achEnd[40];                 /* [bp-68] */
    char    achStart[40];               /* [bp-40] */
    char    achName[10];                /* [bp-18] */
    char    achExt[6];                  /* [bp-0E] */
    char    achDrive[6];                /* [bp-08] */
    LPSTR   lpSpec;
    int     i;

    switch (msg) {
    case WM_INITDIALOG:
        gfExtractSetting = 1;
        GetSelection(&gwExtractStart, &gwExtractEnd, 1);
    show:
        if (gfTimeFormat) {
            FormatFrameTime(gwExtractStart, achStart);
            FormatFrameTime(gwExtractEnd, achEnd);
            SetDlgItemText(hDlg, IDC_EXTRACTSTART, achStart);
            SetDlgItemText(hDlg, IDC_EXTRACTEND, achEnd);
        } else {
            SetDlgItemInt(hDlg, IDC_EXTRACTSTART, gwExtractStart, FALSE);
            SetDlgItemInt(hDlg, IDC_EXTRACTEND, gwExtractEnd, FALSE);
        }
        gfExtractSetting = 0;
        return TRUE;

    case WM_COMMAND:
        switch (wParam) {
        case IDOK:
        case IDCANCEL:
            break;

        case IDC_SETSELECTION:
            SetSelectionDialog(hDlg);
            gfExtractSetting = 1;
            GetSelection(&gwExtractStart, &gwExtractEnd, 1);
            goto show;

        case IDC_FILETYPE:
            if (HIWORD(lParam) != CBN_SELCHANGE)
                break;
            i = (int)SendDlgItemMessage(hDlg, IDC_FILETYPE, CB_GETCURSEL, 0, 0L);
            if (i < 0)
                i = 0;
            lpSpec = (LPSTR)gofn.lpstrFilter +
                     (WORD)SendDlgItemMessage(hDlg, IDC_FILETYPE, CB_GETITEMDATA, i, 0L);
            GetDlgItemText(hDlg, IDC_FILENAME, achFile, sizeof(achFile));
            if (achFile[0] == '*') {
                SetDlgItemText(hDlg, IDC_FILENAME, lpSpec);
            } else {
                SplitPath(achFile, achDrive, achDir, achName, achExt);
                MakePath(achFile, achDrive, achDir, achName, NULL);
                AddExtension(achFile, lpSpec, sizeof(achFile));
                SetDlgItemText(hDlg, IDC_FILENAME, achFile);
            }
            break;
        }
        break;
    }
    return FALSE;
}

/*
 * s11:03FA-065D  ExtractFileDialog  (FAR PASCAL)
 *
 * File/Extract: asks for a file of a type that can be saved from one of
 * the media types in pTypes.  On OK returns 1 with the file name, the
 * chosen tool's logical and physical types and the selection; returns 2
 * when cancelled (extended error 0x20C) or out of memory.  wFlags is not
 * used.
 */
WORD FAR PASCAL ExtractFileDialog(MEDTYPE *pTypes, PSTR pszFile, int cbFile, MEDTYPE *pmtLogical,
                                  MEDTYPE *pmtPhysical, WORD *pwStart, WORD *pwEnd, WORD wFlags,
                                  HWND hwnd, LPSTR lpszTitle)
{
    WRKTOOLINFO info;                   /* [bp-24] */
    MEDTYPE    *pt;
    HGLOBAL     h;                      /* [bp-12] */
    LPSTR       lpFilter;               /* [bp-08] */
    LPSTR       lpEnd;                  /* [bp-04] */
    WORD        hArray;                 /* [bp-10] */
    WORD        wTool;
    BOOL        f;

    *pszFile = '\0';
    h = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT | GMEM_DDESHARE, 0x400);
    if (!h)
        goto nomem;
    lpEnd = lpFilter = GlobalLock(h);
    hArray = WrkCreateToolArray(5);
    if (!hArray) {
        GlobalFree(h);
        goto nomem;
    }

    for (pt = pTypes; *pt; pt++) {
        for (wTool = 0; (wTool = WrkIterTools(wTool, 1)) != 0; ) {
            info.wVersion = 0x0100;
            WrkGetToolInfo(wTool, &info);
            if (info.medtypeLogical != *pt || !(info.wFlags & WTF_SAVE))
                continue;
            WrkAddToToolArray(hArray, wTool, 1);
            WrkGetToolTitle(wTool, lpEnd, 0x400 - (OFFSETOF(lpEnd) - OFFSETOF(lpFilter)));
            while (*lpEnd++)
                ;
            WrkGetExtString(info.wExtString, lpEnd, 0x400 - (OFFSETOF(lpEnd) - OFFSETOF(lpFilter)));
            while (*lpEnd++)
                ;
        }
    }

    gofn.lStructSize = sizeof(OPENFILENAME);
    *lpEnd = '\0';
    gofn.hwndOwner = hwnd;
    gofn.lpstrFilter = lpFilter;
    gofn.nFilterIndex = gwExtractFilter;
    gofn.lpstrCustomFilter = NULL;
    gofn.nMaxCustFilter = 0;
    gofn.lpstrFile = pszFile;
    gofn.nMaxFile = (long)cbFile;
    gofn.lpstrFileTitle = NULL;
    gofn.nMaxFileTitle = 0;
    gofn.lpstrInitialDir = NULL;
    gofn.lpstrTitle = lpszTitle;
    gofn.Flags = OFN_NOREADONLYRETURN | OFN_PATHMUSTEXIST | OFN_ENABLETEMPLATE |
                 OFN_ENABLEHOOK | OFN_HIDEREADONLY | OFN_OVERWRITEPROMPT;
    gofn.lpstrDefExt = NULL;
    gofn.lCustData = 0;
    gofn.hInstance = ghInst;
    gofn.lpfnHook = (LPOFNHOOKPROC)MakeProcInstance((FARPROC)ExtractHookProc, ghInst);
    gofn.lpTemplateName = "EXTRACTFILE";
    f = GetSaveFileName(&gofn);
    FreeProcInstance((FARPROC)gofn.lpfnHook);
    gwExtractFilter = (WORD)gofn.nFilterIndex;
    GlobalUnlock(h);
    GlobalFree(h);

    wTool = WrkGetToolArrayEntry(hArray, (WORD)gofn.nFilterIndex - 1);
    if (wTool) {
        info.wVersion = 0x0100;
        WrkGetToolInfo(wTool, &info);
        *pmtLogical = info.medtypeLogical;
        *pmtPhysical = info.medtypePhysical;
    }
    WrkDestroyToolArray(hArray);
    if (!f) {
        medSetExtError(ERR_CANCELLED, ghInst);
        return 2;
    }
    *pwStart = gwExtractStart;
    *pwEnd = gwExtractEnd;
    return 1;

nomem:
    MessageBeep(MB_ICONEXCLAMATION);
    ErrorResBox(hwnd, ghInst, MB_ICONEXCLAMATION, IDS_CAPTION, IDS_NOMEMORY);
    return 2;
}

/*
 * s11:065E-06EF  SaveAsShowOptions  (NEAR PASCAL)
 *
 * The compression settings in the Save As dialog.
 */
static void NEAR PASCAL SaveAsShowOptions(HWND hDlg)
{
    char ach[128];                      /* [bp-80] */

    SetWindowText(GetDlgItem(hDlg, IDC_COMPRESSORNAME), GetCompressorName(TRUE));
    sprintf(ach, "%d", (UINT)(gCompOptions.dwDataRate >> 10));
    SetWindowText(GetDlgItem(hDlg, IDC_DATARATEVALUE), ach);
    LoadString(ghInst, (gCompOptions.dwFlags & COMPF_INTERLEAVE) ? IDS_STREAMAUDIO : IDS_NONSTREAMINGAUDIO,
               ach, sizeof(ach));
    SetWindowText(GetDlgItem(hDlg, IDC_STREAMING), ach);
}

/*
 * s11:06F0-0833  SaveAsHookProc  (FAR PASCAL, loads DS from SS, not
 * exported)
 */
BOOL FAR PASCAL _loadds SaveAsHookProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
    char    achFile[144];               /* [bp-12A] */
    char    achDir[140];                /* [bp-9A] */
    char    achName[10];                /* [bp-18] */
    char    achExt[6];                  /* [bp-0E] */
    char    achDrive[6];                /* [bp-08] */
    LPSTR   lpSpec;
    int     i;

    switch (msg) {
    case WM_INITDIALOG:
        gfSaveCopy = 0;
        CheckDlgButton(hDlg, IDC_SAVECOPY, 0);
        SaveAsShowOptions(hDlg);
        return TRUE;

    case WM_COMMAND:
        switch (wParam) {
        case IDOK:
            gfSaveCopy = IsDlgButtonChecked(hDlg, IDC_SAVECOPY);
            break;

        case IDCANCEL:
            break;

        case IDC_COMPRESSIONOPTIONS:
            DoCompressionOptions(hDlg);
            SaveAsShowOptions(hDlg);
            return TRUE;

        case IDC_FILETYPE:
            if (HIWORD(lParam) != CBN_SELCHANGE)
                break;
            i = (int)SendDlgItemMessage(hDlg, IDC_FILETYPE, CB_GETCURSEL, 0, 0L);
            if (i < 0)
                i = 0;
            lpSpec = (LPSTR)gofn.lpstrFilter +
                     (WORD)SendDlgItemMessage(hDlg, IDC_FILETYPE, CB_GETITEMDATA, i, 0L);
            GetDlgItemText(hDlg, IDC_FILENAME, achFile, sizeof(achFile));
            if (achFile[0] == '*') {
                SetDlgItemText(hDlg, IDC_FILENAME, lpSpec);
            } else {
                SplitPath(achFile, achDrive, achDir, achName, achExt);
                MakePath(achFile, achDrive, achDir, achName, NULL);
                AddExtension(achFile, lpSpec, sizeof(achFile));
                SetDlgItemText(hDlg, IDC_FILENAME, achFile);
            }
            break;
        }
        break;
    }
    return FALSE;
}

/*
 * s11:0834-0A21  SaveAsDialog  (FAR PASCAL)
 *
 * File/Save As: the file types of the tools that can save the DIBS
 * (sequence of DIBs) type.  Returns 1 with the name, *pmt = 'AVI ' and
 * "Save Copy"; 2 when cancelled (extended error 0x20C) or out of
 * memory.  wFlags is not used.
 */
WORD FAR PASCAL SaveAsDialog(PSTR pszFile, int cbFile, MEDTYPE *pmt, BOOL *pfSaveCopy,
                             WORD wFlags, HWND hwnd, LPSTR lpszTitle)
{
    WRKTOOLINFO info;                   /* [bp-20] */
    HGLOBAL     h;                      /* [bp-0C] */
    LPSTR       lpFilter;               /* [bp-08] */
    LPSTR       lpEnd;
    WORD        hArray;                 /* [bp-0A] */
    WORD        wTool;
    BOOL        f;

    h = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT | GMEM_DDESHARE, 0x400);
    if (!h)
        goto nomem;
    lpEnd = lpFilter = GlobalLock(h);
    hArray = WrkCreateToolArray(5);
    if (!hArray) {
        GlobalFree(h);
        goto nomem;
    }

    for (wTool = 0; (wTool = WrkIterTools(wTool, 1)) != 0; ) {
        info.wVersion = 0x0100;
        WrkGetToolInfo(wTool, &info);
        if (info.medtypeLogical != MEDTYPE_DIBS || !(info.wFlags & WTF_SAVE))
            continue;
        WrkAddToToolArray(hArray, wTool, 1);
        WrkGetToolTitle(wTool, lpEnd, 0x400 - (OFFSETOF(lpEnd) - OFFSETOF(lpFilter)));
        while (*lpEnd++)
            ;
        WrkGetExtString(info.wExtString, lpEnd, 0x400 - (OFFSETOF(lpEnd) - OFFSETOF(lpFilter)));
        while (*lpEnd++)
            ;
    }

    gofn.lStructSize = sizeof(OPENFILENAME);
    *lpEnd = '\0';
    gofn.hwndOwner = hwnd;
    gofn.lpstrFilter = lpFilter;
    gofn.nFilterIndex = 0;
    gofn.lpstrCustomFilter = NULL;
    gofn.nMaxCustFilter = 0;
    gofn.lpstrFile = pszFile;
    gofn.nMaxFile = (long)cbFile;
    gofn.lpstrFileTitle = NULL;
    gofn.nMaxFileTitle = 0;
    gofn.lpstrInitialDir = NULL;
    gofn.lpstrTitle = lpszTitle;
    gofn.Flags = OFN_NOREADONLYRETURN | OFN_SHAREAWARE | OFN_PATHMUSTEXIST | OFN_ENABLETEMPLATE |
                 OFN_ENABLEHOOK | OFN_HIDEREADONLY | OFN_OVERWRITEPROMPT;
    gofn.lpstrDefExt = "avi";
    gofn.lCustData = 0;
    gofn.hInstance = ghInst;
    gofn.lpfnHook = (LPOFNHOOKPROC)MakeProcInstance((FARPROC)SaveAsHookProc, ghInst);
    gofn.lpTemplateName = "AVISAVEAS";
    f = GetSaveFileName(&gofn);
    FreeProcInstance((FARPROC)gofn.lpfnHook);
    GlobalUnlock(h);
    GlobalFree(h);
    *pmt = MEDTYPE_AVI;
    WrkDestroyToolArray(hArray);
    if (!f) {
        medSetExtError(ERR_CANCELLED, ghInst);
        return 2;
    }
    *pfSaveCopy = gfSaveCopy;
    return 1;

nomem:
    MessageBeep(MB_ICONEXCLAMATION);
    ErrorResBox(hwnd, ghInst, MB_ICONEXCLAMATION, IDS_CAPTION, IDS_NOMEMORY);
    return 2;
}
