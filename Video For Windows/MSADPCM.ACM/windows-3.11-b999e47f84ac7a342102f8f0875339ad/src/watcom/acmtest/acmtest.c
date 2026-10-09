/*
 * acmtest.c - A/B test harness for MSADPCM.ACM builds (Win16, Open Watcom)
 *
 * usage (from DOS):  win acmtest <codec.acm> <output file>
 *
 * Loads the codec with LoadLibrary and talks to its DriverProc directly
 * (no MSACM.DLL needed), runs a fixed, deterministic set of ACM driver
 * requests: driver, format tag and format details, format suggestions,
 * stream open and size queries, PCM -> ADPCM conversions with both
 * encoders, ADPCM -> PCM conversions of the results and of fuzzed ADPCM
 * data.  Every result (return codes, structures, output buffers including
 * a guard area after the expected end) goes to the output file as tagged
 * records.  Running it against the original driver and a rebuilt one and
 * comparing the two files shows whether the rebuild behaves identically.
 * Exits Windows when done.
 *
 * Tags starting with "fz7" are fuzzed blocks with predictor 7, for which
 * the original decoder reads uninitialised stack; they are expected to
 * differ.
 */

#include <windows.h>
#include <mmsystem.h>
#include <string.h>
#include <toolhelp.h>
#include "mmreg.h"
#include "msacm.h"
#include "msacmdrv.h"

typedef LRESULT (FAR PASCAL *DRVPROC)(DWORD, HDRVR, UINT, LPARAM, LPARAM);

static DRVPROC      pfnDriverProc;
static DWORD        dwID;
static HFILE        hOut = HFILE_ERROR;
static WORD         wSeq;
static char         achLog[128];            /* progress log, reopened per line */

#define GUARD       256                     /* bytes checked after each output */
#define FILLBYTE    0xA5

static const int    aiCoef1[7] = { 256,  512,  0, 192, 240,  460,  392 };
static const int    aiCoef2[7] = {   0, -256,  0,  64,   0, -208, -232 };
static const DWORD  adwRate[4] = { 8000, 11025, 22050, 44100 };

/* ------------------------------------------------------------------ */
/* output                                                              */
/* ------------------------------------------------------------------ */

static void Rec(const char FAR *tag, const void _huge *data, DWORD cb)
{
    char hdr[24];

    _fmemset(hdr, 0, sizeof(hdr));
    lstrcpyn(hdr, tag, 16);
    *(WORD FAR *)(hdr + 16) = wSeq++;
    *(DWORD FAR *)(hdr + 20) = cb;
    _lwrite(hOut, hdr, sizeof(hdr));
    if (cb)
        _hwrite(hOut, (const char _huge *)data, cb);
}

static void Log(const char FAR *tag, LONG l)
{
    char    ach[64];
    HFILE   h;

    /* host-side writes are buffered, so a hang would lose the tail of the
     * output; this small log is closed after every line */
    if (achLog[0] && (h = _lopen(achLog, OF_WRITE)) != HFILE_ERROR) {
        _llseek(h, 0, 2);
        wsprintf(ach, "%u %s %ld\r\n", wSeq - 1, tag, l);
        _lwrite(h, ach, lstrlen(ach));
        _lclose(h);
    }
}

static void RecL(const char FAR *tag, LONG l)
{
    Rec(tag, &l, sizeof(l));
}

static LRESULT Send(UINT msg, LPARAM l1, LPARAM l2)
{
    return (*pfnDriverProc)(dwID, (HDRVR)1, msg, l1, l2);
}

static void _huge *Alloc(DWORD cb)
{
    return GlobalLock(GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, cb ? cb : 1));
}

static void Free(void _huge *p)
{
    if (p) {
        HGLOBAL h = (HGLOBAL)LOWORD(GlobalHandle(SELECTOROF(p)));
        GlobalUnlock(h);
        GlobalFree(h);
    }
}

static void Fill(void _huge *p, DWORD cb, BYTE b)
{
    BYTE _huge *pb = (BYTE _huge *)p;
    while (cb--)
        *pb++ = b;
}

/* ------------------------------------------------------------------ */
/* formats                                                             */
/* ------------------------------------------------------------------ */

typedef struct {
    WAVEFORMATEX    wfx;
    WORD            wSamplesPerBlock;
    WORD            wNumCoef;
    ADPCMCOEFSET    aCoef[7];
    BYTE            abPad[14];              /* 64 bytes in all */
} ADPCMFMT;

static void MakePCM(LPWAVEFORMATEX p, DWORD rate, WORD ch, WORD bits)
{
    _fmemset(p, 0, sizeof(*p));
    p->wFormatTag      = WAVE_FORMAT_PCM;
    p->nChannels       = ch;
    p->nSamplesPerSec  = rate;
    p->wBitsPerSample  = bits;
    p->nBlockAlign     = (WORD)((bits / 8) * ch);
    p->nAvgBytesPerSec = rate * p->nBlockAlign;
}

static WORD StdBlockAlign(DWORD rate, WORD ch)
{
    WORD ba = (WORD)(256 * ch);
    if (rate > 11025)
        ba *= (WORD)(rate / 11000);
    return ba;
}

