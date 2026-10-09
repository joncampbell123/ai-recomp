/*
 * msvideo.h - stand-in for the MSVIDEO headers of the Video for Windows
 * 1.1 DK (MSVIDEO.H, COMPMAN.H, COMPDDK.H, DRAWDIB.H), written for the
 * Open Watcom build.
 *
 * Only what the VfW 1.1 tools use: video capture driver access, the
 * installable compression manager and DrawDib.  Win16 layouts.  The
 * functions are imported from MSVIDEO.DLL by ordinal in the linker
 * directives (ordinals in the comments); the argument sizes were checked
 * against the RETF of each export.
 */

#ifndef _INC_MSVIDEO
#define _INC_MSVIDEO

#ifndef VFWAPI
#define VFWAPI                  FAR PASCAL
#endif
#define VFWAPIV                 FAR CDECL

#ifndef mmioFOURCC
#define mmioFOURCC(ch0, ch1, ch2, ch3) \
    ((DWORD)(BYTE)(ch0) | ((DWORD)(BYTE)(ch1) << 8) | \
     ((DWORD)(BYTE)(ch2) << 16) | ((DWORD)(BYTE)(ch3) << 24))
#endif

/* ------------------------------------------------------------------ */
/* video capture drivers (MSVIDEO.H)                                   */
/* ------------------------------------------------------------------ */

DECLARE_HANDLE(HVIDEO);
typedef HVIDEO FAR *LPHVIDEO;

#define DV_ERR_OK               0
#define DV_ERR_BASE             1
#define DV_ERR_NONSPECIFIC      (DV_ERR_BASE)
#define DV_ERR_BADFORMAT        (DV_ERR_BASE + 1)
#define DV_ERR_STILLPLAYING     (DV_ERR_BASE + 2)
#define DV_ERR_UNPREPARED       (DV_ERR_BASE + 3)
#define DV_ERR_SYNC             (DV_ERR_BASE + 4)
#define DV_ERR_TOOMANYCHANNELS  (DV_ERR_BASE + 5)
#define DV_ERR_NOTDETECTED      (DV_ERR_BASE + 6)
#define DV_ERR_BADINSTALL       (DV_ERR_BASE + 7)
#define DV_ERR_CREATEPALETTE    (DV_ERR_BASE + 8)
#define DV_ERR_SIZEFIELD        (DV_ERR_BASE + 9)
#define DV_ERR_PARAM1           (DV_ERR_BASE + 10)
#define DV_ERR_PARAM2           (DV_ERR_BASE + 11)
#define DV_ERR_CONFIG1          (DV_ERR_BASE + 12)
#define DV_ERR_CONFIG2          (DV_ERR_BASE + 13)
#define DV_ERR_FLAGS            (DV_ERR_BASE + 14)
#define DV_ERR_13               (DV_ERR_BASE + 15)
#define DV_ERR_NOTSUPPORTED     (DV_ERR_BASE + 16)
#define DV_ERR_NOMEM            (DV_ERR_BASE + 17)
#define DV_ERR_ALLOCATED        (DV_ERR_BASE + 18)
#define DV_ERR_BADDEVICEID      (DV_ERR_BASE + 19)
#define DV_ERR_INVALHANDLE      (DV_ERR_BASE + 20)
#define DV_ERR_BADERRNUM        (DV_ERR_BASE + 21)
#define DV_ERR_NO_BUFFERS       (DV_ERR_BASE + 22)
#define DV_ERR_MEM_CONFLICT     (DV_ERR_BASE + 23)
#define DV_ERR_IO_CONFLICT      (DV_ERR_BASE + 24)
#define DV_ERR_DMA_CONFLICT     (DV_ERR_BASE + 25)
#define DV_ERR_INT_CONFLICT     (DV_ERR_BASE + 26)
#define DV_ERR_PROTECT_ONLY     (DV_ERR_BASE + 27)
#define DV_ERR_LASTERROR        (DV_ERR_BASE + 27)
#define DV_ERR_USER_MSG         (DV_ERR_BASE + 1000)

