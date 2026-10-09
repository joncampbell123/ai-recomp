/*
 * frames.h - types and prototypes of the reconstructed VIDEDIT.EXE code
 * in segment 6: frames.c, framerct.c, paltable.c, remap.c, framedrw.c,
 * fullfrm.c.
 *
 * The movie's video track is a table of FRAMEs.  A frame either refers to
 * a frame of a MediaMan video element (an AVI file's video stream, or a
 * still picture element) or holds a DIB in memory, and it carries the
 * part of that image it shows (rcSrc), where the part goes in the movie
 * frame (rcDst), the palette it uses and a colour remap table.
 */

#ifndef _FRAMES_H_
#define _FRAMES_H_

/* ------------------------------------------------------------------ */
/* the frame table                                                     */
/* ------------------------------------------------------------------ */

typedef struct tagFRAME {
    MEDID       medid;          /* 00 video element holding the frame; 0: wFrame is a DIB */
    WORD        wFrame;         /* 04 frame number in medid (0xFFFF: the element is one
                                      picture), or the DIB in memory when medid is 0 */
    WORD        wFlags;         /* 06 FF_* */
    RECT        rcSrc;          /* 08 part of the image shown */
    RECT        rcDst;          /* 10 where it goes in the movie frame */
    HPALETTE    hpal;           /* 18 palette (a paltable.c reference) */
    WORD        selRemap;       /* 1A colour remap table to hpal (a remap.c reference) */
    WORD        wStream;        /* 1C passed to the element along with wFrame */
    WORD        w1E;            /* 1E not used */
} FRAME;                        /* 0x20 bytes */
typedef FRAME _huge *HPFRAME;

/* FRAME.wFlags */
#define FF_PALDIRTY         0x0004  /* palette must be taken from the DIB again */
#define FF_CHECKED          0x0100  /* format of the DIB examined */
#define FF_TRUECOLOR        0x0200  /* more than 8 bits per pixel */
#define FF_COMPLETE         0x0400  /* a complete image: BI_RGB, or RLE without skips */
#define FF_KEYFRAME         0x1000  /* the element says it is a key frame */

/* GetFrameDIB wFlags; the DIB cache's flags are the same word */
#define GFD_PALETTE         0x0001  /* only the palette is wanted, if it is known */
#define GFD_NOCACHE         0x0002  /* don't make room for it or move it up in the cache */
#define GFD_PIN             0x0010  /* keep it in the cache (Load File into Memory) */
#define GFD_RELOAD          0x0100  /* palette unknown: read 8-bit DIBs again */

/* GetFrameInfo */
#define FI_PALCHANGE        0x00000001L /* the palette differs from the previous frame's */
#define FI_KEYFRAME         0x00000010L /* key frame in its element */
#define FI_COMPLETE         0x00000020L /* key frame covering the whole movie frame */

/* GetFullFrame wFlags */
#define GFF_REBUILD         0x0001  /* don't take a complete frame's own DIB as is */
#define GFF_ESCAPE          0x0010  /* Escape cancels (returns 1) */
#define GFF_STATUS          0x0020  /* always show progress */
#define GFF_CONVERT         0x0040  /* convert to the movie's bit depth */

/*
 * Messages of the video element (the type VidEdit registers for the
 * frames of an AVI video stream).  wFrame and wStream travel as lParam1
 * and lParam2.
 */
#define VEM_GETINFO         (MED_USER + 0)  /* l1 = VIDEOELEMINFO FAR *, l2 = size */
#define VEM_GETFRAME        (MED_USER + 5)  /* -> MAKELONG(hdib, flags), flags 0x100: key frame */
#define VEM_GETLENGTH       (MED_USER + 6)  /* -> number of frames */
#define VEM_KEYFRAME        (MED_USER + 7)  /* -> wFrame + 1 if wFrame is a key frame */
#define VEM_GETDATASIZE     (MED_USER + 9)  /* -> size of the frame's data in the file */
#define VEM_GETDATA         (MED_USER + 10) /* -> MAKELONG(hdata, flags): the data as stored */

