/*
 * drvproc.c - code segment 1 of MSVIDC.DRV (0x0CA4 bytes, PRELOAD)
 *
 * Library entry, the installable-driver/ICM message dispatcher, and the
 * instance, state and decompression-setup handlers.
 *
 * Unless noted, functions are NEAR PASCAL (callee pops; first argument
 * at the highest stack address).  Return values travel in DX:AX.
 */

#include <windows.h>
#include <stdlib.h>
#include "msvidc.h"

WORD        gfCPU286;                   /* DS:0000 */
ICSTATE     DefaultState = 75;          /* DS:0002 */
LPVOID      glpDitherTable;             /* DS:1410 */
HMODULE     ghModule;                   /* DS:1414 */

DWORD NEAR PASCAL SetState(PINSTINFO pinst, LPVOID pv, DWORD dwSize);
LONG  NEAR PASCAL DecompressEnd(PINSTINFO pinst);
LONG  NEAR PASCAL DrawEnd(PINSTINFO pinst);

/*
 * seg1:0000-001E  LibEntry  (assembly, FAR, the NE entry point CS:IP)
 *
 * Windows enters with DI = hInstance, DS = DGROUP, CX = local heap size,
 * ES:SI = command line.
 *
 *      inc   bp
 *      push  bp
 *      mov   bp,sp
 *      push  ds
 *      push  di                ; LibMain(hInstance,
 *      push  cx                ;         cbHeap,
 *      push  es                ;         lpszCmdLine)
 *      push  si
 *      jcxz  @f
 *      push  0
 *      push  0
 *      push  cx
 *      call  LOCALINIT         ; LocalInit(0, 0, cbHeap)
 * @@:  call  LibMain           ; near, pops its own 8 bytes
 *      lea   sp,[bp-2]
 *      pop   ds
 *      pop   bp
 *      dec   bp
 *      retf
 */

/*
 * seg1:001F-0024  WEP  (exported ordinal 1, FAR PASCAL)
 */
int FAR PASCAL WEP(int nParam)
{
    return 1;
}

/*
 * seg1:0C94-0CA1  LibMain  (NEAR PASCAL, called by LibEntry)
 */
int NEAR PASCAL LibMain(HMODULE hModule, WORD cbHeap, LPSTR lpszCmdLine)
{
    ghModule = hModule;
    return 1;
}

/*
 * seg1:0028-0160  BuildDitherTable  (NEAR, no arguments)
 *
 * Allocates 0x1FA00 bytes and fills two tables used by the 16->8 bpp
 * decompressor:
 *   offset 0x00000: WORD [32][32][32]  RGB555 -> index into the 40^3 cube
 *   offset 0x10000: BYTE [40][40][40]  cube cell -> dither palette index
 * The cube is 40 wide per axis so that a 0..7 dither offset can be added
 * to a 5-bit component without overflowing.
 */
LPVOID NEAR BuildDitherTable(void)
{
    LPVOID  lp;
    LPWORD  pw;
    LPBYTE  pb;
    UINT    r, g, b;

    lp = GlobalLock(GlobalAlloc(GMEM_MOVEABLE | GMEM_SHARE, 0x1FA00L));
    if (lp == NULL)
        return NULL;

    pw = (LPWORD)lp;
    for (r = 0; r < 32; r++)
        for (g = 0; g < 32; g++)
            for (b = 0; b < 32; b++)
                *pw++ = (r * 40 + g) * 40 + b;

    pb = (LPBYTE)((BYTE _huge *)lp + 0x10000L);
    for (r = 0; r < 40; r++)
        for (g = 0; g < 40; g++)
            for (b = 0; b < 40; b++)
                *pb++ = abDitherIndex[aiDitherR[r]][aiDitherG[g]][aiDitherB[b]];

    return lp;
}

/*
 * seg1:0161-016F  Load  (DRV_LOAD)
 */
