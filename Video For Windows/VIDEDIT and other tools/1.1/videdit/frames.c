/*
 * frames.c - code segment 6 of VIDEDIT.EXE, s06:0000-4D47: the frame table
 *
 * The movie's video track is the huge array gpFrames of gnFrames FRAMEs
 * (see frames.h).  A frame refers to a frame of a MediaMan video element
 * or holds a DIB of its own; DIBs read from elements go through a cache
 * limited by the "Memory for caching images" preference.  This file has
 * the editing operations on the table (insert, remove, cut and paste,
 * with one level of undo), the movie's palettes, the clipboard format
 * of VidEdit and frame rate conversion.
 *
 * Functions are in the order of the binary.  Unless noted they are FAR
 * PASCAL; the NEAR ones are only called from this segment.
 */

#include "videdit.h"
#include <string.h>
#include "frames.h"

#ifndef min
#define min(a, b)   (((a) < (b)) ? (a) : (b))
#endif
#ifndef max
#define max(a, b)   (((a) > (b)) ? (a) : (b))
#endif

/* string table */
#define IDS_CAPTION             126     /* "VidEdit" */
#define IDS_NOTENOUGHMEMORY     299     /* "Not enough memory to complete operation." */
#define IDS_CANTCONVERTRATE     290     /* "Cannot convert frames to the new frame rate." */
#define IDS_RATEOUTOFMEMORY     291     /* "Out of memory.  Frame rate conversion incomplete." */
#define IDS_LOSTMOVIEELEMENT    280     /* "Cannot re-open a resource ... frames of your movie is now gone." */
#define IDS_LOSTCLIPELEMENT     281     /* "... frames on the clipboard is now gone." */
#define IDS_LOADINTOMEMORY      400     /* "The file %s cannot be used effectively unless ..." */
#define IDS_LOADINGVIDEOFILE    401     /* "Loading Video File" */
#define IDS_STRETCHNEW          405     /* "The new video is not the same size ... Stretch it ...?" */

/* the frame table, DS:02B0-02BB */
UINT        gnFrames;                   /* DS:02B0 */
BOOL        gfStretchNew;               /* DS:02B2 the user chose to stretch inserted video */
HPFRAME     gpFrames;                   /* DS:02B4 */
static UINT gcFramesAlloc = 50;         /* DS:02B8 entries allocated */
UINT        gwVideoBits;                /* DS:02BA */
BOOL        gfPalettePasted;            /* DS:02BC */
                                        /* DS:02BE gcfDibRange, see shared.c */

/* the DIB cache, DS:02C0-02CF: most recently used first, the pinned
 * entries (Load File into Memory) at the top */
typedef struct tagCACHEENTRY {
    MEDID       medid;                  /* 00 */
    WORD        wStream;                /* 04 */
    WORD        wFrame;                 /* 06 */
    HANDLE      hdib;                   /* 08 */
    WORD        aw[3];                  /* 0A not used */
} CACHEENTRY;                           /* 0x10 bytes */

static CACHEENTRY _huge *gpCache;       /* DS:02C0 */
static UINT     gnCachePinned;          /* DS:02C4 */
static UINT     gcCacheAlloc;           /* DS:02C6 */
static UINT     gnCache;                /* DS:02C8 */
static DWORD    gcbCache;               /* DS:02CA */
static HANDLE   ghdibUncached;          /* DS:02CE last DIB handed out without caching it */

/* undo of the last edit of the table, DS:02D0-02DB */
static HPFRAME  gpUndoFrames;           /* DS:02D0 the frames the edit removed */
static UINT     giUndoRemoved;          /* DS:02D4 where they were */
static UINT     gnUndoRemoved;          /* DS:02D6 */
static UINT     giUndoInserted;         /* DS:02D8 where the edit put frames in */
static UINT     gnUndoInserted;         /* DS:02DA */

char        gszVidfrmDibRange[] = "VidfrmDibRange"; /* DS:02DC */
                                        /* DS:02EB-0362 string literals of this file */

static MEDID FAR *glpClipMedids;        /* DS:1BFC ghDibRangeList locked: elements the clipboard refers to */
UINT        gwVideoBitsUndo;            /* DS:3086 */

/*
 * s06:0000-004F  FreeFrame  (NEAR PASCAL, the FRAME passed by value)
 *
 * Releases what a frame refers to.
 */
static void NEAR PASCAL FreeFrame(FRAME fr)
{
    if (fr.medid)
        medRelease(fr.medid, 0L);
    if (fr.medid == 0 && fr.wFrame)
        GlobalFree((HGLOBAL)fr.wFrame);
    if (fr.hpal)
        PalRelease(fr.hpal, 1L);
    if (fr.selRemap)
        RemapRelease(fr.selRemap, 1L);
}

/*
 * s06:0051-00D0  the 16 colours of the VGA system palette, R G B, in the
 * code segment: once with the dark colours at 0xBF (0051) and once at
 * 0x80 (0091).  MakeIdentityPalette compares palettes with them.
 */
static PALETTEENTRY _based(_segname("_CODE")) gapeVgaBF[16] = {
    { 0x00, 0x00, 0x00, 0 }, { 0xBF, 0x00, 0x00, 0 }, { 0x00, 0xBF, 0x00, 0 }, { 0xBF, 0xBF, 0x00, 0 },
    { 0x00, 0x00, 0xBF, 0 }, { 0xBF, 0x00, 0xBF, 0 }, { 0x00, 0xBF, 0xBF, 0 }, { 0xC0, 0xC0, 0xC0, 0 },
    { 0x80, 0x80, 0x80, 0 }, { 0xFF, 0x00, 0x00, 0 }, { 0x00, 0xFF, 0x00, 0 }, { 0xFF, 0xFF, 0x00, 0 },
    { 0x00, 0x00, 0xFF, 0 }, { 0xFF, 0x00, 0xFF, 0 }, { 0x00, 0xFF, 0xFF, 0 }, { 0xFF, 0xFF, 0xFF, 0 }
};
static PALETTEENTRY _based(_segname("_CODE")) gapeVga80[16] = {
    { 0x00, 0x00, 0x00, 0 }, { 0x80, 0x00, 0x00, 0 }, { 0x00, 0x80, 0x00, 0 }, { 0x80, 0x80, 0x00, 0 },
    { 0x00, 0x00, 0x80, 0 }, { 0x80, 0x00, 0x80, 0 }, { 0x00, 0x80, 0x80, 0 }, { 0xC0, 0xC0, 0xC0, 0 },
    { 0x80, 0x80, 0x80, 0 }, { 0xFF, 0x00, 0x00, 0 }, { 0x00, 0xFF, 0x00, 0 }, { 0xFF, 0xFF, 0x00, 0 },
    { 0x00, 0x00, 0xFF, 0 }, { 0xFF, 0x00, 0xFF, 0 }, { 0x00, 0xFF, 0xFF, 0 }, { 0xFF, 0xFF, 0xFF, 0 }
};

/*
 * s06:00D2-02F8  InsertFrameSlots  (NEAR PASCAL)
 *
 * Makes room for nFrames empty frames at iPos, growing the table in steps
 * of 50 entries.  A position past the end also adds empty frames up to
 * it.  At most 0x7FF0 frames.
 */
static BOOL NEAR PASCAL InsertFrameSlots(UINT iPos, UINT nFrames)
{
    UINT    iFirst;
    UINT    nNew;
    UINT    nGrow;
    UINT    i;
    WORD    sel;
    RECT    rc;
    HPFRAME pfr;

    if (nFrames + gnFrames > 0x7FF0)
        return FALSE;

    if (gpFrames == NULL) {
        gpFrames = (HPFRAME)GAllocPtrF(GMEM_MOVEABLE, (DWORD)gcFramesAlloc * sizeof(FRAME));
        if (gpFrames == NULL)
            return FALSE;
    }

    iFirst = min(iPos, gnFrames);
    nNew = max(iPos, gnFrames) + nFrames;

    if (nNew > gcFramesAlloc) {
        nGrow = nNew - gcFramesAlloc + 50;
        sel = GReAllocSelF(SELECTOROF(gpFrames), (DWORD)(gcFramesAlloc + nGrow) * sizeof(FRAME), 0);
        if (sel == 0)
            return FALSE;
        gpFrames = (HPFRAME)MAKELP(sel, 0);
        gcFramesAlloc += nGrow;
    }

    if (iPos < gnFrames)
        hmemmove(&gpFrames[iPos + nFrames], &gpFrames[iPos],
                 (DWORD)(gnFrames - iPos) * sizeof(FRAME));

    gnFrames = nNew;

    rc.left = 0;
    rc.top = 0;
    rc.right = gcxMovie;
    rc.bottom = gcyMovie;
    for (i = iFirst; i < nFrames + iPos; i++) {
        pfr = &gpFrames[i];
        pfr->medid = 0;
        pfr->wStream = 0;
        pfr->wFrame = 0;
        pfr->hpal = NULL;
        pfr->wFlags = 0;
        pfr->rcSrc = rc;
        pfr->rcDst = rc;
        pfr->selRemap = 0;
    }

    return TRUE;
}

/*
 * s06:02FA-05D3  PutFrames  (NEAR PASCAL)
 *
 * Puts nFrames frames from pfrSrc at iPos, inserting them in insert mode
 * and replacing the frames there in overwrite mode.  With fOwned the
 * frames' references go with them and the buffer is freed; otherwise
 * the frames are copies and take references of their own (a DIB in
 * memory is copied).
 *
 * Bug of the original: when taking the references fails, the cleanup
 * releases only one thing per frame (the element, else the DIB, else the
 * palette, else the remap table), and an element is released only if
 * its frame number is not 0.
 */
static BOOL NEAR PASCAL PutFrames(UINT iPos, UINT nFrames, HPFRAME pfrSrc, BOOL fOwned)
{
    UINT        iEnd;
    UINT        i;
    UINT        j;
    HPFRAME     pfr;
    MedReturn   mr;

    if (gfInsertMode) {
        if (!InsertFrameSlots(iPos, nFrames))
            return FALSE;
    } else {
        iEnd = iPos + nFrames;
        if (iEnd > gnFrames) {
            if (!InsertFrameSlots(gnFrames, iPos - gnFrames + nFrames))
                return FALSE;
            gnFrames = iEnd;
        }
        for (i = iPos; i < iEnd; i++)
            FreeFrame(gpFrames[i]);
    }

    hmemmove(&gpFrames[iPos], pfrSrc, (DWORD)nFrames * sizeof(FRAME));
    FramesChanged(iPos, 0, FALSE);

    if (fOwned) {
        GFreePtr(pfrSrc);
        return TRUE;
    }

    for (i = 0; i < nFrames; i++) {
        pfr = &pfrSrc[i];
        if (pfr->medid) {
            if (medAccess(pfr->medid, 0L, &mr, FALSE, NULL, 0L) != 1)
                goto Fail;
        } else if (pfr->wFrame) {
            pfr->wFrame = CopyHandle(pfr->wFrame);
            if (pfr->wFrame == 0)
                goto Fail;
        }
        if (pfr->hpal && !PalAddRef(pfr->hpal, 1L))
            goto Fail;
        if (pfr->selRemap && !RemapAddRef(pfr->selRemap, 1L)) {
            PalRelease(pfrSrc[i].hpal, 1L);
            goto Fail;
        }
    }

    return TRUE;

Fail:
    for (j = 0; j < i; j++) {
        pfr = &pfrSrc[j];
        if (pfr->medid && pfr->wFrame)
            medRelease(pfr->medid, 0L);
        else if (pfr->medid == 0 && pfr->wFrame)
            GlobalFree((HGLOBAL)pfr->wFrame);
        else if (pfr->hpal)
            PalRelease(pfr->hpal, 1L);
        else if (pfr->selRemap)
            RemapRelease(pfr->selRemap, 1L);
    }
    GFreePtr(pfrSrc);
    return FALSE;
}

/*
 * s06:05D6-0675  RemoveFrameSlots  (NEAR PASCAL)
 *
 * Removes nFrames entries at iPos from the table without releasing them.
 */
static BOOL NEAR PASCAL RemoveFrameSlots(UINT iPos, UINT nFrames)
{
    if (nFrames) {
        if (nFrames + iPos < gnFrames)
            hmemmove(&gpFrames[iPos], &gpFrames[nFrames + iPos],
                     (DWORD)(gnFrames - nFrames - iPos) * sizeof(FRAME));
        gnFrames -= nFrames;
    }
    return TRUE;
}

