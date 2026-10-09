/*
 * file.c - code segment 1 of VIDCAP.EXE, 3E6A-5279: files - saving a
 * frame as a DIB, the common file dialogs, setting up the capture file
 * (with its size), Save Captured Video As, loading and saving palettes,
 * and the hook for the Set Capture File dialog.
 *
 * The file type filters are RCDATA resources 301-304.
 */

#include <windows.h>
#include <mmsystem.h>
#include <commdlg.h>
#include <dlgs.h>
#include "vidcap.h"

#define IDT_SETFILE     800

BOOL        gfCaptureFileHasData = FALSE;               /* DS:04FC an AVI file */
static char gszSetFileTemplate[] = "SETFILE";           /* DS:04FE */
static char gszFileIni[]         = "mmtools.ini";       /* DS:0506 */
static char gszKeyCapFile[]      = "capfile";           /* DS:0512 */
static char gszTitleSep[]        = " - ";               /* DS:051A */
static char gszVidEdit[]         = "VIDEdit -n ";       /* DS:051E */
static char gszClock[]           = "clock$";            /* DS:052A */
static char gszPrn[]             = "prn";               /* DS:0531 */
static char gszCon[]             = "con";               /* DS:0535 */
static char gszAux[]             = "aux";               /* DS:0539 */
static char gszNul[]             = "nul";               /* DS:053D */
static char gszLpt[]             = "lpt";               /* DS:0541 */
static char gszCom[]             = "com";               /* DS:0545 */

static char gachFileName[144];          /* DS:1466 */
static UINT gidSetFileTimer;            /* DS:14F6 */
BOOL        gfCapFileExisted;           /* DS:15EC */
DWORD       gdwCapFileSize;             /* DS:2F3E */

/*
 * seg1:3E6A-3F8E  WriteDIBFile  (FAR PASCAL)
 *
 * Writes the last frame to hmmio as a .BMP file.
 */
BOOL FAR PASCAL WriteDIBFile(HMMIO hmmio)
{
    BITMAPFILEHEADER    bf;
    LPBITMAPINFOHEADER  lpbi;
    HANDLE              h;
    DWORD               cbColors;
    DWORD               cb;
    BOOL                f;

    f = FALSE;
    if (glpFrameBits == NULL || glpbiCapture == NULL)
        return FALSE;

    h = FrameToDIB();
    lpbi = (LPBITMAPINFOHEADER)GlobalLock(h);
    if (lpbi) {
        bf.bfType = 0x4D42;     /* "BM" */
        cbColors = (lpbi->biBitCount > 8) ? 0L : lpbi->biClrUsed * 4;
        bf.bfSize = lpbi->biSizeImage + lpbi->biSize + cbColors + sizeof(BITMAPFILEHEADER);
        bf.bfReserved1 = 0;
        bf.bfReserved2 = 0;
        bf.bfOffBits = bf.bfSize - lpbi->biSizeImage;
        cb = bf.bfSize - sizeof(BITMAPFILEHEADER);
        if (mmioWrite(hmmio, (HPSTR)&bf, (LONG)sizeof(BITMAPFILEHEADER)) == sizeof(BITMAPFILEHEADER) &&
            mmioWrite(hmmio, (HPSTR)lpbi, cb) == (LONG)cb)
            f = TRUE;
    }

    if (lpbi)
        GlobalUnlock(h);
    if (h)
        GlobalFree(h);
    return f;
}

/*
 * seg1:3F92-4064  GetFileName  (NEAR PASCAL)
 *
 * GetOpenFileName or GetSaveFileName, with idHelp as the help context.
 */
