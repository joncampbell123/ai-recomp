/*
 * capavi.c - code segment 7 of VIDCAP.EXE, 0000-286C: streaming capture
 * to an AVI file (Capture/Video), an early form of the capture engine
 * that became AVICAP.DLL.
 *
 * The capture file is written while capturing: AVIFileInit seeks past
 * room for the header, the frames and audio go out as RIFF chunks padded
 * with JUNK to 2 KB boundaries, and AVIFileFini then writes the header
 * (RIFF AVI / LIST hdrl) in front of the data, the LIST movi around it
 * and the idx1 index after it.  The index is kept in memory as one DWORD
 * per chunk: the size and the IS_* flags.
 *
 * Video buffers (up to 1000) and four half-second audio buffers are
 * allocated with AllocMem: below 1 MB (GlobalDosAlloc) when capturing to
 * disk, so DOS can write them directly, else from global memory with a
 * DOS bounce buffer for the writes.
 *
 * Error returns are string IDs (IDS_ERR_*).  Functions are FAR CDECL
 * unless noted.
 */

#include <windows.h>
#include <mmsystem.h>
#include <time.h>
#include "vidcap.h"

#define CHUNK_GRANULARITY   2048        /* data chunks end on 2 KB boundaries */
#define MAX_VIDEO_BUFFERS   1000
#define MAX_WAVE_BUFFERS    4
#define MAX_DUMMY_FRAMES    248

/* index entry flags, above the 28-bit size */
#define IS_AUDIO_CHUNK      0x80000000L
#define IS_KEYFRAME_CHUNK   0x40000000L
#define IS_DUMMY_CHUNK      0x20000000L
#define IS_GRANULAR_CHUNK   0x10000000L
#define INDEX_MASK          0xF0000000L

/* SMARTDRV.EXE: INT 2Fh AX=4A10h BX=0003h, cache state of drive BP */
#define SMARTDRV_WRITE      0x0080      /* write caching off */
#define SMARTDRV_READ       0x0040      /* read caching off */

/* wave mapper query (DRVM_MAPPER_STATUS, WAVEIN_MAPPER_STATUS_MAPPED) */
#define DRVM_MAPPER_STATUS              0x2000
#define WAVEIN_MAPPER_STATUS_MAPPED     1

typedef struct {
    DWORD   dwType;
    DWORD   dwSize;
} RIFF, FAR *LPRIFF;

typedef struct {                        /* DPMI 0500h buffer */
    DWORD   dwLargestFree;
    DWORD   dwMaxUnlockedPages;
    DWORD   dwMaxLockedPages;
    DWORD   dwLinearPages;
    DWORD   dwUnlockedPages;
    DWORD   dwFreePages;
    DWORD   dwPhysicalPages;
    DWORD   dwFreeLinearPages;
    DWORD   dwSwapPages;
    BYTE    abReserved[12];
} DPMIMEMINFO;                          /* 0x30 bytes */

HGLOBAL         ghIndex;                /* DS:1534 */
static DWORD    gdwDosBufSize;          /* DS:1538 */
static DWORD _huge *glpdwIndex;         /* DS:1544 */
DWORD           gdwIndex;               /* DS:1548 entries used */
DWORD           gdwIndexSize;           /* DS:154C entries, [VidCap] IndexSize */
LPVIDEOHDR      galpVHdr[MAX_VIDEO_BUFFERS];    /* DS:1608 */
int             giVideoBuffer;          /* DS:260E next buffer to come back */
DWORD           gdwAVIHdrSize;          /* DS:2634 where the data starts */
LPBYTE          glpInfoChunks;          /* DS:2638 INFO chunks for the header */
int             gnVideoBuffers;         /* DS:26B8 */
static RIFF     garcDummy[256];         /* DS:26BE dropped-frame chunks */
LPWAVEHDR       galpwh[MAX_WAVE_BUFFERS];       /* DS:2EBE */
static LPBYTE   glpDosBuf;              /* DS:2EDE */
int             giWaveBuffer;           /* DS:2EE6 */
BOOL            gfUsingDOSMemory;       /* DS:2EEA */
DWORD           gdwAVIHdrPos;           /* DS:2EF2 offset of the avih data */
HMMIO           ghmmio;                 /* DS:2EF6 the capture file */
BOOL            gfWaveMapped;           /* DS:2F12 the wave mapper converts: yield */
BOOL            gfVariableFrames;       /* DS:2F16 compressed format */
DWORD           gdwActualMicroSecPerFrame;      /* DS:2F1A */
DWORD           gdwVideoChunks;         /* DS:2F20 */
time_t          gtimeCapture;           /* DS:2F42 */
static DWORD _huge *glpdwIndexNext;     /* DS:2FE4 */
DWORD           gcbInfoChunks;          /* DS:2FEA */
DWORD           gdwWaveBytes;           /* DS:3036 */
DWORD           gdwVideoBufferSize;     /* DS:303C chunk with header and padding */
HWAVEIN         ghWaveIn;               /* DS:3050 */
DWORD           gdwFramesDropped;       /* DS:3064 */
BOOL            gfAudioLost;            /* DS:3068 all wave buffers were full */
DWORD           gdwWaveBufferSize;      /* DS:306A */
DWORD           gdwWaveChunks;          /* DS:306E */

/*
 * seg7:0000-00CB  InitIndex
 *
 * Records the chunk sizes and allocates the index (IndexSize entries,
 * kept between 1800 and 324000) if it isn't allocated yet.
 */
BOOL FAR InitIndex(DWORD dwVideoBufferSize, DWORD dwWaveBufferSize)
{
    gdwVideoBufferSize = dwVideoBufferSize;
    gdwWaveBufferSize = dwWaveBufferSize;
    gdwIndex = 0;

    if (glpdwIndex)
        return TRUE;

    gdwIndexSize = max(gdwIndexSize, 1800L);
    gdwIndexSize = min(gdwIndexSize, 324000L);

    ghIndex = GlobalAlloc(GMEM_MOVEABLE, gdwIndexSize * sizeof(DWORD));
    if (ghIndex) {
        glpdwIndexNext = glpdwIndex = (DWORD _huge *)GlobalLock(ghIndex);
        if (glpdwIndex) {
            GlobalPageLock(ghIndex);
            return TRUE;
        }
        GlobalFree(ghIndex);
    }
    ghIndex = NULL;
    glpdwIndex = NULL;
    return FALSE;
}

/*
 * seg7:00CC-0115  FiniIndex
 */
void FAR FiniIndex(void)
{
    if (ghIndex) {
        GlobalPageUnlock(ghIndex);
        if (glpdwIndex)
            GlobalUnlock(ghIndex);
        GlobalFree(ghIndex);
    }
    ghIndex = NULL;
    glpdwIndex = NULL;
}

/*
 * seg7:0116-018E  IndexVideo
 *
 * dwSize may carry IS_DUMMY_CHUNK/IS_GRANULAR_CHUNK.  FALSE if the index
 * is full.
 */
BOOL FAR IndexVideo(DWORD dwSize, BOOL fKeyFrame)
{
    BOOL    f;

    f = (gdwIndex < gdwIndexSize);
    if (f) {
        *glpdwIndexNext++ = dwSize | (fKeyFrame ? IS_KEYFRAME_CHUNK : 0);
        gdwIndex++;
        gdwVideoChunks++;
    }
    return f;
}

/*
 * seg7:0190-01FA  IndexAudio
 */
BOOL FAR IndexAudio(DWORD dwSize)
{
    BOOL    f;

    f = (gdwIndex < gdwIndexSize);
    if (f) {
        *glpdwIndexNext++ = dwSize | IS_AUDIO_CHUNK;
        gdwIndex++;
        gdwWaveChunks++;
    }
    return f;
}

