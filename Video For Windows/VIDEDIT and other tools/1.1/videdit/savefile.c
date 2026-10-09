/*
 * savefile.c - code segment 11 of VIDEDIT.EXE, s11:0A22-1480: saving
 * through MediaMan.
 *
 * A movie (or the extracted part of one) is saved by creating a DIBS
 * ("sequence of DIBs", MEDDIBS.MMH) element whose frames come from a
 * physical type VidEdit registers for the occasion ('AVIE', or the next
 * free four-character code) with SaveHandler as its handler, and saving
 * that element with medSaveAs as the target type.  The writer then asks
 * the handler for the streams, formats, compressor and state, and the
 * frames one by one; the audio is a WAVE element of the range, kept in
 * gmedidSaveWave.  The audio track, MIDI (a stub in this version) and a
 * single frame (an MDIB element) are saved directly.
 *
 * Data: DGROUP module at DS:0700-074F (initialised) and DS:1D30-1D43.
 * Functions are in the order of the binary.
 */

#include "videdit.h"
#include "compress.h"
#include "savefile.h"

/* string table */
#define IDS_CAPTION             126     /* "VidEdit" */
#define IDS_FRAMENOMEMORY       230     /* "Unable to construct video frame to save: Out of memory." */
#define IDS_PREPAREAUDIO        250     /* "Unable to prepare waveform audio information for saving." */
#define IDS_PREPAREMIDI         251     /* (not in the string table) */
#define IDS_CREATEIMAGE         252     /* "Unable to create image to save." */
#define IDS_USERCANCELLED       276     /* "User cancelled the save operation." */
#define IDS_PALETTETOOLARGE     283     /* "This video contains a palette that is too large ..." */
#define IDS_INTERNALERROR       297     /* "Internal VidEdit error." */

#define MEDTYPE_AVIE            medFOURCC('A', 'V', 'I', 'E')
#define MEDTYPE_DIBS            medFOURCC('D', 'I', 'B', 'S')
#define MEDTYPE_MDIB            medFOURCC('M', 'D', 'I', 'B')
#define MEDTYPE_DSEQ            medFOURCC('D', 'S', 'E', 'Q')
#define FCC_NONE                mmioFOURCC('n', 'o', 'n', 'e')

/* the messages the DIBS writer sends to the frame source (the same ones
   VidEdit sends to an AVI movie element) */
#define SRC_STREAMTYPE          (MED_USER + 1)  /* lParam1 stream -> 'vids'/'auds' */
#define SRC_FORMAT              (MED_USER + 2)  /* (buffer, stream) -> size */
#define SRC_HANDLER             (MED_USER + 3)  /* lParam1 stream -> compressor */
#define SRC_STATE               (MED_USER + 4)  /* (buffer, stream) -> size */
#define SRC_FRAME               (MED_USER + 5)  /* (frame, stream) -> MAKELONG(hdib, progress | flags) */
#define SRC_FRAMES              (MED_USER + 6)  /* -> number of frames */
#define WAVE_GETFMT             (MED_USER + 10)
#define WAVE_GETFMTSIZE         (MED_USER + 11)

/* creation data of a DIBS element */
typedef struct {
    DWORD   dwFlags;                    /* 00 DCF_* */
    DWORD   dw04;                       /* 04 0x1F */
    DWORD   dwWidth;                    /* 08 */
    DWORD   dwHeight;                   /* 0C */
    DWORD   dwDataRate;                 /* 10 0 = no limit */
    DWORD   dwUSecPerFrame;             /* 14 */
    DWORD   dwStreams;                  /* 18 */
    DWORD   dwQuality;                  /* 1C */
    DWORD   dw20;                       /* 20 not set */
    DWORD   dwKeyFrameEvery;            /* 24 0 = no key frames */
    DWORD   dwInterleave;               /* 28 0 = not interleaved */
    DWORD   dwFrames;                   /* 2C */
    MEDTYPE medtypeSource;              /* 30 type of the frame source */
} DIBSCREATE;                           /* 0x34 bytes */

