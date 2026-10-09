/*
 * video.c - code segment 1 of VIDCAP.EXE, 2A76-3E68: the capture format,
 * the capture palette, palette capture, the frame buffer, drawing the
 * frame and copying it out as a DIB.
 *
 * The capture driver's channels (opened in init.c): ghVideoIn (VIDEO_IN,
 * format and frames), ghVideoExtIn (VIDEO_EXTERNALIN, source) and
 * ghVideoExtOut (VIDEO_EXTERNALOUT, overlay/display).  glpbiCapture is
 * the current format with room for a colour table, glpFrameBits the last
 * frame grabbed into gvhFrame.
 *
 * Functions are FAR CDECL unless noted.
 */

#include <windows.h>
#include <mmsystem.h>
#include "vidcap.h"

#define WIDTHBYTES16(w, bpp)    ((((WORD)(w) * (bpp)) + 31 & ~31) >> 3)

HPALETTE        ghpalCurrent = NULL;    /* DS:0348 the capture palette */
BOOL            gfDefaultPal = TRUE;    /* DS:034A it is the default palette */
int             gnPalFrames  = 0;       /* DS:034C Capture Palette frames so far */
int             gnPalColors  = 256;     /* DS:034E Capture Palette colours */
                                        /* DS:0350 "%d" */
HGLOBAL         ghFrameBits;            /* DS:0354 */
HGLOBAL         ghbiCapture;            /* DS:0356 */
LPBYTE          glpFrameBits;           /* DS:0358 the last frame */
LPBITMAPINFOHEADER glpbiCapture;        /* DS:035C the capture format */
int             gcxCapture;             /* DS:0360 */
int             gcyCapture;             /* DS:0362 */

/* palette capture */
static LPWORD           glpRGB16;       /* DS:13DE 16-bit frame for the histogram */
static VIDEOHDR         gvhPal;         /* DS:13E2 */
static BITMAPINFOHEADER gbihPal;        /* DS:140A the RGB 5:5:5 format */
static LPVOID           glpHist;        /* DS:1432 histogram (dibmap.c) */
static HGLOBAL          ghFormat;       /* DS:1436 the format to go back to */
static LPBITMAPINFOHEADER glpbiFormat;  /* DS:1438 */
static DWORD            gdwFormatSize;  /* DS:143C */
static UINT             gidPalTimer;    /* DS:1440 */

/*
 * seg1:2A76-2AA9  FreeCapturePalette
 */
void FAR FreeCapturePalette(void)
{
    if (ghpalCurrent && ghpalCurrent != GetStockObject(DEFAULT_PALETTE))
        DeleteObject(ghpalCurrent);
    ghpalCurrent = NULL;
}

/*
 * seg1:2AAA-2B34  GetCapturePalette
 *
 * Reads the driver's palette into ghpalCurrent (the default palette if
 * there is none) and puts it into the format's colour table.  A driver
 * without a palette is taken as not palettized.
 */
int FAR GetCapturePalette(void)
{
    struct {
        WORD            palVersion;
        WORD            palNumEntries;
        PALETTEENTRY    palPalEntry[256];
    } pal;

    FreeCapturePalette();
    pal.palVersion = 0x300;
    pal.palNumEntries = 256;
    if (gfVideoDevice) {
        if (videoConfigure(ghVideoIn, DVM_PALETTE, VIDEO_CONFIGURE_GET | VIDEO_CONFIGURE_CURRENT,
                           NULL, &pal, sizeof(pal), NULL, 0L) == DV_ERR_OK)
            ghpalCurrent = CreatePalette((LPLOGPALETTE)&pal);
        else
            gfPalettized = FALSE;
    }
    if (ghpalCurrent == NULL)
        ghpalCurrent = GetStockObject(DEFAULT_PALETTE);
    SetFormatPalette(ghpalCurrent);
    return 0;
}

/*
 * seg1:2B36-2B4A  InitCapturePalette
 */
int FAR InitCapturePalette(void)
{
    return GetCapturePalette();
}

