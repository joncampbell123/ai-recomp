/*
 * codec.c - code segment 2 of MSADPCM.ACM, offsets 0010-0F45
 * (segment size 0x166E, PRELOAD)
 *
 * Format helpers, the ACM driver message handlers and DriverProc.
 *
 * seg2:0000-000F holds 16 zero bytes and seg2:0F46-166D is the Microsoft
 * C runtime (startup, heap, environment, error messages and
 * ___EXPORTEDSTUB).  Nothing in the driver calls into it, and its
 * initialisation never runs (see init.c), so it is not reconstructed.
 *
 * Unless noted, functions are NEAR PASCAL (callee pops; first argument at
 * the highest stack address).  Return values travel in DX:AX.
 */

#include <windows.h>
#include <string.h>               /* _fmemcpy */
#include "msadpcm.h"

#ifndef min
#define min(a, b)   (((a) < (b)) ? (a) : (b))
#endif

/* DS:0010 */
static const UINT gauFormatTagIndexToTag[CODEC_MAX_FORMAT_TAGS] =
{
    WAVE_FORMAT_PCM,
    WAVE_FORMAT_ADPCM
};

/* DS:0014 */
static const UINT gauFormatIndexToSampleRate[CODEC_MAX_SAMPLE_RATES] =
{
    8000,
    11025,
    22050,
    44100
};

/* DS:001C */
static const UINT gauFormatIndexToBitsPerSample[CODEC_MAX_BITSPERSAMPLE_PCM] =
{
    8,
    16
};


/*
 * seg2:0010-007B  pcmIsValidFormat
 *
 * Checks, in this order: non-NULL, WAVE_FORMAT_PCM, 8 or 16 bits, 1 or 2
 * channels, nBlockAlign and nAvgBytesPerSec consistent.
 */
BOOL FNLOCAL pcmIsValidFormat(LPWAVEFORMATEX pwfx)
{
    if (NULL == pwfx)
        return (FALSE);

    if (WAVE_FORMAT_PCM != pwfx->wFormatTag)
        return (FALSE);

    if ((8 != pwfx->wBitsPerSample) && (16 != pwfx->wBitsPerSample))
        return (FALSE);

    if ((pwfx->nChannels < 1) || (pwfx->nChannels > MSADPCM_MAX_CHANNELS))
        return (FALSE);

    if (PCM_BLOCKALIGNMENT(pwfx) != pwfx->nBlockAlign)
        return (FALSE);

    if (PCM_AVGBYTESPERSEC(pwfx) != pwfx->nAvgBytesPerSec)
        return (FALSE);

    return (TRUE);
}


/*
 * seg2:007E-00B5  adpcmIsValidFormat
 *
 * Only the tag, the bit depth and the channel count are checked; the
 * block size, samples per block and coefficients are left to
 * adpcmIsMagicFormat, which checks only the coefficients.
 */
BOOL FNLOCAL adpcmIsValidFormat(LPWAVEFORMATEX pwfx)
{
    if (NULL == pwfx)
        return (FALSE);

    if (WAVE_FORMAT_ADPCM != pwfx->wFormatTag)
        return (FALSE);

    if (MSADPCM_BITS_PER_SAMPLE != pwfx->wBitsPerSample)
        return (FALSE);

    if ((pwfx->nChannels < 1) || (pwfx->nChannels > MSADPCM_MAX_CHANNELS))
        return (FALSE);

    return (TRUE);
}


/*
 * seg2:00B6-0112  adpcmIsMagicFormat
 *
 * TRUE if the format carries exactly the 7 standard coefficient sets.
 * No NULL check.
 */
BOOL FNLOCAL adpcmIsMagicFormat(LPADPCMWAVEFORMAT pwfadpcm)
{
    UINT    u;

    if (pwfadpcm->wfx.cbSize < MSADPCM_WFX_EXTRA_BYTES)
        return (FALSE);

    if (MSADPCM_MAX_COEFFICIENTS != pwfadpcm->wNumCoef)
        return (FALSE);

    for (u = 0; u < MSADPCM_MAX_COEFFICIENTS; u++)
    {
        if (pwfadpcm->aCoef[u].iCoef1 != gaiCoef1[u])
            return (FALSE);

        if (pwfadpcm->aCoef[u].iCoef2 != gaiCoef2[u])
            return (FALSE);
    }

    return (TRUE);
}


/*
 * seg2:0114-015E  adpcmBlockAlign
 *
 * 256 bytes per channel, times nSamplesPerSec / 11000 above 11025 Hz:
 * 256/512 at 8 and 11 kHz, 512/1024 at 22 kHz, 1024/2048 at 44 kHz.
 */
UINT FNLOCAL adpcmBlockAlign(LPWAVEFORMATEX pwfx)
{
    UINT    uBlockAlign;
    UINT    uChannelShift;

    uChannelShift = pwfx->nChannels >> 1;
    uBlockAlign   = 256 << uChannelShift;

    if (pwfx->nSamplesPerSec > 11025)
    {
        uBlockAlign *= (UINT)(pwfx->nSamplesPerSec / 11000);
    }

    return (uBlockAlign);
}


