/*
 * arrowini.c - code segment 4 of VIDCAP.EXE (0x0069 bytes): registration
 * of the "ComArrow" control (arrow.c), alone in a load-on-call segment.
 */

#include <windows.h>
#include "vidcap.h"

/*
 * seg4:0000-0067  ArrowInit
 */
BOOL FAR PASCAL ArrowInit(HINSTANCE hInst)
{
    WNDCLASS wc;

    wc.lpszClassName = gszArrowClass;
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