static BOOL NEAR PASCAL GetFileName(LPSTR lpszFile, DWORD cbFile, LPCSTR lpszFilter, LPCSTR lpszTitle,
                                    DWORD dwFlags, int idHelp, BOOL fOpen)
{
    OPENFILENAME    ofn;
    BOOL            f;

    _fmemset(&ofn, 0, sizeof(ofn));
    ofn.lStructSize     = sizeof(ofn);
    ofn.hwndOwner       = ghwndApp;
    ofn.hInstance       = ghInst;
    ofn.lpstrFilter     = lpszFilter;
    ofn.lpstrCustomFilter = NULL;
    ofn.nMaxCustFilter  = 0;
    ofn.nFilterIndex    = 1;
    ofn.lpstrFile       = lpszFile;
    ofn.nMaxFile        = cbFile;
    ofn.lpstrFileTitle  = NULL;
    ofn.nMaxFileTitle   = 0;
    ofn.lpstrInitialDir = NULL;
    ofn.lpstrTitle      = lpszTitle;
    ofn.Flags           = dwFlags;
    ofn.lpstrDefExt     = NULL;
    ofn.lCustData       = 0;
    ofn.lpfnHook        = NULL;
    ofn.lpTemplateName  = NULL;

    gidHelpContext = idHelp;
    if (fOpen)
        f = GetOpenFileName(&ofn);
    else
        f = GetSaveFileName(&ofn);
    gidHelpContext = 0;
    return f;
}

/*
 * seg1:4068-41FC  AllocCaptureFile  (FAR PASCAL)
 *
 * Set File Size: makes lpszFile the size asked for in the dialog (in
 * MB) by writing its last byte.  Cancel deletes a file that did not
 * exist before (and clears the capture file name).
 *
 * Quirk of the original: if the file cannot be created again, the wait
 * cursor is not restored.
 */
BOOL FAR PASCAL AllocCaptureFile(HWND hwnd, LPSTR lpszFile)
{
    HMMIO   hmmio;
    HMMIO   hmmioRO;
    DWORD   dwSize;
    int     nMB;

    gfCapFileExisted = TRUE;
    hmmio = mmioOpen(lpszFile, NULL, MMIO_WRITE);
    if (hmmio == NULL) {
        gfCapFileExisted = FALSE;
        hmmio = mmioOpen(lpszFile, NULL, MMIO_CREATE | MMIO_WRITE);
        if (hmmio == NULL) {
            hmmioRO = mmioOpen(lpszFile, NULL, MMIO_READ);
            if (hmmioRO) {
                MsgBoxID(ghwndApp, ghInst, MB_ICONEXCLAMATION, IDS_APPNAME2, IDS_ERR_READONLY, lpszFile);
                mmioClose(hmmioRO, 0);
            } else {
                MsgBoxID(ghwndApp, ghInst, MB_ICONEXCLAMATION, IDS_APPNAME2, IDS_ERR_CANTOPEN, lpszFile);
            }
            return FALSE;
        }
    }

    gdwCapFileSize = mmioSeek(hmmio, 0L, SEEK_END);

    nMB = DoDialogParam(IDD_FILESIZE, hwnd, (FARPROC)FileSizeDlgProc, (LPARAM)lpszFile);
    if (nMB == 0) {
        mmioClose(hmmio, 0);
        if (!gfCapFileExisted) {
            mmioOpen(lpszFile, NULL, MMIO_DELETE);
            gachCaptureFile[0] = '\0';
        }
        return FALSE;
    }

    dwSize = (DWORD)nMB << 20;
    ghcurOld = SetCursor(ghcurWait);
    mmioClose(hmmio, 0);
    mmioOpen(lpszFile, NULL, MMIO_DELETE);
    hmmio = mmioOpen(lpszFile, NULL, MMIO_CREATE | MMIO_WRITE);
    if (hmmio == NULL)
        return FALSE;
    mmioSeek(hmmio, dwSize - 1, SEEK_SET);
    mmioWrite(hmmio, (HPSTR)&nMB, 1L);
    mmioClose(hmmio, 0);
    SetCursor(ghcurOld);
    return TRUE;
}

/*
 * seg1:4200-4492  SetCaptureFile
 *
 * File/Set Capture File.  A new file is set up with AllocCaptureFile; an
 * existing one is checked for being an AVI file (which enables Edit).
 * The name is saved as [VidCap] capfile in MMTOOLS.INI and shown in the
 * title.  Returns TRUE if the capture file was changed.
 */
