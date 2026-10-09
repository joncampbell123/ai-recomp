/*
 * framebox.c - the "aviframebox" control: code segment 5, s05:62A4-6970
 *
 * An edit field with spin arrows for a frame number, used by the Go To,
 * Set Selection and Synchronize dialogs.  With the time format preference
 * the field shows and accepts "hh:mm:ss.cc"; WM_GETTEXT always answers
 * with a frame number.  Edit notifications go to the parent under the
 * control's own ID, and the arrows send a faked EN_KILLFOCUS so that the
 * dialog updates.  The module's data is DS:12E4-1335.
 *
 * Window words (cbWndExtra 6): 0 the edit control, 2 the arrows, 4 the
 * largest frame number (0x7FF0, set with FBM_SETMAX).
 */

#include "videdit.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "vedwnd.h"

#define IDC_FB_EDIT     5000
#define IDC_FB_ARROW    5001

static LONG NEAR PASCAL FrameBoxSetTextIfChanged(HWND hwnd, LPSTR lpsz);
static LONG NEAR PASCAL FrameBoxGetText(HWND hwnd, WORD cb, LPSTR lpBuf);
static LONG NEAR PASCAL FrameBoxSpin(HWND hwnd, WORD code, LPARAM lParam);

/*
 * s05:62A4-6305  FrameBoxInit  (FAR PASCAL)
 *
 * A global class.  hInst is not used (the class gets the application's).
 */
BOOL FAR PASCAL FrameBoxInit(HINSTANCE hInst, HINSTANCE hPrev)
{
    WNDCLASS wc;

    if (!hPrev) {
        wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
        wc.hIcon         = NULL;
        wc.lpszMenuName  = NULL;
        wc.lpszClassName = "aviframebox";
        wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
        wc.hInstance     = ghInst;
        wc.style         = CS_GLOBALCLASS | CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc   = FrameBoxWndProc;
        wc.cbClsExtra    = 0;
        wc.cbWndExtra    = 6;
        if (!RegisterClass(&wc))
            return FALSE;
    }
    return TRUE;
}

/*
 * s05:6308-63CD  FormatFrameTime  (FAR PASCAL)
 *
 * A frame number as "hh:mm:ss.cc" (gCompOptions.dwUSecPerFrame microseconds per frame).  The
 * seconds and the hundredths are each rounded, so the hundredths can come
 * out negative and borrow a second.
 */
int FAR PASCAL FormatFrameTime(WORD wFrame, LPSTR lpsz)
{
    LONG    lSec, lCenti;
    UINT    uHours, uMinutes, uSeconds;

    lSec = muldiv32((LONG)wFrame, gCompOptions.dwUSecPerFrame, 1000000L);
    lCenti = muldiv32((LONG)wFrame, gCompOptions.dwUSecPerFrame, 10000L) - lSec * 100;
    if (lCenti < 0) {
        lSec--;
        lCenti += 100;
    }
    uHours = (UINT)((DWORD)lSec / 3600);
    lSec -= uHours * 3600;
    uMinutes = (UINT)((DWORD)lSec / 60);
    uSeconds = (UINT)lSec - uMinutes * 60;
    return wsprintf(lpsz, "%02u:%02u:%02u.%02lu", uHours, uMinutes, uSeconds, lCenti);
}

/*
 * s05:63D0-642F  FrameBoxSetText  (FAR PASCAL)
 *
 * Sets the edit control from a frame number, converted to a time with the
 * time format preference.
 */
LONG FAR PASCAL FrameBoxSetText(HWND hwndEdit, LPSTR lpsz)
{
    char ach[16];

    if (gfTimeFormat && *lpsz) {
        lstrcpy(ach, lpsz);
        FormatFrameTime(atoi(ach), ach);
        return SendMessage(hwndEdit, WM_SETTEXT, 0, (LPARAM)(LPSTR)ach);
    }
    return SendMessage(hwndEdit, WM_SETTEXT, 0, (LPARAM)lpsz);
}