/* ADPCM format with the given block size and samples per block (0: matching) */
static void MakeADPCM(ADPCMFMT FAR *p, DWORD rate, WORD ch, WORD ba, WORD spb)
{
    int i;

    _fmemset(p, 0, sizeof(*p));
    if (spb == 0)
        spb = (WORD)((ba - 7 * ch) * 8 / (4 * ch) + 2);
    p->wfx.wFormatTag      = WAVE_FORMAT_ADPCM;
    p->wfx.nChannels       = ch;
    p->wfx.nSamplesPerSec  = rate;
    p->wfx.nBlockAlign     = ba;
    p->wfx.wBitsPerSample  = 4;
    p->wfx.cbSize          = 32;
    p->wfx.nAvgBytesPerSec = rate * ba / spb;
    p->wSamplesPerBlock    = spb;
    p->wNumCoef            = 7;
    for (i = 0; i < 7; i++) {
        p->aCoef[i].iCoef1 = (short)aiCoef1[i];
        p->aCoef[i].iCoef2 = (short)aiCoef2[i];
    }
}

/* ------------------------------------------------------------------ */
/* test signals                                                        */
/* ------------------------------------------------------------------ */

static DWORD dwRand;

static WORD Rnd(void)
{
    dwRand = dwRand * 1103515245UL + 12345UL;
    return (WORD)(dwRand >> 16);
}

/* smooth periodic wave from a 16-bit phase (two parabolas) */
static int Wave(WORD phase, int amp)
{
    long t = phase & 0x7FFF;
    long y = (t * (32768L - t)) >> 13;      /* 0..32768 */
    y = (y * amp) >> 15;
    return (int)((phase & 0x8000) ? -y : y);
}

static int Clip(long l)
{
    return (l > 32767) ? 32767 : (l < -32768L) ? -32768 : (int)l;
}

typedef struct {
    WORD    wPhase;
    WORD    wPhase2;
    int     iSq;
    int     nSq;
} SIGSTATE;

/*
 * 16-bit samples that take the real-time encoder's step size to -32757:
 * the product shifted right by 8 lands in 32768..32783, which the
 * assembly's 16-bit clamp test lets through (found by a search over the
 * encoder's arithmetic)
 */
static const int aiCraft[13] = { 0, 0, -16896, -18944, -25600, -9281, 27519,
                                 21836, -16834, 15851, 32207, -32455, 23086 };

static int Sample(int sig, DWORD i, int c, SIGSTATE *s)
{
    long l;

    if (sig == 8) {                         /* crafted start, then the mix */
        if (c == 0 && i < 13)
            return aiCraft[(int)i];
        sig = 7;
    }

    switch (sig) {
    case 0:                                 /* silence */
        return 0;
    case 1:                                 /* sweep */
        s->wPhase += (WORD)(150 + (i >> 4) + c * 37);
        return Wave(s->wPhase, 30000);
    case 2:                                 /* full-scale noise */
        return (int)Rnd();
    case 3:                                 /* full-scale square, varying period */
        if (--s->nSq <= 0) {
            s->iSq = s->iSq > 0 ? -32768 : 32767;
            s->nSq = 3 + (int)((i / 97 + c * 5) % 23);
        }
        return s->iSq;
    case 4:                                 /* DC offset and low noise */
        return 1000 - c * 3000 + (int)(Rnd() & 63) - 32;
    case 5:                                 /* sparse impulses */
        if ((i + c * 11) % 37 == 0)
            return ((i / 37) & 1) ? -32768 : 32767;
        return 0;
    case 6:                                 /* full-scale Nyquist */
        return ((i + c) & 1) ? -32768 : 32767;
    default:                                /* mix */
        s->wPhase  += (WORD)(300 + c * 50);
        s->wPhase2 += (WORD)(2900 - c * 100);
        l = (long)Wave(s->wPhase, 16000) + Wave(s->wPhase2, 9000) + (int)(Rnd() & 511) - 256;
        if ((i & 1023) < 40)
            l *= 3;
        return Clip(l);
    }
}

static void MakeSignal(BYTE _huge *pb, DWORD cFrames, WORD ch, WORD bits, int sig)
{
    SIGSTATE    s[2];
    DWORD       i;
    int         c, v;

    _fmemset(s, 0, sizeof(s));
    dwRand = 0x1234567UL + sig;
    for (i = 0; i < cFrames; i++)
        for (c = 0; c < ch; c++) {
            v = Sample(sig, i, c, &s[c]);
            if (bits == 8) {
                *pb++ = (BYTE)(((UINT)v >> 8) ^ 0x80);
            } else {
                *(short _huge *)pb = (short)v;
                pb += 2;
            }
        }
}

/* ------------------------------------------------------------------ */
/* stream helpers                                                      */
/* ------------------------------------------------------------------ */

static LRESULT Open(ACMDRVSTREAMINSTANCE FAR *psi, LPWAVEFORMATEX src, LPWAVEFORMATEX dst, DWORD fdwOpen)
{
    _fmemset(psi, 0, sizeof(*psi));
    psi->cbStruct = sizeof(*psi);
    psi->pwfxSrc  = src;
    psi->pwfxDst  = dst;
    psi->fdwOpen  = fdwOpen;
    return Send(ACMDM_STREAM_OPEN, (LPARAM)psi, 0);
}

static LRESULT Size(ACMDRVSTREAMINSTANCE FAR *psi, DWORD fdw, DWORD cbSrc, DWORD cbDst,
                    DWORD FAR *pcbSrc, DWORD FAR *pcbDst)
{
    ACMDRVSTREAMSIZE    ss;
    LRESULT             lr;

    _fmemset(&ss, 0, sizeof(ss));
    ss.cbStruct    = sizeof(ss);
    ss.fdwSize     = fdw;
    ss.cbSrcLength = cbSrc;
    ss.cbDstLength = cbDst;
    lr = Send(ACMDM_STREAM_SIZE, (LPARAM)psi, (LPARAM)(LPVOID)&ss);
    *pcbSrc = ss.cbSrcLength;
    *pcbDst = ss.cbDstLength;
    return lr;
}

