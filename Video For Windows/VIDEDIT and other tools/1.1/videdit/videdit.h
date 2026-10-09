/*
 * videdit.h - master header of the reconstructed VIDEDIT.EXE (VidEdit
 * 1.1, the Video for Windows 1.1 AVI editor, Win16 NE application,
 * MD5 840b3657148e73ca12662d40fd4b61e1).
 *
 * Reconstructed from the binary.  Offsets in comments are:
 *   sNN:xxxx   code segment NN, offset xxxx (s01..s20)
 *   DS:xxxx    automatic data segment (seg21) offset
 *
 * Built with a Microsoft C 7 / Visual C++ 1.x class compiler (LINK 5.50,
 * 386 code: ENTER/LEAVE, o32 pushes, inline 8087 code with WIN87EM),
 * medium model: far code, near data.  Unless a prototype says otherwise,
 * functions are FAR _cdecl.  Window and dialog procedures are not
 * exported and have no DS-loading prologue; they rely on DS being
 * DGROUP when they are entered.
 *
 * VidEdit is built on the Media Manager (MEDIAMAN.DLL, media elements
 * with type handlers), the Workbench (WRKBENCH.DLL, tool registration,
 * file dialogs, progress bars) and WINCOM.DLL, the framework of the
 * Multimedia Development Kit tools (BitEdit, PalEdit, WaveEdit).
 */

#ifndef _VIDEDIT_H_
#define _VIDEDIT_H_

#include <windows.h>
#include <mmsystem.h>
#include <commdlg.h>
#include <shellapi.h>
#include <msvideo.h>
#include <avifmt.h>
#include <mediaman.h>
#include <wrkbench.h>
#include <wincom.h>
#include "resource.h"

#ifndef RC_INVOKED

#include "shared.h"

/* types and prototypes of each part of the program */
#include "vemain.h"     /* s01-s03, s08-s10, s13-s15: main window, files, undo, init */
#include "vedwnd.h"     /* s05: frame window, status bar, controls */
#include "frames.h"     /* s06: the frame table, palettes, full frames */
#include "edit.h"       /* s04, s07: view, edit commands, DIBs, audio */
#include "compress.h"   /* s12: compression options */
#include "filedlg.h"    /* s11: file dialogs */
#include "savefile.h"   /* s11: saving */
#include "preview.h"    /* s19: preview */
#include "crop.h"       /* s20: Crop dialog */
#include "stats.h"      /* s17: Statistics */
#include "croprect.h"   /* s18: crop rectangle */
#include "dibmap.h"     /* s16: palettes, colour reduction, bit depth conversion */

#endif /* RC_INVOKED */

#endif /* _VIDEDIT_H_ */