/*
 * s06:0678-078B  TakeFrames  (NEAR PASCAL)
 *
 * Moves nFrames frames at iPos into a new buffer, with their references.
 * In insert mode they are removed from the table, in overwrite mode
 * their places become empty frames (whose rectangles stay as they were).
 */
static HPFRAME NEAR PASCAL TakeFrames(UINT iPos, UINT nFrames)
{
    DWORD   cb;
    HPFRAME pfrNew;
    HPFRAME pfr;
    UINT    i;

    cb = (DWORD)nFrames * sizeof(FRAME);
    pfrNew = (HPFRAME)GAllocPtrF(GMEM_MOVEABLE, cb);
    if (pfrNew == NULL)
        return NULL;

    hmemmove(pfrNew, &gpFrames[iPos], cb);

    if (gfInsertMode)
        RemoveFrameSlots(iPos, nFrames);
    else {
        for (i = iPos; i < nFrames + iPos; i++) {
            pfr = &gpFrames[i];
            pfr->medid = 0;
            pfr->wStream = 0;
            pfr->wFrame = 0;
            pfr->wFlags = 0;
            pfr->hpal = NULL;
            pfr->selRemap = 0;
        }
    }

    FramesChanged(iPos, 0, FALSE);
    return pfrNew;
}

/*
 * s06:078E-0881  ReadElementDIB  (NEAR PASCAL)
 *
 * Gets frame wFrame of a video element as a DIB.  0xFFFF means the
 * element is a single picture, which is asked for as clipboard data.
 * If the element can't deliver the frame, the MediaMan error goes to the
 * status bar.
 */
static HANDLE NEAR PASCAL ReadElementDIB(MEDID medid, WORD wStream, WORD wFrame, BOOL NEAR *pfKey)
{
    char            ach[128];
    VIDEOELEMINFO   vei;
    DWORD           dw;
    HANDLE          hdib;
    HCURSOR         hcurOld = NULL;

    if (wFrame == 0xFFFF) {
        dw = medSendMessage(medid, MED_GETCLIPBOARDDATA, 0L, 0L);
        if (HIWORD(dw) != CF_DIB)
            return NULL;
        hdib = LOWORD(dw);
        if (pfKey)
            *pfKey = TRUE;
    } else {
        medSendMessage(medid, VEM_GETINFO, (LONG)(LPVOID)&vei, (LONG)sizeof(vei));
        if (!(vei.dwFlags & VEIF_FAST))
            hcurOld = SetCursor(LoadCursor(NULL, IDC_WAIT));

        dw = medSendMessage(medid, VEM_GETFRAME, (LONG)wFrame, (LONG)wStream);
        hdib = LOWORD(dw);
        if (hdib == 0) {
            ach[0] = '\0';
            medGetErrorText(medGetError(), ach, sizeof(ach));
            StatusSetMessage(ach);
        }
        if (hdib == 0xFFFF)
            hdib = NULL;
        if (pfKey)
            *pfKey = (HIWORD(dw) & 0x0100) ? TRUE : FALSE;
    }

    if (hcurOld)
        SetCursor(hcurOld);
    return hdib;
}

/*
 * s06:0884-0DA1  CacheGetDIB  (NEAR PASCAL)
 *
 * Gets a frame of an element through the DIB cache.  A DIB found in the
 * cache moves to the front (behind the pinned ones) unless GFD_NOCACHE;
 * a palette-based one is read again with GFD_RELOAD.  A new DIB goes in
 * at the front after old ones were dropped from the end to keep the
 * cache under the limit; one that doesn't fit is handed out and freed on
 * the next call.  GFD_PIN pins the entry.
 *
 * The DIB of a cache hit is locked and never unlocked.
 */
static HANDLE NEAR PASCAL CacheGetDIB(MEDID medid, WORD wStream, WORD wFrame, WORD wCache,
                                      BOOL NEAR *pfKey)
{
    CACHEENTRY          ce;
    CACHEENTRY _huge   *pce;
    LPBITMAPINFOHEADER  lpbi;
    DWORD               cb;
    HANDLE              hdib;
    HANDLE              h;
    WORD                sel;
    UINT                i;
    UINT                n;
    BOOL                fCached = FALSE;

    if (ghdibUncached) {
        GlobalFree(ghdibUncached);
        ghdibUncached = NULL;
    }
    if (pfKey)
        *pfKey = FALSE;

    for (i = 0; i < gnCache; i++) {
        if (gpCache[i].medid == medid && gpCache[i].wStream == wStream && gpCache[i].wFrame == wFrame)
            break;
    }

    if (i < gnCache) {
        lpbi = (LPBITMAPINFOHEADER)GlobalLock(gpCache[i].hdib);
        if (lpbi->biBitCount <= 8 && (wCache & GFD_RELOAD)) {
            h = ReadElementDIB(medid, wStream, wFrame, pfKey);
            if (h) {
                GlobalFree(gpCache[i].hdib);
                gpCache[i].hdib = h;
            }
        }

        ce = gpCache[i];
        if (!(wCache & GFD_NOCACHE) && i >= gnCachePinned) {
            if (i > gnCachePinned) {
                hmemmove(&gpCache[gnCachePinned + 1], &gpCache[gnCachePinned],
                         (DWORD)(i - gnCachePinned) * sizeof(CACHEENTRY));
                gpCache[gnCachePinned] = ce;
            }
            if (wCache & GFD_PIN)
                gnCachePinned++;
        }
        return ce.hdib;
    }

    hdib = ReadElementDIB(medid, wStream, wFrame, pfKey);
    if (hdib == NULL)
        return NULL;

    cb = GlobalSize(hdib) + 0x400;

    if (!(wCache & GFD_NOCACHE)) {
        while (gnCache > gnCachePinned && gcbCache + cb > ((DWORD)gwCacheKB << 10)) {
            gcbCache -= GlobalSize(gpCache[gnCache - 1].hdib) + 0x400;
            GlobalFree(gpCache[gnCache - 1].hdib);
            gnCache--;
        }
    }

    if ((wCache & GFD_PIN) || gnCache == gnCachePinned || gcbCache + cb < ((DWORD)gwCacheKB << 10)) {
        if (gnCache == gcCacheAlloc) {
            n = gcCacheAlloc + 20;
            if (gcCacheAlloc == 0) {
                gpCache = (CACHEENTRY _huge *)GAllocPtrF(GMEM_MOVEABLE, (DWORD)n * sizeof(CACHEENTRY));
                if (gpCache)
                    gcCacheAlloc = n;
            } else {
                sel = GReAllocSelF(SELECTOROF(gpCache), (DWORD)n * sizeof(CACHEENTRY), 0);
                if (sel) {
                    gpCache = (CACHEENTRY _huge *)MAKELP(sel, 0);
                    gcCacheAlloc = n;
                }
            }
        }

        if (gnCache < gcCacheAlloc) {
            if (gnCache > gnCachePinned)
                hmemmove(&gpCache[gnCachePinned + 1], &gpCache[gnCachePinned],
                         (DWORD)(gnCache - gnCachePinned) * sizeof(CACHEENTRY));
            pce = &gpCache[gnCachePinned];
            pce->medid = medid;
            pce->wStream = wStream;
            pce->wFrame = wFrame;
            pce->hdib = hdib;
            gcbCache += cb;
            gnCache++;
            if (wCache & GFD_PIN)
                gnCachePinned++;
            fCached = TRUE;
        }
    }

    if (!fCached)
        ghdibUncached = hdib;
    return hdib;
}

/*
 * s06:0DA4-0DF7  CacheFlush  (NEAR)
 */
static void NEAR CacheFlush(void)
{
    while (gnCache) {
        gnCache--;
        GlobalFree(gpCache[gnCache].hdib);
    }
    gnCachePinned = 0;
    gcbCache = 0;
}

/*
 * s06:0DF8-0FC9  MakeIdentityPalette  (NEAR _cdecl)
 *
 * On a palette device, a 256-colour palette whose first and last eight
 * entries are the system colours (either flavour of the VGA colours, or
 * what the system palette has) gets the 20 system colours put in exactly
 * and PC_NOCOLLAPSE on the 236 others, so it maps 1:1 to the hardware.
 */
static BOOL NEAR MakeIdentityPalette(HPALETTE hpal)
{
    PALETTEENTRY    ape[256];
    PALETTEENTRY    apeSys[16];
    BYTE           *p;
    HDC             hdc;
    WORD            nColors;
    BOOL            fPalDevice;
    BOOL            fIdentity = FALSE;
    int             i;
    int             n;

    hdc = GetDC(NULL);
    fPalDevice = (GetDeviceCaps(hdc, RASTERCAPS) & RC_PALETTE) != 0;
    ReleaseDC(NULL, hdc);

    if (hpal == NULL || !fPalDevice)
        return FALSE;

    GetObject(hpal, sizeof(nColors), &nColors);
    if (nColors != 256)
        return FALSE;

    GetPaletteEntries(hpal, 0, 8, &ape[0]);
    GetPaletteEntries(hpal, 248, 8, &ape[8]);

    for (n = 0, i = 0; i < 16; i++, n++) {
        if (gapeVga80[i].peRed != ape[i].peRed || gapeVga80[i].peGreen != ape[i].peGreen ||
            gapeVga80[i].peBlue != ape[i].peBlue)
            break;
    }
    if (n != 16) {
        for (n = 0, i = 0; i < 16; i++, n++) {
            if (gapeVgaBF[i].peRed != ape[i].peRed || gapeVgaBF[i].peGreen != ape[i].peGreen ||
                gapeVgaBF[i].peBlue != ape[i].peBlue)
                break;
        }
        if (n != 16) {
            hdc = GetDC(NULL);
            GetSystemPaletteEntries(hdc, 0, 8, &apeSys[0]);
            GetSystemPaletteEntries(hdc, 248, 8, &apeSys[8]);
            ReleaseDC(NULL, hdc);

            for (n = 0, i = 0; i < 16; i++, n++) {
                if (apeSys[i].peRed != ape[i].peRed || apeSys[i].peGreen != ape[i].peGreen ||
                    apeSys[i].peBlue != ape[i].peBlue)
                    break;
            }
            if (n != 16)
                return fIdentity;
        }
    }

    fIdentity = TRUE;

    GetPaletteEntries(hpal, 0, 256, ape);
    hdc = GetDC(NULL);
    GetSystemPaletteEntries(hdc, 0, 10, ape);
    GetSystemPaletteEntries(hdc, 246, 10, &ape[246]);
    ReleaseDC(NULL, hdc);

    for (p = &ape[10].peFlags; p < &ape[246].peFlags; p += sizeof(PALETTEENTRY))
        *p = PC_NOCOLLAPSE;

    SetPaletteEntries(hpal, 0, 256, ape);
    return fIdentity;
}

/*
 * s06:0FCA-1405  GetFrameDIB
 *
 * The DIB of frame iFrame, as DIB_PAL_COLORS indices into the frame's
 * palette (*phpal, may be NULL).  With GFD_PALETTE only the palette is
 * wanted: if it is already known, it is returned without the DIB.
 *
 * The first time a frame's DIB is seen, its format is checked: true
 * colour, an RLE delta frame or a complete one; other compressions are
 * refused.  For a palette-based DIB without a palette (or with a dirty
 * one) the palette is taken from the DIB, made an identity palette if
 * possible and entered in the palette table; a frame that already had a
 * palette keeps it and gets a remap table to it instead.  The colour
 * table is then rewritten as indices, through the remap table if there
 * is one.
 *
 * The DIB stays locked (twice where the palette is built: the original
 * locks it once more and throws the pointer away).
 */
