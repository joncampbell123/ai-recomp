/*
 * vemain.h - types, globals and prototypes of the VidEdit application
 * core: WinMain and the main commands (videdit.c), undo (undo.c),
 * hmemmove (hmem.c), files (file.c), muldiv32 (muldiv32.c), start-up
 * and termination (init.c, term.c), the MIDI stubs (midistub.c), the
 * Preferences and Go To dialogs (prefs.c, goto.c), RLE decompression
 * (rle.c) and two unreferenced modules (arrowini.c, mplayer.c).
 */

#ifndef _VEMAIN_H_
#define _VEMAIN_H_

/* string IDs used by these files */
#define IDS_APPNAME             100     /* "VidEdit" */
#define IDS_COPYRIGHT           101
#define IDS_VERSION             102     /* "1.1" */
#define IDS_APPTITLE            126     /* "VidEdit" (message box captions) */
#define IDS_TITLEPREFIX         127     /* "VidEdit - " */
#define IDS_TITLEFRAME          129     /* " (Frame " */
#define IDS_CONTROLTITLE        151     /* "VidEdit Control - " */
#define IDS_UNTITLED            152     /* "Untitled" */
#define IDS_155                 155     /* not in the string table */
#define IDS_NOMPLAYER           172     /* "Unable to start the Media Player" */
#define IDS_ABOUT               180     /* "About VidEdit" */
#define IDS_SAVECHANGES         181
#define IDS_SAVECHANGESUNTITLED 182
#define IDS_SAVETITLE           183     /* "Save Video File" */
#define IDS_REVERT              184
#define IDS_OPENTITLE           186     /* "Open Video File" */
#define IDS_EXTRACTTITLE        187     /* "Extract File" */
#define IDS_INSERTTITLE         188     /* "Insert File" */
#define IDS_READONLY            190
#define IDS_INUSE               191
#define IDS_NOVIDEO             225
#define IDS_MULTIVIDEO          226
#define IDS_MULTIAUDIO          227
#define IDS_BADINSERTVIDEO      260
#define IDS_BADINSERTAUDIO      261
#define IDS_CAPTUREFILE         282     /* "%ls was a capture file.  ..." */
#define IDS_NOVIDCAP            296
#define IDS_UNDOERROR           298
#define IDS_NOOVERWRITE         253
#define IDS_UNDO                300     /* menu texts 300..313 */
#define IDS_UNDOUNDO            301
#define IDS_UNDODELETE          302
#define IDS_UNDOINSERT          303
#define IDS_UNDOCUT             304
#define IDS_UNDOPASTE           305
#define IDS_UNDOPASTEPALETTE    306
#define IDS_UNDOSYNC            307
#define IDS_UNDOCROP            308
#define IDS_UNDORESIZE          309
#define IDS_UNDORESIZE2         311
#define IDS_UNDOVIDEOFORMAT     312
#define IDS_UNDOAUDIOFORMAT     313
#define IDS_GOTOTIME            482
#define IDS_GOTOFRAME           483
#define IDS_TBOPEN              1010    /* tool bar status texts */
#define IDS_TBSAVE              1011
#define IDS_TBCUT               1012
#define IDS_TBNOCUT             1013
#define IDS_TBCOPY              1014
#define IDS_TBNOCOPY            1015
#define IDS_TBPASTE             1016
#define IDS_TBNOPASTE           1017
#define IDS_TBNOUNDO            1020    /* 1020..1033 */
#define IDS_TBUNDOUNDO          1021
#define IDS_TBUNDODELETE        1022
#define IDS_TBUNDOINSERT        1023
#define IDS_TBUNDOCUT           1024
#define IDS_TBUNDOPASTE         1025
#define IDS_TBUNDOPASTEPALETTE  1026
#define IDS_TBUNDOSYNC          1027
#define IDS_TBUNDOCROP          1028
#define IDS_TBUNDORESIZE        1029
#define IDS_TBUNDORESIZE2       1031
#define IDS_TBUNDOVIDEOFORMAT   1032
#define IDS_TBUNDOAUDIOFORMAT   1033
#define IDS_TBZOOMHALF          1046
#define IDS_TBZOOM1             1047
#define IDS_TBZOOM2             1048
#define IDS_TBZOOM4             1049
#define IDS_TRACKAUDIO          1065    /* "Audio" */
#define IDS_TRACKVIDEO          1066    /* "Video" */
#define IDS_TRACKBOTH           1067    /* "Audio/Video" */
#define IDS_INS                 1068
#define IDS_OVR                 1069
#define IDS_STATKEY                   1070
#define IDS_STATPAL                   1071

