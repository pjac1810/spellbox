/* SPDX-License-Identifier: GPL-3.0-only */
/* SPELLBOX's speech chip, in integers: the frames of speech.h (energy, pitch, ten reflection coefficients each
 * 25 ms) drive a chirp or noise through a 10-stage lattice filter at 8 kHz, interpolated eight times a frame,
 * out through an 8-bit DAC and a small speaker. The bending knobs (SP) reach into it the way a soldering iron
 * would: the clock, the pitch, the frame rate, corrupted coefficients, frames played twice. */
#include "fm1.h"
#include "sb.h"
#include "speech.h"

volatile sp_param_t SP = {0, 64, 64, 0, 0, 0, 0, 64};
volatile uint16_t sp_mark;
volatile uint8_t sp_sing, sp_level;

/* ---- the queue: loop() writes, audio() reads */
#define QN 64
static volatile uint16_t q[QN];
static volatile uint32_t q_wr, q_rd, flush_to, flush_seq;
static uint32_t flush_seen;
static volatile uint8_t playing; /* audio(): something is under way */

void sp_queue(uint16_t item) {
  if (q_wr - q_rd >= QN) return;
  q[q_wr % QN] = item;
  RING_PUBLISH();
  q_wr = q_wr + 1;
}
void sp_stop(void) {
  flush_to = q_wr;
  RING_PUBLISH();
  flush_seq = flush_seq + 1;
}
int sp_busy(void) { return q_wr != q_rd || playing || flush_seq != flush_seen; }

/* ---- the chip */
static const uint8_t KBITS[10] = SP_KBITS;
static uint32_t bitpos, frame_start[4];
static uint8_t talking, stopping;
static int32_t e_old, e_new, p_old, p_new, k_old[10], k_new[10]; /* p: period x 16 */
static int32_t e_cur, p_cur, k_cur[10];
static int32_t b[11];
static uint32_t pc;          /* where the pitch period is, x 16 */
static uint16_t lfsr = 0x1u; /* the noise */
static uint32_t n8;          /* samples of 8 kHz into the frame */
static uint32_t frame_len, step_len;
static int32_t deemph, dac;
/* beeps and silences */
static uint32_t tone_left, tone_inc, tone_ph, pause_left;
/* 44.1 kHz */
static uint32_t acc; /* 8 kHz samples due, x 65536 */
static int32_t hp_x, hp_y, lp;

static uint32_t getbits(int n) {
  uint32_t v = 0;
  for (int i = 0; i < n; i++, bitpos++) v = v << 1 | (uint32_t)(SP_BITS[bitpos >> 3] >> (7 - (bitpos & 7)) & 1u);
  return v;
}

/* 2^(-n/12) x 4096, n = -12..12 */
static const uint16_t SEMI[25] = {8192, 7732, 7298, 6889, 6502, 6137, 5793, 5468, 5161, 4871, 4598, 4340, 4096,
                                  3866, 3649, 3444, 3251, 3069, 2896, 2734, 2580, 2436, 2299, 2170, 2048};

static uint32_t clock_hz(void) {
  int32_t c = SP.clock < 0 ? 0 : SP.clock > 127 ? 127 : SP.clock;
  return c <= 64 ? 4000u + (uint32_t)c * 4000u / 64u : 8000u + (uint32_t)(c - 64) * 6000u / 63u;
}