/*
 * seg7:01FC-0421  WriteIndex
 *
 * Writes the idx1 chunk, working out each chunk's offset from the sizes:
 * every chunk is followed by JUNK up to 2 KB if fJunkChunkWritten (not
 * audio), and a run of dummy (dropped) frames shares one padding at its
 * last frame.  Quirks: an overflowed index returns TRUE without writing
 * anything, and the final mmioAscend uses ghmmio rather than hmmio.
 */
static BOOL FAR WriteIndex(HMMIO hmmio, BOOL fJunkChunkWritten)
{
    MMCKINFO        ck;
    AVIINDEXENTRY   avie;
    DWORD _huge     *lpdw;
    DWORD           dwIndex;
    DWORD           dwOffset;
    DWORD           dwDummySize;
    DWORD           dw;
    BOOL            fAudio;
    BOOL            fKeyFrame;
    BOOL            fDummy;
    BOOL            fGranular;

    if (gdwIndex > gdwIndexSize)
        return TRUE;

    dwOffset = gdwAVIHdrSize;

    ck.cksize = 0;
    ck.ckid = ckidAVINEWINDEX;
    ck.fccType = 0;
    if (mmioCreateChunk(hmmio, &ck, 0))
        return FALSE;

    lpdw = glpdwIndex;
    for (dwIndex = 0; dwIndex < gdwIndex; dwIndex++) {
        dw = *lpdw++;
        fAudio    = (dw & IS_AUDIO_CHUNK) != 0;
        fKeyFrame = (dw & IS_KEYFRAME_CHUNK) != 0;
        fDummy    = (dw & IS_DUMMY_CHUNK) != 0;
        fGranular = (dw & IS_GRANULAR_CHUNK) != 0;
        dw &= ~INDEX_MASK;

        if (fAudio) {
            avie.ckid = MAKEAVICKID(cktypeWAVEbytes, 1);
            avie.dwFlags = 0;
        } else {
            avie.ckid = MAKEAVICKID(cktypeDIBbits, 0);
            avie.dwFlags = fKeyFrame ? AVIIF_KEYFRAME : 0;
        }
        avie.dwChunkLength = dw;
        avie.dwChunkOffset = dwOffset;

        if (mmioWrite(hmmio, (HPSTR)&avie, sizeof(avie)) != sizeof(avie))
            return FALSE;

        dw += sizeof(RIFF);
        dwOffset += dw;

        if (fDummy) {
            dwDummySize += sizeof(RIFF);
            if (!fGranular)
                continue;
            dw = dwDummySize;
        } else {
            dwDummySize = 0;
        }

        if (fJunkChunkWritten & !fAudio) {
            if (dw & (CHUNK_GRANULARITY - 1)) {
                dw = CHUNK_GRANULARITY - (dw & (CHUNK_GRANULARITY - 1));
                if (dw < sizeof(RIFF))
                    dw += CHUNK_GRANULARITY;
                dwOffset += dw;
            }
        }

        if (dwOffset & 1)
            dwOffset++;
    }

    return mmioAscend(ghmmio, &ck, 0) == 0;
}

/*
 * seg7:0422-0437  AllocDosMem  (NEAR PASCAL, no DS prologue)
 *
 * Memory below 1 MB; the pointer is selector:0 of GlobalDosAlloc's block.
 */
static LPVOID NEAR PASCAL AllocDosMem(DWORD dw)
{
    return MAKELP(LOWORD(GlobalDosAlloc(dw)), 0);
}

/*
 * seg7:0438-0477  AllocMem  (NEAR PASCAL, no DS prologue)
 */
static LPVOID NEAR PASCAL AllocMem(DWORD dw)
{
    HGLOBAL h;

    if (gfUsingDOSMemory)
        return AllocDosMem(dw);

    h = GlobalAlloc(GMEM_MOVEABLE, dw);
    if (h)
        return GlobalLock(h);
    return NULL;
}

/*
 * seg7:0478-0486  FreeMem  (NEAR PASCAL, no DS prologue)
 *
 * Frees either kind by selector (GlobalFree, also for DOS blocks).
 */
static void NEAR PASCAL FreeMem(LPVOID lp)
{
    GlobalFree((HGLOBAL)SELECTOROF(lp));
}

/*
 * seg7:0488-04F9  GetFreePhysicalMemory
 *
 * GetFreeSpace in standard mode, else the largest lockable block from
 * DPMI function 0500h, in bytes.  (Unoptimised in the original because
 * of the inline assembly.)
 */
DWORD FAR GetFreePhysicalMemory(void)
{
    DPMIMEMINFO dmi;
    BOOL        fError;

    if (GetWinFlags() & WF_STANDARD)
        return GetFreeSpace(0);

    _asm {
        mov     ax, 0500h
        push    ss
        pop     es
        lea     di, dmi
        int     31h
        sbb     ax, ax
        mov     fError, ax
    }
    if (fError)
        return 0;
    return dmi.dwMaxLockedPages << 12;
}

/*
 * seg7:04FA-0522  CallSmartDrv  (NEAR PASCAL)
 *
 * SmartDrive's get/set cache state for drive iDrive (0 = A:): wFunction
 * 0 get, 1/2 read cache on/off, 3/4 write cache on/off.  Returns the
 * state from DL.
 */
static WORD NEAR PASCAL CallSmartDrv(int iDrive, WORD wFunction)
{
    WORD    w;

    _asm {
        push    bp
        mov     ax, 4A10h
        mov     bx, 0003h
        mov     dl, byte ptr wFunction
        mov     bp, iDrive
        int     2Fh
        mov     al, dl
        xor     ah, ah
        pop     bp
        mov     w, ax
    }
    return w;
}

/*
 * seg7:0523-0591  SmartDrv  (FAR PASCAL)
 *
 * Sets the drive's SmartDrive caching from w (SMARTDRV_WRITE and
 * SMARTDRV_READ set mean off) and returns the previous state, which can
 * be passed back to restore it.
 */
WORD FAR PASCAL SmartDrv(char chDrive, WORD w)
{
    int     iDrive;
    WORD    wSave;

    iDrive = (chDrive | 0x20) - 'a';
    wSave = CallSmartDrv(iDrive, 0);
    CallSmartDrv(iDrive, (w & SMARTDRV_WRITE) ? 4 : 3);
    CallSmartDrv(iDrive, (w & SMARTDRV_READ) ? 2 : 1);
    return wSave;
}

/*
 * seg7:0593-06F9  AVIFileInit
 *
 * Opens (or creates) the capture file, sets one up first if there is no
 * capture file name, and seeks to the start of the data, leaving 2 KB
 * for the header (a non-standard format header gets more).  Before that
 * the existing file is read back to front in 50000 byte steps.  Returns
 * the file or NULL.
 */
HMMIO FAR AVIFileInit(LPBITMAPINFOHEADER lpbi)
{
    char    ach[128];
    HMMIO   hmmio;
    LONG    l;

    if (lpbi == NULL)
        lpbi = glpbiCapture;

    if (gachCaptureFile[0] == 0 && !SetCaptureFile())
        return NULL;

    hmmio = mmioOpen(gachCaptureFile, NULL, MMIO_WRITE);
    if (hmmio == NULL) {
        hmmio = mmioOpen(gachCaptureFile, NULL, MMIO_CREATE | MMIO_WRITE);
        if (hmmio == NULL)
            return NULL;
    }

    l = mmioSeek(hmmio, 0, SEEK_END);
    while (l > 0) {
        l = mmioSeek(hmmio, -min(l, 50000L), SEEK_CUR);
        mmioRead(hmmio, ach, sizeof(ach));
    }

    if (lpbi->biSize == sizeof(BITMAPINFOHEADER))
        gdwAVIHdrSize = AVI_HEADERSIZE;
    else
        gdwAVIHdrSize = (lpbi->biSize & ~(DWORD)(CHUNK_GRANULARITY - 1)) + 2 * CHUNK_GRANULARITY;
    mmioSeek(hmmio, gdwAVIHdrSize, SEEK_SET);

    if (!InitIndex(gdwVideoBufferSize, gdwWaveBufferSize)) {
        mmioClose(hmmio, 0);
        return NULL;
    }

    gdwVideoChunks = 0;
    gdwWaveChunks = 0;
    return hmmio;
}

