/*
 * file.c - code segment 1 of VIDEDIT.EXE, s01:195E-29DB.
 *
 * New, Open, Save, Save As, Revert, Insert and Extract, and running
 * VidCap.  A movie is opened through the Workbench file dialogs as a
 * MediaMan element of type 'DIBS'; its AVI handler reports the streams,
 * and the first video stream and the first audio stream are loaded into
 * the frame table (segment 6) and the audio track (segment 7).  Saving
 * goes through segment 11; Save writes a temporary file on the same
 * drive and then replaces the original with it.
 *
 * Every MEDID the movie uses is kept in a global list, and the Workbench
 * is told (message 0xB100) when the movie is cleared.
 */

#include <dos.h>
#include "videdit.h"
#include "vemain.h"
#include "vedwnd.h"
#include "frames.h"
#include "edit.h"
#include "compress.h"
#include "preview.h"
#include "filedlg.h"

#define max(a, b)   (((a) > (b)) ? (a) : (b))

#define mtDIBS      medFOURCC('D', 'I', 'B', 'S')
#define mtWAVE      medFOURCC('W', 'A', 'V', 'E')
#define mtMDIB      medFOURCC('M', 'D', 'I', 'B')
#define mtMIDI      medFOURCC('M', 'I', 'D', 'I')
#define mtDSEQ      medFOURCC('D', 'S', 'E', 'Q')
#define mtAVI       medFOURCC('A', 'V', 'I', ' ')
#define mtAVI0      medFOURCC('A', 'V', 'I', '0')

/* data */
                                            /* DS:1376 gmedidMovie, DS:137A gfInFileDialog: shared.c */
HGLOBAL         ghMedidList;                /* DS:137C */
WORD            gcMedidList;                /* DS:137E */
static char     szAvi1[] = "avi";           /* DS:1380 */
static char     szAvi2[] = "avi";           /* DS:1384 */
static MEDTYPE  gamtInsert[] = { mtDIBS, mtWAVE, mtMDIB, 0 };   /* DS:1388 */
static char     szVidCap[] = "VidCap -n ";  /* DS:1398 */
WORD            gwLoadedFrames;             /* DS:01B4 */
char            gszFileName[144];           /* DS:3094 */

/*
 * s01:195E-1A39  AddMedid  (FAR PASCAL)
 *
 * Adds a MEDID to the movie's list unless it is there already; the list
 * grows by 32 entries at a time.
 */
BOOL FAR PASCAL AddMedid(MEDID medid)
{
    MEDID FAR  *lp;
    BOOL        fFound;
    WORD        i;

    fFound = FALSE;
    lp = (MEDID FAR *)GlobalLock(ghMedidList);
    if (lp == NULL)
        return FALSE;
    for (i = 0; i < gcMedidList; i++)
        if (lp[i] == medid)
            fFound = TRUE;
    if (!fFound) {
        if (gcMedidList && (gcMedidList & 0x1F) == 0) {
            GlobalUnlock(ghMedidList);
            ghMedidList = GlobalReAlloc(ghMedidList, GlobalSize(ghMedidList) + 0x80,
                                        GMEM_MOVEABLE | GMEM_SHARE);
            if (ghMedidList == NULL)
                return FALSE;
            lp = (MEDID FAR *)GlobalLock(ghMedidList);
            if (lp == NULL)
                return FALSE;
        }
        lp[gcMedidList++] = medid;
    }
    GlobalUnlock(ghMedidList);
    return TRUE;
}

/*
 * s01:1A3C-1B12  IsFileInUse  (NEAR PASCAL)
 *
 * TRUE (with a message) if any element of the file is accessed, for
 * example by another instance of VidEdit.
 */
static BOOL NEAR PASCAL IsFileInUse(LPSTR lpszFile, MEDTYPE mt)
{
    MEDID       medid;
    HANDLE      h;
    DWORD FAR  *lp;
    DWORD       i;
    BOOL        fInUse;

    fInUse = FALSE;
    medid = medLocate(lpszFile, mt, MEDF_LOCATE, NULL);
    if (medid && (h = medGetAliases(medid)) != NULL) {
        i = 0;
        lp = (DWORD FAR *)GlobalLock(h);
        if (lp[0] != 0) {
            do {
                if (medIsAccessed(lp[1 + (WORD)i]))
                    fInUse = TRUE;
                i++;
            } while (lp[0] > i);
        }
        GlobalUnlock(h);
        GlobalFree(h);
    }
    if (fInUse) {
        MessageBeep(MB_ICONEXCLAMATION);
        ErrorResBox(ghwndApp, ghInst, MB_ICONEXCLAMATION, IDS_APPTITLE, IDS_INUSE, lpszFile);
        return TRUE;
    }
    return FALSE;
}

