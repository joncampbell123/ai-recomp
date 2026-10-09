/*
 * undo.c - code segment 1 of VIDEDIT.EXE, s01:1312-178F.
 *
 * One level of undo: gwUndoType is the command that can be
 * undone (IDM_INSERT, IDM_DELETE, IDM_CUT, IDM_PASTE, IDM_PASTEPALETTE,
 * IDM_CROP, IDM_RESIZE, IDM_SYNCHRONIZE, IDM_AUDIOFORMAT,
 * IDM_VIDEOFORMAT and 0x137, a second kind of resize), or IDM_UNDO
 * after an undo, with the undone command in gwRedoType so that it can be
 * redone.  The frame and audio modules keep the data; edits are undone
 * by running them again the other way with the insert/overwrite mode
 * swapped.
 */

#include "videdit.h"
#include "vemain.h"
#include "vedwnd.h"
#include "frames.h"
#include "edit.h"
#include "compress.h"

#define IDM_RESIZE2     0x137           /* the other resize undo (from s04) */

#define SWAP(a, b)      ((a) ^= (b), (b) ^= (a), (a) ^= (b))

/* data */
WORD    gcxUndo;                        /* DS:2CBC movie size before the edit */
WORD    gcyUndo;                        /* DS:2CBE */
BOOL    gfUndoVideo;                    /* DS:2CC0 the edit changed the video track */
BOOL    gfUndoAudio;                    /* DS:2CC2 ... the audio track */
BOOL    gfUndoInsertMode;               /* DS:2CC4 swapped with gfInsertMode for undo */
BOOL    gfUndoDirty;                    /* DS:2CC6 the dirty flag before the edit */
WORD    gwUndoFrame;                    /* DS:2D54 position and selection to restore */
WORD    gwRedoFrame;                    /* DS:31A4 */
WORD    gwRedoSelEnd;                   /* DS:31BE */
WORD    gwRedoSelStart;                 /* DS:31F0 */
WORD    gwUndoSelEnd;                   /* DS:3276 */
WORD    gwUndoSelStart;                 /* DS:3278 */

/*
 * s01:1312-142B  SetUndoMenu  (FAR PASCAL)
 *
 * Sets the text of the Undo item (and the tool bar's undo text) for the
 * current undo type.  Returns FALSE if there is nothing to undo.
 */
BOOL FAR PASCAL SetUndoMenu(HMENU hMenu, UINT id)
{
    char    ach[30];
    UINT    wFlags;
    UINT    ids;
    UINT    idsTB;

    wFlags = MF_BYCOMMAND;
    switch (gwUndoType) {
    case IDM_INSERT:        ids = IDS_UNDOINSERT;       idsTB = IDS_TBUNDOINSERT;       break;
    case IDM_PASTEPALETTE:  ids = IDS_UNDOPASTEPALETTE; idsTB = IDS_TBUNDOPASTEPALETTE; break;
    case IDM_DELETE:        ids = IDS_UNDODELETE;       idsTB = IDS_TBUNDODELETE;       break;
    case IDM_CROP:          ids = IDS_UNDOCROP;         idsTB = IDS_TBUNDOCROP;         break;
    case IDM_RESIZE:        ids = IDS_UNDORESIZE;       idsTB = IDS_TBUNDORESIZE;       break;
    case IDM_SYNCHRONIZE:   ids = IDS_UNDOSYNC;         idsTB = IDS_TBUNDOSYNC;         break;
    case IDM_AUDIOFORMAT:   ids = IDS_UNDOAUDIOFORMAT;  idsTB = IDS_TBUNDOAUDIOFORMAT;  break;
    case IDM_VIDEOFORMAT:   ids = IDS_UNDOVIDEOFORMAT;  idsTB = IDS_TBUNDOVIDEOFORMAT;  break;
    case IDM_RESIZE2:       ids = IDS_UNDORESIZE2;      idsTB = IDS_TBUNDORESIZE2;      break;
    case IDM_CUT:           ids = IDS_UNDOCUT;          idsTB = IDS_TBUNDOCUT;          break;
    case IDM_PASTE:         ids = IDS_UNDOPASTE;        idsTB = IDS_TBUNDOPASTE;        break;
    case IDM_UNDO:          ids = IDS_UNDOUNDO;         idsTB = IDS_TBUNDOUNDO;         break;
    default:
        wFlags = MF_BYCOMMAND | MF_GRAYED;
        ids = IDS_UNDO;
        idsTB = IDS_TBNOUNDO;
        break;
    }
    LoadString(ghInst, ids, ach, sizeof(ach));
    ModifyMenu(hMenu, id, wFlags, id, ach);
    toolbarModifyString(ghwndToolBar, 5, idsTB);
    return (idsTB != IDS_TBNOUNDO) ? TRUE : FALSE;
}

