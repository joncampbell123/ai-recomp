/*
 * help.c - code segment 2 of VIDCAP.EXE, 07FA-0953: F1 help.
 *
 * A WH_MSGFILTER hook catches F1 in dialog boxes and menus: with a help
 * context (the ID of the dialog that is up) it opens VIDCAP.HLP at that
 * topic, otherwise it does Help/Contents.
 */

#include <windows.h>
#include "vidcap.h"

char        gachHelpFile[80];           /* DS:263C */

/*
 * seg2:07FA-0868  HelpMsgFilter  (exported ordinal 8, _loadds in the Watcom build)
 */
DWORD FAR PASCAL _loadds HelpMsgFilter(int nCode, WORD wParam, LPMSG lpMsg)
{
    if (nCode >= 0 && lpMsg->message == WM_KEYDOWN && lpMsg->wParam == VK_F1) {
        if (gidHelpContext)
            WinHelp(ghwndApp, gachHelpFile, HELP_CONTEXT, (DWORD)(WORD)gidHelpContext);
        else
            SendMessage(ghwndApp, WM_COMMAND, IDM_HELPCONTENTS, 0L);
    }
    return DefHookProc(nCode, wParam, (LONG)lpMsg, (HOOKPROC FAR *)&ghhkMsgFilter);
}

/*
 * seg2:086C-091C  InitHelp
 *
 * The help file is VIDCAP.HLP in the program's directory ("?" if the
 * path is too long).
 */
BOOL FAR InitHelp(void)
{
    LPSTR   p;
    int     n;

    n = GetModuleFileName(ghInst, gachHelpFile, sizeof(gachHelpFile));
    p = gachHelpFile + n;
    while (p > gachHelpFile) {
        if (*p == '\\' || *p == ':') {
            p[1] = '\0';
            break;
        }
        n--;
        p = AnsiPrev(gachHelpFile, p);
    }
    lstrcat(gachHelpFile, (n + 13 < (int)sizeof(gachHelpFile)) ? "vidcap.hlp" : "?");

    glpfnMsgFilter = MakeProcInstance((FARPROC)HelpMsgFilter, ghInst);
    ghhkMsgFilter = (FARPROC)SetWindowsHook(WH_MSGFILTER, (HOOKPROC)glpfnMsgFilter);
    gidHelpContext = 0;
    return TRUE;
}

/*
 * seg2:091E-0953  TermHelp
 */
void FAR TermHelp(void)
{
    if (ghhkMsgFilter) {
        UnhookWindowsHook(WH_MSGFILTER, (HOOKPROC)glpfnMsgFilter);
        FreeProcInstance(glpfnMsgFilter);
    }
}