BOOL FAR SetCaptureFile(void)
{
    char            achOld[144];
    char            achTitle[80];
    char            achCaption[256];
    OPENFILENAME    ofn;
    OFSTRUCT        of;
    MMCKINFO        ck;
    HGLOBAL         hres;
    HMMIO           hmmio;
    BOOL            f;

    _fmemset(&ofn, 0, sizeof(ofn));
    lstrcpy(achOld, gachCaptureFile);

    hres = LoadResource(ghInst, FindResource(ghInst, MAKEINTRESOURCE(IDR_FILTER_AVI), RT_RCDATA));
    if (hres == NULL)
        ofn.lpstrFilter = NULL;
    else
        ofn.lpstrFilter = LockResource(hres);

    lstrcpy(gachFileName, gachCaptureFile);
    achTitle[0] = '\0';
    LoadString(ghInst, IDS_SETCAPFILE_TITLE, achTitle, sizeof(achTitle));

    ofn.lpstrTitle      = achTitle;
    ofn.lStructSize     = sizeof(ofn);
    ofn.hwndOwner       = ghwndApp;
    ofn.lpstrCustomFilter = NULL;
    ofn.nMaxCustFilter  = 0;
    ofn.nFilterIndex    = 1;
    ofn.lpstrFile       = gachFileName;
    ofn.nMaxFile        = sizeof(gachFileName);
    ofn.lpstrFileTitle  = NULL;
    ofn.nMaxFileTitle   = 0;
    ofn.lpstrInitialDir = NULL;
    ofn.Flags           = OFN_HIDEREADONLY | OFN_ENABLEHOOK | OFN_ENABLETEMPLATE |
                          OFN_PATHMUSTEXIST | OFN_NOREADONLYRETURN;
    ofn.lpstrDefExt     = NULL;
    ofn.lCustData       = 0;
    ofn.hInstance       = ghInst;
    ofn.lpfnHook        = (UINT (CALLBACK *)(HWND, UINT, WPARAM, LPARAM))
                          MakeProcInstance((FARPROC)SetFileDlgHook, ghInst);
    ofn.lpTemplateName  = gszSetFileTemplate;

    gidHelpContext = IDH_SETCAPFILE;
    f = !GetOpenFileName(&ofn);
    gidHelpContext = 0;
    FreeProcInstance((FARPROC)ofn.lpfnHook);
    if (hres)
        GlobalUnlock(hres);

    if (f) {
        f = FALSE;
        goto title;
    }

    AnsiUpper(gachFileName);
    if (OpenFile(gachFileName, &of, OF_EXIST) == HFILE_ERROR) {
        /* a new file: set its size */
        lstrcpy(gachCaptureFile, gachFileName);
        if (!AllocCaptureFile(ghwndApp, gachCaptureFile))
            goto restore;
        gfCaptureFileHasData = FALSE;
    } else {
        hmmio = mmioOpen(gachFileName, NULL, MMIO_READ);
        if (hmmio == NULL)
            goto restore;
        if (mmioDescend(hmmio, &ck, NULL, 0) == 0 &&
            ck.ckid == FOURCC_RIFF && ck.fccType == formtypeAVI)
            gfCaptureFileHasData = TRUE;
        else
            gfCaptureFileHasData = FALSE;
        mmioClose(hmmio, 0);
    }

    toolbarModifyState(ghwndToolbar, BTN_EDIT, gfCaptureFileHasData ? BTNST_UP : BTNST_GRAYED);
    lstrcpy(gachCaptureFile, gachFileName);
    WritePrivateProfileString(gszAppName, gszKeyCapFile, gachCaptureFile, gszFileIni);
    f = TRUE;
    goto title;

restore:
    lstrcpy(gachCaptureFile, achOld);
    f = FALSE;

title:
    lstrcpy(achCaption, gszAppName);
    if (gachCaptureFile[0]) {
        lstrcat(achCaption, gszTitleSep);
        lstrcat(achCaption, gachCaptureFile);
    }
    SetWindowText(ghwndApp, achCaption);
    return f;
}

