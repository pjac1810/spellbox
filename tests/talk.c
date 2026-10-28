/* SPDX-License-Identifier: GPL-3.0-only */
/* Host check of the speech chip: says utterances (argv: ids, or a range a-b) into one WAV (argv[1]), prints levels */
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
static int16_t buf[44100 * 600];
int main(int argc, char **argv) {
  sp_init();
  uint32_t pos = 0; int peak = 0; long clip = 0;
  for (int a = 2; a < argc; a++) {
    int lo, hi;
    if (sscanf(argv[a], "%d-%d", &lo, &hi) != 2) hi = lo = atoi(argv[a]);
    for (int id = lo; id <= hi; id++) {
      sp_queue(SPQ_SAY(id)); sp_queue(SPQ_PAUSE(25));
      while (sp_busy()) {
        int32_t blk[512]; memset(blk, 0, sizeof blk);
        sp_render(blk, 256);
        for (int i = 0; i < 256 && pos < sizeof buf / 4; i++) {
          int v = blk[2 * i]; if (abs(v) > peak) peak = abs(v); if (abs(v) > 32000) clip++;
          buf[2 * pos] = buf[2 * pos + 1] = (int16_t)v; pos++;
        }
      }
    }
  }
  FILE *f = fopen(argv[1], "wb");
  uint32_t data = pos * 4, riff = 36 + data, rate = 44100, br = rate * 4, fmt = 16; uint16_t pcm = 1, ch = 2, al = 4, bits = 16;
  fwrite("RIFF", 1, 4, f), fwrite(&riff, 4, 1, f), fwrite("WAVEfmt ", 1, 8, f), fwrite(&fmt, 4, 1, f), fwrite(&pcm, 2, 1, f), fwrite(&ch, 2, 1, f);
  fwrite(&rate, 4, 1, f), fwrite(&br, 4, 1, f), fwrite(&al, 2, 1, f), fwrite(&bits, 2, 1, f), fwrite("data", 1, 4, f), fwrite(&data, 4, 1, f);
  fwrite(buf, 4, pos, f); fclose(f);
  printf("%.1f s, peak %d, clipped %ld\n", pos / 44100.0, peak, clip);
  return 0;
}
