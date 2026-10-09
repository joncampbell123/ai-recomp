/*
 * term.c - code segment 3 of VIDEDIT.EXE (0x016E bytes, PRELOAD
 * DISCARDABLE), the termination segment.
 *
 * AppTerm, called by WinMain after the message loop, and termination
 * code of other modules (the audio module's TermWave, undo.c's
 * TermUndo), apparently placed here with alloc_text like init.c.
 */

#include "videdit.h"
#include "vemain.h"
#include "edit.h"
#include "preview.h"
#include "croprect.h"
#include "dibmap.h"

/*
 * seg3:0000-0020  DeleteMenuBitmaps  (NEAR)
 */
static void NEAR DeleteMenuBitmaps(void)
{
    if (ghbmMenuCheck)
        DeleteObject(ghbmMenuCheck);
    if (ghbmMenuUncheck)
        DeleteObject(ghbmMenuUncheck);
}

/*
 * seg3:0022-0158  AppTerm
 *
 * Saves the settings (only if WrkClientInit was reached), unhooks help,
 * deletes the GDI objects, frees the palette module's gray palette and
 * RGB555 map, tells the other modules, leaves the Workbench and unregisters
 * from Pen Windows.
 */
BOOL FAR AppTerm(void)
{
    FARPROC lpfnRegisterPenApp;
    LPVOID  lp;

    if (gfWrkClient) {
        WrkClientExit();
        SaveSettings();
    }
    TermHelp();
    if (ghdcDib)
        DeleteDC(ghdcDib);
    if (ghfontApp && ghfontApp != GetStockObject(SYSTEM_FONT))
        DeleteObject(ghfontApp);
    if (ghbrBtnText)
        DeleteObject(ghbrBtnText);
    if (ghbrBtnFace)
        DeleteObject(ghbrBtnFace);
    if (ghbrBtnShadow)
        DeleteObject(ghbrBtnShadow);
    if (ghbrBtnHighlight)
        DeleteObject(ghbrBtnHighlight);
    if (ghbrFillPat)
        DeleteObject(ghbrFillPat);
    if (DibGetGrayPalette())
        DeleteObject(DibGetGrayPalette());
    if (DibGetPaletteMap()) {
        lp = DibGetPaletteMap();
        GlobalUnlock((HGLOBAL)SELECTOROF(lp));
        lp = DibGetPaletteMap();
        GlobalFree((HGLOBAL)SELECTOROF(lp));
    }
    TermUndo();
    ClipboardMakeStatic();
    TermWave();
    TermPreview();
    DeleteControlBarFonts();
    if (gwWrkInstance)
        WrkRemoveInstance(gwWrkInstance);
    if (ghdd)
        DrawDibClose(ghdd);
    DeleteMenuBitmaps();
    CropTermBrush();

    lpfnRegisterPenApp = GetProcAddress((HINSTANCE)GetSystemMetrics(SM_PENWINDOWS), gszRegisterPenApp);
    if (lpfnRegisterPenApp)
        (*(void (FAR PASCAL *)(WORD, BOOL))lpfnRegisterPenApp)(1, FALSE);
    return TRUE;
}

/*
 * seg3:015A-0163  TermWave  (the audio module's)
 */
void FAR TermWave(void)
{
    FreeLibrary(ghMedWave);
}

/*
 * seg3:0164-0165  TermStub0164  (unreferenced)
 */
void FAR TermStub0164(void)
{
}

/*
 * seg3:0166-016D  TermUndo  (undo.c's)
 */
void FAR TermUndo(void)
{
    SetUndo(0);
}