/* the next frame from the bits: 0 = it went on, 1 = the utterance is over */
static int next_frame(void) {
  for (int i = 0; i < 10; i++) k_old[i] = k_new[i];
  e_old = e_new, p_old = p_new;
  /* STUTTER: back a frame now and then */
  if (SP.stutter > 0 && frame_start[1] && (int32_t)(random32() >> 25) < SP.stutter) bitpos = frame_start[1];
  for (int i = 3; i > 0; i--) frame_start[i] = frame_start[i - 1];
  frame_start[0] = bitpos;
  uint32_t e = getbits(4);
  if (e == 15) return 1;
  if (e == 0) {
    e_new = 0;
    return 0;
  }
  uint32_t rep = getbits(1), p = getbits(5);
  e_new = SP_ENERGY[e];
  int32_t per = (int32_t)SP_PERIOD[p] * 16;
  if (p && sp_sing) { /* sing: the period of the note at this clock */
    uint32_t f100 = note_inc(sp_sing) / 974u;
    per = f100 ? (int32_t)(clock_hz() * 1600u / f100) : per;
  } else if (p) {
    int32_t m = SP.mono < 0 ? 0 : SP.mono > 127 ? 127 : SP.mono; /* monotone: toward 100 Hz */
    per += ((80 * 16 - per) * m) >> 7;
    int s = SP.pitch < -12 ? -12 : SP.pitch > 12 ? 12 : SP.pitch;
    per = (per * SEMI[s + 12]) >> 12;
  }
  p_new = p ? (per < 32 ? 32 : per) : 0;
  if (!rep) {
    int n = p ? 10 : 4;
    for (int i = 0; i < 10; i++) {
      if (i < n) k_new[i] = SP_KTAB[i][getbits(KBITS[i])];
      else k_new[i] = 0;
    }
  }
  /* BEND: a coefficient or two knocked off its value */
  if (SP.bend > 0) {
    uint32_t r = random32();
    if ((int32_t)(r >> 25) < SP.bend) {
      int i = (int)((r >> 8) % 10);
      k_new[i] = SP_KTAB[i][(r >> 12) % (1u << KBITS[i])] * (SP.bend > 96 ? -1 : 1);
    }
    if (SP.bend > 80 && (r & 0xFFu) < (uint32_t)(SP.bend - 80) * 3 && p_new) p_new = p_new * ((r >> 20 & 3u) + 1) / 2 + 32;
  }
  return 0;
}

static void start(uint16_t item) {
  if (item & 0x8000u) { /* a beep */
    uint32_t note = item >> 6 & 0x7Fu, n = item & 0x3Fu;
    tone_inc = note_inc((int)note), tone_left = n * 1102u + 1u, tone_ph = 0;
  } else if ((item & 0xE000u) == 0x6000u) {
    pause_left = (item & 0x1FFFu) * 441u + 1u;
  } else if (item & 0x4000u) {
    sp_mark = item & 0x1FFFu;
  } else if (item < SP_COUNT) {
    bitpos = SP_AT[item];
    for (int i = 0; i < 4; i++) frame_start[i] = 0;
    for (int i = 0; i < 10; i++) k_new[i] = 0;
    for (int i = 0; i < 11; i++) b[i] = 0;
    e_new = 0, p_new = 0, n8 = 0, talking = 1, stopping = 0, deemph = 0;
    next_frame();
    e_old = 0;
  }
}

