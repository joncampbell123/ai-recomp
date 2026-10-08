/*
 * compress.c - code segment 3, offsets 0x0000-0x06B7
 *
 * ICM compression message handlers and the Configure dialog.  All
 * functions here are FAR PASCAL (called far from DriverProc in seg1, or
 * via PUSH CS / CALL NEAR from inside seg3).
 */

#include <windows.h>
#include "msvidc.h"

PINSTINFO   gpinstCompress;             /* DS:172A */

static PINSTINFO pinstConfig;           /* DS:09FA */
static char chDecimal = '.';            /* DS:07C8 */
char szConfigure[] = "Configure";       /* DS:073E */

/*
 * seg3:0000-00B2  CompressQuery  (ICM_COMPRESS_QUERY)
 *
 * Input: uncompressed 8/16/24/32 bpp, at least 4x4.  Output: 8 or 16 bpp
 * 'MSVC'/'CRAM' whose size matches the input after rounding both down
 * to a multiple of 4.
 */
LONG FAR PASCAL CompressQuery(PINSTINFO pinst, LPBITMAPINFOHEADER lpbiIn,
                              LPBITMAPINFOHEADER lpbiOut)
{
    if (lpbiIn == NULL)
        return ICERR_BADFORMAT;

    if (lpbiIn->biBitCount != 8 && lpbiIn->biBitCount != 16 &&
        lpbiIn->biBitCount != 24 && lpbiIn->biBitCount != 32)
        return ICERR_BADFORMAT;

    if (lpbiIn->biWidth < 4 || lpbiIn->biHeight < 4)
        return ICERR_BADFORMAT;

    if (lpbiIn->biCompression != BI_RGB)
        return ICERR_BADFORMAT;

    if (lpbiOut == NULL)
        return ICERR_OK;

    if (lpbiOut->biCompression != FOURCC_MSVC && lpbiOut->biCompression != FOURCC_CRAM)
        return ICERR_BADFORMAT;

    if (lpbiOut->biBitCount != 16 && lpbiOut->biBitCount != 8)
        return ICERR_BADFORMAT;

    if ((lpbiOut->biWidth & ~3) != (lpbiIn->biWidth & ~3) ||
        (lpbiOut->biHeight & ~3) != (lpbiIn->biHeight & ~3))
        return ICERR_BADFORMAT;

    return ICERR_OK;
}

/*
 * seg3:00B6-01EA  CompressGetFormat  (ICM_COMPRESS_GET_FORMAT)
 *
 * 8 bpp input gives 8-bit CRAM with the input palette, anything else
 * gives 16-bit CRAM.  The size returned for lpbiOut == NULL is taken
 * from the low word only: the header plus colour table (8 bpp), or the
 * sign-extended low word of biSize (other depths).
 */
LONG FAR PASCAL CompressGetFormat(PINSTINFO pinst, LPBITMAPINFOHEADER lpbiIn,
                                  LPBITMAPINFOHEADER lpbiOut)
{
    LONG l;

    if (l = CompressQuery(pinst, lpbiIn, NULL))
        return l;

    if (lpbiIn->biBitCount == 8) {
        l = lpbiIn->biSize + (UINT)lpbiIn->biClrUsed * sizeof(RGBQUAD);

        if (lpbiOut == NULL)
            return l;

        hmemcpy(lpbiOut, lpbiIn, (LONG)(int)l);
        lpbiOut->biWidth       = lpbiIn->biWidth & ~3;
        lpbiOut->biHeight      = lpbiIn->biHeight & ~3;
        lpbiOut->biBitCount    = 8;
        lpbiOut->biCompression = FOURCC_CRAM;
        lpbiOut->biSizeImage   = CompressGetSize(pinst, lpbiIn, lpbiOut);
    }
    else {
        if (lpbiOut == NULL)
            return (LONG)(int)lpbiIn->biSize;

        *lpbiOut = *lpbiIn;
        lpbiOut->biWidth       = lpbiIn->biWidth & ~3;
        lpbiOut->biHeight      = lpbiIn->biHeight & ~3;
        lpbiOut->biBitCount    = 16;
        lpbiOut->biClrUsed     = 0;
        lpbiOut->biCompression = FOURCC_CRAM;
        lpbiOut->biSizeImage   = CompressGetSize(pinst, lpbiIn, lpbiOut);
    }

    return ICERR_OK;
}

