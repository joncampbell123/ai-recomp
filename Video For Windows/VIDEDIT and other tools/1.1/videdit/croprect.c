/*
 * croprect.c - code segment 18 of VIDEDIT.EXE (0x0BFA bytes), the crop
 * rectangle tracker
 *
 * While the Crop Video dialog (crop.c, segment 20) is up, the frame
 * window shows the crop rectangle as a "marching ants" frame: a 4 pixel
 * wide band drawn with an 8x8 diagonal pattern brush whose origin moves
 * one pixel every 75 ms, plus four 8x8 handles just outside the corners.
 * The dialog forwards the frame window's mouse messages here:
 *
 *   - a drag on a corner handle sizes the rectangle (the corner flips when
 *     the rectangle is dragged through itself),
 *   - a drag on the frame band moves it,
 *   - a click inside or outside the rectangle starts a new one once the
 *     mouse has moved 3 pixels; a click without a drag puts the old one
 *     back.
 *
 * The rectangle is kept in image coordinates (grcCrop) and clipped to the
 * frame size (gcxMovie x gcyMovie, gcxMovie x gcyMovie); s04 converts between
 * window and image coordinates.  Every change is reported to the dialog
 * through CropShowRect / CropShowPoint.
 *
 * One module: its data are DS:109C-10C3 (the "AnimSTrackBar"/"GrayBlob"
 * class names every module carries, then the pattern bits).  All functions
 * are FAR PASCAL except the three NEAR PASCAL helpers.
 */

#include "videdit.h"
#include "crop.h"
#include "croprect.h"

/* gwCropFlags */
#define CROPF_ACTIVE        0x0001      /* CropStart was called */
#define CROPF_NOHANDLES     0x0002      /* draw no corner handles */
#define CROPF_XCOLLAPSED    0x0010      /* width went to 0 while sizing */
#define CROPF_YCOLLAPSED    0x0020      /* height went to 0 while sizing */

/* gwCropState */
#define CROP_NONE           0           /* no rectangle */
#define CROP_SHOWN          1           /* rectangle shown, ants marching */
#define CROP_SIZING         2           /* dragging a corner */
#define CROP_MOVING         3           /* dragging the frame */
#define CROP_CLICKED        4           /* button down, no drag yet */

#define CROP_TIMER          0x0400
#define CROP_TIMERMS        75

/* initialised data */
static WORD awCropPattern[8] = {        /* DS:10B4 8x8 monochrome bitmap */
    0x0038, 0x0070, 0x00E0, 0x00C1, 0x0083, 0x0007, 0x000E, 0x001C
};

/* uninitialised data */
HWND        ghwndCrop;                  /* DS:2A62 window the rectangle is drawn in */
WORD        gwCropSaveUpdate;           /* DS:2A66 gwFrameUpdate before CropStart */
RECT        grcCropSave;                /* DS:2A68 rectangle before a click */
WORD        gwCropFlags;                /* DS:2D10 CROPF_* */
COLORREF    gcrCropFore;                /* DS:2D58 */
RECT        grcCropDrawn;               /* DS:3062 rectangle as last drawn */
RECT        grcCrop;                    /* DS:3088 the rectangle, image coordinates */
COLORREF    gcrCropBack;                /* DS:3090 */
WORD        gwCropState;                /* DS:3178 CROP_* */
int         giCropAnts;                 /* DS:317A brush origin, 0..7 */
HBRUSH      ghbrCrop;                   /* DS:3194 */
WORD        gwCropCorner;               /* DS:319C corner being sized: bit 0 right, bit 1 bottom */
RECT        grcCropStart;               /* DS:3268 rectangle when the drag started */
POINT       gptCropGrab;                /* DS:3270 grab offset (sizing) or grab point (moving, click) */