HANDLE FAR PASCAL GetFrameDIB(UINT iFrame, HPALETTE NEAR *phpal, WORD wFlags)
{
    HPFRAME             pfr;
    LPBITMAPINFOHEADER  lpbi;
    LPBITMAPINFOHEADER  lpbi2;
    WORD FAR           *pw;
    BYTE FAR           *pbRemap;
    HANDLE              hdib;
    HPALETTE            hpal = NULL;
    BOOL                fKey;
    BOOL                fPartial;
    UINT                i;

    if (iFrame >= gnFrames)
        goto Fail;

    if (wFlags & GFD_PALETTE) {
        pfr = &gpFrames[iFrame];
        if ((pfr->wFlags & FF_TRUECOLOR) || pfr->hpal) {
            if (phpal)
                *phpal = pfr->hpal;
            return NULL;
        }
    }

    pfr = &gpFrames[iFrame];
    if (pfr->medid == 0 && pfr->wFrame == 0)
        goto Fail;

    if (pfr->medid == 0) {
        hdib = pfr->wFrame;
        fKey = FALSE;
    } else {
        if (pfr->hpal == NULL || (pfr->wFlags & FF_PALDIRTY))
            wFlags |= GFD_RELOAD;
        hdib = CacheGetDIB(gpFrames[iFrame].medid, gpFrames[iFrame].wStream,
                           gpFrames[iFrame].wFrame, wFlags, &fKey);
    }

    if (hdib == NULL)
        goto Done;

    if (fKey)
        gpFrames[iFrame].wFlags |= FF_KEYFRAME | FF_COMPLETE;

    lpbi = (LPBITMAPINFOHEADER)GlobalLock(hdib);

    if (gwVideoBits == 0) {
        gwVideoBits = lpbi->biBitCount;
        if (gwVideoBits < 8)
            gwVideoBits = 8;
    }
    if (lpbi->biBitCount > 8)
        gnMovieColors = max(gnMovieColors, 64);

    pfr = &gpFrames[iFrame];
    if (!(pfr->wFlags & FF_CHECKED)) {
        if (lpbi->biBitCount > 8)
            pfr->wFlags |= FF_TRUECOLOR;
        if (lpbi->biCompression == BI_RLE8) {
            if (RleCheckFrame(hdib, (BOOL FAR *)&fPartial)) {
                if (!fPartial)
                    gpFrames[iFrame].wFlags |= FF_COMPLETE;
                gpFrames[iFrame].wFlags |= FF_CHECKED;
            } else
                hdib = NULL;
        } else if (lpbi->biCompression == BI_RGB)
            pfr->wFlags |= FF_CHECKED | FF_COMPLETE;
        else
            hdib = NULL;
    }

    if (hdib && lpbi->biBitCount <= 8) {
        pfr = &gpFrames[iFrame];
        if (pfr->hpal == NULL || (pfr->wFlags & FF_PALDIRTY)) {
            GlobalLock(hdib);
            hpal = CreateDibPalette((HANDLE)SELECTOROF(GlobalLock(hdib)));
            if (hpal) {
                MakeIdentityPalette(hpal);
                hpal = PalAddRef(hpal, 1L);
            }
            if (hpal == NULL)
                return NULL;

            pfr = &gpFrames[iFrame];
            if (pfr->hpal == NULL) {
                pfr->hpal = hpal;
                pfr->selRemap = 0;
            } else {
                pfr->selRemap = RemapCreate(hpal, pfr->hpal, 0, 1L);
                gpFrames[iFrame].wFlags &= ~FF_PALDIRTY;
                PalRelease(hpal, 1L);
            }
        }

        if (gpFrames[iFrame].selRemap == 0) {
            /* the palette is not used with DIB_PAL_COLORS; the original
             * passes whatever register is left over */
            SetDibUsage((HANDLE)SELECTOROF(GlobalLock(hdib)), hpal, DIB_PAL_COLORS);
        } else {
            lpbi2 = (LPBITMAPINFOHEADER)GlobalLock(hdib);
            pw = (WORD FAR *)((LPBYTE)lpbi2 + (WORD)lpbi2->biSize);
            pbRemap = (BYTE FAR *)MAKELP(gpFrames[iFrame].selRemap, 0);
            for (i = 0; i < (UINT)lpbi2->biClrUsed; i++)
                pw[i] = pbRemap[i];
            GlobalUnlock(hdib);
        }
    }

Done:
    if (phpal)
        *phpal = gpFrames[iFrame].hpal;
    return hdib;

Fail:
    if (phpal)
        *phpal = NULL;
    return NULL;
}

/*
 * s06:1408-172E  InsertFrameElements  (NEAR PASCAL)
 *
 * Puts nFrames frames of element medid at iPos (inserting or overwriting
 * as the mode says): frame i refers to frame wFirst + i of the element.
 * With medid 0, wFirst is a DIB in memory (one frame).  Each frame takes
 * a reference to the element.  If the movie is empty the new video sets
 * its size; video of another size is either stretched to the movie's
 * size (the user is asked) or clipped to it.  The first frame is read to
 * learn its palette and depth.
 */
static BOOL NEAR PASCAL InsertFrameElements(UINT iPos, UINT nFrames, MEDID medid, WORD wStream,
                                            WORD wFirst, int cx, int cy, WORD wFlags)
{
    char                ach[200];
    char                achTitle[40];
    RECT                rcSrc;
    RECT                rcDst;
    MedReturn           mr;
    HPALETTE            hpal = NULL;
    WORD                nColors;
    HANDLE              hdib;
    LPBITMAPINFOHEADER  lpbi;
    HPFRAME             pfr;
    UINT                i;

    rcSrc.left = 0;
    rcSrc.top = 0;
    rcSrc.right = cx;
    rcSrc.bottom = cy;
    rcDst.left = 0;
    rcDst.top = 0;
    rcDst.right = cx;
    rcDst.bottom = cy;
    gfStretchNew = FALSE;

    if (gnFrames == 0)
        SetFrameSize(cx, cy);
    else if (gcxMovie != (WORD)cx || gcyMovie != (WORD)cy) {
        LoadString((HINSTANCE)ghInst, IDS_STRETCHNEW, ach, sizeof(ach));
        LoadString((HINSTANCE)ghInst, IDS_CAPTION, achTitle, sizeof(achTitle));
        if (MessageBox(NULL, ach, achTitle,
                       MB_TASKMODAL | MB_DEFBUTTON2 | MB_ICONQUESTION | MB_YESNO) == IDYES) {
            gfStretchNew = TRUE;
            rcDst.right = gcxMovie;
            rcDst.bottom = gcyMovie;
        } else {
            if (rcDst.right > (int)gcxMovie)
                rcSrc.right = rcDst.right = gcxMovie;
            if (rcDst.bottom > (int)gcyMovie)
                rcSrc.bottom = rcDst.bottom = gcyMovie;
        }
    }

    if (gfInsertMode) {
        if (!InsertFrameSlots(iPos, nFrames))
            return FALSE;
    } else if (iPos + nFrames > gnFrames) {
        if (!InsertFrameSlots(gnFrames, iPos - gnFrames + nFrames))
            return FALSE;
    }

    for (i = 0; i < nFrames; i++) {
        if (!gfInsertMode)
            FreeFrame(gpFrames[i + iPos]);
        if (medid)
            medAccess(medid, 0L, &mr, FALSE, NULL, 0L);

        pfr = &gpFrames[i + iPos];
        pfr->medid = medid;
        pfr->wStream = wStream;
        pfr->wFrame = i + wFirst;
        pfr->rcSrc = rcSrc;
        pfr->rcDst = rcDst;
        pfr->hpal = NULL;

        if (medid == 0 && i + wFirst != 0)
            gpFrames[i + iPos].wFlags = FF_COMPLETE | FF_KEYFRAME;
        else
            gpFrames[i + iPos].wFlags = 0;

        if (i == 0) {
            hdib = GetFrameDIB(iPos, &hpal, 0);
            if (hpal) {
                GetObject(hpal, sizeof(nColors), &nColors);
                if (nColors > gnMovieColors)
                    gnMovieColors = nColors;
            }
            if (hdib) {
                lpbi = (LPBITMAPINFOHEADER)GlobalLock(hdib);
                if (lpbi->biBitCount > 8)
                    wFlags |= FF_TRUECOLOR;
            }
        }

        gpFrames[i + iPos].wFlags |= wFlags;
    }

    if (medid && medGetPhysicalType(medid) == medFOURCC('D', 'S', 'E', 'Q'))
        gnMovieColors = 256;

    FramesChanged(iPos, 0, FALSE);
    return TRUE;
}

/*
 * s06:1732-184A  BlankFrames
 *
 * Inserts nFrames empty frames at iPos, or in overwrite mode empties the
 * frames there (at most up to the end of the movie).  Nothing happens at
 * or past the end of the movie.
 */
BOOL FAR PASCAL BlankFrames(UINT iPos, UINT nFrames)
{
    RECT    rc;
    HPFRAME pfr;
    UINT    i;

    if (iPos >= gnFrames || nFrames < 1)
        return TRUE;

    if (gfInsertMode) {
        if (!InsertFrameSlots(iPos, nFrames))
            return FALSE;
        return TRUE;
    }

    if (nFrames + iPos > gnFrames)
        nFrames = gnFrames - iPos;

    for (i = 0; i < nFrames; i++) {
        FreeFrame(gpFrames[i + iPos]);
        rc.left = 0;
        rc.top = 0;
        rc.right = gcxMovie;
        rc.bottom = gcyMovie;
        pfr = &gpFrames[i + iPos];
        pfr->medid = 0;
        pfr->wStream = 0;
        pfr->wFrame = 0;
        pfr->hpal = NULL;
        pfr->wFlags = 0;
        pfr->rcSrc = rc;
        pfr->rcDst = rc;
        pfr->selRemap = 0;
    }

    return TRUE;
}

/*
 * s06:184E-187F  InsertDIBFrame  (NEAR PASCAL)
 *
 * Puts a DIB in memory at iPos as a complete frame.  The DIB stays locked.
 */
static BOOL NEAR PASCAL InsertDIBFrame(UINT iPos, HANDLE hdib)
{
    LPBITMAPINFOHEADER lpbi;

    lpbi = (LPBITMAPINFOHEADER)GlobalLock(hdib);
    return InsertFrameElements(iPos, 1, 0L, 0, hdib, (int)lpbi->biWidth, (int)lpbi->biHeight,
                               FF_COMPLETE);
}

/*
 * s06:1882-1A57  RemoveFrames
 *
 * Takes frames out of the movie for an edit and keeps them for undo:
 * nSel frames from iStart when pasting in insert mode (the selection is
 * replaced), nFrames otherwise.  If the frame after them is not complete,
 * it would lose the frames it builds on, so a full frame of it is built
 * first and put in its place (one more frame is taken out).  The undo
 * state records what was removed and what was put in.
 */
BOOL FAR PASCAL RemoveFrames(UINT iStart, UINT nSel, UINT nFrames, BOOL fPaste)
{
    RECT    rc;
    HANDLE  hdib;
    HPFRAME pfr;
    UINT    iEnd;
    UINT    nCut;
    UINT    iFull;
    UINT    n;
    BOOL    fFull = FALSE;

    gwVideoBitsUndo = gwVideoBits;

    iEnd = ((gfInsertMode && fPaste) ? nSel : nFrames) + iStart;
    if (gnFrames > iEnd)
        fFull = (GetFrameInfo(iEnd, FI_COMPLETE) & FI_COMPLETE) == 0;

    nCut = iEnd - iStart;
    if (fFull)
        nCut++;
    iFull = iEnd;

    if (fFull) {
        hdib = GetFullFrame(iEnd, NULL, GFF_REBUILD);
        if (hdib)
            hdib = CopyHandle(hdib);
        if (hdib == NULL)
            return FALSE;
        SetRect(&rc, 0, 0, gcxMovie, gcyMovie);
    }

    if (gnFrames <= iStart)
        n = 0;
    else {
        n = nCut;
        if (n + iStart > gnFrames)
            n = gnFrames - iStart;
    }

    if (n) {
        gpUndoFrames = TakeFrames(iStart, n);
        if (gpUndoFrames == NULL) {
            if (fFull) {
                GlobalUnlock(hdib);
                GlobalFree(hdib);
            }
            return FALSE;
        }
    }

    gnUndoRemoved = n;
    giUndoRemoved = iStart;

    if (fFull) {
        if (gfInsertMode) {
            if (!InsertFrameSlots(iStart, 1)) {
                GlobalUnlock(hdib);
                GlobalFree(hdib);
                SetUndoTracks(TRUE, FALSE, gfInsertMode);
                Undo();
                SetUndo(0);
                return FALSE;
            }
            iFull = iStart;
        } else
            iFull = iStart + n - 1;

        pfr = &gpFrames[iFull];
        pfr->medid = 0;
        pfr->wStream = 0;
        pfr->wFrame = hdib;
        pfr->rcSrc = rc;
        pfr->rcDst = rc;
        pfr->hpal = NULL;
        pfr->wFlags = FF_COMPLETE | FF_KEYFRAME;
        FramesChanged(iStart, 0, FALSE);
    }

    gnUndoInserted = fFull ? 1 : 0;
    giUndoInserted = iFull;
    return TRUE;
}