/*
 * seg7:06FA-0F6B  AVIFileFini
 *
 * Writes the AVI header in front of the captured data, closes the movi
 * list, writes the index and closes the file.  With audio the frame rate
 * is recomputed from the amount of audio captured.  The main header still
 * has the VfW 1.0 dwScale/dwRate/dwStart/dwLength fields (dwReserved in
 * 1.1).  The colour table written is the current palette with red and
 * blue swapped, flags kept.  Sets gfCaptureFileHasData and the Edit
 * button from the result.  WriteIndex's result is ignored.
 */
BOOL FAR AVIFileFini(LPBITMAPINFOHEADER lpbi, HMMIO hmmio, BOOL fSound, BOOL fJunkChunkWritten)
{
    PALETTEENTRY    ape[256];
    MainAVIHeader   aviHdr;
    AVIStreamHeader strhdr;
    MMCKINFO        ckRiff;
    MMCKINFO        ckStream;
    MMCKINFO        ckList;
    MMCKINFO        ck;
    PCMWAVEFORMAT   pcm;
    DWORD           dwDataEnd;
    DWORD           dw;
    BOOL            fRet;
    int             i;

    fRet = TRUE;
    if (lpbi == NULL)
        lpbi = glpbiCapture;
    if (gdwWaveBytes == 0)
        fSound = FALSE;

    dwDataEnd = mmioSeek(hmmio, 0, SEEK_CUR);
    mmioSeek(hmmio, 0, SEEK_SET);

    ckRiff.cksize = 0;
    ckRiff.fccType = formtypeAVI;
    if (mmioCreateChunk(hmmio, &ckRiff, MMIO_CREATERIFF))
        goto FileError;

    ckList.cksize = 0;
    ckList.fccType = listtypeAVIHEADER;
    if (mmioCreateChunk(hmmio, &ckList, MMIO_CREATELIST))
        goto FileError;

    ck.cksize = sizeof(MainAVIHeader);
    ck.ckid = ckidAVIMAINHDR;
    if (mmioCreateChunk(hmmio, &ck, 0))
        goto FileError;
    gdwAVIHdrPos = ck.dwDataOffset;

    _fmemset(&aviHdr, 0, sizeof(aviHdr));

    if (fSound && gdwVideoChunks && !gfAudioLost)
        aviHdr.dwMicroSecPerFrame = (DWORD)((double)gdwWaveBytes /
            ((double)(gwBits >> 3) * (double)gdwVideoChunks * (double)gwChannels *
             (double)gwFrequency) * 1000000.0 + 0.5);
    else
        aviHdr.dwMicroSecPerFrame = gdwMicroSecPerFrame;
    gdwActualMicroSecPerFrame = aviHdr.dwMicroSecPerFrame;

    aviHdr.dwMaxBytesPerSec = (DWORD)((double)lpbi->biSizeImage *
                                      (1000000.0 / (double)gdwMicroSecPerFrame)) +
                              gdwWaveBufferSize * 2;
    aviHdr.dwPaddingGranularity = 0;
    aviHdr.dwFlags = AVIF_WASCAPTUREFILE | AVIF_HASINDEX;
    aviHdr.dwStreams = fSound ? 2 : 1;
    aviHdr.dwTotalFrames = gdwVideoChunks;
    aviHdr.dwInitialFrames = 0;
    aviHdr.dwSuggestedBufferSize = 0;
    aviHdr.dwWidth = lpbi->biWidth;
    aviHdr.dwHeight = lpbi->biHeight;
    aviHdr.dwReserved[0] = aviHdr.dwMicroSecPerFrame;   /* dwScale */
    aviHdr.dwReserved[1] = 1000000L;                    /* dwRate */
    aviHdr.dwReserved[2] = 0;                           /* dwStart */
    aviHdr.dwReserved[3] = gdwVideoChunks;              /* dwLength */

    if (mmioWrite(hmmio, (HPSTR)&aviHdr, sizeof(aviHdr)) != sizeof(aviHdr))
        goto FileError;
    if (mmioAscend(hmmio, &ck, 0))
        goto FileError;

    /* the video stream */
    ckStream.cksize = 0;
    ckStream.fccType = listtypeSTREAMHEADER;
    if (mmioCreateChunk(hmmio, &ckStream, MMIO_CREATELIST))
        goto FileError;

    _fmemset(&strhdr, 0, sizeof(strhdr));
    strhdr.fccType = streamtypeVIDEO;
    strhdr.fccHandler = gCompVars.hic ? gCompVars.fccHandler : lpbi->biCompression;
    if (strhdr.fccHandler == BI_RLE8)
        strhdr.fccHandler = mmioFOURCC('R', 'L', 'E', ' ');
    strhdr.dwFlags = 0;
    strhdr.dwInitialFrames = 0;
    strhdr.dwScale = aviHdr.dwMicroSecPerFrame;
    strhdr.dwRate = 1000000L;
    strhdr.dwStart = 0;
    strhdr.dwLength = gdwVideoChunks;
    strhdr.dwQuality = (DWORD)-1L;
    strhdr.dwSampleSize = 0;

    ck.ckid = ckidSTREAMHEADER;
    strhdr.wPriority = 0;
    strhdr.wLanguage = 0;
    if (mmioCreateChunk(hmmio, &ck, 0))
        goto FileError;
    if (mmioWrite(hmmio, (HPSTR)&strhdr, sizeof(strhdr)) != sizeof(strhdr))
        goto FileError;
    if (mmioAscend(hmmio, &ck, 0))
        goto FileError;

    if (lpbi->biBitCount > 8)
        lpbi->biClrUsed = 0;

    ck.cksize = lpbi->biSize + lpbi->biClrUsed * sizeof(RGBQUAD);
    ck.ckid = ckidSTREAMFORMAT;
    if (mmioCreateChunk(hmmio, &ck, 0))
        goto FileError;
    if (mmioWrite(hmmio, (HPSTR)lpbi, lpbi->biSize) != (LONG)lpbi->biSize)
        goto FileError;

    if (lpbi->biClrUsed) {
        if ((int)lpbi->biClrUsed != GetPaletteEntries(ghpalCurrent, 0, (int)lpbi->biClrUsed, ape))
            goto FileError;
        for (i = 0; i < (int)lpbi->biClrUsed; i++) {
            ape[i].peBlue ^= ape[i].peRed;
            ape[i].peRed ^= ape[i].peBlue;
            ape[i].peBlue ^= ape[i].peRed;
        }
        dw = lpbi->biClrUsed * sizeof(RGBQUAD);
        if (mmioWrite(hmmio, (HPSTR)ape, dw) != (LONG)dw)
            goto FileError;
    }

    if (mmioAscend(hmmio, &ck, 0))
        goto FileError;
    if (mmioAscend(hmmio, &ckStream, 0))
        goto FileError;

    /* the audio stream */
    if (fSound) {
        ckStream.cksize = 0;
        ckStream.fccType = listtypeSTREAMHEADER;
        if (mmioCreateChunk(hmmio, &ckStream, MMIO_CREATELIST))
            goto FileError;

        _fmemset(&strhdr, 0, sizeof(strhdr));
        strhdr.fccType = streamtypeAUDIO;
        strhdr.fccHandler = 0;
        strhdr.dwFlags = 0;
        strhdr.dwInitialFrames = 0;
        strhdr.dwScale = 1;
        strhdr.dwRate = gwFrequency;
        strhdr.dwStart = 0;
        strhdr.dwLength = gdwWaveBytes / (DWORD)((gwBits >> 3) * gwChannels);
        strhdr.dwQuality = (DWORD)-1L;
        strhdr.dwSampleSize = (gwBits * gwChannels) >> 3;

        ck.ckid = ckidSTREAMHEADER;
        strhdr.wPriority = 0;
        strhdr.wLanguage = 0;
        if (mmioCreateChunk(hmmio, &ck, 0))
            goto FileError;
        if (mmioWrite(hmmio, (HPSTR)&strhdr, sizeof(strhdr)) != sizeof(strhdr))
            goto FileError;
        if (mmioAscend(hmmio, &ck, 0))
            goto FileError;

        pcm.wf.wFormatTag = WAVE_FORMAT_PCM;
        pcm.wf.nChannels = gwChannels;
        pcm.wf.nSamplesPerSec = gwFrequency;
        pcm.wf.nBlockAlign = (gwChannels * gwBits) >> 3;
        pcm.wf.nAvgBytesPerSec = (DWORD)gwFrequency * pcm.wf.nBlockAlign;
        pcm.wBitsPerSample = gwBits;

        ck.cksize = sizeof(PCMWAVEFORMAT);
        ck.ckid = ckidSTREAMFORMAT;
        if (mmioCreateChunk(hmmio, &ck, 0))
            goto FileError;
        if (mmioWrite(hmmio, (HPSTR)&pcm, sizeof(pcm)) != sizeof(pcm))
            goto FileError;
        if (mmioAscend(hmmio, &ck, 0))
            goto FileError;
        if (mmioAscend(hmmio, &ckStream, 0))
            goto FileError;
    }

    if (glpInfoChunks) {
        if (mmioWrite(hmmio, (HPSTR)glpInfoChunks, gcbInfoChunks) != (LONG)gcbInfoChunks)
            goto FileError;
    }

    if (mmioAscend(hmmio, &ckList, 0))
        goto FileError;

    /* JUNK up to the data, which starts at gdwAVIHdrSize */
    ck.ckid = ckidAVIPADDING;
    if (mmioCreateChunk(hmmio, &ck, 0))
        goto FileError;
    mmioSeek(hmmio, gdwAVIHdrSize - 3 * sizeof(DWORD), SEEK_SET);
    if (mmioAscend(hmmio, &ck, 0))
        goto FileError;

    ckList.cksize = 0;
    ckList.fccType = listtypeAVIMOVIE;
    if (mmioCreateChunk(hmmio, &ckList, MMIO_CREATELIST))
        goto FileError;
    mmioSeek(hmmio, dwDataEnd + (dwDataEnd & 1), SEEK_SET);
    mmioAscend(hmmio, &ckList, 0);

    WriteIndex(hmmio, fJunkChunkWritten);
    FiniIndex();
    gfCaptureFileHasData = TRUE;
    goto Done;

FileError:
    fRet = FALSE;
    gfCaptureFileHasData = fRet;

Done:
    toolbarModifyState(ghwndToolbar, BTN_EDIT, gfCaptureFileHasData ? BTNST_UP : BTNST_GRAYED);
    mmioAscend(hmmio, &ckRiff, 0);
    mmioSeek(hmmio, 0, SEEK_END);
    mmioFlush(hmmio, 0);
    mmioClose(hmmio, 0);
    return fRet;
}