/*
 * seg2:0160-0197  adpcmSamplesPerBlock
 *
 * Two samples per channel in the header, plus two per data byte (mono)
 * or one per data byte (stereo).  16-bit unsigned arithmetic, so a block
 * smaller than the header wraps.
 */
UINT FNLOCAL adpcmSamplesPerBlock(LPWAVEFORMATEX pwfx)
{
    UINT    uSamplesPerBlock;
    UINT    uChannelShift;
    UINT    uHeaderBytes;
    UINT    uBitsPerSample;

    uChannelShift  = pwfx->nChannels >> 1;
    uHeaderBytes   = MSADPCM_HEADER_LENGTH << uChannelShift;
    uBitsPerSample = MSADPCM_BITS_PER_SAMPLE << uChannelShift;

    uSamplesPerBlock  = (pwfx->nBlockAlign - uHeaderBytes) * 8;
    uSamplesPerBlock /= uBitsPerSample;
    uSamplesPerBlock += 2;

    return (uSamplesPerBlock);
}


/*
 * seg2:0198-01D8  adpcmAvgBytesPerSec
 */
DWORD FNLOCAL adpcmAvgBytesPerSec(LPWAVEFORMATEX pwfx)
{
    UINT    uSamplesPerBlock;

    uSamplesPerBlock = adpcmSamplesPerBlock(pwfx);

    return (((DWORD)pwfx->nBlockAlign * pwfx->nSamplesPerSec) / uSamplesPerBlock);
}


/*
 * seg2:01DA-0226  adpcmCopyCoefficients
 */
BOOL FNLOCAL adpcmCopyCoefficients(LPADPCMWAVEFORMAT pwfadpcm)
{
    UINT    u;

    pwfadpcm->wNumCoef = MSADPCM_MAX_COEFFICIENTS;

    for (u = 0; u < MSADPCM_MAX_COEFFICIENTS; u++)
    {
        pwfadpcm->aCoef[u].iCoef1 = (short)gaiCoef1[u];
        pwfadpcm->aCoef[u].iCoef2 = (short)gaiCoef2[u];
    }

    return (TRUE);
}


/*
 * seg2:0228-02B0  acmdDriverOpen  (DRV_OPEN)
 */
LRESULT FNLOCAL acmdDriverOpen(HDRVR hdrvr, LPACMDRVOPENDESC paod)
{
    PCODECINST  pci;

    /* opened without an open description (Drivers control panel) */
    if (NULL == paod)
        return (BOGUS_DRIVER_ID);

    if (ACMDRIVERDETAILS_FCCTYPE_AUDIOCODEC != paod->fccType)
        return (0L);

    pci = (PCODECINST)LocalAlloc(LPTR, sizeof(*pci));
    if (NULL == pci)
    {
        paod->dwError = MMSYSERR_NOMEM;
        return (0L);
    }

    pci->fccType    = paod->fccType;
    pci->DriverProc = NULL;
    pci->hdrvr      = hdrvr;
    pci->vdwACM     = paod->dwVersion;
    pci->dwFlags    = paod->dwFlags;

    paod->dwError = MMSYSERR_NOERROR;

    return ((LRESULT)(UINT)pci);
}


/*
 * seg2:02B2-02C8  acmdDriverClose  (DRV_CLOSE)
 */
LRESULT FNLOCAL acmdDriverClose(PCODECINST pci)
{
    if (NULL != pci)
    {
        LocalFree((HLOCAL)pci);
    }

    return (1L);
}


/*
 * seg2:02CA-02CF  acmdDriverConfigure  (DRV_CONFIGURE, DRV_QUERYCONFIGURE)
 *
 * No configuration: 0 for both the query and the request.
 */
LRESULT FNLOCAL acmdDriverConfigure(PCODECINST pci, HWND hwnd, LPDRVCONFIGINFO pdci)
{
    return (0L);
}


/*
 * seg2:02D0-03E0  acmdDriverDetails  (ACMDM_DRIVER_DETAILS)
 *
 * The details are built in a 0x396-byte ACMDRIVERDETAILS on the stack and
 * min(padd->cbStruct, 0x396) bytes are copied out.  hicon and the strings
 * are filled only when the caller's structure reaches them.  There is no
 * licensing string (ID 4); LoadString leaves an empty string.
 */
