/*
 * paltable.c - code segment 6 of VIDEDIT.EXE, s06:5298-54ED: the table of
 * palettes the frames use
 *
 * Frames share palettes through a table of (HPALETTE, reference count)
 * pairs.  Adding a palette equal to one in the table gives back the one
 * in the table and deletes the new one.
 *
 * Functions are in the order of the binary.
 */

#include "videdit.h"
#include "frames.h"

typedef struct tagPALREF {
    HPALETTE    hpal;                   /* 00 */
    DWORD       dwRef;                  /* 02 */
} PALREF;                               /* 6 bytes */

/* DS:10DC-10E3 */
static PALREF FAR  *gpPalTable;         /* DS:10DC */
static UINT         gnPalTable;         /* DS:10E0 */
static UINT         gcPalTable;         /* DS:10E2 entries allocated */

/*
 * s06:5298-52EA  PalTableFree
 *
 * Deletes the palettes and frees the table.
 *
 * Bug of the original: the loop deletes the first palette of the table
 * over and over instead of each one in turn.
 */
void FAR PalTableFree(void)
{
    UINT    i;

    if (gnPalTable) {
        for (i = 0; i < gnPalTable; i++)
            DeleteObject(gpPalTable[0].hpal);
        gnPalTable = 0;
    }

    if (gpPalTable) {
        GFreePtr(gpPalTable);
        gpPalTable = NULL;
    }
    gcPalTable = 0;
}

/*
 * s06:52EC-544E  PalAddRef
 *
 * Adds dwRef references to hpal and returns the palette to use: hpal
 * itself, or an equal palette already in the table (hpal is deleted
 * then).  0 if there is no memory (hpal is deleted too).
 */
HPALETTE FAR PASCAL PalAddRef(HPALETTE hpal, DWORD dwRef)
{
    PALREF FAR *p;
    WORD        sel;
    UINT        i;

    for (i = 0, p = gpPalTable; i < gnPalTable; i++, p++) {
        if (p->hpal == hpal) {
            p->dwRef += dwRef;
            return p->hpal;
        }
    }

    for (i = 0, p = gpPalTable; i < gnPalTable; i++, p++) {
        if (PalEq(hpal, p->hpal)) {
            p->dwRef += dwRef;
            DeleteObject(hpal);
            return p->hpal;
        }
    }

    if (gnPalTable >= gcPalTable) {
        if (gpPalTable) {
            sel = GReAllocSelF(SELECTOROF(gpPalTable), (DWORD)(WORD)((gcPalTable + 32) * sizeof(PALREF)), 0);
            if (sel == 0) {
                DeleteObject(hpal);
                return 0;
            }
            gpPalTable = (PALREF FAR *)MAKELP(sel, 0);
        } else {
            gpPalTable = (PALREF FAR *)GAllocPtrF(GMEM_MOVEABLE, 32 * sizeof(PALREF));
            if (gpPalTable == NULL) {
                DeleteObject(hpal);
                return 0;
            }
        }
        gcPalTable += 32;
    }

    gpPalTable[gnPalTable].hpal = hpal;
    gpPalTable[gnPalTable].dwRef = dwRef;
    gnPalTable++;
    return hpal;
}

/*
 * s06:5452-54EA  PalRelease
 *
 * Takes dwRef references from hpal; at 0 the palette is deleted and its
 * entry replaced by the last one.  FALSE if hpal is not in the table.
 */
BOOL FAR PASCAL PalRelease(HPALETTE hpal, DWORD dwRef)
{
    PALREF FAR *p;
    UINT        i;

    if (hpal == 0 || dwRef == 0)
        return TRUE;

    for (i = 0, p = gpPalTable; i < gnPalTable; i++, p++) {
        if (p->hpal == hpal)
            break;
    }
    if (i >= gnPalTable)
        return FALSE;

    if (p->dwRef == dwRef) {
        DeleteObject(p->hpal);
        if (gnPalTable - 1 > i)
            *p = gpPalTable[gnPalTable - 1];
        gnPalTable--;
    } else
        p->dwRef -= dwRef;

    return TRUE;
}