/*
 * s06:1A5A-1ACE  ReleaseClipboardMedids
 *
 * Releases the elements the movie on the clipboard referred to (when the
 * clipboard is emptied) and shrinks the list to 0x20 bytes.
 */
void FAR ReleaseClipboardMedids(void)
{
    UINT    i;

    glpClipMedids = (MEDID FAR *)GlobalLock((HGLOBAL)ghDibRangeList);
    if (glpClipMedids == NULL)
        return;

    for (i = 0; i < gcDibRangeList; i++) {
        if (glpClipMedids[i])
            medRelease(glpClipMedids[i], 0L);
    }
    gcDibRangeList = 0;
    GlobalUnlock((HGLOBAL)ghDibRangeList);
    ghDibRangeList = GlobalReAlloc((HGLOBAL)ghDibRangeList, 0x20, GMEM_SHARE | GMEM_MOVEABLE);
}

/*
 * s06:1AD0-1B70  InsertStillFrame
 *
 * Puts a still picture element in as one frame (replacing the selection
 * in insert mode).
 */
BOOL FAR PASCAL InsertStillFrame(UINT iPos, UINT nSel, WORD FAR *lpwFrames, MEDID medid)
{
    LPBITMAPINFOHEADER  lpbi;
    BOOL                f;

    RemoveFrames(iPos, nSel, 1, TRUE);
    if (lpwFrames)
        *lpwFrames = 1;

    lpbi = (LPBITMAPINFOHEADER)medLock(medid, 1, 0L);
    f = InsertFrameElements(iPos, 1, medid, 0, 0xFFFF, (int)lpbi->biWidth, (int)lpbi->biHeight,
                            FF_COMPLETE);
    medUnlock(medid, 0, 0L, 0L);

    if (!f) {
        SetUndoTracks(TRUE, f, gfInsertMode);
        Undo();
        SetUndo(f);
        return FALSE;
    }

    giUndoInserted = iPos;
    gnUndoInserted++;
    return f;
}

/*
 * s06:1B74-1BDD  HasPaletteChanges  (unreferenced)
 *
 * TRUE if the frames use more than one palette.
 */
BOOL FAR HasPaletteChanges(void)
{
    HPALETTE    hpal;
    HPALETTE    hpalFirst = NULL;
    UINT        i;

    for (i = 0; i < gnFrames; i++) {
        hpal = gpFrames[i].hpal;
        if (hpal) {
            if (hpalFirst && hpal != hpalFirst)
                return TRUE;
            hpalFirst = hpal;
        }
    }
    return FALSE;
}

/*
 * s06:1BDE-1C75  GetMovieFormat
 *
 * The movie's format as a BITMAPINFOHEADER.
 */
BOOL FAR PASCAL GetMovieFormat(LPBITMAPINFOHEADER lpbi)
{
    if (gnFrames == 0)
        return FALSE;

    lpbi->biSize = sizeof(BITMAPINFOHEADER);
    lpbi->biWidth = gcxMovie;
    lpbi->biHeight = gcyMovie;
    lpbi->biPlanes = 1;
    lpbi->biBitCount = gwVideoBits;
    lpbi->biCompression = BI_RGB;
    lpbi->biSizeImage = 0;
    lpbi->biXPelsPerMeter = 0;
    lpbi->biYPelsPerMeter = 0;
    lpbi->biClrUsed = gnMovieColors;
    lpbi->biClrImportant = 0;

    if (gwVideoBits > 8)
        lpbi->biClrUsed = 0;
    if (gnMovieColors == 0 && gwVideoBits <= 8)
        lpbi->biClrUsed = (int)(1 << gwVideoBits);
    return TRUE;
}

/*
 * s06:1C78-1D02  FreeAllFrames
 */
void FAR FreeAllFrames(void)
{
    UINT    i;

    for (i = 0; i < gnFrames; i++)
        FreeFrame(gpFrames[i]);

    PalTableFree();
    RemapTableFree();
    gnMovieColors = 1;
    gnFrames = 0;
    gwVideoBits = 0;
    CacheFlush();
    FramesChanged(0, 0, FALSE);
}

/*
 * s06:1D04-1D46  DeleteFrames
 */
BOOL FAR PASCAL DeleteFrames(UINT iStart, UINT nFrames)
{
    if (nFrames + iStart > gnFrames)
        nFrames = (iStart > gnFrames) ? 0 : gnFrames - iStart;

    if (nFrames) {
        if (!RemoveFrames(iStart, 0, nFrames, FALSE))
            return FALSE;
        FramesChanged(iStart, 0, FALSE);
    }
    return TRUE;
}

/*
 * s06:1D4A-1D7E  RedrawFrame
 *
 * Repaints the current frame right away (unless the preview draws),
 * if iFrom or iTo is within the movie.
 */
void FAR PASCAL RedrawFrame(UINT iFrom, UINT iTo)
{
    HDC     hdc;

    if (gfPreviewDrawing == 0 && (iFrom <= gnFrames || iTo <= gnFrames)) {
        hdc = FrameGetDC();
        PaintFrame(hdc, FALSE);
        ReleaseDC((HWND)ghwndFrame, hdc);
    }
}

/*
 * s06:1D82-1D90  GetFrameCount
 */
UINT FAR GetFrameCount(void)
{
    if (gnFrames == 0)
        gwVideoBits = 0;
    return gnFrames;
}

/*
 * s06:1D92-20E8  InsertVideoElement
 *
 * Puts the frames of a video element in at iPos, replacing the selection
 * (nSel frames) unless fNoRemove, and filling up to nMin frames with
 * empty ones.  The element's frame rate is converted to the movie's.  If
 * the element is slow to read, the user is offered to load all its
 * frames into the cache now.  *lpwFrames gets the number of frames.
 */
BOOL FAR PASCAL InsertVideoElement(UINT iPos, UINT nSel, UINT nMin, WORD FAR *lpwFrames,
                                   MEDID medid, WORD wStream, BOOL fNoRemove)
{
    VIDEOELEMINFO   vei;
    MSG             msg;
    DWORD           dwUSec;
    HCURSOR         hcurOld = NULL;
    BOOL            fOK = TRUE;
    UINT            nFrames;
    UINT            nMovie;
    UINT            n;
    UINT            i;
    int             idLoad;

    medSendMessage(medid, VEM_GETINFO, (LONG)(LPVOID)&vei, (LONG)sizeof(vei));
    dwUSec = vei.dwMicroSecPerFrame;
    if (gnFrames == 0 && dwUSec)
        gCompOptions.dwUSecPerFrame = dwUSec;
    if (dwUSec == 0)
        dwUSec = gCompOptions.dwUSecPerFrame;

    if (!(vei.dwFlags & VEIF_LENGTHKNOWN)) {
        hcurOld = SetCursor(LoadCursor(NULL, IDC_WAIT));
        StatusSetMessage("Finding length of AVI file");
    }
    nFrames = (UINT)medSendMessage(medid, VEM_GETLENGTH, 0L, 0L);
    StatusSetMessage(NULL);
    if (hcurOld)
        SetCursor(hcurOld);

    if (nFrames < 1)
        return FALSE;

    nMovie = (UINT)muldiv32((LONG)nFrames, (LONG)dwUSec, (LONG)gCompOptions.dwUSecPerFrame);
    idLoad = nMovie;
    if (nMin > nFrames)
        idLoad += nMin - nFrames;

    if (!fNoRemove)
        RemoveFrames(iPos, nSel, idLoad, TRUE);

    if (lpwFrames)
        *lpwFrames = nFrames;

    if (!(vei.dwFlags & VEIF_INMEMORY)) {
        char    achFormat[200];
        char    achName[144];
        char    achText[200];
        char    achTitle[40];

        LoadString((HINSTANCE)ghInst, IDS_LOADINTOMEMORY, achFormat, sizeof(achFormat));
        medGetFileName(medid, achName, sizeof(achName));
        wsprintf(achText, achFormat, (LPSTR)achName);
        LoadString((HINSTANCE)ghInst, IDS_LOADINGVIDEOFILE, achTitle, sizeof(achTitle));
        idLoad = MessageBox(NULL, achText, achTitle, MB_TASKMODAL | MB_ICONQUESTION | MB_YESNO);
    }

    if (!gfInsertMode && nMovie < nFrames)
        fOK = InsertFrameSlots(iPos + nMovie, nFrames - nMovie);

    if (fOK) {
        fOK = InsertFrameElements(iPos, nFrames, medid, wStream, 0, (int)vei.dwWidth,
                                  (int)vei.dwHeight,
                                  (vei.dwFlags & VEIF_ALLKEY) ? FF_COMPLETE : 0);
        if (!fOK && !gfInsertMode && nMovie < nFrames)
            RemoveFrameSlots(iPos + nMovie, nFrames - nMovie);
    }

    if (!fOK)
        goto Fail;

    giUndoInserted = iPos;
    if (gfInsertMode)
        gnUndoInserted += nFrames;
    else
        gnUndoInserted += min(gnFrames - giUndoInserted, max(nMin, nFrames));

    if (dwUSec) {
        n = ConvertFrameRate(dwUSec, gCompOptions.dwUSecPerFrame, iPos, iPos + nFrames);
        if (n == 0)
            return FALSE;
        gnUndoInserted += n - nFrames;
        nFrames = n;
        if (lpwFrames)
            *lpwFrames = n;
    }

    if (nMin > nFrames) {
        fOK = BlankFrames(iPos + nFrames, nMin - nFrames);
        if (!fOK)
            goto Fail;
        i = iPos;
        if (gfInsertMode && nFrames + i != gnFrames)
            gnUndoInserted += nMin - nFrames;
    } else
        i = iPos;

    if (!(vei.dwFlags & VEIF_INMEMORY) && idLoad == IDYES) {
        char    achStatus[50];

        hcurOld = SetCursor(LoadCursor(NULL, IDC_WAIT));
        GetAsyncKeyState(VK_ESCAPE);
        for (; i < iPos + nFrames; i++) {
            if (GetAsyncKeyState(VK_ESCAPE) & 1)
                break;
            wsprintf(achStatus, "Loading frame #%u/%u, press Escape to Cancel", i - iPos, nFrames);
            StatusSetMessage(achStatus);
            if (!GetFrameDIB(i, NULL, GFD_PIN))
                break;
            if (PeekMessage(&msg, NULL, 0, 0, PM_NOREMOVE))
                Yield();
        }
        StatusSetMessage(NULL);
        if (hcurOld)
            SetCursor(hcurOld);
    }

    return fOK;

Fail:
    SetUndoTracks(TRUE, FALSE, gfInsertMode);
    Undo();
    SetUndo(0);
    return FALSE;
}

/*
 * s06:20EC-24F6  ApplyPalette
 *
 * Paste Palette: gives frames iStart.. (nFrames, 0: to the end) the
 * palette hpal.  Without bit 0 of wFlags the frames are remapped to it
 * (frames without a palette are marked dirty); with it they just take it.
 * The edit is undoable: the frames are taken out and put back as copies.
 * If the first frame is not complete, a full frame is built for it.
 *
 * In the original fFull is not initialised when iStart is at the end of
 * the movie, which can't happen here.
 */