/*
 * seg1:2B4C-2B60  TermCapturePalette
 */
void FAR TermCapturePalette(void)
{
    FreeCapturePalette();
}

/*
 * seg1:2B62-2C90  SetCapturePalette
 *
 * Gives the driver a new palette (taking over hpal), with an RGB 5:5:5 ->
 * index map if one is given and the driver takes it, and reads it back.
 * Returns TRUE if done.
 *
 * Quirk of the original: a palette of one entry or less is kept as the
 * current palette but not passed on.
 */
BOOL FAR SetCapturePalette(HPALETTE hpal, LPVOID lpMap)
{
    struct {
        WORD            palVersion;
        WORD            palNumEntries;
        PALETTEENTRY    palPalEntry[256];
    } pal;
    int nColors;

    UpdateWindow(ghwndApp);
    UpdateWindow(ghwndFrame);
    if (hpal == NULL)
        return FALSE;

    FreeCapturePalette();
    ghpalCurrent = hpal;
    GetObject(hpal, sizeof(nColors), &nColors);
    if (nColors <= 1)
        return FALSE;
    if (nColors > 256)
        nColors = 256;

    ghcurOld = SetCursor(ghcurWait);
    StatusText(MAKEINTRESOURCE(IDS_BUILDPALMAP));

    pal.palVersion = 0x300;
    pal.palNumEntries = nColors;
    GetPaletteEntries(hpal, 0, nColors, pal.palPalEntry);

    if (gfVideoDevice) {
        if (lpMap == NULL ||
            videoConfigure(ghVideoIn, DVM_PALETTERGB555, VIDEO_CONFIGURE_SET, NULL,
                           &pal, sizeof(pal), lpMap, 0x8000L) != DV_ERR_OK)
            videoConfigure(ghVideoIn, DVM_PALETTE, VIDEO_CONFIGURE_SET, NULL,
                           &pal, sizeof(pal), NULL, 0L);
    }

    GetCapturePalette();
    gfDefaultPal = FALSE;
    InvalidateRect(ghwndShow, NULL, TRUE);
    UpdateWindow(ghwndShow);
    SetCursor(ghcurOld);
    StatusText(NULL);
    return TRUE;
}

/*
 * seg1:2C92-2D07  CopyPalette
 */
HPALETTE FAR CopyPalette(HPALETTE hpal)
{
    LPLOGPALETTE    plp;
    HPALETTE        hpalNew;
    int             nColors;

    if (hpal == NULL)
        return NULL;
    GetObject(hpal, sizeof(nColors), &nColors);
    if (nColors == 0)
        return NULL;
    plp = (LPLOGPALETTE)(LOGPALETTE NEAR *)LocalAlloc(LPTR, (nColors + 2) * 4);
    if (plp == NULL)
        return NULL;
    plp->palVersion = 0x300;
    plp->palNumEntries = nColors;
    GetPaletteEntries(hpal, 0, nColors, plp->palPalEntry);
    hpalNew = CreatePalette(plp);
    LocalFree((HLOCAL)OFFSETOF(plp));
    return hpalNew;
}

/*
 * seg1:2D08-3099  CapturePalette
 *
 * Capture/Palette: frames are grabbed as RGB 5:5:5 into a histogram
 * (Capture Palette dialog), then an optimal palette with gnPalColors
 * entries is made and given to the driver with its 5:5:5 map.  The
 * driver's format is switched to 16 bits for each grab and back after.
 */
