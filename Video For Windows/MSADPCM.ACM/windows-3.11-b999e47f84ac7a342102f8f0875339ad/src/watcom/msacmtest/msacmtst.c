/*
 * msacmtst.c - A/B test of MSADPCM.ACM through the Audio Compression
 * Manager (Win16, Open Watcom)
 *
 * usage (from DOS):  win msacmtst <output file>
 *
 * Unlike acmtest, which calls the codec's DriverProc directly, this goes
 * through MSACM.DLL (Video for Windows 1.1) with the codec installed as
 * MSACM.msadpcm in SYSTEM.INI, the way applications use it: driver
 * enumeration and details, format tags, format enumeration (with the
 * ACM's format names), format suggestions, and streamed conversions in
 * chunks with prepared headers and the START, END and BLOCKALIGN flags.
 * Run it once with each driver installed and compare the outputs with
 * ../acmtest/abdiff.py.  MSACM.DLL is loaded at run time, so no import
 * library is needed.  Exits Windows when done.
 */

#include <windows.h>
#include <mmsystem.h>
#include <string.h>
#include "mmreg.h"
#include "msacm.h"
#include "msacmdrv.h"

DECLARE_HANDLE(HACMDRIVERID);
DECLARE_HANDLE(HACMDRIVER);

typedef struct {
    DWORD   cbStruct;
    DWORD   fdwStatus;
    DWORD   dwUser;
    LPBYTE  pbSrc;
    DWORD   cbSrcLength;
    DWORD   cbSrcLengthUsed;
    DWORD   dwSrcUser;
    LPBYTE  pbDst;
    DWORD   cbDstLength;
    DWORD   cbDstLengthUsed;
    DWORD   dwDstUser;
    DWORD   dwReservedDriver[10];
} ACMSTREAMHEADER, FAR *LPACMSTREAMHEADER;

typedef BOOL (FAR PASCAL *DRVENUMCB)(HACMDRIVERID, DWORD, DWORD);
typedef BOOL (FAR PASCAL *FMTENUMCB)(HACMDRIVERID, LPACMFORMATDETAILS, DWORD, DWORD);

static DWORD (FAR PASCAL *pacmGetVersion)(void);
static UINT (FAR PASCAL *pacmDriverEnum)(DRVENUMCB, DWORD, DWORD);
static UINT (FAR PASCAL *pacmDriverDetails)(HACMDRIVERID, LPACMDRIVERDETAILS, DWORD);
static UINT (FAR PASCAL *pacmDriverOpen)(HACMDRIVER FAR *, HACMDRIVERID, DWORD);
static UINT (FAR PASCAL *pacmDriverClose)(HACMDRIVER, DWORD);
static UINT (FAR PASCAL *pacmFormatTagDetails)(HACMDRIVER, LPACMFORMATTAGDETAILS, DWORD);
static UINT (FAR PASCAL *pacmFormatEnum)(HACMDRIVER, LPACMFORMATDETAILS, FMTENUMCB, DWORD, DWORD);
static UINT (FAR PASCAL *pacmFormatSuggest)(HACMDRIVER, LPWAVEFORMATEX, LPWAVEFORMATEX, DWORD, DWORD);
static UINT (FAR PASCAL *pacmStreamOpen)(HACMSTREAM FAR *, HACMDRIVER, LPWAVEFORMATEX, LPWAVEFORMATEX,
                                         LPWAVEFILTER, DWORD, DWORD, DWORD);
static UINT (FAR PASCAL *pacmStreamClose)(HACMSTREAM, DWORD);
static UINT (FAR PASCAL *pacmStreamSize)(HACMSTREAM, DWORD, LPDWORD, DWORD);
static UINT (FAR PASCAL *pacmStreamPrepareHeader)(HACMSTREAM, LPACMSTREAMHEADER, DWORD);
static UINT (FAR PASCAL *pacmStreamUnprepareHeader)(HACMSTREAM, LPACMSTREAMHEADER, DWORD);
static UINT (FAR PASCAL *pacmStreamConvert)(HACMSTREAM, LPACMSTREAMHEADER, DWORD);

static HFILE        hOut = HFILE_ERROR;
static WORD         wSeq;
static char         achLog[128];
static HACMDRIVERID hadidADPCM;
static HINSTANCE    ghInst;
static FMTENUMCB    pfnFmtEnum;

static const int    aiCoef1[7] = { 256,  512,  0, 192, 240,  460,  392 };
static const int    aiCoef2[7] = {   0, -256,  0,  64,   0, -208, -232 };
static const DWORD  adwRate[4] = { 8000, 11025, 22050, 44100 };

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

