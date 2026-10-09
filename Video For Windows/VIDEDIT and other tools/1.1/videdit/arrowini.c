/*
 * arrowini.c - code segment 10 of VIDEDIT.EXE (0x0059 bytes), not
 * referenced.
 *
 * A registration of the "ComArrow" spin control class (its window
 * procedure is ArrowControlProc in segment 5) that nothing calls; the
 * class name it uses is the arrow module's string in DGROUP.
 */

#include "videdit.h"
#include "vemain.h"
#include "vedwnd.h"

/*
 * s10:0000-0058  ArrowInit10  (FAR PASCAL, unreferenced)
 */
BOOL FAR PASCAL ArrowInit10(HINSTANCE hInst)
{
    WNDCLASS wc;

    wc.lpszClassName = "ComArrow";
    wc.hInstance     = hInst;
    wc.lpfnWndProc   = ArrowControlProc;
    wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
    wc.hIcon         = NULL;
    wc.lpszMenuName  = NULL;
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.cbClsExtra    = 0;
    wc.cbWndExtra    = 0;
    return RegisterClass(&wc) != 0;
}