static BOOL FAR PASCAL  CropStopTracking(BOOL fErase);
static WORD FAR PASCAL  CropBeginDrag(POINT pt, WORD wFlags);
static BOOL FAR PASCAL  CropNewRect(POINT pt);
static BOOL FAR PASCAL  CropBeginClick(POINT pt);
static BOOL FAR PASCAL  CropBeginSize(POINT pt, WORD wCorner);
static BOOL FAR PASCAL  CropBeginMove(POINT pt);
static WORD FAR PASCAL  CropHitTest(POINT pt);
static void FAR PASCAL  CropSetGrabSigns(WORD wCorner);
static void NEAR PASCAL CropAddHandleRgns(RECT *prc, HRGN hrgn, HRGN hrgnTmp);
static void NEAR PASCAL CropMakeFrameRgn(RECT *prc, HRGN hrgn, HRGN hrgnTmp, BOOL fHandles);
static void NEAR PASCAL CropDrawRgn(BOOL fErase, BOOL fDraw);
static void NEAR PASCAL CropSizeTo(POINT pt);

/*
 * s18:0000-003D  CropInitBrush  (FAR PASCAL, called from s02 at start-up)
 *
 * Makes the pattern brush: blue and white diagonal stripes.
 */
BOOL FAR PASCAL CropInitBrush(void)
{
    HBITMAP hbm;

    hbm = CreateBitmap(8, 8, 1, 1, (LPSTR)awCropPattern);
    ghbrCrop = CreatePatternBrush(hbm);
    DeleteObject(hbm);
    gcrCropFore = RGB(0, 0, 255);
    gcrCropBack = RGB(255, 255, 255);
    return TRUE;
}

/*
 * s18:003E-005A  CropTermBrush  (FAR PASCAL, called from s03 at exit)
 */
BOOL FAR PASCAL CropTermBrush(void)
{
    if (ghbrCrop == NULL)
        return FALSE;

    DeleteObject(ghbrCrop);
    ghbrCrop = NULL;
    return TRUE;
}

/*
 * s18:005C-0084  CropNormalizeRect  (FAR PASCAL)
 */
void FAR PASCAL CropNormalizeRect(RECT *prc)
{
    int t;

    if (prc->right < prc->left) {
        t = prc->right;
        prc->right = prc->left;
        prc->left = t;
    }
    if (prc->top > prc->bottom) {
        t = prc->top;
        prc->top = prc->bottom;
        prc->bottom = t;
    }
}

/*
 * s18:0086-00C3  CropStart  (FAR PASCAL, called from the Crop dialog's
 * WM_INITDIALOG)
 *
 * Starts tracking in hwnd.  While the dialog is up the frame update mode
 * (gwFrameUpdate, the View menu command) is forced to Full Frame Update.  The
 * window that is repainted is ghwndFrame, not hwnd; the dialog passes ghwndFrame.
 */
BOOL FAR PASCAL CropStart(HWND hwnd, WORD wFlags)
{
    gwCropFlags = (wFlags & CROPF_NOHANDLES) | CROPF_ACTIVE;
    ghwndCrop = hwnd;
    gwCropSaveUpdate = gwFrameUpdate;
    gwFrameUpdate = IDM_FULLFRAMEUPDATE;
    InvalidateRect(ghwndFrame, NULL, FALSE);
    UpdateWindow(ghwndFrame);
    return TRUE;
}

/*
 * s18:00C4-0126  CropStopTracking  (FAR PASCAL)
 *
 * Ends a drag and removes the rectangle (erasing it if fErase).  The
 * compiler's range check before the switch has a branch that can never
 * be taken.
 */
static BOOL FAR PASCAL CropStopTracking(BOOL fErase)
{
    switch (gwCropState) {
    case CROP_SIZING:
    case CROP_MOVING:
        ReleaseCapture();
        /* fall through */
    case CROP_SHOWN:
        KillTimer(ghwndCrop, CROP_TIMER);
        ViewStopAutoScroll();
        if (fErase)
            CropDraw(TRUE, FALSE);
        break;

    case CROP_CLICKED:
        ReleaseCapture();
        break;
    }

    gwCropState = CROP_NONE;
    gwCropFlags &= ~(CROPF_XCOLLAPSED | CROPF_YCOLLAPSED);
    StatusSetMessage(NULL);
    return TRUE;
}

/*
 * s18:0128-0142  CropEnd  (FAR PASCAL, called from the Crop dialog when it
 * closes)
 *
 * Ghidra didn't find this function: it is reached only from segment 20.
 */
