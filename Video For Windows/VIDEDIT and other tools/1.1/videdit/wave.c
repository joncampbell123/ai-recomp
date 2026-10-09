/*
 * wave.c - code segment 7 of VIDEDIT.EXE (0x1ACA bytes): the movie's
 * audio track.
 *
 * The audio is a MediaMan WAVE element (gmedidWave) edited through the
 * WAVE handler's messages (WAVE_CUT, WAVE_PASTE, WAVE_COPY, ...).  Audio
 * positions are sample numbers; frame n starts at sample
 *     n * nSamplesPerSec * usecPerFrame / 1000000 - glWaveOffset,
 * so glWaveOffset (the "synchronize" offset) shifts the sound against the
 * video.  Every edit records how to undo itself: glUndoLen samples were
 * inserted at glUndoPos, ghwaveUndo holds what was removed there, and
 * glWaveOffsetUndo the previous offset.
 *
 * In insert mode (gfInsertMode = 1, the default) edits change the length of the
 * sound; in overwrite mode deleted audio is replaced by silence and pasted
 * audio replaces what was there.
 *
 * Also here: preview playback through waveOut (16 buffers of 16 KB filled
 * with WAVE_READ in the movie's format, refilled on MM_WOM_DONE), and the
 * wave clipboard: VidEdit's private "WaveEditIntSound" format holds an
 * HMEDWAVE, CF_WAVE is rendered on demand as a RIFF WAVE image.
 *
 * Functions are in the order of the binary.  The init code that loads
 * MEDWAVE.MMH and registers the clipboard format is in seg2.
 */

#include <string.h>
#include "videdit.h"
#include "vemain.h"
#include "edit.h"

#define IDS_LOADINGAUDIO        194     /* "Loading audio into memory..." */

/* initialised data, DS:03F0-0489 (module data starts at DS:03D6) */
MEDID           gmedidWave = 0;         /* DS:03F0 */
static LONG     glWaveOffset = 0;       /* DS:03F4 samples before frame 0 */
static LONG     glUndoPos = 0;          /* DS:03F8 */
static LONG     glUndoLen = 0;          /* DS:03FC samples to remove on undo */
static HMEDWAVE    ghwaveUndo = 0;         /* DS:0400 samples to put back on undo */
                                        /* DS:0404 "WaveEditIntSound", DS:0415 "medwave.mmh", */
                                        /* DS:0421-0481 the MEDWAVE.MMH export names (seg2) */
static HWAVEOUT ghWaveOut = 0;          /* DS:0482 */
static BOOL     gfWavePlaying = FALSE;  /* DS:0484 */
static HMEDWAVE    ghwaveClip = 0;         /* DS:0486 the sound on the clipboard */

/* uninitialised */
static PCMWAVEFORMAT gWaveFormat;       /* DS:1C02 format of gmedidWave */
static PCMWAVEFORMAT gWaveFormatUndo;   /* DS:1C12 */
static LONG     glWaveOffsetUndo;       /* DS:1C22 */
                                        /* DS:1C26 gcfWave, the "WaveEditIntSound" format */
static LONG     glPlayLoop;             /* DS:1C28 */
static LONG     glPlayPos;              /* DS:1C2C */
static LONG     glPlayEnd;              /* DS:1C30 */
static BOOL     gfPlayLoop;             /* DS:1C34 */
static WORD     gcWaveBuf;              /* DS:1C36 */
static WORD     gcWaveQueued;           /* DS:1C38 */
static WORD     giWaveNext;             /* DS:1C3A */
static LPWAVEHDR galpWaveHdr[16];       /* DS:1C3C */

/*
 * s07:0000-007B  WaveFrameToSample  (NEAR PASCAL)
 *
 * First sample of a frame, clipped to the length of the sound if fClip.
 */
static LONG NEAR PASCAL WaveFrameToSample(WORD wFrame, BOOL fClip)
{
    LONG l, lLen;

    if (gmedidWave == 0)
        return 0;

    l = muldivrd32((DWORD)wFrame * gWaveFormat.wf.nSamplesPerSec, gCompOptions.dwUSecPerFrame, 1000000L);
    l -= glWaveOffset;
    if (fClip) {
        lLen = medSendMessage(gmedidWave, WAVE_GETSIZE, 0L, 0L);
        if (lLen < l)
            l = lLen;
    }
    return l;
}

/*
 * s07:007C-00D5  WaveChooseFormat
 *
 * Edit / Audio Format: WAVE_SETFMT without a format makes the handler ask
 * the user (parent window in lParam2).  gidHelpContext is 0x70 while its dialog
 * is up.
 */
void FAR WaveChooseFormat(void)
{
    gidHelpContext = 0x70;
    if (medSendMessage(gmedidWave, WAVE_SETFMT, 0L, (LONG)(WORD)ghwndApp)) {
        SetUndo(IDM_AUDIOFORMAT);
        gWaveFormatUndo = gWaveFormat;
        medSendMessage(gmedidWave, WAVE_GETFMT, (LONG)(LPVOID)&gWaveFormat, sizeof(gWaveFormat));
    }
    gidHelpContext = 0;
}

/*
 * s07:00D6-012D  WaveUndoFormat
 */
void FAR WaveUndoFormat(void)
{
    PCMWAVEFORMAT fmt;

    medSendMessage(gmedidWave, WAVE_GETFMT, (LONG)(LPVOID)&fmt, sizeof(fmt));
    medSendMessage(gmedidWave, WAVE_SETFMT, (LONG)(LPVOID)&gWaveFormatUndo, 0L);
    gWaveFormat = gWaveFormatUndo;
    gWaveFormatUndo = fmt;
}