/* convert; records return, used lengths and the output plus guard */
static DWORD Convert(const char *tag, ACMDRVSTREAMINSTANCE FAR *psi, void _huge *src, DWORD cbSrc,
                     void _huge *dst, DWORD cbDst, DWORD fdwConvert)
{
    ACMDRVSTREAMHEADER  sh;
    char                ach[16];
    LRESULT             lr;

    Fill(dst, cbDst + GUARD, FILLBYTE);
    _fmemset(&sh, 0, sizeof(sh));
    sh.cbStruct    = sizeof(sh);
    sh.pbSrc       = (LPBYTE)src;
    sh.cbSrcLength = cbSrc;
    sh.pbDst       = (LPBYTE)dst;
    sh.cbDstLength = cbDst;
    sh.fdwConvert  = fdwConvert;
    lr = Send(ACMDM_STREAM_CONVERT, (LPARAM)psi, (LPARAM)(LPVOID)&sh);
    wsprintf(ach, "%s.r", (LPSTR)tag);  RecL(ach, lr);
    wsprintf(ach, "%s.us", (LPSTR)tag); RecL(ach, sh.cbSrcLengthUsed);
    wsprintf(ach, "%s.ud", (LPSTR)tag); RecL(ach, sh.cbDstLengthUsed);
    wsprintf(ach, "%s.d", (LPSTR)tag);  Rec(ach, dst, cbDst + GUARD);
    Log(tag, sh.cbDstLengthUsed);
    return (lr == 0) ? sh.cbDstLengthUsed : 0;
}

/* decode ADPCM data to 8 and 16 bits, whole and cut, with and without BLOCKALIGN */
static void DecodeAll(const char *tag, ADPCMFMT FAR *pad, void _huge *src, DWORD cbSrc)
{
    WAVEFORMATEX            pcm;
    ACMDRVSTREAMINSTANCE    si;
    DWORD                   cbDst, cbOwn, d1, d2, cbCut;
    void _huge             *dst;
    char                    ach[16];
    int                     bits, k;

    for (bits = 8; bits <= 16; bits += 8) {
        MakePCM(&pcm, pad->wfx.nSamplesPerSec, pad->wfx.nChannels, (WORD)bits);
        wsprintf(ach, "%s.o%d", (LPSTR)tag, bits);
        RecL(ach, Open(&si, &pad->wfx, &pcm, 0));
        for (k = 0; k < 3; k++) {
            cbCut = (k == 1 && cbSrc > 37) ? cbSrc - 37 : cbSrc;
            wsprintf(ach, "%s.s%d%d", (LPSTR)tag, bits, k);
            RecL(ach, Size(&si, ACM_STREAMSIZEF_SOURCE, cbCut, 0, &d1, &d2));
            RecL(ach, d2);
            cbOwn = (cbCut / pad->wfx.nBlockAlign + 1) * pad->wSamplesPerBlock * pcm.nBlockAlign;
            cbDst = (d2 > cbOwn) ? d2 : cbOwn;
            dst = Alloc(cbDst + GUARD);
            wsprintf(ach, "%s.c%d%d", (LPSTR)tag, bits, k);
            Convert(ach, &si, src, cbCut, dst, cbDst, k == 2 ? ACM_STREAMCONVERTF_BLOCKALIGN : 0);
            Free(dst);
        }
        Send(ACMDM_STREAM_CLOSE, (LPARAM)(LPVOID)&si, 0);
    }
}

/* ------------------------------------------------------------------ */
/* conversions                                                         */
/* ------------------------------------------------------------------ */

static void TestEncode(const char *tag, DWORD rate, WORD ch, WORD bits, WORD ba, WORD spb,
                       DWORD fdwOpen, int sig, DWORD cFrames, DWORD fdwConvert, BOOL fDecode)
{
    WAVEFORMATEX            pcm;
    ADPCMFMT                ad;
    ACMDRVSTREAMINSTANCE    si;
    DWORD                   cbSrc, cbDst, d1, d2, cbOut;
    void _huge             *src;
    void _huge             *dst;
    char                    ach[16];

    MakePCM(&pcm, rate, ch, bits);
    MakeADPCM(&ad, rate, ch, ba, spb);
    cbSrc = cFrames * pcm.nBlockAlign;

    RecL("enc", (LONG)rate * 1000 + ch * 100 + bits);
    RecL("enc.p", (LONG)sig << 24 | (LONG)fdwOpen << 16 | fdwConvert);
    RecL("enc.n", cFrames);
    wsprintf(ach, "%s.o", (LPSTR)tag);
    RecL(ach, Open(&si, &pcm, &ad.wfx, fdwOpen));
    RecL("enc.drv", si.dwDriver != 0);
    wsprintf(ach, "%s.s", (LPSTR)tag);
    RecL(ach, Size(&si, ACM_STREAMSIZEF_SOURCE, cbSrc, 0, &d1, &d2));
    RecL(ach, d2);

    /* generous: the encoder ignores cbDstLength */
    cbDst = cbSrc / 2 + (DWORD)(cFrames / (spb ? spb : 2) + 2) * 16 * ch + 64;
    if (d2 > cbDst)
        cbDst = d2;
    src = Alloc(cbSrc + GUARD);
    dst = Alloc(cbDst + GUARD);
    MakeSignal((BYTE _huge *)src, cFrames, ch, bits, sig);

    cbOut = Convert(tag, &si, src, cbSrc, dst, cbDst, fdwConvert);
    Send(ACMDM_STREAM_CLOSE, (LPARAM)(LPVOID)&si, 0);

    if (fDecode && cbOut)
        DecodeAll(tag, &ad, dst, cbOut);

    Free(src);
    Free(dst);
}