/*
 * seg7:0F6C-0FCA  AVIWrite  (NEAR PASCAL, no DS prologue)
 *
 * Writes to the capture file, through the DOS buffer if there is one.
 */
static BOOL NEAR PASCAL AVIWrite(LPVOID p, DWORD cb)
{
    if (glpDosBuf) {
        hmemcpy(glpDosBuf, p, cb);
        p = glpDosBuf;
    }
    return mmioWrite(ghmmio, (HPSTR)p, cb) == (LONG)cb;
}

/*
 * seg7:0FCC-1116  AVIAudioInit
 *
 * Opens the wave input (wave mapper, PCM from the Audio Format settings,
 * window callback if hwnd) and allocates the wave buffers: WAVEHDR, RIFF
 * header, data.
 */
int FAR AVIAudioInit(HWND hwnd)
{
    PCMWAVEFORMAT   pcm;
    LPWAVEHDR       lpwh;
    LPRIFF          lpriff;
    int             n;
    int             i;

    pcm.wf.wFormatTag = WAVE_FORMAT_PCM;
    pcm.wf.nChannels = gwChannels;
    pcm.wf.nSamplesPerSec = gwFrequency;
    pcm.wf.nBlockAlign = (gwChannels * gwBits) >> 3;
    pcm.wf.nAvgBytesPerSec = (DWORD)gwFrequency * pcm.wf.nBlockAlign;
    pcm.wBitsPerSample = gwBits;

    if (waveInOpen(&ghWaveIn, (UINT)WAVE_MAPPER, (LPWAVEFORMAT)&pcm, (DWORD)(UINT)hwnd, 0L,
                   hwnd ? CALLBACK_WINDOW : 0L))
        return IDS_ERR_WAVEOPEN;

    gfWaveMapped = WaveInMapped(ghWaveIn);
    gfAudioLost = FALSE;

    n = 0;
    for (i = 0; i < MAX_WAVE_BUFFERS; i++) {
        lpwh = (LPWAVEHDR)AllocMem(gdwWaveBufferSize + sizeof(WAVEHDR));
        if (lpwh == NULL)
            break;
        galpwh[i] = lpwh;
        lpwh->lpData = (LPBYTE)lpwh + sizeof(WAVEHDR) + sizeof(RIFF);
        lpwh->dwBufferLength = gdwWaveBufferSize - sizeof(RIFF);
        lpwh->dwBytesRecorded = 0;
        lpwh->dwUser = 0;
        lpwh->dwFlags = 0;
        lpwh->dwLoops = 0;
        lpriff = (LPRIFF)(lpwh + 1);
        lpriff->dwType = MAKEAVICKID(cktypeWAVEbytes, 1);
        lpriff->dwSize = gdwWaveBufferSize - sizeof(RIFF);
        n++;
    }
    if (n != MAX_WAVE_BUFFERS)
        return IDS_ERR_WAVEMEM;

    giWaveBuffer = 0;
    gdwWaveBytes = 0;
    gdwWaveChunks = 0;
    return 0;
}

/*
 * seg7:1118-1166  AVIAudioPrepare
 */
int FAR AVIAudioPrepare(void)
{
    LPWAVEHDR FAR *plpwh;

    for (plpwh = galpwh; plpwh < galpwh + MAX_WAVE_BUFFERS; plpwh++) {
        if (waveInPrepareHeader(ghWaveIn, *plpwh, sizeof(WAVEHDR)) ||
            waveInAddBuffer(ghWaveIn, *plpwh, sizeof(WAVEHDR)))
            return IDS_ERR_WAVEMEM;
    }
    return 0;
}

/*
 * seg7:1168-11D6  AVIAudioFini
 */
int FAR AVIAudioFini(void)
{
    LPWAVEHDR FAR *plpwh;

    if (ghWaveIn) {
        waveInReset(ghWaveIn);
        for (plpwh = galpwh; plpwh < galpwh + MAX_WAVE_BUFFERS; plpwh++) {
            if (*plpwh) {
                if ((*plpwh)->dwFlags & WHDR_PREPARED)
                    waveInUnprepareHeader(ghWaveIn, *plpwh, sizeof(WAVEHDR));
                FreeMem(*plpwh);
                *plpwh = NULL;
            }
        }
        waveInClose(ghWaveIn);
        ghWaveIn = NULL;
    }
    return 0;
}

/*
 * seg7:11D8-1267  AVIFini
 */
