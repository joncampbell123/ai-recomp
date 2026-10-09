/*
 * adpcm386.c - MSADPCM.ACM segment 4 (0x12AF bytes): the decoder and the
 * real-time encoder.
 *
 * C reconstruction of hand-written assembly.  The segment has no
 * relocations and is 16-bit code using 386 instructions and 32-bit
 * addressing: the routines load DS:SI and ES:DI from the far pointers,
 * clear the upper halves of ESI and EDI, and step through the buffers
 * with [esi]/[es:edi].  This relies on the selector of a huge block
 * having a limit that covers the rest of the block, which Windows 3.1
 * provides in enhanced mode.  It needs a 386 even though acmdStreamOpen
 * picks these routines without checking the CPU.  The C versions use
 * huge pointers instead.
 *
 * Layout:
 *   0000-0007  decoder dispatch table (4 near offsets)
 *   0008-0027  copy of gaiP4 used by the decoder (code segment)
 *   0028-0103  adpcmDecode4Bit
 *   0104-02A7  decode: mono, 8-bit output
 *   02A8-0443  decode: mono, 16-bit output
 *   0444-073F  decode: stereo, 8-bit output
 *   0740-0A20  decode: stereo, 16-bit output
 *   0A22-0A29  encoder dispatch table
 *   0A2A-0A49  copy of gaiP4 used by the encoder
 *   0A4A-0AB9  adpcmEncode4Bit_RealTime
 *   0ABA-0C89  encode: mono, 8-bit input
 *   0C8A-0E51  encode: mono, 16-bit input
 *   0E52-1061  encode: stereo, 8-bit input
 *   1062-125F  encode: stereo, 16-bit input
 *   1260-12AE  unreferenced first-delta helper (see the end of this file)
 *
 * The four variants of each routine are copies of one loop that differ
 * only in how samples are read or written; here each routine is one loop
 * with the sample size as a parameter.
 *
 * Arithmetic that matches the assembly rather than the C encoder:
 *  - The step size update keeps bits 8..23 of the 32-bit product
 *    (P4 * delta), and the clamp to 16 tests the sign of (delta - 16) in
 *    16-bit arithmetic.  A step size from -32768 to -32753 therefore
 *    wraps past the test and stays negative.  This happens when the
 *    product shifted right by 8 is 32768..32783, which takes particular
 *    input (acmtest uses a crafted 13-sample sequence), or with a step
 *    size of 10923..10927 in a block header followed by code -8.
 *  - The encoder divides with a 32/16-bit IDIV, which cannot overflow for
 *    16-bit samples and step sizes outside that window.
 */

#include <windows.h>
#include "msadpcm.h"

/* seg4:0008 and seg4:0A2A: two copies of gaiP4 in the code segment */
static const int _based(_segname("_CODE")) aiP4Decode[16] =
    { 230, 230, 230, 230, 307, 409, 512, 614, 768, 614, 512, 409, 307, 230, 230, 230 };
static const int _based(_segname("_CODE")) aiP4Encode[16] =
    { 230, 230, 230, 230, 307, 409, 512, 614, 768, 614, 512, 409, 307, 230, 230, 230 };

/* channel state; the assembly keeps it in [bp-xx] slots, left channel first */
typedef struct tADPCMCHANNEL {
    int     iCoef1;
    int     iCoef2;
    int     iDelta;
    int     iSamp1;                 /* most recent sample */
    int     iSamp2;
} ADPCMCHANNEL, FAR *LPADPCMCHANNEL;

/* new step size, with the 16-bit truncation and sign test of the assembly */
#define NEXT_DELTA(aiP4, iCode, iDelta, iNew)                                   \
    {                                                                           \
        (iNew) = (int)(((long)(aiP4)[(iCode) & MSADPCM_OUTPUT4MASK] * (iDelta)) \
                       >> MSADPCM_PSCALE);                                      \
        if ((int)((UINT)(iNew) - MSADPCM_DELTA4_MIN) < 0)                       \
            (iNew) = MSADPCM_DELTA4_MIN;                                        \
    }

/* clamp a 32-bit sample to 16 bits */
#define CLAMP16(l)                                                              \
    (((l) > 32767L) ? 32767 : (((l) < -32768L) ? -32768 : (int)(l)))


/*
 * Decode one signed 4-bit code (-8..7) for a channel.  The new step size
 * is computed first; the sample uses the old one.
 */