/*
 * seg1:4494-450D  EditCapture
 *
 * Runs "VIDEdit -n <capture file>".
 */
BOOL FAR EditCapture(void)
{
    char    ach[256];
    BOOL    f;

    f = TRUE;
    lstrcpy(ach, gszVidEdit);
    lstrcat(ach, gachCaptureFile);
    ghcurOld = SetCursor(ghcurWait);
    if (WinExec(ach, SW_SHOWNORMAL) < 32) {
        MsgBoxID(ghwndApp, ghInst, MB_ICONEXCLAMATION, IDS_APPNAME2, IDS_ERR_VIDEDIT);
        f = FALSE;
    }
    SetCursor(ghcurOld);
    return f;
}

/*
 * seg1:450E-4747  LoadPalette
 *
 * File/Load Palette: a RIFF "PAL " file.
 *
 * Quirks of the original: the wait cursor is never restored, and if
 * CreatePalette fails the result is TRUE without a message.
 */
BOOL FAR LoadPalette(void)
{
    char            achFile[144];
    char            achTitle[80];
    MMCKINFO        ckRIFF;
    MMCKINFO        ck;
    HGLOBAL         hres;
    LPCSTR          lpszFilter;
    LPLOGPALETTE    plp;
    HPALETTE        hpal;
    HMMIO           hmmio;
    UINT            ids;
    UINT            cb;
    BOOL            f;

    hmmio = NULL;
    plp = NULL;

    hres = LoadResource(ghInst, FindResource(ghInst, MAKEINTRESOURCE(IDR_FILTER_PAL), RT_RCDATA));
    lpszFilter = hres ? LockResource(hres) : NULL;
    achFile[0] = '\0';
    achTitle[0] = '\0';
    LoadString(ghInst, IDS_LOADPAL_TITLE, achTitle, sizeof(achTitle));
    f = GetFileName(achFile, sizeof(achFile), lpszFilter, achTitle,
                    OFN_HIDEREADONLY | OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOREADONLYRETURN,
                    IDH_LOADPALETTE, TRUE);
    if (hres)
        GlobalUnlock(hres);
    if (!f)
        goto done;

    AnsiUpper(achFile);
    plp = (LPLOGPALETTE)(LOGPALETTE NEAR *)LocalAlloc(LPTR, sizeof(LOGPALETTE) + 255 * sizeof(PALETTEENTRY));
    if (plp == NULL) {
        ids = IDS_ERR_NOMEM;
        goto error;
    }
    hmmio = mmioOpen(achFile, NULL, MMIO_READ);
    if (hmmio == NULL) {
        ids = IDS_ERR_CANTOPEN;
        goto error;
    }
    SetCursor(ghcurWait);

    ckRIFF.fccType = mmioFOURCC('P', 'A', 'L', ' ');
    if (mmioDescend(hmmio, &ckRIFF, NULL, MMIO_FINDRIFF))
        goto bad;
    ck.cksize = 0;
    ck.ckid = mmioFOURCC('d', 'a', 't', 'a');
    if (mmioDescend(hmmio, &ck, &ckRIFF, MMIO_FINDCHUNK))
        goto bad;
    if (mmioRead(hmmio, (HPSTR)plp, 4L) != 4L)
        goto bad;
    if (plp->palVersion != 0x300 || plp->palNumEntries > 256)
        goto bad;
    cb = plp->palNumEntries * sizeof(PALETTEENTRY);
    if (mmioRead(hmmio, (HPSTR)plp->palPalEntry, (LONG)cb) != (LONG)cb)
        goto bad;

    f = TRUE;
    hpal = CreatePalette(plp);
    if (hpal == NULL)
        goto done;
    SetCapturePalette(hpal, NULL);
    videoFrame(ghVideoIn, &gvhFrame);
    InvalidateRect(ghwndShow, NULL, TRUE);
    UpdateWindow(ghwndShow);
    gfDefaultPal = FALSE;
    f = TRUE;
    goto done;

bad:
    ids = IDS_ERR_OPENPAL;
error:
    f = FALSE;
    if (ids)
        MsgBoxID(ghwndApp, ghInst, MB_ICONEXCLAMATION, IDS_APPNAME2, ids, (LPSTR)achFile);
done:
    if (hmmio)
        mmioClose(hmmio, 0);
    if (plp)
        LocalFree((HLOCAL)OFFSETOF(plp));
    return f;
}