/*
 * seg3:01EE-0262  CompressBegin  (ICM_COMPRESS_BEGIN)
 *
 * 8-bit input or output needs the global palette tables (DS:0A74,
 * DS:0E74, inverse table at DS:1274), so only one instance at a time
 * may do it.  Note that nCompress is set before CompressSetup runs and
 * stays set even if CompressSetup fails.
 */
LONG FAR PASCAL CompressBegin(PINSTINFO pinst, LPBITMAPINFOHEADER lpbiIn,
                              LPBITMAPINFOHEADER lpbiOut)
{
    LONG l;

    if (l = CompressQuery(pinst, lpbiIn, lpbiOut))
        return l;

    if (lpbiIn->biBitCount == 8 || lpbiOut->biBitCount == 8) {
        if (gpinstCompress != NULL && gpinstCompress != pinst)
            return ICERR_ERROR;
        gpinstCompress = pinst;
    }

    pinst->nCompress = 1;

    return CompressSetup(lpbiIn, lpbiOut);
}

/*
 * seg3:0266-02D1  CompressGetSize  (ICM_COMPRESS_GET_SIZE)
 *
 * Worst case: 10 bytes per 4x4 block for 8-bit output, 20 for 16-bit,
 * plus 2.  Uses only the low words of the input dimensions.
 */
LONG FAR PASCAL CompressGetSize(PINSTINFO pinst, LPBITMAPINFOHEADER lpbiIn,
                                LPBITMAPINFOHEADER lpbiOut)
{
    int dx = (int)lpbiIn->biWidth;
    int dy = (int)lpbiIn->biHeight;

    if (lpbiOut->biBitCount == 8)
        return (DWORD)((LONG)dx * dy * 10) / 16 + 2;
    else
        return (DWORD)((LONG)dx * dy * 10) / 8 + 2;
}

/*
 * seg3:02D4-0504  Compress  (ICM_COMPRESS)
 *
 * The quality value (0..10000) is turned into "badness" = 10000 - Q,
 * 2500 for ICQUALITY_DEFAULT.  The temporal badness is
 * badness * 100 / ratio, so a ratio of 75 makes delta frames
 * 1/0.75 times worse.  Both go through QualityToThreshold().  The
 * encoders return the compressed size, or -1 on failure.
 *
 * The frame is a key frame if there is no previous frame, or if the
 * encoder skipped no blocks (g_cSkipped, DS:141A, == 0).
 */
LONG FAR PASCAL Compress(PINSTINFO pinst, ICCOMPRESS FAR *icinfo, DWORD dwSize)
{
    LONG                l;
    BOOL                fBegin;
    LPBITMAPINFOHEADER  lpbiIn  = icinfo->lpbiInput;
    LPBITMAPINFOHEADER  lpbiOut = icinfo->lpbiOutput;
    DWORD               dwBadness;
    LONG                lTemporalBadness;
    LONG                lThreshold;
    LONG                lTemporalThreshold;
    void (FAR *lpfnFPOld)();

    if (l = CompressQuery(pinst, lpbiIn, lpbiOut))
        return l;

    fBegin = !pinst->nCompress;
    if (fBegin) {
        if (l = CompressBegin(pinst, icinfo->lpbiInput, icinfo->lpbiOutput))
            return l;
    }

    lpfnFPOld = _FPInit();

    if (icinfo->dwQuality == ICQUALITY_DEFAULT)
        dwBadness = 2500;
    else
        dwBadness = ICQUALITY_HIGH - icinfo->dwQuality;

    lTemporalBadness   = (LONG)MulDiv((int)dwBadness, 100, pinst->CurrentState);
    lThreshold         = QualityToThreshold(dwBadness);
    lTemporalThreshold = QualityToThreshold((DWORD)lTemporalBadness);

    if (pinst->Status)
        (*pinst->Status)(pinst->lParam, ICSTATUS_START, 0L);

    if (lpbiOut->biBitCount == 8)
        l = CompressFrame8(icinfo->lpbiInput, icinfo->lpInput, icinfo->lpOutput,
                           lThreshold, lTemporalThreshold,
                           icinfo->lpbiPrev, icinfo->lpPrev,
                           pinst->Status, pinst->lParam);
    else
        l = CompressFrame16(icinfo->lpbiInput, icinfo->lpInput, icinfo->lpOutput,
                           lThreshold, lTemporalThreshold,
                           icinfo->lpbiPrev, icinfo->lpPrev,
                           pinst->Status, pinst->lParam);

    if (pinst->Status)
        (*pinst->Status)(pinst->lParam, ICSTATUS_END, 0L);

    _FPTerm(lpfnFPOld);

    if (l == -1L)
        return ICERR_ERROR;

    lpbiOut->biWidth       = lpbiIn->biWidth & ~3;
    lpbiOut->biHeight      = lpbiIn->biHeight & ~3;
    lpbiOut->biCompression = FOURCC_CRAM;
    lpbiOut->biSizeImage   = l;

    if (icinfo->lpckid)
        *icinfo->lpckid = TWOCC_DC;

    if (icinfo->lpdwFlags) {
        *icinfo->lpdwFlags = AVIIF_TWOCC;
        if (icinfo->lpbiPrev == NULL || g_cSkipped == 0)
            *icinfo->lpdwFlags = AVIIF_TWOCC | AVIIF_KEYFRAME;
    }

    if (fBegin)
        CompressEnd(pinst);

    return ICERR_OK;
}