BOOL FAR PASCAL CropEnd(void)
{
    CropStopTracking(TRUE);
    gwCropFlags = 0;
    gwCropState = CROP_NONE;
    ghwndCrop = NULL;
    gwFrameUpdate = gwCropSaveUpdate;
    return TRUE;
}

/*
 * s18:0144-016C  CropGetRect  (FAR PASCAL, unreferenced)
 */
BOOL FAR PASCAL CropGetRect(RECT *prc)
{
    if (gwCropState != CROP_NONE && gwCropState != CROP_CLICKED) {
        CopyRect(prc, &grcCrop);
        return TRUE;
    }
    return FALSE;
}

/*
 * s18:016E-0234  CropSetRect  (FAR PASCAL, called from the Crop dialog)
 *
 * Shows a new rectangle (image coordinates), clipped to the frame, and
 * starts the ants.  Does nothing before CropStart.
 */
BOOL FAR PASCAL CropSetRect(RECT *prc)
{
    RECT rc;

    if (!(gwCropFlags & CROPF_ACTIVE))
        return FALSE;

    if (gwCropState != CROP_NONE)
        CropStopTracking(TRUE);

    CopyRect(&rc, prc);
    CropNormalizeRect(&rc);

    if (rc.right > (int)gcxMovie)
        rc.right = gcxMovie;
    else if (rc.left < 0)
        rc.left = 0;

    if (rc.bottom > (int)gcyMovie)
        rc.bottom = gcyMovie;
    else if (rc.top < 0)
        rc.top = 0;

    grcCrop = rc;
    grcCropDrawn = rc;
    gptCropGrab.x = 0;
    gptCropGrab.y = 0;
    giCropAnts = 0;
    CropDraw(FALSE, TRUE);

    gwCropState = CROP_SHOWN;
    SetTimer(ghwndCrop, CROP_TIMER, CROP_TIMERMS, NULL);
    ViewStopAutoScroll();
    CropShowRect(rc);
    return TRUE;
}

/*
 * s18:0236-02A8  CropBeginDrag  (FAR PASCAL)
 *
 * Mouse button down while the rectangle is shown.  Returns:
 *   1  sizing started (from any corner),
 *   4  outside the rectangle (also for a corner if wFlags & 2),
 *   5  inside the rectangle,
 *   6  moving started (frame band; band hits return 4 if wFlags & 1),
 *   7  nothing to do (not in CROP_SHOWN, or the drag couldn't start).
 */
static WORD FAR PASCAL CropBeginDrag(POINT pt, WORD wFlags)
{
    WORD wHit;

    if (gwCropState != CROP_SHOWN)
        return CROPHIT_NONE;

    wHit = CropHitTest(pt);

    if (wHit <= 3) {
        if (wFlags & 2)
            return CROPHIT_OUTSIDE;
        return CropBeginSize(pt, wHit) ? 1 : CROPHIT_NONE;
    }

    if (wHit == CROPHIT_BORDER) {
        if (wFlags & 1)
            return CROPHIT_OUTSIDE;
        return CropBeginMove(pt) ? CROPHIT_BORDER : CROPHIT_NONE;
    }

    return wHit;
}

/*
 * s18:02AA-02EE  CropNewRect  (FAR PASCAL)
 *
 * Starts a new, empty rectangle at pt (window coordinates) and sizes its
 * bottom right corner, as if the handle had been grabbed at its centre
 * (9 pixels out).
 */
static BOOL FAR PASCAL CropNewRect(POINT pt)
{
    RECT rc;

    rc.left = rc.right = pt.x;
    rc.top = rc.bottom = pt.y;
    FrameClientToImageRect(&rc);

    if (!CropSetRect(&rc))
        return FALSE;

    pt.x += 9;
    pt.y += 9;
    CropBeginSize(pt, 3);
    return TRUE;
}

/*
 * s18:02F0-032E  CropBeginClick  (FAR PASCAL)
 *
 * Button down away from the handles and the band: remember the point
 * (window coordinates) and wait for a drag.
 */