/*
 * s01:142E-14EF  SetUndo  (FAR PASCAL)
 *
 * Throws away the data kept for the current undo and records a new undo
 * type (0: none).  Recording a type also marks the movie changed.
 * Nothing is freed for IDM_RESIZE.
 */
void FAR PASCAL SetUndo(UINT wType)
{
    switch (gwUndoType) {
    case IDM_PASTEPALETTE:
        ClearFrameUndo();
        break;
    case IDM_CROP:
    case IDM_RESIZE2:
        ClearFrameRectUndo();
        break;
    case IDM_INSERT:
    case IDM_DELETE:
    case IDM_CUT:
    case IDM_PASTE:
    clear:
        if (gfUndoVideo)
            ClearFrameUndo();
        if (gfUndoAudio)
            WaveClearUndo();
        break;
    case IDM_UNDO:
        switch (gwRedoType) {
        case IDM_PASTEPALETTE:
            ClearFrameUndo();
            break;
        case IDM_CROP:
        case IDM_RESIZE2:
            ClearFrameRectUndo();
            break;
        case IDM_INSERT:
        case IDM_DELETE:
        case IDM_CUT:
        case IDM_PASTE:
            goto clear;
        }
        break;
    }

    gwUndoType = wType;
    gwRedoType = 0;
    switch (wType) {
    case IDM_INSERT:
    case IDM_DELETE:
    case IDM_CROP:
    case IDM_RESIZE:
    case IDM_RESIZE2:
    case IDM_CUT:
    case IDM_PASTE:
        gcxUndo = gcxMovie;
        gcyUndo = gcyMovie;
        break;
    }
    gfUndoDirty = gfDirty;
    if (wType)
        SetDirty();
}

/*
 * s01:14F2-1509  SetUndoSelection  (FAR PASCAL)
 */
void FAR PASCAL SetUndoSelection(WORD wSelStart, WORD wSelEnd, WORD wFrame)
{
    gwUndoSelStart = wSelStart;
    gwUndoSelEnd = wSelEnd;
    gwUndoFrame = wFrame;
}

/*
 * s01:150C-1523  SetRedoSelection  (FAR PASCAL)
 */
void FAR PASCAL SetRedoSelection(WORD wSelStart, WORD wSelEnd, WORD wFrame)
{
    gwRedoSelStart = wSelStart;
    gwRedoSelEnd = wSelEnd;
    gwRedoFrame = wFrame;
}

/*
 * s01:1526-15F5  UndoEdit  (NEAR PASCAL)
 *
 * Undoes (fUndo) or redoes an insert, delete, cut or paste: restores
 * the movie size, then lets the frame and audio modules reverse their
 * edits with the insert mode swapped, and restores the position and
 * selection.
 */