/*
 * s07:012E-0217  WaveCountFrames  (FAR PASCAL)
 *
 * Number of frames a WAVE element covers at the movie's frame rate.  A
 * last frame with less than 1% of a frame's worth of sound is not
 * counted.  The sample rate is the element's own, but the check uses
 * WaveFrameToSample, which works with the movie's format and offset.
 */
int FAR PASCAL WaveCountFrames(MEDID medid)
{
    LONG            lLen;
    PCMWAVEFORMAT   fmt;
    double          dSamples;
    int             n;

    if (medid == 0)
        return 0;

    lLen = medSendMessage(medid, WAVE_GETSIZE, 0L, 0L);
    medSendMessage(medid, WAVE_GETFMT, (LONG)(LPVOID)&fmt, sizeof(fmt));
    dSamples = (double)fmt.wf.nSamplesPerSec * (double)gCompOptions.dwUSecPerFrame * 1e-6;
    n = (int)((dSamples * 0.999999999 + lLen) / dSamples);
    if (lLen - WaveFrameToSample(n - 1, FALSE) < (LONG)dSamples / 100)
        n--;
    return n;
}

/*
 * s07:0218-02D5  WaveConvertedLength  (NEAR PASCAL)
 *
 * Length of a piece of sound once converted to the movie's sample rate
 * (rates are compared as rounded integer ratios).
 */
static LONG NEAR PASCAL WaveConvertedLength(HMEDWAVE hwave)
{
    PCMWAVEFORMAT FAR  *lpfmt;
    LONG                lLen;
    WORD                w;

    lpfmt = MediaWaveFormatHWAVE(hwave);
    lLen = MediaWaveSizeHWAVE(hwave);
    if (lpfmt == NULL || lLen == 0)
        return 0;

    if (gmedidWave) {
        w = (WORD)((gWaveFormat.wf.nSamplesPerSec + 400) / lpfmt->wf.nSamplesPerSec);
        if (w >= 1)
            lLen *= w;
        else
            lLen /= (WORD)((lpfmt->wf.nSamplesPerSec + 400) / gWaveFormat.wf.nSamplesPerSec);
    }
    return lLen;
}

/*
 * s07:02D6-036F  WavePaste  (NEAR PASCAL)
 *
 * Pastes a piece of sound at lPos; in overwrite mode the samples it
 * replaces are cut first, and put back if the paste fails.  Returns the
 * number of samples pasted.
 */
static LONG NEAR PASCAL WavePaste(HMEDWAVE hwave, LONG lPos)
{
    HMEDWAVE   hwaveOld = 0;
    LONG    l;

    if (!gfInsertMode)
        hwaveOld = medSendMessage(gmedidWave, WAVE_CUT, lPos, WaveConvertedLength(hwave));

    l = medSendMessage(gmedidWave, WAVE_PASTE, lPos, hwave);
    if (l == 0 && hwaveOld) {
        if (medSendMessage(gmedidWave, WAVE_PASTE, lPos, hwaveOld) == 0)
            MediaWaveFreeHWAVE(hwaveOld);
        return 0;
    }
    if (hwaveOld)
        MediaWaveFreeHWAVE(hwaveOld);
    return l;
}

/*
 * s07:0370-0515  WaveInsertSilence  (NEAR PASCAL)
 *
 * Writes lSamples samples of silence at lPos, 16 KB at a time.  Quirks:
 * the position advances by WAVE_WRITE's return value while the count is
 * in bytes; the buffer is filled with 0x80 only up to the first chunk's
 * size (enough, since later chunks are not larger); and the clean-up
 * after a failure compares the sample count with the remaining byte
 * count.
 */
static BOOL NEAR PASCAL WaveInsertSilence(LONG lPos, LONG lSamples)
{
    HGLOBAL         h = 0;
    LPSTR           lpBuf = NULL;
    LONG            lPosStart = lPos;
    LONG            lSamplesStart = lSamples;
    LONG            l;
    BOOL            f16, fStereo;
    WORD            cb;
    WaveWriteStruct ww;
    HMEDWAVE           hw;

    if (lSamples == 0 || gmedidWave == 0)
        return 1;

    fStereo = (gWaveFormat.wf.nChannels == 2);
    f16 = (gWaveFormat.wBitsPerSample == 16);
    if (f16)
        lSamples += lSamples;
    if (fStereo)
        lSamples += lSamples;

    if (lSamples) {
        do {
            cb = ((DWORD)lSamples > 0x4000) ? 0x4000 : (WORD)lSamples;
            if (lpBuf == NULL) {
                h = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, 0x4000L);
                if (h == 0)
                    return 0;
                lpBuf = GlobalLock(h);
                ww.fpchBuffer = lpBuf;
                if (!f16 && cb)
                    _fmemset(lpBuf, 0x80, cb);
            }
            ww.nLength = cb;
            l = medSendMessage(gmedidWave, WAVE_WRITE, lPos, (LONG)(LPVOID)&ww);
            if (l <= 0) {
                if ((DWORD)lSamplesStart > (DWORD)lSamples) {
                    hw = medSendMessage(gmedidWave, WAVE_CUT, lPosStart, lPos - lPosStart);
                    if (hw)
                        MediaWaveFreeHWAVE(hw);
                }
                if (h) {
                    GlobalUnlock(h);
                    GlobalFree(h);
                }
                return 0;
            }
            lPos += l;
            lSamples -= cb;
        } while (lSamples != 0);
    }

    if (h) {
        GlobalUnlock(h);
        GlobalFree(h);
    }
    return 1;
}

