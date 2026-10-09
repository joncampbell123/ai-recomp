/*
 * goto.c - code segment 14 of VIDEDIT.EXE (0x01E0 bytes): the Go To
 * dialog (IDD_GOTO), Edit Go To.
 *
 * The position is typed into an "aviframebox" control (segment 5),
 * which shows frames or times.  Each change restarts a 1-second timer;
 * when it fires (or OK is pressed, or the control loses the focus) the
 * text is checked: a number from 0 to 0x7FF0 is kept, anything else
 * beeps and selects the text.
 */

#include "videdit.h"
#include "vemain.h"
#include "vedwnd.h"

WORD    gwGoToFrame;                    /* DS:21CA result */
WORD    gwGoToEdit;                     /* DS:21CC value being edited */
WORD    gidGoToTimer;                   /* DS:21CE timer (= the control id) or 0 */
BOOL    gfGoToInit;                     /* DS:21D0 ignore EN_ notifications */

/*
 * s14:0000-0042  GoTo
 */
void FAR GoTo(void)
{
    FARPROC lpfn;
    int     n;

    lpfn = MakeProcInstance((FARPROC)GoToDlgProc, ghInst);
    n = DoDialog(IDD_GOTO, ghwndApp, lpfn);
    FreeProcInstance(lpfn);
    if (n)
        SeekTo(gwGoToFrame);
}

/*
 * s14:0044-01DE  GoToDlgProc  (FAR PASCAL, not exported; loads DS from
 * SS; returns a LONG)
 */
LONG FAR PASCAL GoToDlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
    char    ach[80];
    BOOL    fOK;
    int     n;

    switch (msg) {
    case WM_INITDIALOG:
        LoadString(ghInst, gfTimeFormat ? IDS_GOTOTIME : IDS_GOTOFRAME, ach, sizeof(ach));
        SetWindowText(hDlg, ach);
        gfGoToInit = TRUE;
        gwGoToEdit = gwCurFrame;
        SetDlgItemInt(hDlg, IDC_GOTO_FRAME, gwGoToEdit, FALSE);
        gfGoToInit = FALSE;
        gidGoToTimer = 0;
        return TRUE;

    case WM_COMMAND:
        switch (wParam) {
        case IDOK:
            if (gidGoToTimer)
                SendMessage(hDlg, WM_TIMER, gidGoToTimer, 0L);
            gwGoToFrame = gwGoToEdit;
            EndDialog(hDlg, TRUE);
            break;

        case IDCANCEL:
            if (gidGoToTimer)
                KillTimer(hDlg, gidGoToTimer);
            EndDialog(hDlg, FALSE);
            break;

        case IDC_GOTO_FRAME:
            if (gfGoToInit)
                break;
            if (HIWORD(lParam) == EN_KILLFOCUS) {
                if (gidGoToTimer)
                    SendMessage(hDlg, WM_TIMER, gidGoToTimer, 0L);
            } else if (HIWORD(lParam) == EN_CHANGE) {
                if (gidGoToTimer)
                    KillTimer(hDlg, gidGoToTimer);
                gidGoToTimer = wParam;
                if (!SetTimer(hDlg, gidGoToTimer, 1000, NULL))
                    SendMessage(hDlg, WM_TIMER, gidGoToTimer, 0L);
            }
            break;
        }
        return TRUE;

    case WM_TIMER:
        KillTimer(hDlg, gidGoToTimer);
        n = GetDlgItemInt(hDlg, gidGoToTimer, &fOK, TRUE);
        if (fOK && n >= 0 && n <= 0x7FF0) {
            gwGoToEdit = n;
        } else {
            MessageBeep(0);
            SendDlgItemMessage(hDlg, gidGoToTimer, EM_SETSEL, 0, MAKELONG(0, 0x7FFF));
        }
        gidGoToTimer = 0;
        return TRUE;
    }
    return FALSE;
}
