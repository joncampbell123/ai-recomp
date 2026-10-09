/*
 * data.c - VIDCAP.EXE globals whose module can't be told from the code.
 *
 * DS:01D4-0285 is the initialised data of a module linked between
 * frame.c and dosutil.c that has no code in seg1: the capture settings
 * and the driver channels (most likely the capture module itself).  The
 * uninitialised variables below are used from several modules and are
 * put here for want of a better place.
 */

#include <windows.h>
#include "vidcap.h"

COMPVARS    gCompVars = { sizeof(COMPVARS) };   /* DS:01D4 Video Compression */
DWORD       gdwMicroSecPerFrame = 66666;        /* DS:0212 15 frames/s */
BOOL        gfCaptureAudio    = TRUE;           /* DS:0216 */
BOOL        gfPalettized      = TRUE;           /* DS:0218 */
UINT        gwFrequency       = 11025;          /* DS:021A */
UINT        gwChannels        = 1;              /* DS:021C */
UINT        gwBits            = 8;              /* DS:021E */
BOOL        gfLimitEnabled    = FALSE;          /* DS:0220 */
UINT        gwTimeLimit       = 100;            /* DS:0222 seconds */
HCURSOR     ghcurWait         = NULL;           /* DS:0224 */
HCURSOR     ghcurOld          = NULL;           /* DS:0226 */
BOOL        gfCaptureToMemory = FALSE;          /* DS:0228 */
HVIDEO      ghVideoIn         = NULL;           /* DS:022A */
HVIDEO      ghVideoExtIn      = NULL;           /* DS:022C */
HVIDEO      ghVideoExtOut     = NULL;           /* DS:022E */
BOOL        gfMCIControl      = FALSE;          /* DS:0230 */
char        gachMCIDevice[80] = "";             /* DS:0232 */
UINT        gidTimer          = 0;              /* DS:0282 */
BOOL        gfWarnedDefaultPal = FALSE;         /* DS:0284 */

BOOL        gfOverlay;                          /* DS:155A */
BOOL        gfPreview;                          /* DS:25F8 */
BOOL        gfCenterImage;                      /* DS:25FC */
UINT        gidHelpContext;                     /* DS:2F30 */
char        gachCaptureFile[144];               /* DS:2F4A */
VIDEOHDR    gvhFrame;                           /* DS:2FEE */
HDRAWDIB    ghdd;                               /* DS:3018 */
