/*
 * compress.h - compression options (compress.c, segment 12)
 */

#ifndef _COMPRESS_H_
#define _COMPRESS_H_

/*
 * The options used when saving.  The current ones are at DS:322A
 * (gCompOptions), a copy at DS:2D2A (gCompOptionsSaved).
 */
typedef struct {
    DWORD   dwFlags;                    /* 00 COMPF_* */
    DWORD   dwUSecPerFrame;             /* 04 */
    DWORD   dwDataRate;                 /* 08 bytes per second */
    FOURCC  fccHandler;                 /* 0C video compressor */
    DWORD   cbState;                    /* 10 compressor state */
    LPVOID  lpState;                    /* 14 GlobalAlloc'd */
    DWORD   dwQuality;                  /* 18 0..10000 */
    DWORD   dwKeyFrameEvery;            /* 1C */
    DWORD   dwInterleave;               /* 20 audio every n frames */
} COMPOPTIONS, FAR *LPCOMPOPTIONS;      /* 0x24 bytes */

#define COMPF_NOPAD             0x0001  /* no padding for CD-ROM */
#define COMPF_INTERLEAVE        0x0002
#define COMPF_DATARATE          0x0004
#define COMPF_KEYFRAMES         0x0008

extern COMPOPTIONS  gCompOptions;       /* DS:322A (defined in shared.c) */
extern COMPOPTIONS  gCompOptionsSaved;  /* DS:2D2A (defined in shared.c) */

void  FAR _cdecl    hmemset(BYTE _huge *hp, BYTE b, LONG cb);
LPSTR FAR PASCAL    GetCompressorName(BOOL fCurrent);
void  FAR PASCAL    GetCompressDefaults(LPCOMPOPTIONS lpopt, BOOL fFrameRate);
void  FAR PASCAL    DoCompressionOptions(HWND hwndParent);
LONG  FAR PASCAL _loadds CompressDlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam);
BOOL  FAR PASCAL    SetUSecPerFrame(DWORD dwUSecPerFrame);
BOOL  FAR           SwapUSecPerFrame(void);

#endif /* _COMPRESS_H_ */