/*
 * s01:1B14-1FDD  LoadMedid  (NEAR PASCAL)
 *
 * Inserts the first video stream and the first audio stream of a movie
 * element at wStart, replacing wLen frames, and returns the number of
 * frames inserted in *lpwFrames.  MIDI streams are noted but not used;
 * further video and audio streams are refused with a message.  For a
 * new movie (fNew) the compression options are taken from the file.
 */
static BOOL NEAR PASCAL LoadMedid(WORD wStart, WORD wLen, LPWORD lpwFrames, MEDID medid, BOOL fNew)
{
    MOVIEINFO   mi;
    WORD        wAudioFrames;
    WORD        wVideoFrames;
    BOOL        fOK;
    WORD        wMinFrames;
    MEDID       medidAudio;
    MEDID       medidMidi;
    DWORD       iStream;
    DWORD       iVideo;
    DWORD       fcc;
    BOOL        fWarnedVideo;
    BOOL        fWarnedAudio;
    BOOL        fVideo;
    BOOL        fAudio;
    WORD        sel;

    medidAudio = 0;
    medidMidi = 0;
    fOK = TRUE;
    wAudioFrames = wVideoFrames = 0;
    wMinFrames = 0;
    fWarnedVideo = fWarnedAudio = FALSE;

    UpdateWindow(ghwndApp);
    medSendMessage(medid, MED_GETMOVIEINFO, (LONG)(LPVOID)&mi, sizeof(mi));
    iVideo = mi.dwStreams;
    for (iStream = 0; iStream < mi.dwStreams; iStream++) {
        fcc = medSendMessage(medid, MED_GETSTREAMTYPE, iStream, 0L);
        if (fcc == streamtypeVIDEO) {
            if (iVideo == mi.dwStreams) {
                iVideo = iStream;
            } else if (!fWarnedVideo) {
                fWarnedVideo = TRUE;
                MessageBeep(MB_ICONEXCLAMATION);
                ErrorResBox(ghwndApp, ghInst, MB_ICONEXCLAMATION, IDS_APPTITLE, IDS_MULTIVIDEO);
            }
        } else if (fcc == streamtypeAUDIO) {
            if (medidAudio == 0) {
                medidAudio = medSendMessage(medid, MED_GETSTREAMELEMENT, 0L, iStream);
            } else if (!fWarnedAudio) {
                fWarnedAudio = TRUE;
                MessageBeep(MB_ICONEXCLAMATION);
                ErrorResBox(ghwndApp, ghInst, MB_ICONEXCLAMATION, IDS_APPTITLE, IDS_MULTIAUDIO);
            }
        } else if (fcc == mmioFOURCC('m', 'i', 'd', 's')) {
            if (medidMidi == 0)
                medidMidi = medSendMessage(medid, MED_GETSTREAMELEMENT, 0L, iStream);
        }
    }
    if (iVideo >= mi.dwStreams) {
        MessageBeep(MB_ICONEXCLAMATION);
        ErrorResBox(ghwndApp, ghInst, MB_ICONEXCLAMATION, IDS_APPTITLE, IDS_NOVIDEO);
        return FALSE;
    }

    fVideo = (fNew || gfEditVideo) ? TRUE : FALSE;
    fAudio = (fNew || gfEditAudio) ? TRUE : FALSE;
    SetUndoTracks(fVideo, fAudio, gfInsertMode);

    if (fNew) {
        GetCompressDefaults(&gCompOptions, TRUE);
        if (mi.dw14)
            gCompOptions.dwUSecPerFrame = mi.dw14;
        if (mi.dw10)
            gCompOptions.dwDataRate = mi.dw10;
        if (!(LOBYTE(mi.dwFlags) & 0x01))
            *(BYTE *)&gCompOptions.dwFlags |= COMPF_INTERLEAVE;
        else
            *(BYTE *)&gCompOptions.dwFlags &= ~(COMPF_INTERLEAVE | COMPF_DATARATE);
        if (LOBYTE(mi.dwFlags) & 0x40)
            *(BYTE *)&gCompOptions.dwFlags |= COMPF_NOPAD;
        else
            *(BYTE *)&gCompOptions.dwFlags &= ~COMPF_NOPAD;
        if (LOBYTE(mi.dwFlags2) & 0x08) {
            gCompOptions.dwQuality = mi.dw1C;
            gCompOptions.dwInterleave = mi.dw28;
            gCompOptions.dwKeyFrameEvery = mi.dw24;
            gCompOptions.dwDataRate = mi.dw10;
            if (gCompOptions.dwInterleave)
                *(BYTE *)&gCompOptions.dwFlags |= COMPF_INTERLEAVE;
        } else {
            gCompOptions.dwQuality = (DWORD)ICQUALITY_DEFAULT;
            gCompOptions.dwKeyFrameEvery = 0;
            gCompOptions.dwInterleave = (LOBYTE(mi.dwFlags) & 0x01) ? 0 : 1;
        }
    }

    if (fVideo) {
        if (fAudio && medidAudio)
            wMinFrames = WaveCountFrames(medidAudio);
        if (fNew) {
            gCompOptions.fccHandler = medSendMessage(medid, MED_GETSTREAMHANDLER, iVideo, 0L);
            gCompOptions.cbState = medSendMessage(medid, MED_GETSTREAMFORMAT, 0L, iVideo);
            if (gCompOptions.cbState) {
                ghMemTemp = GlobalAlloc(GMEM_MOVEABLE, gCompOptions.cbState);
                sel = ghMemTemp ? SELECTOROF(GlobalLock(ghMemTemp)) : 0;
                gCompOptions.lpState = MAKELP(sel, 0);
                if (gCompOptions.lpState == NULL)
                    gCompOptions.cbState = 0;
                else
                    medSendMessage(medid, MED_GETSTREAMFORMAT, (LONG)gCompOptions.lpState, iVideo);
            }
        }
        fOK = InsertVideoElement(wStart, wLen, wMinFrames, &wVideoFrames, medid, (WORD)iVideo, fNew);
        if (!fOK) {
            MessageBeep(MB_ICONEXCLAMATION);
            ErrorResBox(ghwndApp, ghInst, MB_ICONEXCLAMATION, IDS_APPTITLE, IDS_BADINSERTVIDEO);
        }
    }

    if (fAudio && fOK) {
        if (wMinFrames < wVideoFrames)
            wMinFrames = wVideoFrames;
        fOK = WaveInsertElement(wStart, wLen, wMinFrames, &wAudioFrames, medidAudio);
        if (!fOK) {
            SetUndoTracks(fVideo, FALSE, gfInsertMode);
            Undo();
            SetUndo(0);
            MessageBeep(MB_ICONEXCLAMATION);
            ErrorResBox(ghwndApp, ghInst, MB_ICONEXCLAMATION, IDS_APPTITLE, IDS_BADINSERTAUDIO);
        }
    }

    if (lpwFrames)
        *lpwFrames = max(wAudioFrames, wVideoFrames);

    if (fNew && fOK) {
        if (gCompOptions.dwDataRate == 0)
            *(BYTE *)&gCompOptions.dwFlags &= ~COMPF_DATARATE;
        else
            *(BYTE *)&gCompOptions.dwFlags |= COMPF_DATARATE;
        if (gCompOptions.dwKeyFrameEvery == 0)
            *(BYTE *)&gCompOptions.dwFlags &= ~COMPF_KEYFRAMES;
        else
            *(BYTE *)&gCompOptions.dwFlags |= COMPF_KEYFRAMES;
        if (gCompOptions.dwInterleave == 0)
            *(BYTE *)&gCompOptions.dwFlags &= ~COMPF_INTERLEAVE;
        else
            *(BYTE *)&gCompOptions.dwFlags |= COMPF_INTERLEAVE;
        if (!(*(BYTE *)&gCompOptions.dwFlags & COMPF_INTERLEAVE))
            *(BYTE *)&gCompOptions.dwFlags |= COMPF_NOPAD;
        if (gCompOptions.fccHandler == 0)
            gCompOptions.fccHandler = mmioFOURCC('D', 'I', 'B', ' ');
        if (gCompOptions.fccHandler == mmioFOURCC('n', 'o', 'n', 'e'))
            gCompOptions.fccHandler = mmioFOURCC('D', 'I', 'B', ' ');
        if (gCompOptions.fccHandler == mmioFOURCC('R', 'L', 'E', '0'))
            gCompOptions.fccHandler = mmioFOURCC('R', 'L', 'E', ' ');
        if (gCompOptions.fccHandler == mmioFOURCC('D', 'I', 'B', ' ')) {
            gCompOptions.dwKeyFrameEvery = 1;
            *(BYTE *)&gCompOptions.dwFlags |= COMPF_KEYFRAMES;
        }
        gCompOptionsSaved = gCompOptions;
        gwLoadedFrames = max(wAudioFrames, wVideoFrames);
    }
    return fOK;
}