typedef struct tagVIDEOELEMINFO {
    DWORD   dw00;
    DWORD   dwFlags;            /* 04 VEIF_* */
    DWORD   dwWidth;            /* 08 */
    DWORD   dwHeight;           /* 0C */
    DWORD   dw10;
    DWORD   dwMicroSecPerFrame; /* 14 */
    DWORD   adw18[5];
} VIDEOELEMINFO;                /* 0x2C bytes */

#define VEIF_INMEMORY       0x0002L /* frames are quick to get: don't offer loading into memory */
#define VEIF_FAST           0x0004L /* no hourglass while getting a frame */
#define VEIF_LENGTHKNOWN    0x0010L /* counting the frames is quick */
#define VEIF_ALLKEY         0x0020L /* every frame is complete */

/* the movie on the clipboard (private format gcfDibRange): a WORD with the
 * number of FRAMEs that follow, the DWORD frame rate, padding up to 0x20,
 * the FRAMEs, then for each frame its DIB, palette and remap table, each
 * preceded by its DWORD size.  The first frame is on the clipboard as
 * CF_DIB (plus CF_BITMAP and CF_PALETTE) instead. */

/* ------------------------------------------------------------------ */
/* frames.c                                                            */
/* ------------------------------------------------------------------ */

extern UINT     gnFrames;               /* DS:02B0 */
extern BOOL     gfStretchNew;           /* DS:02B2 */
extern HPFRAME  gpFrames;               /* DS:02B4 */
extern UINT     gwVideoBits;            /* DS:02BA bits per pixel of the movie (0: unknown) */
extern BOOL     gfPalettePasted;        /* DS:02BC */
extern char     gszVidfrmDibRange[];    /* DS:02DC "VidfrmDibRange" */
extern UINT     gwVideoBitsUndo;        /* DS:3086 */

HANDLE  FAR PASCAL GetFrameDIB(UINT iFrame, HPALETTE NEAR *phpal, WORD wFlags);
BOOL    FAR PASCAL BlankFrames(UINT iPos, UINT nFrames);
BOOL    FAR PASCAL RemoveFrames(UINT iStart, UINT nSel, UINT nFrames, BOOL fPaste);
void    FAR ReleaseClipboardMedids(void);
BOOL    FAR PASCAL InsertStillFrame(UINT iPos, UINT nSel, WORD FAR *lpwFrames, MEDID medid);
BOOL    FAR HasPaletteChanges(void);
BOOL    FAR PASCAL GetMovieFormat(LPBITMAPINFOHEADER lpbi);
void    FAR FreeAllFrames(void);
BOOL    FAR PASCAL DeleteFrames(UINT iStart, UINT nFrames);
void    FAR PASCAL RedrawFrame(UINT iFrom, UINT iTo);
UINT    FAR GetFrameCount(void);
BOOL    FAR PASCAL InsertVideoElement(UINT iPos, UINT nSel, UINT nMin, WORD FAR *lpwFrames,
                                      MEDID medid, WORD wStream, BOOL fNoRemove);
BOOL    FAR PASCAL ApplyPalette(UINT iStart, UINT nFrames, HPALETTE hpal, WORD wFlags);
BOOL    FAR UndoFrames(void);
void    FAR ClearFrameUndo(void);
DWORD   FAR PASCAL GetFrameInfo(UINT iFrame, DWORD dwMask);
void    FAR PASCAL GetFrameDataSize(UINT iFrame, DWORD FAR *lpdwData, DWORD FAR *lpdwPal);
BOOL    FAR CanPasteFrames(void);
BOOL    FAR PASCAL CopyFrames(UINT iStart, UINT nFrames);
UINT    FAR PASCAL GetClipboardFrameCount(DWORD FAR *lpdwUSecPerFrame);
BOOL    FAR PASCAL PasteFrames(UINT iPos, UINT nSel, UINT nFrames);
BOOL    FAR PASCAL LoadAllFrames(HWND hwnd);
UINT    FAR PASCAL ConvertFrameRate(DWORD dwUSecSrc, DWORD dwUSecDst, UINT iStart, UINT iEnd);
void    FAR PASCAL ReplaceLostMedid(MEDID medid);
BOOL    FAR IsUnchangedAvi(void);
HANDLE  FAR PASCAL GetSaveFrame(UINT iFrame, BOOL FAR *lpfKey);