/*
 * seg1:4748-4B99  SaveCaptureAs
 *
 * File/Save Captured Video As: copies the capture file's RIFF chunk to a
 * new file, in blocks of up to 256 KB, after making sure the whole size
 * can be written.  Escape stops it (the new file is deleted).
 *
 * Quirks of the original: the filter resource is unlocked again each
 * time the dialog is retried, and after Escape or a write error the
 * result is still TRUE.
 */
BOOL FAR SaveCaptureAs(void)
{
    char        achFile[144];
    char        achTitle[80];
    char        achFormat[80];
    MMCKINFO    ckRIFF;
    HGLOBAL     hres;
    LPCSTR      lpszFilter;
    HGLOBAL     hBuf;
    HPSTR       lpBuf;
    HMMIO       hmmioSrc;
    HMMIO       hmmioDst;
    LONG        cbBuf;
    LONG        lTotal;
    LONG        lLeft;
    BOOL        f;

    f = TRUE;
    hmmioSrc = NULL;
    hmmioDst = NULL;
    hBuf = NULL;
    cbBuf = 0x40000L;

    hres = LoadResource(ghInst, FindResource(ghInst, MAKEINTRESOURCE(IDR_FILTER_AVI2), RT_RCDATA));
    lpszFilter = hres ? LockResource(hres) : NULL;
    achFile[0] = '\0';

    for (;;) {
        achTitle[0] = '\0';
        LoadString(ghInst, IDS_SAVEAS_TITLE, achTitle, sizeof(achTitle));
        if (!GetFileName(achFile, sizeof(achFile), lpszFilter, achTitle,
                         OFN_OVERWRITEPROMPT | OFN_HIDEREADONLY | OFN_PATHMUSTEXIST | OFN_NOREADONLYRETURN,
                         IDH_SAVEAS, FALSE)) {
            if (hres)
                GlobalUnlock(hres);
            goto cancel;
        }
        if (hres)
            GlobalUnlock(hres);

        AnsiUpper(achFile);
        AnsiUpper(gachCaptureFile);
        if (lstrcmp(achFile, gachCaptureFile) != 0)
            break;

        LoadString(ghInst, IDS_OVERWRITECAPFILE, achTitle, sizeof(achTitle));
        if (MessageBox(ghwndApp, achTitle, gszAppName, MB_RETRYCANCEL | MB_ICONEXCLAMATION) != IDRETRY)
            goto cancel;
    }

    for (;;) {
        hBuf = GlobalAlloc(GMEM_MOVEABLE, cbBuf);
        if (hBuf)
            break;
        if ((cbBuf /= 2) == 0)
            goto cancel;
    }

    hmmioDst = mmioOpen(achFile, NULL, MMIO_CREATE | MMIO_WRITE);
    if (hmmioDst == NULL) {
        MsgBoxID(ghwndApp, ghInst, MB_ICONEXCLAMATION, IDS_APPNAME2, IDS_ERR_CANTOPEN, (LPSTR)achFile);
        f = FALSE;
        goto cleanup;
    }
    hmmioSrc = mmioOpen(gachCaptureFile, NULL, MMIO_READ);
    if (hmmioSrc == NULL) {
        MsgBoxID(ghwndApp, ghInst, MB_ICONEXCLAMATION, IDS_APPNAME2, IDS_ERR_CANTOPEN, (LPSTR)gachCaptureFile);
        f = FALSE;
        goto cleanup;
    }

    ghcurOld = SetCursor(ghcurWait);
    LoadString(ghInst, IDS_SAVEAS_WAIT, achFormat, sizeof(achFormat));
    StatusText(achFormat);
    Yield();
    LoadString(ghInst, IDS_SAVEAS_PROGRESS, achFormat, sizeof(achFormat));

    ckRIFF.fccType = formtypeAVI;
    if (mmioDescend(hmmioSrc, &ckRIFF, NULL, MMIO_FINDRIFF))
        goto restorecursor;
    lLeft = lTotal = ckRIFF.cksize + 8;
    mmioAscend(hmmioSrc, &ckRIFF, 0);
    mmioSeek(hmmioSrc, 0L, SEEK_SET);

    /* reserve the space first */
    mmioSeek(hmmioDst, lTotal - 1, SEEK_SET);
    mmioWrite(hmmioDst, achTitle, 1L);
    if (mmioSeek(hmmioDst, 0L, SEEK_END) < lTotal) {
        MsgBoxID(ghwndApp, ghInst, MB_ICONEXCLAMATION, IDS_APPNAME2, IDS_ERR_DISKFULL, (LPSTR)achFile);
        goto abort;
    }
    mmioSeek(hmmioDst, 0L, SEEK_SET);

    UpdateWindow(ghwndShow);
    UpdateWindow(ghwndFrame);
    lpBuf = GlobalLock(hBuf);

    while (lLeft > 0) {
        if (cbBuf > lLeft)
            cbBuf = lLeft;
        mmioRead(hmmioSrc, lpBuf, cbBuf);
        if (mmioWrite(hmmioDst, lpBuf, cbBuf) <= 0) {
            MsgBoxID(ghwndApp, ghInst, MB_ICONEXCLAMATION, IDS_APPNAME2, IDS_ERR_DISKFULL, (LPSTR)achFile);
            mmioClose(hmmioDst, 0);
            mmioOpen(achFile, NULL, MMIO_DELETE);
            hmmioDst = NULL;
            goto restorecursor;
        }
        if (GetAsyncKeyState(VK_ESCAPE) & 1)
            goto abort;
        lLeft -= cbBuf;
        wsprintf(achTitle, achFormat, muldiv32(lTotal - lLeft, 100L, lTotal));
        StatusText(achTitle);
        Yield();
    }
    goto restorecursor;

abort:
    mmioClose(hmmioDst, 0);
    mmioOpen(achFile, NULL, MMIO_DELETE);
    hmmioDst = NULL;
restorecursor:
    SetCursor(ghcurOld);
    goto cleanup;

cancel:
    f = FALSE;
cleanup:
    if (hmmioSrc)
        mmioClose(hmmioSrc, 0);
    if (hmmioDst) {
        mmioSeek(hmmioDst, 0L, SEEK_END);
        mmioClose(hmmioDst, 0);
    }
    if (hBuf) {
        GlobalUnlock(hBuf);
        GlobalFree(hBuf);
    }
    StatusText(NULL);
    return f;
}