/*
 * s01:1FE0-2046  NewFile  (FAR PASCAL)
 */
void FAR PASCAL NewFile(BOOL fDefaultSize)
{
    FileClear(FALSE);
    gszFileName[0] = '\0';
    SetLength(0, TRUE);
    SeekTo(0);
    UpdateTitles();
    GetCompressDefaults(&gCompOptions, TRUE);
    gCompOptionsSaved = gCompOptions;
    gwLoadedFrames = 0;
    if (fDefaultSize) {
        SetFrameSize(gcxDefault, gcyDefault);
        ViewLayout(FALSE);
    }
}

/*
 * s01:2048-20F9  OpenFileByName  (FAR PASCAL)
 *
 * Opens the file named (the command line, a dropped file, Revert, or
 * the file just saved).  Returns FALSE only if the file can't be opened
 * as a movie element.
 */
BOOL FAR PASCAL OpenFileByName(LPSTR lpszFile)
{
    char    achTitle[50];
    MedReturn mr;                       /* [bp-08] */
    MEDID   medid;
    WORD    id;

    LoadString(ghInst, IDS_OPENTITLE, achTitle, sizeof(achTitle));
    gfInFileDialog = TRUE;
    id = WrkOpenFileName(lpszFile, &mr, mtDIBS, 0x2001, ghwndApp, achTitle);
    medid = mr.medid;
    gfInFileDialog = FALSE;
    if (id != 1)
        return FALSE;
    FileClear(TRUE);
    SetLength(0, FALSE);
    SeekTo(0);
    gmedidMovie = medid;
    if (LoadMedid(0, 0, NULL, medid, TRUE)) {
        AddMedid(gmedidMovie);
        lstrcpy(gszFileName, lpszFile);
        UpdateTitles();
    }
    UpdateLength();
    gfDirty = FALSE;
    return TRUE;
}