/*
 * s07:0516-08F1  WaveInsert  (NEAR PASCAL)
 *
 * Inserts a piece of sound (hwave may be 0) at frame wFrame, for an edit
 * that inserts wFrames video frames and replaces wReplace frames:
 *  - the first sound creates the audio track, with an offset so that it
 *    starts at wFrame;
 *  - samples are cut where the new sound goes (in overwrite mode as much
 *    as the larger of the new sound and wFrames frames, in insert mode
 *    wReplace frames' worth);
 *  - silence fills any gap before the sound;
 *  - afterwards the inserted sound is trimmed or padded with silence to
 *    exactly wFrames frames, to keep the audio in step with the video.
 * The cut-out samples become the undo data.  The overwrite length uses a
 * max() macro, which computes the frame length twice.  When the insertion
 * point is before the start of the sound, the offset is changed and saved
 * as the undo offset after the change.
 */
static BOOL NEAR PASCAL WaveInsert(HMEDWAVE hwave, WORD wFrame, WORD wReplace, WORD wFrames)
{
    MedReturn   mr;
    LONG        lConv;
    LONG        lCut;
    LONG        lLen;
    LONG        lPos;
    LONG        lPasted = 0;
    LONG        lWant;
    HMEDWAVE       hw;

    if (hwave == 0 && gmedidWave == 0)
        return 1;

    if (hwave && gmedidWave == 0) {
        if (!medCreate(&mr, medtypeWAVE, (LONG)MediaWaveFormatHWAVE(hwave)))
            return 0;
        gmedidWave = mr.medid;
        medSendMessage(gmedidWave, WAVE_GETFMT, (LONG)(LPVOID)&gWaveFormat, sizeof(gWaveFormat));
        glWaveOffset = muldivrd32(gWaveFormat.wf.nSamplesPerSec, (DWORD)wFrame * gCompOptions.dwUSecPerFrame, 1000000L);
    }

    lConv = hwave ? WaveConvertedLength(hwave) : 0;

    if (gfInsertMode)
        lCut = WaveFrameToSample(wReplace, FALSE) - WaveFrameToSample(0, FALSE);
    else
        lCut = max(lConv, WaveFrameToSample(wFrames, FALSE) - WaveFrameToSample(0, FALSE));

    lLen = medSendMessage(gmedidWave, WAVE_GETSIZE, 0L, 0L);
    lPos = WaveFrameToSample(wFrame, FALSE);
    if ((DWORD)lPos >= (DWORD)lLen && hwave == 0)
        return 1;

    if (lPos < 0) {
        glWaveOffset += lPos;
        glWaveOffsetUndo = glWaveOffset;
        if (!WaveInsertSilence(0L, -lPos))
            return 0;
        glUndoPos = 0;
        glUndoLen = 0;
        lPos = 0;
    } else if ((DWORD)lLen < (DWORD)lPos) {
        if (!WaveInsertSilence(lLen, lPos - lLen))
            return 0;
        glUndoPos = lLen;
        glUndoLen = lPos - lLen;
        lCut = 0;
    } else {
        glUndoPos = lPos;
        glUndoLen = 0;
    }

    if (lCut && (DWORD)(lCut + glUndoPos) > (DWORD)lLen)
        lCut = lLen - glUndoPos;

    if (lCut) {
        ghwaveUndo = medSendMessage(gmedidWave, WAVE_CUT, glUndoPos, lCut);
        if (ghwaveUndo == 0) {
            glUndoLen = 0;
            return 0;
        }
    }

    if (hwave) {
        lPasted = medSendMessage(gmedidWave, WAVE_PASTE, lPos, hwave);
        if (lPasted == 0) {
            if (ghwaveUndo &&
                medSendMessage(gmedidWave, WAVE_PASTE, glUndoPos, ghwaveUndo))
                ghwaveUndo = 0;
            glUndoLen = 0;
            return 0;
        }
        lPos += lPasted;
        glUndoLen += lPasted;
    } else {
        lPos = WaveFrameToSample(wFrame, FALSE);
        glUndoPos = lPos;
    }

    lLen = medSendMessage(gmedidWave, WAVE_GETSIZE, 0L, 0L);
    if ((DWORD)lLen > (DWORD)lPos && wFrames && gmedidWave) {
        lWant = muldivrd32(gWaveFormat.wf.nSamplesPerSec, (DWORD)wFrames * gCompOptions.dwUSecPerFrame, 1000000L);
        if ((DWORD)lWant < (DWORD)lPasted) {
            hw = medSendMessage(gmedidWave, WAVE_CUT,
                                lPos - (lPasted - lWant), lPasted - lWant);
            if (hw == 0)
                goto fail;
            MediaWaveFreeHWAVE(hw);
        }
        if ((DWORD)lWant > (DWORD)lPasted) {
            if (!WaveInsertSilence(lPos, lWant - lPasted))
                goto fail;
            glUndoLen += lWant - lPasted;
        }
    }
    return 1;

fail:
    SetUndoTracks(0, 1, gfInsertMode);
    Undo();
    SetUndo(0);
    return 0;
}

/*
 * s07:08F2-0995  WaveCopyRange  (NEAR PASCAL)
 *
 * Copies the sound of wCount frames from wStart.  If the range starts
 * before the sound, silence is first inserted at its start (and the
 * offset moved), so even a copy edits the track.
 */
