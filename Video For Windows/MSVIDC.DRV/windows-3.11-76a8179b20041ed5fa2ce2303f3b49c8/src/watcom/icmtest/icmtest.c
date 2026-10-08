/*
 * icmtest.c - A/B test harness for MSVIDC.DRV builds (Win16, Open Watcom)
 *
 * usage (from DOS):  win icmtest <codec.drv> <output file>
 *
 * Loads the codec with LoadLibrary and talks to its DriverProc directly
 * (no MSVIDEO.DLL needed), runs a fixed, deterministic set of ICM
 * compress and decompress operations on synthetic frames, and writes
 * every result (return codes, headers, compressed data, decoded pixels)
 * to the output file as tagged records.  Running it against the original
 * driver and a rebuilt one and comparing the two files shows whether the
 * rebuild behaves identically.  Exits Windows when done.
 */

#include <windows.h>
#include <mmsystem.h>
#include <string.h>
#include "compddk.h"

typedef LONG (FAR PASCAL *DRIVERPROC)(DWORD, HDRVR, UINT, LONG, LONG);

static DRIVERPROC   pfnDriverProc;
static DWORD        dwID;
static HFILE        hOut = HFILE_ERROR;
static WORD         wSeq;
static char         achLog[128];            /* progress log, reopened per line */

#define MAXW        168
#define MAXH        124
#define NFRAMES     4

/* ------------------------------------------------------------------ */

static void Rec(const char FAR *tag, const void _huge *data, DWORD cb)
{
    char hdr[24];
    DWORD len = cb;

    _fmemset(hdr, 0, sizeof(hdr));
    lstrcpyn(hdr, tag, 16);
    *(WORD FAR *)(hdr + 16) = wSeq++;
    *(DWORD FAR *)(hdr + 20) = len;
    _lwrite(hOut, hdr, sizeof(hdr));
    if (cb)
        _hwrite(hOut, (const char _huge *)data, cb);
}

static void RecL(const char FAR *tag, LONG l)
{
    char    ach[64];
    HFILE   h;

    Rec(tag, &l, sizeof(l));

    /* host-side writes are buffered, so a hang would lose the tail of the
     * output; this small log is closed after every line */
    if (achLog[0] && (h = _lopen(achLog, OF_WRITE)) != HFILE_ERROR) {
        _llseek(h, 0, 2);
        wsprintf(ach, "%u %s %ld\r\n", wSeq - 1, tag, l);
        _lwrite(h, ach, lstrlen(ach));
        _lclose(h);
    }
}

static LONG Send(UINT msg, LONG l1, LONG l2)
{
    return (*pfnDriverProc)(dwID, (HDRVR)1, msg, l1, l2);
}

static LPVOID Alloc(DWORD cb)
{
    LPVOID p = GlobalLock(GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, cb));
    return p;
}

static void Free(LPVOID p)
{
    if (p) {
        HGLOBAL h = (HGLOBAL)LOWORD(GlobalHandle(SELECTOROF(p)));
        GlobalUnlock(h);
        GlobalFree(h);
    }
}

/* ------------------------------------------------------------------ */
/* synthetic frames                                                    */
/* ------------------------------------------------------------------ */

static DWORD dwRand;

static WORD Rnd(void)
{
    dwRand = dwRand * 1103515245UL + 12345UL;
    return (WORD)(dwRand >> 16);
}

/* a gradient, a moving block and some noise; frame f moves things */
static void MakeRGB(int x, int y, int w, int h, int f, BYTE *r, BYTE *g, BYTE *b)
{
    int bx = 8 + f * 6, by = 6 + f * 3;

    *r = (BYTE)(x * 255 / w);
    *g = (BYTE)(y * 255 / h);
    *b = (BYTE)((x + y) * 2);
    if (x >= bx && x < bx + w / 3 && y >= by && y < by + h / 3) {
        *r = 230; *g = 40; *b = 60;
    }
    if (y > h * 2 / 3 && (Rnd() & 7) == 0) {
        *r ^= (BYTE)Rnd(); *g ^= (BYTE)Rnd();
    }
    if (x < 12)                                 /* flat area */
        *r = *g = *b = 128;
}

static void MakePalette(RGBQUAD FAR *pal)
{
    int i;
    for (i = 0; i < 256; i++) {               /* 6x6x6 cube + greys */
        if (i < 216) {
            pal[i].rgbRed   = (BYTE)((i / 36) * 51);
            pal[i].rgbGreen = (BYTE)(((i / 6) % 6) * 51);
            pal[i].rgbBlue  = (BYTE)((i % 6) * 51);
        }
        else {
            pal[i].rgbRed = pal[i].rgbGreen = pal[i].rgbBlue = (BYTE)((i - 216) * 6);
        }
        pal[i].rgbReserved = 0;
    }
}