LRESULT FNLOCAL acmdDriverDetails(PCODECINST pci, LPACMDRIVERDETAILS padd)
{
    ACMDRIVERDETAILS    add;
    DWORD               cbStruct;

    cbStruct     = min(padd->cbStruct, sizeof(ACMDRIVERDETAILS));
    add.cbStruct = cbStruct;

    add.fccType     = ACMDRIVERDETAILS_FCCTYPE_AUDIOCODEC;
    add.fccComp     = ACMDRIVERDETAILS_FCCCOMP_UNDEFINED;
    add.wMid        = MM_MICROSOFT;
    add.wPid        = MM_MSFT_ACM_MSADPCM;
    add.vdwACM      = VERSION_MSACM;
    add.vdwDriver   = VERSION_CODEC;
    add.fdwSupport  = ACMDRIVERDETAILS_SUPPORTF_CODEC;
    add.cFormatTags = CODEC_MAX_FORMAT_TAGS;
    add.cFilterTags = CODEC_MAX_FILTER_TAGS;

    if (FIELD_OFFSET(ACMDRIVERDETAILS, hicon) < cbStruct)
    {
        add.hicon = NULL;

        LoadString(ghinst, IDS_ACM_DRIVER_SHORTNAME, add.szShortName, sizeof(add.szShortName));
        LoadString(ghinst, IDS_ACM_DRIVER_LONGNAME,  add.szLongName,  sizeof(add.szLongName));

        if (FIELD_OFFSET(ACMDRIVERDETAILS, szCopyright) < cbStruct)
        {
            LoadString(ghinst, IDS_ACM_DRIVER_COPYRIGHT, add.szCopyright, sizeof(add.szCopyright));
            LoadString(ghinst, IDS_ACM_DRIVER_LICENSING, add.szLicensing, sizeof(add.szLicensing));
            LoadString(ghinst, IDS_ACM_DRIVER_FEATURES,  add.szFeatures,  sizeof(add.szFeatures));
        }
    }

    _fmemcpy(padd, &add, (UINT)add.cbStruct);

    return (MMSYSERR_NOERROR);
}


/*
 * seg2:03E2-03E8  acmdDriverAbout  (ACMDM_DRIVER_ABOUT)
 *
 * Always MMSYSERR_NOTSUPPORTED, both for the query (hwnd == -1) and the
 * request, so the ACM shows its default about box.
 */
LRESULT FNLOCAL acmdDriverAbout(PCODECINST pci, HWND hwnd)
{
    return (MMSYSERR_NOTSUPPORTED);
}


/*
 * seg2:03EA-060A  acmdFormatSuggest  (ACMDM_FORMAT_SUGGEST)
 *
 * PCM -> ADPCM: same rate and channels, 4 bits, standard block size and
 * coefficients.  ADPCM -> PCM: same rate and channels, 16 bits unless the
 * caller fixes 8 or 16 with ACM_FORMATSUGGESTF_WBITSPERSAMPLE.  The
 * destination is written field by field while it is being checked, so a
 * late rejection (the bit depth on the ADPCM path) leaves it partly
 * filled.  cbwfxDst is not checked.
 */
LRESULT FNLOCAL acmdFormatSuggest(PCODECINST pci, LPACMDRVFORMATSUGGEST padfs)
{
    LPWAVEFORMATEX      pwfxSrc;
    LPWAVEFORMATEX      pwfxDst;
    LPADPCMWAVEFORMAT   pwfadpcm;

    pwfxSrc = padfs->pwfxSrc;
    pwfxDst = padfs->pwfxDst;

    switch (pwfxSrc->wFormatTag)
    {
        case WAVE_FORMAT_PCM:
            if (!pcmIsValidFormat(pwfxSrc))
                return (ACMERR_NOTPOSSIBLE);

            if (padfs->fdwSuggest & ACM_FORMATSUGGESTF_WFORMATTAG)
            {
                if (WAVE_FORMAT_ADPCM != pwfxDst->wFormatTag)
                    return (ACMERR_NOTPOSSIBLE);
            }

            if (padfs->fdwSuggest & ACM_FORMATSUGGESTF_NCHANNELS)
            {
                if (pwfxSrc->nChannels != pwfxDst->nChannels)
                    return (ACMERR_NOTPOSSIBLE);
            }

            if (padfs->fdwSuggest & ACM_FORMATSUGGESTF_NSAMPLESPERSEC)
            {
                if (pwfxSrc->nSamplesPerSec != pwfxDst->nSamplesPerSec)
                    return (ACMERR_NOTPOSSIBLE);
            }

            if (padfs->fdwSuggest & ACM_FORMATSUGGESTF_WBITSPERSAMPLE)
            {
                if (MSADPCM_BITS_PER_SAMPLE != pwfxDst->wBitsPerSample)
                    return (ACMERR_NOTPOSSIBLE);
            }

            pwfadpcm = (LPADPCMWAVEFORMAT)pwfxDst;

            pwfxDst->wFormatTag      = WAVE_FORMAT_ADPCM;
            pwfxDst->nSamplesPerSec  = pwfxSrc->nSamplesPerSec;
            pwfxDst->nChannels       = pwfxSrc->nChannels;
            pwfxDst->wBitsPerSample  = MSADPCM_BITS_PER_SAMPLE;
            pwfxDst->nBlockAlign     = adpcmBlockAlign(pwfxDst);
            pwfxDst->nAvgBytesPerSec = adpcmAvgBytesPerSec(pwfxDst);
            pwfxDst->cbSize          = MSADPCM_WFX_EXTRA_BYTES;

            pwfadpcm->wSamplesPerBlock = adpcmSamplesPerBlock(pwfxDst);
            adpcmCopyCoefficients(pwfadpcm);

            return (MMSYSERR_NOERROR);

        case WAVE_FORMAT_ADPCM:
            if (!adpcmIsValidFormat(pwfxSrc))
                return (ACMERR_NOTPOSSIBLE);

            if (!adpcmIsMagicFormat((LPADPCMWAVEFORMAT)pwfxSrc))
                return (ACMERR_NOTPOSSIBLE);

            if (padfs->fdwSuggest & ACM_FORMATSUGGESTF_WFORMATTAG)
            {
                if (WAVE_FORMAT_PCM != pwfxDst->wFormatTag)
                    return (ACMERR_NOTPOSSIBLE);
            }

            if (padfs->fdwSuggest & ACM_FORMATSUGGESTF_NCHANNELS)
            {
                if (pwfxSrc->nChannels != pwfxDst->nChannels)
                    return (ACMERR_NOTPOSSIBLE);
            }

            if (padfs->fdwSuggest & ACM_FORMATSUGGESTF_NSAMPLESPERSEC)
            {
                if (pwfxSrc->nSamplesPerSec != pwfxDst->nSamplesPerSec)
                    return (ACMERR_NOTPOSSIBLE);
            }

            pwfxDst->wFormatTag     = WAVE_FORMAT_PCM;
            pwfxDst->nSamplesPerSec = pwfxSrc->nSamplesPerSec;
            pwfxDst->nChannels      = pwfxSrc->nChannels;

            if (padfs->fdwSuggest & ACM_FORMATSUGGESTF_WBITSPERSAMPLE)
            {
                if ((8 != pwfxDst->wBitsPerSample) && (16 != pwfxDst->wBitsPerSample))
                    return (ACMERR_NOTPOSSIBLE);
            }
            else
            {
                pwfxDst->wBitsPerSample = 16;
            }

            pwfxDst->nBlockAlign     = PCM_BLOCKALIGNMENT(pwfxDst);
            pwfxDst->nAvgBytesPerSec = PCM_AVGBYTESPERSEC(pwfxDst);

            return (MMSYSERR_NOERROR);
    }

    return (ACMERR_NOTPOSSIBLE);
}