static BOOL NEAR PASCAL WaveCopyRange(WORD wStart, WORD wCount, HMEDWAVE *phwave)
{
    LONG    lPos, lLen;

    if (gmedidWave) {
        lPos = WaveFrameToSample(wStart, TRUE);
        lLen = WaveFrameToSample(wStart + wCount, TRUE) - lPos;
        if (lLen) {
            if (lPos < 0) {
                glWaveOffset += lPos;
                if (!WaveInsertSilence(0L, -lPos))
                    return 0;
                lPos = 0;
            }
            *phwave = medSendMessage(gmedidWave, WAVE_COPY, lPos, lLen);
            if (*phwave == 0)
                return 0;
            return 1;
        }
    }
    *phwave = 0;
    return 1;
}

/*
 * s07:0996-09BD  WaveRelease
 */
void FAR WaveRelease(void)
{
    if (gmedidWave) {
        medRelease(gmedidWave, 0L);
        gmedidWave = 0;
    }
    glWaveOffset = 0;
}

/*
 * s07:09BE-0BF7  WaveInsertElement  (FAR PASCAL)
 *
 * Inserts the sound of a WAVE element (File / Insert, opening a file).
 * With no audio track yet the element itself becomes the track.  If
 * lpwFrames is given it receives the number of frames the element covers
 * (computed as in WaveCountFrames, but the 1% correction is computed and
 * then not used).  An empty audio track is released first.
 */
BOOL FAR PASCAL WaveInsertElement(WORD wFrame, WORD wReplace, WORD wFrames,
                                  LPWORD lpwFrames, MEDID medid)
{
    MedReturn       mr;
    PCMWAVEFORMAT   fmt;
    HMEDWAVE           hwave = 0;
    DWORD           dwLen;
    double          dSamples;
    BOOL            f;
    int             n;

    if (gmedidWave && medSendMessage(gmedidWave, WAVE_GETSIZE, 0L, 0L) == 0)
        WaveRelease();

    glWaveOffsetUndo = glWaveOffset;
    ghwaveUndo = 0;
    glUndoLen = 0;

    if (gmedidWave == 0 && medid == 0)
        return 1;

    if (gmedidWave == 0) {
        gmedidWave = medid;
        medAccess(medid, 0L, &mr, FALSE, NULL, 0L);
        medSendMessage(gmedidWave, WAVE_GETFMT, (LONG)(LPVOID)&gWaveFormat, sizeof(gWaveFormat));
        glWaveOffset = muldivrd32((DWORD)wFrame * gCompOptions.dwUSecPerFrame, gWaveFormat.wf.nSamplesPerSec, 1000000L);
        dwLen = medSendMessage(medid, WAVE_GETSIZE, 0L, 0L);
        glUndoPos = 0;
        glUndoLen = dwLen;
    } else {
        dwLen = medid ? medSendMessage(medid, WAVE_GETSIZE, 0L, 0L) : 0;
        if (dwLen) {
            hwave = medSendMessage(medid, WAVE_COPY, 0L, dwLen);
            if (hwave == 0)
                return 0;
        }
        f = WaveInsert(hwave, wFrame, wReplace, wFrames);
        if (hwave)
            MediaWaveFreeHWAVE(hwave);
        if (!f)
            return 0;
    }

    if (lpwFrames && medid) {
        medSendMessage(medid, WAVE_GETFMT, (LONG)(LPVOID)&fmt, sizeof(fmt));
        dSamples = (double)fmt.wf.nSamplesPerSec * (double)gCompOptions.dwUSecPerFrame * 1e-6;
        *lpwFrames = n = (int)((dSamples * 0.999999999 + (double)dwLen) / dSamples);
        if ((LONG)dwLen - WaveFrameToSample(n - 1, FALSE) < (LONG)dSamples / 100)
            n--;
    }
    return 1;
}

/*
 * s07:0BF8-0D1D  WaveDelete  (FAR PASCAL)
 *
 * Deletes the sound of wCount frames from wStart (including the partial
 * frame at the end of the sound if the range reaches it).  In overwrite
 * mode the gap is filled with silence.
 */
BOOL FAR PASCAL WaveDelete(WORD wStart, WORD wCount)
{
    LONG    lLen, lSize;

    ghwaveUndo = 0;
    glWaveOffsetUndo = glWaveOffset;
    glUndoLen = 0;

    if (gmedidWave == 0)
        return 1;

    if (WaveNumFrames() - wCount == wStart)
        wCount++;

    glUndoPos = WaveFrameToSample(wStart, TRUE);
    lLen = WaveFrameToSample(wStart + wCount, TRUE) - glUndoPos;
    if (lLen == 0) {
        ghwaveUndo = 0;
        return 1;
    }

    lSize = medSendMessage(gmedidWave, WAVE_GETSIZE, 0L, 0L);
    if ((DWORD)(lLen + glUndoPos) > (DWORD)lSize)
        lLen = lSize - glUndoPos;

    if ((DWORD)lSize > (DWORD)glUndoPos) {
        ghwaveUndo = medSendMessage(gmedidWave, WAVE_CUT, glUndoPos, lLen);
        if (ghwaveUndo == 0)
            return 0;
        if (!gfInsertMode && !WaveInsertSilence(glUndoPos, lLen)) {
            if (medSendMessage(gmedidWave, WAVE_PASTE, glUndoPos, ghwaveUndo))
                ghwaveUndo = 0;
            return 0;
        }
    }
    return 1;
}

/*
 * s07:0D1E-0DFD  WaveNumFrames
 *
 * Number of frames the audio track covers (see WaveCountFrames).
 */
