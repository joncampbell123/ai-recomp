/*
 * dibmap.h - colour histograms, optimal palettes, colour reduction
 * (dibmap.c, invcmap.c, dibconv.c: segment 16)
 */

#ifndef _DIBMAP_H_
#define _DIBMAP_H_

/* 32768 counts, indexed by RGB555 */
typedef DWORD huge *LPHISTOGRAM;

#define RGB16(r, g, b)  ((((WORD)(r) >> 3) << 10) | (((WORD)(g) >> 3) << 5) | ((WORD)(b) >> 3))

/* dibmap.c */
LPHISTOGRAM FAR InitHistogram(LPHISTOGRAM lpHistogram);
void        FAR FreeHistogram(LPHISTOGRAM lpHistogram);
BOOL        FAR DibHistogram(LPBITMAPINFOHEADER lpbi, LPBYTE lpBits, int x, int y, int dx, int dy,
                             LPHISTOGRAM lpHistogram);
HPALETTE    FAR HistogramPalette(LPHISTOGRAM lpHistogram, LPBYTE lp16to8, int nColors);
HANDLE      FAR DibReduce(LPBITMAPINFOHEADER lpbiIn, LPBYTE pbIn, HPALETTE hpal, LPBYTE lp16to8);

/* invcmap.c */
BOOL FAR PASCAL BuildInverseMap(HPALETTE hpal, LPBYTE lpMap);

/* dibconv.c */
HPALETTE    FAR DibGetGrayPalette(void);
LPBYTE      FAR DibGetPaletteMap(void);
BOOL        FAR DibConvertBitCount(HANDLE FAR *phdib, HPALETTE FAR *phpal, WORD wBitCount);

#endif /* _DIBMAP_H_ */