/*
 * seg2:060C-073D  acmdFormatTagDetails  (ACMDM_FORMATTAG_DETAILS)
 *
 * Index 0 is PCM (16 standard formats, empty name: the ACM names PCM
 * itself), index 1 is ADPCM (8 standard formats, 50-byte format).
 * LARGESTSIZE with WAVE_FORMAT_UNKNOWN answers ADPCM.
 */
LRESULT FNLOCAL acmdFormatTagDetails(PCODECINST pci, LPACMFORMATTAGDETAILS padft, DWORD fdwDetails)
{
    UINT    uFormatTag;

    switch (ACM_FORMATTAGDETAILSF_QUERYMASK & fdwDetails)
    {
        case ACM_FORMATTAGDETAILSF_INDEX:
            if (CODEC_MAX_FORMAT_TAGS <= padft->dwFormatTagIndex)
                return (ACMERR_NOTPOSSIBLE);

            uFormatTag = gauFormatTagIndexToTag[(UINT)padft->dwFormatTagIndex];
            break;

        case ACM_FORMATTAGDETAILSF_LARGESTSIZE:
            switch (padft->dwFormatTag)
            {
                case WAVE_FORMAT_UNKNOWN:
                case WAVE_FORMAT_ADPCM:
                    uFormatTag = WAVE_FORMAT_ADPCM;
                    break;

                case WAVE_FORMAT_PCM:
                    uFormatTag = WAVE_FORMAT_PCM;
                    break;

                default:
                    return (ACMERR_NOTPOSSIBLE);
            }
            break;

        case ACM_FORMATTAGDETAILSF_FORMATTAG:
            switch (padft->dwFormatTag)
            {
                case WAVE_FORMAT_ADPCM:
                    uFormatTag = WAVE_FORMAT_ADPCM;
                    break;

                case WAVE_FORMAT_PCM:
                    uFormatTag = WAVE_FORMAT_PCM;
                    break;

                default:
                    return (ACMERR_NOTPOSSIBLE);
            }
            break;

        default:
            return (MMSYSERR_NOTSUPPORTED);
    }

    switch (uFormatTag)
    {
        case WAVE_FORMAT_PCM:
            padft->dwFormatTagIndex = 0;
            padft->dwFormatTag      = WAVE_FORMAT_PCM;
            padft->cbFormatSize     = sizeof(PCMWAVEFORMAT);
            padft->fdwSupport       = ACMDRIVERDETAILS_SUPPORTF_CODEC;
            padft->cStandardFormats = CODEC_MAX_STANDARD_FORMATS_PCM;
            padft->szFormatTag[0]   = '\0';
            break;

        case WAVE_FORMAT_ADPCM:
            padft->dwFormatTagIndex = 1;
            padft->dwFormatTag      = WAVE_FORMAT_ADPCM;
            padft->cbFormatSize     = sizeof(WAVEFORMATEX) + MSADPCM_WFX_EXTRA_BYTES;
            padft->fdwSupport       = ACMDRIVERDETAILS_SUPPORTF_CODEC;
            padft->cStandardFormats = CODEC_MAX_STANDARD_FORMATS_ADPCM;
            LoadString(ghinst, IDS_ACM_DRIVER_TAG_NAME, padft->szFormatTag,
                       sizeof(padft->szFormatTag));
            break;

        default:
            return (ACMERR_NOTPOSSIBLE);
    }

    padft->cbStruct = min(padft->cbStruct, sizeof(*padft));

    return (MMSYSERR_NOERROR);
}