#define DCF_NOTINTERLEAVED      0x0001L
#define DCF_NOPAD               0x00C0L
#define DCF_NORECOMPRESS        0x0100L

/* MDIBCreateStruct of MEDBITS.H */
typedef struct {
    DWORD           dwWidth;            /* 00 */
    DWORD           dwHeight;           /* 04 */
    WORD            wDepth;             /* 08 */
    MEDID           medidPalette;       /* 0A */
    WORD            wPalSize;           /* 0E */
    BOOL            fRGBQuads;          /* 10 */
    LPPALETTEENTRY  lpPalEntries;       /* 12 */
} MDIBCREATE;                           /* 0x16 bytes */

/* initialised data, DS:0700-074F (after the module's "AnimSTrackBar",
   "GrayBlob" header strings and three "Error Saving File") */
WORD        gfSaveDSEQ      = 0;        /* DS:074E saving to a DIB sequence */

/* uninitialised data */
WORD        gwSaveStart;                /* DS:1D30 first frame to save */
WORD        gwSaveCount;                /* DS:1D32 */
MEDID       gmedidSaveWave;             /* DS:1D34 the audio of the range */
MEDID       gmedidSaveMidi;             /* DS:1D38 */
WORD        gfSaveNoRecompress;         /* DS:1D3C compressor "none": frames as stored */
HPALETTE    ghpalSaveLast;              /* DS:1D3E palette of the last 8 bit frame */
WORD        gfSaveLastStatic;           /* DS:1D40 it used the static colours */
WORD        gfSavePalWarned;            /* DS:1D42 */

/*
 * s11:0A22-0D0F  SaveMovie  (FAR PASCAL)
 *
 * Saves frames wStart.. (wCount of them), with their audio if fAudio,
 * as pszFile of type mtSave, with the current compression options (or,
 * for "none", the frames as they are stored with the key frame and
 * quality settings of DS:2D2A).  Returns 1 if saved; errors are shown.
 * wUnused is not used.
 */