static BOOL FAR PASCAL CropBeginClick(POINT pt)
{
    if (!(gwCropFlags & CROPF_ACTIVE))
        return FALSE;

    gptCropGrab = pt;
    if (gwCropState != CROP_NONE)
        CropStopTracking(FALSE);

    gwCropState = CROP_CLICKED;
    ViewStopAutoScroll();
    SetCapture(ghwndCrop);
    return TRUE;
}

/*
 * s18:0330-03D6  CropBeginSize  (FAR PASCAL)
 *
 * gptCropGrab becomes the distance from the grab point to the corner, with
 * a sign that makes "pointer + gptCropGrab" the new edge.
 */
static BOOL FAR PASCAL CropBeginSize(POINT pt, WORD wCorner)
{
    int x;
    int y;

    gwCropCorner = wCorner;
    gwCropState = CROP_SIZING;
    grcCropStart = grcCrop;
    grcCropDrawn = grcCropStart;

    FrameClientToImage(&pt);

    x = (wCorner & 1) ? grcCrop.right : grcCrop.left;
    gptCropGrab.x = pt.x - x;
    y = (wCorner & 2) ? grcCrop.bottom : grcCrop.top;
    gptCropGrab.y = pt.y - y;
    CropSetGrabSigns(wCorner);

    SetCapture(ghwndCrop);
    CropShowRect(grcCrop);
    return TRUE;
}

/*
 * s18:03D8-0434  CropBeginMove  (FAR PASCAL)
 */
static BOOL FAR PASCAL CropBeginMove(POINT pt)
{
    gwCropState = CROP_MOVING;
    grcCropStart = grcCrop;
    grcCropDrawn = grcCropStart;
    gptCropGrab = pt;
    FrameClientToImage(&gptCropGrab);
    SetCapture(ghwndCrop);
    CropShowRect(grcCrop);
    return TRUE;
}

/*
 * s18:0436-0538  CropHitTest  (FAR PASCAL)
 *
 * pt is in window coordinates.  Returns the corner (0-3: bit 0 right,
 * bit 1 bottom) for a handle, or CROPHIT_BORDER / _INSIDE / _OUTSIDE.
 * The handles are tested even when they aren't drawn.
 */
static WORD FAR PASCAL CropHitTest(POINT pt)
{
    HRGN    hrgnTmp;
    HRGN    hrgnBand;
    HRGN    hrgnHandles;
    RECT    rc;
    WORD    wHit;

    if (gwCropState == CROP_NONE || gwCropState == CROP_CLICKED)
        return CROPHIT_OUTSIDE;

    hrgnTmp = CreateRectRgn(0, 0, 0, 0);
    hrgnBand = CreateRectRgn(0, 0, 0, 0);
    hrgnHandles = CreateRectRgn(0, 0, 0, 0);

    rc = grcCrop;
    FrameImageToClientRect(&rc);
    CropNormalizeRect(&rc);
    CropMakeFrameRgn(&rc, hrgnBand, hrgnTmp, FALSE);

    if (PtInRegion(hrgnBand, pt.x, pt.y)) {
        wHit = CROPHIT_BORDER;
    } else if (PtInRect(&rc, pt)) {
        wHit = CROPHIT_INSIDE;
    } else {
        CropAddHandleRgns(&rc, hrgnHandles, hrgnTmp);
        if (PtInRegion(hrgnHandles, pt.x, pt.y)) {
            wHit = 0;
            if (rc.right < pt.x)
                wHit = 1;
            if (rc.bottom < pt.y)
                wHit |= 2;
        } else {
            wHit = CROPHIT_OUTSIDE;
        }
    }

    DeleteObject(hrgnTmp);
    DeleteObject(hrgnBand);
    DeleteObject(hrgnHandles);
    return wHit;
}

/*
 * s18:053A-0569  CropSetGrabSigns  (FAR PASCAL)
 */
static void FAR PASCAL CropSetGrabSigns(WORD wCorner)
{
    if (gptCropGrab.x < 0)
        gptCropGrab.x = -gptCropGrab.x;
    if (gptCropGrab.y < 0)
        gptCropGrab.y = -gptCropGrab.y;
    if (wCorner & 1)
        gptCropGrab.x = -gptCropGrab.x;
    if (wCorner & 2)
        gptCropGrab.y = -gptCropGrab.y;
}