/*
 * seg2:0740-08F1  acmdFormatDetails  (ACMDM_FORMAT_DETAILS)
 *
 * PCM index bits: 0 = channels-1, 1 = 8/16 bits, 2-3 = sample rate.
 * ADPCM index bits: 0 = channels-1, 1-2 = sample rate.
 * A format built for ACM_FORMATDETAILSF_INDEX is run through the same
 * validation as a caller's format for ACM_FORMATDETAILSF_FORMAT.
 * szFormat is always returned empty; the ACM builds the text.
 */
LRESULT FNLOCAL acmdFormatDetails(PCODECINST pci, LPACMFORMATDETAILS padf, DWORD fdwDetails)
{
    LPWAVEFORMATEX      pwfx;
    LPADPCMWAVEFORMAT   pwfadpcm;
    UINT                uFormatIndex;

    pwfx = padf->pwfx;

    switch (ACM_FORMATDETAILSF_QUERYMASK & fdwDetails)
    {
        case ACM_FORMATDETAILSF_INDEX:
            switch (padf->dwFormatTag)
            {
                case WAVE_FORMAT_PCM:
                    if (CODEC_MAX_STANDARD_FORMATS_PCM <= padf->dwFormatIndex)
                        return (ACMERR_NOTPOSSIBLE);

                    pwfx->wFormatTag      = WAVE_FORMAT_PCM;

                    uFormatIndex          = (UINT)padf->dwFormatIndex;
                    pwfx->nSamplesPerSec  = gauFormatIndexToSampleRate[uFormatIndex / (CODEC_MAX_CHANNELS * CODEC_MAX_BITSPERSAMPLE_PCM)];
                    pwfx->nChannels       = (WORD)(uFormatIndex % CODEC_MAX_CHANNELS) + 1;
                    pwfx->wBitsPerSample  = (WORD)gauFormatIndexToBitsPerSample[(uFormatIndex / CODEC_MAX_CHANNELS) % CODEC_MAX_BITSPERSAMPLE_PCM];
                    pwfx->nBlockAlign     = PCM_BLOCKALIGNMENT(pwfx);
                    pwfx->nAvgBytesPerSec = PCM_AVGBYTESPERSEC(pwfx);
                    break;

                case WAVE_FORMAT_ADPCM:
                    if (CODEC_MAX_STANDARD_FORMATS_ADPCM <= padf->dwFormatIndex)
                        return (ACMERR_NOTPOSSIBLE);

                    pwfadpcm = (LPADPCMWAVEFORMAT)pwfx;

                    pwfx->wFormatTag      = WAVE_FORMAT_ADPCM;

                    uFormatIndex          = (UINT)padf->dwFormatIndex;
                    pwfx->nSamplesPerSec  = gauFormatIndexToSampleRate[uFormatIndex / CODEC_MAX_CHANNELS];
                    pwfx->nChannels       = (WORD)(uFormatIndex % CODEC_MAX_CHANNELS) + 1;
                    pwfx->wBitsPerSample  = MSADPCM_BITS_PER_SAMPLE;
                    pwfx->nBlockAlign     = adpcmBlockAlign(pwfx);
                    pwfx->nAvgBytesPerSec = adpcmAvgBytesPerSec(pwfx);
                    pwfx->cbSize          = MSADPCM_WFX_EXTRA_BYTES;

                    pwfadpcm->wSamplesPerBlock = adpcmSamplesPerBlock(pwfx);
                    adpcmCopyCoefficients(pwfadpcm);
                    break;

                default:
                    return (ACMERR_NOTPOSSIBLE);
            }

            /* fall through */

        case ACM_FORMATDETAILSF_FORMAT:
            switch (pwfx->wFormatTag)
            {
                case WAVE_FORMAT_PCM:
                    if (!pcmIsValidFormat(pwfx))
                        return (ACMERR_NOTPOSSIBLE);
                    break;

                case WAVE_FORMAT_ADPCM:
                    if (!adpcmIsValidFormat(pwfx))
                        return (ACMERR_NOTPOSSIBLE);

                    if (!adpcmIsMagicFormat((LPADPCMWAVEFORMAT)pwfx))
                        return (ACMERR_NOTPOSSIBLE);
                    break;

                default:
                    return (ACMERR_NOTPOSSIBLE);
            }
            break;

        default:
            return (MMSYSERR_NOTSUPPORTED);
    }

    padf->fdwSupport  = ACMDRIVERDETAILS_SUPPORTF_CODEC;
    padf->szFormat[0] = '\0';

    padf->cbStruct = min(padf->cbStruct, sizeof(*padf));

    return (MMSYSERR_NOERROR);
}


