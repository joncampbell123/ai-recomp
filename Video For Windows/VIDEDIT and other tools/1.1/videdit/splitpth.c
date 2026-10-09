/*
 * splitpth.c - splitting a path name: code segment 5, s05:28E6-2B6D
 *
 * A far-pointer _splitpath that also reports problems with the name and
 * extension.
 */

#include "videdit.h"
#include "vedwnd.h"

/*
 * s05:28E6-292A  IsBadFileChar  (NEAR PASCAL)
 *
 * Characters that may not appear in a file name or extension:
 * " , * / | : ; < = > ? [ \ ] ^
 */
static BOOL NEAR PASCAL IsBadFileChar(char c)
{
    if (c == '"' || c == ',' || c == '*' || c == '/' || c == '|')
        return TRUE;
    if (c >= ':' && c <= '?')
        return TRUE;
    if (c >= '[' && c <= '^')
        return TRUE;
    return FALSE;
}

/*
 * s05:292E-2B6D  SplitPath  (FAR PASCAL)
 *
 * Splits lpszPath into drive ("C:"), directory (with the trailing
 * backslash), name and extension (with the dot); any output may be NULL.
 * Forward slashes in the path are changed to backslashes in place.
 * Returns 0, 1 if the name or extension was longer than 255 characters
 * (and cut), 2 if it contains a character IsBadFileChar rejects.
 */
int FAR PASCAL SplitPath(LPSTR lpszPath, LPSTR lpszDrive, LPSTR lpszDir, LPSTR lpszName,
                         LPSTR lpszExt)
{
    LPSTR   lpExt = NULL;
    LPSTR   lpName = NULL;
    LPSTR   lpEnd;
    LPSTR   lp;
    int     rc = 0;
    UINT    n, i;

    if (lpszPath[1] == ':') {
        if (lpszDrive) {
            lstrcpyn(lpszDrive, lpszPath, 3);
            lpszDrive[2] = '\0';
        }
        lpszPath += 2;
    } else if (lpszDrive) {
        *lpszDrive = '\0';
    }

    lp = lpszPath;
    if (*lp) {
        do {
            if (*lp == '/')
                *lp = '\\';
            if (*lp == '\\')
                lpName = lp + 1;
            else if (*lp == '.')
                lpExt = lp;
            lp++;
        } while (*lp);
    }
    lpEnd = lp;

    if (lpName) {
        if (lpszDir) {
            n = OFFSETOF(lpName) - OFFSETOF(lpszPath);
            if ((int)n > 0xFF)
                n = 0xFF;
            lstrcpyn(lpszDir, lpszPath, n + 1);
            lpszDir[n] = '\0';
        }
        lpszPath = lpName;
    } else if (lpszDir) {
        *lpszDir = '\0';
    }

    if (lpExt != NULL && OFFSETOF(lpszPath) <= OFFSETOF(lpExt)) {
        if (lpszName) {
            n = OFFSETOF(lpExt) - OFFSETOF(lpszPath);
            if (n >= 0x100) {
                n = 0xFF;
                rc = 1;
            }
            for (i = 0; i < n; i++)
                if (IsBadFileChar(lpszPath[i]))
                    rc = 2;
            lstrcpyn(lpszName, lpszPath, n + 1);
            lpszName[n] = '\0';
        }
        if (lpszExt) {
            n = OFFSETOF(lpEnd) - OFFSETOF(lpExt);
            if (n >= 0x100) {
                n = 0xFF;
                rc = 1;
            }
            for (i = 0; i < n; i++)
                if (IsBadFileChar(lpExt[i]))
                    rc = 2;
            lstrcpyn(lpszExt, lpExt, n + 1);
            lpszExt[n] = '\0';
        }
    } else {
        if (lpszName) {
            n = OFFSETOF(lpEnd) - OFFSETOF(lpszPath);
            if (n >= 0x100) {
                n = 0xFF;
                rc = 1;
            }
            for (i = 0; i < n; i++)
                if (IsBadFileChar(lpszPath[i]))
                    rc = 2;
            lstrcpyn(lpszName, lpszPath, n + 1);
            lpszName[n] = '\0';
        }
        if (lpszExt)
            *lpszExt = '\0';
    }

    return rc;
}
