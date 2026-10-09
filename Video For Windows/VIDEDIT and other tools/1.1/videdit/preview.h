/*
 * preview.h - File/Play Preview (preview.c, segment 19)
 */

#ifndef _PREVIEW_H_
#define _PREVIEW_H_

BOOL FAR        PreviewSetTracks(void);
BOOL FAR PASCAL PreviewSetWindow(BOOL fShow);
BOOL FAR        PreviewSetRect(void);
void FAR        PreviewUpdatePosition(void);
void FAR        PreviewIdle(void);
void FAR PASCAL PreviewPaint(HDC hdc);
void FAR        PreviewRealize(void);
void FAR        StopPreview(void);
void FAR PASCAL StartPreview(BOOL fSelection);
void FAR        ClosePreview(void);
void FAR        TermPreview(void);
void FAR PASCAL PreviewNotify(LPARAM lParam, WPARAM wParam);

extern WORD     gfPreviewSel;           /* DS:1122 */

#endif /* _PREVIEW_H_ */