/*
 * seg2:08F4-09B4  acmdStreamQuery  (helper of acmdStreamOpen)
 *
 * ADPCM -> PCM or PCM -> ADPCM with the same rate and channel count, and
 * the standard coefficients on the ADPCM side.  The ADPCM block size and
 * samples per block are not checked.  pwfltr and fdwOpen are unused.
 */
LRESULT FNLOCAL acmdStreamQuery(PCODECINST pci, LPWAVEFORMATEX pwfxSrc, LPWAVEFORMATEX pwfxDst,
                                LPWAVEFILTER pwfltr, DWORD fdwOpen)
{
    if (adpcmIsValidFormat(pwfxSrc))
    {
        if (!pcmIsValidFormat(pwfxDst))
            return (ACMERR_NOTPOSSIBLE);

        if (pwfxSrc->nChannels != pwfxDst->nChannels)
            return (ACMERR_NOTPOSSIBLE);

        if (pwfxSrc->nSamplesPerSec != pwfxDst->nSamplesPerSec)
            return (ACMERR_NOTPOSSIBLE);

        if (!adpcmIsMagicFormat((LPADPCMWAVEFORMAT)pwfxSrc))
            return (ACMERR_NOTPOSSIBLE);

        return (MMSYSERR_NOERROR);
    }

    if (pcmIsValidFormat(pwfxSrc))
    {
        if (!adpcmIsValidFormat(pwfxDst))
            return (ACMERR_NOTPOSSIBLE);

        if (pwfxSrc->nChannels != pwfxDst->nChannels)
            return (ACMERR_NOTPOSSIBLE);

        if (pwfxSrc->nSamplesPerSec != pwfxDst->nSamplesPerSec)
            return (ACMERR_NOTPOSSIBLE);

        if (!adpcmIsMagicFormat((LPADPCMWAVEFORMAT)pwfxDst))
            return (ACMERR_NOTPOSSIBLE);

        return (MMSYSERR_NOERROR);
    }

    return (ACMERR_NOTPOSSIBLE);
}


/*
 * seg2:09B6-0A38  acmdStreamOpen  (ACMDM_STREAM_OPEN)
 *
 * Stores the converter in padsi->dwDriver.  Encoding uses the full-search
 * encoder in seg3 for ACM_STREAMOPENF_NONREALTIME and the fixed-predictor
 * assembly encoder in seg4 otherwise.  ACM_STREAMOPENF_QUERY gets the same
 * treatment as a real open; there is nothing to allocate.
 */
LRESULT FNLOCAL acmdStreamOpen(PCODECINST pci, LPACMDRVSTREAMINSTANCE padsi)
{
    UINT    uFormatTag;

    if (MMSYSERR_NOERROR != acmdStreamQuery(pci, padsi->pwfxSrc, padsi->pwfxDst,
                                            padsi->pwfltr, padsi->fdwOpen))
        return (ACMERR_NOTPOSSIBLE);

    uFormatTag = padsi->pwfxSrc->wFormatTag;

    if (WAVE_FORMAT_ADPCM == uFormatTag)
    {
        padsi->dwDriver = (DWORD)(CONVERTPROC)adpcmDecode4Bit;
    }
    else if (WAVE_FORMAT_PCM == uFormatTag)
    {
        if (ACM_STREAMOPENF_NONREALTIME & padsi->fdwOpen)
            padsi->dwDriver = (DWORD)(CONVERTPROC)adpcmEncode4Bit;
        else
            padsi->dwDriver = (DWORD)(CONVERTPROC)adpcmEncode4Bit_RealTime;
    }
    else
    {
        return (ACMERR_NOTPOSSIBLE);
    }

    return (MMSYSERR_NOERROR);
}


/*
 * seg2:0A3A-0A3F  acmdStreamClose  (ACMDM_STREAM_CLOSE)
 */
LRESULT FNLOCAL acmdStreamClose(PCODECINST pci, LPACMDRVSTREAMINSTANCE padsi)
{
    return (MMSYSERR_NOERROR);
}


/*
 * seg2:0A40-0C6C  acmdStreamSize  (ACMDM_STREAM_SIZE)
 *
 * Source sizes round up to whole blocks, destination sizes round down.
 * The bytes-per-block products are 16-bit (UINT * WORD), as in the
 * original; they overflow only for non-standard formats.
 */