int FAR WaveNumFrames(void)
{
    LONG    lLen;
    double  dSamples;
    int     n;

    if (gmedidWave == 0)
        return 0;

    lLen = medSendMessage(gmedidWave, WAVE_GETSIZE, 0L, 0L) + glWaveOffset;
    if (lLen <= 0)
        return 0;

    dSamples = (double)gWaveFormat.wf.nSamplesPerSec * (double)gCompOptions.dwUSecPerFrame * 1e-6;
    n = (int)((dSamples * 0.999999999 + lLen) / dSamples);
    if (lLen - WaveFrameToSample(n - 1, FALSE) < (LONG)dSamples / 100)
        n--;
    return n;
}

/*
 * s07:0DFE-0F1F  WaveUndo
 *
 * Undoes the last audio edit: removes what it inserted (replacing it with
 * silence in overwrite mode), puts back what it removed, swaps the offset
 * back, and keeps the removed samples as the new undo data, so a second
 * undo redoes the edit.
 */
BOOL FAR WaveUndo(void)
{
    HMEDWAVE   hwaveCut = 0;
    BOOL    fSilence = FALSE;
    LONG    lSilence;
    LONG    lSize;
    HMEDWAVE   hw;

    if (glUndoLen) {
        hwaveCut = medSendMessage(gmedidWave, WAVE_CUT, glUndoPos, glUndoLen);
        if (hwaveCut == 0)
            return 0;
        if (!gfInsertMode) {
            lSize = medSendMessage(gmedidWave, WAVE_GETSIZE, 0L, 0L);
            if (lSize > glUndoPos) {
                fSilence = TRUE;
                lSilence = glUndoLen;
                if (!WaveInsertSilence(glUndoPos, lSilence))
                    goto restore;
            }
        }
        glUndoLen = 0;
    }

    if (ghwaveUndo) {
        if (WavePaste(ghwaveUndo, glUndoPos) == 0) {
            if (fSilence) {
                hw = medSendMessage(gmedidWave, WAVE_CUT, glUndoPos, lSilence);
                if (hw)
                    MediaWaveFreeHWAVE(hw);
            }
            goto restore;
        }
        glUndoLen = WaveConvertedLength(ghwaveUndo);
        MediaWaveFreeHWAVE(ghwaveUndo);
    }

    WaveSwapOffset();
    ghwaveUndo = hwaveCut;
    return 1;

restore:
    if (medSendMessage(gmedidWave, WAVE_PASTE, glUndoPos, hwaveCut) == 0)
        MediaWaveFreeHWAVE(hwaveCut);
    return 0;
}

/*
 * s07:0F20-0F43  WaveClearUndo
 */
void FAR WaveClearUndo(void)
{
    if (ghwaveUndo) {
        MediaWaveFreeHWAVE(ghwaveUndo);
        ghwaveUndo = 0;
    }
    glUndoLen = 0;
}

/*
 * s07:0F44-0F79  WaveAdjustOffset  (FAR PASCAL)
 *
 * Synchronize: moves the sound by lMilliseconds.
 */
BOOL FAR PASCAL WaveAdjustOffset(LONG lMilliseconds)
{
    if (gmedidWave) {
        glWaveOffsetUndo = glWaveOffset;
        glWaveOffset += muldivrd32(lMilliseconds, gWaveFormat.wf.nSamplesPerSec, 1000L);
    }
    return 1;
}

/*
 * s07:0F7A-0F9B  WaveSwapOffset
 */
BOOL FAR WaveSwapOffset(void)
{
    LONG l;

    l = glWaveOffset;
    glWaveOffset = glWaveOffsetUndo;
    glWaveOffsetUndo = l;
    return 1;
}

/*
 * s07:0F9C-0F9F  WaveNothing  (FAR PASCAL, unreferenced)
 *
 * An empty function with one LONG argument: just RETF 4.
 */
void FAR PASCAL WaveNothing(LONG l)
{
}

/*
 * s07:0FA0-10D3  WaveOpenDevice
 *
 * Opens the wave mapper in the movie's format (stopping any
 * sndPlaySound sound if the first try fails) with the window ghwndFrame
 * getting the callbacks, and prepares up to 16 buffers of 16 KB.  Fewer
 * than 2 buffers is a failure.  Already open: TRUE.
 */
BOOL FAR WaveOpenDevice(void)
{
    UINT        w;
    LPWAVEHDR   lpwh;

    if (gmedidWave == 0 || ghWaveOut)
        return 1;

    w = waveOutOpen(&ghWaveOut, WAVE_MAPPER, (LPWAVEFORMAT)&gWaveFormat,
                    (DWORD)(WORD)ghwndFrame, 0L, CALLBACK_WINDOW);
    if (w) {
        sndPlaySound(NULL, 0);
        w = waveOutOpen(&ghWaveOut, WAVE_MAPPER, (LPWAVEFORMAT)&gWaveFormat,
                        (DWORD)(WORD)ghwndFrame, 0L, CALLBACK_WINDOW);
    }
    if (w)
        return 0;

    gcWaveBuf = 0;
    do {
        lpwh = (LPWAVEHDR)GAllocPtrF(GMEM_MOVEABLE | GMEM_SHARE, sizeof(WAVEHDR) + 0x4000L);
        galpWaveHdr[gcWaveBuf] = lpwh;
        if (lpwh == NULL)
            break;
        lpwh->dwFlags = WHDR_DONE;
        lpwh->lpData = (LPSTR)(lpwh + 1);
        lpwh->dwBufferLength = 0x4000;
        if (waveOutPrepareHeader(ghWaveOut, galpWaveHdr[gcWaveBuf], sizeof(WAVEHDR))) {
            GFreePtr(galpWaveHdr[gcWaveBuf]);
            break;
        }
    } while (++gcWaveBuf < 16);

    if (gcWaveBuf < 2) {
        WaveCloseDevice();
        return 0;
    }

    gcWaveQueued = giWaveNext = gfWavePlaying = 0;
    return 1;
}