/* ------------------------------------------------------------------ */
/* framerct.c                                                          */
/* ------------------------------------------------------------------ */

void    FAR PASCAL CropFrames(UINT x, UINT y, UINT cx, UINT cy);
void    FAR PASCAL ResizeFrames(UINT cx, UINT cy);
void    FAR UndoFrameRects(void);
void    FAR ClearFrameRectUndo(void);
BOOL    FAR PASCAL FindPaletteChange(UINT iStart, UINT NEAR *pnFrames);

/* ------------------------------------------------------------------ */
/* paltable.c, remap.c                                                 */
/* ------------------------------------------------------------------ */

void     FAR PalTableFree(void);
HPALETTE FAR PASCAL PalAddRef(HPALETTE hpal, DWORD dwRef);
BOOL     FAR PASCAL PalRelease(HPALETTE hpal, DWORD dwRef);

void    FAR RemapTableFree(void);
BOOL    FAR PASCAL RemapRelease(WORD selRemap, DWORD dwRef);
WORD    FAR PASCAL RemapCreate(HPALETTE hpalSrc, HPALETTE hpalDst, WORD selRemapIn, DWORD dwRef);
WORD    FAR PASCAL RemapAddRef(WORD selRemap, DWORD dwRef);
WORD    FAR PASCAL RemapSize(WORD selRemap);

/* ------------------------------------------------------------------ */
/* framedrw.c                                                          */
/* ------------------------------------------------------------------ */

/* a BITMAPINFO for DIB_PAL_COLORS with the identity colour table */
typedef struct tagPALINDEXBMI {
    BITMAPINFOHEADER    bmiHeader;      /* 00 */
    WORD                awIndex[256];   /* 28 0, 1, 2, ... 255 */
} PALINDEXBMI;

extern PALINDEXBMI gbmiPalIndex;        /* DS:2A94 */

BOOL    NEAR PASCAL DrawFrameDIB(HDC hdc, HANDLE hdib, HPALETTE hpal, RECT FAR *lprcSrc,
                                 RECT FAR *lprcDst, WORD wZoom, WORD wFlags);
void    FAR PASCAL FramesChanged(UINT iStart, UINT nFrames, BOOL fUnused);
LONG    FAR PASCAL VideoFormatDlgProc(HWND hdlg, UINT msg, WPARAM wParam, LPARAM lParam);
void    FAR VideoFormat(void);
void    FAR UndoVideoFormat(void);
void    FAR PASCAL PaintFrame(HDC hdc, BOOL fErase);
UINT    FAR PASCAL RealizeFramePalette(HDC hdc);

/* ------------------------------------------------------------------ */
/* fullfrm.c                                                           */
/* ------------------------------------------------------------------ */

extern BOOL     gfPalIndexReady;        /* DS:129C gawPalIndex filled in */
extern BOOL     gfFullFrameStale;       /* DS:129E */

WORD    FAR PASCAL GetFullFrame(UINT iFrame, HPALETTE NEAR *phpal, WORD wFlags);
int     FAR PASCAL StretchRLEDIBits(HDC hdc, int xd, int yd, int wd, int hd, int xs, int ys,
                                    int ws, int hs, LPVOID lpBits, LPBITMAPINFOHEADER lpbi,
                                    UINT wUsage, DWORD rop);

#endif /* _FRAMES_H_ */