static BOOL NEAR PASCAL UndoEdit(BOOL fUndo)
{
    BOOL    fOK;
    WORD    cx, cy;

    cx = gcxUndo;
    cy = gcyUndo;
    gcxUndo = gcxMovie;
    gcyUndo = gcyMovie;
    SetFrameSize(cx, cy);
    SWAP(gfInsertMode, gfUndoInsertMode);

    fOK = TRUE;
    if (gfUndoVideo && !UndoFrames())
        fOK = FALSE;
    if (gfUndoAudio && !WaveUndo())
        fOK = FALSE;

    if (fUndo) {
        SetSelection(0xFFFF, 0xFFFF);
        SeekTo(gwUndoFrame);
        UpdateLength();
        SetSelection(gwUndoSelStart, gwUndoSelEnd);
    } else {
        SetSelection(0xFFFF, 0xFFFF);
        SeekTo(gwRedoFrame);
        UpdateLength();
        SetSelection(gwRedoSelStart, gwRedoSelEnd);
    }
    SWAP(gfInsertMode, gfUndoInsertMode);
    return fOK;
}

/*
 * s01:15F8-1774  Undo
 *
 * Quirk: the success flag is set only for edits and Paste Palette.
 * Undoing a crop, resize, synchronize or format change (or undoing with
 * nothing to undo) tests an uninitialised stack word, so whether
 * "An error occurred while undoing" appears depends on what was left on
 * the stack.  The dirty flag is swapped with the one saved by SetUndo.
 */
void FAR Undo(void)
{
    UINT    wType;
    UINT    wRedo;
    BOOL    fOK;                /* not set on every path, see above */
    WORD    cx, cy;

    wType = gwUndoType;
    wRedo = gwRedoType;
    gwUndoType = IDM_UNDO;
    gwRedoType = wType;

    switch (wType) {
    case IDM_INSERT:
    case IDM_DELETE:
    case IDM_CUT:
    case IDM_PASTE:
        fOK = UndoEdit(TRUE);
        break;

    case IDM_UNDO:              /* redo */
        gwRedoType = 0;
        gwUndoType = wRedo;
        switch (wRedo) {
        case IDM_SYNCHRONIZE:
            goto sync;
        case IDM_RESIZE:
            goto resize;
        case IDM_INSERT:
        case IDM_DELETE:
        case IDM_CUT:
        case IDM_PASTE:
            fOK = UndoEdit(FALSE);
            break;
        case IDM_PASTEPALETTE:
            goto pastepal;
        case IDM_CROP:
        case IDM_RESIZE2:
            goto crop;
        case IDM_AUDIOFORMAT:
            goto audiofmt;
        case IDM_VIDEOFORMAT:
            goto videofmt;
        }
        break;

    case IDM_PASTEPALETTE:
    pastepal:
        fOK = UndoFrames();
        SWAP(gnMovieColors, gnMovieColorsUndo);
        UpdateLength();
        break;

    case IDM_CROP:
    case IDM_RESIZE2:
    crop:
        UndoFrameRects();
        /* fall through */
    case IDM_RESIZE:
    resize:
        cx = gcxUndo;
        cy = gcyUndo;
        gcxUndo = gcxMovie;
        gcyUndo = gcyMovie;
        SetFrameSize(cx, cy);
        break;

    case IDM_SYNCHRONIZE:
    sync:
        WaveSwapOffset();
        MidiSwapOffset();
        SwapUSecPerFrame();
        UpdateLength();
        break;

    case IDM_AUDIOFORMAT:
    audiofmt:
        WaveUndoFormat();
        break;

    case IDM_VIDEOFORMAT:
    videofmt:
        UndoVideoFormat();
        break;
    }

    if (fOK) {
        if (gfUndoDirty) {
            gfUndoDirty = gfDirty;
            SetDirty();
        } else {
            gfUndoDirty = gfDirty;
            gfDirty = FALSE;
        }
    } else {
        MessageBeep(MB_ICONEXCLAMATION);
        ErrorResBox(ghwndApp, ghInst, MB_ICONEXCLAMATION, IDS_APPTITLE, IDS_UNDOERROR);
        SetUndo(0);
    }
}

/*
 * s01:1776-178D  SetUndoTracks  (FAR PASCAL)
 */
void FAR PASCAL SetUndoTracks(BOOL fVideo, BOOL fAudio, BOOL fInsertMode)
{
    gfUndoVideo = fVideo;
    gfUndoAudio = fAudio;
    gfUndoInsertMode = fInsertMode;
}