/*
 * s07:10D4-12C5  WaveFillBuffers  (NEAR)
 *
 * Fills and queues every free buffer: silence before the start of the
 * sound, then WAVE_READ in the movie's format; loops back to glPlayLoop
 * at the end if gfPlayLoop.  The silence is written one byte (8-bit) or
 * one word (16-bit) per sample, whatever the number of channels.
 */
static BOOL NEAR WaveFillBuffers(void)
{
    WaveReadStruct  wr;                 /* nLength and fpchBuffer double as counters */
    DWORD           dwMax;
    LONG            lCount;
    LONG            lSilence;
    LONG            l;

    if (!gfWavePlaying)
        return 1;

    while (gcWaveQueued < gcWaveBuf) {
        if (glPlayEnd <= glPlayPos) {
            if (!gfPlayLoop)
                return 1;
            glPlayPos = glPlayLoop;
        }

        dwMax = (WORD)0x4000 / gWaveFormat.wf.nBlockAlign;
        lCount = glPlayEnd - glPlayPos;
        if (lCount > (LONG)dwMax)
            lCount = dwMax;
        wr.nLength = lCount;
        wr.fpchBuffer = galpWaveHdr[giWaveNext]->lpData;

        if (glPlayPos < 0) {
            lSilence = -glPlayPos;
            if (lSilence > wr.nLength)
                lSilence = wr.nLength;
            if (gWaveFormat.wBitsPerSample == 8) {
                for (l = 0; l < lSilence; l++)
                    *wr.fpchBuffer++ = (char)0x80;
            } else {
                for (l = 0; l < lSilence; l++) {
                    *(int FAR *)wr.fpchBuffer = 0;
                    wr.fpchBuffer += 2;
                }
            }
            glPlayPos += lSilence;
            wr.nLength -= lSilence;
        }

        if (glPlayPos >= 0) {
            wr.nPosition = glPlayPos;
            l = medSendMessage(gmedidWave, WAVE_READ, (LONG)(LPVOID)&wr,
                               (LONG)(LPVOID)&gWaveFormat);
            if (l != wr.nLength)
                return 0;
            glPlayPos += wr.nLength;
        }

        galpWaveHdr[giWaveNext]->dwBufferLength = lCount * gWaveFormat.wf.nBlockAlign;
        if (waveOutWrite(ghWaveOut, galpWaveHdr[giWaveNext], sizeof(WAVEHDR)))
            return 0;
        gcWaveQueued++;
        if (++giWaveNext >= gcWaveBuf)
            giWaveNext = 0;
    }
    return 1;
}

/*
 * s07:12C6-1395  WavePlay  (FAR PASCAL)
 *
 * Plays the sound of wCount frames from wStart, lMsOffset milliseconds
 * early.  While playing, a new call only moves the end.  fWait: wait
 * (yielding) until all buffers have played.
 */
BOOL FAR PASCAL WavePlay(WORD wStart, WORD wCount, LONG lMsOffset, BOOL fWait, BOOL fLoop)
{
    LONG    lBase, lStart, lEnd;

    if (gmedidWave == 0)
        return 1;
    if (!WaveOpenDevice())
        return 0;

    lBase = muldivrd32(lMsOffset, gWaveFormat.wf.nSamplesPerSec, 1000L);
    lStart = WaveFrameToSample(wStart, TRUE) - lBase;
    lEnd = WaveFrameToSample(wStart + wCount, TRUE) - lBase;
    if (lEnd > lStart) {
        if (!gfWavePlaying) {
            waveOutPause(ghWaveOut);
            glPlayLoop = glPlayPos = lStart;
            glPlayEnd = lEnd;
            gfWavePlaying = TRUE;
        } else {
            glPlayEnd = lEnd;
        }
        gfPlayLoop = fLoop;
        WaveFillBuffers();
        waveOutRestart(ghWaveOut);
        if (fWait)
            while (gcWaveQueued)
                Yield();
    }
    return 1;
}

/*
 * s07:1396-13AB  WaveOutMessage  (FAR PASCAL)
 *
 * Called by the window that gets the waveOut callbacks.
 */
void FAR PASCAL WaveOutMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (msg == MM_WOM_DONE) {
        gcWaveQueued--;
        WaveFillBuffers();
    }
}

/*
 * s07:13AC-13C3  WaveStop
 */
void FAR WaveStop(void)
{
    if (ghWaveOut) {
        waveOutReset(ghWaveOut);
        gfWavePlaying = FALSE;
    }
}

/*
 * s07:13C4-1427  WaveCloseDevice
 */
void FAR WaveCloseDevice(void)
{
    WaveStop();
    if (ghWaveOut) {
        while (gcWaveBuf) {
            waveOutUnprepareHeader(ghWaveOut, galpWaveHdr[--gcWaveBuf], sizeof(WAVEHDR));
            GFreePtr(galpWaveHdr[gcWaveBuf]);
        }
        waveOutClose(ghWaveOut);
        ghWaveOut = 0;
    }
}

