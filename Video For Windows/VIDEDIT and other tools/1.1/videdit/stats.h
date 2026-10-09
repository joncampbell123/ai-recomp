/*
 * stats.h - Video/Statistics (stats.c, segment 17)
 */

#ifndef _STATS_H_
#define _STATS_H_

LONG FAR PASCAL _loadds StatsDlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam);
void FAR PASCAL DoStatistics(HWND hwndParent);

#endif /* _STATS_H_ */