void FAR AVIFini(void)
{
    LPVIDEOHDR FAR *plpvh;

    if (glpDosBuf) {
        FreeMem(glpDosBuf);
        glpDosBuf = NULL;
    }

    if (ghVideoIn) {
        videoStreamReset(ghVideoIn);
        for (plpvh = galpVHdr; plpvh < galpVHdr + MAX_VIDEO_BUFFERS; plpvh++) {
            if (*plpvh) {
                if ((*plpvh)->dwFlags & VHDR_PREPARED)
                    videoStreamUnprepareHeader(ghVideoIn, *plpvh, sizeof(VIDEOHDR));
                FreeMem(*plpvh);
                *plpvh = NULL;
            }
        }
        videoStreamFini(ghVideoIn);
    }

    if (ghWaveIn)
        AVIAudioFini();
}

/*
 * seg7:1268-12C9  CalcWaveBufferSize
 *
 * Half a second of audio rounded down to 2 KB, at least 10 KB; 0 without
 * audio.
 */
DWORD FAR CalcWaveBufferSize(void)
{
    DWORD   dw;

    if (!gfCaptureAudio)
        return 0;

    dw = (DWORD)((gwBits >> 3) * gwFrequency) * gwChannels / 2;
    dw -= dw & (CHUNK_GRANULARITY - 1);
    if (dw < 10240)
        dw = 10240;
    return dw;
}

/*
 * seg7:12CA-162E  AVIInit
 *
 * Sets up audio and as many video buffers as fit (in global memory 80%
 * of the free memory less some for audio, at most 1000; at least 3 in
 * DOS memory, 4 otherwise), each holding a VIDEOHDR, the 00db chunk and
 * JUNK to 2 KB, and starts the video stream.  The chunk sizes use
 * glpbiCapture's biSizeImage; lpbi only decides whether frames vary in
 * size.  A video buffer that won't prepare ends the list.
 */
int FAR AVIInit(LPBITMAPINFOHEADER lpbi)
{
    LPVIDEOHDR      lpvh;
    LPRIFF          lpriff;
    DWORD           dwJunk;
    DWORD           dwFree;
    DWORD           dw;
    DWORD           dwSize;
    int             nBuffers;
    int             err;
    int             i;

    if (lpbi == NULL)
        lpbi = glpbiCapture;

    _fmemset(galpVHdr, 0, sizeof(galpVHdr));
    _fmemset(galpwh, 0, sizeof(galpwh));

    gdwWaveBufferSize = CalcWaveBufferSize();
    gdwVideoBufferSize = glpbiCapture->biSizeImage + sizeof(RIFF);
    gfVariableFrames = (lpbi->biCompression != BI_RGB);

    dwJunk = CHUNK_GRANULARITY - (gdwVideoBufferSize & (CHUNK_GRANULARITY - 1));
    if (dwJunk) {
        if (dwJunk < sizeof(RIFF))
            dwJunk += CHUNK_GRANULARITY;
        gdwVideoBufferSize += dwJunk;
        dwJunk -= sizeof(RIFF);
    } else {
        dwJunk = 0;
    }

    gdwDosBufSize = max(gdwVideoBufferSize, gdwWaveBufferSize);
    if (!gfUsingDOSMemory)
        glpDosBuf = (LPBYTE)AllocDosMem(gdwDosBufSize);

    if (gfCaptureAudio) {
        err = AVIAudioInit(NULL);
        if (err) {
            if (err == IDS_ERR_WAVEOPEN) {
                err = IDS_ERR_WAVEOPEN;
                goto Error;
            }
            goto ErrorWaveMem;
        }
    }

    nBuffers = MAX_VIDEO_BUFFERS;
    if (!gfUsingDOSMemory) {
        dwFree = GetFreePhysicalMemory();
        dw = (gdwWaveBufferSize + 8192) * 4;
        if (dw < dwFree)
            dwFree -= dw;
        nBuffers = (int)(dwFree * 8 / 10 / gdwVideoBufferSize);
        if (nBuffers > MAX_VIDEO_BUFFERS)
            nBuffers = MAX_VIDEO_BUFFERS;
    }

    for (i = 0; i < nBuffers; i++) {
        lpvh = (LPVIDEOHDR)AllocMem(gdwVideoBufferSize + sizeof(VIDEOHDR));
        if (lpvh == NULL)
            break;
        galpVHdr[i] = lpvh;
        lpvh->lpData = (LPBYTE)lpvh + sizeof(VIDEOHDR) + sizeof(RIFF);
        dwSize = glpbiCapture->biSizeImage;
        lpvh->dwBufferLength = dwSize;
        lpvh->dwBytesUsed = 0;
        lpvh->dwTimeCaptured = 0;
        lpvh->dwUser = 0;
        lpvh->dwFlags = 0;

        lpriff = (LPRIFF)(lpvh + 1);
        lpriff->dwType = MAKEAVICKID(cktypeDIBbits, 0);
        lpriff->dwSize = dwSize;
        if (dwJunk) {
            lpriff = (LPRIFF)((BYTE _huge *)lpriff + lpriff->dwSize + sizeof(RIFF));
            lpriff->dwType = ckidAVIPADDING;
            lpriff->dwSize = dwJunk;
        }
    }
    gnVideoBuffers = i;
    giVideoBuffer = 0;
    gdwVideoChunks = 0;

    if ((gfUsingDOSMemory && gnVideoBuffers < 3) || (!gfUsingDOSMemory && gnVideoBuffers < 4))
        goto ErrorVideoMem;

    gdwFramesDropped = 0;

    if (gfCaptureAudio) {
        err = AVIAudioPrepare();
        if (err)
            goto ErrorWaveMem;
    }

    if (videoStreamInit(ghVideoIn, gdwMicroSecPerFrame, 0L, 0L, 0L)) {
        err = IDS_ERR_VIDEOOPEN;
        goto Error;
    }

    for (i = 0; i < gnVideoBuffers; i++) {
        if (videoStreamPrepareHeader(ghVideoIn, galpVHdr[i], sizeof(VIDEOHDR))) {
            gnVideoBuffers = i;
            break;
        }
        if (videoStreamAddBuffer(ghVideoIn, galpVHdr[i], sizeof(VIDEOHDR)))
            goto ErrorVideoMem;
    }
    return 0;

ErrorWaveMem:
    err = IDS_ERR_WAVEMEM;
    goto Error;

ErrorVideoMem:
    err = IDS_ERR_VIDEOMEM;

Error:
    AVIFini();
    return err;
}

/*
 * seg7:1630-165D  GetWaveInPosition  (unreferenced)
 */
DWORD FAR GetWaveInPosition(HWAVEIN hwi)
{
    MMTIME  mmt;

    mmt.wType = TIME_SAMPLES;
    waveInGetPosition(hwi, &mmt, sizeof(mmt));
    return mmt.u.sample;
}

/*
 * seg7:165E-1751  AVIWriteDummyFrames
 *
 * Writes nCount empty 00db chunks for dropped frames, padded together to
 * 2 KB, and indexes them (the last one granular).
 */
BOOL FAR AVIWriteDummyFrames(int nCount)
{
    LPRIFF  lpriff;
    DWORD   dwBytes;
    DWORD   dwJunk;
    int     i;

    lpriff = garcDummy;
    for (i = 0; i < nCount; i++) {
        IndexVideo(IS_DUMMY_CHUNK | ((nCount - i == 1) ? IS_GRANULAR_CHUNK : 0), FALSE);
        lpriff->dwType = MAKEAVICKID(cktypeDIBbits, 0);
        lpriff->dwSize = 0;
        lpriff++;
    }

    dwBytes = (DWORD)(nCount * sizeof(RIFF));
    dwJunk = dwBytes & (CHUNK_GRANULARITY - 1);
    if (dwJunk) {
        dwJunk = CHUNK_GRANULARITY - dwJunk;
        if (dwJunk < sizeof(RIFF))
            dwJunk += CHUNK_GRANULARITY;
        dwBytes += dwJunk;
        dwJunk -= sizeof(RIFF);
    } else {
        dwJunk = 0;
    }

    if (dwJunk) {
        lpriff->dwType = ckidAVIPADDING;
        lpriff->dwSize = dwJunk;
    }
    return AVIWrite(garcDummy, dwBytes);
}