static int NEAR DecodeSample(LPADPCMCHANNEL pch, int iInput)
{
    int     iDelta;
    long    lSamp;

    iDelta = pch->iDelta;
    NEXT_DELTA(aiP4Decode, iInput, iDelta, pch->iDelta);

    lSamp  = (((long)pch->iSamp1 * pch->iCoef1) + ((long)pch->iSamp2 * pch->iCoef2)) >> MSADPCM_CSCALE;
    lSamp += (long)iInput * iDelta;

    pch->iSamp2 = pch->iSamp1;
    pch->iSamp1 = CLAMP16(lSamp);

    return (pch->iSamp1);
}

/* store one decoded sample; 8-bit output is the high byte plus 128 */
#define PUT_SAMPLE(pb, nBits, iSamp)                                            \
    {                                                                           \
        if (8 == (nBits))                                                       \
        {                                                                       \
            *(pb)++ = (BYTE)(((UINT)(iSamp) >> 8) + 0x80);                      \
        }                                                                       \
        else                                                                    \
        {                                                                       \
            *(short _huge *)(pb) = (short)(iSamp);                              \
            (pb) += 2;                                                          \
        }                                                                       \
    }

/* read a little-endian 16-bit word from the source */
#define GET_WORD(pb)    ((pb) += 2, *(short _huge *)((pb) - 2))


/*
 * seg4:0028-0103  adpcmDecode4Bit  (FAR PASCAL, retf 14h)
 *
 * Decodes cbSrcLength bytes of MS ADPCM.  Returns the number of PCM bytes
 * as computed before decoding, or 0 if a block has a bad predictor.
 *
 * Quirks of the original:
 *  - The output size is computed up front from the block count:
 *    wSamplesPerBlock frames per whole block, plus 2 frames per channel
 *    for the header and 2 (mono) or 1 (stereo) per data byte in a partial
 *    last block.  A partial block shorter than its header is ignored.
 *    The decoding loop counts pairs of frames, so with an odd frame count
 *    (a stereo partial block of odd length) the last frame is not written
 *    but is included in the return value.
 *  - The block count is a 16-bit DIV of cbSrcLength by nBlockAlign, which
 *    raises a divide fault for 65536 or more blocks.  The C computes the
 *    quotient in 32 bits and keeps the low 16.
 *  - The decoder does not skip to the next nBlockAlign boundary: after the
 *    header it decodes (wSamplesPerBlock / 2 - 1) data bytes per channel
 *    pair and starts the next block right after them.  This is only
 *    correct when nBlockAlign and wSamplesPerBlock agree, which
 *    adpcmIsValidFormat does not check.
 *  - The coefficients are copied (wNumCoef words each) from gaiCoef1 and
 *    gaiCoef2 in DGROUP, not from the format, into stack arrays.  A
 *    predictor index is rejected only if it is greater than wNumCoef, so
 *    index 7 reads one uninitialised stack word of each array.
 *  - If wSamplesPerBlock is below 4 the per-block data count is 0 or
 *    wraps, and the 16-bit loop counter runs 65536 times.
 *  - Data already written stays in the buffer when a bad predictor stops
 *    the decoding.
 */
