/*
 * filedlg.h - File/Insert, Extract and Save As dialogs (filedlg.c, segment 11)
 */

#ifndef _FILEDLG_H_
#define _FILEDLG_H_

WORD FAR PASCAL InsertFileDialog(FPMedReturn lpmr, MEDTYPE *pTypes, WORD wFlags, HWND hwnd,
                                 LPSTR lpszTitle);
BOOL FAR PASCAL _loadds ExtractHookProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam);
WORD FAR PASCAL ExtractFileDialog(MEDTYPE *pTypes, PSTR pszFile, int cbFile, MEDTYPE *pmtLogical,
                                  MEDTYPE *pmtPhysical, WORD *pwStart, WORD *pwEnd, WORD wFlags,
                                  HWND hwnd, LPSTR lpszTitle);
BOOL FAR PASCAL _loadds SaveAsHookProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam);
WORD FAR PASCAL SaveAsDialog(PSTR pszFile, int cbFile, MEDTYPE *pmt, BOOL *pfSaveCopy,
                             WORD wFlags, HWND hwnd, LPSTR lpszTitle);

extern OPENFILENAME gofn;               /* DS:1CE2 */

#endif /* _FILEDLG_H_ */