/*
 * s18:056C-05E2  CropAddHandleRgns  (NEAR PASCAL)
 *
 * Adds the four 8x8 handles to hrgn: 5 to 13 pixels outside each corner.
 */
static void NEAR PASCAL CropAddHandleRgns(RECT *prc, HRGN hrgn, HRGN hrgnTmp)
{
    int i;
    int dx;
    int dy;

    for (i = 0; i < 4; i++) {
        dx = (i & 1) ? prc->right - prc->left + 18 : 0;
        dy = (i & 2) ? prc->bottom - prc->top + 18 : 0;
        SetRectRgn(hrgnTmp, prc->left + dx - 13, prc->top + dy - 13,
                   prc->left + dx - 5, prc->top + dy - 5);
        CombineRgn(hrgn, hrgn, hrgnTmp, RGN_OR);
    }
}

/*
 * s18:05E4-0644  CropMakeFrameRgn  (NEAR PASCAL)
 *
 * hrgn = the 4 pixel band around *prc, plus the handles if fHandles.
 */
static void NEAR PASCAL CropMakeFrameRgn(RECT *prc, HRGN hrgn, HRGN hrgnTmp, BOOL fHandles)
{
    SetRectRgn(hrgn, prc->left - 4, prc->top - 4, prc->right + 4, prc->bottom + 4);
    SetRectRgn(hrgnTmp, prc->left, prc->top, prc->right, prc->bottom);
    CombineRgn(hrgn, hrgn, hrgnTmp, RGN_DIFF);

    if (fHandles)
        CropAddHandleRgns(prc, hrgn, hrgnTmp);
}

/*
 * s18:0646-0652  CropDraw  (FAR PASCAL)
 */
void FAR PASCAL CropDraw(BOOL fErase, BOOL fDraw)
{
    CropDrawRgn(fErase, fDraw);
}

/*
 * s18:0654-0867  CropDrawRgn  (NEAR PASCAL)
 *
 * fErase removes the frame drawn at grcCropDrawn by repainting the frame
 * image under it (without the part the new frame will cover, if fDraw);
 * fDraw draws the frame at grcCrop.  The old rectangle is also inflated by
 * 13 and converted to window coordinates before the repaint, but the
 * result is not used.
 */
static void NEAR PASCAL CropDrawRgn(BOOL fErase, BOOL fDraw)
{
    HRGN        hrgnOld;
    HRGN        hrgnNew;
    HRGN        hrgnTmp;
    HRGN        hrgnClip;
    HDC         hdc;
    RECT        rcNew;
    RECT        rcOld;
    HBRUSH      hbrOld;
    COLORREF    crText;
    COLORREF    crBk;

    hrgnOld = CreateRectRgn(0, 0, 0, 0);
    hrgnNew = CreateRectRgn(0, 0, 0, 0);
    hrgnTmp = CreateRectRgn(0, 0, 0, 0);
    hrgnClip = CreateRectRgn(0, 0, 0, 0);
    hdc = GetDC(ghwndCrop);

    if (fDraw) {
        rcNew = grcCrop;
        FrameImageToClientRect(&rcNew);
        CropMakeFrameRgn(&rcNew, hrgnNew, hrgnTmp, !(gwCropFlags & CROPF_NOHANDLES));
    }

    if (fErase) {
        SaveDC(hdc);
        rcOld = grcCropDrawn;
        FrameImageToClientRect(&rcOld);
        CropMakeFrameRgn(&rcOld, hrgnOld, hrgnTmp, !(gwCropFlags & CROPF_NOHANDLES));
        if (fDraw) {
            CombineRgn(hrgnClip, hrgnOld, hrgnNew, RGN_DIFF);
            SelectClipRgn(hdc, hrgnClip);
        } else {
            SelectClipRgn(hdc, hrgnOld);
        }

        rcOld = grcCropDrawn;
        InflateRect(&rcOld, 13, 13);
        FrameImageToClientRect(&rcOld);

        SetWindowOrg(hdc, gxScroll, gyScroll);
        PaintFrame(hdc, FALSE);
        RestoreDC(hdc, -1);
    }

    if (fDraw) {
        SetBrushOrg(hdc, giCropAnts, giCropAnts);
        UnrealizeObject(ghbrCrop);
        hbrOld = SelectObject(hdc, ghbrCrop);
        crText = SetTextColor(hdc, gcrCropFore);
        crBk = SetBkColor(hdc, gcrCropBack);
        SelectClipRgn(hdc, hrgnNew);
        PatBlt(hdc, rcNew.left - 13, rcNew.top - 13,
               rcNew.right - rcNew.left + 26, rcNew.bottom - rcNew.top + 26, PATCOPY);
        SelectObject(hdc, hbrOld);
        SetTextColor(hdc, crText);
        SetBkColor(hdc, crBk);
    }

    ReleaseDC(ghwndCrop, hdc);
    DeleteObject(hrgnOld);
    DeleteObject(hrgnNew);
    DeleteObject(hrgnTmp);
    DeleteObject(hrgnClip);
}

