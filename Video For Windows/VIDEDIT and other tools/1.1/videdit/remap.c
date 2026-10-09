/*
 * remap.c - code segment 6 of VIDEDIT.EXE, s06:54EE-5903: colour remap
 * tables
 *
 * A remap table is 256 bytes mapping the colours of one palette to the
 * nearest colours of another (optionally after another remap table); a
 * frame whose palette was replaced keeps its DIB and draws it through
 * one.  Tables are shared through a reference-counted list and are known
 * by the selector of their locked memory.  A table holds references to
 * the two palettes and to the table it was built on.
 *
 * Functions are in the order of the binary.
 */

#include "videdit.h"
#include "frames.h"

typedef struct tagREMAPREF {
    WORD        sel;                    /* 00 the table */
    HPALETTE    hpalDst;                /* 02 */
    HPALETTE    hpalSrc;                /* 04 */
    WORD        selIn;                  /* 06 table applied before this one */
    DWORD       dwRef;                  /* 08 */
} REMAPREF;                             /* 0x0C bytes */

/* DS:10FC-1103 */
static REMAPREF FAR *gpRemapTable;      /* DS:10FC */
static UINT         gnRemapTable;       /* DS:1100 */
static UINT         gcRemapTable;       /* DS:1102 entries allocated */

static WORD NEAR PASCAL RemapAdd(WORD sel, HPALETTE hpalSrc, HPALETTE hpalDst, WORD selIn,
                                 DWORD dwRef);

/*
 * s06:54EE-554C  RemapTableFree
 *
 * Bug of the original: like PalTableFree, it frees the first table of the
 * list over and over instead of each one.
 */
void FAR RemapTableFree(void)
{
    UINT    i;

    if (gnRemapTable) {
        for (i = 0; i < gnRemapTable; i++) {
            GlobalUnlock((HGLOBAL)gpRemapTable[0].sel);
            GlobalFree((HGLOBAL)gpRemapTable[0].sel);
        }
        gnRemapTable = 0;
    }

    if (gpRemapTable) {
        GFreePtr(gpRemapTable);
        gpRemapTable = NULL;
    }
    gcRemapTable = 0;
}

/*
 * s06:554E-563C  RemapRelease
 *
 * Takes dwRef references from a table; at 0 it is freed with its
 * references to the palettes and the table under it.  FALSE if unknown.
 */
BOOL FAR PASCAL RemapRelease(WORD selRemap, DWORD dwRef)
{
    REMAPREF FAR   *p;
    UINT            i;

    if (selRemap == 0 || dwRef == 0)
        return TRUE;

    for (i = 0, p = gpRemapTable; i < gnRemapTable; i++, p++) {
        if (p->sel == selRemap)
            break;
    }
    if (i >= gnRemapTable)
        return FALSE;

    if (p->dwRef == dwRef) {
        GlobalUnlock((HGLOBAL)p->sel);
        GlobalFree((HGLOBAL)p->sel);
        if (p->hpalSrc)
            PalRelease(p->hpalSrc, 1L);
        if (p->hpalDst)
            PalRelease(p->hpalDst, 1L);
        if (p->selIn)
            RemapRelease(p->selIn, 1L);
        if (gnRemapTable - 1 > i)
            *p = gpRemapTable[gnRemapTable - 1];
        gnRemapTable--;
    } else
        p->dwRef -= dwRef;

    return TRUE;
}

/*
 * s06:5640-5763  RemapCreate
 *
 * A table mapping the colours of hpalSrc (taken through selRemapIn if
 * given, which then covers 256 entries) to the nearest colours of
 * hpalDst, with dwRef references.  An existing table for the same three
 * is shared.  0 if there is no memory.
 */
