/* SPDX-License-Identifier: GPL-3.0-only */
/* SPELLBOX's text to speech: a word's phonemes from the pronouncing dictionary, else from a known word and its
 * ending (-S, -ED, -ING, -LY ...), else from the letter-to-sound rules; then the phonemes, strung together with an
 * intonation, go to the speech chip. */
#include "fm1.h"
#include "sb.h"
#include "speech.h"

static int is_vowel_ph(uint8_t p) { return (p & 0x3F) < PH_VOWELS; }

/* ---- the dictionary */

static int cmp(const char *a, int na, const char *b, int nb) {
  for (int i = 0; i < na && i < nb; i++)
    if (a[i] != b[i]) return (uint8_t)a[i] < (uint8_t)b[i] ? -1 : 1;
  return na - nb;
}

/* w (n capitals) -> its phonemes, or -1 */
static int dict(const char *w, int n, uint8_t *out, int max) {
  int lo = 0, hi = DICT_BUCKETS - 1, b = -1;
  while (lo <= hi) { /* the last bucket whose first word is <= w */
    int mid = (lo + hi) / 2;
    const uint8_t *e = &DICT[DICT_AT[mid]];
    if (cmp((const char *)e + 1, e[0] & 15, w, n) <= 0) b = mid, lo = mid + 1;
    else hi = mid - 1;
  }
  if (b < 0) return -1;
  char cur[16];
  int len = 0;
  uint32_t at = DICT_AT[b], end = b + 1 < DICT_BUCKETS ? DICT_AT[b + 1] : DICT_SIZE;
  while (at < end) {
    int shared = DICT[at] >> 4, add = DICT[at] & 15;
    at++;
    len = shared;
    for (int i = 0; i < add && len < 15; i++) cur[len++] = (char)DICT[at + (uint32_t)i];
    at += (uint32_t)add;
    int np = DICT[at++];
    int c = cmp(cur, len, w, n);
    if (c == 0) {
      int k = np < max ? np : max;
      for (int i = 0; i < k; i++) out[i] = DICT[at + (uint32_t)i];
      return k;
    }
    if (c > 0) return -1;
    at += (uint32_t)np;
  }
  return -1;
}

/* ---- the rules (see tools/gen_speech.py) */

static int is_v(char c) { return c == 'A' || c == 'E' || c == 'I' || c == 'O' || c == 'U' || c == 'Y'; }
static int is_v5(char c) { return c == 'A' || c == 'E' || c == 'I' || c == 'O' || c == 'U'; }

/* the context ctx read from word[i] in the direction step (word has a space at both ends) */
static int ctx_ok(const char *word, int len, int i, const char *ctx, int step) {
  int n = 0;
  while (ctx[n]) n++;
  for (int j = 0; j < n; j++) {
    char c = ctx[step > 0 ? j : n - 1 - j];
    char ch = i >= 0 && i < len ? word[i] : ' ';
    switch (c) {
      case ' ':
        if (ch != ' ') return 0;
        i += step;
        break;
      case '#':
        if (!is_v5(ch)) return 0;
        while (i >= 0 && i < len && is_v5(word[i])) i += step;
        break;
      case ':':
        while (i >= 0 && i < len && !is_v(word[i]) && word[i] != ' ') i += step;
        break;
      case '^':
        if (is_v(ch) || ch == ' ') return 0;
        i += step;
        break;
      case '.':
        if (!(ch == 'B' || ch == 'D' || ch == 'V' || ch == 'G' || ch == 'J' || ch == 'L' || ch == 'M' || ch == 'N' || ch == 'R' ||
              ch == 'W' || ch == 'Z'))
          return 0;
        i += step;
        break;
      case '+':
        if (!(ch == 'E' || ch == 'I' || ch == 'Y')) return 0;
        i += step;
        break;
      case '%': {
        static const char *const SUF[6] = {"ING", "ELY", "ER", "ES", "ED", "E"};
        int found = 0;
        for (int s = 0; s < 6 && !found; s++) {
          int k = 0;
          while (SUF[s][k] && i + k < len && word[i + k] == SUF[s][k]) k++;
          if (!SUF[s][k] && (i + k >= len || word[i + k] == ' ')) i += k, found = 1;
        }
        if (!found) return 0;
        break;
      }
      default:
        if (ch != c) return 0;
        i += step;
        break;
    }
  }
  return 1;
}

static int rules(const char *w, int n, uint8_t *out, int max) {
  char word[40];
  int len = 0, k = 0;
  word[len++] = ' ';
  for (int i = 0; i < n && len < 38; i++) word[len++] = w[i];
  word[len++] = ' ';
  int i = 1;
  while (i < len - 1) {
    int r = 0;
    for (; r < RULES; r++) {
      const rule_t *R = &RULE[r];
      int m = 0;
      while (R->match[m] && i + m < len && word[i + m] == R->match[m]) m++;
      if (R->match[m]) continue;
      if (!ctx_ok(word, len, i + m, R->right, 1) || !ctx_ok(word, len, i - 1, R->left, -1)) continue;
      for (int p = 0; p < R->n && k < max; p++) out[k++] = R->ph[p];
      i += m;
      break;
    }
    if (r == RULES) i++;
  }
  for (int j = 0; j < k; j++) /* the first vowel takes the stress */
    if (is_vowel_ph(out[j])) {
      out[j] |= PH_STRESS;
      break;
    }
  return k;
}