BOOL FAR PASCAL ApplyPalette(UINT iStart, UINT nFrames, HPALETTE hpal, WORD wFlags)
{
    RECT        rc;
    HPFRAME     pfr;
    HCURSOR     hcurOld = NULL;
    HANDLE      hdib;
    HPALETTE    hpalUse;
    WORD        selOld;
    WORD        nColors;
    BOOL        fFull = FALSE;
    BOOL        fNoRemap;
    UINT        i;

    if (nFrames == 0)
        nFrames = gnFrames - iStart;
    if (nFrames + iStart > gnFrames)
        nFrames = (iStart > gnFrames) ? 0 : gnFrames - iStart;
    if (nFrames == 0)
        return TRUE;

    if (!RemoveFrames(iStart, nFrames, nFrames, TRUE))
        goto Fail2;
    if (!PutFrames(iStart, nFrames, gpUndoFrames, FALSE))
        goto Fail;

    giUndoInserted = iStart;
    gnUndoInserted += nFrames;

    if (iStart < gnFrames)
        fFull = (GetFrameInfo(iStart, FI_COMPLETE) & FI_COMPLETE) == 0;

    if (fFull) {
        hdib = GetFullFrame(iStart, NULL, GFF_REBUILD);
        if (hdib)
            hdib = CopyHandle(hdib);
        if (hdib == NULL)
            goto Fail;
        SetRect(&rc, 0, 0, gcxMovie, gcyMovie);
        FreeFrame(gpFrames[iStart]);
        pfr = &gpFrames[iStart];
        pfr->medid = 0;
        pfr->wStream = 0;
        pfr->wFrame = hdib;
        pfr->rcSrc = rc;
        pfr->rcDst = rc;
        pfr->hpal = NULL;
        pfr->selRemap = 0;
        pfr->wFlags = FF_COMPLETE | FF_KEYFRAME;
    }

    fNoRemap = wFlags & 1;
    if (!fNoRemap)
        StatusSetMessage("Remapping to new palette...");

    /* PalAddRef may hand back an equal palette already in the table (and
     * delete hpal), so each frame adds a reference to what the last call
     * returned */
    hpalUse = hpal;
    for (i = iStart; i < nFrames + iStart; i++) {
        gfPalettePasted = TRUE;
        hpalUse = PalAddRef(hpalUse, 1L);
        if (hpalUse == NULL)
            goto Fail;

        if (!fNoRemap) {
            pfr = &gpFrames[i];
            if (!(pfr->wFlags & FF_TRUECOLOR)) {
                if (pfr->wFlags & FF_PALDIRTY) {
                    hcurOld = SetCursor(LoadCursor(NULL, IDC_WAIT));
                    GetFrameDIB(i, NULL, GFD_NOCACHE);
                }
                pfr = &gpFrames[i];
                if (pfr->hpal) {
                    selOld = pfr->selRemap;
                    pfr->selRemap = RemapCreate(pfr->hpal, hpalUse, selOld, 1L);
                    if (gpFrames[i].selRemap == 0)
                        goto Fail;
                    if (selOld)
                        RemapRelease(selOld, 1L);
                } else
                    pfr->wFlags |= FF_PALDIRTY;
            }
        }

        pfr = &gpFrames[i];
        if (pfr->hpal && pfr->hpal != hpalUse && (pfr->wFlags & FF_PALDIRTY)) {
            hcurOld = SetCursor(LoadCursor(NULL, IDC_WAIT));
            GetFrameDIB(i, NULL, GFD_NOCACHE);
        }

        pfr = &gpFrames[i];
        if (pfr->hpal)
            PalRelease(pfr->hpal, 1L);
        gpFrames[i].hpal = hpalUse;
    }
    hpal = hpalUse;

    if (hcurOld)
        SetCursor(hcurOld);
    if (!fNoRemap)
        StatusSetMessage(NULL);

    GetObject(hpal, sizeof(nColors), &nColors);
    gnMovieColorsUndo = gnMovieColors;
    if ((int)gnMovieColors < (int)nColors)
        gnMovieColors = nColors;
    if (nFrames == gnFrames && iStart == 0 && (int)nColors > 0)
        gnMovieColors = nColors;

    gwVideoBits = 8;
    FramesChanged(iStart, nFrames, TRUE);
    return TRUE;

Fail:
    SetUndoTracks(TRUE, FALSE, gfInsertMode);
    Undo();
Fail2:
    SetUndo(0);
    return FALSE;
}

/*
 * s06:24FA-2585  UndoFrames
 *
 * Undoes the last edit of the frame table (and a video format change):
 * takes out the frames it put in and puts back the ones it removed; the
 * state is swapped so that undoing again redoes.
 */
BOOL FAR UndoFrames(void)
{
    HPFRAME pfr = NULL;
    UINT    t;

    UndoVideoFormat();

    if (gnUndoInserted) {
        pfr = TakeFrames(giUndoInserted, gnUndoInserted);
        if (pfr == NULL)
            return FALSE;
    }

    if (gnUndoRemoved) {
        if (!PutFrames(giUndoRemoved, gnUndoRemoved, gpUndoFrames, TRUE))
            return FALSE;
    }

    gpUndoFrames = pfr;
    t = gnUndoRemoved;
    gnUndoRemoved = gnUndoInserted;
    gnUndoInserted = t;
    t = giUndoRemoved;
    giUndoRemoved = giUndoInserted;
    giUndoInserted = t;
    return TRUE;
}

/*
 * s06:2586-2637  ClearFrameUndo
 */
void FAR ClearFrameUndo(void)
{
    HCURSOR hcurOld;
    UINT    i;

    hcurOld = SetCursor(LoadCursor(NULL, IDC_WAIT));

    if (gnUndoRemoved) {
        for (i = 0; i < gnUndoRemoved; i++)
            FreeFrame(gpUndoFrames[i]);
        GFreePtr(gpUndoFrames);
        gpUndoFrames = NULL;
        gnUndoRemoved = 0;
    }
    gnUndoInserted = 0;

    if (hcurOld)
        SetCursor(hcurOld);
}

/*
 * s06:2638-2933  GetFrameInfo
 *
 * FI_* flags of a frame, as far as dwMask asks.  Frame 0 is always a
 * complete key frame.  The element is asked whether its frame is a key
 * frame (the answer is kept in the frame).  FI_PALCHANGE compares the
 * palette with that of the previous non-empty frame.
 */
DWORD FAR PASCAL GetFrameInfo(UINT iFrame, DWORD dwMask)
{
    DWORD       dwFlags = 0;
    DWORD       dw;
    LONG        l;
    HPALETTE    hpal;
    HPALETTE    hpalPrev;
    HPFRAME     pfr;

    if (iFrame >= gnFrames)
        return 0;

    if (dwMask & (FI_KEYFRAME | FI_COMPLETE)) {
        if (iFrame == 0)
            dwFlags = FI_KEYFRAME | FI_COMPLETE;
        else {
            pfr = &gpFrames[iFrame];
            if (!(pfr->wFlags & FF_KEYFRAME) && pfr->medid && pfr->wFrame != 0xFFFF) {
                dw = medSendMessage(pfr->medid, VEM_KEYFRAME, (LONG)pfr->wFrame, (LONG)pfr->wStream);
                if ((DWORD)(WORD)(gpFrames[iFrame].wFrame + 1) == dw)
                    gpFrames[iFrame].wFlags |= FF_KEYFRAME | FF_COMPLETE;
            }

            pfr = &gpFrames[iFrame];
            if (pfr->wFlags & FF_KEYFRAME)
                dwFlags = FI_KEYFRAME;
            if (pfr->rcDst.left <= 0 && pfr->rcDst.top <= 0 &&
                pfr->rcDst.right >= (int)gcxMovie && pfr->rcDst.bottom >= (int)gcyMovie &&
                (pfr->wFlags & FF_COMPLETE))
                dwFlags |= FI_COMPLETE;
        }
    }

    if ((dwMask & FI_PALCHANGE) && gwVideoBits <= 8) {
        pfr = &gpFrames[iFrame];
        if (pfr->wFrame || pfr->medid) {
            for (l = (LONG)iFrame - 1; l >= 0; l--) {
                pfr = &gpFrames[(UINT)l];
                if (pfr->wFrame || pfr->medid)
                    break;
            }
            if (l >= 0) {
                GetFrameDIB(iFrame, &hpal, GFD_PALETTE);
                GetFrameDIB((UINT)l, &hpalPrev, GFD_PALETTE);
                if (hpalPrev != hpal)
                    dwFlags |= FI_PALCHANGE;
            } else
                dwFlags |= FI_PALCHANGE;
        }
    }

    return dwFlags & dwMask;
}

/*
 * s06:2936-2CA9  GetFrameDataSize
 *
 * For the Statistics: the size of a frame's data (from the file when the
 * element knows it, else the DIB's image size) and of the palette change
 * that goes with it.  The first frame's palette counts as 4 bytes per
 * colour; a later palette change as 16 bytes for each run of changed
 * colours plus 4 per colour (the whole palette plus 16 if the previous
 * frame had none).
 */
void FAR PASCAL GetFrameDataSize(UINT iFrame, DWORD FAR *lpdwData, DWORD FAR *lpdwPal)
{
    PALETTEENTRY FAR   *ppe;
    PALETTEENTRY FAR   *ppePrev;
    LPBITMAPINFOHEADER  lpbi;
    DWORD               dwPal = 0;
    HPFRAME             pfr;
    HANDLE              hdib;
    HPALETTE            hpal;
    HPALETTE            hpalPrev;
    WORD                nColors;
    WORD                nColorsPrev;
    WORD                sel;
    UINT                i;

    if (iFrame >= gnFrames) {
        *lpdwData = 0;
        *lpdwPal = 0;
        return;
    }

    pfr = &gpFrames[iFrame];
    if (pfr->medid && pfr->wFrame != 0xFFFF) {
        *lpdwData = medSendMessage(pfr->medid, VEM_GETDATASIZE, (LONG)pfr->wFrame,
                                   (LONG)pfr->wStream);
        if (*lpdwData) {
            *lpdwPal = 0;
            return;
        }
    }

    hdib = GetFrameDIB(iFrame, &hpal, 0);
    if (hdib) {
        lpbi = (LPBITMAPINFOHEADER)GlobalLock(hdib);
        *lpdwData = lpbi->biSizeImage;
        GlobalUnlock(hdib);
    } else
        *lpdwData = 0;

    if (hpal && iFrame == 0) {
        GetObject(hpal, sizeof(nColors), &nColors);
        dwPal = (WORD)(nColors << 2);
    } else if (hpal && iFrame != 0 && gpFrames[iFrame - 1].hpal != hpal) {
        GetFrameDIB(iFrame - 1, &hpalPrev, GFD_PALETTE);
        GetFrameDIB(iFrame, &hpal, GFD_PALETTE);
        if (hpalPrev != hpal) {
            GetObject(hpal, sizeof(nColors), &nColors);
            if (hpalPrev == NULL)
                dwPal = (WORD)((nColors + 4) << 2);
            else {
                GetObject(hpalPrev, sizeof(nColorsPrev), &nColorsPrev);
                sel = GAllocSelF(GMEM_MOVEABLE, (DWORD)((nColorsPrev + nColors) << 2));
                if (sel) {
                    ppe = (PALETTEENTRY FAR *)MAKELP(sel, 0);
                    ppePrev = ppe + nColors;
                    GetPaletteEntries(hpal, 0, nColors, ppe);
                    GetPaletteEntries(hpalPrev, 0, nColorsPrev, ppePrev);

                    for (i = 0; i < nColors; i++) {
                        if (i < nColorsPrev && ppe[i].peRed == ppePrev[i].peRed &&
                            ppe[i].peBlue == ppePrev[i].peBlue && ppe[i].peGreen == ppePrev[i].peGreen)
                            continue;
                        dwPal += 16;
                        if (i >= nColors)
                            continue;
                        for (;;) {
                            if (i < nColorsPrev && ppe[i].peRed == ppePrev[i].peRed &&
                                ppe[i].peBlue == ppePrev[i].peBlue &&
                                ppe[i].peGreen == ppePrev[i].peGreen)
                                break;
                            dwPal += 4;
                            i++;
                            if (i >= nColors)
                                break;
                        }
                    }
                }
                GlobalUnlock((HGLOBAL)sel);
                GlobalFree((HGLOBAL)sel);
            }
        }
    }

    *lpdwPal = dwPal;
}

/*
 * s06:2CAC-2CD2  CanPasteFrames
 */
BOOL FAR CanPasteFrames(void)
{
    if (IsClipboardFormatAvailable(gcfDibRange) || IsClipboardFormatAvailable(CF_DIB) ||
        IsClipboardFormatAvailable(CF_BITMAP))
        return TRUE;
    return FALSE;
}

/*
 * s06:2CD4-34FD  CopyFrames
 *
 * Copies frames to the clipboard (which the caller has opened and
 * emptied).  The first one goes as a full frame in CF_DIB, CF_BITMAP and
 * CF_PALETTE (16-bit movies as 24-bit); the others as FRAMEs in VidEdit's
 * private format, followed by the data of frames held in memory, their
 * palettes (as LOGPALETTEs) and remap tables.  The elements the frames
 * refer to get a reference for the clipboard, kept in the list ghDibRangeList.
 */