/*
 * Fuzzed ADPCM: random headers and data.  mode 0: deltas 16..2047,
 * 1: any 16-bit delta, 2: bad predictor (9) in block 2, 3: predictor 7,
 * 4: delta 10923..10927 followed by code -8, which takes the step size
 * into the decoder's 16-bit wrap window.
 */
static void TestFuzz(const char *tag, DWORD rate, WORD ch, WORD ba, WORD spb, int mode,
                     WORD seed, DWORD cBlocks, WORD cbPartial)
{
    ADPCMFMT                ad;
    DWORD                   cb, b;
    BYTE _huge             *src;
    BYTE _huge             *p;
    WORD                    i, c;

    MakeADPCM(&ad, rate, ch, ba, spb);
    cb = cBlocks * ba + cbPartial;
    src = (BYTE _huge *)Alloc(cb + GUARD);
    dwRand = 0xC0FFEEUL ^ ((DWORD)seed << 8);
    for (b = 0, p = src; b * ba < cb; b++) {
        BYTE _huge *blk = p;
        for (i = 0; i < ba && (DWORD)(p - src) < cb; i++)
            *p++ = (BYTE)Rnd();
        for (c = 0; c < ch && (DWORD)(blk - src) + 7 * ch <= cb; c++) {
            short d;
            blk[c] = (BYTE)(Rnd() % 7);
            if (mode == 2 && b == 2)
                blk[c] = 9;
            if (mode == 3 && b == 1)
                blk[c] = 7;
            d = (short)Rnd();
            if (mode != 1)
                d = 16 + (d & 2031);
            if (mode == 4)
                d = (short)(10923 + (b + c) % 5);
            *(short _huge *)(blk + ch + 2 * c) = d;
        }
        if (mode == 4 && (DWORD)(blk - src) + 7 * ch < cb)
            blk[7 * ch] = 0x88;
    }
    RecL("fuzz", (LONG)rate * 1000 + ch * 100 + mode);
    RecL("fuzz.n", cb);
    DecodeAll(tag, &ad, src, cb);
    Free(src);
}

/* ------------------------------------------------------------------ */
/* message tests                                                       */
/* ------------------------------------------------------------------ */

static void TestDetails(void)
{
    static const DWORD  acb[] = { 0x396, 0x400, 0x24, 0x25, 0x26, 0xC6, 0xC7, 0x10, 4, 0 };
    static const DWORD  aTag[] = { 0, 1, 2, 3, 0x10002UL, 0xFFFFFFFFUL };
    static const DWORD  aIdx[] = { 0, 1, 2, 0x10000UL, 0xFFFFFFFFUL };
    static const DWORD  aFdw[] = { 0, 1, 2, 3, 0x10, 0x11, 0xF };
    BYTE _huge         *buf;
    ACMDRIVERDETAILS FAR *padd;
    ACMFORMATTAGDETAILS FAR *paft;
    int                 i, j, k;

    buf = (BYTE _huge *)Alloc(0x400);
    padd = (ACMDRIVERDETAILS FAR *)buf;
    for (i = 0; i < sizeof(acb) / sizeof(acb[0]); i++) {
        Fill(buf, 0x400, 0xCC);
        padd->cbStruct = acb[i];
        RecL("dd.r", Send(ACMDM_DRIVER_DETAILS, (LPARAM)(LPVOID)padd, 0));
        Rec("dd.d", buf, 0x400);
    }

    paft = (ACMFORMATTAGDETAILS FAR *)buf;
    for (i = 0; i < sizeof(aFdw) / sizeof(aFdw[0]); i++)
        for (j = 0; j < 6; j++)
            for (k = 0; k < 3; k++) {
                Fill(buf, 0x80, 0xCC);
                paft->cbStruct = k == 0 ? 0x48 : k == 1 ? 0x60 : 0x20;
                paft->dwFormatTag = aTag[j];
                paft->dwFormatTagIndex = aIdx[j < 5 ? j : 4];
                RecL("ftd.r", Send(ACMDM_FORMATTAG_DETAILS, (LPARAM)(LPVOID)paft, aFdw[i]));
                Rec("ftd.d", buf, 0x80);
            }
    Free(buf);
}

/* formats used for format details, suggestions and stream opens */
#define NFMT 48
static BYTE afmt[NFMT][64];

