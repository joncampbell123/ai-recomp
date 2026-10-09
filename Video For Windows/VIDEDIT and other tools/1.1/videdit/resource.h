/*
 * resource.h - menu commands, dialog templates and other resource IDs of
 * the reconstructed VIDEDIT.EXE (shared by videdit.rc and the C source).
 *
 * String IDs and dialog control IDs are defined next to the code that
 * uses them.
 */

#ifndef _RESOURCE_H_
#define _RESOURCE_H_

/* File */
#define IDM_NEW                 11
#define IDM_OPEN                12
#define IDM_SAVE                13
#define IDM_SAVEAS              14
#define IDM_REVERT              15
#define IDM_INSERT              16
#define IDM_EXTRACT             17
#define IDM_CAPTURE             18
#define IDM_PREVIEW             19
#define IDM_EXIT                20

/* Edit */
#define IDM_UNDO                772
#define IDM_CUT                 768
#define IDM_COPY                769
#define IDM_PASTE               770
#define IDM_PASTEPALETTE        30
#define IDM_DELETE              31
#define IDM_TRACKVIDEO          32
#define IDM_TRACKAUDIO          33
#define IDM_TRACKBOTH           34
#define IDM_PREFERENCES         36
#define IDM_GOTO                37
#define IDM_SETSELECTION        38
#define IDM_INSERTMODE          39      /* Insert key: insert or overwrite mode */

/* View */
#define IDM_ZOOM1               42
#define IDM_ZOOM2               43
#define IDM_ZOOM4               44
#define IDM_ZOOMHALF            45
#define IDM_FULLSCREEN          48      /* string only */
#define IDM_FULLFRAMEUPDATE     50
#define IDM_FASTFRAMEUPDATE     51
#define IDM_DRAWFULLFRAME       52

/* Video */
#define IDM_COMPRESSIONOPTIONS  60
#define IDM_CROP                61
#define IDM_RESIZE              62
#define IDM_SYNCHRONIZE         63
#define IDM_CREATEPALETTE       64
#define IDM_LOADINTOMEMORY      65
#define IDM_CONVERTFRAMERATE    66
#define IDM_STATISTICS          67
#define IDM_AUDIOFORMAT         68
#define IDM_VIDEOFORMAT         69

/* Help */
#define IDM_ABOUT               70
#define IDM_HELPCONTENTS        71

/* accelerator-only commands */
#define IDM_ACCEL80             80      /* Ctrl+P */
#define IDM_ACCEL81             81      /* Ctrl+Q, Alt+P, Ctrl+Alt+P */
#define IDM_ACCEL82             82      /* Ctrl+S, Esc */

/* dialog templates */
#define IDD_CROP                100
#define IDD_PASTEPALETTE        101
#define IDD_CREATEPALETTE       102
#define IDD_PALETTECREATED      103
#define IDD_CONVERTFRAMERATE    104
#define IDD_SETSELECTION        105
#define IDD_STATISTICS          106
#define IDD_RESIZE              107
#define IDD_GOTO                108
#define IDD_PREFERENCES         109
#define IDD_SYNCHRONIZE         110
#define IDD_VIDEOFORMAT         111
#define IDD_COMPRESSION         115
#define IDD_MSVCCONFIG          116     /* "Microsoft Graphic Compressor" */
#define IDD_RLECONFIG           117     /* "Microsoft Animation Compressor" */
#define IDD_CRUNCH              400     /* "Crunch or Die" (unused?) */
#define IDD_FRAMEINFO           901     /* child: "Frame Information" */
#define IDD_EXTRACTFILE         "EXTRACTFILE"
#define IDD_AVISAVEAS           "AVISAVEAS"
#define IDD_SELRANGE            "IDA_SELRANGE"

/* bitmaps, cursors, the intro DIB */
#define IDB_100                 100
#define IDB_101                 101
#define IDB_103                 103
#define IDB_104                 104
#define IDB_110                 110
#define IDB_111                 111
#define IDB_112                 112
#define IDB_FILLPAT             "FILLPAT"
#define IDC_CURSOR400           400
#define IDC_CURSOR401           401
#define IDB_INTRO               402

#endif /* _RESOURCE_H_ */