/* stream callback messages */
#ifndef MM_DRVM_OPEN
#define MM_DRVM_OPEN            0x3D0
#define MM_DRVM_CLOSE           0x3D1
#define MM_DRVM_DATA            0x3D2
#define MM_DRVM_ERROR           0x3D3
#endif
#define DV_VM_OPEN              MM_DRVM_OPEN
#define DV_VM_CLOSE             MM_DRVM_CLOSE
#define DV_VM_DATA              MM_DRVM_DATA
#define DV_VM_ERROR             MM_DRVM_ERROR

typedef struct videohdr_tag {
    LPBYTE      lpData;                 /* 00 */
    DWORD       dwBufferLength;         /* 04 */
    DWORD       dwBytesUsed;            /* 08 */
    DWORD       dwTimeCaptured;         /* 0C ms from the start of the stream */
    DWORD       dwUser;                 /* 10 */
    DWORD       dwFlags;                /* 14 VHDR_* */
    DWORD       dwReserved[4];          /* 18 */
} VIDEOHDR, NEAR *PVIDEOHDR, FAR *LPVIDEOHDR;   /* 0x28 bytes */

#define VHDR_DONE               0x00000001L
#define VHDR_PREPARED           0x00000002L
#define VHDR_INQUEUE            0x00000004L
#define VHDR_KEYFRAME           0x00000008L

typedef struct channel_caps_tag {
    DWORD       dwFlags;                /* VCAPS_* */
    DWORD       dwSrcRectXMod;
    DWORD       dwSrcRectYMod;
    DWORD       dwSrcRectWidthMod;
    DWORD       dwSrcRectHeightMod;
    DWORD       dwDstRectXMod;
    DWORD       dwDstRectYMod;
    DWORD       dwDstRectWidthMod;
    DWORD       dwDstRectHeightMod;
} CHANNEL_CAPS, NEAR *PCHANNEL_CAPS, FAR *LPCHANNEL_CAPS;  /* 0x24 bytes */

#define VCAPS_OVERLAY           0x00000001L
#define VCAPS_SRC_CAN_CLIP      0x00000002L
#define VCAPS_DST_CAN_CLIP      0x00000004L
#define VCAPS_CAN_SCALE         0x00000008L

/* channels for videoOpen */
#define VIDEO_EXTERNALIN        0x0001
#define VIDEO_EXTERNALOUT       0x0002
#define VIDEO_IN                0x0004
#define VIDEO_OUT               0x0008

#define VIDEO_DLG_QUERY         0x0010

#define VIDEO_CONFIGURE_QUERY       0x8000
#define VIDEO_CONFIGURE_SET         0x1000
#define VIDEO_CONFIGURE_GET         0x2000
#define VIDEO_CONFIGURE_QUERYSIZE   0x0001
#define VIDEO_CONFIGURE_CURRENT     0x0010
#define VIDEO_CONFIGURE_NOMINAL     0x0020
#define VIDEO_CONFIGURE_MIN         0x0040
#define VIDEO_CONFIGURE_MAX         0x0080

#define DVM_USER                0x4000
#define DVM_CONFIGURE_START     0x1000
#define DVM_CONFIGURE_END       0x1FFF
#define DVM_PALETTE             (DVM_CONFIGURE_START + 1)
#define DVM_FORMAT              (DVM_CONFIGURE_START + 2)
#define DVM_PALETTERGB555       (DVM_CONFIGURE_START + 3)
#define DVM_SRC_RECT            (DVM_CONFIGURE_START + 4)
#define DVM_DST_RECT            (DVM_CONFIGURE_START + 5)

DWORD VFWAPI videoGetErrorText(HVIDEO hVideo, UINT wError, LPSTR lpText, UINT wSize);   /* 21 */
DWORD VFWAPI videoOpen(LPHVIDEO lphVideo, DWORD dwDevice, DWORD dwFlags);               /* 28 */
DWORD VFWAPI videoClose(HVIDEO hVideo);                                                 /* 29 */
DWORD VFWAPI videoDialog(HVIDEO hVideo, HWND hWndParent, DWORD dwFlags);                /* 30 */
DWORD VFWAPI videoFrame(HVIDEO hVideo, LPVIDEOHDR lpVHdr);                              /* 31 */
DWORD VFWAPI videoConfigure(HVIDEO hVideo, UINT msg, DWORD dwFlags, LPDWORD lpdwReturn,
                            LPVOID lpData1, DWORD dwSize1, LPVOID lpData2, DWORD dwSize2); /* 32 */