/*
 * s05:6432-661C  FrameBoxWndProc  (window procedure of "aviframebox")
 */
LRESULT FAR PASCAL FrameBoxWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    RECT    rc;
    HWND    hwndEdit, hwndArrow;
    int     cxArrow;

    switch (msg) {
    case WM_CREATE:
        GetClientRect(hwnd, &rc);
        cxArrow = ((LOWORD(GetDialogBaseUnits()) * 6) >> 2) - 1;
        hwndEdit = CreateWindow("edit", "", WS_CHILD | WS_BORDER | WS_TABSTOP,
                                0, 0, rc.right - cxArrow, rc.bottom, hwnd, (HMENU)IDC_FB_EDIT,
                                (HINSTANCE)GetWindowWord(hwnd, GWW_HINSTANCE), NULL);
        if (hwndEdit == NULL)
            return 0L;
        SetWindowWord(hwnd, 0, (WORD)hwndEdit);
        SendMessage(hwndEdit, EM_LIMITTEXT, 12, 0L);
        ShowWindow(hwndEdit, SW_SHOW);

        hwndArrow = CreateWindow("comarrow", "", WS_CHILD | WS_BORDER | WS_TABSTOP,
                                 rc.right - cxArrow, 0, cxArrow, rc.bottom, hwnd, (HMENU)IDC_FB_ARROW,
                                 (HINSTANCE)GetWindowWord(hwnd, GWW_HINSTANCE), NULL);
        if (hwndArrow == NULL)
            return 0L;
        SetWindowWord(hwnd, 2, (WORD)hwndArrow);
        ShowWindow(hwndArrow, SW_SHOW);
        SetWindowWord(hwnd, 4, 0x7FF0);
        return 0L;

    case WM_DESTROY:
        DestroyWindow((HWND)GetWindowWord(hwnd, 0));
        DestroyWindow((HWND)GetWindowWord(hwnd, 2));
        return 0L;

    case WM_SETFOCUS:
        SetFocus((HWND)GetWindowWord(hwnd, 0));
        return 0L;

    case WM_SETTEXT:
        return FrameBoxSetTextIfChanged(hwnd, (LPSTR)lParam);

    case WM_GETTEXT:
        return FrameBoxGetText(hwnd, wParam, (LPSTR)lParam);

    case WM_SETFONT:
    case EM_SETSEL:
        SendMessage((HWND)GetWindowWord(hwnd, 0), msg, wParam, lParam);
        return 0L;

    case WM_COMMAND:
        if (wParam == IDC_FB_EDIT)
            SendMessage(GetParent(hwnd), WM_COMMAND, GetWindowWord(hwnd, GWW_ID), lParam);
        return 0L;

    case WM_VSCROLL:
        return FrameBoxSpin(hwnd, wParam, lParam);

    case FBM_SETMAX:
        SetWindowWord(hwnd, 4, wParam);
        return 0L;
    }

    return DefWindowProc(hwnd, msg, wParam, lParam);
}

/*
 * s05:6620-6689  FrameBoxSetTextIfChanged  (NEAR PASCAL)
 *
 * WM_SETTEXT: only replaces the text if the frame number differs, then
 * selects it.
 */
static LONG NEAR PASCAL FrameBoxSetTextIfChanged(HWND hwnd, LPSTR lpsz)
{
    char    ach[12];
    LONG    l;

    l = FrameBoxGetText(hwnd, sizeof(ach), ach);
    if (lstrcmp(ach, lpsz) != 0)
        l = FrameBoxSetText((HWND)GetWindowWord(hwnd, 0), lpsz);
    SendMessage((HWND)GetWindowWord(hwnd, 0), EM_SETSEL, 0, MAKELONG(0x7FFF, 0));
    return l;
}