/*
 * s07:1428-1465  WaveFrameBytes  (FAR PASCAL)
 *
 * Bytes of sound in frame wFrame.
 */
void FAR PASCAL WaveFrameBytes(WORD wFrame, LPDWORD lpdwBytes)
{
    LONG l;

    l = WaveFrameToSample(wFrame, TRUE);
    *lpdwBytes = (DWORD)(WaveFrameToSample(wFrame + 1, TRUE) - l) * gWaveFormat.wf.nBlockAlign;
}

/*
 * s07:1466-1583  WaveGetRangeElement  (FAR PASCAL)
 *
 * A WAVE element with the sound of wCount frames from wStart, for saving
 * or extracting.  If fShare and the range is the whole movie (and the
 * sound has no offset), the audio track itself is returned with another
 * access.  The size of the copied sound is asked for and ignored.
 */
BOOL FAR PASCAL WaveGetRangeElement(WORD wStart, WORD wCount, MEDID *pmedid, BOOL fShare)
{
    MedReturn   mr;
    HMEDWAVE       hwave;
    BOOL        f;

    if (gmedidWave == 0) {
        *pmedid = 0;
        return 1;
    }

    if (fShare && gmedidWave && glWaveOffset == 0 && wStart == 0 && wCount == gwLengthShown &&
        medSendMessage(gmedidWave, WAVE_GETSIZE, 0L, 0L)) {
        medAccess(gmedidWave, 0L, &mr, FALSE, NULL, 0L);
        *pmedid = gmedidWave;
        return 1;
    }

    f = WaveCopyRange(wStart, wCount, &hwave);
    if (!f || hwave == 0) {
        *pmedid = 0;
        return f;
    }

    if (!medCreate(&mr, medtypeWAVE, (LONG)(LPVOID)&gWaveFormat)) {
        MediaWaveFreeHWAVE(hwave);
        return 0;
    }
    MediaWaveSizeHWAVE(hwave);
    if (!medSendMessage(mr.medid, WAVE_PASTE, 0L, hwave)) {
        medRelease(mr.medid, 0L);
        MediaWaveFreeHWAVE(hwave);
        return 0;
    }
    MediaWaveFreeHWAVE(hwave);
    *pmedid = mr.medid;
    return 1;
}

/*
 * s07:1584-158D  WaveCanPaste
 */
BOOL FAR WaveCanPaste(void)
{
    return IsClipboardFormatAvailable(gcfWave);
}

/*
 * s07:158E-1609  WaveCopyToClipboard  (FAR PASCAL)
 *
 * Puts a copy of the sound of wCount frames from wStart on the clipboard
 * in the private format, and offers CF_WAVE for delayed rendering.  The
 * clipboard must be open.
 */
BOOL FAR PASCAL WaveCopyToClipboard(WORD wStart, WORD wCount)
{
    HMEDWAVE           hwave;
    BOOL            f;
    HGLOBAL         h;
    HMEDWAVE FAR      *lp;

    f = WaveCopyRange(wStart, wCount, &hwave);
    if (f && hwave) {
        h = GlobalAlloc(GMEM_MOVEABLE | GMEM_SHARE, 4L);
        if (h == 0)
            return 0;
        lp = (HMEDWAVE FAR *)GlobalLock(h);
        *lp = hwave;
        GlobalUnlock(h);
        SetClipboardData(gcfWave, h);
        SetClipboardData(CF_WAVE, 0);
        ghwaveClip = hwave;
    }
    return f;
}

/*
 * s07:160A-1711  WaveRenderClipboard
 *
 * Renders CF_WAVE: a RIFF WAVE image of the clipboard sound.  Bug: the
 * "is there any sound" test looks at the byte count before it has been
 * computed (an uninitialised local), so whether anything is rendered
 * depends on what was on the stack.  The length passed to
 * MediaWaveReadHWAVE is in bytes.
 */
void FAR WaveRenderClipboard(void)
{
    PCMWAVEFORMAT FAR  *lpfmt;
    LONG                lLen;
    DWORD               cb;
    HGLOBAL             h;
    LPSTR               lp;

    lpfmt = MediaWaveFormatHWAVE(ghwaveClip);
    lLen = MediaWaveSizeHWAVE(ghwaveClip);
    if (lpfmt == NULL || cb == 0)
        return;

    cb = (LONG)lpfmt->wf.nBlockAlign * lLen;
    h = GlobalAlloc(GMEM_MOVEABLE, cb + 44);
    if (h == 0)
        return;
    lp = GlobalLock(h);
    *(DWORD FAR *)(lp + 0) = mmioFOURCC('R', 'I', 'F', 'F');
    *(DWORD FAR *)(lp + 4) = (cb & 1) + cb + 36;
    *(DWORD FAR *)(lp + 8) = mmioFOURCC('W', 'A', 'V', 'E');
    *(DWORD FAR *)(lp + 12) = mmioFOURCC('f', 'm', 't', ' ');
    *(DWORD FAR *)(lp + 16) = 16;
    hmemmove(lp + 20, lpfmt, 16L);
    *(DWORD FAR *)(lp + 36) = mmioFOURCC('d', 'a', 't', 'a');
    *(DWORD FAR *)(lp + 40) = cb;
    MediaWaveReadHWAVE(ghwaveClip, 0L, cb, lp + 44, NULL);
    GlobalUnlock(h);
    SetClipboardData(CF_WAVE, h);
}

/*
 * s07:1712-1837  WaveClipboardFrames
 *
 * Number of frames the clipboard sound covers at the movie's rate (or
 * its own rate when there is no audio track).  The clipboard data is
 * locked and never unlocked.
 */