/* Preferences dialog (IDD_PREFERENCES) */
#define IDC_PREF_CENTER         1050
#define IDC_PREF_RESIZE         1051
#define IDC_PREF_CACHE          1052
#define IDC_PREF_FRAMENUM       1053
#define IDC_PREF_TIMEFORMAT     1054
#define IDC_PREF_BKDEFAULT      1055
#define IDC_PREF_BKLTGRAY       1056
#define IDC_PREF_BKDKGRAY       1057
#define IDC_PREF_BKBLACK        1058
#define IDC_PREF_OVERWRITE      1060
#define IDC_PREF_STATUSBAR      1061
#define IDC_PREF_TOOLBAR        1062

/* Go To dialog (IDD_GOTO) */
#define IDC_GOTO_FRAME          1070

/* the movie's information block, medSendMessage(medid, MED_USER, lp, 0x2C) */
#define MED_GETMOVIEINFO        (MED_USER + 0x00)
#define MED_GETSTREAMTYPE       (MED_USER + 0x01)
#define MED_GETSTREAMHANDLER    (MED_USER + 0x03)
#define MED_GETSTREAMFORMAT     (MED_USER + 0x04)
#define MED_GETSTREAMELEMENT    (MED_USER + 0x05)

typedef struct {
    DWORD   dwFlags;            /* 00 0x10000 = was a capture file */
    DWORD   dwFlags2;           /* 04 */
    DWORD   dw08;
    DWORD   dw0C;
    DWORD   dw10;               /* 10 */
    DWORD   dw14;               /* 14 */
    DWORD   dwStreams;          /* 18 */
    DWORD   dw1C;               /* 1C */
    DWORD   dw20;
    DWORD   dw24;               /* 24 */
    DWORD   dw28;               /* 28 */
} MOVIEINFO;                    /* 0x2C bytes */

/* ------------------------------------------------------------------ */
/* globals defined in these files                                      */
/* ------------------------------------------------------------------ */