LRESULT FNLOCAL acmdStreamSize(PCODECINST pci, LPACMDRVSTREAMINSTANCE padsi, LPACMDRVSTREAMSIZE padss)
{
    LPWAVEFORMATEX      pwfxSrc;
    LPWAVEFORMATEX      pwfxDst;
    LPADPCMWAVEFORMAT   pwfadpcm;
    DWORD               cb;
    DWORD               cbBytesPerBlock;

    pwfxSrc = padsi->pwfxSrc;
    pwfxDst = padsi->pwfxDst;

    switch (ACM_STREAMSIZEF_QUERYMASK & padss->fdwSize)
    {
        case ACM_STREAMSIZEF_SOURCE:
            if (WAVE_FORMAT_ADPCM == pwfxSrc->wFormatTag)
            {
                /* PCM bytes needed for cbSrcLength ADPCM bytes, rounded up */
                cb = padss->cbSrcLength / pwfxSrc->nBlockAlign;
                if (0 == cb)
                    return (ACMERR_NOTPOSSIBLE);

                pwfadpcm = (LPADPCMWAVEFORMAT)pwfxSrc;
                cbBytesPerBlock = (UINT)(pwfxDst->nBlockAlign * pwfadpcm->wSamplesPerBlock);

                if ((0xFFFFFFFFL / cbBytesPerBlock) < cb)
                    return (ACMERR_NOTPOSSIBLE);

                if (0 == (padss->cbSrcLength % pwfxSrc->nBlockAlign))
                    cb = cb * cbBytesPerBlock;
                else
                    cb = (cb + 1) * cbBytesPerBlock;
            }
            else
            {
                /* ADPCM bytes needed for cbSrcLength PCM bytes, rounded up */
                pwfadpcm = (LPADPCMWAVEFORMAT)pwfxDst;
                cbBytesPerBlock = (UINT)(pwfxSrc->nBlockAlign * pwfadpcm->wSamplesPerBlock);

                cb = padss->cbSrcLength / cbBytesPerBlock;

                if (0 == (padss->cbSrcLength % cbBytesPerBlock))
                    cb = cb * pwfxDst->nBlockAlign;
                else
                    cb = (cb + 1) * pwfxDst->nBlockAlign;

                if (0L == cb)
                    return (ACMERR_NOTPOSSIBLE);
            }

            padss->cbDstLength = cb;
            return (MMSYSERR_NOERROR);

        case ACM_STREAMSIZEF_DESTINATION:
            if (WAVE_FORMAT_ADPCM == pwfxDst->wFormatTag)
            {
                /* PCM bytes that fit in cbDstLength ADPCM bytes, rounded down */
                cb = padss->cbDstLength / pwfxDst->nBlockAlign;
                if (0 == cb)
                    return (ACMERR_NOTPOSSIBLE);

                pwfadpcm = (LPADPCMWAVEFORMAT)pwfxDst;
                cbBytesPerBlock = (UINT)(pwfxSrc->nBlockAlign * pwfadpcm->wSamplesPerBlock);

                if ((0xFFFFFFFFL / cbBytesPerBlock) < cb)
                    return (ACMERR_NOTPOSSIBLE);

                cb = cb * cbBytesPerBlock;
            }
            else
            {
                /* ADPCM bytes that decode into cbDstLength PCM bytes, rounded down */
                pwfadpcm = (LPADPCMWAVEFORMAT)pwfxSrc;
                cbBytesPerBlock = (UINT)(pwfadpcm->wSamplesPerBlock * pwfxDst->nBlockAlign);

                cb = padss->cbDstLength / cbBytesPerBlock;
                if (0 == cb)
                    return (ACMERR_NOTPOSSIBLE);

                cb = cb * pwfxSrc->nBlockAlign;
            }

            padss->cbSrcLength = cb;
            return (MMSYSERR_NOERROR);
    }

    return (MMSYSERR_NOTSUPPORTED);
}


/*
 * seg2:0C6E-0DDA  acmdStreamConvert  (ACMDM_STREAM_CONVERT)
 *
 * Encoding: the source is cut to whole PCM sample frames, to whole ADPCM
 * blocks for ACM_STREAMCONVERTF_BLOCKALIGN, and then to an even number of
 * frames if there are more than 2.  The assembly encoder needs the even
 * count: its mono loop takes two samples per pass and only stops on zero.
 * The frame size comes from wBitsPerSample and nChannels, not from
 * nBlockAlign.
 *
 * Decoding: the whole source is passed on, or whole blocks only for
 * ACM_STREAMCONVERTF_BLOCKALIGN; the decoder handles a partial last block.
 *
 * cbDstLength is not checked; the ACM's stream size answer has to be
 * right.
 */