static DWORD Stride(int w, int bpp)
{
    return (((DWORD)w * bpp + 31) & ~31L) >> 3;
}

static void MakeFrame(LPBITMAPINFOHEADER lpbi, BYTE _huge *bits, int f)
{
    int     w = (int)lpbi->biWidth, h = (int)lpbi->biHeight, x, y;
    DWORD   stride = Stride(w, lpbi->biBitCount);
    BYTE    r, g, b;

    dwRand = 1234567UL + f;
    for (y = 0; y < h; y++) {
        BYTE _huge *p = bits + stride * y;
        for (x = 0; x < w; x++) {
            MakeRGB(x, y, w, h, f, &r, &g, &b);
            switch (lpbi->biBitCount) {
            case 8:
                *p++ = (BYTE)((r / 51) * 36 + (g / 51) * 6 + (b / 51));
                break;
            case 16:
                *(WORD _huge *)p = (WORD)(((r >> 3) << 10) | ((g >> 3) << 5) | (b >> 3));
                p += 2;
                break;
            case 24:
                *p++ = b; *p++ = g; *p++ = r;
                break;
            case 32:
                *p++ = b; *p++ = g; *p++ = r; *p++ = 0;
                break;
            }
        }
    }
}

/* ------------------------------------------------------------------ */

typedef struct {
    BITMAPINFOHEADER bi;
    RGBQUAD          pal[256];
} BMI;

static BMI  biIn, biOut, biDec;
static char achTag[16];

static void Tag(const char *fmt, int a, int b, int c, int d)
{
    wsprintf(achTag, fmt, a, b, c, d);
}

/*
 * Compress NFRAMES frames of a w x h, inBpp image to CRAM outBpp (8 or
 * 16) at the given quality, then decompress every frame to each output
 * the codec can do.
 */