static void BuildFormats(void)
{
    static const DWORD aOdd[] = { 5512, 32000, 48000, 96000, 65535UL, 65536UL, 1000000UL, 2000000UL };
    LPWAVEFORMATEX  p;
    ADPCMFMT FAR   *pa;
    int             n = 0, i;

    _fmemset(afmt, 0xCC, sizeof(afmt));
    /* 0-7: standard PCM, a mix of rates, channels, bits */
    for (i = 0; i < 8; i++)
        MakePCM((LPWAVEFORMATEX)afmt[n++], adwRate[i & 3], (WORD)(1 + (i >> 2)), (WORD)((i & 1) ? 16 : 8));
    /* 8-15: odd rates, 16-bit, mono and stereo */
    for (i = 0; i < 8; i++)
        MakePCM((LPWAVEFORMATEX)afmt[n++], aOdd[i], (WORD)(1 + (i & 1)), 16);
    /* 16-21: invalid PCM */
    p = (LPWAVEFORMATEX)afmt[n++]; MakePCM(p, 22050, 1, 16); p->wBitsPerSample = 12;
    p = (LPWAVEFORMATEX)afmt[n++]; MakePCM(p, 22050, 3, 16);
    p = (LPWAVEFORMATEX)afmt[n++]; MakePCM(p, 22050, 2, 16); p->nBlockAlign = 2;
    p = (LPWAVEFORMATEX)afmt[n++]; MakePCM(p, 22050, 1, 8);  p->nAvgBytesPerSec++;
    p = (LPWAVEFORMATEX)afmt[n++]; MakePCM(p, 22050, 1, 8);  p->wFormatTag = 3;
    p = (LPWAVEFORMATEX)afmt[n++]; MakePCM(p, 22050, 0, 8);
    /* 22-29: standard ADPCM */
    for (i = 0; i < 8; i++) {
        pa = (ADPCMFMT FAR *)afmt[n++];
        MakeADPCM(pa, adwRate[i >> 1], (WORD)(1 + (i & 1)), StdBlockAlign(adwRate[i >> 1], (WORD)(1 + (i & 1))), 0);
    }
    /* 30-33: odd ADPCM that still has the standard coefficients */
    MakeADPCM((ADPCMFMT FAR *)afmt[n++], 8000, 1, 512, 0);
    MakeADPCM((ADPCMFMT FAR *)afmt[n++], 48000, 2, 4096, 0);
    MakeADPCM((ADPCMFMT FAR *)afmt[n++], 22050, 1, 300, 500);   /* inconsistent */
    MakeADPCM((ADPCMFMT FAR *)afmt[n++], 11025, 2, 512, 501);   /* odd spb */
    /* 34-41: invalid ADPCM */
    pa = (ADPCMFMT FAR *)afmt[n++]; MakeADPCM(pa, 22050, 1, 512, 0); pa->aCoef[3].iCoef2 = 65;
    pa = (ADPCMFMT FAR *)afmt[n++]; MakeADPCM(pa, 22050, 1, 512, 0); pa->wfx.cbSize = 31;
    pa = (ADPCMFMT FAR *)afmt[n++]; MakeADPCM(pa, 22050, 1, 512, 0); pa->wNumCoef = 6;
    pa = (ADPCMFMT FAR *)afmt[n++]; MakeADPCM(pa, 22050, 1, 512, 0); pa->wfx.wBitsPerSample = 3;
    pa = (ADPCMFMT FAR *)afmt[n++]; MakeADPCM(pa, 22050, 3, 768, 0);
    pa = (ADPCMFMT FAR *)afmt[n++]; MakeADPCM(pa, 22050, 1, 512, 0); pa->wNumCoef = 8;
    pa = (ADPCMFMT FAR *)afmt[n++]; MakeADPCM(pa, 22050, 1, 512, 0); pa->wfx.cbSize = 40;
    pa = (ADPCMFMT FAR *)afmt[n++]; MakeADPCM(pa, 22050, 0, 512, 7);
    /* 42-47: other tags, and a zeroed format */
    p = (LPWAVEFORMATEX)afmt[n++]; MakePCM(p, 8000, 1, 8); p->wFormatTag = 0;
    p = (LPWAVEFORMATEX)afmt[n++]; MakePCM(p, 8000, 1, 8); p->wFormatTag = 0x11;
    p = (LPWAVEFORMATEX)afmt[n++]; MakePCM(p, 8000, 1, 8); p->wFormatTag = 0x102;
    _fmemset(afmt[n++], 0, 64);
    MakePCM((LPWAVEFORMATEX)afmt[n++], 8000, 1, 16);
    MakePCM((LPWAVEFORMATEX)afmt[n++], 44100, 2, 16);
}

static void TestFormatDetails(void)
{
    static const DWORD  aIdx[] = { 0, 1, 2, 5, 7, 8, 9, 15, 16, 17, 0x10000UL, 0xFFFFFFFFUL };
    static const DWORD  aTag[] = { 0, 1, 2, 3, 0x10001UL };
    ACMFORMATDETAILS    afd;
    BYTE                wfx[64];
    int                 i, j, k;

    for (k = 0; k < 2; k++)                         /* cbStruct 0x98, 0x40 */
        for (i = 0; i < 5; i++)
            for (j = 0; j < 12; j++) {
                _fmemset(&afd, 0xCC, sizeof(afd));
                _fmemset(wfx, 0xCC, sizeof(wfx));
                afd.cbStruct      = k ? 0x40 : sizeof(afd);
                afd.dwFormatTag   = aTag[i];
                afd.dwFormatIndex = aIdx[j];
                afd.pwfx          = (LPWAVEFORMATEX)wfx;
                afd.cbwfx         = sizeof(wfx);
                RecL("fd.r", Send(ACMDM_FORMAT_DETAILS, (LPARAM)(LPVOID)&afd, ACM_FORMATDETAILSF_INDEX));
                Rec("fd.a", &afd, sizeof(afd));
                Rec("fd.w", wfx, sizeof(wfx));
            }

    for (i = 0; i < NFMT; i++)
        for (k = 1; k <= 3; k++) {
            _fmemset(&afd, 0xCC, sizeof(afd));
            _fmemcpy(wfx, afmt[i], sizeof(wfx));
            afd.cbStruct    = sizeof(afd);
            afd.dwFormatTag = ((LPWAVEFORMATEX)wfx)->wFormatTag;
            afd.pwfx        = (LPWAVEFORMATEX)wfx;
            afd.cbwfx       = sizeof(wfx);
            RecL("fdf.r", Send(ACMDM_FORMAT_DETAILS, (LPARAM)(LPVOID)&afd, k));
            Rec("fdf.a", &afd, sizeof(afd));
            Rec("fdf.w", wfx, sizeof(wfx));
        }
}