/*
 * s18:086A-09C2  CropSizeTo  (NEAR PASCAL)
 *
 * Moves the corner being sized to pt (window coordinates).  When the
 * rectangle has been dragged down to zero width (or height) the corner is
 * flipped to the other side once the pointer has passed the opposite
 * edge, so sizing continues through the rectangle.  The two axes differ:
 * the width collapses when it reaches 0, the height only when it would
 * become negative.
 */
static void NEAR PASCAL CropSizeTo(POINT pt)
{
    BOOL f;

    FrameClientToImage(&pt);

    /* x */
    f = TRUE;
    if (gwCropFlags & CROPF_XCOLLAPSED) {
        if (grcCrop.left - gptCropGrab.x >= pt.x)
            gwCropCorner &= ~1;
        else if (gptCropGrab.x + grcCrop.right <= pt.x)
            gwCropCorner |= 1;
        else
            f = FALSE;
        if (f) {
            gwCropFlags &= ~CROPF_XCOLLAPSED;
            CropSetGrabSigns(gwCropCorner);
        }
    }
    if (f) {
        if (gwCropCorner & 1)
            grcCrop.right = pt.x + gptCropGrab.x;
        else
            grcCrop.left = pt.x + gptCropGrab.x;

        if (grcCrop.right > (int)gcxMovie)
            grcCrop.right = gcxMovie;
        else if (grcCrop.left < 0)
            grcCrop.left = 0;

        if (grcCrop.right <= grcCrop.left) {
            if (gwCropCorner & 1)
                grcCrop.right = grcCrop.left;
            else
                grcCrop.left = grcCrop.right;
            if (gptCropGrab.x < 0)
                gptCropGrab.x = -gptCropGrab.x;
            gwCropFlags |= CROPF_XCOLLAPSED;
        }
    }

    /* y */
    f = TRUE;
    if (gwCropFlags & CROPF_YCOLLAPSED) {
        if (grcCrop.top - gptCropGrab.y >= pt.y)
            gwCropCorner &= ~2;
        else if (gptCropGrab.y + grcCrop.bottom <= pt.y)
            gwCropCorner |= 2;
        else
            f = FALSE;
        if (f) {
            gwCropFlags &= ~CROPF_YCOLLAPSED;
            CropSetGrabSigns(gwCropCorner);
        }
    }
    if (f) {
        if (gwCropCorner & 2)
            grcCrop.bottom = pt.y + gptCropGrab.y;
        else
            grcCrop.top = pt.y + gptCropGrab.y;

        if (grcCrop.bottom > (int)gcyMovie)
            grcCrop.bottom = gcyMovie;
        else if (grcCrop.top < 0)
            grcCrop.top = 0;

        if (grcCrop.top > grcCrop.bottom) {
            if (gwCropCorner & 2)
                grcCrop.bottom = grcCrop.top;
            else
                grcCrop.top = grcCrop.bottom;
            if (gptCropGrab.y < 0)
                gptCropGrab.y = -gptCropGrab.y;
            gwCropFlags |= CROPF_YCOLLAPSED;
        }
    }
}

