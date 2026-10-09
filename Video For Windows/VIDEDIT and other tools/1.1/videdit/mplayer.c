/*
 * mplayer.c - code segment 15 of VIDEDIT.EXE (0x0065 bytes), not
 * referenced.
 *
 * Plays a movie by running Media Player ("mplayer AviVideo!file"), an
 * earlier way of doing File Play Preview, which now uses MCI directly
 * (segment 19).  The strings are this module's part of DGROUP
 * (DS:0BC8-0C03).
 */

#include "videdit.h"
#include "vemain.h"
#include "vedwnd.h"

static char szPlayFile[] = "mplayer AviVideo!%s";      /* DS:0BDF */
static char szPlay[]     = "mplayer AviVideo";         /* DS:0BF3 */

/*
 * s15:0000-0064  PlayWithMPlayer  (FAR PASCAL, unreferenced)
 */
void FAR PASCAL PlayWithMPlayer(LPSTR lpszFile)
{
    char    ach[154];
    LPSTR   lpszCmd;

    if (lpszFile && *lpszFile) {
        wsprintf(ach, szPlayFile, lpszFile);
        lpszCmd = ach;
    } else {
        lpszCmd = szPlay;
    }
    if (WinExec(lpszCmd, SW_SHOWNORMAL) < 32)
        ErrorResBox(ghwndApp, ghInst, MB_ICONEXCLAMATION, IDS_APPTITLE, IDS_NOMPLAYER);
}
