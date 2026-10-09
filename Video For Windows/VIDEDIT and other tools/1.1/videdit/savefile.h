/*
 * savefile.h - saving through MediaMan (savefile.c, segment 11)
 */

#ifndef _SAVEFILE_H_
#define _SAVEFILE_H_

WORD  FAR PASCAL SaveMovie(WORD wStart, WORD wCount, PSTR pszFile, MEDTYPE mtSave, WORD wUnused,
                          BOOL fAudio, BOOL fMidi);
WORD  FAR PASCAL SaveAudio(WORD wStart, WORD wCount, PSTR pszFile, MEDTYPE mtSave);
WORD  FAR PASCAL SaveMidi(WORD wStart, WORD wCount, PSTR pszFile, MEDTYPE mtSave);
WORD  FAR PASCAL SaveFrame(WORD wFrame, PSTR pszFile, MEDTYPE mtSave);
DWORD FAR PASCAL _loadds SaveHandler(MEDID medid, MEDMSG msg, MEDINFO medinfo, LONG lParam1, LONG lParam2);

#endif /* _SAVEFILE_H_ */