DWORD VFWAPI videoGetChannelCaps(HVIDEO hVideo, LPCHANNEL_CAPS lpChannelCaps, DWORD dwSize); /* 34 */
DWORD VFWAPI videoUpdate(HVIDEO hVideo, HWND hWnd, HDC hDC);                            /* 35 */
DWORD VFWAPI videoStreamAddBuffer(HVIDEO hVideo, LPVIDEOHDR lpVHdr, DWORD dwSize);      /* 40 */
DWORD VFWAPI videoStreamFini(HVIDEO hVideo);                                            /* 41 */
DWORD VFWAPI videoStreamGetError(HVIDEO hVideo, LPDWORD lpdwErrorFirst, LPDWORD lpdwErrorLast); /* 42 */
DWORD VFWAPI videoStreamInit(HVIDEO hVideo, DWORD dwMicroSecPerFrame, DWORD dwCallback,
                             DWORD dwCallbackInst, DWORD dwFlags);                      /* 44 */
DWORD VFWAPI videoStreamPrepareHeader(HVIDEO hVideo, LPVIDEOHDR lpVHdr, DWORD dwSize);  /* 46 */
DWORD VFWAPI videoStreamReset(HVIDEO hVideo);                                           /* 47 */
DWORD VFWAPI videoStreamStart(HVIDEO hVideo);                                           /* 49 */
DWORD VFWAPI videoStreamStop(HVIDEO hVideo);                                            /* 50 */
DWORD VFWAPI videoStreamUnprepareHeader(HVIDEO hVideo, LPVIDEOHDR lpVHdr, DWORD dwSize); /* 51 */
DWORD VFWAPI videoMessage(HVIDEO hVideo, UINT msg, DWORD dwP1, DWORD dwP2);             /* 60 */

/* ------------------------------------------------------------------ */
/* DrawDib (DRAWDIB.H)                                                 */
/* ------------------------------------------------------------------ */

typedef HANDLE HDRAWDIB;

#define DDF_UPDATE              0x0002
#define DDF_SAME_HDC            0x0004
#define DDF_SAME_DRAW           0x0008
#define DDF_DONTDRAW            0x0010
#define DDF_ANIMATE             0x0020
#define DDF_BUFFER              0x0040
#define DDF_JUSTDRAWIT          0x0080
#define DDF_FULLSCREEN          0x0100
#define DDF_BACKGROUNDPAL       0x0200
#define DDF_NOTKEYFRAME         0x0400
#define DDF_HURRYUP             0x0800
#define DDF_HALFTONE            0x1000
#define DDF_PREROLL             DDF_DONTDRAW
#define DDF_SAME_DIB            DDF_SAME_DRAW
#define DDF_SAME_SIZE           DDF_SAME_DRAW

HDRAWDIB VFWAPI DrawDibOpen(void);                                                      /* 102 */
BOOL     VFWAPI DrawDibClose(HDRAWDIB hdd);                                             /* 103 */
BOOL     VFWAPI DrawDibDraw(HDRAWDIB hdd, HDC hdc, int xDst, int yDst, int dxDst, int dyDst,
                            LPBITMAPINFOHEADER lpbi, LPVOID lpBits,
                            int xSrc, int ySrc, int dxSrc, int dySrc, UINT wFlags);    /* 106 */
HPALETTE VFWAPI DrawDibGetPalette(HDRAWDIB hdd);                                        /* 108 */
UINT     VFWAPI DrawDibRealize(HDRAWDIB hdd, HDC hdc, BOOL fBackground);                /* 112 */
LPVOID   VFWAPI DrawDibGetBuffer(HDRAWDIB hdd, LPBITMAPINFOHEADER lpbi, DWORD dwSize, DWORD dwFlags); /* 120 */

/* ------------------------------------------------------------------ */
/* installable compressors (COMPMAN.H, COMPDDK.H)                      */
/* ------------------------------------------------------------------ */

DECLARE_HANDLE(HIC);