/* ---- a word: the dictionary, a known word with an ending, or the rules */

static int ends(const char *w, int n, const char *s) {
  int k = 0;
  while (s[k]) k++;
  if (n <= k + 1) return 0;
  for (int i = 0; i < k; i++)
    if (w[n - k + i] != s[i]) return 0;
  return k;
}
static int voiceless(uint8_t p) {
  p &= 0x3F;
  return p == P_P || p == P_T || p == P_K || p == P_F || p == P_TH || p == P_S || p == P_SH || p == P_CH;
}
static int sibilant(uint8_t p) {
  p &= 0x3F;
  return p == P_S || p == P_Z || p == P_SH || p == P_ZH || p == P_CH || p == P_JH;
}

int tts_word(const char *w, int n, uint8_t *out, int max) {
  int k = dict(w, n, out, max);
  if (k >= 0) return k;
  char stem[20];
  if (n > 15) n = 15;
  /* the endings: the stem as is, with an E back (MAKING), or without its doubled last letter (RUNNING) */
  static const char *const END[] = {"S", "ES", "ED", "D", "ING", "LY", "ER", "ERS", "EST", "NESS"};
  for (int e = 0; e < 10; e++) {
    int s = ends(w, n, END[e]);
    if (!s) continue;
    for (int form = 0; form < 3; form++) {
      int m = n - s;
      for (int i = 0; i < m; i++) stem[i] = w[i];
      if (form == 1) stem[m++] = 'E';
      if (form == 2) {
        if (m < 3 || stem[m - 1] != stem[m - 2]) continue;
        m--;
      }
      k = dict(stem, m, out, max - 4);
      if (k <= 0) continue;
      uint8_t last = out[k - 1];
      switch (e) {
        case 0:
        case 1:
          if (sibilant(last)) out[k++] = P_IH, out[k++] = P_Z;
          else out[k++] = voiceless(last) ? P_S : P_Z;
          break;
        case 2:
        case 3:
          if ((last & 0x3F) == P_T || (last & 0x3F) == P_D) out[k++] = P_IH, out[k++] = P_D;
          else out[k++] = voiceless(last) ? P_T : P_D;
          break;
        case 4: out[k++] = P_IH, out[k++] = P_NG; break;
        case 5: out[k++] = P_L, out[k++] = P_IY; break;
        case 6: out[k++] = P_AXR; break;
        case 7: out[k++] = P_AXR, out[k++] = P_Z; break;
        case 8: out[k++] = P_IH, out[k++] = P_S, out[k++] = P_T; break;
        default: out[k++] = P_N, out[k++] = P_IH, out[k++] = P_S; break;
      }
      return k;
    }
  }
  return rules(w, n, out, max);
}

/* ---- a phrase to the speech chip: the phonemes one after the other, with a falling intonation */

int tts_say(const char *text, uint8_t *ph_out, int ph_max) {
  uint8_t ph[96];
  int np = 0;
  int i = 0, n = 0;
  while (text[n]) n++;
  while (i < n) { /* the words */
    while (i < n && !(text[i] >= 'A' && text[i] <= 'Z')) i++;
    int a = i;
    while (i < n && text[i] >= 'A' && text[i] <= 'Z') i++;
    if (i > a) np += tts_word(text + a, i - a, ph + np, 96 - np);
  }
  int stressed = 0, last = -1;
  for (int j = 0; j < np; j++)
    if (ph[j] & PH_STRESS) stressed++, last = j;
  /* the pitch: 120 Hz down to 100 Hz over the phrase; stressed vowels a step up, the last one falling */
  int s = 0, first = 1;
  for (int j = 0; j < np; j++) {
    uint8_t p = ph[j] & 0x3F;
    if (is_vowel_ph(p)) {
      int hz = 120 - 20 * j / (np ? np : 1);
      if (ph[j] & PH_STRESS) hz = j == last && stressed > 1 ? hz * 9 / 10 : hz * 115 / 100 - s * 3, s++;
      sp_queue(SPQ_PITCH((uint16_t)(8000 * 16 / hz)));
    }
    uint16_t it = (uint16_t)(SP_PHONEME + p);
    sp_queue(first ? SPQ_SAYH(it) : SPQ_SAYC(it));
    first = 0;
  }
  sp_queue(SPQ_PITCH(0));
  int k = np < ph_max ? np : ph_max;
  for (int j = 0; j < k; j++) ph_out[j] = ph[j];
  return np;
}