/*
 * seg1:4B9A-4E35  SavePalette
 *
 * File/Save Palette as a RIFF "PAL " file.
 *
 * Quirks of the original: an existing file is overwritten without being
 * truncated, and the LOGPALETTE is not freed when writing fails.
 */
BOOL FAR SavePalette(void)
{
    char            achFile[144];
    char            achTitle[80];
    MMCKINFO        ckRIFF;
    MMCKINFO        ck;
    HGLOBAL         hres;
    LPCSTR          lpszFilter;
    LOGPALETTE NEAR *plp;
    HCURSOR         hcurOld;
    HMMIO           hmmio;
    UINT            ids;
    UINT            cb;
    int             nColors;
    int             i;
    BOOL            f;

    hmmio = NULL;
    ids = 0;
    hcurOld = NULL;

    hres = LoadResource(ghInst, FindResource(ghInst, MAKEINTRESOURCE(IDR_FILTER_PAL), RT_RCDATA));
    lpszFilter = hres ? LockResource(hres) : NULL;
    achFile[0] = '\0';
    achTitle[0] = '\0';
    LoadString(ghInst, IDS_SAVEPAL_TITLE, achTitle, sizeof(achTitle));
    f = GetFileName(achFile, sizeof(achFile), lpszFilter, achTitle,
                    OFN_OVERWRITEPROMPT | OFN_HIDEREADONLY | OFN_PATHMUSTEXIST | OFN_NOREADONLYRETURN,
                    IDH_SAVEPALETTE, FALSE);
    if (hres)
        GlobalUnlock(hres);
    if (!f) {
        f = FALSE;
        goto done;
    }

    AnsiUpper(achFile);
    hmmio = mmioOpen(achFile, NULL, MMIO_WRITE);
    if (hmmio == NULL)
        hmmio = mmioOpen(achFile, NULL, MMIO_CREATE | MMIO_WRITE);
    if (hmmio == NULL) {
        hmmio = mmioOpen(achFile, NULL, MMIO_READ);
        ids = hmmio ? IDS_ERR_READONLY : IDS_ERR_CANTOPEN;
        goto error;
    }

    hcurOld = SetCursor(ghcurWait);
    mmioSeek(hmmio, 0L, SEEK_SET);

    ckRIFF.fccType = mmioFOURCC('P', 'A', 'L', ' ');
    if (mmioCreateChunk(hmmio, &ckRIFF, MMIO_CREATERIFF))
        goto bad;
    ck.cksize = 0;
    ck.ckid = mmioFOURCC('d', 'a', 't', 'a');
    if (mmioCreateChunk(hmmio, &ck, 0))
        goto bad;

    GetObject(ghpalCurrent, sizeof(nColors), &nColors);
    if (nColors < 1 || nColors > 256)
        goto bad;
    cb = (nColors + 1) * 4;
    plp = (LOGPALETTE NEAR *)LocalAlloc(LPTR, cb);
    if (plp == NULL)
        goto error;
    plp->palVersion = 0x300;
    plp->palNumEntries = nColors;
    GetPaletteEntries(ghpalCurrent, 0, nColors, plp->palPalEntry);
    for (i = 0; i < nColors; i++)
        plp->palPalEntry[i].peFlags = 0;

    if (mmioWrite(hmmio, (HPSTR)(LPLOGPALETTE)plp, (LONG)cb) != (LONG)cb)
        goto bad;
    if (mmioAscend(hmmio, &ck, 0))
        goto bad;
    if (mmioAscend(hmmio, &ckRIFF, 0))
        goto bad;
    LocalFree((HLOCAL)plp);
    f = TRUE;
    goto done;

bad:
    ids = IDS_ERR_SAVEPAL;
error:
    f = FALSE;
    if (ids)
        MsgBoxID(ghwndApp, ghInst, MB_ICONEXCLAMATION, IDS_APPNAME2, ids, (LPSTR)achFile);
done:
    if (hmmio)
        mmioClose(hmmio, 0);
    if (hcurOld)
        SetCursor(hcurOld);
    return f;
}