/*
 * s18:09C4-09D8  CropTimer  (FAR PASCAL, called from s04 on WM_TIMER 0x400)
 *
 * Ghidra didn't find this function: it is reached only from segment 4.
 */
void FAR PASCAL CropTimer(void)
{
    giCropAnts = (giCropAnts + 1) % 8;
    CropDrawRgn(FALSE, TRUE);
}

/*
 * s18:09DA-0B54  CropMouseMove  (FAR PASCAL, from the Crop dialog's mouse
 * forwarding)
 *
 * pt is in window coordinates.  The view scrolls to follow the pointer
 * unless wFlags has 0x1000 (ViewAutoScroll gets the window coordinates,
 * not the converted point).  A click turns into a new rectangle when the
 * pointer has moved at least 3 pixels (squared distance >= 9).
 */
void FAR PASCAL CropMouseMove(POINT pt, WORD wFlags)
{
    POINT   ptImage;
    int     dx;
    int     dy;
    DWORD   dw;

    switch (gwCropState) {
    case CROP_SIZING:
        CropSizeTo(pt);
        break;

    case CROP_MOVING:
        ptImage = pt;
        FrameClientToImage(&ptImage);
        dy = ptImage.y - gptCropGrab.y;
        dx = ptImage.x - gptCropGrab.x;

        if (grcCropStart.left + dx < 0)
            dx = -grcCropStart.left;
        else if (grcCropStart.right + dx > (int)gcxMovie)
            dx = gcxMovie - grcCropStart.right;

        if (grcCropStart.top + dy < 0)
            dy = -grcCropStart.top;
        else if (grcCropStart.bottom + dy > (int)gcyMovie)
            dy = gcyMovie - grcCropStart.bottom;

        grcCrop = grcCropStart;
        OffsetRect(&grcCrop, dx, dy);
        break;

    case CROP_CLICKED:
        ptImage = pt;
        FrameClientToImage(&ptImage);
        CropShowPoint(ptImage);

        dy = gptCropGrab.y - pt.y;
        dx = gptCropGrab.x - pt.x;
        dw = (long)dy * dy + (long)dx * dx;
        if (dw >= 9) {
            ReleaseCapture();
            gwCropState = CROP_NONE;
            CropNewRect(gptCropGrab);
        }
        return;

    default:
        return;
    }

    if (!(wFlags & 0x1000))
        ViewAutoScroll(pt);

    giCropAnts = (giCropAnts + 1) % 8;
    CropDrawRgn(TRUE, TRUE);
    grcCropDrawn = grcCrop;
    CropShowRect(grcCrop);
}

/*
 * s18:0B56-0B8B  CropButtonUp  (FAR PASCAL)
 *
 * A click that never became a drag puts back the rectangle from before.
 */
void FAR PASCAL CropButtonUp(void)
{
    switch (gwCropState) {
    case CROP_SIZING:
    case CROP_MOVING:
        gwCropState = CROP_SHOWN;
        gwCropFlags &= ~(CROPF_XCOLLAPSED | CROPF_YCOLLAPSED);
        CropDrawRgn(FALSE, TRUE);
        ViewStopAutoScroll();
        ReleaseCapture();
        break;

    case CROP_CLICKED:
        CropSetRect(&grcCropSave);
        break;
    }
}

/*
 * s18:0B8C-0BF7  CropButtonDown  (FAR PASCAL)
 *
 * Left or right button (fRight is not used).  A press inside the
 * rectangle starts a new one just like a press outside it; only the band
 * moves the rectangle.
 */
void FAR PASCAL CropButtonDown(POINT pt, BOOL fRight)
{
    WORD wHit;

    if (gwCropState == CROP_NONE) {
        CropNewRect(pt);
        return;
    }

    if (gwCropState != CROP_SHOWN)
        return;

    wHit = CropBeginDrag(pt, 0);
    if (wHit == CROPHIT_OUTSIDE || wHit == CROPHIT_INSIDE) {
        grcCropSave = grcCrop;
        CropStopTracking(TRUE);
        CropBeginClick(pt);
    } else if (wHit == CROPHIT_NONE) {
        MessageBeep(0);
        CropStopTracking(TRUE);
    }
}