void FAR CapturePalette(void)
{
    char        ach[132];
    LPVOID      lpMap;
    DWORD       dwErr;
    HPALETTE    hpal;

    glpRGB16 = NULL;
    lpMap = NULL;
    ghFormat = NULL;
    glpbiFormat = NULL;

    videoConfigure(ghVideoIn, DVM_FORMAT, VIDEO_CONFIGURE_GET | VIDEO_CONFIGURE_QUERYSIZE,
                   &gdwFormatSize, NULL, 0L, NULL, 0L);
    if (gdwFormatSize == 0)
        gdwFormatSize = sizeof(BITMAPINFOHEADER);
    ghFormat = GlobalAlloc(GMEM_MOVEABLE, gdwFormatSize);
    if (ghFormat == NULL)
        goto nomem;
    glpbiFormat = (LPBITMAPINFOHEADER)GlobalLock(ghFormat);

    dwErr = videoConfigure(ghVideoIn, DVM_FORMAT, VIDEO_CONFIGURE_GET | VIDEO_CONFIGURE_CURRENT,
                           NULL, glpbiFormat, gdwFormatSize, NULL, 0L);
    if (dwErr)
        goto done;

    /* see whether the driver can do RGB 5:5:5 */
    gbihPal.biPlanes        = 1;
    gbihPal.biBitCount      = 16;
    gbihPal.biCompression   = BI_RGB;
    gbihPal.biWidth         = gcxCapture;
    gbihPal.biHeight        = gcyCapture;
    gbihPal.biSizeImage     = gbihPal.biWidth * gbihPal.biHeight * 2;
    gbihPal.biXPelsPerMeter = 0;
    gbihPal.biYPelsPerMeter = 0;
    gbihPal.biClrUsed       = 0;
    gbihPal.biClrImportant  = 0;
    gbihPal.biSize          = sizeof(BITMAPINFOHEADER);
    dwErr = videoConfigure(ghVideoIn, DVM_FORMAT, VIDEO_CONFIGURE_SET, NULL,
                           &gbihPal, sizeof(BITMAPINFOHEADER), NULL, 0L);
    if (dwErr)
        goto done;
    dwErr = videoConfigure(ghVideoIn, DVM_FORMAT, VIDEO_CONFIGURE_SET, NULL,
                           glpbiFormat, gdwFormatSize, NULL, 0L);
    if (dwErr)
        goto done;

    glpRGB16 = (LPWORD)GlobalLock(GlobalAlloc(GHND, gbihPal.biSizeImage));
    lpMap = GlobalLock(GlobalAlloc(GMEM_MOVEABLE, 0x8000L));
    glpHist = InitHistogram(NULL);
    if (glpRGB16 == NULL || lpMap == NULL || glpHist == NULL)
        goto nomem;

    gvhPal.lpData = (LPBYTE)glpRGB16;
    gvhPal.dwBufferLength = (long)gcxCapture * gcyCapture * 2;
    gvhPal.dwUser = 0;
    gvhPal.dwFlags = 0;

    if (!DoDialogParam(IDD_CAPPALETTE, ghwndApp, (FARPROC)CapPalDlgProc, 0L))
        goto done;

    UpdateWindow(ghwndApp);
    UpdateWindow(ghwndFrame);
    StatusText(MAKEINTRESOURCE(IDS_OPTIMALPAL));
    ghcurOld = SetCursor(ghcurWait);
    hpal = HistogramPalette(glpHist, lpMap, gnPalColors);
    videoConfigure(ghVideoIn, DVM_FORMAT, VIDEO_CONFIGURE_SET, NULL,
                   glpbiFormat, gdwFormatSize, NULL, 0L);
    SetCapturePalette(hpal, lpMap);
    videoFrame(ghVideoIn, &gvhFrame);
    SetCursor(ghcurOld);
    InvalidateRect(ghwndShow, NULL, TRUE);
    UpdateWindow(ghwndShow);
    gfDefaultPal = FALSE;
    goto done;

nomem:
    dwErr = DV_ERR_NOMEM;

done:
    StatusText(NULL);
    if (glpHist)
        FreeHistogram(glpHist);
    if (glpRGB16)
        GlobalFree((HGLOBAL)SELECTOROF(glpRGB16));
    if (lpMap)
        GlobalFree((HGLOBAL)SELECTOROF(lpMap));
    if (glpbiFormat && ghFormat)
        GlobalUnlock(ghFormat);
    if (ghFormat)
        GlobalFree(ghFormat);
    if (dwErr) {
        if (videoGetErrorText(NULL, (UINT)dwErr, ach, sizeof(ach)) == DV_ERR_OK)
            ErrMsg(ach);
    }
}