extern char         gszIniFile[];           /* DS:0028 "mmtools.ini" */
extern char         gszAppName[];           /* DS:004C "VidEdit" */
extern char         gszViewClass[];         /* DS:0054 "VidEditWindow" */
extern char         gszRegisterPenApp[];    /* DS:0070 "RegisterPenApp" */
extern HOOKPROC     glpfnOldMsgFilter;      /* DS:0062 */
extern FARPROC      glpfnHelpMsgFilter;     /* DS:0066 */
extern HACCEL       ghAccel;                /* DS:006C */
extern HDC          ghdcDib;                /* DS:006E */
extern HCURSOR      ghcurCross;             /* DS:0080 */
extern HCURSOR      ghcurArrow;             /* DS:0082 */
extern WORD         gwEditor;               /* DS:0084 */
extern WORD         gwWrkInstance;          /* DS:0086 */
extern BOOL         gfWrkClient;            /* DS:0088 */
extern WORD         gwLoadedFrames;         /* DS:01B4 */
extern HWND         ghwndBlob;              /* DS:01B8 */
extern BOOL         gfInQuerySave;          /* DS:028A */
extern BOOL         gfInOpen;               /* DS:028C */
extern DWORD        gdwMidiOffset;          /* DS:04A2 */
extern HGLOBAL      ghMedidList;            /* DS:137C */
extern WORD         gcMedidList;            /* DS:137E */
extern WORD         gcToolRepeat;           /* DS:1BE2 */
extern char         gsz155[20];             /* DS:1BE4 */
extern DWORD        gdwMidiOffsetUndo;      /* DS:1C7C */
extern WORD         gcxUndo;                /* DS:2CBC */
extern WORD         gcyUndo;                /* DS:2CBE */
extern BOOL         gfUndoVideo;            /* DS:2CC0 */
extern BOOL         gfUndoAudio;            /* DS:2CC2 */
extern BOOL         gfUndoInsertMode;       /* DS:2CC4 */
extern BOOL         gfUndoDirty;            /* DS:2CC6 */
extern COLORREF     gclrBtnShadow;          /* DS:2D0A */
extern RECT         grcAppInit;             /* DS:2D22 */
extern BOOL         gfInitializing;         /* DS:2D52 */
extern WORD         gwUndoFrame;            /* DS:2D54 */
extern int          gcyPanel;               /* DS:2D56 */
extern COLORREF     gclrBtnFace;            /* DS:3072 */
extern int          gcxClient;              /* DS:307E */
extern char         gszP[5];                /* DS:3080 */
extern char         gszFileName[144];       /* DS:3094 */
extern char         gszHelpFile[80];        /* DS:3126 */
extern FARPROC      glpfnMediaWaveCopyHWAVE;/* DS:3184 */
extern BOOL         gfWin31;                /* DS:3188 */
extern WORD         gwRedoFrame;            /* DS:31A4 */
extern char         gszK[5];                /* DS:31A6 */
extern COLORREF     gclrBtnText;            /* DS:31B6 */
extern WORD         gwRedoSelEnd;           /* DS:31BE */
extern WORD         gwRedoSelStart;         /* DS:31F0 */
extern HWND         ghwndStatus;            /* DS:321E */
extern COLORREF     gclrBtnHighlight;       /* DS:3220 */
extern DWORD        gdwWinFlags;            /* DS:325A */
extern int          gcyClient;              /* DS:325E */
extern WORD         gwUndoSelEnd;           /* DS:3276 */
extern WORD         gwUndoSelStart;         /* DS:3278 */

/* ------------------------------------------------------------------ */
/* videdit.c (s01:0010-1311)                                           */
/* ------------------------------------------------------------------ */