DWORD FNGLOBAL adpcmDecode4Bit(LPWAVEFORMATEX pwfADPCM, HPBYTE pbSrc,
                               LPWAVEFORMATEX pwfPCM, HPBYTE pbDst,
                               DWORD cbSrcLength)
{
    LPADPCMWAVEFORMAT   pwfadpcm;
    BYTE                nBitsPerSample;         /* [bp-0F] */
    BYTE                nChannels;              /* [bp-0D] */
    BYTE                nNumCoef;               /* [bp-0E] */
    UINT                cPairsPerBlock;         /* [bp-0A] */
    DWORD               cPairs;                 /* [bp-08] frame pairs left */
    DWORD               cbDst;                  /* [bp-04] */
    UINT                cBlocks;
    UINT                cbPartial;
    UINT                cbHeader;
    UINT                n;                      /* [bp-0C] */
    UINT                u;
    DWORD               dwFrames;
    DWORD               dw;
    BYTE                b;
    BYTE                bPredictor;
    int                 aiCoef1[256];           /* [bp-224] */
    int                 aiCoef2[256];           /* [bp-424] */
    ADPCMCHANNEL        ach[MSADPCM_MAX_CHANNELS];

    pwfadpcm = (LPADPCMWAVEFORMAT)pwfADPCM;

    nBitsPerSample = (BYTE)pwfPCM->wBitsPerSample;
    nChannels      = (BYTE)pwfPCM->nChannels;
    nNumCoef       = (BYTE)pwfadpcm->wNumCoef;
    cPairsPerBlock = (pwfadpcm->wSamplesPerBlock >> 1) - 1;

    cBlocks   = (UINT)(cbSrcLength / pwfadpcm->wfx.nBlockAlign);
    cbPartial = (UINT)(cbSrcLength % pwfadpcm->wfx.nBlockAlign);
    dwFrames  = (DWORD)cBlocks * pwfadpcm->wSamplesPerBlock;
    cbHeader  = MSADPCM_HEADER_LENGTH * nChannels;

    if ((0 != cbPartial) && (cbPartial >= cbHeader))
    {
        dw = cbPartial - cbHeader;
        if (1 == nChannels)
            dw <<= 1;
        dwFrames += dw + 2;
    }

    /* CL = (BYTE)(nChannels - 1 + nBitsPerSample / 16); SHL EBX,CL */
    cbDst  = dwFrames << ((BYTE)(nChannels - 1 + (nBitsPerSample >> 4)) & 31);
    cPairs = dwFrames >> 1;

    /* rep movsw from DS:0020 and DS:002E; wNumCoef is 7 once the stream is open */
    for (u = 0; u < nNumCoef; u++)
    {
        aiCoef1[u] = gaiCoef1[u];
        aiCoef2[u] = gaiCoef2[u];
    }

    if (nChannels & 2)
    {
        /* seg4:0444 (8-bit) and seg4:0740 (16-bit) */
        while (0 != cPairs)
        {
            bPredictor = *pbSrc++;
            b          = *pbSrc++;

            if (bPredictor > nNumCoef)
                return (0L);
            ach[0].iCoef1 = aiCoef1[bPredictor];
            ach[0].iCoef2 = aiCoef2[bPredictor];

            if (b > nNumCoef)
                return (0L);
            ach[1].iCoef1 = aiCoef1[b];
            ach[1].iCoef2 = aiCoef2[b];

            ach[0].iDelta = GET_WORD(pbSrc);
            ach[1].iDelta = GET_WORD(pbSrc);
            ach[0].iSamp1 = GET_WORD(pbSrc);
            ach[1].iSamp1 = GET_WORD(pbSrc);
            ach[0].iSamp2 = GET_WORD(pbSrc);
            ach[1].iSamp2 = GET_WORD(pbSrc);

            /* the header's two sample frames, oldest first */
            PUT_SAMPLE(pbDst, nBitsPerSample, ach[0].iSamp2);
            PUT_SAMPLE(pbDst, nBitsPerSample, ach[1].iSamp2);
            PUT_SAMPLE(pbDst, nBitsPerSample, ach[0].iSamp1);
            PUT_SAMPLE(pbDst, nBitsPerSample, ach[1].iSamp1);

            if (0 == --cPairs)
                break;

            n = (cPairs >= cPairsPerBlock) ? cPairsPerBlock : (UINT)cPairs;
            cPairs -= n;

            /* two bytes (two frames) per pass: left in the high nibble */
            do
            {
                b = *pbSrc++;
                PUT_SAMPLE(pbDst, nBitsPerSample, DecodeSample(&ach[0], (signed char)b >> 4));
                PUT_SAMPLE(pbDst, nBitsPerSample, DecodeSample(&ach[1], (signed char)(b << 4) >> 4));

                b = *pbSrc++;
                PUT_SAMPLE(pbDst, nBitsPerSample, DecodeSample(&ach[0], (signed char)b >> 4));
                PUT_SAMPLE(pbDst, nBitsPerSample, DecodeSample(&ach[1], (signed char)(b << 4) >> 4));
            } while (0 != --n);
        }
    }
    else
    {
        /* seg4:0104 (8-bit) and seg4:02A8 (16-bit) */
        while (0 != cPairs)
        {
            bPredictor = *pbSrc++;
            if (bPredictor > nNumCoef)
                return (0L);
            ach[0].iCoef1 = aiCoef1[bPredictor];
            ach[0].iCoef2 = aiCoef2[bPredictor];

            ach[0].iDelta = GET_WORD(pbSrc);
            ach[0].iSamp1 = GET_WORD(pbSrc);
            ach[0].iSamp2 = GET_WORD(pbSrc);

            PUT_SAMPLE(pbDst, nBitsPerSample, ach[0].iSamp2);
            PUT_SAMPLE(pbDst, nBitsPerSample, ach[0].iSamp1);

            if (0 == --cPairs)
                break;

            n = (cPairs >= cPairsPerBlock) ? cPairsPerBlock : (UINT)cPairs;
            cPairs -= n;

            /* one byte (two samples) per pass: high nibble first */
            do
            {
                b = *pbSrc++;
                PUT_SAMPLE(pbDst, nBitsPerSample, DecodeSample(&ach[0], (signed char)b >> 4));
                PUT_SAMPLE(pbDst, nBitsPerSample, DecodeSample(&ach[0], (signed char)(b << 4) >> 4));
            } while (0 != --n);
        }
    }

    return (cbDst);
}


