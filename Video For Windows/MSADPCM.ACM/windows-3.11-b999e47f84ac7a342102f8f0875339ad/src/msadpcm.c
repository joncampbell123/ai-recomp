/*
 * msadpcm.c - code segment 3 of MSADPCM.ACM (0x099F bytes)
 *
 * The MS ADPCM coefficient tables and the C encoder used for streams
 * opened with ACM_STREAMOPENF_NONREALTIME.  For every block it encodes
 * the block once with each of the 7 coefficient sets, keeps the set with
 * the smallest squared error per channel, and then encodes the block for
 * real with that set.
 */

#include <windows.h>
#include "msadpcm.h"

/* DS:0020, DS:002E: the standard coefficient sets, 8.8 fixed point */
const int gaiCoef1[MSADPCM_MAX_COEFFICIENTS] = { 256,  512,  0, 192, 240,  460,  392 };
const int gaiCoef2[MSADPCM_MAX_COEFFICIENTS] = {   0, -256,  0,  64,   0, -208, -232 };

/* DS:003C: step size adaptation, indexed by the 4-bit code, 8.8 fixed point */
const int gaiP4[16] = { 230, 230, 230, 230, 307, 409, 512, 614,
                        768, 614, 512, 409, 307, 230, 230, 230 };


/*
 * seg3:0000-0162  adpcmEncode4Bit_FirstDelta  (NEAR PASCAL, ret 0Eh)
 *
 * Initial step size for a block: the mean absolute error of three
 * predictions over the first five samples (iP5 oldest), divided by 4,
 * i.e. the sum divided by 12, at least MSADPCM_DELTA4_MIN.
 */
int FNLOCAL adpcmEncode4Bit_FirstDelta(int iCoef1, int iCoef2, int iP5, int iP4,
                                       int iP3, int iP2, int iP1)
{
    long    lTotal;
    int     iRtn;
    long    lTemp;

    lTemp  = (((long)iP5 * iCoef2) + ((long)iP4 * iCoef1)) >> MSADPCM_CSCALE;
    lTotal = (lTemp > iP3) ? (lTemp - iP3) : (iP3 - lTemp);

    lTemp   = (((long)iP4 * iCoef2) + ((long)iP3 * iCoef1)) >> MSADPCM_CSCALE;
    lTotal += (lTemp > iP2) ? (lTemp - iP2) : (iP2 - lTemp);

    lTemp   = (((long)iP3 * iCoef2) + ((long)iP2 * iCoef1)) >> MSADPCM_CSCALE;
    lTotal += (lTemp > iP1) ? (lTemp - iP1) : (iP1 - lTemp);

    iRtn = (int)(lTotal / 12);
    if (iRtn < MSADPCM_DELTA4_MIN)
        iRtn = MSADPCM_DELTA4_MIN;

    return (iRtn);
}


/*
 * Fetch one PCM sample as a signed 16-bit value.  8-bit samples are
 * unsigned and become (b - 128) << 8.
 */
#define READ_SAMPLE(pb, nBits)                                          \
    ((8 == (nBits)) ? (int)((UINT)(BYTE)(*(pb)++ - 0x80) << 8)          \
                    : ((pb) += 2, *(short FAR *)((pb) - 2)))


/*
 * seg3:0164-099E  adpcmEncode4Bit  (FAR PASCAL, retf 14h)
 *
 * Encodes cbSrcLength bytes of 8 or 16-bit PCM.  Returns the number of
 * bytes written to pbDst, or 0 if the work buffer can't be allocated.
 *
 * Each block is first copied with hmemcpy into a GlobalAlloc'd buffer of
 * one block of PCM, so the inner loops read through a far pointer while
 * pbDst is advanced as a huge pointer.  Details of the original:
 *
 *  - The coefficients come from the destination format (pwfADPCM->aCoef),
 *    which acmdStreamOpen has checked against the standard set.
 *  - The last block holds whatever is left.  It is not padded, and its
 *    header is complete even when it has fewer than 2 sample frames.
 *  - The first delta always uses the first 5 sample frames of the work
 *    buffer.  If the block has fewer than 5, it uses stale data from
 *    earlier use of that memory (GlobalAlloc does not clear it).  With
 *    fewer than 2 frames, the header's second sample is stale too.
 *  - Nibbles are packed high first.  If a block has an odd number of
 *    nibbles after the header, the last one is never written.
 *    acmdStreamConvert rounds the frame count to even when there are more
 *    than 2, and full blocks have an even count, so this only affects
 *    non-standard formats.
 *  - The squared error is computed in signed 32-bit arithmetic and can
 *    wrap for errors above 46340.
 *  - The work buffer is freed with GlobalFree(SELECTOROF(p)), without
 *    GlobalUnlock.
 */