/*
 * seg1:309A-3453  CapPalDlgProc  (exported ordinal 2)
 *
 * Frame grabs one frame into the histogram; Start/Stop does it every
 * 100 ms.  The dialog ends with the number of frames grabbed.  At 256 and
 * 236 colours the arrows step by 20, between them.
 */
BOOL FAR PASCAL CapPalDlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
    char    ach[40];
    char    achFmt[40];
    UINT    n;

    switch (msg) {
    case WM_INITDIALOG:
        gnPalFrames = 0;
        wsprintf(ach, "%d", gnPalColors);
        SetDlgItemText(hDlg, IDC_PALCOLORS, ach);
        PositionDialog(hDlg, ghwndShow);
        return TRUE;

    case WM_COMMAND:
        switch (wParam) {
        case IDCANCEL:
            if (gidPalTimer) {
                KillTimer(hDlg, IDT_PALETTE);
                gidPalTimer = 0;
            }
            n = GetDlgItemInt(hDlg, IDC_PALCOLORS, (BOOL FAR *)ach, FALSE);
            if (n > 256)
                n = 256;
            if (n < 2)
                n = 2;
            gnPalColors = n;
            EndDialog(hDlg, gnPalFrames);
            break;

        case IDC_PALFRAME:
            videoConfigure(ghVideoIn, DVM_FORMAT, VIDEO_CONFIGURE_SET, NULL,
                           &gbihPal, sizeof(BITMAPINFOHEADER), NULL, 0L);
            videoFrame(ghVideoIn, &gvhPal);
            DibHistogram(&gbihPal, (LPBYTE)glpRGB16, 0, 0, -1, -1, glpHist);
            videoConfigure(ghVideoIn, DVM_FORMAT, VIDEO_CONFIGURE_SET, NULL,
                           glpbiFormat, gdwFormatSize, NULL, 0L);
            if (gnPalFrames == 0) {
                LoadString(ghInst, IDS_CLOSE, ach, sizeof(ach));
                SetDlgItemText(hDlg, IDCANCEL, ach);
            }
            gnPalFrames++;
            LoadString(ghInst, IDS_NFRAMES, achFmt, sizeof(achFmt));
            wsprintf(ach, achFmt, gnPalFrames);
            SetDlgItemText(hDlg, IDC_PALNUMFRAMES, ach);
            videoFrame(ghVideoIn, &gvhFrame);
            InvalidateRect(ghwndShow, NULL, TRUE);
            UpdateWindow(ghwndShow);
            return TRUE;

        case IDC_PALSTART:
            SetFocus(GetDlgItem(hDlg, IDC_PALSTART));
            if (gnPalFrames == 0) {
                LoadString(ghInst, IDS_CLOSE, ach, sizeof(ach));
                SetDlgItemText(hDlg, IDCANCEL, ach);
            }
            if (gidPalTimer == 0) {
                gidPalTimer = SetTimer(hDlg, IDT_PALETTE, 100, NULL);
                if (gidPalTimer == 0) {
                    MessageBeep(0);
                    return TRUE;
                }
                EnableWindow(GetDlgItem(hDlg, IDC_PALFRAME), FALSE);
                LoadString(ghInst, IDS_STOP, ach, sizeof(ach));
                SetDlgItemText(hDlg, IDC_PALSTART, ach);
            } else {
                LoadString(ghInst, IDS_START, ach, sizeof(ach));
                SetDlgItemText(hDlg, IDC_PALSTART, ach);
                EnableWindow(GetDlgItem(hDlg, IDC_PALFRAME), TRUE);
                KillTimer(hDlg, IDT_PALETTE);
                gidPalTimer = 0;
            }
            return TRUE;

        case IDC_PALCOLORS:
            if (HIWORD(lParam) == EN_KILLFOCUS) {
                n = GetDlgItemInt(hDlg, IDC_PALCOLORS, NULL, FALSE);
                if (n < 2) {
                    MessageBeep(0);
                    SetDlgItemInt(hDlg, IDC_PALCOLORS, 2, FALSE);
                } else if (n > 256) {
                    MessageBeep(0);
                    SetDlgItemInt(hDlg, IDC_PALCOLORS, 256, FALSE);
                }
            }
            return TRUE;
        }
        break;

    case WM_TIMER:
        if (wParam == IDT_PALETTE)
            SendMessage(hDlg, WM_COMMAND, IDC_PALFRAME, 0L);
        break;

    case WM_VSCROLL:
        n = GetDlgItemInt(hDlg, IDC_PALCOLORS, NULL, FALSE);
        if (n == 256)
            ArrowEditChangeStep2(GetDlgItem(hDlg, IDC_PALCOLORS), wParam, 2L, 256L, 20, 20);
        else if (n == 236)
            ArrowEditChangeStep2(GetDlgItem(hDlg, IDC_PALCOLORS), wParam, 2L, 256L, 20, 1);
        else
            ArrowEditChange(GetDlgItem(hDlg, IDC_PALCOLORS), wParam, 2L, 256L);
        break;
    }

    return FALSE;
}