int FAR WaveClipboardFrames(void)
{
    HGLOBAL             h;
    HMEDWAVE FAR          *lp;
    HMEDWAVE               hwave;
    PCMWAVEFORMAT FAR  *lpfmt;
    LONG                lLen;
    DWORD               dwRate;
    double              dSamples;
    int                 n;

    h = GetClipboardData(gcfWave);
    if (h == 0)
        return 0;
    lp = (HMEDWAVE FAR *)GlobalLock(h);
    if (lp == NULL)
        return 0;
    hwave = *lp;
    lpfmt = MediaWaveFormatHWAVE(hwave);
    lLen = WaveConvertedLength(hwave);
    if (lpfmt == NULL || lLen == 0)
        return 0;

    dwRate = gmedidWave ? gWaveFormat.wf.nSamplesPerSec : lpfmt->wf.nSamplesPerSec;
    dSamples = (double)gCompOptions.dwUSecPerFrame * (double)dwRate * 1e-6;
    n = (int)((dSamples * 0.999999999 + lLen) / dSamples);
    if (lLen - WaveFrameToSample(n - 1, FALSE) < (LONG)dSamples / 100)
        n--;
    return n;
}

/*
 * s07:1838-18C3  WavePasteFromClipboard  (FAR PASCAL)
 *
 * Pastes the clipboard sound (or nothing, just keeping the track in step,
 * if there is none) for an edit inserting wFrames frames at wFrame.  The
 * clipboard data is locked and never unlocked.
 */
BOOL FAR PASCAL WavePasteFromClipboard(WORD wFrame, WORD wReplace, WORD wFrames)
{
    HMEDWAVE       hwave = 0;
    HGLOBAL     h;
    HMEDWAVE FAR  *lp;

    glWaveOffsetUndo = glWaveOffset;
    ghwaveUndo = 0;
    glUndoLen = 0;

    h = GetClipboardData(gcfWave);
    if (h) {
        lp = (HMEDWAVE FAR *)GlobalLock(h);
        if (lp == NULL)
            return 0;
        hwave = *lp;
    }

    if (gmedidWave && medSendMessage(gmedidWave, WAVE_GETSIZE, 0L, 0L) == 0)
        WaveRelease();

    return WaveInsert(hwave, wFrame, wReplace, wFrames);
}

/*
 * s07:18C4-18E3  WaveDestroyClipboard
 */
void FAR WaveDestroyClipboard(void)
{
    if (IsClipboardFormatAvailable(gcfWave)) {
        MediaWaveFreeHWAVE(ghwaveClip);
        ghwaveClip = 0;
    }
}

/*
 * s07:18E4-1919  WaveGetFormatInfo  (FAR PASCAL)
 */
BOOL FAR PASCAL WaveGetFormatInfo(LPWORD lpwBits, LPDWORD lpdwRate, LPWORD lpwChannels)
{
    if (gmedidWave == 0)
        return 0;
    *lpwChannels = gWaveFormat.wf.nChannels;
    *lpdwRate = gWaveFormat.wf.nSamplesPerSec;
    *lpwBits = gWaveFormat.wBitsPerSample;
    return 1;
}

/*
 * s07:191A-1AC9  WaveLoadIntoMemory  (FAR PASCAL)
 *
 * Video / Load File into Memory, for the sound: reads the whole track
 * and writes it into a new WAVE element, which replaces the track.  The
 * argument is not used.
 */
BOOL FAR PASCAL WaveLoadIntoMemory(WORD wUnused)
{
    LONG            lLen;
    LPSTR           lpBuf;
    MedReturn       mr;
    WaveReadStruct  wr;
    WaveWriteStruct ww;
    LONG            lWritten;
    HCURSOR         hcurOld;

    if (gmedidWave == 0)
        return 1;

    StatusSetMessage((LPCSTR)MAKEINTRESOURCE(IDS_LOADINGAUDIO));
    lLen = medSendMessage(gmedidWave, WAVE_GETSIZE, 0L, 0L);
    lpBuf = GAllocPtrF(GMEM_MOVEABLE, (DWORD)lLen * gWaveFormat.wf.nBlockAlign);
    if (lpBuf == NULL)
        return 0;

    if (!medCreate(&mr, medtypeWAVE, (LONG)(LPVOID)&gWaveFormat)) {
        GFreePtr(lpBuf);
        return 0;
    }

    hcurOld = SetCursor(LoadCursor(0, IDC_WAIT));
    wr.nLength = lLen;
    wr.fpchBuffer = lpBuf;
    wr.nPosition = 0;
    if (medSendMessage(gmedidWave, WAVE_READ, (LONG)(LPVOID)&wr,
                       (LONG)(LPVOID)&gWaveFormat) != lLen) {
        GFreePtr(lpBuf);
        medRelease(mr.medid, 0L);
        if (hcurOld)
            SetCursor(hcurOld);
        return 0;
    }

    ww.nLength = lLen * gWaveFormat.wf.nBlockAlign;
    ww.fpchBuffer = lpBuf;
    lWritten = medSendMessage(mr.medid, WAVE_WRITE, 0L, (LONG)(LPVOID)&ww);
    GFreePtr(lpBuf);
    if (hcurOld)
        SetCursor(hcurOld);

    if (lWritten != lLen) {
        medRelease(mr.medid, 0L);
        return 0;
    }

    medRelease(gmedidWave, 0L);
    gmedidWave = mr.medid;
    return 1;
}