static void TestSuggest(void)
{
    static const DWORD  aFlags[] = { 0, ACM_FORMATSUGGESTF_WFORMATTAG, ACM_FORMATSUGGESTF_NCHANNELS,
                                     ACM_FORMATSUGGESTF_NSAMPLESPERSEC, ACM_FORMATSUGGESTF_WBITSPERSAMPLE,
                                     ACM_FORMATSUGGESTF_TYPEMASK & 0x000F0000UL };
    ACMDRVFORMATSUGGEST fs;
    BYTE                dst[64];
    LPWAVEFORMATEX      ps, pd;
    int                 i, f, v;

    for (i = 0; i < NFMT; i++) {
        ps = (LPWAVEFORMATEX)afmt[i];
        for (f = 0; f < 6; f++)
            for (v = 0; v < 4; v++) {
                _fmemset(dst, 0xCC, sizeof(dst));
                pd = (LPWAVEFORMATEX)dst;
                switch (v) {
                case 0:                             /* zeroed */
                    _fmemset(dst, 0, 18);
                    break;
                case 1:                             /* what the driver would pick */
                    pd->wFormatTag     = ps->wFormatTag == 1 ? 2 : 1;
                    pd->nChannels      = ps->nChannels;
                    pd->nSamplesPerSec = ps->nSamplesPerSec;
                    pd->wBitsPerSample = ps->wFormatTag == 1 ? 4 : 16;
                    break;
                case 2:                             /* the other way, 8-bit */
                    pd->wFormatTag     = ps->wFormatTag == 1 ? 2 : 1;
                    pd->nChannels      = ps->nChannels;
                    pd->nSamplesPerSec = ps->nSamplesPerSec;
                    pd->wBitsPerSample = ps->wFormatTag == 1 ? 4 : 8;
                    break;
                default:                            /* mismatching everything */
                    pd->wFormatTag     = ps->wFormatTag;
                    pd->nChannels      = 3 - ps->nChannels;
                    pd->nSamplesPerSec = ps->nSamplesPerSec + 1;
                    pd->wBitsPerSample = 12;
                    break;
                }
                _fmemset(&fs, 0, sizeof(fs));
                fs.cbStruct   = sizeof(fs);
                fs.fdwSuggest = aFlags[f];
                fs.pwfxSrc    = ps;
                fs.cbwfxSrc   = 64;
                fs.pwfxDst    = pd;
                fs.cbwfxDst   = 64;
                RecL("sug.r", Send(ACMDM_FORMAT_SUGGEST, (LPARAM)(LPVOID)&fs, 0));
                Rec("sug.d", dst, sizeof(dst));
            }
    }
}

static void TestOpenSize(void)
{
    static const DWORD  aLen[] = { 0, 1, 2, 3, 7, 13, 14, 255, 256, 257, 511, 512, 513, 1000, 1024,
                                   2047, 2048, 4095, 65535UL, 65536UL, 100000UL, 1000000UL,
                                   0x7FFFFFFFUL, 0x80000000UL, 0xFFFFFFFFUL };
    static const DWORD  aOpen[] = { 0, ACM_STREAMOPENF_QUERY, ACM_STREAMOPENF_NONREALTIME, 0xFFFFFFFFUL };
    ACMDRVSTREAMINSTANCE si;
    DWORD               d1, d2;
    int                 i, j, k, o;

    /* every pair of formats, for the open checks */
    for (i = 0; i < NFMT; i++)
        for (j = 0; j < NFMT; j++)
            for (o = 0; o < 4; o++) {
                if (o && ((i * 7 + j * 3 + o) % 5))   /* thin out the flag variants */
                    continue;
                RecL("op.r", Open(&si, (LPWAVEFORMATEX)afmt[i], (LPWAVEFORMATEX)afmt[j], aOpen[o]));
                RecL("op.drv", si.dwDriver != 0);
                if (si.dwDriver)
                    Send(ACMDM_STREAM_CLOSE, (LPARAM)(LPVOID)&si, 0);
            }

    /* sizes for every valid PCM/ADPCM pair with the same rate and channels */
    for (i = 0; i < NFMT; i++)
        for (j = 0; j < NFMT; j++) {
            LPWAVEFORMATEX s = (LPWAVEFORMATEX)afmt[i];
            LPWAVEFORMATEX d = (LPWAVEFORMATEX)afmt[j];
            if (Open(&si, s, d, 0) != 0)
                continue;
            RecL("sz.pair", i * 100 + j);
            for (k = 0; k < sizeof(aLen) / sizeof(aLen[0]); k++)
                for (o = 0; o < 3; o++) {
                    d1 = d2 = 0xDEADBEEFUL;
                    RecL("sz.r", Size(&si, o, aLen[k], aLen[k], &d1, &d2));
                    RecL("sz.s", d1);
                    RecL("sz.d", d2);
                }
            Send(ACMDM_STREAM_CLOSE, (LPARAM)(LPVOID)&si, 0);
        }
}