/*
 * seg1:3456-34D1  InitBitmapInfoHeader
 *
 * The fallback format: 160x120, 8 bits.
 */
void FAR InitBitmapInfoHeader(LPBITMAPINFOHEADER lpbi)
{
    lpbi->biSize          = sizeof(BITMAPINFOHEADER);
    lpbi->biWidth         = 160;
    lpbi->biHeight        = 120;
    lpbi->biBitCount      = 8;
    lpbi->biPlanes        = 1;
    lpbi->biCompression   = BI_RGB;
    lpbi->biSizeImage     = 160L * 120;
    lpbi->biXPelsPerMeter = 0;
    lpbi->biYPelsPerMeter = 0;
    lpbi->biClrUsed       = 256;
    lpbi->biClrImportant  = 0;
}

/*
 * seg1:34D2-355A  AllocCaptureFormat
 *
 * A copy of the format in glpbiCapture, with 1 KB for a colour table.
 */
DWORD FAR AllocCaptureFormat(LPBITMAPINFOHEADER lpbi)
{
    if (ghbiCapture) {
        GlobalUnlock(ghbiCapture);
        GlobalFree(ghbiCapture);
        ghbiCapture = NULL;
        glpbiCapture = NULL;
    }
    ghbiCapture = GlobalAlloc(GMEM_MOVEABLE, lpbi->biSize + 1024);
    if (ghbiCapture == NULL)
        return DV_ERR_NOMEM;
    glpbiCapture = (LPBITMAPINFOHEADER)GlobalLock(ghbiCapture);
    hmemcpy(glpbiCapture, lpbi, lpbi->biSize);
    return 0L;
}

/*
 * seg1:355C-35C3  AllocFrameBuffer
 */
DWORD FAR AllocFrameBuffer(LPBITMAPINFOHEADER lpbi)
{
    if (ghFrameBits) {
        GlobalUnlock(ghFrameBits);
        GlobalFree(ghFrameBits);
        ghFrameBits = NULL;
        glpFrameBits = NULL;
    }
    ghFrameBits = GlobalAlloc(GMEM_MOVEABLE, lpbi->biSizeImage);
    if (ghFrameBits == NULL)
        return DV_ERR_NOMEM;
    glpFrameBits = GlobalLock(ghFrameBits);
    return 0L;
}

/*
 * seg1:35C4-35F4  InitVideoFormat
 *
 * Returns the low word of AllocCaptureFormat's result.
 */
int FAR InitVideoFormat(void)
{
    BITMAPINFOHEADER bi;

    ghdd = DrawDibOpen();
    InitBitmapInfoHeader(&bi);
    return (int)AllocCaptureFormat(&bi);
}

/*
 * seg1:35F6-365A  TermVideoFormat
 */