/*
 * s05:668C-6891  FrameBoxGetText  (NEAR PASCAL)
 *
 * WM_GETTEXT: the frame number as text.  A time "[[h:]m:]s[.cc]" is
 * converted with the frame time; only digits, ':' and '.' are allowed
 * (-1 otherwise, or when empty).  The original tests strchr and strrchr
 * results by reading the byte they point to, so a NULL result reads DS:0
 * (which is 0 in VidEdit); here NULL is tested directly.
 */
static LONG NEAR PASCAL FrameBoxGetText(HWND hwnd, WORD cb, LPSTR lpBuf)
{
    char    ach[12];
    char    achOut[12];
    char   *p, *q;
    LONG    lHours = 0, lMinutes = 0, lSeconds;
    WORD    wCenti = 0;
    int     n;

    if (!gfTimeFormat)
        return SendMessage((HWND)GetWindowWord(hwnd, 0), WM_GETTEXT, cb, (LPARAM)lpBuf);

    SendMessage((HWND)GetWindowWord(hwnd, 0), WM_GETTEXT, sizeof(ach), (LPARAM)(LPSTR)ach);
    if (ach[0] == '\0')
        return -1L;
    for (p = ach; *p; p++)
        if (!isdigit(*p) && *p != '.' && *p != ':')
            return -1L;

    p = strchr(ach, '.');
    if (p != NULL) {
        q = strrchr(ach, '.');
        if (q != p)
            return -1L;
        q = p + 1;
        if (strlen(q) > 2)
            q[2] = '\0';
        wCenti = atoi(q);
        if (lstrlen(q) == 1)
            wCenti *= 10;
        *p = '\0';
    }

    p = strrchr(ach, ':');
    lSeconds = atoi(p != NULL ? p + 1 : ach);
    if (p != NULL) {
        *p = '\0';
        p = strrchr(ach, ':');
        lMinutes = atoi(p != NULL ? p + 1 : ach);
        if (p != NULL) {
            *p = '\0';
            lHours = atoi(ach);
        }
    }

    sprintf(achOut, "%d",
            (int)muldiv32(1L, (((lHours * 60 + lMinutes) * 60 + lSeconds) * 100 + wCenti) * 10000L,
                          gCompOptions.dwUSecPerFrame));
    n = strlen(achOut);
    lstrcpy(lpBuf, achOut);
    return (LONG)n;
}

/*
 * s05:6894-6970  FrameBoxSpin  (NEAR PASCAL)
 *
 * WM_VSCROLL from the arrows: one frame up or down within 0..max.  The
 * WM_NEXTDLGCTL meant to select the edit field is sent to the control
 * itself.  Always tells the parent with a faked EN_KILLFOCUS.
 */
static LONG NEAR PASCAL FrameBoxSpin(HWND hwnd, WORD code, LPARAM lParam)
{
    char    ach[12];
    int     n;

    FrameBoxGetText(hwnd, sizeof(ach), ach);
    n = atoi(ach);

    if (code == SB_LINEUP) {
        if (n < -1 || (WORD)GetWindowWord(hwnd, 4) <= (WORD)n)
            goto beep;
        n++;
        wsprintf(ach, "%d", n);
    } else if (code == SB_LINEDOWN) {
        if (n <= 0 || (WORD)(GetWindowWord(hwnd, 4) + 1) < (WORD)n)
            goto beep;
        n--;
        wsprintf(ach, "%d", n);
    } else {
        goto notify;
    }

    SendMessage(hwnd, WM_NEXTDLGCTL, GetWindowWord(hwnd, 0), 1L);
    FrameBoxSetText((HWND)GetWindowWord(hwnd, 0), ach);
    SendMessage((HWND)GetWindowWord(hwnd, 0), EM_SETSEL, 0, MAKELONG(0x7FFF, 0));
    goto notify;

beep:
    MessageBeep(0);
notify:
    SendMessage(GetParent(hwnd), WM_COMMAND, GetWindowWord(hwnd, GWW_ID), MAKELONG(0, EN_KILLFOCUS));
    return (LONG)(WORD)n;
}
