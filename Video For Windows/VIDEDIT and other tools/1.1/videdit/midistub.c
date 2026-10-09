/*
 * midistub.c - code segment 8 of VIDEDIT.EXE (0x008F bytes, no
 * relocations): the MIDI track, compiled out.
 *
 * Empty versions of a MIDI counterpart of the audio track (segment 7):
 * a MIDI track never has frames, so File Extract never offers MIDI and
 * the save and frame-information code skip it.  Only the sync offset is
 * real: Synchronize sets it and Undo swaps it, like the audio offset.
 * The data is the module's own part of DGROUP (DS:048A-04A7).
 *
 * Most of the stubs are not referenced; they are named by address here,
 * with the argument sizes and results of the original.
 */

#include "videdit.h"
#include "vemain.h"

DWORD   gdwMidiOffset;                  /* DS:04A2 */
DWORD   gdwMidiOffsetUndo;              /* DS:1C7C */

/* s08:0000-0005  (NEAR PASCAL, unreferenced) */
DWORD NEAR PASCAL MidiStub0000(WORD w)
{
    return 0L;
}

/* s08:0006-000F  (unreferenced) */
void FAR MidiStub0006(void)
{
    gdwMidiOffset = 0;
}

/* s08:0010-0015  (FAR PASCAL, unreferenced) */
BOOL FAR PASCAL MidiStub0010(WORD w1, WORD w2, WORD w3)
{
    return TRUE;
}

/* s08:0016-001A  (FAR PASCAL, unreferenced) */
BOOL FAR PASCAL MidiStub0016(WORD w1, WORD w2)
{
    return FALSE;
}

/* s08:001C-001E  MidiNumFrames */
int FAR MidiNumFrames(void)
{
    return 0;
}

/* s08:0020-0022  (unreferenced) */
int FAR MidiStub0020(void)
{
    return 0;
}

/* s08:0024  (unreferenced) */
void FAR MidiStub0024(void)
{
}

/* s08:0026-0028  (unreferenced) */
int FAR MidiStub0026(void)
{
    return 0;
}

/* s08:002A-002E  MidiAdjustOffset  (FAR PASCAL) */
BOOL FAR PASCAL MidiAdjustOffset(LONG lMilliseconds)
{
    return FALSE;
}

/*
 * s08:0030-0050  MidiSwapOffset
 */
BOOL FAR MidiSwapOffset(void)
{
    DWORD dw;

    dw = gdwMidiOffset;
    gdwMidiOffset = gdwMidiOffsetUndo;
    gdwMidiOffsetUndo = dw;
    return TRUE;
}

/* s08:0052-0054  (FAR PASCAL, unreferenced) */
void FAR PASCAL MidiStub0052(WORD w1, WORD w2)
{
}

/* s08:0056-0059  (unreferenced) */
BOOL FAR MidiStub0056(void)
{
    return TRUE;
}

/* s08:005A-005F  (FAR PASCAL, unreferenced) */
BOOL FAR PASCAL MidiStub005a(WORD w1, WORD w2, WORD w3, WORD w4, WORD w5)
{
    return TRUE;
}

/* s08:0060  (unreferenced) */
void FAR MidiStub0060(void)
{
}

/* s08:0062  (unreferenced) */
void FAR MidiStub0062(void)
{
}

/* s08:0064-0075  MidiFrameBytes  (FAR PASCAL) */
void FAR PASCAL MidiFrameBytes(WORD wFrame, LPDWORD lpdwBytes)
{
    *lpdwBytes = 0;
}

/*
 * s08:0076-007B  MidiGetRangeElement  (FAR PASCAL)
 *
 * Returns TRUE without setting *pmedid.
 */
BOOL FAR PASCAL MidiGetRangeElement(WORD wStart, WORD wCount, MEDID NEAR *pmedid)
{
    return TRUE;
}

/* s08:007C-007E  (unreferenced) */
int FAR MidiStub007c(void)
{
    return 0;
}

/* s08:0080-0085  (FAR PASCAL, unreferenced) */
BOOL FAR PASCAL MidiStub0080(WORD w1, WORD w2)
{
    return TRUE;
}

/* s08:0086-0088  (unreferenced) */
int FAR MidiStub0086(void)
{
    return 0;
}

/* s08:008A-008F  (FAR PASCAL, unreferenced) */
BOOL FAR PASCAL MidiStub008a(WORD w1, WORD w2)
{
    return TRUE;
}
