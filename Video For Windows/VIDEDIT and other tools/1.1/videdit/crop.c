/*
 * crop.c - code segment 20 of VIDEDIT.EXE (0x05ED bytes): Video/Crop.
 *
 * The Crop Video dialog (IDD_CROP) is modal, but it enables the main
 * window again so that the crop rectangle can be dragged in the frame
 * window (the tracker is croprect.c, segment 18).  The X/Y/Width/Height
 * edits and the rectangle follow each other: typing in an edit starts a
 * one-second timer after which the value is checked and the rectangle
 * moved; dragging the rectangle calls CropShowRect.
 *
 * Data: DGROUP module at DS:1172-118D (initialised) and DS:2A7C-2A87.
 * Functions are in the order of the binary.
 */

#include "videdit.h"
#include "crop.h"

/* dialog controls */
#define IDC_CROPX               190
#define IDC_CROPY               191
#define IDC_CROPWIDTH           192
#define IDC_CROPHEIGHT          193
#define IDC_CROPXARROW          180     /* arrows are their edit's ID - 10 */

/* string table */
#define IDS_CAPTION             126     /* "VidEdit" */
#define IDS_BADNUMBER           201     /* "Bad numeric value." */

/* initialised data, DS:1172-118D (after the module's "AnimSTrackBar",
   "GrayBlob" header strings) */
WORD        gfCropping          = 0;    /* DS:118A Crop dialog is up (read by segment 5) */
WORD        gfCropSetting       = 0;    /* DS:118C CropShowRect is filling in the edits */

/* uninitialised data */
WORD        gxCrop;                     /* DS:2A7C the crop rectangle */
WORD        gyCrop;                     /* DS:2A7E */
WORD        gcxCrop;                    /* DS:2A80 */
WORD        gcyCrop;                    /* DS:2A82 */
WORD        gidCropEdit;                /* DS:2A84 edit whose timer is running, also the timer ID */
HWND        ghdlgCrop;                  /* DS:2A86 */

/*
 * s20:0000-0095  CropDrawFrame  (NEAR)
 *
 * Inverts a two-pixel frame around the crop rectangle in the frame
 * window.  Only called after an invalid value on OK, where it leaves an
 * inverted frame behind.
 */
static void NEAR CropDrawFrame(void)
{
    HDC hdc;

    hdc = FrameGetDC();
    PatBlt(hdc, gxCrop - 2, gyCrop - 2, 2, gcyCrop + 4, DSTINVERT);
    PatBlt(hdc, gxCrop, gyCrop - 2, gcxCrop, 2, DSTINVERT);
    PatBlt(hdc, gcxCrop + gxCrop, gyCrop - 2, 2, gcyCrop + 4, DSTINVERT);
    PatBlt(hdc, gxCrop, gcyCrop + gyCrop, gcxCrop, 2, DSTINVERT);
    ReleaseDC(ghwndFrame, hdc);
}

/*
 * s20:0096-046F  CropDlgProc  (FAR PASCAL, loads DS from SS, not exported)
 *
 * Quirks of the original:
 *  - When a typed value is rejected the old value is put back in the
 *    edit, but the variable keeps the rejected value (0 for something
 *    that isn't a number), and the timer isn't cleared.
 *  - The X and Y checks on the timer allow up to width - 1 and
 *    height - 1 without looking at the width and height of the
 *    rectangle; OK checks x + width and y + height.
 */