BOOL FAR PASCAL CopyFrames(UINT iStart, UINT nFrames)
{
    BYTE _huge         *pClip;
    LPLOGPALETTE        plp;
    LPBITMAPINFOHEADER  lpbi;
    LPVOID              lpData;
    HPFRAME             pfr;
    MedReturn           mr;
    DWORD               off;
    DWORD               cb;
    HANDLE              hdib = NULL;
    HANDLE              hFull;
    HPALETTE            hpalClip = NULL;
    HPALETTE            hpalFrame;
    HBITMAP             hbm = NULL;
    HGLOBAL             hClip;
    HGLOBAL             hlp;
    WORD                nColors;
    UINT                wBits;
    BOOL                fFound;
    UINT                i;
    UINT                k;

    if (iStart >= gnFrames)
        return TRUE;
    if (nFrames + iStart > gnFrames)
        nFrames = gnFrames - iStart;

    off = (DWORD)nFrames * sizeof(FRAME);
    hClip = GlobalAlloc(GMEM_SHARE | GMEM_MOVEABLE, off);
    if (hClip == NULL)
        goto Fail;
    pClip = (BYTE _huge *)GlobalLock(hClip);
    *(WORD _huge *)pClip = nFrames - 1;
    *(DWORD _huge *)(pClip + 2) = gCompOptions.dwUSecPerFrame;

    for (i = iStart; i < nFrames + iStart; i++) {
        if (i == iStart) {
            wBits = gwVideoBits;
            if (wBits == 16)
                gwVideoBits = 24;
            hFull = GetFullFrame(i, &hpalFrame, GFF_CONVERT | GFF_ESCAPE | GFF_REBUILD);
            gwVideoBits = wBits;
            if (hFull == NULL || hFull == 1)
                goto Fail;
            hdib = CopyHandle(hFull);
            if (hdib == NULL)
                goto Fail;
            if (hpalFrame) {
                hpalClip = CopyPalette(hpalFrame);
                if (hpalClip == NULL)
                    goto Fail;
            }
            hbm = BitmapFromDib(hdib, hpalClip, 0);
            continue;
        }

        ((HPFRAME)pClip)[i - iStart] = gpFrames[i];

        if (gpFrames[i].medid) {
            fFound = FALSE;
            glpClipMedids = (MEDID FAR *)GlobalLock((HGLOBAL)ghDibRangeList);
            if (glpClipMedids == NULL)
                goto Fail;
            for (k = 0; k < gcDibRangeList; k++) {
                if (gpFrames[i].medid == glpClipMedids[k])
                    fFound = TRUE;
            }
            if (!fFound) {
                if (medAccess(gpFrames[i].medid, 0L, &mr, FALSE, NULL, 0L) != 1)
                    goto Fail;
                if (gcDibRangeList && !(gcDibRangeList & 7)) {
                    GlobalUnlock((HGLOBAL)ghDibRangeList);
                    ghDibRangeList = GlobalReAlloc((HGLOBAL)ghDibRangeList, GlobalSize((HGLOBAL)ghDibRangeList) + 0x20,
                                           GMEM_SHARE | GMEM_MOVEABLE);
                    if (ghDibRangeList == 0)
                        goto FailRelease;
                    glpClipMedids = (MEDID FAR *)GlobalLock((HGLOBAL)ghDibRangeList);
                    if (glpClipMedids == NULL)
                        goto FailRelease;
                }
                glpClipMedids[gcDibRangeList] = gpFrames[i].medid;
                gcDibRangeList++;
            }
            GlobalUnlock((HGLOBAL)ghDibRangeList);
        }

        pfr = &gpFrames[i];
        if (pfr->medid == 0 && pfr->wFrame) {
            lpbi = (LPBITMAPINFOHEADER)GlobalLock((HGLOBAL)gpFrames[i].wFrame);
            if (lpbi == NULL)
                goto Fail;
            cb = (lpbi->biClrUsed << 2) + lpbi->biSizeImage + lpbi->biSize;
            GlobalUnlock(hClip);
            hClip = GlobalReAlloc(hClip, GlobalSize(hClip) + cb + 4, GMEM_SHARE | GMEM_MOVEABLE);
            if (hClip == NULL)
                goto FailUnlockDIB;
            pClip = (BYTE _huge *)GlobalLock(hClip);
            *(DWORD _huge *)(pClip + off) = cb;
            off += 4;
            hmemmove(pClip + off, lpbi, cb);
            off += cb;
            GlobalUnlock((HGLOBAL)gpFrames[i].wFrame);
        }

        if (gpFrames[i].hpal) {
            if (GetObject(gpFrames[i].hpal, sizeof(nColors), &nColors) != sizeof(nColors))
                goto Fail;
            cb = (WORD)((nColors + 1) << 2);
            GlobalUnlock(hClip);
            hClip = GlobalReAlloc(hClip, GlobalSize(hClip) + cb + 4, GMEM_SHARE | GMEM_MOVEABLE);
            if (hClip == NULL)
                goto Fail;
            pClip = (BYTE _huge *)GlobalLock(hClip);
            *(DWORD _huge *)(pClip + off) = cb;
            off += 4;

            hlp = GlobalAlloc(GMEM_SHARE | GMEM_MOVEABLE, cb);
            if (hlp == NULL)
                goto Fail;
            plp = (LPLOGPALETTE)GlobalLock(hlp);
            plp->palVersion = 0x300;
            plp->palNumEntries = nColors;
            if (!GetPaletteEntries(gpFrames[i].hpal, 0, nColors, plp->palPalEntry))
                goto FailLogPal;
            hmemmove(pClip + off, plp, cb);
            GlobalUnlock(hlp);
            GlobalFree(hlp);
            off += cb;
        }

        if (gpFrames[i].selRemap) {
            lpData = GlobalLock((HGLOBAL)gpFrames[i].selRemap);
            if (lpData == NULL)
                goto FailUnlockRemap;
            cb = RemapSize(gpFrames[i].selRemap);
            GlobalUnlock(hClip);
            hClip = GlobalReAlloc(hClip, GlobalSize(hClip) + cb + 4, GMEM_SHARE | GMEM_MOVEABLE);
            if (hClip == NULL)
                goto FailUnlockRemap;
            pClip = (BYTE _huge *)GlobalLock(hClip);
            *(DWORD _huge *)(pClip + off) = cb;
            off += 4;
            hmemmove(pClip + off, lpData, cb);
            off += cb;
            GlobalUnlock((HGLOBAL)gpFrames[i].selRemap);
        }
    }

    SetClipboardData(CF_DIB, hdib);
    if (hbm)
        SetClipboardData(CF_BITMAP, hbm);
    if (hpalClip)
        SetClipboardData(CF_PALETTE, hpalClip);
    if (hClip) {
        GlobalUnlock(hClip);
        SetClipboardData(gcfDibRange, hClip);
    }
    return TRUE;

FailRelease:
    medRelease(gpFrames[i].medid, 0L);
    goto Fail;

FailUnlockDIB:
    GlobalUnlock((HGLOBAL)gpFrames[i].wFrame);
    goto Fail;

FailLogPal:
    GlobalUnlock(hlp);
    GlobalFree(hlp);
    goto Fail;

FailUnlockRemap:
    GlobalUnlock((HGLOBAL)gpFrames[i].selRemap);

Fail:
    if (hdib)
        GlobalFree(hdib);
    if (hpalClip)
        DeleteObject(hpalClip);
    if (hbm)
        DeleteObject(hbm);
    if (hClip) {
        GlobalUnlock(hClip);
        GlobalFree(hClip);
    }
    ReleaseClipboardMedids();
    return FALSE;
}

/*
 * s06:3500-3596  GetClipboardFrameCount
 *
 * How many frames the clipboard holds, and their frame rate (0 if not
 * VidEdit's).
 */
UINT FAR PASCAL GetClipboardFrameCount(DWORD FAR *lpdwUSecPerFrame)
{
    BYTE _huge *p;
    HANDLE      hdib;
    HANDLE      hbm;
    HGLOBAL     hClip;
    UINT        n = 0;

    hdib = GetClipboardData(CF_DIB);
    hbm = GetClipboardData(CF_BITMAP);
    hClip = GetClipboardData(gcfDibRange);

    if (lpdwUSecPerFrame)
        *lpdwUSecPerFrame = 0;
    if (hdib || hbm)
        n = 1;

    if (hClip) {
        p = (BYTE _huge *)GlobalLock(hClip);
        n += *(WORD _huge *)p;
        if (lpdwUSecPerFrame)
            *lpdwUSecPerFrame = *(DWORD _huge *)(p + 2);
        GlobalUnlock(hClip);
    }
    return n;
}

/*
 * s06:359A-3DCF  PasteFrames
 *
 * Pastes the frames on the clipboard at iPos (replacing the selection in
 * insert mode): the picture (CF_DIB, else CF_BITMAP converted at the
 * screen's depth) as the first frame, then VidEdit's frames, each taking
 * its own references and copies of its data.  Frames of another size are
 * stretched or clipped as chosen for the picture.  The frame rate of the
 * clipboard frames is converted to the movie's.
 *
 * In the original, fFits is not initialised without a picture on the
 * clipboard (VidEdit always puts one there), nor hClip when inserting the
 * picture fails.  The clipboard's DIB is locked and never unlocked.
 */
