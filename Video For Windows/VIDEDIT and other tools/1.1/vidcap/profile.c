/*
 * profile.c - code segment 2 of VIDCAP.EXE, 0000-07F9: the settings in
 * [VidCap] of MMTOOLS.INI.
 *
 * The key names are kept in the code segment.  Values equal to the
 * default are not written (except the frame period, the capture file
 * name and the MCI device).
 */

#include <windows.h>
#include <stdlib.h>
#include "vidcap.h"

#define CODESTR static char __based(__segname("_CODE"))

CODESTR szLiveWindow[]      = "LiveWindow";         /* seg2:0063 */
CODESTR szCtrlHeight[]      = "CtrlHeight";         /* seg2:006E */
CODESTR szCtrlWidth[]       = "CtrlWidth";          /* seg2:0079 */
CODESTR szInsertMode[]      = "InsertMode";         /* seg2:0083 unused */
CODESTR szTimeFormat[]      = "TimeFormat";         /* seg2:008E unused */
CODESTR szCenterImage[]     = "CenterImage";        /* seg2:0099 */
CODESTR szCtrlYPos[]        = "CtrlYPos";           /* seg2:00A5 */
CODESTR szCtrlXPos[]        = "CtrlXPos";           /* seg2:00AE */
CODESTR szStatusBar[]       = "StatusBar";          /* seg2:00B7 */
CODESTR szToolBar[]         = "ToolBar";            /* seg2:00C1 */
CODESTR szNone[]            = "None";               /* seg2:00C9 unused */
CODESTR szEmpty[]           = "";                   /* seg2:00CE */
CODESTR szBlack[]           = "Black";              /* seg2:00CF */
CODESTR szDkGray[]          = "DkGray";             /* seg2:00D5 */
CODESTR szLtGray[]          = "LtGray";             /* seg2:00DC */
CODESTR szDefault[]         = "Default";            /* seg2:00E3 */
CODESTR szBackgroundColor[] = "BackgroundColor";    /* seg2:00EB */
CODESTR szStepCaptureAt2x[] = "StepCaptureAt2x";    /* seg2:00FB */
CODESTR szAverageFrames[]   = "AverageFrames";      /* seg2:010B */
CODESTR szIndexSize[]       = "IndexSizeInKFrames"; /* seg2:0119 */
CODESTR szYield[]           = "Yield";              /* seg2:012C */
CODESTR szCapFile[]         = "CapFile";            /* seg2:0132 */
CODESTR szFrequency[]       = "Frequency";          /* seg2:013A */
CODESTR szChannels[]        = "Channels";           /* seg2:0144 */
CODESTR szBits[]            = "Bits";               /* seg2:014D */
CODESTR szSound[]           = "Sound";              /* seg2:0152 */
CODESTR szMCIDevice[]       = "MCIDevice";          /* seg2:0158 */
CODESTR szTimeLimit[]       = "TimeLimit";          /* seg2:0162 */
CODESTR szLimitEnabled[]    = "LimitEnabled";       /* seg2:016C */
CODESTR szMicroSecPerFrame[] = "MicroSecPerFrame";  /* seg2:0179 */
CODESTR szCaptureToMemory[] = "CaptureToMemory";    /* seg2:018A */
CODESTR szOverlayWindow[]   = "OverlayWindow";      /* seg2:019A */

#define S(sz)   ((LPSTR)(sz))

/*
 * seg2:0000-0060  WriteProfileInt
 */
void FAR PASCAL WriteProfileInt(LPCSTR lpszSection, LPCSTR lpszKey, int nDefault, int nValue)
{
    char ach[30];

    wsprintf(ach, "%d", nValue);
    if (GetPrivateProfileInt(lpszSection, lpszKey, nDefault, gszIniFile) != nValue)
        WritePrivateProfileString(lpszSection, lpszKey, ach, gszIniFile);
}

/*
 * seg2:01A8-0200  GetProfileBool  (NEAR PASCAL)
 *
 * "y", "Y", "1" are TRUE, "n", "N", "0" FALSE, anything else the
 * default.
 */