/*
 * s01:20FC-21A7  FileOpen
 *
 * File Open: the Workbench open dialog, help context 120.
 */
void FAR FileOpen(void)
{
    char    achTitle[80];
    MedReturn mr;                       /* [bp-08] */
    MEDID   medid;
    WORD    id;

    gfInFileDialog = TRUE;
    gidHelpContext = 120;
    LoadString(ghInst, IDS_OPENTITLE, achTitle, sizeof(achTitle));
    id = WrkOpenDialog(&mr, mtDIBS, 0x2001, ghwndApp, achTitle);
    medid = mr.medid;
    gfInFileDialog = FALSE;
    gidHelpContext = 0;
    if (id != 1)
        return;
    FileClear(TRUE);
    SetLength(0, FALSE);
    SeekTo(0);
    gmedidMovie = medid;
    if (LoadMedid(0, 0, NULL, medid, TRUE)) {
        medGetFileName(gmedidMovie, gszFileName, sizeof(gszFileName));
        UpdateTitles();
        AddMedid(gmedidMovie);
    }
    UpdateLength();
    gfDirty = FALSE;
}

/*
 * s01:21A8-2294  FileClear  (FAR PASCAL)
 *
 * Releases the movie: broadcasts 0xB100 for every MEDID used, starts a
 * new list, and empties undo, the selection, the frames, the audio and
 * the preview.  The parameter is not used.
 */