DWORD FNGLOBAL adpcmEncode4Bit(LPWAVEFORMATEX pwfPCM, HPBYTE pbSrc,
                               LPWAVEFORMATEX pwfADPCM, HPBYTE pbDst,
                               DWORD cbSrcLength)
{
    LPADPCMWAVEFORMAT   pwfadpcm;
    BYTE                nChannels;              /* [bp-11] */
    BYTE                nBitsPerSample;         /* [bp-12] */
    UINT                cbHeader;               /* [bp-5E] */
    UINT                cbSample;               /* [bp-44] bytes per sample frame */
    UINT                cSamplesPerBlock;       /* [bp-5C] */
    UINT                cBlockSamples;          /* [bp-42] */
    DWORD               cbSrcDone;              /* [bp-1A] */
    DWORD               cbDstTotal;             /* [bp-22] */
    DWORD               cbBlock;                /* [bp-B6] */
    LPBYTE              pbTemp;                 /* [bp-40] one block of PCM */
    LPBYTE              pbSamples;              /* [bp-2C] */
    LPBYTE              pbBlockData;            /* [bp-BA] pbTemp + 2 frames */
    BYTE                i;                      /* [bp-08] coefficient set */
    BYTE                m;                      /* [bp-07] channel */
    UINT                n;
    int                 iCoef1;                 /* [bp-2E] */
    int                 iCoef2;                 /* [bp-26] */
    int                 iSample;
    int                 iSamp1;
    int                 iDelta;
    int                 iOutput;
    int                 bOutputByte;            /* [bp-5A] */
    BOOL                fFirstNibble;           /* [bp-24] */
    long                lPrediction;
    long                lSample;
    long                lDelta;
    long                lSamp;
    long                lError;
    DWORD               dwBest;
    int                 aiSamples[ENCODE_DELTA_LOOKAHEAD][MSADPCM_MAX_CHANNELS];     /* [bp-58] */
    int                 aiFirstDelta[MSADPCM_MAX_COEFFICIENTS][MSADPCM_MAX_CHANNELS];/* [bp-7A] */
    DWORD               adwTotalError[MSADPCM_MAX_COEFFICIENTS][MSADPCM_MAX_CHANNELS];/* [bp-B2] */
    int                 aiDelta[MSADPCM_MAX_CHANNELS];      /* [bp-3C] */
    int                 aiSamp2[MSADPCM_MAX_CHANNELS];      /* [bp-38] */
    int                 aiSamp1[MSADPCM_MAX_CHANNELS];      /* [bp-34] */
    BYTE                abBestPredictor[MSADPCM_MAX_CHANNELS]; /* [bp-28] */

    pwfadpcm = (LPADPCMWAVEFORMAT)pwfADPCM;

    nChannels      = (BYTE)pwfPCM->nChannels;
    nBitsPerSample = (BYTE)pwfPCM->wBitsPerSample;
    cbHeader       = MSADPCM_HEADER_LENGTH * nChannels;

    cbSrcDone  = 0;
    cbDstTotal = 0;

    cbSample         = (nBitsPerSample >> 3) << (nChannels >> 1);
    cSamplesPerBlock = pwfadpcm->wSamplesPerBlock;

    pbTemp = (LPBYTE)GlobalLock(GlobalAlloc(GMEM_MOVEABLE | GMEM_SHARE,
                                            (DWORD)(cbSample * cSamplesPerBlock)));
    if (NULL == pbTemp)
        return (0L);

    while (cbSrcDone < cbSrcLength)
    {
        cBlockSamples = cSamplesPerBlock;
        if ((cbSrcLength - cbSrcDone) / cbSample < cSamplesPerBlock)
            cBlockSamples = (UINT)((cbSrcLength - cbSrcDone) / cbSample);

        if (0 == cBlockSamples)
            break;

        cbBlock = (UINT)(cBlockSamples * cbSample);
        hmemcpy(pbTemp, pbSrc + cbSrcDone, cbBlock);

        /*
         * pass 1: encode the block with every coefficient set and add up
         * the squared error per channel
         */
        pbBlockData = pbTemp + 2 * cbSample;

        for (i = 0; i < MSADPCM_MAX_COEFFICIENTS; i++)
        {
            pbSamples = pbTemp;

            iCoef1 = pwfadpcm->aCoef[i].iCoef1;
            iCoef2 = pwfadpcm->aCoef[i].iCoef2;

            for (n = 0; n < ENCODE_DELTA_LOOKAHEAD; n++)
            {
                for (m = 0; m < nChannels; m++)
                {
                    aiSamples[n][m] = READ_SAMPLE(pbSamples, nBitsPerSample);
                }
            }

            for (m = 0; m < nChannels; m++)
            {
                adwTotalError[i][m] = 0;

                aiSamp1[m] = aiSamples[1][m];
                aiSamp2[m] = aiSamples[0][m];

                iDelta = adpcmEncode4Bit_FirstDelta(iCoef1, iCoef2,
                                                    aiSamples[0][m], aiSamples[1][m],
                                                    aiSamples[2][m], aiSamples[3][m],
                                                    aiSamples[4][m]);
                aiFirstDelta[i][m] = iDelta;
                aiDelta[m]         = iDelta;
            }

            pbSamples = pbBlockData;

            for (n = 2; n < cBlockSamples; n++)
            {
                for (m = 0; m < nChannels; m++)
                {
                    iSamp1 = aiSamp1[m];
                    iDelta = aiDelta[m];

                    lPrediction = (((long)iSamp1 * iCoef1) + ((long)aiSamp2[m] * iCoef2)) >> MSADPCM_CSCALE;

                    iSample = READ_SAMPLE(pbSamples, nBitsPerSample);
                    lSample = iSample;
                    lDelta  = iDelta;

                    iOutput = (int)((lSample - lPrediction) / lDelta);
                    if (iOutput > MSADPCM_OUTPUT4MAX)
                        iOutput = MSADPCM_OUTPUT4MAX;
                    else if (iOutput < MSADPCM_OUTPUT4MIN)
                        iOutput = MSADPCM_OUTPUT4MIN;

                    lSamp = lPrediction + ((long)iOutput * lDelta);
                    if (lSamp > 32767)
                        lSamp = 32767;
                    else if (lSamp < -32768L)
                        lSamp = -32768L;

                    iDelta = (int)((gaiP4[iOutput & MSADPCM_OUTPUT4MASK] * lDelta) >> MSADPCM_PSCALE);
                    if (iDelta < MSADPCM_DELTA4_MIN)
                        iDelta = MSADPCM_DELTA4_MIN;

                    aiDelta[m] = iDelta;
                    aiSamp2[m] = iSamp1;
                    aiSamp1[m] = (int)lSamp;

                    lError = lSamp - lSample;
                    adwTotalError[i][m] += (lError * lError) >> 7;
                }
            }
        }

        /*
         * pick the coefficient set with the smallest error for each
         * channel; ties go to the lower index
         */
        for (m = 0; m < nChannels; m++)
        {
            abBestPredictor[m] = 0;
            dwBest = adwTotalError[0][m];

            for (i = 1; i < MSADPCM_MAX_COEFFICIENTS; i++)
            {
                if (adwTotalError[i][m] < dwBest)
                {
                    abBestPredictor[m] = i;
                    dwBest = adwTotalError[i][m];
                }
            }
        }

        /*
         * pass 2: encode the block with the chosen sets
         */
        pbSamples = pbTemp;

        for (m = 0; m < nChannels; m++)
        {
            aiDelta[m] = aiFirstDelta[abBestPredictor[m]][m];
        }

        if (8 == nBitsPerSample)
        {
            for (m = 0; m < nChannels; m++)
                aiSamp2[m] = READ_SAMPLE(pbSamples, 8);
            for (m = 0; m < nChannels; m++)
                aiSamp1[m] = READ_SAMPLE(pbSamples, 8);
        }
        else
        {
            for (m = 0; m < nChannels; m++)
                aiSamp2[m] = READ_SAMPLE(pbSamples, 16);
            for (m = 0; m < nChannels; m++)
                aiSamp1[m] = READ_SAMPLE(pbSamples, 16);
        }

        /*
         * block header: predictor bytes, then the delta, second sample and
         * first sample words, one of each per channel
         */
        for (m = 0; m < nChannels; m++)
        {
            *pbDst++ = abBestPredictor[m];
        }

        for (m = 0; m < nChannels; m++)
        {
            *(short _huge *)pbDst = (short)aiDelta[m];
            pbDst += 2;
        }

        for (m = 0; m < nChannels; m++)
        {
            *(short _huge *)pbDst = (short)aiSamp1[m];
            pbDst += 2;
        }

        for (m = 0; m < nChannels; m++)
        {
            *(short _huge *)pbDst = (short)aiSamp2[m];
            pbDst += 2;
        }

        cbDstTotal += cbHeader;

        fFirstNibble = TRUE;

        for (n = 2; n < cBlockSamples; n++)
        {
            for (m = 0; m < nChannels; m++)
            {
                iSamp1 = aiSamp1[m];
                iDelta = aiDelta[m];

                iCoef1 = pwfadpcm->aCoef[abBestPredictor[m]].iCoef1;
                iCoef2 = pwfadpcm->aCoef[abBestPredictor[m]].iCoef2;

                lPrediction = (((long)iSamp1 * iCoef1) + ((long)aiSamp2[m] * iCoef2)) >> MSADPCM_CSCALE;

                iSample = READ_SAMPLE(pbSamples, nBitsPerSample);
                lDelta  = iDelta;

                iOutput = (int)(((long)iSample - lPrediction) / lDelta);
                if (iOutput > MSADPCM_OUTPUT4MAX)
                    iOutput = MSADPCM_OUTPUT4MAX;
                else if (iOutput < MSADPCM_OUTPUT4MIN)
                    iOutput = MSADPCM_OUTPUT4MIN;

                lSamp = lPrediction + ((long)iOutput * lDelta);
                if (lSamp > 32767)
                    lSamp = 32767;
                else if (lSamp < -32768L)
                    lSamp = -32768L;

                iDelta = (int)((gaiP4[iOutput & MSADPCM_OUTPUT4MASK] * lDelta) >> MSADPCM_PSCALE);
                if (iDelta < MSADPCM_DELTA4_MIN)
                    iDelta = MSADPCM_DELTA4_MIN;

                aiDelta[m] = iDelta;
                aiSamp2[m] = iSamp1;
                aiSamp1[m] = (int)lSamp;

                if (fFirstNibble)
                {
                    bOutputByte  = (iOutput & MSADPCM_OUTPUT4MASK) << 4;
                    fFirstNibble = FALSE;
                }
                else
                {
                    *pbDst++ = (BYTE)(bOutputByte | (iOutput & MSADPCM_OUTPUT4MASK));
                    cbDstTotal++;
                    fFirstNibble = TRUE;
                }
            }
        }

        cbSrcDone += cbBlock;
    }

    GlobalFree((HGLOBAL)SELECTOROF(pbTemp));

    return (cbDstTotal);
}