#define ICTYPE_VIDEO            mmioFOURCC('v', 'i', 'd', 'c')
#define ICTYPE_AUDIO            mmioFOURCC('a', 'u', 'd', 'c')

#define ICERR_OK                0L
#define ICERR_DONTDRAW          1L
#define ICERR_NEWPALETTE        2L
#define ICERR_GOTOKEYFRAME      3L
#define ICERR_UNSUPPORTED       (-1L)
#define ICERR_BADFORMAT         (-2L)
#define ICERR_MEMORY            (-3L)
#define ICERR_INTERNAL          (-4L)
#define ICERR_BADFLAGS          (-5L)
#define ICERR_BADPARAM          (-6L)
#define ICERR_BADSIZE           (-7L)
#define ICERR_BADHANDLE         (-8L)
#define ICERR_CANTUPDATE        (-9L)
#define ICERR_ABORT             (-10L)
#define ICERR_ERROR             (-100L)
#define ICERR_BADBITDEPTH       (-200L)
#define ICERR_BADIMAGESIZE      (-201L)
#define ICERR_CUSTOM            (-400L)

#define ICMODE_COMPRESS         1
#define ICMODE_DECOMPRESS       2
#define ICMODE_FASTDECOMPRESS   3
#define ICMODE_QUERY            4
#define ICMODE_FASTCOMPRESS     5
#define ICMODE_DRAW             8

#define ICQUALITY_LOW           0
#define ICQUALITY_HIGH          10000
#define ICQUALITY_DEFAULT       (-1)

#ifndef DRV_USER
#define DRV_USER                0x4000
#endif
#define ICM_USER                (DRV_USER + 0x0000)
#define ICM_RESERVED            (DRV_USER + 0x1000)

#define ICM_GETSTATE            (ICM_RESERVED + 0)
#define ICM_SETSTATE            (ICM_RESERVED + 1)
#define ICM_GETINFO             (ICM_RESERVED + 2)
#define ICM_CONFIGURE           (ICM_RESERVED + 10)
#define ICM_ABOUT               (ICM_RESERVED + 11)
#define ICM_GETDEFAULTQUALITY   (ICM_RESERVED + 30)
#define ICM_GETQUALITY          (ICM_RESERVED + 31)
#define ICM_SETQUALITY          (ICM_RESERVED + 32)

#define ICM_COMPRESS_GET_FORMAT     (ICM_USER + 4)
#define ICM_COMPRESS_GET_SIZE       (ICM_USER + 5)
#define ICM_COMPRESS_QUERY          (ICM_USER + 6)
#define ICM_COMPRESS_BEGIN          (ICM_USER + 7)
#define ICM_COMPRESS                (ICM_USER + 8)
#define ICM_COMPRESS_END            (ICM_USER + 9)
#define ICM_DECOMPRESS_GET_FORMAT   (ICM_USER + 10)
#define ICM_DECOMPRESS_QUERY        (ICM_USER + 11)
#define ICM_DECOMPRESS_BEGIN        (ICM_USER + 12)
#define ICM_DECOMPRESS              (ICM_USER + 13)
#define ICM_DECOMPRESS_END          (ICM_USER + 14)
#define ICM_DECOMPRESS_GET_PALETTE  (ICM_USER + 30)
#define ICM_GETBUFFERSWANTED        (ICM_USER + 41)
#define ICM_GETDEFAULTKEYFRAMERATE  (ICM_USER + 42)

#define ICMF_CONFIGURE_QUERY    0x00000001L
#define ICMF_ABOUT_QUERY        0x00000001L

typedef struct {
    DWORD   dwSize;                     /* 00 sizeof(ICINFO) */
    DWORD   fccType;                    /* 04 */
    DWORD   fccHandler;                 /* 08 */
    DWORD   dwFlags;                    /* 0C VIDCF_* */
    DWORD   dwVersion;                  /* 10 */
    DWORD   dwVersionICM;               /* 14 */
    char    szName[16];                 /* 18 */
    char    szDescription[128];         /* 28 */
    char    szDriver[128];              /* A8 */
} ICINFO;                               /* 0x128 bytes */