BOOL FAR PASCAL PasteFrames(UINT iPos, UINT nSel, UINT nFrames)
{
    BYTE _huge     *pClip;
    HPFRAME         pfrSrc;
    HPFRAME         pfrDst;
    HPFRAME         pfrNew;
    LPLOGPALETTE    plp;
    MedReturn       mr;
    DWORD           off;
    DWORD           cb;
    DWORD           dwUSec;
    HGLOBAL         hClip = NULL;
    HGLOBAL         hNew = NULL;
    HGLOBAL         h;
    HGLOBAL         hlp;
    HANDLE          hdibClip;
    HANDLE          hdib;
    HPALETTE        hpalClip;
    HPALETTE        hpal;
    HBITMAP         hbm;
    HDC             hdc;
    WORD            sel;
    int             nBits;
    BOOL            fFits = FALSE;
    UINT            iPosOrig;
    UINT            nFramesOrig;
    UINT            nMovieOrig;
    UINT            nClip;
    UINT            nMovie;
    UINT            nEntries;
    UINT            k;
    UINT            n;

    if (gfInsertMode) {
        if (nFrames - nSel + gnFrames > 0x7FF0)
            return FALSE;
    } else if (nFrames + iPos > 0x7FF0)
        return FALSE;

    nFramesOrig = nFrames;
    iPosOrig = iPos;
    nMovieOrig = gnFrames;

    hdibClip = GetClipboardData(CF_DIB);
    hpalClip = GetClipboardData(CF_PALETTE);
    hbm = GetClipboardData(CF_BITMAP);

    if (hdibClip) {
        hdib = CopyHandle((HANDLE)SELECTOROF(GlobalLock(hdibClip)));
        if (hdib == NULL)
            return FALSE;
    } else if (hbm) {
        hdc = GetDC(NULL);
        nBits = GetDeviceCaps(hdc, BITSPIXEL) * GetDeviceCaps(hdc, PLANES);
        nBits = (nBits > 8) ? 24 : 8;
        ReleaseDC(NULL, hdc);
        hdib = DibFromBitmap(hbm, 0L, nBits, hpalClip, 0);
        if (hdib == NULL)
            return FALSE;
    } else
        hdib = NULL;

    gfStretchNew = FALSE;

    nClip = GetClipboardFrameCount(&dwUSec);
    if (dwUSec == 0)
        dwUSec = gCompOptions.dwUSecPerFrame;
    nMovie = (UINT)muldiv32((LONG)nClip, (LONG)dwUSec, (LONG)gCompOptions.dwUSecPerFrame);
    RemoveFrames(iPos, nSel, nMovie - nClip + nFrames, TRUE);
    if (!gfInsertMode && nClip > nMovie)
        InsertFrameSlots(iPos + nMovie, nClip - nMovie);

    if (hdib) {
        if (!InsertDIBFrame(iPos, hdib)) {
            GlobalFree(hdib);
            goto Fail;
        }
        giUndoInserted = iPos;
        if (!gfInsertMode) {
            if (gnFrames - giUndoInserted >= nFrames) {
                fFits = TRUE;
                gnUndoInserted += nFrames;
            } else {
                fFits = FALSE;
                gnUndoInserted++;
            }
        } else
            gnUndoInserted++;
        nFrames--;
        iPos++;
    }

    hClip = GetClipboardData(gcfDibRange);
    if (hClip) {
        pClip = (BYTE _huge *)GlobalLock(hClip);
        if (pClip == NULL)
            goto Fail;

        nEntries = *(WORD _huge *)pClip;
        dwUSec = *(DWORD _huge *)(pClip + 2);
        if (nMovieOrig == 0 && dwUSec)
            gCompOptions.dwUSecPerFrame = dwUSec;

        if (nEntries) {
            pfrSrc = (HPFRAME)(pClip + 0x20);
            off = (DWORD)(nEntries + 1) * sizeof(FRAME);

            hNew = GlobalAlloc(GMEM_SHARE | GMEM_MOVEABLE, (DWORD)nEntries * sizeof(FRAME));
            if (hNew == NULL)
                goto Fail;
            pfrNew = pfrDst = (HPFRAME)GlobalLock(hNew);

            for (k = 0; k < nEntries; k++) {
                *pfrDst = *pfrSrc;

                if (gfStretchNew) {
                    pfrDst->rcDst.left = 0;
                    pfrDst->rcDst.top = 0;
                    pfrDst->rcDst.right = gcxMovie;
                    pfrDst->rcDst.bottom = gcyMovie;
                } else {
                    if (pfrDst->rcDst.right > (int)gcxMovie) {
                        if (pfrDst->rcDst.left > (int)gcxMovie)
                            pfrDst->rcDst.left = gcxMovie - 1;
                        pfrDst->rcSrc.right = MulDiv(pfrDst->rcSrc.right - pfrDst->rcSrc.left,
                                                     gcxMovie - pfrDst->rcDst.left,
                                                     pfrDst->rcDst.right - pfrDst->rcDst.left)
                                              + pfrDst->rcSrc.left;
                        pfrDst->rcDst.right = gcxMovie;
                    }
                    if (pfrDst->rcDst.bottom > (int)gcyMovie) {
                        if (pfrDst->rcDst.top > (int)gcyMovie)
                            pfrDst->rcDst.top = gcyMovie - 1;
                        pfrDst->rcSrc.bottom = MulDiv(pfrDst->rcSrc.bottom - pfrDst->rcSrc.top,
                                                      gcyMovie - pfrDst->rcDst.top,
                                                      pfrDst->rcDst.bottom - pfrDst->rcDst.top)
                                               + pfrDst->rcSrc.top;
                        pfrDst->rcDst.bottom = gcyMovie;
                    }
                }

                if (pfrSrc->medid) {
                    if (medAccess(pfrSrc->medid, 0L, &mr, FALSE, NULL, 0L) != 1)
                        goto FailLoop;
                }

                if (pfrSrc->medid == 0 && pfrSrc->wFrame) {
                    cb = *(DWORD _huge *)(pClip + off);
                    off += 4;
                    h = GlobalAlloc(GMEM_SHARE | GMEM_MOVEABLE, cb);
                    if (h == NULL)
                        goto FailElement;
                    hmemmove(GlobalLock(h), pClip + off, cb);
                    off += cb;
                    pfrDst->wFrame = h;
                    GlobalUnlock(h);
                }

                if (pfrSrc->hpal) {
                    cb = *(DWORD _huge *)(pClip + off);
                    off += 4;
                    hlp = GlobalAlloc(GMEM_SHARE | GMEM_MOVEABLE, cb);
                    if (hlp == NULL)
                        goto FailDIB;
                    plp = (LPLOGPALETTE)GlobalLock(hlp);
                    hmemmove(plp, pClip + off, cb);
                    hpal = CreatePalette(plp);
                    if (hpal == NULL)
                        goto FailLogPal;
                    off += cb;
                    GlobalUnlock(hlp);
                    GlobalFree(hlp);
                    hpal = PalAddRef(hpal, 1L);
                    if (hpal == NULL)
                        goto FailDIB;
                    pfrDst->hpal = hpal;
                }

                if (pfrSrc->selRemap) {
                    cb = *(DWORD _huge *)(pClip + off);
                    off += 4;
                    h = GlobalAlloc(GMEM_SHARE | GMEM_MOVEABLE, cb);
                    if (h == NULL)
                        goto FailPalette;
                    hmemmove(GlobalLock(h), pClip + off, cb);
                    off += cb;
                    GlobalUnlock(h);
                    sel = RemapAddRef((WORD)SELECTOROF(GlobalLock(h)), 1L);
                    if (sel == 0)
                        goto FailPalette;
                    pfrDst->selRemap = sel;
                }

                pfrDst++;
                pfrSrc++;
            }

            if (!PutFrames(iPos, nEntries, pfrNew, TRUE))
                goto FailLoop;

            if (gfInsertMode || !fFits)
                gnUndoInserted += nEntries;
            nFrames -= nEntries;
            iPos += nEntries;
        }
    }

    if (hClip == NULL)
        n = nFrames;
    else {
        k = ConvertFrameRate(dwUSec, gCompOptions.dwUSecPerFrame, iPosOrig, iPosOrig - nFrames + nFramesOrig);
        if (k == 0)
            return FALSE;
        n = nFrames;
        gnUndoInserted += k - nFramesOrig + n;
    }

    if (n && gfInsertMode && iPos < gnFrames) {
        if (!InsertFrameSlots(iPos, n))
            goto Fail;
        gnUndoInserted += n;
    }
    return TRUE;

FailElement:
    if (pfrSrc->medid)
        medRelease(pfrSrc->medid, 0L);
    goto FailLoop;

FailLogPal:
    if (pfrSrc->medid)
        medRelease(pfrSrc->medid, 0L);
    if (pfrSrc->medid == 0 && pfrSrc->wFrame)
        GlobalFree((HGLOBAL)pfrDst->wFrame);
    GlobalUnlock(hlp);
    GlobalFree(hlp);
    goto FailLoop;

FailDIB:
    if (pfrSrc->medid)
        medRelease(pfrSrc->medid, 0L);
    if (pfrSrc->medid == 0 && pfrSrc->wFrame)
        GlobalFree((HGLOBAL)pfrDst->wFrame);
    goto FailLoop;

FailPalette:
    if (pfrSrc->medid)
        medRelease(pfrSrc->medid, 0L);
    if (pfrSrc->medid == 0 && pfrSrc->wFrame)
        GlobalFree((HGLOBAL)pfrDst->wFrame);
    if (pfrSrc->hpal)
        PalRelease(pfrDst->hpal, 1L);

FailLoop:
    pfrSrc = (HPFRAME)(pClip + 0x20);
    pfrDst = pfrNew;
    for (n = k; n; n--) {
        if (pfrSrc->medid)
            medRelease(pfrSrc->medid, 0L);
        if (pfrSrc->medid == 0 && pfrSrc->wFrame)
            GlobalFree((HGLOBAL)pfrDst->wFrame);
        if (pfrSrc->hpal)
            PalRelease(pfrDst->hpal, 1L);
        if (pfrSrc->selRemap)
            RemapRelease(pfrDst->selRemap, 1L);
        pfrSrc++;
        pfrDst++;
    }

Fail:
    if (hClip)
        GlobalUnlock(hClip);
    if (hNew) {
        GlobalUnlock(hNew);
        GlobalFree(hNew);
    }
    SetUndoTracks(TRUE, FALSE, gfInsertMode);
    Undo();
    SetUndo(0);
    return FALSE;
}

/*
 * s06:3DD2-3EAF  LoadAllFrames  (not found by Ghidra: called from s01)
 *
 * Load File into Memory: reads every frame that comes from an element
 * into the cache, pinned.  Escape cancels.
 */
BOOL FAR PASCAL LoadAllFrames(HWND hwnd)
{
    char    ach[50];
    HCURSOR hcurOld;
    BOOL    fOK = TRUE;
    UINT    i;

    hcurOld = SetCursor(LoadCursor(NULL, IDC_WAIT));
    GetAsyncKeyState(VK_ESCAPE);

    for (i = 0; i < gnFrames; i++) {
        if (GetAsyncKeyState(VK_ESCAPE) & 1) {
            fOK = FALSE;
            break;
        }
        if (gpFrames[i].medid) {
            wsprintf(ach, "Loading frame #%u", i);
            StatusSetMessage(ach);
            if (!GetFrameDIB(i, NULL, GFD_PIN)) {
                MessageBeep(MB_ICONEXCLAMATION);
                ErrorResBox(hwnd, (HINSTANCE)ghInst, MB_ICONEXCLAMATION, IDS_CAPTION,
                           IDS_NOTENOUGHMEMORY);
                fOK = FALSE;
                break;
            }
        }
    }

    StatusSetMessage(NULL);
    if (hcurOld)
        SetCursor(hcurOld);
    return fOK;
}

/*
 * s06:3EB2-46AB  ConvertFrameRate
 *
 * Converts frames iStart..iEnd-1 from a rate of dwUSecSrc microseconds
 * per frame to dwUSecDst, returning the new number of frames (0 on
 * error).  More frames: each new frame takes the source frame it maps
 * to, the first time that frame is used (the others stay empty, so they
 * repeat it).  Fewer frames: each new frame gets the frames that map to
 * it collapsed into one: the last one if it is complete, the only one
 * with data, or else a full frame built from them.  The current frame
 * moves with the conversion and the movie takes the new rate.
 */