/*
 * seg7:1752-18ED  AVIWriteVideoFrame
 *
 * Compresses the frame if a compressor is chosen (the output buffer has
 * room for the chunk header in front), and writes the chunk.  Frames of
 * a compressed format get their real size and their own JUNK; others
 * are written as the whole buffer.
 */
BOOL FAR AVIWriteVideoFrame(LPVIDEOHDR lpvh)
{
    BYTE _huge  *lpData;
    LPRIFF      lpriff;
    DWORD       dwSize;
    DWORD       dwJunk;
    LONG        lSize;
    BOOL        fKey;

    if (gCompVars.hic) {
        lSize = 0;
        lpData = (BYTE _huge *)ICSeqCompressFrame(&gCompVars, 0, lpvh->lpData, &fKey, &lSize);
        ((LPRIFF)lpData)[-1].dwType = MAKEAVICKID(cktypeDIBbits, 0);
        ((LPRIFF)lpData)[-1].dwSize = lSize;
        if (fKey)
            lpvh->dwFlags |= VHDR_KEYFRAME;
        else
            lpvh->dwFlags &= ~VHDR_KEYFRAME;
        lpvh->dwBytesUsed = lSize;
    } else {
        lpData = lpvh->lpData;
    }

    if (gfVariableFrames) {
        *(DWORD _huge *)(lpData - sizeof(DWORD)) = lpvh->dwBytesUsed;
        if (lpvh->dwBytesUsed & 1)
            lpvh->dwBytesUsed++;

        dwSize = lpvh->dwBytesUsed + sizeof(RIFF);
        dwJunk = dwSize & (CHUNK_GRANULARITY - 1);
        if (dwJunk) {
            dwJunk = CHUNK_GRANULARITY - dwJunk;
            if (dwJunk < sizeof(RIFF))
                dwJunk += CHUNK_GRANULARITY;
            dwSize += dwJunk;
            lpriff = (LPRIFF)(lpData + lpvh->dwBytesUsed);
            lpriff->dwType = ckidAVIPADDING;
            lpriff->dwSize = dwJunk - sizeof(RIFF);
        }
    } else {
        dwSize = gdwVideoBufferSize;
    }

    return AVIWrite((LPBYTE)(lpData - sizeof(RIFF)), dwSize);
}

/*
 * seg7:18EE-1B39  SetInfoChunk  (FAR PASCAL)
 *
 * Adds, replaces or (lpData NULL or cbData 0) removes an INFO chunk for
 * the file header; fccInfoID 0 removes them all.  Pointer arithmetic is
 * on offsets.  Quirk: removing the last chunk shrinks gcbInfoChunks but
 * neither reallocates nor returns TRUE.
 */
BOOL FAR PASCAL SetInfoChunk(LPCAPINFOCHUNK lpcic)
{
    DWORD   ckid;
    LPVOID  lpData;
    LONG    cb;
    LPBYTE  lp;
    LPBYTE  lpw;
    LPBYTE  lpEnd;
    LPBYTE  lpNext;
    LONG    cbSizeThis;
    BOOL    fOK;

    ckid = lpcic->fccInfoID;
    lpData = lpcic->lpData;
    cb = lpcic->cbData;
    fOK = FALSE;

    if (ckid == 0) {
        if (glpInfoChunks) {
            GlobalUnlock(GlobalHandle(SELECTOROF(glpInfoChunks)));
            GlobalFree(GlobalHandle(SELECTOROF(glpInfoChunks)));
            glpInfoChunks = NULL;
            gcbInfoChunks = 0;
        }
        return TRUE;
    }

    lpw = glpInfoChunks;
    lpEnd = glpInfoChunks + (UINT)gcbInfoChunks;
    while (lpw < lpEnd) {
        cbSizeThis = ((DWORD FAR *)lpw)[1];
        cbSizeThis += cbSizeThis & 1;
        lpNext = lpw + (UINT)cbSizeThis + sizeof(DWORD) * 2;
        if (*(DWORD FAR *)lpw == ckid) {
            gcbInfoChunks -= cbSizeThis + sizeof(DWORD) * 2;
            if (lpNext < lpEnd) {
                hmemcpy(lpw, lpNext, (DWORD)(UINT)OFFSETOF(lpEnd) - (UINT)OFFSETOF(lpNext));
                GlobalUnlock(GlobalHandle(SELECTOROF(glpInfoChunks)));
                glpInfoChunks = (LPBYTE)GlobalLock(GlobalReAlloc(
                    GlobalHandle(SELECTOROF(glpInfoChunks)), gcbInfoChunks, GMEM_MOVEABLE));
                fOK = TRUE;
            }
            break;
        }
        lpw = lpNext;
    }

    if (lpData == NULL || cb == 0)
        return fOK;

    cb += (cb & 1) + sizeof(DWORD) * 2;
    if (glpInfoChunks) {
        GlobalUnlock(GlobalHandle(SELECTOROF(glpInfoChunks)));
        lp = (LPBYTE)GlobalLock(GlobalReAlloc(GlobalHandle(SELECTOROF(glpInfoChunks)),
                                              gcbInfoChunks + cb, GMEM_MOVEABLE));
    } else {
        lp = (LPBYTE)GlobalLock(GlobalAlloc(GMEM_MOVEABLE, cb));
    }
    if (lp == NULL)
        return FALSE;

    ((DWORD FAR *)(lp + (UINT)gcbInfoChunks))[0] = ckid;
    ((DWORD FAR *)(lp + (UINT)gcbInfoChunks))[1] = lpcic->cbData;
    hmemcpy(lp + (UINT)gcbInfoChunks + sizeof(DWORD) * 2, lpData, cb - sizeof(DWORD) * 2);
    glpInfoChunks = lp;
    gcbInfoChunks += cb;
    return TRUE;
}

/*
 * seg7:1B3C-2830  CaptureVideo
 *
 * Capture/Video.  Opens the file, allocates the buffers, confirms, and
 * streams video (and audio) to disk until Escape or a mouse click (any
 * key state since the last check), the time limit (also the MCI segment
 * and the index capacity), the index filling up or an error; then drains
 * the buffers, writes the header and index and reports.  Frames late by
 * more than a frame time are made up with up to 248 dummy frames.  While
 * capturing, the preview shows key frames when the buffers are empty.
 * SmartDrive caching of the capture drive is switched off meanwhile.
 *
 * Quirks: a failing AVIFileInit returns without restoring the cursor,
 * the DC or ICSeqCompressFrameEnd; if DrawDibDraw fails the preview DC is
 * never released; the time from the timeGetTime call at the stop is
 * discarded.
 */