#define VIDCF_QUALITY           0x0001
#define VIDCF_CRUNCH            0x0002
#define VIDCF_TEMPORAL          0x0004
#define VIDCF_COMPRESSFRAMES    0x0008
#define VIDCF_DRAW              0x0010
#define VIDCF_FASTTEMPORALC     0x0020
#define VIDCF_FASTTEMPORALD     0x0080

#define ICCOMPRESS_KEYFRAME     0x00000001L
#define ICDECOMPRESS_HURRYUP    0x80000000L

typedef struct {
    LONG            cbSize;             /* 00 */
    DWORD           dwFlags;            /* 04 ICMF_COMPVARS_VALID */
    HIC             hic;                /* 08 */
    DWORD           fccType;            /* 0A */
    DWORD           fccHandler;         /* 0E */
    LPBITMAPINFO    lpbiIn;             /* 12 */
    LPBITMAPINFO    lpbiOut;            /* 16 */
    LPVOID          lpBitsOut;          /* 1A */
    LPVOID          lpBitsPrev;         /* 1E */
    LONG            lFrame;             /* 22 */
    LONG            lKey;               /* 26 */
    LONG            lDataRate;          /* 2A */
    LONG            lQ;                 /* 2E */
    LONG            lKeyCount;          /* 32 */
    LPVOID          lpState;            /* 36 */
    LONG            cbState;            /* 3A */
} COMPVARS, FAR *PCOMPVARS;             /* 0x3E bytes */

#define ICMF_COMPVARS_VALID     0x00000001L

#define ICMF_CHOOSE_KEYFRAME        0x0001
#define ICMF_CHOOSE_DATARATE        0x0002
#define ICMF_CHOOSE_PREVIEW         0x0004
#define ICMF_CHOOSE_ALLCOMPRESSORS  0x0008

BOOL    VFWAPI ICInfo(DWORD fccType, DWORD fccHandler, ICINFO FAR *lpicinfo);           /* 200 */
HIC     VFWAPI ICOpen(DWORD fccType, DWORD fccHandler, UINT wMode);                     /* 203 */
LRESULT VFWAPI ICClose(HIC hic);                                                        /* 204 */
LRESULT VFWAPI ICSendMessage(HIC hic, UINT msg, DWORD dw1, DWORD dw2);                  /* 205 */
LRESULT VFWAPI ICGetInfo(HIC hic, ICINFO FAR *picinfo, DWORD cb);                       /* 212 */
DWORD   VFWAPIV ICCompress(HIC hic, DWORD dwFlags, LPBITMAPINFOHEADER lpbiOutput, LPVOID lpData,
                           LPBITMAPINFOHEADER lpbiInput, LPVOID lpBits, LPDWORD lpckid,
                           LPDWORD lpdwFlags, LONG lFrameNum, DWORD dwFrameSize, DWORD dwQuality,
                           LPBITMAPINFOHEADER lpbiPrev, LPVOID lpPrev);                 /* 224 _ICCOMPRESS */
HANDLE  VFWAPI ICImageDecompress(HIC hic, UINT uiFlags, LPBITMAPINFO lpbiIn, LPVOID lpBits,
                                 LPBITMAPINFO lpbiOut);                                 /* 241 */
BOOL    VFWAPI ICCompressorChoose(HWND hwnd, UINT uiFlags, LPVOID pvIn, LPVOID lpData,
                                  PCOMPVARS pc, LPSTR lpszTitle);                       /* 242 */
void    VFWAPI ICCompressorFree(PCOMPVARS pc);                                          /* 243 */
BOOL    VFWAPI ICSeqCompressFrameStart(PCOMPVARS pc, LPBITMAPINFO lpbiIn);              /* 244 */
void    VFWAPI ICSeqCompressFrameEnd(PCOMPVARS pc);                                     /* 245 */
LPVOID  VFWAPI ICSeqCompressFrame(PCOMPVARS pc, UINT uiFlags, LPVOID lpBits,
                                  BOOL FAR *pfKey, LONG FAR *plSize);                   /* 246 */

#define ICQueryAbout(hic) \
    (ICSendMessage(hic, ICM_ABOUT, (DWORD)-1, ICMF_ABOUT_QUERY) == ICERR_OK)
#define ICAbout(hic, hwnd) \
    ICSendMessage(hic, ICM_ABOUT, (DWORD)(UINT)(hwnd), 0)