static void RecL(const char FAR *tag, LONG l)
{
    char    ach[64];
    HFILE   h;

    Rec(tag, &l, sizeof(l));
    if (achLog[0] && (h = _lopen(achLog, OF_WRITE)) != HFILE_ERROR) {
        _llseek(h, 0, 2);
        wsprintf(ach, "%u %s %ld\r\n", wSeq - 1, tag, l);
        _lwrite(h, ach, lstrlen(ach));
        _lclose(h);
    }
}

static void RecS(const char FAR *tag, const char FAR *s)
{
    Rec(tag, s, lstrlen(s));
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

/* ------------------------------------------------------------------ */
/* formats and signals (as in acmtest)                                 */
/* ------------------------------------------------------------------ */

typedef struct {
    WAVEFORMATEX    wfx;
    WORD            wSamplesPerBlock;
    WORD            wNumCoef;
    ADPCMCOEFSET    aCoef[7];
    BYTE            abPad[14];
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

static void MakeADPCM(ADPCMFMT FAR *p, DWORD rate, WORD ch)
{
    WORD    ba = (WORD)(256 * ch * ((rate > 11025) ? rate / 11000 : 1));
    WORD    spb = (WORD)((ba - 7 * ch) * 8 / (4 * ch) + 2);
    int     i;

    _fmemset(p, 0, sizeof(*p));
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

static DWORD dwRand;

static WORD Rnd(void)
{
    dwRand = dwRand * 1103515245UL + 12345UL;
    return (WORD)(dwRand >> 16);
}

static int Wave(WORD phase, int amp)
{
    long t = phase & 0x7FFF;
    long y = (t * (32768L - t)) >> 13;
    y = (y * amp) >> 15;
    return (int)((phase & 0x8000) ? -y : y);
}

/* a sweep plus a second tone, some noise and occasional bursts */
static void MakeSignal(BYTE _huge *pb, DWORD cFrames, WORD ch, WORD bits)
{
    WORD    ph[2] = { 0, 0 }, ph2[2] = { 0, 0 };
    DWORD   i;
    long    l;
    int     c, v;

    dwRand = 0x2468ACEUL;
    for (i = 0; i < cFrames; i++)
        for (c = 0; c < ch; c++) {
            ph[c]  += (WORD)(200 + (i >> 5) + c * 61);
            ph2[c] += (WORD)(3100 - c * 170);
            l = (long)Wave(ph[c], 15000) + Wave(ph2[c], 7000) + (int)(Rnd() & 1023) - 512;
            if ((i & 2047) < 64)
                l *= 3;
            v = (l > 32767) ? 32767 : (l < -32768L) ? -32768 : (int)l;
            if (bits == 8) {
                *pb++ = (BYTE)(((UINT)v >> 8) ^ 0x80);
            } else {
                *(short _huge *)pb = (short)v;
                pb += 2;
            }
        }
}

/* ------------------------------------------------------------------ */
/* enumeration callbacks                                               */
/* ------------------------------------------------------------------ */

BOOL FAR PASCAL __export DrvEnumCB(HACMDRIVERID hadid, DWORD dwInstance, DWORD fdwSupport)
{
    ACMDRIVERDETAILS    add;

    _fmemset(&add, 0, sizeof(add));
    add.cbStruct = sizeof(add);
    if ((*pacmDriverDetails)(hadid, &add, 0) == 0 && lstrcmp(add.szShortName, "MS-ADPCM") == 0) {
        hadidADPCM = hadid;
        RecL("drv.support", fdwSupport);
    }
    return TRUE;
}

BOOL FAR PASCAL __export FmtEnumCB(HACMDRIVERID hadid, LPACMFORMATDETAILS pafd, DWORD dwInstance, DWORD fdwSupport)
{
    RecL("fe.idx", pafd->dwFormatIndex);
    RecL("fe.tag", pafd->dwFormatTag);
    RecL("fe.sup", pafd->fdwSupport);
    Rec("fe.wfx", pafd->pwfx, 18 + (pafd->pwfx->wFormatTag == 1 ? 0 : pafd->pwfx->cbSize));
    RecS("fe.name", pafd->szFormat);
    return TRUE;
}

/* ------------------------------------------------------------------ */
/* streaming                                                           */
/* ------------------------------------------------------------------ */

/*
 * Convert cbSrc bytes in chunks that fill a destination buffer of
 * cbChunkDst bytes, like an application streaming to a fixed buffer.
 * Returns the output (caller frees) and its length.
 */
static BYTE _huge *Stream(const char *tag, LPWAVEFORMATEX src, LPWAVEFORMATEX dst, DWORD fdwOpen,
                          BYTE _huge *pbSrc, DWORD cbSrc, DWORD cbChunkDst, DWORD FAR *pcbOut)
{
    HACMSTREAM      has;
    ACMSTREAMHEADER ash;
    DWORD           cbChunkSrc, cbTotal, cbOutMax, cbDone, cbOut, fdw;
    BYTE _huge     *pbOut;
    BYTE _huge     *pbBuf;
    char            ach[16];
    UINT            mmr;
    int             n = 0;

    *pcbOut = 0;
    mmr = (*pacmStreamOpen)(&has, NULL, src, dst, NULL, 0, 0, fdwOpen);
    wsprintf(ach, "%s.open", (LPSTR)tag); RecL(ach, mmr);
    if (mmr)
        return NULL;

    cbChunkSrc = 0;
    mmr = (*pacmStreamSize)(has, cbChunkDst, &cbChunkSrc, ACM_STREAMSIZEF_DESTINATION);
    wsprintf(ach, "%s.szd", (LPSTR)tag); RecL(ach, mmr); RecL(ach, cbChunkSrc);
    cbTotal = 0;
    mmr = (*pacmStreamSize)(has, cbSrc, &cbTotal, ACM_STREAMSIZEF_SOURCE);
    wsprintf(ach, "%s.szs", (LPSTR)tag); RecL(ach, mmr); RecL(ach, cbTotal);
    if (cbChunkSrc == 0)
        cbChunkSrc = cbSrc;

    cbOutMax = cbTotal + cbChunkDst + 1024;
    pbOut = (BYTE _huge *)Alloc(cbOutMax);
    pbBuf = (BYTE _huge *)Alloc(cbChunkDst);
    cbDone = cbOut = 0;

    while (cbDone < cbSrc && n < 2000) {
        _fmemset(&ash, 0, sizeof(ash));
        ash.cbStruct    = sizeof(ash);
        ash.pbSrc       = (LPBYTE)(pbSrc + cbDone);
        ash.cbSrcLength = (cbSrc - cbDone < cbChunkSrc) ? cbSrc - cbDone : cbChunkSrc;
        ash.pbDst       = (LPBYTE)pbBuf;
        ash.cbDstLength = cbChunkDst;
        mmr = (*pacmStreamPrepareHeader)(has, &ash, 0);
        if (mmr) {
            wsprintf(ach, "%s.prep", (LPSTR)tag); RecL(ach, mmr);
            break;
        }
        fdw = ACM_STREAMCONVERTF_BLOCKALIGN;
        if (n == 0)
            fdw |= ACM_STREAMCONVERTF_START;
        if (cbDone + ash.cbSrcLength >= cbSrc)
            fdw = (n == 0) ? ACM_STREAMCONVERTF_START | ACM_STREAMCONVERTF_END : ACM_STREAMCONVERTF_END;
        mmr = (*pacmStreamConvert)(has, &ash, fdw);
        wsprintf(ach, "%s.cv", (LPSTR)tag);
        RecL(ach, mmr);
        RecL(ach, ash.cbSrcLengthUsed);
        RecL(ach, ash.cbDstLengthUsed);
        if (ash.cbDstLengthUsed && cbOut + ash.cbDstLengthUsed <= cbOutMax) {
            hmemcpy(pbOut + cbOut, pbBuf, ash.cbDstLengthUsed);
            cbOut += ash.cbDstLengthUsed;
        }
        (*pacmStreamUnprepareHeader)(has, &ash, 0);
        if (mmr || ash.cbSrcLengthUsed == 0)
            break;
        cbDone += ash.cbSrcLengthUsed;
        n++;
    }

    wsprintf(ach, "%s.out", (LPSTR)tag);
    Rec(ach, pbOut, cbOut);
    (*pacmStreamClose)(has, 0);
    Free(pbBuf);
    *pcbOut = cbOut;
    return pbOut;
}

static void TestStreams(void)
{
    WAVEFORMATEX    pcm, pcm2;
    ADPCMFMT        ad;
    BYTE _huge     *pbPCM;
    BYTE _huge     *pbADPCM;
    BYTE _huge     *pbBack;
    DWORD           cFrames, cbPCM, cbADPCM, cbBack;
    char            ach[16];
    int             r, c, b, rt, ob;

    for (r = 0; r < 4; r++)
        for (c = 1; c <= 2; c++)
            for (b = 8; b <= 16; b += 8)
                for (rt = 0; rt < 2; rt++) {
                    MakePCM(&pcm, adwRate[r], (WORD)c, (WORD)b);
                    MakeADPCM(&ad, adwRate[r], (WORD)c);
                    cFrames = 3L * ad.wSamplesPerBlock + 777;
                    cbPCM = cFrames * pcm.nBlockAlign;
                    pbPCM = (BYTE _huge *)Alloc(cbPCM);
                    MakeSignal(pbPCM, cFrames, (WORD)c, (WORD)b);

                    wsprintf(ach, "s%d%d%d%c", r, c, b / 8, rt ? 'n' : 'r');
                    pbADPCM = Stream(ach, &pcm, &ad.wfx, rt ? ACM_STREAMOPENF_NONREALTIME : 0,
                                     pbPCM, cbPCM, 2L * ad.wfx.nBlockAlign, &cbADPCM);
                    if (pbADPCM && cbADPCM) {
                        for (ob = 8; ob <= 16; ob += 8) {
                            MakePCM(&pcm2, adwRate[r], (WORD)c, (WORD)ob);
                            wsprintf(ach, "d%d%d%d%c%d", r, c, b / 8, rt ? 'n' : 'r', ob / 8);
                            pbBack = Stream(ach, &ad.wfx, &pcm2, 0, pbADPCM, cbADPCM,
                                            3000L * pcm2.nBlockAlign, &cbBack);
                            Free(pbBack);
                        }
                    }
                    Free(pbADPCM);
                    Free(pbPCM);
                }
}

static void TestFormats(HACMDRIVER had)
{
    ACMFORMATTAGDETAILS aftd;
    ACMFORMATDETAILS    afd;
    BYTE                wfx[64], dst[64];
    WAVEFORMATEX        pcm;
    UINT                mmr;
    int                 i, r, c, b, f;

    for (i = 0; i < 3; i++) {
        _fmemset(&aftd, 0, sizeof(aftd));
        aftd.cbStruct = sizeof(aftd);
        aftd.dwFormatTagIndex = i;
        RecL("ftd.r", (*pacmFormatTagDetails)(had, &aftd, ACM_FORMATTAGDETAILSF_INDEX));
        RecL("ftd.tag", aftd.dwFormatTag);
        RecL("ftd.cb", aftd.cbFormatSize);
        RecL("ftd.sup", aftd.fdwSupport);
        RecL("ftd.n", aftd.cStandardFormats);
        RecS("ftd.name", aftd.szFormatTag);
    }

    for (i = 0; i < 3; i++) {
        _fmemset(&afd, 0, sizeof(afd));
        _fmemset(wfx, 0, sizeof(wfx));
        afd.cbStruct    = sizeof(afd);
        afd.dwFormatTag = i == 0 ? WAVE_FORMAT_ADPCM : WAVE_FORMAT_PCM;
        afd.pwfx        = (LPWAVEFORMATEX)wfx;
        afd.cbwfx       = sizeof(wfx);
        ((LPWAVEFORMATEX)wfx)->wFormatTag = (WORD)afd.dwFormatTag;
        mmr = (*pacmFormatEnum)(i == 2 ? NULL : had, &afd, pfnFmtEnum, 0,
                                i == 2 ? ACM_FORMATENUMF_WFORMATTAG : 0);
        RecL("fe.r", mmr);
    }

    /* suggestions with no driver given: the ACM asks the drivers */
    for (r = 0; r < 4; r++)
        for (c = 1; c <= 2; c++)
            for (b = 8; b <= 16; b += 8)
                for (f = 0; f < 2; f++) {
                    MakePCM(&pcm, adwRate[r], (WORD)c, (WORD)b);
                    _fmemset(dst, 0, sizeof(dst));
                    ((LPWAVEFORMATEX)dst)->wFormatTag = WAVE_FORMAT_ADPCM;
                    RecL("sug.r", (*pacmFormatSuggest)(NULL, &pcm, (LPWAVEFORMATEX)dst, sizeof(dst),
                                                       f ? ACM_FORMATSUGGESTF_WFORMATTAG : 0));
                    Rec("sug.d", dst, sizeof(dst));
                }
}

static void RunTests(void)
{
    ACMDRIVERDETAILS    add;
    HACMDRIVER          had;
    DRVENUMCB           pfnDrv;
    UINT                mmr;

    RecL("version", (*pacmGetVersion)());

    pfnDrv = (DRVENUMCB)MakeProcInstance((FARPROC)DrvEnumCB, ghInst);
    pfnFmtEnum = (FMTENUMCB)MakeProcInstance((FARPROC)FmtEnumCB, ghInst);
    RecL("drvenum", (*pacmDriverEnum)(pfnDrv, 0, 0));
    RecL("found", hadidADPCM != NULL);
    if (!hadidADPCM)
        return;

    _fmemset(&add, 0, sizeof(add));
    add.cbStruct = sizeof(add);
    RecL("dd.r", (*pacmDriverDetails)(hadidADPCM, &add, 0));
    Rec("dd.fixed", &add, 0x26);
    RecS("dd.short", add.szShortName);
    RecS("dd.long", add.szLongName);
    RecS("dd.copy", add.szCopyright);
    RecS("dd.lic", add.szLicensing);
    RecS("dd.feat", add.szFeatures);

    mmr = (*pacmDriverOpen)(&had, hadidADPCM, 0);
    RecL("drvopen", mmr);
    if (mmr == 0) {
        TestFormats(had);
        RecL("drvclose", (*pacmDriverClose)(had, 0));
    }

    TestStreams();
}

static FARPROC Get(HINSTANCE h, LPCSTR name)
{
    FARPROC p = GetProcAddress(h, name);
    RecL(name, p != NULL);
    return p;
}

int PASCAL WinMain(HINSTANCE hInst, HINSTANCE hPrev, LPSTR lpCmd, int nShow)
{
    char        achOut[128];
    HINSTANCE   hACM;
    LPSTR       p;
    int         n;

    ghInst = hInst;
    p = lpCmd;
    for (n = 0; *p && *p != ' ' && n < 127; n++) achOut[n] = *p++;
    achOut[n] = 0;

    hOut = _lcreat(achOut, 0);
    lstrcpy(achLog, achOut);
    for (p = achLog + lstrlen(achLog); p > achLog && p[-1] != '.' && p[-1] != '\\'; p--)
        ;
    if (p > achLog && p[-1] == '.')
        p[-1] = 0;
    lstrcat(achLog, ".LOG");
    _lclose(_lcreat(achLog, 0));
    if (hOut != HFILE_ERROR) {
        SetErrorMode(SEM_NOOPENFILEERRORBOX);
        hACM = LoadLibrary("MSACM.DLL");
        RecL("loadlib", hACM >= HINSTANCE_ERROR ? 0 : (LONG)(UINT)hACM);
        if (hACM >= HINSTANCE_ERROR) {
            *(FARPROC *)&pacmGetVersion            = Get(hACM, "acmGetVersion");
            *(FARPROC *)&pacmDriverEnum            = Get(hACM, "acmDriverEnum");
            *(FARPROC *)&pacmDriverDetails         = Get(hACM, "acmDriverDetails");
            *(FARPROC *)&pacmDriverOpen            = Get(hACM, "acmDriverOpen");
            *(FARPROC *)&pacmDriverClose           = Get(hACM, "acmDriverClose");
            *(FARPROC *)&pacmFormatTagDetails      = Get(hACM, "acmFormatTagDetails");
            *(FARPROC *)&pacmFormatEnum            = Get(hACM, "acmFormatEnum");
            *(FARPROC *)&pacmFormatSuggest         = Get(hACM, "acmFormatSuggest");
            *(FARPROC *)&pacmStreamOpen            = Get(hACM, "acmStreamOpen");
            *(FARPROC *)&pacmStreamClose           = Get(hACM, "acmStreamClose");
            *(FARPROC *)&pacmStreamSize            = Get(hACM, "acmStreamSize");
            *(FARPROC *)&pacmStreamPrepareHeader   = Get(hACM, "acmStreamPrepareHeader");
            *(FARPROC *)&pacmStreamUnprepareHeader = Get(hACM, "acmStreamUnprepareHeader");
            *(FARPROC *)&pacmStreamConvert         = Get(hACM, "acmStreamConvert");
            if (pacmGetVersion && pacmDriverEnum && pacmDriverDetails && pacmDriverOpen &&
                pacmDriverClose && pacmFormatTagDetails && pacmFormatEnum && pacmFormatSuggest &&
                pacmStreamOpen && pacmStreamClose && pacmStreamSize && pacmStreamPrepareHeader &&
                pacmStreamUnprepareHeader && pacmStreamConvert)
                RunTests();
            FreeLibrary(hACM);
        }
        Rec("end", NULL, 0);
        _lclose(hOut);
    }

    ExitWindows(0, 0);
    return 0;
}