void FAR TermVideoFormat(void)
{
    if (ghFrameBits) {
        GlobalUnlock(ghFrameBits);
        GlobalFree(ghFrameBits);
        ghFrameBits = NULL;
    }
    if (ghbiCapture) {
        GlobalUnlock(ghbiCapture);
        GlobalFree(ghbiCapture);
        ghbiCapture = NULL;
    }
    if (ghdd) {
        DrawDibClose(ghdd);
        ghdd = NULL;
    }
}

/*
 * seg1:365C-3704  SetDriverFormat
 *
 * Sets the driver's format and makes the source and destination
 * rectangles the full frame.
 */
DWORD FAR SetDriverFormat(LPBITMAPINFOHEADER lpbi, DWORD cb)
{
    RECT    rc;
    DWORD   dwErr;

    rc.left = 0;
    rc.top = 0;
    rc.right = (int)lpbi->biWidth;
    rc.bottom = (int)lpbi->biHeight;

    dwErr = videoConfigure(ghVideoIn, DVM_FORMAT, VIDEO_CONFIGURE_SET, NULL, lpbi, cb, NULL, 0L);
    if (dwErr == 0) {
        videoMessage(ghVideoExtIn, DVM_DST_RECT, (DWORD)(LPVOID)&rc, VIDEO_CONFIGURE_SET);
        videoMessage(ghVideoIn, DVM_SRC_RECT, (DWORD)(LPVOID)&rc, VIDEO_CONFIGURE_SET);
        videoMessage(ghVideoIn, DVM_DST_RECT, (DWORD)(LPVOID)&rc, VIDEO_CONFIGURE_SET);
    }
    return dwErr;
}

/*
 * seg1:3706-37F6  SetCaptureFormat
 *
 * Makes lpbi the capture format: fills in the image size and colour
 * count, gives it to the driver and sets up the frame buffer.
 */
DWORD FAR SetCaptureFormat(LPBITMAPINFOHEADER lpbi)
{
    DWORD dwErr;

    if (lpbi->biSizeImage == 0)
        lpbi->biSizeImage = (long)(int)WIDTHBYTES16(lpbi->biWidth, lpbi->biBitCount) * lpbi->biHeight;
    if (lpbi->biBitCount <= 8 && lpbi->biClrUsed == 0)
        lpbi->biClrUsed = (long)(int)(1 << lpbi->biBitCount);

    dwErr = SetDriverFormat(lpbi, lpbi->biSize);
    if (dwErr)
        return dwErr;
    dwErr = AllocCaptureFormat(lpbi);
    if (dwErr)
        return dwErr;
    dwErr = AllocFrameBuffer(lpbi);
    if (dwErr)
        return dwErr;

    gcxCapture = (int)lpbi->biWidth;
    gcyCapture = (int)lpbi->biHeight;
    gvhFrame.lpData = glpFrameBits;
    gvhFrame.dwBufferLength = lpbi->biSizeImage;
    gvhFrame.dwUser = 0;
    gvhFrame.dwFlags = 0;
    return 0L;
}

/*
 * seg1:37F8-392D  GetCaptureFormat
 *
 * Reads the driver's format and makes it the capture format (or the
 * fallback, after showing the error), then grabs a frame.  Returns the
 * low word of the error.
 */