void FAR PASCAL FileClear(BOOL fUnused)
{
    MEDID FAR  *lp;
    HCURSOR     hcur;
    WORD        i;

    hcur = NULL;
    if (ghMedidList && (lp = (MEDID FAR *)GlobalLock(ghMedidList)) != NULL) {
        for (i = 0; i < gcMedidList; i++)
            WrkBroadcastMessage(0xB100L, ghInst, lp[i]);
        GlobalUnlock(ghMedidList);
        GlobalFree(ghMedidList);
    }
    ghMedidList = GlobalAlloc(GMEM_MOVEABLE | GMEM_SHARE, 0x80L);
    gcMedidList = 0;

    if (gwLengthShown)
        hcur = SetCursor(LoadCursor(NULL, IDC_WAIT));
    SetUndo(0);
    SetSelection(0xFFFF, 0xFFFF);
    FreeAllFrames();
    WaveRelease();
    ClosePreview();
    gfInMemory = FALSE;
    if (gmedidMovie) {
        medRelease(gmedidMovie, 0L);
        gmedidMovie = 0;
    }
    if (hcur)
        SetCursor(hcur);
}

static BOOL NEAR PASCAL DoSave(UINT id);

/*
 * s01:2296-233F  FileSave  (FAR PASCAL)
 *
 * Save asks first if the file was a capture file.
 */
BOOL FAR PASCAL FileSave(BOOL fSaveAs)
{
    char        achMsg[240];
    char        achCaption[80];
    MOVIEINFO   mi;

    if (!fSaveAs && gmedidMovie) {
        medSendMessage(gmedidMovie, MED_GETMOVIEINFO, (LONG)(LPVOID)&mi, sizeof(mi));
        if (HIWORD(mi.dwFlags) & 0x0001) {
            LoadString(ghInst, IDS_CAPTUREFILE, achCaption, sizeof(achCaption));
            wsprintf(achMsg, achCaption, (LPSTR)gszFileName);
            LoadString(ghInst, IDS_APPTITLE, achCaption, sizeof(achCaption));
            MessageBeep(MB_ICONQUESTION);
            if (MessageBox(ghwndApp, achMsg, achCaption, MB_YESNO | MB_ICONQUESTION) == IDNO)
                return FALSE;
        }
    }
    return DoSave(fSaveAs ? IDM_SAVEAS : IDM_SAVE);
}

/*
 * s01:2342-260F  DoSave  (NEAR PASCAL)
 *
 * Save keeps the file's type (AVI); Save As asks for a name and type
 * (help context 123, or 124 for "save copy").  A new name is refused if
 * the file is in use.  Save writes to a temporary file on the drive of
 * the original, deletes the original and renames the temporary file;
 * the movie is then reopened from the saved file (except when saving
 * from the "save changes?" prompt, unless that came from File Open).
 */