static void TestSequence(int w, int h, int inBpp, int outBpp, LONG quality, BOOL fTemporal)
{
    BYTE _huge *frames[NFRAMES];
    BYTE _huge *cram[NFRAMES];
    DWORD       cbCram[NFRAMES];
    BYTE _huge *dec;
    DWORD       cbIn, cbMax;
    ICCOMPRESS  icc;
    ICDECOMPRESS icd;
    DWORD       ckid, dwFlags;
    LONG        l;
    int         f, dbpp, stretch;

    _fmemset(&biIn, 0, sizeof(biIn));
    biIn.bi.biSize = sizeof(BITMAPINFOHEADER);
    biIn.bi.biWidth = w;
    biIn.bi.biHeight = h;
    biIn.bi.biPlanes = 1;
    biIn.bi.biBitCount = inBpp;
    biIn.bi.biCompression = BI_RGB;
    if (inBpp == 8) {
        biIn.bi.biClrUsed = 256;
        MakePalette(biIn.pal);
    }
    cbIn = Stride(w, inBpp) * h;
    biIn.bi.biSizeImage = cbIn;

    Tag("seq%dx%d.%d>%d", w, h, inBpp, outBpp);
    RecL(achTag, quality);

    /* output format: ask the codec where it decides (8 bpp input gives
     * CRAM8, everything else CRAM16), build it ourselves otherwise */
    if ((inBpp == 8) == (outBpp == 8)) {
        l = Send(ICM_COMPRESS_GET_FORMAT, (LONG)(LPVOID)&biIn, 0);
        RecL("getfmtsize", l);
        _fmemset(&biOut, 0, sizeof(biOut));
        l = Send(ICM_COMPRESS_GET_FORMAT, (LONG)(LPVOID)&biIn, (LONG)(LPVOID)&biOut);
        RecL("getfmt", l);
    }
    else {
        /* 16/24/32 -> CRAM8 with our own palette, or 8 -> CRAM16 */
        biOut = biIn;
        biOut.bi.biBitCount = outBpp;
        biOut.bi.biClrUsed = (outBpp == 8) ? 256 : 0;
        biOut.bi.biCompression = mmioFOURCC('C','R','A','M');
        if (outBpp == 8)
            MakePalette(biOut.pal);
        biOut.bi.biWidth &= ~3;
        biOut.bi.biHeight &= ~3;
    }
    Rec("biOut", &biOut, sizeof(biOut));

    l = Send(ICM_COMPRESS_QUERY, (LONG)(LPVOID)&biIn, (LONG)(LPVOID)&biOut);
    RecL("query", l);
    if (l != ICERR_OK)
        return;

    cbMax = Send(ICM_COMPRESS_GET_SIZE, (LONG)(LPVOID)&biIn, (LONG)(LPVOID)&biOut);
    RecL("getsize", cbMax);

    for (f = 0; f < NFRAMES; f++) {
        frames[f] = Alloc(cbIn);
        cram[f] = Alloc(cbMax + 16);
        MakeFrame(&biIn.bi, frames[f], f);
    }

    l = Send(ICM_COMPRESS_BEGIN, (LONG)(LPVOID)&biIn, (LONG)(LPVOID)&biOut);
    RecL("cbegin", l);

    for (f = 0; f < NFRAMES; f++) {
        _fmemset(&icc, 0, sizeof(icc));
        icc.dwFlags = (f == 0) ? 1 /* ICCOMPRESS_KEYFRAME */ : 0;
        icc.lpbiOutput = &biOut.bi;
        icc.lpOutput = cram[f];
        icc.lpbiInput = &biIn.bi;
        icc.lpInput = frames[f];
        icc.lpckid = &ckid;
        icc.lpdwFlags = &dwFlags;
        icc.lFrameNum = f;
        icc.dwFrameSize = 0;
        icc.dwQuality = quality;
        icc.lpbiPrev = (fTemporal && f) ? &biIn.bi : NULL;
        icc.lpPrev = (fTemporal && f) ? frames[f - 1] : NULL;
        ckid = dwFlags = 0xDEADBEEFUL;
        l = Send(ICM_COMPRESS, (LONG)(LPVOID)&icc, sizeof(icc));
        RecL("compress", l);
        RecL("ckid", ckid);
        RecL("flags", dwFlags);
        Rec("biOutAfter", &biOut.bi, sizeof(BITMAPINFOHEADER));
        cbCram[f] = biOut.bi.biSizeImage;
        Rec("cram", cram[f], cbCram[f] < cbMax + 16 ? cbCram[f] : cbMax + 16);
    }

    l = Send(ICM_COMPRESS_END, 0, 0);
    RecL("cend", l);

    /* decompress to every depth / stretch, recording failures too */
    for (dbpp = 8; dbpp <= 32; dbpp += 8) {
        for (stretch = 1; stretch <= 2; stretch++) {
            DWORD cbDec;

            biDec = biOut;
            biDec.bi.biCompression = BI_RGB;
            biDec.bi.biBitCount = dbpp;
            biDec.bi.biWidth = biOut.bi.biWidth * stretch;
            biDec.bi.biHeight = biOut.bi.biHeight * stretch;
            biDec.bi.biSizeImage = 0;
            biDec.bi.biClrUsed = (dbpp == 8) ? 256 : 0;

            Tag("dq%d.x%d", dbpp, stretch, 0, 0);
            l = Send(ICM_DECOMPRESS_QUERY, (LONG)(LPVOID)&biOut, (LONG)(LPVOID)&biDec);
            RecL(achTag, l);
            if (l != ICERR_OK)
                continue;

            if (dbpp == 8) {
                l = Send(ICM_DECOMPRESS_GET_PALETTE, (LONG)(LPVOID)&biOut, (LONG)(LPVOID)&biDec);
                RecL("getpal", l);
                Rec("pal", biDec.pal, sizeof(biDec.pal));
            }

            l = Send(ICM_DECOMPRESS_BEGIN, (LONG)(LPVOID)&biOut, (LONG)(LPVOID)&biDec);
            RecL("dbegin", l);
            RecL("dsizeimage", biDec.bi.biSizeImage);
            cbDec = Stride((int)biDec.bi.biWidth, dbpp) * biDec.bi.biHeight;
            dec = Alloc(cbDec + 64);

            for (f = 0; f < NFRAMES; f++) {
                /* keep the previous output: skips leave pixels alone */
                biOut.bi.biSizeImage = cbCram[f];
                _fmemset(&icd, 0, sizeof(icd));
                icd.lpbiInput = &biOut.bi;
                icd.lpInput = cram[f];
                icd.lpbiOutput = &biDec.bi;
                icd.lpOutput = dec;
                l = Send(ICM_DECOMPRESS, (LONG)(LPVOID)&icd, sizeof(icd));
                RecL("decompress", l);
                Rec("pixels", dec, cbDec + 64);
            }

            l = Send(ICM_DECOMPRESS_END, 0, 0);
            RecL("dend", l);
            Free(dec);
        }
    }

    for (f = 0; f < NFRAMES; f++) {
        Free(frames[f]);
        Free(cram[f]);
    }
}

