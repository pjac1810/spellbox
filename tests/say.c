/* SPDX-License-Identifier: GPL-3.0-only */
/* Host check of the text to speech: argv[1] = "words": prints each word's phonemes; else says argv[2..] into argv[1] (WAV) */
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
static int16_t buf[44100 * 120 * 2];
int main(int argc, char **argv) {
  if (argc > 1 && !strcmp(argv[1], "words")) {
    char w[64];
    while (scanf("%63s", w) == 1) {
      uint8_t ph[64];
      int n = tts_word(w, (int)strlen(w), ph, 64);
      printf("%s", w);
      for (int i = 0; i < n; i++) printf(" %s%s", PH_NAME[ph[i] & 0x3F], (ph[i] & PH_STRESS) ? "1" : "");
      printf("\n");
    }
    return 0;
  }
  sp_init();
  uint32_t pos = 0;
  int peak = 0;
  for (int a = 2; a < argc; a++) {
    uint8_t ph[96];
    tts_say(argv[a], ph, 96);
    sp_queue(SPQ_PAUSE(40));
    while (sp_busy()) {
      int32_t blk[512];
      memset(blk, 0, sizeof blk);
      sp_render(blk, 256);
      for (int i = 0; i < 256 && pos < sizeof buf / 4; i++) {
        int v = blk[2 * i];
        if (abs(v) > peak) peak = abs(v);
        buf[2 * pos] = buf[2 * pos + 1] = (int16_t)v, pos++;
      }
    }
  }
  FILE *f = fopen(argv[1], "wb");
  uint32_t data = pos * 4, riff = 36 + data, rate = 44100, br = rate * 4, fmt = 16;
  uint16_t pcm = 1, ch = 2, al = 4, bits = 16;
  fwrite("RIFF", 1, 4, f), fwrite(&riff, 4, 1, f), fwrite("WAVEfmt ", 1, 8, f), fwrite(&fmt, 4, 1, f), fwrite(&pcm, 2, 1, f), fwrite(&ch, 2, 1, f);
  fwrite(&rate, 4, 1, f), fwrite(&br, 4, 1, f), fwrite(&al, 2, 1, f), fwrite(&bits, 2, 1, f), fwrite("data", 1, 4, f), fwrite(&data, 4, 1, f);
  fwrite(buf, 4, pos, f), fclose(f);
  printf("%.1f s, peak %d\n", pos / 44100.0, peak);
  return 0;
}
