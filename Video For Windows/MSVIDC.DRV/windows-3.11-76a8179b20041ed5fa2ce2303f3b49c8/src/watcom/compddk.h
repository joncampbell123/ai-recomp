/*
 * compddk.h - stand-in for the Video for Windows 1.1 DK header, written
 * for the Open Watcom build (Open Watcom ships no VfW headers).
 *
 * Only what MSVIDC uses.  Win16 layouts; the field offsets agree with the
 * accesses in the original MSVIDC.DRV binary.
 */

#ifndef _INC_COMPDDK
#define _INC_COMPDDK

#ifndef mmioFOURCC
#define mmioFOURCC(ch0, ch1, ch2, ch3) \
    ((DWORD)(BYTE)(ch0) | ((DWORD)(BYTE)(ch1) << 8) | \
     ((DWORD)(BYTE)(ch2) << 16) | ((DWORD)(BYTE)(ch3) << 24))
#endif

#define ICVERSION               0x0104

/* error returns */
#define ICERR_OK                0L
#define ICERR_DONTDRAW          1L
#define ICERR_NEWPALETTE        2L
#define ICERR_UNSUPPORTED       -1L
#define ICERR_BADFORMAT         -2L
#define ICERR_MEMORY            -3L
#define ICERR_INTERNAL          -4L
#define ICERR_BADFLAGS          -5L
#define ICERR_BADPARAM          -6L
#define ICERR_BADSIZE           -7L
#define ICERR_BADHANDLE         -8L
#define ICERR_CANTUPDATE        -9L
#define ICERR_ERROR             -100L
#define ICERR_BADBITDEPTH       -200L
#define ICERR_BADIMAGESIZE      -201L

/* ICINFO.dwFlags */
#define VIDCF_QUALITY           0x0001
#define VIDCF_CRUNCH            0x0002
#define VIDCF_TEMPORAL          0x0004
#define VIDCF_COMPRESSFRAMES    0x0008
#define VIDCF_DRAW              0x0010
#define VIDCF_FASTTEMPORALC     0x0020
#define VIDCF_FASTTEMPORALD     0x0080

#define ICQUALITY_LOW           0
#define ICQUALITY_HIGH          10000
#define ICQUALITY_DEFAULT       -1

/* status callback messages */
#define ICSTATUS_START          0
#define ICSTATUS_STATUS         1
#define ICSTATUS_END            2
#define ICSTATUS_ERROR          3
#define ICSTATUS_YIELD          4

/* AVI index flags returned through ICCOMPRESS.lpdwFlags */
#ifndef AVIIF_TWOCC
#define AVIIF_LIST              0x00000001L
#define AVIIF_TWOCC             0x00000002L
#define AVIIF_KEYFRAME          0x00000010L
#endif

/* messages */
#define ICM_USER                (DRV_USER + 0x0000)
#define ICM_RESERVED            (DRV_USER + 0x1000)