LRESULT FNLOCAL acmdStreamConvert(PCODECINST pci, LPACMDRVSTREAMINSTANCE padsi, LPACMDRVSTREAMHEADER padsh)
{
    LPWAVEFORMATEX      pwfpcm;
    LPADPCMWAVEFORMAT   pwfadpcm;
    BOOL                fBlockAlign;
    DWORD               dw;
    CONVERTPROC         fpConvert;

    fBlockAlign = (0 != (ACM_STREAMCONVERTF_BLOCKALIGN & padsh->fdwConvert));

    if (WAVE_FORMAT_PCM == padsi->pwfxSrc->wFormatTag)
    {
        pwfpcm   = padsi->pwfxSrc;
        pwfadpcm = (LPADPCMWAVEFORMAT)padsi->pwfxDst;

        dw = PCM_BYTESTOSAMPLES(pwfpcm, padsh->cbSrcLength);

        if (fBlockAlign)
        {
            dw = (dw / pwfadpcm->wSamplesPerBlock) * pwfadpcm->wSamplesPerBlock;
        }

        if (dw > 2)
        {
            dw &= ~1L;
        }

        padsh->cbSrcLengthUsed = PCM_SAMPLESTOBYTES(pwfpcm, dw);
    }
    else
    {
        pwfadpcm = (LPADPCMWAVEFORMAT)padsi->pwfxSrc;

        if (fBlockAlign)
        {
            dw  = padsh->cbSrcLength / pwfadpcm->wfx.nBlockAlign;
            dw *= pwfadpcm->wfx.nBlockAlign;
        }
        else
        {
            dw = padsh->cbSrcLength;
        }

        padsh->cbSrcLengthUsed = dw;
    }

    fpConvert = (CONVERTPROC)padsi->dwDriver;

    padsh->cbDstLengthUsed = (*fpConvert)(padsi->pwfxSrc, (HPBYTE)padsh->pbSrc,
                                          padsi->pwfxDst, (HPBYTE)padsh->pbDst,
                                          padsh->cbSrcLengthUsed);

    return (MMSYSERR_NOERROR);
}


/*
 * seg2:0DDC-0F44  DriverProc  (exported ordinal 2, FAR PASCAL _loadds, retf 10h)
 *
 * DRV_ENABLE and DRV_DISABLE go to DefDriverProc.  Unhandled messages at
 * or above ACMDM_USER return MMSYSERR_NOTSUPPORTED; ACMDM_FILTER*,
 * ACMDM_STREAM_RESET/PREPARE/UNPREPARE and ACMDM_DRIVER_NOTIFY are among
 * them.
 */
LRESULT FNEXPORT DriverProc(DWORD dwId, HDRVR hdrvr, UINT uMsg, LPARAM lParam1, LPARAM lParam2)
{
    PCODECINST  pci;

    pci = (BOGUS_DRIVER_ID == dwId) ? NULL : (PCODECINST)(UINT)dwId;

    switch (uMsg)
    {
        case DRV_LOAD:
            return (1L);

        case DRV_FREE:
            return (1L);

        case DRV_OPEN:
            return (acmdDriverOpen(hdrvr, (LPACMDRVOPENDESC)lParam2));

        case DRV_CLOSE:
            return (acmdDriverClose(pci));

        case DRV_INSTALL:
            return ((LRESULT)DRVCNF_RESTART);

        case DRV_REMOVE:
            return ((LRESULT)DRVCNF_RESTART);

        case DRV_QUERYCONFIGURE:
            lParam1 = -1L;
            lParam2 = 0L;

            /* fall through */

        case DRV_CONFIGURE:
            return (acmdDriverConfigure(pci, (HWND)(UINT)lParam1, (LPDRVCONFIGINFO)lParam2));

        case ACMDM_DRIVER_DETAILS:
            return (acmdDriverDetails(pci, (LPACMDRIVERDETAILS)lParam1));

        case ACMDM_DRIVER_ABOUT:
            return (acmdDriverAbout(pci, (HWND)(UINT)lParam1));

        case ACMDM_FORMATTAG_DETAILS:
            return (acmdFormatTagDetails(pci, (LPACMFORMATTAGDETAILS)lParam1, (DWORD)lParam2));

        case ACMDM_FORMAT_DETAILS:
            return (acmdFormatDetails(pci, (LPACMFORMATDETAILS)lParam1, (DWORD)lParam2));

        case ACMDM_FORMAT_SUGGEST:
            return (acmdFormatSuggest(pci, (LPACMDRVFORMATSUGGEST)lParam1));

        case ACMDM_STREAM_OPEN:
            return (acmdStreamOpen(pci, (LPACMDRVSTREAMINSTANCE)lParam1));

        case ACMDM_STREAM_CLOSE:
            return (acmdStreamClose(pci, (LPACMDRVSTREAMINSTANCE)lParam1));

        case ACMDM_STREAM_SIZE:
            return (acmdStreamSize(pci, (LPACMDRVSTREAMINSTANCE)lParam1, (LPACMDRVSTREAMSIZE)lParam2));

        case ACMDM_STREAM_CONVERT:
            return (acmdStreamConvert(pci, (LPACMDRVSTREAMINSTANCE)lParam1, (LPACMDRVSTREAMHEADER)lParam2));
    }

    if (uMsg >= ACMDM_USER)
        return (MMSYSERR_NOTSUPPORTED);

    return (DefDriverProc(dwId, hdrvr, uMsg, lParam1, lParam2));
}