static void TestMessages(void)
{
    ACMDRVOPENDESC  od;
    DWORD           id;

    RecL("load", (*pfnDriverProc)(0, (HDRVR)1, DRV_LOAD, 0, 0));
    RecL("enable", (*pfnDriverProc)(0, (HDRVR)1, DRV_ENABLE, 0, 0));
    RecL("open0", (*pfnDriverProc)(0, (HDRVR)1, DRV_OPEN, 0, 0));

    _fmemset(&od, 0, sizeof(od));
    od.cbStruct  = sizeof(od);
    od.fccType   = mmioFOURCC('v', 'i', 'd', 'c');
    od.dwVersion = 0x02010000UL;
    od.dwError   = 0x12345678UL;
    RecL("openvidc", (*pfnDriverProc)(0, (HDRVR)1, DRV_OPEN, 0, (LPARAM)(LPVOID)&od));
    RecL("openvidce", od.dwError);

    od.fccType = mmioFOURCC('a', 'u', 'd', 'c');
    od.dwFlags = 0x55AA;
    id = (*pfnDriverProc)(0, (HDRVR)1, DRV_OPEN, 0, (LPARAM)(LPVOID)&od);
    RecL("open", id ? 1 : 0);
    RecL("opene", od.dwError);
    dwID = id;

    RecL("qconfig", Send(DRV_QUERYCONFIGURE, 0, 0));
    RecL("config", Send(DRV_CONFIGURE, 0, 0));
    RecL("install", Send(DRV_INSTALL, 0, 0));
    RecL("remove", Send(DRV_REMOVE, 0, 0));
    RecL("power", Send(DRV_POWER, 0, 0));
    RecL("exitsess", Send(DRV_EXITSESSION, 0, 0));
    RecL("qabout", Send(ACMDM_DRIVER_ABOUT, -1L, 0));
    RecL("about", Send(ACMDM_DRIVER_ABOUT, 0, 0));
    RecL("notify", Send(ACMDM_DRIVER_NOTIFY, 0, 0));
    RecL("hwcaps", Send(ACMDM_HARDWARE_WAVE_CAPS_INPUT, 0, 0));
    RecL("filttag", Send(ACMDM_FILTERTAG_DETAILS, 0, 0));
    RecL("filter", Send(ACMDM_FILTER_DETAILS, 0, 0));
    RecL("reset", Send(ACMDM_STREAM_RESET, 0, 0));
    RecL("prepare", Send(ACMDM_STREAM_PREPARE, 0, 0));
    RecL("unprep", Send(ACMDM_STREAM_UNPREPARE, 0, 0));
    RecL("user", Send(ACMDM_USER + 5, 0, 0));
    RecL("user2", Send(0x7FFF, 0, 0));
    RecL("user3", Send(0xFFFF, 0, 0));
    RecL("low", Send(0x3FFF, 0, 0));
    /* the driver ID 1 (open without description) means "no instance" */
    RecL("close1", (*pfnDriverProc)(1, (HDRVR)1, DRV_CLOSE, 0, 0));
    RecL("close0", (*pfnDriverProc)(0, (HDRVR)1, DRV_CLOSE, 0, 0));
}

/* ------------------------------------------------------------------ */

static void TestLeak(void)
{
    GLOBALINFO  gi;
    WORD        w;
    int         i;

    gi.dwSize = sizeof(gi);
    GlobalInfo(&gi);
    w = gi.wcItems;
    for (i = 0; i < 10; i++)
        TestEncode("lk", 8000, 1, 16, 256, 0, ACM_STREAMOPENF_NONREALTIME, 1, 1200, 0, FALSE);
    GlobalInfo(&gi);
    RecL("leak", (LONG)gi.wcItems - w);
}