int FAR GetCaptureFormat(void)
{
    char                ach[132];
    BITMAPINFOHEADER    bi;
    LPBITMAPINFOHEADER  lpbi;
    HGLOBAL             h;
    DWORD               dwSize;
    DWORD               dwErr;

    dwSize = 0;
    if (!gfVideoDevice)
        return 0;

    videoConfigure(ghVideoIn, DVM_FORMAT, VIDEO_CONFIGURE_GET | VIDEO_CONFIGURE_QUERYSIZE,
                   &dwSize, NULL, 0L, NULL, 0L);
    if (dwSize == 0)
        dwSize = sizeof(BITMAPINFOHEADER);
    h = GlobalAlloc(GMEM_MOVEABLE, dwSize);
    if (h == NULL)
        return DV_ERR_NOMEM;
    lpbi = (LPBITMAPINFOHEADER)GlobalLock(h);

    dwErr = videoConfigure(ghVideoIn, DVM_FORMAT, VIDEO_CONFIGURE_GET | VIDEO_CONFIGURE_CURRENT,
                           NULL, lpbi, dwSize, NULL, 0L);
    if (dwErr == 0) {
        dwErr = SetCaptureFormat(lpbi);
        if (dwErr) {
            videoGetErrorText(NULL, (UINT)dwErr, ach, sizeof(ach));
            ErrMsg(ach);
            InitBitmapInfoHeader(&bi);
            dwErr = SetCaptureFormat(&bi);
        }
        if (dwErr == 0)
            videoFrame(ghVideoIn, &gvhFrame);
    }

    if (lpbi)
        GlobalUnlock(h);
    if (h)
        GlobalFree(h);
    return (int)dwErr;
}

/*
 * seg1:392E-3998  MapBits
 *
 * Translates cb bytes through a 256-byte table.
 */
void FAR MapBits(BYTE _huge *lpBits, DWORD cb, BYTE FAR *lpMap)
{
    while (cb--) {
        *lpBits = ((BYTE _huge *)lpMap)[*lpBits];
        lpBits++;
    }
}

/*
 * seg1:399A-3B68  SetFormatPalette
 *
 * Copies the palette into the 8-bit format's colour table and remaps the
 * frame through it.  Returns TRUE if there was a format and a frame.
 *
 * Quirk of the original: the map is built from the colour table after it
 * has been overwritten with the palette, so it is the identity for the
 * entries the palette has.
 */
BOOL FAR SetFormatPalette(HPALETTE hpal)
{
    BYTE                abMap[256];
    PALETTEENTRY        pe;
    LPBITMAPINFOHEADER  lpbi;
    RGBQUAD FAR        *prgb;
    LPBYTE              lpBits;
    DWORD               cb;
    int                 nColors;
    int                 i;

    if (hpal == NULL || glpFrameBits == NULL || glpbiCapture == NULL)
        return FALSE;

    lpbi = glpbiCapture;
    prgb = (RGBQUAD FAR *)((LPBYTE)lpbi + (WORD)lpbi->biSize);
    lpBits = glpFrameBits;
    GetObject(hpal, sizeof(nColors), &nColors);
    if (nColors > 256)
        nColors = 256;

    if (lpbi->biBitCount == 8) {
        for (i = 0; i < nColors; i++) {
            GetPaletteEntries(hpal, i, 1, &pe);
            prgb[i].rgbRed = pe.peRed;
            prgb[i].rgbGreen = pe.peGreen;
            prgb[i].rgbBlue = pe.peBlue;
        }
    }

    if (lpbi->biBitCount == 8 && lpbi->biCompression == BI_RGB) {
        for (i = 0; i < (int)lpbi->biClrUsed; i++)
            abMap[i] = (BYTE)GetNearestPaletteIndex(hpal,
                                RGB(prgb[i].rgbRed, prgb[i].rgbGreen, prgb[i].rgbBlue));
        cb = lpbi->biSizeImage;
        if (cb == 0)
            cb = (long)(int)WIDTHBYTES16(lpbi->biWidth, lpbi->biBitCount) * lpbi->biHeight;
        if (LOWORD(lpbi->biCompression) == BI_RGB)
            MapBits(lpBits, cb, abMap);
    }

    if (lpbi->biBitCount <= 8)
        lpbi->biClrUsed = nColors;
    return TRUE;
}

/*
 * seg1:3B6A-3BFF  DrawFrame
 *
 * Draws the last frame at the top left with DrawDib, or fills the window
 * with black if there is none or DrawDib fails.
 */