static BOOL NEAR PASCAL DoSave(UINT id)
{
    char        achFile[144];
    char        achDir[144];
    char        achTitle[40];
    char        achName[10];
    char        achExt[6];
    MEDTYPE     mt;
    char        achDrive[4];
    BOOL        fCopy;
    unsigned    uAttr;
    HMMIO       hmmio;

    uAttr = 0;
    fCopy = FALSE;
    if (!gwLength)
        return TRUE;

    if (id == IDM_SAVE) {
        if (gszFileName[0] == '\0')
            id = IDM_SAVEAS;
        if (gmedidMovie) {
            mt = medGetPhysicalType(gmedidMovie);
            if (mt == mtAVI0)
                mt = mtAVI;
            if (mt != mtAVI && mt != mtAVI0)
                id = IDM_SAVEAS;
        } else {
            id = IDM_SAVEAS;
        }
    }

    if (id != IDM_SAVE) {
        if (gszFileName[0]) {
            SplitPath(gszFileName, achDrive, achDir, achName, achExt);
            MakePath(achFile, achDrive, achDir, achName, szAvi1);
        } else {
            achFile[0] = '\0';
        }
        LoadString(ghInst, IDS_SAVETITLE, achTitle, sizeof(achTitle));
        gidHelpContext = fCopy ? 124 : 123;
        if (SaveAsDialog(achFile, sizeof(achFile), &mt, &fCopy, 0, ghwndApp, achTitle) != 1) {
            gidHelpContext = 0;
            return FALSE;
        }
        gidHelpContext = 0;
        if (lstrcmp(achFile, gszFileName) != 0) {
            if (IsFileInUse(achFile, mtDIBS))
                return FALSE;
        } else {
            id = IDM_SAVE;
        }
    }

    if (id == IDM_SAVE) {
        _dos_getfileattr(gszFileName, &uAttr);
        if (uAttr & _A_RDONLY) {
            ErrorResBox(ghwndApp, ghInst, MB_ICONEXCLAMATION, IDS_APPTITLE, IDS_READONLY, (LPSTR)gszFileName);
            return FALSE;
        }
    } else {
        hmmio = mmioOpen(achFile, NULL, MMIO_CREATE | MMIO_WRITE);
        if (hmmio == NULL) {
            ErrorResBox(ghwndApp, ghInst, MB_ICONEXCLAMATION, IDS_APPTITLE, IDS_READONLY, (LPSTR)achFile);
            return FALSE;
        }
        mmioClose(hmmio, 0);
    }

    if (id == IDM_SAVE) {
        SplitPath(gszFileName, achDrive, NULL, NULL, NULL);
        GetTempFileName(achDrive[0] | TF_FORCEDRIVE, szAvi2, 0, achFile);
    }

    if (!SaveMovie(0, gwLengthShown, achFile, mt, 1, 1, 0))
        return FALSE;

    if (id == IDM_SAVEAS && !fCopy) {
        lstrcpy(gszFileName, achFile);
        UpdateTitles();
        if (gfInQuerySave && !gfInOpen)
            return TRUE;
        goto reopen;
    }

    if (id == IDM_SAVE && !fCopy) {
        ClipboardMakeStatic();
        FileClear(TRUE);
        if (DosDelete(gszFileName) == 0 && DosRename(achFile, gszFileName) == 0) {
            if (gfInQuerySave && !gfInOpen)
                return TRUE;
            goto reopen;
        }
        MessageBeep(MB_ICONEXCLAMATION);
        ErrorResBox(ghwndApp, ghInst, MB_ICONEXCLAMATION, IDS_APPTITLE, IDS_NOOVERWRITE, (LPSTR)achFile);
        NewFile(TRUE);
        return FALSE;
    }
    return TRUE;

reopen:
    if (!OpenFileByName(gszFileName))
        NewFile(TRUE);
    return TRUE;
}

/*
 * s01:2612-2667  FileRevert
 */
void FAR FileRevert(void)
{
    char achName[14];
    char achExt[6];

    SplitPath(gszFileName, NULL, NULL, achName, achExt);
    lstrcat(achName, achExt);
    if (ErrorResBox(ghwndApp, ghInst, MB_OKCANCEL | MB_ICONQUESTION, IDS_APPTITLE, IDS_REVERT,
                    (LPSTR)achName) == IDOK)
        OpenFileByName(gszFileName);
}

/*
 * s01:2668-27C0  FileInsert
 *
 * Inserts a movie ('DIBS'), a picture ('MDIB') or a sound ('WAVE') at
 * the selection (replacing it) or at the current frame.  Help context
 * 121.
 */
void FAR FileInsert(void)
{
    char    achTitle[40];
    MedReturn mr;
    MEDID   medid;
    MEDTYPE mt;
    WORD    wSelStart;
    WORD    wSelEnd;
    WORD    wFrames;
    WORD    n;

    LoadString(ghInst, IDS_INSERTTITLE, achTitle, sizeof(achTitle));
    gidHelpContext = 121;
    n = InsertFileDialog(&mr, gamtInsert, 0x2001, ghwndApp, achTitle);
    medid = mr.medid;
    gidHelpContext = 0;
    if (n != 1)
        return;

    SetUndo(IDM_INSERT);
    mt = medGetLogicalType(medid);
    GetSelection(&wSelStart, &wSelEnd, 0);
    if (wSelStart == 0xFFFF) {
        n = 0;
        wSelStart = gwCurFrame;
        SetUndoSelection(0xFFFF, 0xFFFF, wSelStart);
    } else {
        n = wSelEnd - wSelStart;
        SetUndoSelection(wSelStart, wSelEnd, gwCurFrame);
    }
    SetSelection(0xFFFF, 0xFFFF);

    switch (mt) {
    case mtMDIB:
        SetUndoTracks(TRUE, FALSE, gfInsertMode);
        InsertStillFrame(wSelStart, n, &wFrames, medid);
        break;
    case mtWAVE:
        SetUndoTracks(FALSE, TRUE, gfInsertMode);
        WaveInsertElement(wSelStart, n, 0, &wFrames, medid);
        break;
    case mtDIBS:
        LoadMedid(wSelStart, n, &wFrames, medid, FALSE);
        break;
    default:
        goto done;
    }
    AddMedid(medid);
done:
    SeekTo(wFrames + wSelStart);
    UpdateLength();
    SetRedoSelection(0xFFFF, 0xFFFF, gwCurFrame);
    medRelease(medid, 0L);
}

