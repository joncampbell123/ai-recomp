/*
 * framerct.c - code segment 6 of VIDEDIT.EXE, s06:4D4A-5297: cropping and
 * resizing the movie by changing the frames' rectangles
 *
 * Crop Video and Resize Video don't touch the images: they change each
 * frame's source and destination rectangles.  The table is saved first
 * so that the change can be undone.  Also here: finding the next palette
 * change.
 *
 * Functions are in the order of the binary.
 */

#include "videdit.h"
#include "frames.h"

#define IDS_FINDINGPALCHANGE    193     /* "Finding palette change" */

/* DS:037C-03B5 */
static WORD     gwRectUndo037C;         /* DS:037C set to 0, never read */
static UINT     gnRectUndo;             /* DS:037E */
static HPFRAME  gpRectUndo;             /* DS:0380 copy of the table before the change */
/* DS:0384: "Releasing %u frames held for crop/stretch undo.\n", a debug
 * message that is not used in the release build */

/*
 * s06:4D4A-4DD9  SaveFrameRects  (NEAR)
 *
 * Copies the frame table for undo.  If there is no memory for it the edit
 * just can't be undone; it always returns TRUE.
 */
static BOOL NEAR SaveFrameRects(void)
{
    gnRectUndo = gnFrames;
    gwRectUndo037C = 0;

    if (gnFrames) {
        gpRectUndo = (HPFRAME)GAllocPtrF(GMEM_MOVEABLE, (DWORD)gnFrames * sizeof(FRAME));
        if (gpRectUndo == NULL) {
            gnRectUndo = 0;
            SetUndo(0);
            return TRUE;
        }
        hmemmove(gpRectUndo, gpFrames, (DWORD)gnFrames * sizeof(FRAME));
    }
    return TRUE;
}

/*
 * s06:4DDA-4FDD  CropFrames
 *
 * Crops the movie to the rectangle (x, y, cx, cy): each frame shows the
 * part of its image that falls in the rectangle, at the top left.  The
 * scale between a frame's source and destination is kept.
 *
 * The new source position is x / scale minus (rcDst.left - rcSrc.left),
 * mixing destination and source coordinates, as in the original.
 */
void FAR PASCAL CropFrames(UINT x, UINT y, UINT cx, UINT cy)
{
    HPFRAME pfr;
    double  dScale;
    double  dOffset;
    int     w;
    int     h;
    UINT    i;

    if (!SaveFrameRects())
        return;

    for (i = 0; i < gnFrames; i++) {
        pfr = &gpFrames[i];
        w = cx;
        h = cy;
        if ((UINT)pfr->rcDst.right < x + cx)
            w = pfr->rcDst.right - x;
        if ((UINT)pfr->rcDst.bottom < y + cy)
            h = pfr->rcDst.bottom - y;

        dScale = (double)(pfr->rcDst.right - pfr->rcDst.left) /
                 (pfr->rcSrc.right - pfr->rcSrc.left);
        dOffset = (double)(DWORD)x / dScale - (pfr->rcDst.left - pfr->rcSrc.left);
        pfr->rcSrc.left = (int)dOffset;
        pfr->rcSrc.right = (int)((double)(DWORD)(UINT)w / dScale + dOffset);
        pfr->rcDst.left = 0;
        pfr->rcDst.right = w;

        dScale = (double)(pfr->rcDst.bottom - pfr->rcDst.top) /
                 (pfr->rcSrc.bottom - pfr->rcSrc.top);
        dOffset = (double)(DWORD)y / dScale - (pfr->rcDst.top - pfr->rcSrc.top);
        pfr->rcSrc.top = (int)dOffset;
        pfr->rcSrc.bottom = (int)((double)(DWORD)(UINT)h / dScale + dOffset);
        pfr->rcDst.top = 0;
        pfr->rcDst.bottom = h;
    }

    FramesChanged(0, 0, TRUE);
}

/*
 * s06:4FE0-50CC  ResizeFrames
 *
 * Resizes the movie to cx by cy: every frame's destination rectangle is
 * scaled (right and bottom, rounded).
 */
void FAR PASCAL ResizeFrames(UINT cx, UINT cy)
{
    HPFRAME pfr;
    double  dx;
    double  dy;
    UINT    i;
    UINT    n;

    if (!SaveFrameRects())
        return;

    dx = (double)(DWORD)cx / (double)(DWORD)gcxMovie;
    dy = (double)(DWORD)cy / (double)(DWORD)gcyMovie;

    n = gnFrames;
    for (i = 0; i < n; i++) {
        pfr = &gpFrames[i];
        pfr->rcDst.right = (int)(pfr->rcDst.right * dx + 0.5);
        pfr->rcDst.bottom = (int)(pfr->rcDst.bottom * dy + 0.5);
    }

    FramesChanged(0, 0, TRUE);
}

/*
 * s06:50D0-51CA  UndoFrameRects
 *
 * Swaps the frames' rectangles with the saved ones (so undoing again
 * redoes).  It goes by the current number of frames.
 */
void FAR UndoFrameRects(void)
{
    RECT    rc;
    HPFRAME pfrUndo;
    HPFRAME pfr;
    UINT    i;

    for (i = 0; i < gnFrames; i++) {
        pfrUndo = &gpRectUndo[i];
        pfr = &gpFrames[i];

        rc = pfrUndo->rcSrc;
        pfrUndo->rcSrc = pfr->rcSrc;
        pfr->rcSrc = rc;

        rc = pfrUndo->rcDst;
        pfrUndo->rcDst = pfr->rcDst;
        pfr->rcDst = rc;
    }

    FramesChanged(0, 0, TRUE);
}

/*
 * s06:51CC-51EF  ClearFrameRectUndo
 */
void FAR ClearFrameRectUndo(void)
{
    if (gnRectUndo) {
        GFreePtr(gpRectUndo);
        gpRectUndo = NULL;
        gnRectUndo = 0;
    }
}

/*
 * s06:51F0-5295  FindPaletteChange  (unreferenced; not found by Ghidra)
 *
 * Counts the frames from iStart that have the same palette as iStart, in
 * *pnFrames (at least 1).  Escape cancels (FALSE).
 */
BOOL FAR PASCAL FindPaletteChange(UINT iStart, UINT NEAR *pnFrames)
{
    HCURSOR     hcurOld;
    HPALETTE    hpalStart;
    HPALETTE    hpal;
    BOOL        fOK = TRUE;

    hcurOld = SetCursor(LoadCursor(NULL, IDC_WAIT));
    GetAsyncKeyState(VK_ESCAPE);
    StatusSetMessage(MAKEINTRESOURCE(IDS_FINDINGPALCHANGE));

    GetFrameDIB(iStart, &hpalStart, GFD_PALETTE | GFD_NOCACHE);
    *pnFrames = 1;

    if (iStart + 1 < gnFrames) {
        do {
            GetFrameDIB(*pnFrames + iStart, &hpal, GFD_PALETTE | GFD_NOCACHE);
            if (hpal != hpalStart)
                break;
            if (GetAsyncKeyState(VK_ESCAPE) & 1) {
                fOK = FALSE;
                break;
            }
            (*pnFrames)++;
        } while (*pnFrames + iStart < gnFrames);
    }

    StatusSetMessage(NULL);
    if (hcurOld)
        SetCursor(hcurOld);
    return fOK;
}