/*
 * Encode one sample for a channel with coefficient set 1 (512, -256),
 * whose prediction 2 * iSamp1 - iSamp2 is exact.  Returns the 4-bit code.
 */
static int NEAR EncodeSample(LPADPCMCHANNEL pch, int iSample)
{
    long    lPrediction;
    long    lSamp;
    int     iOutput;
    int     iDelta;

    lPrediction = 2L * pch->iSamp1 - pch->iSamp2;

    iOutput = (int)(((long)iSample - lPrediction) / pch->iDelta);
    if (iOutput > MSADPCM_OUTPUT4MAX)
        iOutput = MSADPCM_OUTPUT4MAX;
    if (iOutput < MSADPCM_OUTPUT4MIN)
        iOutput = MSADPCM_OUTPUT4MIN;

    iDelta = pch->iDelta;
    NEXT_DELTA(aiP4Encode, iOutput, iDelta, pch->iDelta);

    lSamp = (long)iOutput * iDelta + lPrediction;

    pch->iSamp2 = pch->iSamp1;
    pch->iSamp1 = CLAMP16(lSamp);

    return (iOutput & MSADPCM_OUTPUT4MASK);
}

/* fetch one PCM sample: 8-bit input becomes (b ^ 80h) << 8 */
#define GET_SAMPLE(pb, nBits)                                                   \
    ((8 == (nBits)) ? (int)((UINT)(BYTE)(*(pb)++ ^ 0x80) << 8) : GET_WORD(pb))


/*
 * seg4:0A4A-0AB9  adpcmEncode4Bit_RealTime  (FAR PASCAL, retf 14h)
 *
 * Fast encoder for streams opened without ACM_STREAMOPENF_NONREALTIME.
 * Every block uses coefficient set 1 and starts with a step size of 128;
 * there is no search.  Returns the number of bytes written.
 *
 *  - Blocks hold wSamplesPerBlock rounded down to even frames; the last
 *    one holds what is left and is not padded.  A remainder of fewer
 *    than 2 frames is dropped.
 *  - Encoding stops altogether after a block of exactly 2 frames (a
 *    header only), so a wSamplesPerBlock of 2 or 3 encodes one block.
 *  - In mono the loop takes two samples per pass and ends only when the
 *    count reaches zero, so an odd count would run on through memory;
 *    acmdStreamConvert prevents that.  In stereo each pass is one frame.
 *  - wNumCoef is read into [bp-0C] and not used.
 */