static void RunTests(void)
{
    static const int    aSize[][2] = { { 64, 48 }, { 101, 75 }, { 168, 124 } };
    static const int    aIn[] = { 8, 16, 24, 32 };
    static const LONG   aQ[] = { -1, 7500, 2500, 9800, 0 };
    ICINFO              ici;
    ICOPEN              ico;
    WORD                st;
    DWORD               dw;
    LONG                l;
    int                 s, i, q;

    RecL("drvload", (*pfnDriverProc)(0, (HDRVR)1, DRV_LOAD, 0, 0));
    RecL("drvenable", (*pfnDriverProc)(0, (HDRVR)1, DRV_ENABLE, 0, 0));
    RecL("open0", (*pfnDriverProc)(0, (HDRVR)1, DRV_OPEN, 0, 0));

    _fmemset(&ico, 0, sizeof(ico));
    ico.dwSize = sizeof(ico);
    ico.fccType = mmioFOURCC('v','i','d','c');
    ico.fccHandler = mmioFOURCC('M','S','V','C');
    ico.dwVersion = ICVERSION;
    ico.dwFlags = 3;
    dwID = (*pfnDriverProc)(0, (HDRVR)1, DRV_OPEN, 0, (LONG)(LPVOID)&ico);
    RecL("open", dwID ? 1 : 0);
    RecL("openerr", ico.dwError);
    if (!dwID)
        return;

    _fmemset(&ici, 0, sizeof(ici));
    RecL("getinfo", Send(ICM_GETINFO, (LONG)(LPVOID)&ici, sizeof(ici)));
    Rec("icinfo", &ici, sizeof(ici));
    RecL("getstate0", Send(ICM_GETSTATE, 0, 0));
    st = 0;
    RecL("getstate", Send(ICM_GETSTATE, (LONG)(LPVOID)&st, sizeof(st)));
    RecL("state", st);
    dw = 0;
    RecL("defquality", Send(ICM_GETDEFAULTQUALITY, (LONG)(LPVOID)&dw, 0));
    RecL("defq", dw);
    RecL("qconfigure", Send(ICM_CONFIGURE, -1, 0));
    RecL("qabout", Send(ICM_ABOUT, -1, 0));
    RecL("drawbegin", Send(ICM_DRAW_BEGIN, 0, 0));
    RecL("unknown", Send(ICM_USER + 99, 0, 0));

    for (s = 0; s < 3; s++)
        for (i = 0; i < 4; i++)
            for (q = 0; q < 5; q++) {
                TestSequence(aSize[s][0], aSize[s][1], aIn[i], 16, aQ[q], q != 4);
                TestSequence(aSize[s][0], aSize[s][1], aIn[i], 8,  aQ[q], q != 4);
            }

    /* a different temporal ratio */
    st = 40;
    RecL("setstate", Send(ICM_SETSTATE, (LONG)(LPVOID)&st, sizeof(st)));
    TestSequence(101, 75, 24, 16, 6000, TRUE);
    TestSequence(101, 75, 8, 8, 6000, TRUE);

    RecL("close", (*pfnDriverProc)(dwID, (HDRVR)1, DRV_CLOSE, 0, 0));
    RecL("disable", (*pfnDriverProc)(0, (HDRVR)1, DRV_DISABLE, 0, 0));
    RecL("free", (*pfnDriverProc)(0, (HDRVR)1, DRV_FREE, 0, 0));
}

int PASCAL WinMain(HINSTANCE hInst, HINSTANCE hPrev, LPSTR lpCmd, int nShow)
{
    char        achDrv[128], achOut[128];
    HINSTANCE   hLib;
    LPSTR       p;
    int         n;

    /* "<drv> <out>" */
    p = lpCmd;
    for (n = 0; *p && *p != ' ' && n < 127; n++) achDrv[n] = *p++;
    achDrv[n] = 0;
    while (*p == ' ') p++;
    for (n = 0; *p && *p != ' ' && n < 127; n++) achOut[n] = *p++;
    achOut[n] = 0;

    hOut = _lcreat(achOut, 0);
    lstrcpy(achLog, achOut);                    /* X.OUT -> X.LOG */
    for (p = achLog + lstrlen(achLog); p > achLog && p[-1] != '.' && p[-1] != '\\'; p--)
        ;
    if (p > achLog && p[-1] == '.')
        p[-1] = 0;
    lstrcat(achLog, ".LOG");
    _lclose(_lcreat(achLog, 0));
    if (hOut != HFILE_ERROR) {
        SetErrorMode(SEM_NOOPENFILEERRORBOX);
        hLib = LoadLibrary(achDrv);
        RecL("loadlib", hLib >= HINSTANCE_ERROR ? 0 : (LONG)(UINT)hLib);
        if (hLib >= HINSTANCE_ERROR) {
            pfnDriverProc = (DRIVERPROC)GetProcAddress(hLib, "DriverProc");
            RecL("getproc", pfnDriverProc ? 0 : 1);
            if (pfnDriverProc)
                RunTests();
            FreeLibrary(hLib);
        }
        Rec("end", NULL, 0);
        _lclose(hOut);
    }

    ExitWindows(0, 0);
    return 0;
}
