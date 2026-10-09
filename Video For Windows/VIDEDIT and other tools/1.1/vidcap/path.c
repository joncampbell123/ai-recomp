/*
 * path.c - code segment 1 of VIDCAP.EXE, 116C-141C: splitting a file name.
 */

#include <windows.h>
#include "vidcap.h"

/*
 * seg1:116C-11B0  IsInvalidFileChar  (NEAR PASCAL)
 */
static BOOL NEAR PASCAL IsInvalidFileChar(char c)
{
    if (c == '"' || c == ',' || c == '*' || c == '/' || c == '|' ||
        (c >= ':' && c <= '?') || (c >= '[' && c <= '^'))
        return TRUE;
    return FALSE;
}

/*
 * seg1:11B4-141A  SplitPath
 *
 * Splits lpszPath into drive ("C:"), directory (with the trailing
 * backslash), name and extension (with the dot); any output may be NULL.
 * Forward slashes in lpszPath are changed to backslashes.  Returns 0, 1
 * if a part was cut to 255 characters, or 2 if the name has a character
 * that is not allowed in file names.
 *
 * Quirk of the original: the extension is checked by looking at the
 * first characters of the name instead.
 */
int FAR PASCAL SplitPath(LPSTR lpszPath, LPSTR lpszDrive, LPSTR lpszDir, LPSTR lpszName, LPSTR lpszExt)
{
    LPSTR   p;
    LPSTR   lpSlash;
    LPSTR   lpDot;
    LPSTR   lpEnd;
    UINT    n;
    UINT    i;
    int     ret;

    lpDot = NULL;
    ret = 0;

    if (lpszPath[1] == ':') {
        if (lpszDrive) {
            lstrcpyn(lpszDrive, lpszPath, 3);
            lpszDrive[2] = '\0';
        }
        lpszPath += 2;
    } else if (lpszDrive) {
        *lpszDrive = '\0';
    }

    lpSlash = NULL;
    p = lpszPath;
    if (*lpszPath) {
        do {
            if (*p == '/')
                *p = '\\';
            if (*p == '\\')
                lpSlash = p + 1;
            else if (*p == '.')
                lpDot = p;
            p++;
        } while (*p);
    }
    lpEnd = p;

    if (lpSlash) {
        if (lpszDir) {
            n = (int)(OFFSETOF(lpSlash) - OFFSETOF(lpszPath));
            if ((int)n > 255)
                n = 255;
            lstrcpyn(lpszDir, lpszPath, n + 1);
            lpszDir[n] = '\0';
        }
        lpszPath = lpSlash;
    } else if (lpszDir) {
        *lpszDir = '\0';
    }

    if (lpDot && OFFSETOF(lpszPath) <= OFFSETOF(lpDot)) {
        if (lpszName) {
            n = OFFSETOF(lpDot) - OFFSETOF(lpszPath);
            if (n >= 256) {
                n = 255;
                ret = 1;
            }
            for (i = 0; i < n; i++)
                if (IsInvalidFileChar(lpszPath[i]))
                    ret = 2;
            lstrcpyn(lpszName, lpszPath, n + 1);
            lpszName[n] = '\0';
        }
        if (lpszExt) {
            n = OFFSETOF(lpEnd) - OFFSETOF(lpDot);
            if (n >= 256) {
                n = 255;
                ret = 1;
            }
            for (i = 0; i < n; i++)
                if (IsInvalidFileChar(lpszPath[i]))
                    ret = 2;
            lstrcpyn(lpszExt, lpDot, n + 1);
            lpszExt[n] = '\0';
        }
    } else {
        if (lpszName) {
            n = OFFSETOF(lpEnd) - OFFSETOF(lpszPath);
            if (n >= 256) {
                n = 255;
                ret = 1;
            }
            for (i = 0; i < n; i++)
                if (IsInvalidFileChar(lpszPath[i]))
                    ret = 2;
            lstrcpyn(lpszName, lpszPath, n + 1);
            lpszName[n] = '\0';
        }
        if (lpszExt)
            *lpszExt = '\0';
    }

    return ret;
}