int     PASCAL WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpszCmdLine, int nCmdShow);
BOOL    FAR KbdNavigate(MSG msg);
LRESULT FAR PASCAL InitMenuPopup(HWND hwnd, HMENU hMenu, BOOL fSystemMenu, int iPopup);
LRESULT FAR PASCAL DoCommand(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
void    FAR SetDirty(void);
void    FAR UpdateTitle(void);
void    FAR UpdateTitles(void);
BOOL    FAR QuerySave(void);
void    FAR PASCAL DropFiles(HWND hwnd, HANDLE hDrop);
int     FAR _cdecl DosRename(LPSTR lpszOld, LPSTR lpszNew);
int     FAR _cdecl DosDelete(LPSTR lpszFile);

/* ------------------------------------------------------------------ */
/* undo.c (s01:1312-178F)                                              */
/* ------------------------------------------------------------------ */

BOOL    FAR PASCAL SetUndoMenu(HMENU hMenu, UINT id);
void    FAR PASCAL SetUndo(UINT wType);
void    FAR PASCAL SetUndoSelection(WORD wSelStart, WORD wSelEnd, WORD wFrame);
void    FAR PASCAL SetRedoSelection(WORD wSelStart, WORD wSelEnd, WORD wFrame);
void    FAR Undo(void);
void    FAR PASCAL SetUndoTracks(BOOL fVideo, BOOL fAudio, BOOL fInsertMode);

/* ------------------------------------------------------------------ */
/* hmem.c (s01:1790-195D)                                              */
/* ------------------------------------------------------------------ */

LPVOID  FAR PASCAL hmemmove(LPVOID lpDst, LPVOID lpSrc, DWORD cb);

/* ------------------------------------------------------------------ */
/* file.c (s01:195E-29DB)                                              */
/* ------------------------------------------------------------------ */

BOOL    FAR PASCAL AddMedid(MEDID medid);
void    FAR PASCAL NewFile(BOOL fDefaultSize);
BOOL    FAR PASCAL OpenFileByName(LPSTR lpszFile);
void    FAR FileOpen(void);
void    FAR PASCAL FileClear(BOOL fUnused);
BOOL    FAR PASCAL FileSave(BOOL fSaveAs);
void    FAR FileRevert(void);
void    FAR FileInsert(void);
void    FAR FileExtract(void);
BOOL    FAR RunVidCap(void);
BOOL    FAR IsAviFile(void);

/* ------------------------------------------------------------------ */
/* muldiv32.c (s01:29DC-2FEB)                                          */
/* ------------------------------------------------------------------ */

LONG    FAR PASCAL muldiv32(LONG a, LONG b, LONG c);        /* a * b / c, rounded */
LONG    FAR PASCAL muldivru32(LONG a, LONG b, LONG c);      /* rounded up */
LONG    FAR PASCAL muldivrd32(LONG a, LONG b, LONG c);      /* rounded down */

/* ------------------------------------------------------------------ */
/* init.c (s02)                                                        */
/* ------------------------------------------------------------------ */

void    FAR PASCAL WriteIniInt(LPSTR lpszSection, LPSTR lpszKey, int nDefault, int nValue);
void    FAR SaveSettings(void);
DWORD   FAR PASCAL HelpMsgFilter(int nCode, WORD wParam, LPMSG lpMsg);
BOOL    FAR InitHelp(void);
void    FAR TermHelp(void);
BOOL    FAR PASCAL AppInit(HINSTANCE hInst, HINSTANCE hPrev, int nCmdShow, LPSTR lpszCmdLine);
BOOL    FAR PASCAL InitStatusClasses(HINSTANCE hInst, HINSTANCE hPrev);
LONG    FAR PASCAL CreateChildWindows(HWND hwnd);
BOOL    FAR InitDibClipboard(void);
BOOL    FAR InitWave(void);
BOOL    FAR InitStub1348(void);
void    FAR InitUndo(void);

/* ------------------------------------------------------------------ */
/* term.c (s03)                                                        */
/* ------------------------------------------------------------------ */

BOOL    FAR AppTerm(void);
void    FAR TermWave(void);
void    FAR TermStub0164(void);
void    FAR TermUndo(void);

/* ------------------------------------------------------------------ */
/* midistub.c (s08)                                                    */
/* ------------------------------------------------------------------ */

int     FAR MidiNumFrames(void);
BOOL    FAR PASCAL MidiAdjustOffset(LONG lMilliseconds);
BOOL    FAR MidiSwapOffset(void);
void    FAR PASCAL MidiFrameBytes(WORD wFrame, LPDWORD lpdwBytes);
BOOL    FAR PASCAL MidiGetRangeElement(WORD wStart, WORD wCount, MEDID NEAR *pmedid);

/* ------------------------------------------------------------------ */
/* prefs.c (s09), goto.c (s14)                                         */
/* ------------------------------------------------------------------ */

int     FAR PASCAL Preferences(HWND hwnd);
BOOL    FAR PASCAL PrefsDlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam);
void    FAR GoTo(void);
LONG    FAR PASCAL GoToDlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam);

/* ------------------------------------------------------------------ */
/* rle.c (s13)                                                         */
/* ------------------------------------------------------------------ */

void    FAR RleDecompressDib(WORD selDst, WORD selSrc);
void    FAR RleDecompress(LPBITMAPINFOHEADER lpbi, LPVOID lpDst, LPVOID lpSrc);
BOOL    FAR PASCAL RleCheckFrame(HANDLE hdib, BOOL FAR *lpfPartial);

/* ------------------------------------------------------------------ */
/* unreferenced: arrowini.c (s10), mplayer.c (s15)                     */
/* ------------------------------------------------------------------ */

BOOL    FAR PASCAL ArrowInit10(HINSTANCE hInst);
void    FAR PASCAL PlayWithMPlayer(LPSTR lpszFile);

#endif /* _VEMAIN_H_ */