#define ICQueryConfigure(hic) \
    (ICSendMessage(hic, ICM_CONFIGURE, (DWORD)-1, ICMF_CONFIGURE_QUERY) == ICERR_OK)
#define ICConfigure(hic, hwnd) \
    ICSendMessage(hic, ICM_CONFIGURE, (DWORD)(UINT)(hwnd), 0)
#define ICGetState(hic, pv, cb) \
    ICSendMessage(hic, ICM_GETSTATE, (DWORD)(LPVOID)(pv), (DWORD)(cb))
#define ICSetState(hic, pv, cb) \
    ICSendMessage(hic, ICM_SETSTATE, (DWORD)(LPVOID)(pv), (DWORD)(cb))
#define ICCompressBegin(hic, lpbiInput, lpbiOutput) \
    ICSendMessage(hic, ICM_COMPRESS_BEGIN, (DWORD)(LPVOID)(lpbiInput), (DWORD)(LPVOID)(lpbiOutput))
#define ICCompressQuery(hic, lpbiInput, lpbiOutput) \
    ICSendMessage(hic, ICM_COMPRESS_QUERY, (DWORD)(LPVOID)(lpbiInput), (DWORD)(LPVOID)(lpbiOutput))
#define ICCompressGetFormat(hic, lpbiInput, lpbiOutput) \
    ICSendMessage(hic, ICM_COMPRESS_GET_FORMAT, (DWORD)(LPVOID)(lpbiInput), (DWORD)(LPVOID)(lpbiOutput))
#define ICCompressGetFormatSize(hic, lpbi) \
    ICCompressGetFormat(hic, lpbi, NULL)
#define ICCompressGetSize(hic, lpbiInput, lpbiOutput) \
    ICSendMessage(hic, ICM_COMPRESS_GET_SIZE, (DWORD)(LPVOID)(lpbiInput), (DWORD)(LPVOID)(lpbiOutput))
#define ICCompressEnd(hic) \
    ICSendMessage(hic, ICM_COMPRESS_END, 0, 0)
#define ICDecompressBegin(hic, lpbiInput, lpbiOutput) \
    ICSendMessage(hic, ICM_DECOMPRESS_BEGIN, (DWORD)(LPVOID)(lpbiInput), (DWORD)(LPVOID)(lpbiOutput))
#define ICDecompressQuery(hic, lpbiInput, lpbiOutput) \
    ICSendMessage(hic, ICM_DECOMPRESS_QUERY, (DWORD)(LPVOID)(lpbiInput), (DWORD)(LPVOID)(lpbiOutput))
#define ICDecompressGetFormat(hic, lpbiInput, lpbiOutput) \
    ((LONG)ICSendMessage(hic, ICM_DECOMPRESS_GET_FORMAT, (DWORD)(LPVOID)(lpbiInput), (DWORD)(LPVOID)(lpbiOutput)))
#define ICDecompressGetFormatSize(hic, lpbi) \
    ICDecompressGetFormat(hic, lpbi, NULL)
#define ICDecompressGetPalette(hic, lpbiInput, lpbiOutput) \
    ICSendMessage(hic, ICM_DECOMPRESS_GET_PALETTE, (DWORD)(LPVOID)(lpbiInput), (DWORD)(LPVOID)(lpbiOutput))
#define ICDecompressEnd(hic) \
    ICSendMessage(hic, ICM_DECOMPRESS_END, 0, 0)

/* ------------------------------------------------------------------ */
/* undocumented                                                        */
/* ------------------------------------------------------------------ */

/*
 * StretchDIB (ordinal 115): MSVIDEO's DIB stretcher.  The export has no
 * 16-bit RETF to check; this is the signature of the later Win32 source
 * and must be confirmed from the call sites.
 */
void    VFWAPI StretchDIB(LPBITMAPINFOHEADER biDst, LPVOID lpDst, int DstX, int DstY,
                          int DstXE, int DstYE, LPBITMAPINFOHEADER biSrc, LPVOID lpSrc,
                          int SrcX, int SrcY, int SrcXE, int SrcYE);                   /* 115 */

#endif /* _INC_MSVIDEO */
