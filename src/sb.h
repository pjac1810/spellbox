/* SPDX-License-Identifier: GPL-3.0-only */
/* SPELLBOX: shared definitions */
#ifndef SB_H
#define SB_H
#include <stdint.h>

#ifndef RING_PUBLISH /* what the other side reads is written before the index that says so */
#define RING_PUBLISH() __asm__ volatile("" ::: "memory")
#endif

/* ---- speech.c: the LPC speech chip, beeps, and a queue of what to say */
#define SPQ_SAY(id) ((uint16_t)(id))                                   /* an utterance of speech.h */
#define SPQ_TONE(note, n25ms) ((uint16_t)(0x8000u | (note) << 6 | (n25ms))) /* a beep, MIDI note, 25 ms units 1..63 */
#define SPQ_MARK(v) ((uint16_t)(0x4000u | (v)))                        /* sp_mark = v when reached (v < 0x2000) */
#define SPQ_PAUSE(n10ms) ((uint16_t)(0x6000u | (n10ms)))               /* silence */

typedef struct { /* the circuit-bending knobs, written by loop() */
  int16_t pitch;   /* semitones -12..12 */
  int16_t speed;   /* 0..127, 64 normal */
  int16_t clock;   /* 0..127, 64 = 8 kHz (4 .. 14 kHz): pitch, speed and voice together */
  int16_t bend;    /* 0..127: corrupted coefficients */
  int16_t stutter; /* 0..127: frames played again */
  int16_t mono;    /* 0..127: the intonation flattened */
  int16_t crush;   /* 0..5: bits taken off the 8-bit DAC */
  int16_t tone;    /* 0..127: the speaker, dull .. bright */
} sp_param_t;
extern volatile sp_param_t SP;
extern volatile uint16_t sp_mark;   /* the last SPQ_MARK reached */
extern volatile uint8_t sp_sing;    /* a MIDI note: voiced frames sing at it (0: speak) */
extern volatile uint8_t sp_level;   /* the voice's loudness now, 0..255 (the display glows with it) */

void sp_init(void);
void sp_queue(uint16_t item); /* from loop() */
void sp_stop(void);           /* from loop(): forget the queue, stop talking */
int sp_busy(void);            /* something is queued or playing */
void sp_render(int32_t *out, uint32_t frames);

#endif