/*
 * seg1:4E36-4F37  SaveFrame
 *
 * File/Save Single Frame as a .BMP file.
 */
BOOL FAR SaveFrame(void)
{
    char        achFile[144];
    char        achTitle[80];
    HGLOBAL     hres;
    LPCSTR      lpszFilter;
    HMMIO       hmmio;
    BOOL        f;

    hres = LoadResource(ghInst, FindResource(ghInst, MAKEINTRESOURCE(IDR_FILTER_DIB), RT_RCDATA));
    lpszFilter = hres ? LockResource(hres) : NULL;
    achFile[0] = '\0';
    achTitle[0] = '\0';
    LoadString(ghInst, IDS_SAVEFRAME_TITLE, achTitle, sizeof(achTitle));
    f = GetFileName(achFile, sizeof(achFile), lpszFilter, achTitle,
                    OFN_OVERWRITEPROMPT | OFN_HIDEREADONLY | OFN_PATHMUSTEXIST | OFN_NOREADONLYRETURN,
                    IDH_SAVEFRAME, FALSE);
    if (hres)
        GlobalUnlock(hres);
    if (!f)
        return FALSE;

    AnsiUpper(achFile);
    hmmio = mmioOpen(achFile, NULL, MMIO_CREATE | MMIO_WRITE);
    if (hmmio) {
        f = WriteDIBFile(hmmio);
        mmioClose(hmmio, 0);
        if (f)
            return TRUE;
    }
    MsgBoxID(ghwndApp, ghInst, MB_ICONEXCLAMATION, IDS_APPNAME2, IDS_ERR_SAVEFRAME, (LPSTR)achFile);
    return FALSE;
}

