/*
 * croprect.h - the crop rectangle tracker (croprect.c, segment 18)
 */

#ifndef _CROPRECT_H_
#define _CROPRECT_H_

/* CropHitTest / CropBeginDrag results besides the corners 0-3 */
#define CROPHIT_OUTSIDE     4
#define CROPHIT_INSIDE      5
#define CROPHIT_BORDER      6
#define CROPHIT_NONE        7

BOOL FAR PASCAL CropInitBrush(void);
BOOL FAR PASCAL CropTermBrush(void);
void FAR PASCAL CropNormalizeRect(RECT *prc);
BOOL FAR PASCAL CropStart(HWND hwnd, WORD wFlags);
BOOL FAR PASCAL CropEnd(void);
BOOL FAR PASCAL CropGetRect(RECT *prc);
BOOL FAR PASCAL CropSetRect(RECT *prc);
void FAR PASCAL CropDraw(BOOL fErase, BOOL fDraw);
void FAR PASCAL CropTimer(void);
void FAR PASCAL CropMouseMove(POINT pt, WORD wFlags);
void FAR PASCAL CropButtonUp(void);
void FAR PASCAL CropButtonDown(POINT pt, BOOL fRight);

#endif /* _CROPRECT_H_ */