UINT FAR PASCAL ConvertFrameRate(DWORD dwUSecSrc, DWORD dwUSecDst, UINT iStart, UINT iEnd)
{
    RECT        rc;
    HPFRAME     pfr;
    HPALETTE    hpalTmp;
    HCURSOR     hcurOld;
    HANDLE      hdib;
    DWORD       n;
    LONG        lTotal;
    LONG        j;
    UINT        iNewEnd;
    UINT        iSrc;
    UINT        iSrcPrev = 0;
    UINT        iFrom;
    UINT        iFirst;
    UINT        iSecond;
    UINT        m;
    BOOL        f;

    if (dwUSecDst == dwUSecSrc)
        return iEnd - iStart;

    if ((int)iStart < 0 || gnFrames < iEnd || iEnd <= iStart)
        goto Error;

    n = (DWORD)(iEnd - iStart);
    lTotal = muldiv32((LONG)n, (LONG)dwUSecSrc, (LONG)dwUSecDst) - (LONG)n + (LONG)gnFrames;
    if (lTotal < 1 || lTotal > 0x7FF0)
        goto Error;

    if (gnFrames > gwLengthShown)
        SetLength(gnFrames, TRUE);

    iNewEnd = (UINT)muldiv32((LONG)n, (LONG)dwUSecSrc, (LONG)dwUSecDst) + iStart;

    if (iNewEnd > iEnd) {
        if (gfInsertMode)
            f = InsertFrameSlots(iEnd, iNewEnd - iEnd);
        else if (gnFrames < iNewEnd)
            f = InsertFrameSlots(gnFrames, iNewEnd - gnFrames);
        else
            f = TRUE;
        if (!f) {
            MessageBeep(MB_ICONEXCLAMATION);
            ErrorResBox((HWND)ghwndApp, (HINSTANCE)ghInst, MB_ICONEXCLAMATION, IDS_CAPTION,
                       IDS_NOTENOUGHMEMORY);
            return 0;
        }

        hcurOld = SetCursor(LoadCursor(NULL, IDC_WAIT));
        for (j = (LONG)(int)(iNewEnd - 1); j >= (LONG)(int)iStart; j--) {
            iSrc = (UINT)muldiv32(j - (LONG)iStart, (LONG)dwUSecDst, (LONG)dwUSecSrc) + iStart;
            if (iSrc < (UINT)j) {
                iSrcPrev = (UINT)muldiv32(j - (LONG)iStart - 1, (LONG)dwUSecDst, (LONG)dwUSecSrc)
                           + iStart;
                if (iSrcPrev != iSrc) {
                    gpFrames[(UINT)j] = gpFrames[iSrc];
                    pfr = &gpFrames[iSrc];
                    pfr->wFrame = 0;
                    pfr->medid = 0;
                    pfr->wStream = 0;
                    pfr->wFlags = 0;
                    pfr->hpal = NULL;
                    pfr->selRemap = 0;
                }
            }
        }
        if (hcurOld)
            SetCursor(hcurOld);
    }

    if (iNewEnd < iEnd) {
        hcurOld = SetCursor(LoadCursor(NULL, IDC_WAIT));

        for (j = (LONG)iStart; j < (LONG)(int)iNewEnd; j++) {
            iSrc = (UINT)muldiv32(j - (LONG)iStart, (LONG)dwUSecDst, (LONG)dwUSecSrc) + iStart;
            if (iSrc <= (UINT)j)
                goto Next;

            /* the first frame with data among those mapping to frame j */
            iFrom = iSrcPrev + 1;
            for (m = iFrom; m <= iSrc; m++) {
                if (gpFrames[m].medid || gpFrames[m].wFrame)
                    break;
            }
            if (m > iSrc)
                goto Next;

            if (GetFrameInfo(iSrc, FI_COMPLETE) & FI_COMPLETE) {
                /* the last one is complete: it becomes frame j */
                for (m = iFrom; m <= iSrc; m++) {
                    if (m == iSrc)
                        gpFrames[(UINT)j] = gpFrames[m];
                    else
                        FreeFrame(gpFrames[m]);
                    pfr = &gpFrames[m];
                    pfr->wFrame = 0;
                    pfr->medid = 0;
                    pfr->wStream = 0;
                    pfr->wFlags = 0;
                    pfr->hpal = NULL;
                    pfr->selRemap = 0;
                }
                goto Next;
            }

            /* a second frame with data? */
            iFirst = m;
            for (m++; m <= iSrc; m++) {
                if (gpFrames[m].medid || gpFrames[m].wFrame)
                    break;
            }
            iSecond = m;

            if (iSrc < iSecond && (UINT)j != iFirst) {
                gpFrames[(UINT)j] = gpFrames[iFirst];
                pfr = &gpFrames[iFirst];
                pfr->wFrame = 0;
                pfr->medid = 0;
                pfr->wStream = 0;
                pfr->wFlags = 0;
                pfr->hpal = NULL;
                pfr->selRemap = 0;
            }

            if (iSrc >= iSecond) {
                hdib = CopyHandle(GetFullFrame(iSrc, &hpalTmp, GFF_CONVERT | GFF_STATUS | GFF_REBUILD));
                if (hdib == NULL)
                    goto OutOfMemory;

                for (m = iFrom; m <= iSrc; m++) {
                    FreeFrame(gpFrames[m]);
                    pfr = &gpFrames[m];
                    pfr->wFrame = 0;
                    pfr->medid = 0;
                    pfr->wStream = 0;
                    pfr->wFlags = 0;
                    pfr->hpal = NULL;
                    pfr->selRemap = 0;
                }

                rc.left = 0;
                rc.top = 0;
                rc.right = gcxMovie;
                rc.bottom = gcyMovie;
                pfr = &gpFrames[(UINT)j];
                pfr->medid = 0;
                pfr->wStream = 0;
                pfr->wFrame = hdib;
                pfr->wFlags = FF_COMPLETE | FF_KEYFRAME;
                pfr->hpal = NULL;
                pfr->selRemap = 0;
                pfr->rcSrc = rc;
                pfr->rcDst = rc;
            }
Next:
            iSrcPrev = iSrc;
        }

        StatusSetMessage(NULL);
        for (m = iSrcPrev + 1; m < iEnd; m++)
            FreeFrame(gpFrames[m]);
        RemoveFrameSlots(iNewEnd, iEnd - iNewEnd);
        if (hcurOld)
            SetCursor(hcurOld);
    }

    if (iStart <= gwCurFrame && gwCurFrame < iEnd)
        SeekTo((WORD)muldiv32((LONG)(WORD)(gwCurFrame - iStart), (LONG)dwUSecSrc, (LONG)dwUSecDst) + iStart);

    gCompOptions.dwUSecPerFrame = dwUSecDst;
    SetLength(gnFrames, TRUE);
    UpdateLength();
    SetDirty();
    return (UINT)muldiv32((LONG)n, (LONG)dwUSecSrc, (LONG)dwUSecDst);

OutOfMemory:
    MessageBeep(MB_ICONEXCLAMATION);
    ErrorResBox((HWND)ghwndApp, (HINSTANCE)ghInst, MB_ICONEXCLAMATION, IDS_CAPTION,
               IDS_RATEOUTOFMEMORY);
    StatusSetMessage(NULL);
    if (hcurOld)
        SetCursor(hcurOld);
    SetDirty();
    return 0;

Error:
    MessageBeep(MB_ICONEXCLAMATION);
    ErrorResBox((HWND)ghwndApp, (HINSTANCE)ghInst, MB_ICONEXCLAMATION, IDS_CAPTION,
               IDS_CANTCONVERTRATE);
    return 0;
}

/*
 * s06:46AE-4A30  ReplaceLostMedid  (not found by Ghidra: called from s04)
 *
 * An element some frames refer to went away (it belonged to an instance
 * of VidEdit that was closed): the user is asked for the file again, and
 * the frames, the movie on the clipboard and the clipboard's element list
 * are switched to it.  If the file can't be had, the frames become empty.
 *
 * Bug of the original: when the clipboard frames lose their element, the
 * frame number is cleared in the movie's frame with the same index
 * instead of in the clipboard's frame.
 */
void FAR PASCAL ReplaceLostMedid(MEDID medid)
{
    char        achName[144];
    BYTE _huge *pClip;
    HPFRAME     pfrClip;
    MedReturn   mr;
    MEDID       medidNew = 0;
    HCURSOR     hcurOld;
    HGLOBAL     hClip;
    BOOL        fDone = FALSE;
    BOOL        fClipDone;
    UINT        nEntries;
    UINT        i;

    hcurOld = SetCursor(LoadCursor(NULL, IDC_WAIT));
    SetUndo(0);

    for (i = 0; i < gnFrames; i++) {
        if (gpFrames[i].medid != medid)
            continue;

        if (!fDone) {
            SetDirty();
            medGetFileName(medid, achName, sizeof(achName));
            if (WrkOpenFileName(achName, &mr, medFOURCC('D', 'I', 'B', 'S'), 0x2001,
                                (HWND)ghwndApp, "") == 1) {
                medidNew = mr.medid;
                AddMedid(medidNew);
            } else {
                MessageBeep(MB_ICONEXCLAMATION);
                ErrorResBox((HWND)ghwndApp, (HINSTANCE)ghInst, MB_ICONEXCLAMATION, IDS_CAPTION,
                           IDS_LOSTMOVIEELEMENT);
                medidNew = 0;
            }
        }

        medRelease(medid, 0L);
        gpFrames[i].medid = medidNew;
        if (medidNew == 0)
            gpFrames[i].wFrame = 0;

        if (!fDone)
            fDone = TRUE;
        else if (medidNew)
            medAccess(medidNew, 0L, &mr, FALSE, NULL, 0L);
    }

    StatusSetMessage(NULL);

    if (GetClipboardOwner() == (HWND)ghwndFrame && OpenClipboard((HWND)ghwndFrame)) {
        hClip = GetClipboardData(gcfDibRange);
        if (hClip == NULL) {
            CloseClipboard();
            goto Done;
        }

        pClip = (BYTE _huge *)GlobalLock(hClip);
        nEntries = *(WORD _huge *)pClip;
        pfrClip = (HPFRAME)(pClip + 0x20);
        fClipDone = FALSE;

        for (i = 0; i < nEntries; i++, pfrClip++) {
            if (pfrClip->medid != medid)
                continue;

            if (!fDone && !fClipDone) {
                medGetFileName(medid, achName, sizeof(achName));
                if (WrkOpenFileName(achName, &mr, medFOURCC('D', 'I', 'B', 'S'), 0x2001,
                                    (HWND)ghwndApp, "") == 1) {
                    medidNew = mr.medid;
                    AddMedid(medidNew);
                } else {
                    MessageBeep(MB_ICONEXCLAMATION);
                    ErrorResBox((HWND)ghwndApp, (HINSTANCE)ghInst, MB_ICONEXCLAMATION, IDS_CAPTION,
                               IDS_LOSTCLIPELEMENT);
                    EmptyClipboard();
                    CloseClipboard();
                    medidNew = 0;
                }
                fClipDone = TRUE;
                medRelease(medid, 0L);
            }

            pfrClip->medid = medidNew;
            if (medidNew == 0)
                gpFrames[i].wFrame = 0;

            if (!fClipDone) {
                medRelease(medid, 0L);
                medAccess(medidNew, 0L, &mr, FALSE, NULL, 0L);
                fClipDone = TRUE;
            }
        }

        GlobalUnlock(hClip);
        CloseClipboard();

        glpClipMedids = (MEDID FAR *)GlobalLock((HGLOBAL)ghDibRangeList);
        if (glpClipMedids) {
            for (i = 0; i < gcDibRangeList; i++) {
                if (glpClipMedids[i] == medid)
                    glpClipMedids[i] = medidNew;
            }
            GlobalUnlock((HGLOBAL)ghDibRangeList);
        }
    }

Done:
    if (hcurOld)
        SetCursor(hcurOld);
}

/*
 * s06:4A34-4BDA  IsUnchangedAvi
 *
 * TRUE if the movie can be saved by copying the frames of the AVI file it
 * came from: no palette was pasted, every frame with data comes from
 * that file (gmedidMovie) uncropped and unstretched, and either the movie is
 * 8-bit with no compressor or an RLE one, or the frames are in file order
 * between key frames.
 *
 * In the original wPrev is not initialised; it is only read after a frame
 * from the file was seen, unless the movie starts with an empty frame.
 */
BOOL FAR IsUnchangedAvi(void)
{
    RECT    rc;
    HPFRAME pfr;
    WORD    wPrev = 0;
    UINT    i;

    if (gfPalettePasted)
        return FALSE;
    if (!IsAviFile())
        return FALSE;

    if (gfDirty) {
        rc.left = 0;
        rc.top = 0;
        rc.right = gcxMovie;
        rc.bottom = gcyMovie;

        for (i = 0; i < gnFrames; i++) {
            pfr = &gpFrames[i];
            if (pfr->medid == 0 && pfr->wFrame == 0)
                continue;
            if (pfr->medid && pfr->medid != gmedidMovie)
                return FALSE;
            if (_fmemcmp((RECT FAR *)&pfr->rcSrc, &rc, sizeof(RECT)))
                return FALSE;
            if (_fmemcmp((RECT FAR *)&pfr->rcDst, &rc, sizeof(RECT)))
                return FALSE;
        }

        if (gwVideoBits == 8 &&
            (gCompOptionsSaved.fccHandler == 0 || gCompOptionsSaved.fccHandler == mmioFOURCC('R', 'L', 'E', ' ') ||
             gCompOptionsSaved.fccHandler == mmioFOURCC('R', 'L', 'E', '0')))
            return TRUE;

        for (i = 0; i < gnFrames; i++) {
            pfr = &gpFrames[i];
            if (pfr->medid == 0 && pfr->wFrame == 0)
                continue;
            if (pfr->medid == 0)
                return FALSE;
            if (!(pfr->wFlags & FF_KEYFRAME) && (i == 0 || pfr->wFrame - wPrev - 1 != 0))
                return FALSE;
            if (pfr->medid)
                wPrev = pfr->wFrame;
        }
    }
    return TRUE;
}

/*
 * s06:4BDC-4D47  GetSaveFrame
 *
 * A frame for saving: the element's data as stored (*lpfKey tells if it
 * is a key frame), else a copy of the full frame, and for an empty frame
 * an empty BITMAPINFOHEADER of the movie's size (left locked).
 */
HANDLE FAR PASCAL GetSaveFrame(UINT iFrame, BOOL FAR *lpfKey)
{
    LPBITMAPINFOHEADER  lpbi;
    HPFRAME             pfr;
    DWORD               dw;
    HANDLE              h;

    if (iFrame < gnFrames && !(gpFrames[iFrame].medid == 0 && gpFrames[iFrame].wFrame == 0)) {
        pfr = &gpFrames[iFrame];
        if (pfr->medid && pfr->wFrame != 0xFFFF) {
            dw = medSendMessage(pfr->medid, VEM_GETDATA, (LONG)pfr->wFrame, (LONG)pfr->wStream);
            if (dw == 0 || (LOWORD(dw) == 0xFFFF && HIWORD(dw) == 0xFFFF))
                return NULL;
            if (lpfKey)
                *lpfKey = (HIWORD(dw) & 0x0100) ? TRUE : FALSE;
            return LOWORD(dw);
        }

        h = CopyHandle(GetFullFrame(iFrame, NULL, 0));
        if (lpfKey)
            *lpfKey = TRUE;
        return h;
    }

    h = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, sizeof(BITMAPINFOHEADER));
    if (h) {
        lpbi = (LPBITMAPINFOHEADER)GlobalLock(h);
        lpbi->biSizeImage = 0;
        lpbi->biBitCount = 0;
        lpbi->biWidth = gcxMovie;
        lpbi->biHeight = gcyMovie;
        lpbi->biCompression = BI_RGB;
        lpbi->biClrUsed = 0;
        lpbi->biSize = sizeof(BITMAPINFOHEADER);
    }
    if (lpfKey)
        *lpfKey = FALSE;
    return h;
}