WORD FAR PASCAL SaveMovie(WORD wStart, WORD wCount, PSTR pszFile, MEDTYPE mtSave, WORD wUnused,
                          BOOL fAudio, BOOL fMidi)
{
    DIBSCREATE  dc;                     /* [bp-48] */
    MedReturn   ret;                    /* [bp-14] */
    MEDID       medid = 0;              /* [bp-0C] */
    FPMedHandler lpfn;                  /* [bp-08] */
    MEDTYPE     mt;                     /* [bp-04] */
    WORD        ids;
    WORD        w;

    lpfn = (FPMedHandler)MakeProcInstance((FARPROC)SaveHandler, ghInst);
    if (!lpfn) {
        ids = IDS_INTERNALERROR;
        goto error;
    }
    mt = MEDTYPE_AVIE;
    if (!medRegisterType(mt, lpfn, MEDTYPE_PHYSICAL)) {
        do
            mt++;
        while (!medRegisterType(mt, lpfn, MEDTYPE_PHYSICAL));
    }

    if (fAudio) {
        if (!WaveGetRangeElement(wStart, wCount, &gmedidSaveWave, TRUE)) {
            ids = IDS_PREPAREAUDIO;
            goto cleanup;
        }
    } else {
        gmedidSaveWave = 0;
    }
    if (fMidi) {
        if (!MidiGetRangeElement(wStart, wCount, (MEDID NEAR *)&gmedidSaveMidi)) {
            ids = IDS_PREPAREMIDI;
            goto cleanup;
        }
    } else {
        gmedidSaveMidi = 0;
    }

    dc.dwFlags = 0;
    dc.dw04 = 0x1F;
    dc.dwUSecPerFrame = gCompOptions.dwUSecPerFrame;
    dc.dwDataRate = gCompOptions.dwDataRate;
    dc.dwWidth = gcxMovie;
    dc.dwHeight = gcyMovie;
    w = (gmedidSaveMidi != 0);
    dc.dwStreams = (long)((gmedidSaveWave != 0) + w + 1);
    dc.dwFrames = wCount;
    dc.medtypeSource = mt;
    dc.dwQuality = gCompOptions.dwQuality;
    dc.dwKeyFrameEvery = gCompOptions.dwKeyFrameEvery;
    dc.dwInterleave = gCompOptions.dwInterleave;
    if (gCompOptions.dwFlags & COMPF_NOPAD)
        dc.dwFlags = DCF_NOPAD;
    if (!(gCompOptions.dwFlags & COMPF_KEYFRAMES))
        dc.dwKeyFrameEvery = 0;
    if (!(gCompOptions.dwFlags & COMPF_INTERLEAVE))
        dc.dwFlags |= DCF_NOTINTERLEAVED;
    if (!(gCompOptions.dwFlags & COMPF_INTERLEAVE))
        dc.dwInterleave = 0;
    if (!(gCompOptions.dwFlags & COMPF_DATARATE) || dc.dwDataRate == 0)
        dc.dwDataRate = 0;
    gwSaveStart = wStart;
    gwSaveCount = wCount;
    gfSaveNoRecompress = (gCompOptions.fccHandler == FCC_NONE);
    if (gfSaveNoRecompress) {
        dc.dwFlags |= DCF_NORECOMPRESS;
        dc.dwKeyFrameEvery = gCompOptionsSaved.dwKeyFrameEvery;
        dc.dwQuality = gCompOptionsSaved.dwQuality;
    }

    if (!medCreate(&ret, MEDTYPE_DIBS, (LONG)(LPVOID)&dc)) {
        ids = IDS_INTERNALERROR;
        goto cleanup;
    }
    medid = ret.medid;
    if (!medSetPhysicalType(medid, mtSave)) {
        ids = IDS_INTERNALERROR;
        goto cleanup;
    }
    ids = medSaveAs(medid, &ret, (LPSTR)pszFile, 0L, TRUE, (FPMedDiskInfoCallback)WrkProgBarCB,
                    (LONG)(UINT)ghwndApp);
    if (ids == MEDF_OK) {
        medRelease(ret.medid, 0L);
        medUnregisterType(mt);
        FreeProcInstance((FARPROC)lpfn);
        return 1;
    }
    if (ids == MEDF_ERROR)
        WrkShowResError(ghwndApp, "Error Saving File");
    ids = 0;

cleanup:
    if (medid) {
        medRelease(medid, 0L);
    } else {
        if (gmedidSaveWave) {
            medRelease(gmedidSaveWave, 0L);
            gmedidSaveWave = 0;
        }
        if (gmedidSaveMidi) {
            medRelease(gmedidSaveMidi, 0L);
            gmedidSaveMidi = 0;
        }
    }
    medUnregisterType(mt);
    FreeProcInstance((FARPROC)lpfn);
error:
    if (ids)
        ErrorResBox(ghwndApp, ghInst, MB_ICONEXCLAMATION, IDS_CAPTION, ids);
    return 0;
}

/*
 * s11:0D10-0DC9  SaveAudio  (FAR PASCAL)
 *
 * Saves the audio of frames wStart.. as pszFile of type mtSave.  On
 * success the new element is released, not the WAVE element of the
 * range.
 */
WORD FAR PASCAL SaveAudio(WORD wStart, WORD wCount, PSTR pszFile, MEDTYPE mtSave)
{
    MedReturn   ret;                    /* [bp-0C] */
    MEDID       medid;                  /* [bp-04] */
    WORD        ids;
    WORD        r;

    if (!WaveGetRangeElement(wStart, wCount, &medid, FALSE)) {
        ids = IDS_PREPAREAUDIO;
        goto error;
    }
    if (!medSetPhysicalType(medid, mtSave)) {
        medRelease(medid, 0L);
        ids = IDS_INTERNALERROR;
        goto error;
    }
    r = medSaveAs(medid, &ret, (LPSTR)pszFile, 0L, TRUE, (FPMedDiskInfoCallback)WrkProgBarCB,
                  (LONG)(UINT)ghwndApp);
    if (r == MEDF_OK) {
        medRelease(ret.medid, 0L);
        return r;
    }
    medRelease(medid, 0L);
    if (r == MEDF_ERROR)
        WrkShowResError(ghwndApp, "Error Saving File");
    return 0;

error:
    ErrorResBox(ghwndApp, ghInst, MB_ICONEXCLAMATION, IDS_CAPTION, ids);
    return 0;
}