static BOOL NEAR PASCAL GetProfileBool(LPCSTR lpszSection, LPCSTR lpszKey, BOOL fDefault)
{
    char ach[10];

    GetPrivateProfileString(lpszSection, lpszKey, "X", ach, sizeof(ach), gszIniFile);
    switch (ach[0]) {
    case 'y':
    case 'Y':
    case '1':
        return TRUE;
    case 'n':
    case 'N':
    case '0':
        return FALSE;
    }
    return fDefault;
}

/*
 * seg2:0202-025A  WriteProfileBool  (NEAR PASCAL)
 */
static void NEAR PASCAL WriteProfileBool(LPCSTR lpszSection, LPCSTR lpszKey, BOOL fDefault, BOOL fValue)
{
    char ach[10];

    if (GetProfileBool(lpszSection, lpszKey, fDefault) == fValue)
        return;
    lstrcpy(ach, fValue ? "Yes" : "No");
    WritePrivateProfileString(lpszSection, lpszKey, ach, gszIniFile);
}

/*
 * seg2:025E-051A  SaveSettings
 *
 * Quirks of the original: the capture file name is compared through a
 * 30-character buffer, so a longer name is always written again; and an
 * unknown background colour writes an uninitialised buffer.
 */
void FAR SaveSettings(void)
{
    char ach[30];
    char achValue[30];

    WriteProfileInt(gszAppName, S(szCtrlXPos), 300, grcApp.left);
    WriteProfileInt(gszAppName, S(szCtrlYPos), 200, grcApp.top);
    WriteProfileInt(gszAppName, S(szCtrlWidth), 404, grcApp.right - grcApp.left);
    WriteProfileInt(gszAppName, S(szCtrlHeight), 244, grcApp.bottom - grcApp.top);
    WriteProfileBool(gszAppName, S(szLiveWindow), FALSE, gfPreview != 0);
    WriteProfileBool(gszAppName, S(szOverlayWindow), FALSE, gfOverlay);
    WriteProfileBool(gszAppName, S(szToolBar), TRUE, gfToolBar);
    WriteProfileBool(gszAppName, S(szStatusBar), TRUE, gfStatusBar);
    WriteProfileBool(gszAppName, S(szCenterImage), TRUE, gfCenterImage);
    WriteProfileBool(gszAppName, S(szCaptureToMemory), FALSE, gfCaptureToMemory);

    GetPrivateProfileString(gszAppName, S(szCapFile), "", ach, sizeof(ach), gszIniFile);
    if (lstrcmp(ach, gachCaptureFile))
        WritePrivateProfileString(gszAppName, S(szCapFile), gachCaptureFile, gszIniFile);

    if (gidBkColor == IDC_BKDEFAULT)
        lstrcpy(achValue, S(szDefault));
    if (gidBkColor == IDC_BKLTGRAY)
        lstrcpy(achValue, S(szLtGray));
    if (gidBkColor == IDC_BKDKGRAY)
        lstrcpy(achValue, S(szDkGray));
    if (gidBkColor == IDC_BKBLACK)
        lstrcpy(achValue, S(szBlack));
    GetPrivateProfileString(gszAppName, S(szBackgroundColor), "L", ach, sizeof(ach), gszIniFile);
    if (achValue[0] != ach[0] || achValue[1] != ach[1])
        WritePrivateProfileString(gszAppName, S(szBackgroundColor), achValue, gszIniFile);

    wsprintf(achValue, "%ld", gdwMicroSecPerFrame);
    WritePrivateProfileString(gszAppName, S(szMicroSecPerFrame), achValue, gszIniFile);
    WriteProfileBool(gszAppName, S(szLimitEnabled), FALSE, gfLimitEnabled);
    WriteProfileInt(gszAppName, S(szTimeLimit), 60, gwTimeLimit);
    WriteProfileBool(gszAppName, S(szSound), TRUE, gfCaptureAudio);
    WritePrivateProfileString(gszAppName, S(szMCIDevice),
                              gfMCIControl ? (LPSTR)gachMCIDevice : S(szEmpty), gszIniFile);
    WriteProfileInt(gszAppName, S(szBits), 8, gwBits);
    WriteProfileInt(gszAppName, S(szChannels), 1, gwChannels);
    WriteProfileInt(gszAppName, S(szFrequency), 11025, gwFrequency);
    WriteProfileInt(gszAppName, S(szIndexSize), 34, (int)(gdwIndexSize >> 10));
    WriteProfileInt(gszAppName, S(szAverageFrames), 1, gnAverageFrames);
    WriteProfileInt(gszAppName, S(szStepCaptureAt2x), 0, gf2xSpatial);
}

