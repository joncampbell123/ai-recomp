/*
 * crop.h - Video/Crop (crop.c, segment 20)
 */

#ifndef _CROP_H_
#define _CROP_H_

BOOL FAR PASCAL _loadds CropDlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam);
void FAR PASCAL CropMouseMessage(UINT msg, WPARAM wParam, LPARAM lParam);
void FAR        DoCrop(void);
void FAR PASCAL CropShowPoint(POINT pt);
void FAR PASCAL CropShowRect(RECT rc);

extern WORD     gfCropping;             /* DS:118A */

#endif /* _CROP_H_ */