/*
 * s11:0DCA-0E43  SaveMidi  (FAR PASCAL)
 *
 * The same for the MIDI track, without error messages.  The MIDI
 * functions are stubs in this version, so this always fails.
 */
WORD FAR PASCAL SaveMidi(WORD wStart, WORD wCount, PSTR pszFile, MEDTYPE mtSave)
{
    MedReturn   ret;                    /* [bp-0C] */
    MEDID       medid;                  /* [bp-04] */

    if (!MidiGetRangeElement(wStart, wCount, (MEDID NEAR *)&medid))
        return 0;
    if (!medSetPhysicalType(medid, mtSave)) {
        medRelease(medid, 0L);
        return 0;
    }
    if (medSaveAs(medid, &ret, (LPSTR)pszFile, 0L, TRUE, (FPMedDiskInfoCallback)WrkProgBarCB,
                  (LONG)(UINT)ghwndApp) == MEDF_OK) {
        medRelease(ret.medid, 0L);
        return 1;
    }
    medRelease(medid, 0L);
    return 0;
}

/*
 * s11:0E44-101D  SaveFrame  (FAR PASCAL)
 *
 * Saves frame wFrame (made with gwVideoBits set to 8) as an MDIB element of
 * type mtSave.
 *
 * Quirk: the frame's width and height for medUnlock are read after the
 * frame's memory was unlocked.
 */
WORD FAR PASCAL SaveFrame(WORD wFrame, PSTR pszFile, MEDTYPE mtSave)
{
    MDIBCREATE  mc;                     /* [bp-34] */
    MedReturn   ret;                    /* [bp-1E] */
    struct {
        DWORD   dw;
        WORD    cx;
        WORD    cy;
    } chg;                              /* [bp-16] */
    MEDID       medid;                  /* [bp-0E] */
    LPBITMAPINFOHEADER lpbi;
    LPBITMAPINFOHEADER lpbiDst;
    HANDLE      h;                      /* [bp-02] */
    WORD        wSave;
    WORD        ids;
    WORD        r;

    wSave = gwVideoBits;
    gwVideoBits = 8;
    h = (HANDLE)GetFullFrame(wFrame, NULL, 0x41);
    gwVideoBits = wSave;
    if (!h) {
        ids = IDS_CREATEIMAGE;
        goto error;
    }

    lpbi = (LPBITMAPINFOHEADER)GlobalLock(h);
    if (lpbi->biClrUsed == 0 && lpbi->biBitCount <= 8)
        lpbi->biClrUsed = (long)(1 << lpbi->biBitCount);
    mc.dwWidth = gcxMovie;
    mc.dwHeight = gcyMovie;
    mc.wDepth = lpbi->biBitCount;
    mc.medidPalette = 0;
    mc.wPalSize = (WORD)lpbi->biClrUsed;
    mc.fRGBQuads = TRUE;
    mc.lpPalEntries = (LPPALETTEENTRY)((LPBYTE)lpbi + (WORD)lpbi->biSize);
    if (!medCreate(&ret, MEDTYPE_MDIB, (LONG)(LPVOID)&mc)) {
        GlobalUnlock(h);
        ids = IDS_INTERNALERROR;
        goto error;
    }
    medid = ret.medid;

    lpbiDst = (LPBITMAPINFOHEADER)medLock(medid, 0x0002, 0L);
    hmemmove((LPBYTE)lpbiDst + (WORD)lpbiDst->biSize + (WORD)lpbiDst->biClrUsed * 4,
             (LPBYTE)lpbi + (WORD)lpbi->biSize + (WORD)lpbi->biClrUsed * 4,
             lpbi->biSizeImage);
    GlobalUnlock(h);
    if (!medSetPhysicalType(medid, mtSave)) {
        medRelease(medid, 0L);
        ids = IDS_INTERNALERROR;
        goto error;
    }

    chg.dw = 0;
    chg.cx = (WORD)lpbi->biWidth;
    chg.cy = (WORD)lpbi->biHeight;
    medUnlock(medid, 4, (DWORD)(LPVOID)&chg, 0L);
    r = medSaveAs(medid, &ret, (LPSTR)pszFile, 0L, TRUE, (FPMedDiskInfoCallback)WrkProgBarCB,
                  (LONG)(UINT)ghwndApp);
    if (r == MEDF_OK) {
        medRelease(ret.medid, 0L);
        return r;
    }
    if (r == MEDF_ERROR)
        WrkShowResError(ghwndApp, "Error Saving File");
    medRelease(medid, 0L);
    return 0;

error:
    ErrorResBox(ghwndApp, ghInst, MB_ICONEXCLAMATION, IDS_CAPTION, ids);
    return 0;
}