/* one sample of the chip at its clock */
static int32_t chip(void) {
  if (!talking) {
    dac = 0;
    return 0;
  }
  if (n8 == 0) { /* a new frame */
    int32_t s = SP.speed < 0 ? 0 : SP.speed > 127 ? 127 : SP.speed;
    frame_len = s <= 64 ? 400u - (uint32_t)s * 200u / 64u : 200u - (uint32_t)(s - 64) * 100u / 63u;
    step_len = frame_len / 8u;
    if (stopping) {
      talking = 0;
      return 0;
    }
    if (next_frame()) e_new = 0, p_new = p_old, stopping = 1;
  }
  if (n8 % step_len == 0) { /* interpolate, eight steps a frame (not across voiced / unvoiced / silence) */
    int32_t st = (int32_t)(n8 / step_len) + 1;
    if (st > 8) st = 8;
    int smooth = (p_old != 0) == (p_new != 0) && e_old && e_new;
    if (smooth) {
      e_cur = e_old + (e_new - e_old) * st / 8;
      p_cur = p_old + (p_new - p_old) * st / 8;
      for (int i = 0; i < 10; i++) k_cur[i] = k_old[i] + (k_new[i] - k_old[i]) * st / 8;
    } else {
      e_cur = st >= 2 || !e_old ? e_new : e_old / 2;
      p_cur = p_new;
      for (int i = 0; i < 10; i++) k_cur[i] = k_new[i];
    }
  }
  n8++;
  if (n8 >= frame_len) n8 = 0;

  /* the excitation */
  int32_t x;
  if (p_cur) {
    pc += 16;
    if (pc >= (uint32_t)p_cur) pc -= (uint32_t)p_cur;
    uint32_t i = pc >> 4;
    x = i < 41 ? (SP_CHIRP[i] * e_cur) >> 7 : 0;
  } else {
    lfsr = (uint16_t)((lfsr >> 1) ^ (-(lfsr & 1u) & 0xB400u));
    x = (lfsr & 1u) ? e_cur : -e_cur;
    pc = 0;
  }
  /* the lattice */
  int32_t u = x * 8;
  for (int i = 9; i >= 0; i--) {
    u -= (k_cur[i] * b[i]) >> 14;
    u = u > 65535 ? 65535 : u < -65535 ? -65535 : u;
    int32_t nb = b[i] + ((k_cur[i] * u) >> 14);
    b[i + 1] = nb > 65535 ? 65535 : nb < -65535 ? -65535 : nb;
  }
  b[0] = u;
  deemph = u + ((deemph * 29) >> 5); /* a little of the low end back */
  int32_t d = deemph >> 8;
  d = d > 127 ? 127 : d < -128 ? -128 : d; /* the 8-bit DAC */
  int c = SP.crush < 0 ? 0 : SP.crush > 5 ? 5 : SP.crush;
  d = (d >> c) * (1 << c);
  dac = d;
  return d;
}

void sp_init(void) {
  for (int i = 0; i < 11; i++) b[i] = 0;
  talking = 0;
}

void sp_render(int32_t *out, uint32_t frames) {
  if (flush_seq != flush_seen) {
    flush_seen = flush_seq;
    q_rd = flush_to;
    talking = 0, tone_left = 0, pause_left = 0;
  }
  uint32_t step = clock_hz() * 65536u / SAMPLE_RATE;
  int32_t t = SP.tone < 0 ? 0 : SP.tone > 127 ? 127 : SP.tone;
  int32_t lpk = 3000 + t * 220; /* the speaker's top, as a one-pole coefficient x 32768 */
  uint32_t peak = 0;
  for (uint32_t n = 0; n < frames; n++) {
    /* the next thing in the queue */
    while (!talking && !tone_left && !pause_left && q_rd != q_wr) {
      uint16_t it = q[q_rd % QN];
      q_rd = q_rd + 1;
      start(it);
    }
    playing = (uint8_t)(talking || tone_left || pause_left);
    int32_t s = 0;
    if (talking) {
      acc += step;
      while (acc >= 65536u) acc -= 65536u, chip();
      s = dac * 220; /* the DAC holds between samples */
    } else if (tone_left) {
      tone_left--;
      tone_ph += tone_inc;
      s = (tone_ph >> 31) ? 8000 : -8000;
      if (tone_left < 200) s = s * (int32_t)tone_left / 200;
    } else if (pause_left) {
      pause_left--;
    }
    /* the speaker: no bass, a soft top */
    int32_t hp = s - hp_x + ((hp_y * 31700) >> 15);
    hp_x = s, hp_y = hp;
    lp += ((hp - lp) * lpk) >> 15;
    int32_t o = lp > 32767 ? 32767 : lp < -32767 ? -32767 : lp;
    uint32_t a = (uint32_t)(o < 0 ? -o : o);
    if (a > peak) peak = a;
    out[2 * n] += o;
    out[2 * n + 1] += o;
  }
  sp_level = (uint8_t)(peak >> 7 > 255 ? 255 : peak >> 7);
}