BOOL NEAR Load(void)
{
    gfCPU286 = (WORD)(GetWinFlags() & WF_CPU286);
    return TRUE;
}

/*
 * seg1:0170-01A4  Free  (DRV_FREE)
 */
void NEAR Free(void)
{
    FreeCompressTables();

    if (glpDitherTable != NULL) {
        GlobalUnlock(GlobalHandle(SELECTOROF(glpDitherTable)));
        GlobalFree(GlobalHandle(SELECTOROF(glpDitherTable)));
        glpDitherTable = NULL;
    }
}

/*
 * seg1:01A6-0213  Open  (DRV_OPEN)
 *
 * Only opens of type 'vidc' are accepted; anything else returns 0
 * without touching icinfo->dwError.
 */
PINSTINFO NEAR PASCAL Open(ICOPEN FAR *icinfo)
{
    PINSTINFO pinst;

    if (icinfo->fccType != FOURCC_VIDC)
        return NULL;

    pinst = (PINSTINFO)LocalAlloc(LPTR, sizeof(INSTINFO));
    if (pinst == NULL) {
        icinfo->dwError = (DWORD)ICERR_MEMORY;
        return NULL;
    }

    pinst->dwFlags     = icinfo->dwFlags;
    pinst->nCompress   = 0;
    pinst->nDecompress = 0;
    pinst->nDraw       = 0;
    SetState(pinst, NULL, 0L);

    icinfo->dwError = ICERR_OK;
    return pinst;
}

/*
 * seg1:0216-025B  Close  (DRV_CLOSE)
 */
LONG NEAR PASCAL Close(PINSTINFO pinst)
{
    while (pinst->nCompress > 0)
        CompressEnd(pinst);
    while (pinst->nDecompress > 0)
        DecompressEnd(pinst);
    while (pinst->nDraw > 0)
        DrawEnd(pinst);

    LocalFree((HLOCAL)pinst);
    return 1L;
}

/*
 * seg1:025E-02A1  About  (ICM_ABOUT)
 */
LONG NEAR PASCAL About(PINSTINFO pinst, HWND hwnd)
{
    char achDescription[128];
    char achAbout[64];

    LoadString(ghModule, IDS_DESCRIPTION, achDescription, sizeof(achDescription));
    LoadString(ghModule, IDS_ABOUT, achAbout, sizeof(achAbout));
    MessageBox(hwnd, achDescription, achAbout, MB_OK | MB_ICONINFORMATION);
    return ICERR_OK;
}

/*
 * seg1:02A4-02C4  Configure  (ICM_CONFIGURE)
 *
 * The dialog procedure gets pinst as its init parameter and writes the
 * new ratio straight into pinst->CurrentState on OK.
 */
LONG NEAR PASCAL Configure(PINSTINFO pinst, HWND hwnd)
{
    return (LONG)DialogBoxParam(ghModule, szConfigure, hwnd,
                                ConfigureDlgProc, (LPARAM)(UINT)pinst);
}

/*
 * seg1:02C8-02FB  GetState  (ICM_GETSTATE)
 */
DWORD NEAR PASCAL GetState(PINSTINFO pinst, LPVOID pv, DWORD dwSize)
{
    if (pv == NULL || dwSize == 0)
        return sizeof(ICSTATE);

    if (dwSize < sizeof(ICSTATE))
        return 0;

    *(ICSTATE FAR *)pv = pinst->CurrentState;
    return sizeof(ICSTATE);
}

/*
 * seg1:02FE-0336  SetState  (ICM_SETSTATE)
 *
 * pv == NULL restores the default (75).  Any dwSize other than exactly
 * sizeof(ICSTATE) is rejected.
 */
DWORD NEAR PASCAL SetState(PINSTINFO pinst, LPVOID pv, DWORD dwSize)
{
    if (pv == NULL)
        pinst->CurrentState = DefaultState;
    else if (dwSize == sizeof(ICSTATE))
        pinst->CurrentState = *(ICSTATE FAR *)pv;
    else
        return 0;

    return sizeof(ICSTATE);
}