/*
 * s11:101E-1480  SaveHandler  (FAR PASCAL, loads DS from SS, a MediaMan
 * handler registered by SaveMovie)
 *
 * Serves the movie to the DIBS writer.  A frame is answered with
 * MAKELONG(hdib, progress in percent | flags); with the compressor
 * "none" the frames come as stored (flag 0x100 = key frame).  When the
 * palette of 8 bit frames changes in a way that palette changes can't
 * handle (from or to the static colours, or more than 236 colours), the
 * user is asked once whether to go on.
 *
 * Quirks of the original:
 *  - SRC_HANDLER for the audio stream works out whether there is audio
 *    and then returns 0 anyway.
 *  - The frame index is compared with the number of frames as a WORD.
 */
DWORD FAR PASCAL _loadds SaveHandler(MEDID medid, MEDMSG msg, MEDINFO medinfo, LONG lParam1, LONG lParam2)
{
    char        achText[240];           /* [bp-152] */
    char        achApp[80];             /* [bp-62] */
    BOOL        fKey;                   /* [bp-12] */
    HPALETTE    hpal;                   /* [bp-10] */
    LPBITMAPINFOHEADER lpbi;
    DWORD       dwFlags;                /* [bp-0A] */
    HANDLE      h;                      /* [bp-06] */
    WORD        wPercent;               /* [bp-04] */
    WORD        fStatic;                /* [bp-02] */
    DWORD       cb;
    UINT        cbHdr;
    WORD        wFlags;
    HANDLE      hdib;

    switch (msg) {
    case MED_UNLOAD:
        if (gmedidSaveWave) {
            medRelease(gmedidSaveWave, 0L);
            gmedidSaveWave = 0;
        }
        if (gmedidSaveMidi) {
            medRelease(gmedidSaveMidi, 0L);
            gmedidSaveMidi = 0;
        }
        break;

    case MED_SETPHYSICAL:
    case MED_GETLOADPARAM:
    case MED_FREELOADPARAM:
    case MED_GETSAVEPARAM:
    case MED_FREESAVEPARAM:
        return 1L;

    case 0x0080:
        if (!gmedidMovie)
            break;
    forward:
        return medSendMessage(gmedidMovie, msg, lParam1, lParam2);

    case SRC_STREAMTYPE:
        if (lParam1 == 0)
            return streamtypeVIDEO;
        if (lParam1 == 1 && gmedidSaveWave)
            return streamtypeAUDIO;
        break;

    case SRC_FORMAT:
        if (lParam2 == 0) {
            if (gfSaveNoRecompress)
                goto forward;
            h = (HANDLE)GetFullFrame(gwSaveStart, NULL, 0x40);
            if (!h)
                break;
            lpbi = (LPBITMAPINFOHEADER)GlobalLock(h);
            cbHdr = ((WORD)lpbi->biClrUsed + 10) * 4;
            if (lParam1)
                lmemcpy((LPSTR)lParam1, (LPSTR)lpbi, cbHdr);
            GlobalUnlock(h);
            return (DWORD)cbHdr;
        }
        if (lParam2 == 1 && gmedidSaveWave) {
            cb = medSendMessage(gmedidSaveWave, WAVE_GETFMTSIZE, 0L, 0L);
            if (lParam1)
                medSendMessage(gmedidSaveWave, WAVE_GETFMT, lParam1, cb);
            return cb;
        }
        break;

    case SRC_HANDLER:
        if (lParam1 == 0)
            return gfSaveNoRecompress ? gCompOptionsSaved.fccHandler : gCompOptions.fccHandler;
        /* lParam1 == 1: (gmedidSaveWave != 0) is computed and dropped */
        break;

    case SRC_STATE:
        if (lParam2 != 0)
            break;
        if (gfSaveNoRecompress) {
            if (lParam1 && gCompOptionsSaved.cbState)
                lmemcpy((LPSTR)lParam1, (LPSTR)gCompOptionsSaved.lpState, (WORD)gCompOptionsSaved.cbState);
            return gCompOptionsSaved.cbState;
        }
        if (lParam1 && gCompOptions.cbState)
            lmemcpy((LPSTR)lParam1, (LPSTR)gCompOptions.lpState, (WORD)gCompOptions.cbState);
        return gCompOptions.cbState;


    case SRC_FRAME:
        if (lParam2 == 1 && gmedidSaveWave)
            return gmedidSaveWave;
        if (lParam2 != 0)
            break;
        if ((WORD)lParam1 >= gwSaveCount)
            return (DWORD)-1L;
        wPercent = (WORD)(lParam1 * 100 / (long)gwSaveCount);

        if (gfSaveNoRecompress) {
            hdib = GetSaveFrame((WORD)lParam1 + gwSaveStart, &fKey);
            dwFlags = fKey ? 0x01000000L : 0L;
        } else {
            wFlags = 0;
            dwFlags = 0;
            if (gwSaveCount - 1 > (WORD)lParam1)
                wFlags = 0x20;
            if (lParam1 == 0)
                wFlags |= 0x01;
            gfSaveDSEQ = (medGetPhysicalType(medid) == MEDTYPE_DSEQ);
            if (gfSaveDSEQ)
                wFlags |= 0x01;
            SeekTo((WORD)lParam1 + gwSaveStart);
            hdib = (HANDLE)GetFullFrame((WORD)lParam1 + gwSaveStart, &hpal, wFlags | 0x40);
            if (lParam1 == 0) {
                ghpalSaveLast = NULL;
                gfSavePalWarned = 0;
            }
            if (hdib) {
                hdib = CopyHandle(hdib);
                lpbi = (LPBITMAPINFOHEADER)GlobalLock(hdib);
                if (!gfSavePalWarned && hpal && hpal != ghpalSaveLast && lpbi->biBitCount == 8) {
                    fStatic = PaletteHasStaticColors(hpal);
                    if (ghpalSaveLast && !gfSaveDSEQ &&
                        (gfSaveLastStatic != fStatic ||
                         (!fStatic && (lpbi->biClrUsed > 236 || lpbi->biClrUsed == 0)))) {
                        gfSavePalWarned = 1;
                        LoadString(ghInst, IDS_PALETTETOOLARGE, achText, sizeof(achText));
                        LoadString(ghInst, IDS_CAPTION, achApp, sizeof(achApp));
                        MessageBeep(MB_ICONEXCLAMATION);
                        if (MessageBox(ghwndApp, achText, achApp, MB_OKCANCEL | MB_ICONEXCLAMATION) == IDCANCEL) {
                            GlobalFree(hdib);
                            medSetExtError(IDS_USERCANCELLED, ghInst);
                            break;
                        }
                    }
                    ghpalSaveLast = hpal;
                    gfSaveLastStatic = fStatic;
                }
            }
        }
        if (!hdib)
            medSetExtError(IDS_FRAMENOMEMORY, ghInst);
        return MAKELONG(hdib, wPercent) | dwFlags;

    case SRC_FRAMES:
        return (DWORD)gwSaveCount;
    }
    return 0L;
}