/*
 * seg2:051C-07F9  ReadSettings  (NEAR)
 *
 * MCI control is on when MCIDevice is not empty.
 */
void NEAR ReadSettings(void)
{
    char ach[30];

    grcCtrl.top    = GetPrivateProfileInt(gszAppName, S(szCtrlYPos), 200, gszIniFile);
    grcCtrl.bottom = GetPrivateProfileInt(gszAppName, S(szCtrlHeight), 244, gszIniFile) + grcCtrl.top;
    grcCtrl.left   = GetPrivateProfileInt(gszAppName, S(szCtrlXPos), 300, gszIniFile);
    grcCtrl.right  = GetPrivateProfileInt(gszAppName, S(szCtrlWidth), 404, gszIniFile) + grcCtrl.left;

    gfPreview         = GetProfileBool(gszAppName, S(szLiveWindow), FALSE);
    gfOverlay         = GetProfileBool(gszAppName, S(szOverlayWindow), FALSE);
    gfToolBar         = GetProfileBool(gszAppName, S(szToolBar), TRUE);
    gfStatusBar       = GetProfileBool(gszAppName, S(szStatusBar), TRUE);
    gfCenterImage     = GetProfileBool(gszAppName, S(szCenterImage), TRUE);
    gfCaptureToMemory = GetProfileBool(gszAppName, S(szCaptureToMemory), FALSE);

    GetPrivateProfileString(gszAppName, S(szCapFile), "", gachCaptureFile, 144, gszIniFile);

    GetPrivateProfileString(gszAppName, S(szBackgroundColor), "L", ach, sizeof(ach), gszIniFile);
    switch (ach[0]) {
    case 'l':
    case 'L':
        gidBkColor = IDC_BKLTGRAY;
        break;
    case 'b':
    case 'B':
        gidBkColor = IDC_BKBLACK;
        break;
    default:
        if (ach[1] == 'e' || ach[1] == 'E')
            gidBkColor = IDC_BKDEFAULT;
        if (ach[1] == 'k' || ach[1] == 'K')
            gidBkColor = IDC_BKDKGRAY;
        break;
    }

    GetPrivateProfileString(gszAppName, S(szMicroSecPerFrame), "66666", ach, sizeof(ach), gszIniFile);
    gdwMicroSecPerFrame = atol(ach);
    if (gdwMicroSecPerFrame < 10000 || gdwMicroSecPerFrame > 59999999L)
        gdwMicroSecPerFrame = 66666;

    gfLimitEnabled = GetProfileBool(gszAppName, S(szLimitEnabled), FALSE);
    gwTimeLimit    = GetPrivateProfileInt(gszAppName, S(szTimeLimit), 60, gszIniFile);
    gfCaptureAudio = GetProfileBool(gszAppName, S(szSound), TRUE);
    gfYield        = GetProfileBool(gszAppName, S(szYield), FALSE);

    GetPrivateProfileString(gszAppName, S(szMCIDevice), "", gachMCIDevice, 79, gszIniFile);
    gfMCIControl = (gachMCIDevice[0] != '\0');

    gwBits = GetPrivateProfileInt(gszAppName, S(szBits), 8, gszIniFile);
    if (gwBits != 8 && gwBits != 16)
        gwBits = 8;
    gwChannels = GetPrivateProfileInt(gszAppName, S(szChannels), 1, gszIniFile);
    if (gwChannels != 1 && gwChannels != 2)
        gwChannels = 1;
    gwFrequency = GetPrivateProfileInt(gszAppName, S(szFrequency), 11025, gszIniFile);
    if (gwFrequency != 11025 && gwFrequency != 22050 && gwFrequency != 44100)
        gwFrequency = 11025;

    gdwIndexSize = (DWORD)(WORD)GetPrivateProfileInt(gszAppName, S(szIndexSize), 34, gszIniFile) << 10;
    gnAverageFrames = GetPrivateProfileInt(gszAppName, S(szAverageFrames), 1, gszIniFile);
    gf2xSpatial = GetPrivateProfileInt(gszAppName, S(szStepCaptureAt2x), 0, gszIniFile);
}