#define ICM_GETSTATE            (ICM_RESERVED + 0)
#define ICM_SETSTATE            (ICM_RESERVED + 1)
#define ICM_GETINFO             (ICM_RESERVED + 2)
#define ICM_CONFIGURE           (ICM_RESERVED + 10)
#define ICM_ABOUT               (ICM_RESERVED + 11)
#define ICM_GETERRORTEXT        (ICM_RESERVED + 12)
#define ICM_GETFORMATNAME       (ICM_RESERVED + 20)
#define ICM_ENUMFORMATS         (ICM_RESERVED + 21)
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
#define ICM_DRAW_BEGIN              (ICM_USER + 15)
#define ICM_DRAW_GET_PALETTE        (ICM_USER + 16)
#define ICM_DRAW_START              (ICM_USER + 18)
#define ICM_DRAW_STOP               (ICM_USER + 19)
#define ICM_DRAW_END                (ICM_USER + 21)
#define ICM_DRAW_GETTIME            (ICM_USER + 32)
#define ICM_DRAW                    (ICM_USER + 33)
#define ICM_DRAW_WINDOW             (ICM_USER + 34)
#define ICM_DRAW_SETTIME            (ICM_USER + 35)
#define ICM_DRAW_REALIZE            (ICM_USER + 36)
#define ICM_DECOMPRESS_SET_PALETTE  (ICM_USER + 29)
#define ICM_DECOMPRESS_GET_PALETTE  (ICM_USER + 30)
#define ICM_DRAW_QUERY              (ICM_USER + 31)
#define ICM_GETBUFFERSWANTED        (ICM_USER + 41)
#define ICM_GETDEFAULTKEYFRAMERATE  (ICM_USER + 42)
#define ICM_DECOMPRESSEX_BEGIN      (ICM_USER + 60)
#define ICM_DECOMPRESSEX_QUERY      (ICM_USER + 61)
#define ICM_DECOMPRESSEX            (ICM_USER + 62)
#define ICM_DECOMPRESSEX_END        (ICM_USER + 63)
#define ICM_COMPRESS_FRAMES_INFO    (ICM_USER + 70)
#define ICM_SET_STATUS_PROC         (ICM_USER + 72)

#ifndef RC_INVOKED

/* DRV_OPEN lParam2 */
typedef struct {
    DWORD   dwSize;
    DWORD   fccType;
    DWORD   fccHandler;
    DWORD   dwVersion;
    DWORD   dwFlags;
    LRESULT dwError;
} ICOPEN;

/* ICM_GETINFO, sizeof == 0x128 */
typedef struct {
    DWORD   dwSize;
    DWORD   fccType;
    DWORD   fccHandler;
    DWORD   dwFlags;
    DWORD   dwVersion;
    DWORD   dwVersionICM;
    char    szName[16];
    char    szDescription[128];
    char    szDriver[128];
} ICINFO;

typedef struct {
    DWORD               dwFlags;
    LPBITMAPINFOHEADER  lpbiOutput;
    LPVOID              lpOutput;
    LPBITMAPINFOHEADER  lpbiInput;
    LPVOID              lpInput;
    LPDWORD             lpckid;
    LPDWORD             lpdwFlags;
    LONG                lFrameNum;
    DWORD               dwFrameSize;
    DWORD               dwQuality;
    LPBITMAPINFOHEADER  lpbiPrev;
    LPVOID              lpPrev;
} ICCOMPRESS;

typedef struct {
    DWORD               dwFlags;
    LPBITMAPINFOHEADER  lpbiInput;
    LPVOID              lpInput;
    LPBITMAPINFOHEADER  lpbiOutput;
    LPVOID              lpOutput;
    DWORD               ckid;
} ICDECOMPRESS;

typedef struct {
    DWORD               dwFlags;
    LPBITMAPINFOHEADER  lpbiSrc;
    LPVOID              lpSrc;
    LPBITMAPINFOHEADER  lpbiDst;
    LPVOID              lpDst;
    int                 xDst, yDst, dxDst, dyDst;
    int                 xSrc, ySrc, dxSrc, dySrc;
} ICDECOMPRESSEX;

typedef struct {
    DWORD               dwFlags;
    HPALETTE            hpal;
    HWND                hwnd;
    HDC                 hdc;
    int                 xDst, yDst, dxDst, dyDst;
    LPBITMAPINFOHEADER  lpbi;
    int                 xSrc, ySrc, dxSrc, dySrc;
    DWORD               dwRate;
    DWORD               dwScale;
} ICDRAWBEGIN;

typedef struct {
    DWORD               dwFlags;
    LPVOID              lpFormat;
    LPVOID              lpData;
    DWORD               cbData;
    LONG                lTime;
} ICDRAW;

typedef struct {
    DWORD               dwFlags;
    LPARAM              lParam;
    LONG (CALLBACK *Status)(LPARAM lParam, UINT message, LONG l);
} ICSETSTATUSPROC;

#endif /* RC_INVOKED */

#endif /* _INC_COMPDDK */