BOOL FAR PASCAL _loadds CropDlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
    RECT    rc;
    BOOL    fOK;
    UINT    id;
    WORD    wOld;
    WORD    wMax;

    switch (msg) {
    case WM_INITDIALOG:
        ghdlgCrop = hDlg;
        EnableWindow(ghwndApp, TRUE);
        gxCrop = 0;
        SetDlgItemInt(hDlg, IDC_CROPX, 0, TRUE);
        gyCrop = 0;
        SetDlgItemInt(hDlg, IDC_CROPY, 0, TRUE);
        SetDlgItemInt(hDlg, IDC_CROPWIDTH, gcxCrop = gcxMovie, FALSE);
        SetDlgItemInt(hDlg, IDC_CROPHEIGHT, gcyCrop = gcyMovie, FALSE);
        CropStart(ghwndFrame, 1);
        rc.left = gxCrop;
        rc.right = gxCrop + gcxCrop;
        rc.top = gyCrop;
        rc.bottom = gyCrop + gcyCrop;
        CropSetRect(&rc);
        gidCropEdit = 0;
        break;

    case WM_COMMAND:
        id = wParam;
        switch (wParam) {
        case IDOK:
            if (gidCropEdit)
                SendMessage(hDlg, WM_TIMER, gidCropEdit, 0L);
            id = IDC_CROPX;
            gxCrop = GetDlgItemInt(hDlg, id, &fOK, FALSE);
            if (fOK) {
                id = IDC_CROPY;
                gyCrop = GetDlgItemInt(hDlg, id, &fOK, FALSE);
            }
            if (fOK) {
                id = IDC_CROPWIDTH;
                gcxCrop = GetDlgItemInt(hDlg, id, &fOK, FALSE);
                if (fOK && (gcxCrop == 0 || gxCrop + gcxCrop > gcxMovie))
                    fOK = FALSE;
            }
            if (fOK) {
                id = IDC_CROPHEIGHT;
                gcyCrop = GetDlgItemInt(hDlg, id, &fOK, FALSE);
                if (fOK && (gcyCrop == 0 || gyCrop + gcyCrop > gcyMovie))
                    fOK = FALSE;
            }
            if (!fOK) {
                MessageBeep(0);
                ErrorResBox(hDlg, ghInst, MB_ICONEXCLAMATION, IDS_CAPTION, IDS_BADNUMBER);
                SendDlgItemMessage(hDlg, id, EM_SETSEL, 0, MAKELONG(0, 0x7FFF));
                CropDrawFrame();
                break;
            }
            CropEnd();
            EndDialog(hDlg, TRUE);
            break;

        case IDCANCEL:
            CropEnd();
            EndDialog(hDlg, FALSE);
            break;

        case IDC_CROPX:
        case IDC_CROPY:
        case IDC_CROPWIDTH:
        case IDC_CROPHEIGHT:
            if (gfCropSetting)
                break;
            if (HIWORD(lParam) == EN_KILLFOCUS)
                SendMessage(hDlg, WM_TIMER, gidCropEdit, 0L);
            if (HIWORD(lParam) == EN_CHANGE) {
                KillTimer(hDlg, gidCropEdit);
                gidCropEdit = id;
                if (!SetTimer(hDlg, id, 1000, NULL))
                    SendMessage(hDlg, WM_TIMER, gidCropEdit, 0L);
            }
            break;

        default:
            return FALSE;
        }
        break;

    case WM_TIMER:
        if (wParam != gidCropEdit)
            break;
        KillTimer(hDlg, gidCropEdit);
        switch (gidCropEdit) {
        case IDC_CROPX:
            wOld = gxCrop;
            gxCrop = GetDlgItemInt(hDlg, IDC_CROPX, &fOK, FALSE);
            if (!fOK || gcxMovie - 1 < gxCrop)
                goto bad;
            break;
        case IDC_CROPY:
            wOld = gyCrop;
            gyCrop = GetDlgItemInt(hDlg, IDC_CROPY, &fOK, FALSE);
            if (!fOK || gcyMovie - 1 < gyCrop)
                goto bad;
            break;
        case IDC_CROPWIDTH:
            wOld = gcxCrop;
            gcxCrop = GetDlgItemInt(hDlg, IDC_CROPWIDTH, &fOK, FALSE);
            if (!fOK || gcxMovie - gxCrop < gcxCrop)
                goto bad;
            break;
        case IDC_CROPHEIGHT:
            wOld = gcyCrop;
            gcyCrop = GetDlgItemInt(hDlg, IDC_CROPHEIGHT, &fOK, FALSE);
            if (!fOK || gcyMovie - gyCrop < gcyCrop)
                goto bad;
            break;
        }
        rc.left = gxCrop;
        rc.right = gxCrop + gcxCrop;
        rc.top = gyCrop;
        rc.bottom = gyCrop + gcyCrop;
        gidCropEdit = 0;
        CropSetRect(&rc);
        break;
    bad:
        MessageBeep(0);
        SetDlgItemInt(hDlg, gidCropEdit, wOld, FALSE);
        SendDlgItemMessage(hDlg, gidCropEdit, EM_SETSEL, 0, MAKELONG(0, 0x7FFF));
        break;

    case WM_VSCROLL:
        id = GetWindowWord(HIWORD(lParam), GWW_ID) + (IDC_CROPX - IDC_CROPXARROW);
        if (id == IDC_CROPX)
            wMax = gcxMovie - 1;
        else if (id == IDC_CROPY)
            wMax = gcyMovie - 1;
        else if (id == IDC_CROPWIDTH)
            wMax = gcxMovie - gxCrop;
        else
            wMax = gcyMovie - gyCrop;
        ArrowEditStep(GetDlgItem(hDlg, id), wParam, 0L, wMax, 1);
        break;

    default:
        return FALSE;
    }
    return TRUE;
}