static void RunTests(void)
{
    static const DWORD aLenCases[] = { 0, 1, 2, 3, 4, 5, 6, 7, 9 };
    char    ach[16];
    int     r, c, b, k, sig, i;
    DWORD   rate, n;
    WORD    ch, bits, ba, spb;

    TestMessages();
    if (!dwID)
        return;

    TestDetails();
    BuildFormats();
    TestFormatDetails();
    TestSuggest();
    TestOpenSize();

    /* every standard configuration, both encoders, three signals each */
    for (r = 0; r < 4; r++)
        for (c = 1; c <= 2; c++)
            for (b = 8; b <= 16; b += 8)
                for (k = 0; k < 3; k++) {
                    rate = adwRate[r];
                    ch   = (WORD)c;
                    bits = (WORD)b;
                    ba   = StdBlockAlign(rate, ch);
                    spb  = (WORD)((ba - 7 * ch) * 8 / (4 * ch) + 2);
                    sig  = (r * 4 + c * 2 + b / 8 + k * 3) % 8;
                    n    = 2L * spb + spb / 3 + 1;
                    wsprintf(ach, "e%d%d%d%dr", r, c, b / 8, k);
                    TestEncode(ach, rate, ch, bits, ba, 0, 0, sig, n, k == 2 ? ACM_STREAMCONVERTF_BLOCKALIGN : 0, TRUE);
                    wsprintf(ach, "e%d%d%d%dn", r, c, b / 8, k);
                    TestEncode(ach, rate, ch, bits, ba, 0, ACM_STREAMOPENF_NONREALTIME, sig, n,
                               k == 2 ? ACM_STREAMCONVERTF_BLOCKALIGN : 0, TRUE);
                }

    /* all signals in two configurations */
    for (sig = 0; sig < 8; sig++) {
        wsprintf(ach, "a1%dr", sig);
        TestEncode(ach, 8000, 1, 16, 256, 0, 0, sig, 1500, 0, TRUE);
        wsprintf(ach, "a1%dn", sig);
        TestEncode(ach, 8000, 1, 16, 256, 0, ACM_STREAMOPENF_NONREALTIME, sig, 1500, 0, TRUE);
        wsprintf(ach, "a2%dr", sig);
        TestEncode(ach, 22050, 2, 8, 1024, 0, 0, sig, 1300, 0, TRUE);
        wsprintf(ach, "a2%dn", sig);
        TestEncode(ach, 22050, 2, 8, 1024, 0, ACM_STREAMOPENF_NONREALTIME, sig, 1300, 0, TRUE);
    }

    /* step sizes in the 16-bit wrap window */
    TestEncode("w1r", 8000, 1, 16, 256, 0, 0, 8, 1000, 0, TRUE);
    TestEncode("w1n", 8000, 1, 16, 256, 0, ACM_STREAMOPENF_NONREALTIME, 8, 1000, 0, TRUE);
    TestEncode("w2r", 22050, 2, 16, 1024, 0, 0, 8, 1000, 0, TRUE);
    TestEncode("w2n", 22050, 2, 16, 1024, 0, ACM_STREAMOPENF_NONREALTIME, 8, 1000, 0, TRUE);

    /* tiny conversions (the full-search encoder reads 5 frames regardless) */
    for (i = 0; i < sizeof(aLenCases) / sizeof(aLenCases[0]); i++) {
        wsprintf(ach, "t1%dr", i);
        TestEncode(ach, 11025, 1, 16, 256, 0, 0, 7, aLenCases[i], 0, TRUE);
        wsprintf(ach, "t1%dn", i);
        TestEncode(ach, 11025, 1, 16, 256, 0, ACM_STREAMOPENF_NONREALTIME, 7, aLenCases[i], 0, TRUE);
        wsprintf(ach, "t2%dr", i);
        TestEncode(ach, 11025, 2, 8, 512, 0, 0, 7, aLenCases[i], 0, TRUE);
        wsprintf(ach, "t2%dn", i);
        TestEncode(ach, 11025, 2, 8, 512, 0, ACM_STREAMOPENF_NONREALTIME, 7, aLenCases[i], 0, TRUE);
    }

    /* non-standard but accepted formats */
    TestEncode("x1r", 8000, 1, 16, 512, 0, 0, 1, 3000, 0, TRUE);
    TestEncode("x1n", 8000, 1, 16, 512, 0, ACM_STREAMOPENF_NONREALTIME, 1, 3000, 0, TRUE);
    TestEncode("x2r", 22050, 1, 16, 300, 500, 0, 7, 1600, 0, TRUE);
    TestEncode("x2n", 22050, 1, 16, 300, 500, ACM_STREAMOPENF_NONREALTIME, 7, 1600, 0, TRUE);
    TestEncode("x3r", 11025, 2, 16, 512, 501, 0, 3, 1600, 0, TRUE);
    TestEncode("x3n", 11025, 2, 16, 512, 501, ACM_STREAMOPENF_NONREALTIME, 3, 1600, 0, TRUE);
    TestEncode("x4r", 11025, 1, 8, 256, 101, 0, 2, 900, 0, TRUE);
    TestEncode("x4n", 11025, 1, 8, 256, 101, ACM_STREAMOPENF_NONREALTIME, 2, 900, 0, TRUE);

    /* buffers over 64K */
    TestEncode("h1r", 44100, 2, 16, 2048, 0, 0, 7, 40000, 0, TRUE);
    TestEncode("h1n", 44100, 2, 16, 2048, 0, ACM_STREAMOPENF_NONREALTIME, 7, 40000, 0, TRUE);
    TestEncode("h2r", 8000, 1, 8, 256, 0, 0, 1, 70001, 0, TRUE);
    TestEncode("h2n", 8000, 1, 8, 256, 0, ACM_STREAMOPENF_NONREALTIME, 1, 70001, 0, TRUE);

    /* fuzzed ADPCM */
    for (r = 0; r < 4; r++)
        for (c = 1; c <= 2; c++)
            for (k = 0; k < 3; k++) {
                rate = adwRate[r];
                ba = StdBlockAlign(rate, (WORD)c);
                wsprintf(ach, "fz%d%d%d", k, r, c);
                TestFuzz(ach, rate, (WORD)c, ba, 0, k, (WORD)(r * 10 + c * 3 + k), 3, (WORD)(k * 5 + c * 7 + 3));
            }
    for (r = 0; r < 4; r++)
        for (c = 1; c <= 2; c++) {
            wsprintf(ach, "fz4%d%d", r, c);
            TestFuzz(ach, adwRate[r], (WORD)c, StdBlockAlign(adwRate[r], (WORD)c), 0, 4,
                     (WORD)(r * 10 + c), 4, 0);
        }
    TestFuzz("fz0x", 22050, 1, 300, 500, 0, 77, 4, 0);
    TestFuzz("fz0y", 11025, 2, 512, 501, 1, 78, 3, 100);
    TestFuzz("fz0z", 44100, 2, 2048, 0, 1, 79, 40, 0);       /* over 64K of output */
    TestFuzz("fz71", 8000, 1, 256, 0, 3, 80, 3, 0);
    TestFuzz("fz72", 8000, 2, 512, 0, 3, 81, 3, 0);

    TestLeak();

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

    /* "<acm> <out>" */
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
            pfnDriverProc = (DRVPROC)GetProcAddress(hLib, "DriverProc");
            RecL("getproc", pfnDriverProc ? 0 : 1);
            if (pfnDriverProc)
                RunTests();
            FreeLibrary(hLib);
        }
        Rec("end", NULL, 0);
        Log("end", 0);
        _lclose(hOut);
    }

    ExitWindows(0, 0);
    return 0;
}