/*
 * seg1:033A-03BF  GetInfo  (ICM_GETINFO)
 */
DWORD NEAR PASCAL GetInfo(PINSTINFO pinst, ICINFO FAR *icinfo, DWORD dwSize)
{
    if (icinfo == NULL)
        return sizeof(ICINFO);

    if (dwSize < sizeof(ICINFO))
        return 0;

    icinfo->dwSize       = sizeof(ICINFO);              /* 0x128 */
    icinfo->fccType      = FOURCC_VIDC;
    icinfo->fccHandler   = FOURCC_MSVC;
    icinfo->dwFlags      = VIDCF_QUALITY | VIDCF_TEMPORAL;
    icinfo->dwVersion    = 0x00010000L;
    icinfo->dwVersionICM = ICVERSION;                   /* 0x0104 */
    LoadString(ghModule, IDS_DESCRIPTION, icinfo->szDescription, sizeof(icinfo->szDescription));
    LoadString(ghModule, IDS_NAME, icinfo->szName, sizeof(icinfo->szName));

    return sizeof(ICINFO);
}

/*
 * seg1:03C2-0403  DecompressCheckFormat
 *
 * Accepts 8 or 16 bpp input tagged 'MSVC' or 'CRAM'.  pinst is unused.
 */
LONG NEAR PASCAL DecompressCheckFormat(PINSTINFO pinst, LPBITMAPINFOHEADER lpbiIn)
{
    if (lpbiIn != NULL &&
        (lpbiIn->biBitCount == 16 || lpbiIn->biBitCount == 8) &&
        (lpbiIn->biCompression == FOURCC_MSVC || lpbiIn->biCompression == FOURCC_CRAM))
        return ICERR_OK;

    return ICERR_BADFORMAT;
}

/*
 * seg1:0406-05BF  DecompressQuery  (ICM_DECOMPRESS_QUERY, ICM_DECOMPRESSEX_QUERY)
 *
 * Picks a decompressor from the [src depth][stretch][dst depth] tables:
 * aDecompress386 normally, aDecompress286 on a 286.  On a 286, when the
 * output image is larger than 64K, only the 8->8 entry has a fallback
 * (the huge-pointer C routine at seg3:0B8A); every other 286 entry is
 * refused.
 */
LONG NEAR PASCAL DecompressQuery(PINSTINFO pinst, DWORD dwFlags,
                                 LPBITMAPINFOHEADER lpbiSrc, LPVOID pSrc,
                                 int xSrc, int ySrc, int dxSrc, int dySrc,
                                 LPBITMAPINFOHEADER lpbiDst, LPVOID pDst,
                                 int xDst, int yDst, int dxDst, int dyDst)
{
    DWORD       dwSizeImage;
    int         iSrc, iDst, iStretch;
    DECOMPPROC  fn;

    if (DecompressCheckFormat(pinst, lpbiSrc))
        return ICERR_BADFORMAT;

    if (dxSrc == -1)
        dxSrc = (int)lpbiSrc->biWidth;
    if (dySrc == -1)
        dySrc = (int)lpbiSrc->biHeight;

    /* only whole-frame decompression */
    if (xSrc != 0 || ySrc != 0 ||
        (int)lpbiSrc->biWidth != dxSrc || (int)lpbiSrc->biHeight != dySrc)
        return ICERR_BADPARAM;

    if (lpbiDst == NULL)
        return ICERR_OK;

    if (dxDst == -1)
        dxDst = (int)lpbiDst->biWidth;
    if (dyDst == -1)
        dyDst = abs((int)lpbiDst->biHeight);

    dwSizeImage = (DWORD)(UINT)((((UINT)lpbiDst->biWidth * lpbiDst->biBitCount + 31) & ~31) / 8)
                * (UINT)abs((int)lpbiDst->biHeight);

    if (lpbiDst->biCompression != BI_RGB && lpbiDst->biCompression != BI_1632)
        return ICERR_BADFORMAT;

    iSrc = (lpbiSrc->biBitCount >> 3) - 1;
    iDst = (lpbiDst->biBitCount >> 3) - 1;

    if (dxDst == dxSrc && dyDst == dySrc)
        iStretch = 0;
    else if (dxSrc * 2 == dxDst && dySrc * 2 == dyDst)
        iStretch = 1;
    else
        return ICERR_BADSIZE;

    if (gfCPU286) {
        fn = aDecompress286[iSrc][iStretch][iDst];
        if (fn != NULL && dwSizeImage > 0x10000L) {
            if (fn == (DECOMPPROC)Decompress8To8_286)
                fn = (DECOMPPROC)Decompress8To8Huge;
            else
                fn = NULL;
        }
    }
    else {
        fn = aDecompress386[iSrc][iStretch][iDst];
    }

    if (fn == NULL)
        return ICERR_BADFORMAT;

    pinst->lpfnDecompressTest = fn;
    return ICERR_OK;
}