WORD FAR PASCAL RemapCreate(HPALETTE hpalSrc, HPALETTE hpalDst, WORD selRemapIn, DWORD dwRef)
{
    REMAPREF FAR   *p;
    PALETTEENTRY    pe;
    BYTE FAR       *pbIn;
    BYTE FAR       *pb;
    WORD            sel;
    WORD            nColors;
    WORD            wIndex;
    UINT            i;

    for (i = 0, p = gpRemapTable; i < gnRemapTable; i++, p++) {
        if (p->hpalSrc == hpalSrc && p->hpalDst == hpalDst && p->selIn == selRemapIn) {
            p->dwRef += dwRef;
            return p->sel;
        }
    }

    sel = GAllocSelF(GMEM_MOVEABLE, 256);
    if (sel == 0)
        return 0;
    pb = (BYTE FAR *)MAKELP(sel, 0);

    if (selRemapIn) {
        pbIn = (BYTE FAR *)MAKELP(selRemapIn, 0);
        nColors = 256;
    } else {
        pbIn = NULL;
        GetObject(hpalSrc, sizeof(nColors), &nColors);
    }

    for (i = 0; i < nColors; i++) {
        if (pbIn)
            wIndex = pbIn[i];
        else
            wIndex = i;
        GetPaletteEntries(hpalSrc, wIndex, 1, &pe);
        pb[i] = (BYTE)GetNearestPaletteIndex(hpalDst, RGB(pe.peRed, pe.peGreen, pe.peBlue));
    }

    return RemapAdd(sel, hpalSrc, hpalDst, selRemapIn, dwRef);
}

/*
 * s06:5766-57AD  RemapAddRef
 *
 * Adds dwRef references to a table; a table not in the list (one pasted
 * from the clipboard) is entered.
 */
WORD FAR PASCAL RemapAddRef(WORD selRemap, DWORD dwRef)
{
    REMAPREF FAR   *p;
    UINT            i;

    for (i = 0, p = gpRemapTable; i < gnRemapTable; i++, p++) {
        if (p->sel == selRemap) {
            p->dwRef += dwRef;
            return p->sel;
        }
    }
    return RemapAdd(selRemap, 0, 0, 0, dwRef);
}

/*
 * s06:57B0-57B3  RemapSize
 */
WORD FAR PASCAL RemapSize(WORD selRemap)
{
    return 256;
}

/*
 * s06:57B6-5901  RemapAdd  (NEAR PASCAL)
 *
 * Enters a table in the list (growing it by 32 entries) and takes the
 * references it holds.  If the list can't grow the table is freed and 0
 * returned.
 */
static WORD NEAR PASCAL RemapAdd(WORD sel, HPALETTE hpalSrc, HPALETTE hpalDst, WORD selIn,
                                 DWORD dwRef)
{
    REMAPREF FAR   *p;
    WORD            selTable;
    UINT            i;

    if (gnRemapTable >= gcRemapTable) {
        if (gpRemapTable) {
            selTable = GReAllocSelF(SELECTOROF(gpRemapTable),
                                    (DWORD)(WORD)((gcRemapTable + 32) * sizeof(REMAPREF)), 0);
            if (selTable == 0) {
                GlobalFree((HGLOBAL)sel);
                return 0;
            }
            gpRemapTable = (REMAPREF FAR *)MAKELP(selTable, 0);
        } else {
            gpRemapTable = (REMAPREF FAR *)GAllocPtrF(GMEM_MOVEABLE, 32 * sizeof(REMAPREF));
            if (gpRemapTable == NULL) {
                GlobalFree((HGLOBAL)sel);
                return 0;
            }
        }
        gcRemapTable += 32;
    }

    p = &gpRemapTable[gnRemapTable];
    p->sel = sel;
    p->hpalDst = hpalDst;
    p->hpalSrc = hpalSrc;
    p->selIn = selIn;
    p->dwRef = dwRef;
    gnRemapTable++;

    if (hpalDst)
        PalAddRef(hpalDst, 1L);
    if (hpalSrc)
        PalAddRef(hpalSrc, 1L);

    if (selIn) {
        for (i = 0, p = gpRemapTable; i < gnRemapTable; i++, p++) {
            if (p->sel == selIn) {
                p->dwRef++;
                break;
            }
        }
    }

    return sel;
}
