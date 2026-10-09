/*
 * framewnd.c - VIDEDIT.EXE code segment 4, offsets 0C7A-1029: the frame
 * window.
 *
 * The frame window (ghwndFrame, class "ShowWindow") is the
 * child of the view that shows the current frame, zoomed by gwZoom / 2
 * and scrolled by (gxScroll, gyScroll).  Besides painting it is the window
 * that receives the waveOut and MCI notifications, the delayed clipboard
 * rendering messages, the crop rectangle's mouse input, and the
 * Workbench broadcast 0xB100 that another VidEdit instance sends when it
 * closes.
 *
 * Functions are in the order of the binary.
 */

#include "videdit.h"
#include "vemain.h"
#include "frames.h"
#include "preview.h"
#include "crop.h"
#include "croprect.h"
#include "edit.h"

#define ID_CROPTIMER        0x400       /* croprect.c's timer */
#define WM_WRKLOSTMEDID     0xB100      /* WrkBroadcastMessage from a closing instance */

/* initialised data (module data starts at DS:0218) */
char gszFrameClass[] = "ShowWindow";    /* DS:0230 */
                                        /* DS:023E "Got WM_DESTROYCLIPBOARD", not used */

/*
 * s04:0C7A-0CB3  FrameImageToClient  (FAR PASCAL)
 *
 * Image to frame window coordinates.  Bug: y is computed from the
 * already converted x, not from y.
 */
void FAR PASCAL FrameImageToClient(LPPOINT lppt)
{
    lppt->x = lppt->x * (int)gwZoom / 2;
    lppt->y = lppt->x * (int)gwZoom / 2;
    lppt->x -= gxScroll;
    lppt->y -= gyScroll;
}

/*
 * s04:0CB4-0CEB  FrameClientToImage  (FAR PASCAL)
 */
void FAR PASCAL FrameClientToImage(LPPOINT lppt)
{
    lppt->x += gxScroll;
    lppt->y += gyScroll;
    lppt->x = lppt->x * 2 / (int)gwZoom;
    lppt->y = lppt->y * 2 / (int)gwZoom;
}

/*
 * s04:0CEC-0D4B  FrameImageToClientRect  (FAR PASCAL)
 */
void FAR PASCAL FrameImageToClientRect(LPRECT lprc)
{
    lprc->left = lprc->left * (int)gwZoom / 2;
    lprc->right = lprc->right * (int)gwZoom / 2;
    lprc->top = lprc->top * (int)gwZoom / 2;
    lprc->bottom = lprc->bottom * (int)gwZoom / 2;
    lprc->left -= gxScroll;
    lprc->right -= gxScroll;
    lprc->top -= gyScroll;
    lprc->bottom -= gyScroll;
}

/*
 * s04:0D4C-0DA3  FrameClientToImageRect  (FAR PASCAL)
 */
void FAR PASCAL FrameClientToImageRect(LPRECT lprc)
{
    lprc->left += gxScroll;
    lprc->right += gxScroll;
    lprc->top += gyScroll;
    lprc->bottom += gyScroll;
    lprc->left = lprc->left * 2 / (int)gwZoom;
    lprc->right = lprc->right * 2 / (int)gwZoom;
    lprc->top = lprc->top * 2 / (int)gwZoom;
    lprc->bottom = lprc->bottom * 2 / (int)gwZoom;
}

/*
 * s04:0DA4-0E07  FrameRegisterClass  (FAR PASCAL)
 */
BOOL FAR PASCAL FrameRegisterClass(HINSTANCE hInst, HINSTANCE hPrev)
{
    WNDCLASS wc;

    if (!hPrev) {
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        wc.hIcon = NULL;
        wc.lpszMenuName = NULL;
        wc.lpszClassName = gszFrameClass;
        wc.hbrBackground = (HBRUSH)(COLOR_APPWORKSPACE + 1);
        wc.hInstance = hInst;
        wc.style = CS_HREDRAW | CS_VREDRAW | CS_BYTEALIGNCLIENT;
        wc.lpfnWndProc = FrameWndProc;
        wc.cbClsExtra = 0;
        wc.cbWndExtra = 0;
        if (!RegisterClass(&wc))
            return FALSE;
    }
    return TRUE;
}

/*
 * s04:0E08-0E25  FrameGetDC
 *
 * A DC of the frame window with the window origin at the scroll
 * position.
 */
HDC FAR FrameGetDC(void)
{
    HDC hdc;

    hdc = GetDC(ghwndFrame);
    SetWindowOrg(hdc, gxScroll, gyScroll);
    return hdc;
}

/*
 * s04:0E26-1029  FrameWndProc  (FAR PASCAL, loads DS from SS)
 *
 * gfPreviewDrawing is set while Play Preview shows the movie through MCI in this
 * window.
 */
LRESULT FAR PASCAL FrameWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    PAINTSTRUCT ps;
    HDC         hdc;
    UINT        u;

    switch (msg) {
    case WM_DESTROY:
        ghwndFrame = 0;
        break;

    case WM_SIZE:
        PreviewSetRect();
        break;

    case WM_PAINT:
        PreviewSetRect();
        hdc = BeginPaint(hwnd, &ps);
        if (gfPreviewDrawing) {
            PreviewPaint(hdc);
        } else {
            SetWindowOrg(hdc, gxScroll, gyScroll);
            PaintFrame(hdc, TRUE);
        }
        EndPaint(hwnd, &ps);
        break;

    case WM_ERASEBKGND:
        return 0L;

    case WM_TIMER:
        if (wParam == ID_CROPTIMER)
            CropTimer();
        break;

    case WM_MOUSEMOVE:
        if (GetCapture() == hwnd)
            CropMouseMessage(msg, wParam, lParam);
        break;

    case WM_LBUTTONDOWN:
        if (gfCropping) {
            SetCapture(hwnd);
            CropMouseMessage(msg, wParam, lParam);
        }
        break;

    case WM_LBUTTONUP:
        if (GetCapture() == hwnd) {
            ReleaseCapture();
            CropMouseMessage(msg, wParam, lParam);
        }
        break;

    case WM_RENDERFORMAT:
        if (wParam == CF_WAVE)
            WaveRenderClipboard();
        break;

    case WM_RENDERALLFORMATS:
        ClipboardMakeStatic();
        break;

    case WM_DESTROYCLIPBOARD:
        WaveDestroyClipboard();
        ReleaseClipboardMedids();
        break;

    case WM_QUERYNEWPALETTE:
        if (gfPreviewDrawing) {
            PreviewRealize();
            u = 1;
        } else {
            hdc = GetDC(hwnd);
            u = RealizeFramePalette(hdc);
            ReleaseDC(hwnd, hdc);
            if (u)
                InvalidateRect(hwnd, NULL, TRUE);
        }
        return (LONG)(int)u;

    case WM_PALETTECHANGED:
        InvalidateRect(hwnd, NULL, TRUE);
        break;

    case MM_MCINOTIFY:
        PreviewNotify(lParam, wParam);
        break;

    case MM_WOM_OPEN:
    case MM_WOM_CLOSE:
    case MM_WOM_DONE:
        WaveOutMessage(hwnd, msg, wParam, lParam);
        break;

    case WM_WRKLOSTMEDID:
        if (wParam != (WPARAM)ghInst)
            ReplaceLostMedid((MEDID)lParam);
        break;
    }

    return DefWindowProc(hwnd, msg, wParam, lParam);
}