/*
 * seg1:05C2-069A  DecompressGetFormat  (ICM_DECOMPRESS_GET_FORMAT)
 *
 * The suggested output keeps the input depth, rounds the size down to a
 * multiple of 4 and is uncompressed.  The colour table size counts only
 * the low word of biClrUsed.
 */
LONG NEAR PASCAL DecompressGetFormat(PINSTINFO pinst, LPBITMAPINFOHEADER lpbiIn,
                                     LPBITMAPINFOHEADER lpbiOut)
{
    LONG l;
    int  dx, dy;

    if (l = DecompressCheckFormat(pinst, lpbiIn))
        return l;

    if (lpbiOut == NULL)
        return (UINT)((UINT)lpbiIn->biSize + (UINT)lpbiIn->biClrUsed * sizeof(RGBQUAD));

    hmemcpy(lpbiOut, lpbiIn, (UINT)((UINT)lpbiIn->biSize + (UINT)lpbiIn->biClrUsed * sizeof(RGBQUAD)));

    dx = (int)lpbiIn->biWidth & ~3;
    dy = (int)lpbiIn->biHeight & ~3;

    lpbiOut->biWidth       = dx;
    lpbiOut->biHeight      = dy;
    lpbiOut->biBitCount    = lpbiIn->biBitCount;
    lpbiOut->biCompression = BI_RGB;
    lpbiOut->biSizeImage   = (DWORD)(UINT)((((UINT)dx * lpbiOut->biBitCount + 31) & ~31) / 8)
                           * (UINT)abs(dy);

    return ICERR_OK;
}

/*
 * seg1:069E-0759  DecompressBegin  (ICM_DECOMPRESS_BEGIN, ICM_DECOMPRESSEX_BEGIN)
 *
 * Note that it fills in the caller's lpbiDst->biSizeImage when that is 0,
 * and that the 16->8 dither table is built lazily and kept until DRV_FREE.
 */
LONG NEAR PASCAL DecompressBegin(PINSTINFO pinst, DWORD dwFlags,
                                 LPBITMAPINFOHEADER lpbiSrc, LPVOID pSrc,
                                 int xSrc, int ySrc, int dxSrc, int dySrc,
                                 LPBITMAPINFOHEADER lpbiDst, LPVOID pDst,
                                 int xDst, int yDst, int dxDst, int dyDst)
{
    LONG l;

    if (l = DecompressQuery(pinst, dwFlags, lpbiSrc, pSrc, xSrc, ySrc, dxSrc, dySrc,
                            lpbiDst, pDst, xDst, yDst, dxDst, dyDst))
        return l;

    pinst->lpfnDecompress = pinst->lpfnDecompressTest;

    if (lpbiDst->biSizeImage == 0)
        lpbiDst->biSizeImage = (DWORD)(UINT)((((UINT)lpbiDst->biWidth * lpbiDst->biBitCount + 31) & ~31) / 8)
                             * (UINT)abs((int)lpbiDst->biHeight);

    if (lpbiSrc->biBitCount == 16 && lpbiDst->biBitCount == 8) {
        if (glpDitherTable == NULL)
            glpDitherTable = BuildDitherTable();
        if (glpDitherTable == NULL)
            return ICERR_MEMORY;
    }

    pinst->nDecompress = 1;
    return ICERR_OK;
}