void FAR CaptureVideo(void)
{
    char            achFmt[128];
    char            achMsg[128];
    char            achStatus[128];
    char            achStatusFmt[128];
    char            achSMPTE[40];
    CAPINFOCHUNK    cic;
    LPVIDEOHDR      lpvh;
    LPWAVEHDR       lpwh;
    DWORD           dwVideoError;
    DWORD           dwVideoErrorValue;
    DWORD           dwTimeLimit;
    DWORD           dwStartTime;
    DWORD           dwTime;
    DWORD           dw;
    HPALETTE        hpal;
    HDC             hdc;
    WORD            wSmartDrv;
    BOOL            fPreview;
    BOOL            fOK;
    BOOL            fStop;
    BOOL            fStopping;
    BOOL            f;
    int             nDummy;
    int             nCount;
    int             err;
    int             i;

    dwVideoError = 0;
    StatusText(MAKEINTRESOURCE(IDS_STATUS_SETUP));

    if (GetFreePhysicalMemory() < 0x100000L) {
        MsgBoxID(ghwndApp, ghInst, MB_OK | MB_ICONEXCLAMATION, IDS_APPNAME, IDS_OUTOFMEMORY);
        return;
    }

    ghcurOld = SetCursor(ghcurWait);

    fPreview = gfPreview;
    if (fPreview) {
        hdc = GetDC(ghwndShow);
        SetWindowOrg(hdc, gnHScrollPos, gnVScrollPos);
        hpal = DrawDibGetPalette(ghdd);
        if (hpal)
            hpal = SelectPalette(hdc, hpal, FALSE);
        RealizePalette(hdc);
    }

    if (gfLimitEnabled)
        dwTimeLimit = (DWORD)(gwTimeLimit * 1000.0);
    else
        dwTimeLimit = (DWORD)-1L;

    if (gfMCIControl) {
        if (gdwMCIStopTime == gdwMCIStartTime)
            gdwMCIStopTime = (DWORD)(gwTimeLimit * 1000.0) + gdwMCIStartTime;
        dw = gdwMCIStopTime - gdwMCIStartTime;
        if (gfLimitEnabled)
            dwTimeLimit = min(dwTimeLimit, dw);
    }

    dwTimeLimit = min(dwTimeLimit, muldiv32(gdwIndexSize, gdwMicroSecPerFrame, 1000L));

    if (gfMCIControl) {
        f = FALSE;
        if (MCIDeviceOpen(gachMCIDevice) && MCIDeviceSetPosition(gdwMCIStartTime))
            f = TRUE;
        if (!f) {
            MsgBoxID(ghwndApp, ghInst, MB_OK | MB_ICONEXCLAMATION, IDS_APPNAME, IDS_ERR_MCI);
            goto ErrorStatus;
        }
    }

    if (gCompVars.hic) {
        if (!ICSeqCompressFrameStart(&gCompVars, (LPBITMAPINFO)glpbiCapture)) {
            err = IDS_ERR_COMPRESSOR;
            goto ErrorBox;
        }
        /* room for the chunk header in front of the compressed data */
        gCompVars.lpBitsOut = (LPBYTE)gCompVars.lpBitsOut + sizeof(RIFF);
    }

    ghmmio = AVIFileInit((LPBITMAPINFOHEADER)gCompVars.lpbiOut);
    if (ghmmio == NULL)
        return;

    UpdateWindow(ghwndApp);
    UpdateWindow(ghwndShow);

    gfUsingDOSMemory = !gfCaptureToMemory;
    err = AVIInit((LPBITMAPINFOHEADER)gCompVars.lpbiOut);
    if (err && gfUsingDOSMemory) {
        gfUsingDOSMemory = FALSE;
        err = AVIInit((LPBITMAPINFOHEADER)gCompVars.lpbiOut);
    }
    if (err) {
        MsgBoxID(ghwndApp, ghInst, MB_OK | MB_ICONEXCLAMATION, IDS_APPNAME, err);
        goto ErrorFile;
    }

    LoadString(ghInst, IDS_CONFIRMVIDEO, achFmt, sizeof(achFmt));
    wsprintf(achMsg, achFmt, (LPSTR)gachCaptureFile);
    LoadString(ghInst, IDS_HITESCAPE, achFmt, sizeof(achFmt));
    LoadString(ghInst, IDS_STATUS_CAPTURING, achStatusFmt, sizeof(achStatusFmt));
    StatusText(NULL);
    if (MessageBox(ghwndApp, achMsg, gszAppName, MB_OKCANCEL | MB_ICONEXCLAMATION) == IDCANCEL)
        goto ErrorFile;

    StatusText(achFmt);
    UpdateWindow(ghwndShow);
    UpdateWindow(ghwndFrame);
    GetAsyncKeyState(VK_ESCAPE);
    GetAsyncKeyState(VK_LBUTTON);

    wSmartDrv = SmartDrv(gachCaptureFile[0], (WORD)-1);

    /* IDIT: when; ISMT: where on the MCI source */
    cic.fccInfoID = mmioFOURCC('I', 'D', 'I', 'T');
    time(&gtimeCapture);
    cic.lpData = (LPSTR)ctime(&gtimeCapture);
    cic.cbData = 26;
    SetInfoChunk(&cic);

    cic.fccInfoID = mmioFOURCC('I', 'S', 'M', 'T');
    cic.lpData = NULL;
    cic.cbData = 0;
    SetInfoChunk(&cic);

    if (gfMCIControl) {
        MCIMsToSMPTE(gdwMCIStartTime, achSMPTE);
        cic.lpData = achSMPTE;
        cic.cbData = lstrlen(achSMPTE) + 1;
        SetInfoChunk(&cic);
    }
    if (gfMCIControl)
        MCIDevicePlay();

    dwStartTime = timeGetTime();
    if (gfCaptureAudio)
        waveInStart(ghWaveIn);
    videoStreamStart(ghVideoIn);

    fOK = TRUE;
    fStop = FALSE;
    fStopping = FALSE;
    lpvh = galpVHdr[giVideoBuffer];
    lpwh = galpwh[giWaveBuffer];

    for (;;) {
        videoStreamGetError(ghVideoIn, &dwVideoError, &dwVideoErrorValue);
        dwTime = timeGetTime() - dwStartTime;

        if (lpvh->dwFlags & VHDR_DONE) {
            if (lpvh->dwBytesUsed) {
                dw = muldiv32(gdwVideoChunks + 1, gdwMicroSecPerFrame, 1000L);
                if (gdwVideoChunks && lpvh->dwTimeCaptured > dw) {
                    nDummy = (int)min(muldiv32(lpvh->dwTimeCaptured - dw, 1000L,
                                               gdwMicroSecPerFrame), (DWORD)MAX_DUMMY_FRAMES);
                    gdwFramesDropped += nDummy;
                    fOK = AVIWriteDummyFrames(nDummy);
                    if (!fOK)
                        fStop = TRUE;
                }

                if (!AVIWriteVideoFrame(lpvh)) {
                    fOK = FALSE;
                    fStop = TRUE;
                    StatusText(MAKEINTRESOURCE(IDS_ERR_FILEWRITE));
                } else if (!IndexVideo(lpvh->dwBytesUsed, (BOOL)(lpvh->dwFlags & VHDR_KEYFRAME))) {
                    fStop = TRUE;
                }

                i = (giVideoBuffer + 1) % gnVideoBuffers;
                if (!(galpVHdr[i]->dwFlags & VHDR_DONE) && fPreview && gdwVideoChunks &&
                    (lpvh->dwFlags & VHDR_KEYFRAME))
                    fPreview = DrawDibDraw(ghdd, hdc, 0, 0, gcxCapture, gcyCapture,
                                           glpbiCapture, lpvh->lpData, 0, 0, -1, -1,
                                           DDF_SAME_HDC | DDF_SAME_DRAW);

                if (!fStop &&
                    ((gdwVideoChunks && gdwVideoChunks % 100 == 0) ||
                     !(galpVHdr[i]->dwFlags & VHDR_DONE))) {
                    if (gfYield || gfWaveMapped)
                        Yield();
                    wsprintf(achStatus, achStatusFmt, gdwVideoChunks, gdwFramesDropped,
                             (int)(dwTime / 1000), (int)(dwTime % 1000));
                    StatusText(achStatus);
                }
            }

            lpvh->dwFlags &= ~VHDR_DONE;
            if (videoStreamAddBuffer(ghVideoIn, lpvh, sizeof(VIDEOHDR))) {
                fOK = FALSE;
                fStop = TRUE;
                StatusText(MAKEINTRESOURCE(IDS_ERR_VIDEOADD));
            }
            if (++giVideoBuffer >= gnVideoBuffers)
                giVideoBuffer = 0;
            lpvh = galpVHdr[giVideoBuffer];
        }

        if (gfCaptureAudio) {
            if (gfWaveMapped)
                Yield();

            /* the buffer before this one still done: all were full */
            i = giWaveBuffer ? giWaveBuffer - 1 : MAX_WAVE_BUFFERS - 1;
            if (!fStop && (galpwh[i]->dwFlags & WHDR_DONE))
                gfAudioLost = TRUE;

            nCount = MAX_WAVE_BUFFERS;
            while (fOK && (lpwh->dwFlags & WHDR_DONE)) {
                nCount--;
                if (lpwh->dwBytesRecorded) {
                    ((LPRIFF)lpwh->lpData)[-1].dwSize = lpwh->dwBytesRecorded;
                    if (!AVIWrite(lpwh->lpData - sizeof(RIFF),
                                  (lpwh->dwBytesRecorded + sizeof(RIFF) + 1) & ~1L)) {
                        fOK = FALSE;
                        fStop = TRUE;
                        StatusText(MAKEINTRESOURCE(IDS_ERR_FILEWRITE));
                    } else if (IndexAudio(lpwh->dwBytesRecorded)) {
                        gdwWaveBytes += lpwh->dwBytesRecorded;
                    } else {
                        fStop = TRUE;
                    }
                }

                lpwh->dwBytesRecorded = 0;
                lpwh->dwFlags &= ~WHDR_DONE;
                if (waveInAddBuffer(ghWaveIn, lpwh, sizeof(WAVEHDR))) {
                    fOK = FALSE;
                    fStop = TRUE;
                    StatusText(MAKEINTRESOURCE(IDS_ERR_WAVEADD));
                }
                if (++giWaveBuffer >= MAX_WAVE_BUFFERS)
                    giWaveBuffer = 0;
                lpwh = galpwh[giWaveBuffer];
                if (nCount == 0)
                    break;
            }
        }

        f = fStop;
        if ((GetAsyncKeyState(VK_ESCAPE) & 1) || (GetAsyncKeyState(VK_LBUTTON) & 1))
            f = TRUE;
        if (dwTime > dwTimeLimit)
            f = TRUE;

        if (fStopping && !(lpvh->dwFlags & VHDR_DONE) &&
            (!gfCaptureAudio || !(lpwh->dwFlags & WHDR_DONE)))
            break;

        if (f && !fStopping) {
            fStopping = TRUE;
            if (gfCaptureAudio)
                waveInStop(ghWaveIn);
            videoStreamStop(ghVideoIn);
            timeGetTime();
            if (gfMCIControl)
                MCIDevicePause();
        }

        fStop = f;
        if (!f)
            continue;

        if (!fOK) {
            StatusText(MAKEINTRESOURCE(IDS_ERR_RECORDING));
            break;
        }
        LoadString(ghInst, IDS_STATUS_FINISHED, achStatusFmt, sizeof(achStatusFmt));
        wsprintf(achStatus, achStatusFmt, gdwVideoChunks);
        StatusText(achStatus);
    }

    SmartDrv(gachCaptureFile[0], wSmartDrv);
    while (FlushKeys(FALSE))
        ;

    AVIFini();
    if (ghmmio) {
        AVIFileFini((LPBITMAPINFOHEADER)gCompVars.lpbiOut, ghmmio, gfCaptureAudio, TRUE);
        ghmmio = NULL;
    }

    dwTime = gdwVideoChunks * gdwActualMicroSecPerFrame / 1000;
    if (!fOK)
        MsgBoxID(ghwndApp, ghInst, MB_OK | MB_ICONEXCLAMATION, IDS_APPNAME, IDS_ERR_DATARATE);

    dw = gdwVideoChunks ? muldiv32(gdwVideoChunks, 1000000L, dwTime) : 0;

    if (gfCaptureAudio) {
        LoadString(ghInst, IDS_STATUS_RESULTAUDIO, achStatusFmt, sizeof(achStatusFmt));
        wsprintf(achStatus, achStatusFmt,
                 (int)(dwTime / 1000), (int)(dwTime % 1000),
                 gdwVideoChunks, gdwFramesDropped,
                 (int)(dw / 1000), (int)(dw % 1000),
                 gdwWaveBytes, gwFrequency / 1000, gwFrequency % 1000);
    } else {
        LoadString(ghInst, IDS_STATUS_RESULT, achStatusFmt, sizeof(achStatusFmt));
        wsprintf(achStatus, achStatusFmt,
                 (int)(dwTime / 1000), (int)(dwTime % 1000),
                 gdwVideoChunks, gdwFramesDropped,
                 (int)(dw / 1000), (int)(dw % 1000));
    }
    StatusText(achStatus);

    if (fOK && gdwVideoChunks == 0) {
        err = IDS_WARN_NOFRAMES;
        goto ErrorBox;
    }
    if (fOK && gfCaptureAudio && gdwWaveBytes == 0) {
        err = IDS_ERR_NOAUDIO;
        goto ErrorBox;
    }
    if (fOK && gfCaptureAudio && gfAudioLost) {
        err = IDS_ERR_AUDIOLOST;
        goto ErrorBox;
    }
    if (!fOK || gdwVideoChunks == 0)
        goto Cleanup;

    achStatus[0] = 0;
    achMsg[0] = 0;
    if ((double)gdwFramesDropped / (double)gdwVideoChunks > 0.1) {
        LoadString(ghInst, IDS_DROPPED, achStatusFmt, sizeof(achStatusFmt));
        wsprintf(achStatus, achStatusFmt, gdwFramesDropped, gdwVideoChunks,
                 (int)(muldiv32(gdwFramesDropped, 10000L, gdwVideoChunks) / 100),
                 (int)(muldiv32(gdwFramesDropped, 10000L, gdwVideoChunks) % 100));
    }
    if (achStatus[0])
        MessageBox(ghwndApp, achStatus, gszAppName, MB_OK | MB_ICONEXCLAMATION);
    goto Cleanup;

ErrorFile:
    AVIFini();
    if (ghmmio == NULL)
        goto ErrorStatus;
    AVIFileFini((LPBITMAPINFOHEADER)gCompVars.lpbiOut, ghmmio, gfCaptureAudio, TRUE);
    ghmmio = NULL;

ErrorStatus:
    StatusText(NULL);
    goto Cleanup;

ErrorBox:
    MsgBoxID(ghwndApp, ghInst, MB_OK | MB_ICONEXCLAMATION, IDS_APPNAME, err);

Cleanup:
    if (gCompVars.hic) {
        if (gCompVars.lpBitsOut)
            gCompVars.lpBitsOut = (LPBYTE)gCompVars.lpBitsOut - sizeof(RIFF);
        ICSeqCompressFrameEnd(&gCompVars);
    }
    if (fPreview) {
        if (hpal)
            SelectPalette(hdc, hpal, FALSE);
        ReleaseDC(ghwndShow, hdc);
    }
    if (gfMCIControl)
        MCIDeviceClose();
    if (!gfPreview && !gfOverlay)
        videoFrame(ghVideoIn, &gvhFrame);
    InvalidateRect(ghwndShow, NULL, TRUE);
    SetCursor(ghcurOld);
}

/*
 * seg7:2832-286C  WaveInMapped
 *
 * TRUE if the wave mapper is converting for this device (it then needs
 * the application to yield).
 */
BOOL FAR WaveInMapped(HWAVEIN hwi)
{
    DWORD   dw;

    if (waveInMessage(hwi, DRVM_MAPPER_STATUS, WAVEIN_MAPPER_STATUS_MAPPED,
                      (DWORD)(LPVOID)&dw) == 0 && dw)
        return TRUE;
    return FALSE;
}