DWORD FNGLOBAL adpcmEncode4Bit_RealTime(LPWAVEFORMATEX pwfPCM, HPBYTE pbSrc,
                                        LPWAVEFORMATEX pwfADPCM, HPBYTE pbDst,
                                        DWORD cbSrcLength)
{
    LPADPCMWAVEFORMAT   pwfadpcm;
    UINT                cSamplesPerBlock;       /* [bp-06] */
    UINT                wBitsPerSample;         /* [bp-0E] */
    UINT                nChannels;              /* [bp-0A] */
    DWORD               cbDst;                  /* [bp-04] */
    DWORD               cFrames;                /* [bp+06] */
    UINT                n;                      /* [bp-08] */
    int                 iCode;
    ADPCMCHANNEL        ach[MSADPCM_MAX_CHANNELS];

    pwfadpcm = (LPADPCMWAVEFORMAT)pwfADPCM;

    cSamplesPerBlock = (pwfadpcm->wSamplesPerBlock >> 1) << 1;
    wBitsPerSample   = pwfPCM->wBitsPerSample;
    nChannels        = pwfPCM->nChannels;
    cbDst            = 0;

    /* frames in the source: bytes >> (bits/16 + stereo) */
    cFrames = cbSrcLength >> ((wBitsPerSample >> 4) + ((1 == nChannels) ? 0 : 1));

    ach[0].iCoef1 = ach[1].iCoef1 = 512;
    ach[0].iCoef2 = ach[1].iCoef2 = -256;

    for (;;)
    {
        n = cSamplesPerBlock;
        if (cFrames < n)
        {
            n = (UINT)cFrames;
            if (0 == n)
                break;
        }
        cFrames -= n;

        if (n < 2)
            break;

        /*
         * header: predictor 1 and delta 128 for each channel, then the
         * second and first sample frames
         */
        ach[0].iDelta = ach[1].iDelta = 128;

        if (1 == nChannels)
        {
            ach[0].iSamp2 = GET_SAMPLE(pbSrc, wBitsPerSample);
            ach[0].iSamp1 = GET_SAMPLE(pbSrc, wBitsPerSample);

            *pbDst++ = 1;
            *(short _huge *)pbDst = 128;            pbDst += 2;
            *(short _huge *)pbDst = (short)ach[0].iSamp1; pbDst += 2;
            *(short _huge *)pbDst = (short)ach[0].iSamp2; pbDst += 2;

            cbDst += MSADPCM_HEADER_LENGTH;
        }
        else
        {
            ach[0].iSamp2 = GET_SAMPLE(pbSrc, wBitsPerSample);
            ach[1].iSamp2 = GET_SAMPLE(pbSrc, wBitsPerSample);
            ach[0].iSamp1 = GET_SAMPLE(pbSrc, wBitsPerSample);
            ach[1].iSamp1 = GET_SAMPLE(pbSrc, wBitsPerSample);

            *pbDst++ = 1;
            *pbDst++ = 1;
            *(short _huge *)pbDst = 128;            pbDst += 2;
            *(short _huge *)pbDst = 128;            pbDst += 2;
            *(short _huge *)pbDst = (short)ach[0].iSamp1; pbDst += 2;
            *(short _huge *)pbDst = (short)ach[1].iSamp1; pbDst += 2;
            *(short _huge *)pbDst = (short)ach[0].iSamp2; pbDst += 2;
            *(short _huge *)pbDst = (short)ach[1].iSamp2; pbDst += 2;

            cbDst += 2 * MSADPCM_HEADER_LENGTH;
        }

        n -= 2;
        if (0 == n)
            break;

        if (1 == nChannels)
        {
            do
            {
                iCode = EncodeSample(&ach[0], GET_SAMPLE(pbSrc, wBitsPerSample)) << 4;
                iCode |= EncodeSample(&ach[0], GET_SAMPLE(pbSrc, wBitsPerSample));
                *pbDst++ = (BYTE)iCode;
                cbDst++;
                n -= 2;
            } while (0 != n);
        }
        else
        {
            do
            {
                iCode = EncodeSample(&ach[0], GET_SAMPLE(pbSrc, wBitsPerSample)) << 4;
                iCode |= EncodeSample(&ach[1], GET_SAMPLE(pbSrc, wBitsPerSample));
                *pbDst++ = (BYTE)iCode;
                cbDst++;
            } while (0 != --n);
        }
    }

    return (cbDst);
}


/*
 * seg4:1260-12AE  unreferenced helper, not reconstructed as C
 *
 * A near routine with a register argument (BX = SS-relative pointer to
 * samples) that looks like a version of adpcmEncode4Bit_FirstDelta for
 * coefficient set 1.  It cannot work: the second load overwrites BX,
 * which the third load then uses as the base pointer.
 *
 *      push  si
 *      xor   edx,edx
 *      mov   cx,3
 *      xor   si,si
 * @@:  movsx eax,word [ss:bx+si+4]
 *      add   eax,eax                   ; 2 * s[k+2]
 *      movsx ebx,word [ss:bx+si+2]     ; clobbers the base pointer
 *      sub   eax,ebx                   ; - s[k+1]
 *      movsx ebx,word [ss:bx+si]       ; reads SS:(s[k+1] + si)
 *      cmp   eax,ebx                   ; edx += |eax - ebx|
 *      jng   neg
 *      sub   eax,ebx
 *      jmp   add
 * neg: neg   eax
 *      add   eax,ebx
 * add: add   edx,eax
 *      add   si,2
 *      loop  @b
 *      mov   eax,edx
 *      shld  edx,eax,16
 *      mov   cx,12
 *      idiv  cx                        ; faults if the sum is 393216 or more
 *      cmp   ax,16
 *      jge   @f
 *      mov   ax,16
 * @@:  pop   si
 *      ret
 */