/*
 * seg1:075C-07D1  Decompress  (ICM_DECOMPRESS, ICM_DECOMPRESSEX)
 *
 * Without a prior begin, it does an implicit DecompressBegin and then
 * clears nDecompress again.  Source rectangle and flags are not passed
 * on; the decompressor only gets the destination origin.
 */
LONG NEAR PASCAL Decompress(PINSTINFO pinst, DWORD dwFlags,
                            LPBITMAPINFOHEADER lpbiSrc, LPVOID pSrc,
                            int xSrc, int ySrc, int dxSrc, int dySrc,
                            LPBITMAPINFOHEADER lpbiDst, LPVOID pDst,
                            int xDst, int yDst, int dxDst, int dyDst)
{
    LONG l;

    if (!pinst->nDecompress) {
        if (l = DecompressBegin(pinst, dwFlags, lpbiSrc, pSrc, xSrc, ySrc, dxSrc, dySrc,
                                lpbiDst, pDst, xDst, yDst, dxDst, dyDst))
            return l;
        pinst->nDecompress = 0;
    }

    (*pinst->lpfnDecompress)(lpbiSrc, pSrc, lpbiDst, pDst, (LONG)xDst, (LONG)yDst);
    return ICERR_OK;
}

/*
 * seg1:07D4-08AA  DecompressGetPalette  (ICM_DECOMPRESS_GET_PALETTE)
 *
 * 8-bit CRAM: pass the source colour table through (setting the
 * caller's lpbiIn->biClrUsed to 256 if it was 0).  16-bit CRAM: return
 * the fixed dither palette, written directly after a 40-byte header.
 */
LONG NEAR PASCAL DecompressGetPalette(PINSTINFO pinst, LPBITMAPINFOHEADER lpbiIn,
                                      LPBITMAPINFOHEADER lpbiOut)
{
    LONG        l;
    int         i;
    RGBQUAD FAR *prgb;

    if (l = DecompressCheckFormat(pinst, lpbiIn))
        return l;

    if (lpbiOut->biBitCount != 8)
        return ICERR_BADFORMAT;

    if (lpbiIn->biBitCount == 8) {
        if (lpbiIn->biClrUsed == 0)
            lpbiIn->biClrUsed = 256;

        hmemcpy((LPBYTE)lpbiOut + (UINT)lpbiOut->biSize,
                (LPBYTE)lpbiIn + (UINT)lpbiIn->biSize,
                (UINT)lpbiIn->biClrUsed * sizeof(RGBQUAD));
        lpbiOut->biClrUsed = lpbiIn->biClrUsed;
    }
    else {
        lpbiOut->biClrUsed = 256;
        prgb = (RGBQUAD FAR *)(lpbiOut + 1);
        for (i = 0; i < 256; i++) {
            prgb[i].rgbRed      = argbDitherPal[i][0];
            prgb[i].rgbGreen    = argbDitherPal[i][1];
            prgb[i].rgbBlue     = argbDitherPal[i][2];
            prgb[i].rgbReserved = 0;
        }
    }

    return ICERR_OK;
}

/*
 * seg1:08AE-08C9  DecompressEnd  (ICM_DECOMPRESS_END, ICM_DECOMPRESSEX_END)
 */
LONG NEAR PASCAL DecompressEnd(PINSTINFO pinst)
{
    if (!pinst->nDecompress)
        return ICERR_ERROR;

    pinst->nDecompress = 0;
    return ICERR_OK;
}

