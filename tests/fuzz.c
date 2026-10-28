/* SPDX-License-Identifier: GPL-3.0-only */
/* Host fuzz of the speech chip: random utterances, beeps, marks, stops and bending knobs, under UBSan */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sb.h"
#include "speech.h"
int32_t sine(uint32_t p) { (void)p; return 0; }
static const uint32_t INC_C9[12] = {815363807u, 863847862u, 915214929u, 969636441u, 1027294024u, 1088380105u,
                                    1153098554u, 1221665363u, 1294309365u, 1371273005u, 1452813141u, 1539201906u};
uint32_t note_inc(int note) { if (note < 0) note = 0; if (note > 127) note = 127; return INC_C9[note % 12] >> (10 - note / 12); }
static uint32_t rs = 0x1234567u;
uint32_t random32(void) { rs ^= rs << 13, rs ^= rs >> 17, rs ^= rs << 5; return rs; }
int main(void) {
  srand(5);
  sp_init();
  long blocks = 0, bad = 0;
  for (int b = 0; b < 60000; b++) {
    int r = rand() % 13;
    if (r == 0) sp_queue(SPQ_SAY(rand() % SP_COUNT));
    else if (r == 1) sp_queue(SPQ_TONE(rand() % 128, 1 + rand() % 63));
    else if (r == 2) sp_queue(SPQ_MARK(rand() % 0x2000));
    else if (r == 3) sp_queue(SPQ_PAUSE(rand() % 200));
    else if (r == 8) sp_queue(SPQ_PITCH(rand() % 4096));
    else if (r == 9) sp_queue(rand() % 2 ? SPQ_SAYC(rand() % SP_COUNT) : SPQ_SAYH(rand() % SP_COUNT));
    else if (r == 10) sp_hold = (uint8_t)(rand() % 3 == 0);
    else if (r == 11 && rand() % 4 == 0) {
      static const char *const T[] = {"HELLO WORLD", "QWXZPT", "A", "", "  ", "SPEAK AND SPELL IS A TOY", "RUNNING JUMPED BOXES", "ZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZ"};
      uint8_t ph[96];
      tts_say(T[rand() % 8], ph, 96);
    }
    else if (r == 4 && rand() % 8 == 0) sp_stop();
    else if (r == 5) SP.pitch = (int16_t)(rand() % 40 - 20), SP.speed = (int16_t)(rand() % 200 - 30), SP.clock = (int16_t)(rand() % 200 - 30);
    else if (r == 6) SP.bend = (int16_t)(rand() % 200 - 30), SP.stutter = (int16_t)(rand() % 200 - 30), SP.mono = (int16_t)(rand() % 200 - 30);
    else if (r == 7) SP.crush = (int16_t)(rand() % 10 - 2), SP.tone = (int16_t)(rand() % 200 - 30), sp_sing = (uint8_t)(rand() % 2 ? rand() % 128 : 0);
    int32_t blk[512];
    memset(blk, 0, sizeof blk);
    uint32_t n = 1 + rand() % 256;
    sp_render(blk, n);
    for (uint32_t i = 0; i < 2 * n; i++) bad += blk[i] > 32767 || blk[i] < -32767;
    blocks++;
  }
  printf("fuzz: %ld blocks, %s\n", blocks, bad ? "OUT OF RANGE" : "ok");
  return bad != 0;
}