/*
 * s01:27C2-2941  FileExtract
 *
 * Saves part of the movie in a new file: the selection as a movie
 * ('DIBS'), sound ('WAVE') or MIDI, or one frame as a picture ('MDIB';
 * an MDIB file is written as a one-frame DIB sequence).  Help context
 * 122.  MIDI is offered only if the MIDI track (a stub) has frames.
 */
void FAR FileExtract(void)
{
    char    achFile[144];
    char    achTitle[40];
    MEDTYPE amt[5];
    MEDTYPE mtLogical;
    MEDTYPE mtPhysical;
    WORD    wEnd;
    WORD    wStart;
    WORD    wCount;
    int     i;

    amt[0] = mtDIBS;
    i = 1;
    if (WaveNumFrames()) {
        amt[1] = mtWAVE;
        i = 2;
    }
    amt[i++] = mtMDIB;
    if (MidiNumFrames())
        amt[i++] = mtMIDI;
    amt[i] = 0;

    LoadString(ghInst, IDS_EXTRACTTITLE, achTitle, sizeof(achTitle));
    gidHelpContext = 122;
    i = ExtractFileDialog(amt, achFile, sizeof(achFile), &mtLogical, &mtPhysical, &wStart, &wEnd,
                   0, ghwndApp, achTitle);
    gidHelpContext = 0;
    if (i != 1)
        return;
    if (IsFileInUse(achFile, mtLogical))
        return;

    wCount = wEnd - wStart;
    switch (mtLogical) {
    case mtMDIB:
        if (mtPhysical == mtMDIB)
            SaveMovie(wStart, 1, achFile, mtDSEQ, 1, 1, 1);
        else
            SaveFrame(wStart, achFile, mtPhysical);
        break;
    case mtWAVE:
        SaveAudio(wStart, wCount, achFile, mtPhysical);
        break;
    case mtMIDI:
        SaveMidi(wStart, wCount, achFile, mtPhysical);
        break;
    case mtDIBS:
        SaveMovie(wStart, wCount, achFile, mtPhysical, 1, 1, 1);
        break;
    }
}

/*
 * s01:2942-29AB  RunVidCap
 *
 * File Capture Video.  The command line is just "VidCap -n " (no file
 * name follows the trailing space).
 */
BOOL FAR RunVidCap(void)
{
    char    achCmd[128];
    HCURSOR hcur;
    BOOL    fOK;

    fOK = TRUE;
    lstrcpy(achCmd, szVidCap);
    hcur = SetCursor(LoadCursor(NULL, IDC_WAIT));
    if (WinExec(achCmd, SW_SHOWNORMAL) < 32) {
        MessageBeep(MB_ICONEXCLAMATION);
        ErrorResBox(ghwndApp, ghInst, MB_ICONEXCLAMATION, IDS_APPTITLE, IDS_NOVIDCAP);
        fOK = FALSE;
    }
    SetCursor(hcur);
    return fOK;
}

/*
 * s01:29AC-29DB  IsAviFile
 *
 * TRUE if the open movie is an AVI file ('AVI ' or 'AVI0').
 */
BOOL FAR IsAviFile(void)
{
    MEDTYPE mt;

    if (!gmedidMovie)
        return FALSE;
    mt = medGetPhysicalType(gmedidMovie);
    return (mt == mtAVI || mt == mtAVI0) ? TRUE : FALSE;
}