/*
 * seg1:08CC-08D0  DrawBegin  (ICM_DRAW_BEGIN)       - unsupported
 * seg1:08D4-08D8  Draw       (ICM_DRAW)             - unsupported
 * seg1:08DC-08E0  DrawEnd    (ICM_DRAW_END, Close)  - unsupported
 */
LONG NEAR PASCAL DrawBegin(PINSTINFO pinst, ICDRAWBEGIN FAR *icinfo, DWORD dwSize)
{
    return ICERR_UNSUPPORTED;
}

LONG NEAR PASCAL Draw(PINSTINFO pinst, ICDRAW FAR *icinfo, DWORD dwSize)
{
    return ICERR_UNSUPPORTED;
}

LONG NEAR PASCAL DrawEnd(PINSTINFO pinst)
{
    return ICERR_UNSUPPORTED;
}

/*
 * seg1:08E4-0C91  DriverProc  (exported ordinal 2, FAR PASCAL _loadds)
 */
LONG FAR PASCAL _loadds DriverProc(DWORD dwDriverID, HDRVR hDriver, UINT uiMessage,
                                   LPARAM lParam1, LPARAM lParam2)
{
    PINSTINFO pi = (PINSTINFO)(UINT)dwDriverID;

    switch (uiMessage) {
    case DRV_LOAD:
        return (LONG)Load();

    case DRV_FREE:
        Free();
        return 1L;

    case DRV_OPEN:
        /* opened without an ICOPEN (e.g. by the Drivers control panel) */
        if (lParam2 == 0L)
            return 0xFFFF0000L;
        return (LONG)(UINT)Open((ICOPEN FAR *)lParam2);

    case DRV_CLOSE:
        if (pi)
            Close(pi);
        return 1L;

    case DRV_ENABLE:
    case DRV_DISABLE:
    case DRV_CONFIGURE:
    case DRV_INSTALL:
    case DRV_REMOVE:
        return 1L;

    case DRV_QUERYCONFIGURE:
        return 0L;

    case ICM_GETSTATE:
        return GetState(pi, (LPVOID)lParam1, (DWORD)lParam2);

    case ICM_SETSTATE:
        return SetState(pi, (LPVOID)lParam1, (DWORD)lParam2);

    case ICM_GETINFO:
        return GetInfo(pi, (ICINFO FAR *)lParam1, (DWORD)lParam2);

    case ICM_CONFIGURE:
        if (lParam1 == -1L)                     /* QueryConfigure */
            return ICERR_OK;
        return Configure(pi, (HWND)lParam1);

    case ICM_ABOUT:
        if (lParam1 == -1L)                     /* QueryAbout */
            return ICERR_OK;
        return About(pi, (HWND)lParam1);

    case ICM_GETDEFAULTQUALITY:
        if (lParam1) {
            *((LPDWORD)lParam1) = QUALITY_DEFAULT;
            return ICERR_OK;
        }
        break;

    case ICM_COMPRESS_GET_FORMAT:
        return CompressGetFormat(pi, (LPBITMAPINFOHEADER)lParam1, (LPBITMAPINFOHEADER)lParam2);

    case ICM_COMPRESS_GET_SIZE:
        return CompressGetSize(pi, (LPBITMAPINFOHEADER)lParam1, (LPBITMAPINFOHEADER)lParam2);

    case ICM_COMPRESS_QUERY:
        return CompressQuery(pi, (LPBITMAPINFOHEADER)lParam1, (LPBITMAPINFOHEADER)lParam2);

    case ICM_COMPRESS_BEGIN:
        return CompressBegin(pi, (LPBITMAPINFOHEADER)lParam1, (LPBITMAPINFOHEADER)lParam2);

    case ICM_COMPRESS:
        return Compress(pi, (ICCOMPRESS FAR *)lParam1, (DWORD)lParam2);

    case ICM_COMPRESS_END:
        return CompressEnd(pi);

    case ICM_DECOMPRESS_GET_FORMAT:
        return DecompressGetFormat(pi, (LPBITMAPINFOHEADER)lParam1, (LPBITMAPINFOHEADER)lParam2);

    case ICM_DECOMPRESS_GET_PALETTE:
        return DecompressGetPalette(pi, (LPBITMAPINFOHEADER)lParam1, (LPBITMAPINFOHEADER)lParam2);

    case ICM_DECOMPRESS_QUERY:
        return DecompressQuery(pi, 0, (LPBITMAPINFOHEADER)lParam1, NULL, 0, 0, -1, -1,
                               (LPBITMAPINFOHEADER)lParam2, NULL, 0, 0, -1, -1);

    case ICM_DECOMPRESS_BEGIN:
        return DecompressBegin(pi, 0, (LPBITMAPINFOHEADER)lParam1, NULL, 0, 0, -1, -1,
                               (LPBITMAPINFOHEADER)lParam2, NULL, 0, 0, -1, -1);

    case ICM_DECOMPRESS:
        /* note: icd->dwFlags is not passed on */
        return Decompress(pi, 0,
                          ((ICDECOMPRESS FAR *)lParam1)->lpbiInput,
                          ((ICDECOMPRESS FAR *)lParam1)->lpInput, 0, 0, -1, -1,
                          ((ICDECOMPRESS FAR *)lParam1)->lpbiOutput,
                          ((ICDECOMPRESS FAR *)lParam1)->lpOutput, 0, 0, -1, -1);

    case ICM_DECOMPRESS_END:
    case ICM_DECOMPRESSEX_END:
        return DecompressEnd(pi);

    case ICM_DECOMPRESSEX_QUERY:
    {
        ICDECOMPRESSEX FAR *px = (ICDECOMPRESSEX FAR *)lParam1;

        return DecompressQuery(pi, px->dwFlags, px->lpbiSrc, px->lpSrc,
                               px->xSrc, px->ySrc, px->dxSrc, px->dySrc,
                               px->lpbiDst, px->lpDst,
                               px->xDst, px->yDst, px->dxDst, px->dyDst);
    }

    case ICM_DECOMPRESSEX_BEGIN:
    {
        ICDECOMPRESSEX FAR *px = (ICDECOMPRESSEX FAR *)lParam1;

        return DecompressBegin(pi, px->dwFlags, px->lpbiSrc, px->lpSrc,
                               px->xSrc, px->ySrc, px->dxSrc, px->dySrc,
                               px->lpbiDst, px->lpDst,
                               px->xDst, px->yDst, px->dxDst, px->dyDst);
    }

    case ICM_DECOMPRESSEX:
    {
        ICDECOMPRESSEX FAR *px = (ICDECOMPRESSEX FAR *)lParam1;

        return Decompress(pi, px->dwFlags, px->lpbiSrc, px->lpSrc,
                          px->xSrc, px->ySrc, px->dxSrc, px->dySrc,
                          px->lpbiDst, px->lpDst,
                          px->xDst, px->yDst, px->dxDst, px->dyDst);
    }

    case ICM_DRAW_BEGIN:
        return DrawBegin(pi, (ICDRAWBEGIN FAR *)lParam1, (DWORD)lParam2);

    case ICM_DRAW:
        return Draw(pi, (ICDRAW FAR *)lParam1, (DWORD)lParam2);

    case ICM_DRAW_END:
        return DrawEnd(pi);

    case ICM_SET_STATUS_PROC:
        pi->Status = ((ICSETSTATUSPROC FAR *)lParam1)->Status;
        pi->lParam = ((ICSETSTATUSPROC FAR *)lParam1)->lParam;
        return ICERR_OK;
    }

    if (uiMessage < DRV_USER)
        return DefDriverProc(dwDriverID, hDriver, uiMessage, lParam1, lParam2);

    return ICERR_UNSUPPORTED;
}