/*
 * s20:0472-04CF  CropMouseMessage  (FAR PASCAL)
 *
 * Mouse messages of the frame window while cropping, passed on to the
 * tracker.
 */
void FAR PASCAL CropMouseMessage(UINT msg, WPARAM wParam, LPARAM lParam)
{
    POINT pt;

    pt = MAKEPOINT(lParam);
    switch (msg) {
    case WM_MOUSEMOVE:
        CropMouseMove(pt, wParam);
        break;
    case WM_LBUTTONDOWN:
    case WM_RBUTTONDOWN:
        CropButtonDown(pt, msg == WM_RBUTTONDOWN);
        break;
    case WM_LBUTTONUP:
    case WM_RBUTTONUP:
        CropButtonUp();
        break;
    }
}

/*
 * s20:04D0-055F  DoCrop
 *
 * Video/Crop.  Crops all frames if the rectangle isn't the whole frame.
 */
void FAR DoCrop(void)
{
    FARPROC lpfn;
    int     fOK;

    gfCropping = 1;
    lpfn = MakeProcInstance((FARPROC)CropDlgProc, ghInst);
    fOK = DoDialog(IDD_CROP, ghwndApp, lpfn);
    FreeProcInstance(lpfn);
    gfCropping = 0;

    if (fOK && (gxCrop || gyCrop || gcxCrop != gcxMovie || gcyCrop != gcyMovie)) {
        SetUndo(IDM_CROP);
        CropFrames(gxCrop, gyCrop, gcxCrop, gcyCrop);
        SetFrameSize(gcxCrop, gcyCrop);
    }
}

/*
 * s20:0560-0562  CropShowPoint  (FAR PASCAL)
 *
 * Called by the tracker with the mouse position; does nothing.
 */
void FAR PASCAL CropShowPoint(POINT pt)
{
}

/*
 * s20:0564-05EC  CropShowRect  (FAR PASCAL)
 *
 * Called by the tracker when the rectangle changes: fills in the edits
 * that differ (X and Y signed, width and height unsigned).
 */
void FAR PASCAL CropShowRect(RECT rc)
{
    gfCropSetting = 1;
    if (rc.left != gxCrop) {
        gxCrop = rc.left;
        SetDlgItemInt(ghdlgCrop, IDC_CROPX, gxCrop, TRUE);
    }
    if (rc.top != gyCrop) {
        gyCrop = rc.top;
        SetDlgItemInt(ghdlgCrop, IDC_CROPY, gyCrop, TRUE);
    }
    if (rc.right - rc.left != gcxCrop) {
        gcxCrop = rc.right - rc.left;
        SetDlgItemInt(ghdlgCrop, IDC_CROPWIDTH, gcxCrop, FALSE);
    }
    if (rc.bottom - rc.top != gcyCrop) {
        gcyCrop = rc.bottom - rc.top;
        SetDlgItemInt(ghdlgCrop, IDC_CROPHEIGHT, gcyCrop, FALSE);
    }
    gfCropSetting = 0;
}