void FAR DrawFrame(HWND hwnd, HDC hdc)
{
    RECT    rc;
    BOOL    f;

    f = (glpFrameBits != NULL);
    if (f)
        f = DrawDibDraw(ghdd, hdc, 0, 0, gcxCapture, gcyCapture, glpbiCapture, glpFrameBits,
                        0, 0, -1, -1, 0);
    if (!f) {
        SelectObject(hdc, GetStockObject(BLACK_BRUSH));
        GetClientRect(hwnd, &rc);
        PatBlt(hdc, 0, 0, rc.right, rc.bottom, PATCOPY);
    }
}

/*
 * seg1:3C00-3E3D  FrameToDIB
 *
 * The last frame as a packed DIB in global memory, with the current
 * palette as its colour table.  BI_RGB and BI_RLE8 frames are copied;
 * others are decompressed to 8 or 24 bits with ICImageDecompress.  The
 * copied DIB is returned still locked.
 *
 * Quirk of the original: there is also a test for 8 *and* 24 bits per
 * pixel, which can never be true.
 */
HANDLE FAR FrameToDIB(void)
{
    LPBITMAPINFOHEADER  lpbi;
    PALETTEENTRY        pe;
    RGBQUAD FAR        *prgb;
    HANDLE              h;
    int                 i;

    lpbi = glpbiCapture;
    if (lpbi->biCompression == BI_RGB || lpbi->biCompression == BI_RLE8 ||
        (lpbi->biBitCount == 8 && lpbi->biBitCount == 24))
        goto copy;

    lpbi = (LPBITMAPINFOHEADER)GlobalLock(GlobalAlloc(GMEM_MOVEABLE,
                                          sizeof(BITMAPINFOHEADER) + 256 * sizeof(RGBQUAD)));
    if (lpbi == NULL)
        return NULL;
    hmemcpy(lpbi, glpbiCapture, (long)sizeof(BITMAPINFOHEADER));
    lpbi->biSize = sizeof(BITMAPINFOHEADER);
    lpbi->biCompression = BI_RGB;
    lpbi->biClrUsed = 0;
    lpbi->biClrImportant = 0;
    lpbi->biBitCount = (glpbiCapture->biBitCount <= 8) ? 8 : 24;
    lpbi->biSizeImage = (DWORD)(WORD)((((lpbi->biBitCount == 8) ? 1 : 3) *
                                       (WORD)lpbi->biWidth + 31 & ~31) >> 3) * lpbi->biHeight;
    h = ICImageDecompress(NULL, 0, (LPBITMAPINFO)glpbiCapture, glpFrameBits, (LPBITMAPINFO)lpbi);
    if (lpbi) {
        GlobalUnlock(GlobalHandle(SELECTOROF(lpbi)));
        GlobalFree(GlobalHandle(SELECTOROF(lpbi)));
    }
    return h;

copy:
    h = GlobalAlloc(GMEM_MOVEABLE, lpbi->biClrUsed * 4 + lpbi->biSizeImage + lpbi->biSize);
    if (h == NULL)
        return NULL;
    lpbi = (LPBITMAPINFOHEADER)GlobalLock(h);
    hmemcpy(lpbi, glpbiCapture, glpbiCapture->biSize);
    prgb = (RGBQUAD FAR *)((LPBYTE)lpbi + (WORD)lpbi->biSize);
    for (i = 0; i < (int)glpbiCapture->biClrUsed; i++) {
        GetPaletteEntries(ghpalCurrent, i, 1, &pe);
        prgb[i].rgbRed = pe.peRed;
        prgb[i].rgbGreen = pe.peGreen;
        prgb[i].rgbBlue = pe.peBlue;
        prgb[i].rgbReserved = 0;
    }
    hmemcpy((LPBYTE)lpbi + (WORD)lpbi->biSize + (WORD)lpbi->biClrUsed * 4,
            glpFrameBits, lpbi->biSizeImage);
    return h;
}

/*
 * seg1:3E3E-3E68  HaveFrame
 */
BOOL FAR HaveFrame(void)
{
    return (glpFrameBits && glpbiCapture) ? TRUE : FALSE;
}
