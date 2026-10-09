/*
 * prefs.c - code segment 9 of VIDEDIT.EXE (0x024F bytes): the
 * Preferences dialog (IDD_PREFERENCES), Edit Preferences.
 *
 * The settings are saved to MMTOOLS.INI as soon as OK is pressed.
 */

#include "videdit.h"
#include "vemain.h"
#include "vedwnd.h"
#include "edit.h"

WORD    gidBackgroundDlg;               /* DS:1CBA the radio button chosen */

/*
 * s09:0000-0038  Preferences  (FAR PASCAL)
 */
int FAR PASCAL Preferences(HWND hwnd)
{
    FARPROC lpfn;
    int     n;

    lpfn = MakeProcInstance((FARPROC)PrefsDlgProc, ghInst);
    n = DoDialog(IDD_PREFERENCES, hwnd, lpfn);
    FreeProcInstance(lpfn);
    return n;
}

/*
 * s09:003A-024E  PrefsDlgProc  (FAR PASCAL, not exported; loads DS from
 * SS)
 *
 * "Overwrite Mode" is the inverse of gfInsertMode.  Changing the time
 * format resizes the status bar and repaints two of its fields.
 */
BOOL FAR PASCAL PrefsDlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
    char    ach[40];
    BOOL    f;
    BOOL    fToolBar;
    BOOL    fStatusBar;

    switch (msg) {
    case WM_INITDIALOG:
        CheckDlgButton(hDlg, IDC_PREF_CENTER, gfCenterImage);
        CheckDlgButton(hDlg, IDC_PREF_RESIZE, gfResizeOnOpen);
        CheckDlgButton(hDlg, IDC_PREF_FRAMENUM, gfFrameNumInTitle);
        CheckDlgButton(hDlg, IDC_PREF_TIMEFORMAT, gfTimeFormat);
        CheckDlgButton(hDlg, IDC_PREF_STATUSBAR, gfStatusBar);
        CheckDlgButton(hDlg, IDC_PREF_TOOLBAR, gfToolBar);
        CheckDlgButton(hDlg, IDC_PREF_OVERWRITE, gfInsertMode == 0);
        SetDlgItemInt(hDlg, IDC_PREF_CACHE, gwCacheKB, FALSE);
        gidBackgroundDlg = gidBackground;
        CheckRadioButton(hDlg, IDC_PREF_BKDEFAULT, IDC_PREF_BKBLACK, gidBackgroundDlg);
        return TRUE;

    case WM_COMMAND:
        switch (wParam) {
        case IDOK:
            f = IsDlgButtonChecked(hDlg, IDC_PREF_CENTER);
            if (gfCenterImage != f) {
                gfCenterImage = f;
                InvalidateRect(ghwndFrame, NULL, FALSE);
                ViewLayout(FALSE);
            }
            gfResizeOnOpen = IsDlgButtonChecked(hDlg, IDC_PREF_RESIZE);
            f = IsDlgButtonChecked(hDlg, IDC_PREF_FRAMENUM);
            if (gfFrameNumInTitle != f) {
                gfFrameNumInTitle = f;
                UpdateTitle();
            }
            fToolBar = IsDlgButtonChecked(hDlg, IDC_PREF_TOOLBAR);
            fStatusBar = IsDlgButtonChecked(hDlg, IDC_PREF_STATUSBAR);
            AppShowBars(fToolBar, fStatusBar);
            if (gidBackground != gidBackgroundDlg) {
                gidBackground = gidBackgroundDlg;
                InvalidateRect(ghwndFrame, NULL, FALSE);
                ViewLayout(FALSE);
            }
            f = IsDlgButtonChecked(hDlg, IDC_PREF_TIMEFORMAT);
            if (gfTimeFormat != f) {
                gfTimeFormat = f;
                StatusUpdatePosition();
                SendMessage(ghwndStatus, WM_SIZE, 0, 0L);
                InvalidateRect(ghwndMarkInValue, NULL, FALSE);
                InvalidateRect(ghwndMarkOutValue, NULL, FALSE);
            }
            f = IsDlgButtonChecked(hDlg, IDC_PREF_OVERWRITE) == 0;
            if (gfInsertMode != f) {
                gfInsertMode = f;
                LoadString(ghInst, f ? IDS_INS : IDS_OVR, ach, sizeof(ach));
                SetWindowText(ghwndStatMode, ach);
            }
            gwCacheKB = GetDlgItemInt(hDlg, IDC_PREF_CACHE, NULL, FALSE);
            SaveSettings();
            EndDialog(hDlg, TRUE);
            break;

        case IDCANCEL:
            EndDialog(hDlg, FALSE);
            break;

        case IDC_PREF_BKDEFAULT:
        case IDC_PREF_BKLTGRAY:
        case IDC_PREF_BKDKGRAY:
        case IDC_PREF_BKBLACK:
            gidBackgroundDlg = wParam;
            break;

        default:
            return FALSE;
        }
        return TRUE;
    }
    return FALSE;
}