/*
 * seg3:0508-0536  CompressEnd  (ICM_COMPRESS_END)
 */
LONG FAR PASCAL CompressEnd(PINSTINFO pinst)
{
    if (!pinst->nCompress)
        return ICERR_ERROR;

    if (gpinstCompress == pinst)
        gpinstCompress = NULL;

    pinst->nCompress = 0;

    return CompressCleanup();
}

/*
 * seg3:053A-06B4  ConfigureDlgProc  (FAR PASCAL _loadds)
 *
 * Edits the temporal quality ratio (1..100) with a scroll bar and shows
 * it as "0.75", using the [intl] sDecimal character from WIN.INI.
 */
BOOL FAR PASCAL _loadds ConfigureDlgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    char    ach[10];
    HWND    hwndScroll;
    int     nPos;

    switch (msg) {
    case WM_INITDIALOG:
        pinstConfig = (PINSTINFO)LOWORD(lParam);

        ach[0] = chDecimal;
        ach[1] = 0;
        GetProfileString("intl", "sDecimal", ach, ach, sizeof(ach));
        chDecimal = ach[0];

        hwndScroll = GetDlgItem(hwnd, ID_SCROLL);
        nPos = pinstConfig->CurrentState;
        SetScrollRange(hwndScroll, SB_CTL, 1, 100, TRUE);
        SetScrollPos(hwndScroll, SB_CTL, nPos, TRUE);
        wsprintf(ach, "%d%c%02d", nPos / 100, chDecimal, nPos % 100);
        SetDlgItemText(hwnd, ID_TEXT, ach);
        return TRUE;

    case WM_COMMAND:
        switch (wParam) {
        case IDOK:
            pinstConfig->CurrentState = GetScrollPos(GetDlgItem(hwnd, ID_SCROLL), SB_CTL);
            EndDialog(hwnd, TRUE);
            break;

        case IDCANCEL:
            EndDialog(hwnd, FALSE);
            break;
        }
        break;

    case WM_HSCROLL:
        hwndScroll = (HWND)HIWORD(lParam);
        nPos = GetScrollPos(hwndScroll, SB_CTL);

        switch (wParam) {
        case SB_LINEDOWN:       nPos += 1;  break;
        case SB_LINEUP:         nPos -= 1;  break;
        case SB_PAGEDOWN:       nPos += 10; break;
        case SB_PAGEUP:         nPos -= 10; break;
        case SB_THUMBTRACK:
        case SB_THUMBPOSITION:  nPos = LOWORD(lParam); break;
        default:
            return TRUE;
        }

        SetScrollPos(hwndScroll, SB_CTL, nPos = max(1, min(100, nPos)), TRUE);
        wsprintf(ach, "%d%c%02d", nPos / 100, chDecimal, nPos % 100);
        SetDlgItemText(hwnd, ID_TEXT, ach);
        return TRUE;
    }

    return FALSE;
}