/*
 * TRUE if lpsz has a wildcard.
 */
static BOOL NEAR HasWildcard(LPSTR lpsz)
{
    while (*lpsz) {
        if (!IsDBCSLeadByte(*lpsz) && (*lpsz == '*' || *lpsz == '?'))
            return TRUE;
        lpsz = AnsiNext(lpsz);
    }
    return FALSE;
}

/*
 * seg1:4F38-5277  SetFileDlgHook  (exported ordinal 1, _loadds in the Watcom build)
 *
 * Hook for the Set Capture File dialog (template SETFILE): New Size is
 * enabled for an existing, writable file that is not a device (checked
 * half a second after the name or drive changes), and sets its size.
 */
UINT FAR PASCAL _loadds SetFileDlgHook(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
    char        achFile[144];
    char        achTitle[144];
    OFSTRUCT    of;
    BOOL        f;

    if (msg == gwmFileNameOK)
        return 0;

    switch (msg) {
    case WM_INITDIALOG:
        gidSetFileTimer = 0;
        GetDlgItemText(hDlg, edt1, achFile, sizeof(achFile));
        lstrcpy(achTitle, achFile);
        f = !HasWildcard(achFile) && OpenFile(achFile, &of, OF_EXIST) != HFILE_ERROR;
        EnableWindow(GetDlgItem(hDlg, IDC_NEWSIZE), f);
        return TRUE;

    case WM_COMMAND:
        switch (wParam) {
        case IDOK:
        case IDCANCEL:
            if (gidSetFileTimer)
                KillTimer(hDlg, IDT_SETFILE);
            break;

        case IDC_NEWSIZE:
            GetDlgItemText(hDlg, edt1, achFile, sizeof(achFile));
            if (!AllocCaptureFile(hDlg, achFile))
                return TRUE;
            PostMessage(hDlg, WM_COMMAND, IDOK, 0L);
            break;

        case cmb2:
        case edt1:
            if (HIWORD(lParam) != EN_CHANGE && HIWORD(lParam) != CBN_CLOSEUP)
                break;
            if (gidSetFileTimer)
                KillTimer(hDlg, IDT_SETFILE);
            gidSetFileTimer = SetTimer(hDlg, IDT_SETFILE, 500, NULL);
            if (gidSetFileTimer)
                SendMessage(hDlg, WM_TIMER, IDT_SETFILE, 0L);
            break;
        }
        break;

    case WM_TIMER:
        if (wParam != gidSetFileTimer)
            break;
        KillTimer(hDlg, IDT_SETFILE);
        gidSetFileTimer = 0;
        GetDlgItemText(hDlg, edt1, achFile, sizeof(achFile));
        f = FALSE;
        if (HasWildcard(achFile))
            goto enable;
        if (OpenFile(achFile, &of, OF_EXIST | OF_WRITE) == HFILE_ERROR)
            goto enable;
        GetFileTitle(achFile, achTitle, sizeof(achTitle));
        AnsiLower(achTitle);
        if (lstrcmp(achTitle, gszClock) == 0)
            goto enable;
        if (lstrlen(achTitle) == 3) {
            if (lstrcmp(achTitle, gszPrn) == 0 || lstrcmp(achTitle, gszCon) == 0 ||
                lstrcmp(achTitle, gszAux) == 0 || lstrcmp(achTitle, gszNul) == 0)
                goto enable;
        } else if (lstrlen(achTitle) == 4) {
            achTitle[3] = '\0';
            if (lstrcmp(achTitle, gszLpt) == 0 || lstrcmp(achTitle, gszCom) == 0)
                goto enable;
        }
        f = TRUE;
    enable:
        EnableWindow(GetDlgItem(hDlg, IDC_NEWSIZE), f);
        break;
    }

    return 0;
}
