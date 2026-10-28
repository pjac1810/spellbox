/* SPDX-License-Identifier: GPL-3.0-only */
/* SPELLBOX: a talking spelling game for the M-VAVE FM-1, after the 1978 educational toys (README.md has the map).
 *
 *   keys        the letters: A on the lowest key (F3) .. Z; the top key (G5) is ENTER
 *   PRESETS     the game: SPELL, LETTER, SAY IT, SECRET CODE, MYSTERY WORD, SING   (EDIT: the next one)
 *   SELECT      the level 1..4 (SING: what to sing)        ALGORITHM  English / French
 *   PLAY ENTER   REC ERASE   SEQ REPEAT   SEL a new game
 *   HOME        VOICE: PITCH, SPEED, CLOCK, TONE           ENV  BEND: BEND, STUTTER, MONOTONE, CRUSH
 *   GLO         the bends on / off      SAVE  keep the language, level and knobs
 *   OCT- OCT+   SING: the keys' octave */
#include "fm1.h"
#include "sb.h"
#include "speech.h"

/* ---------------------------------------------------------------- colours */

#define C_BG RGB(18, 16, 14)
#define C_CASE RGB(214, 90, 30) /* the orange case */
#define C_PANEL RGB(40, 37, 33)
#define C_TEXT RGB(236, 230, 218)
#define C_DIM RGB(130, 122, 110)
#define C_FAINT RGB(58, 54, 48)
#define C_GLASS RGB(34, 8, 10)
#define C_SEG RGB(90, 245, 225)
#define C_SEG_OFF RGB(44, 20, 22)
#define C_YELLOW RGB(250, 210, 60)

static int32_t clampi(int32_t v, int32_t lo, int32_t hi) { return v < lo ? lo : v > hi ? hi : v; }
static char *put(char *d, const char *s) {
  while (*s) *d++ = *s++;
  *d = 0;
  return d;
}
static char *put_int(char *d, int32_t v) {
  char b[12];
  format_int(b, v);
  return put(d, b);
}
static int center_text(int cx, int y, const char *s, uint16_t c, int font) {
  return text(cx - text_width(s, font) / 2, y, s, c, font);
}

/* ---------------------------------------------------------------- the 14-segment display */

/* segments: a b c d e f (the outline), g G (the middle halves), h i j (top diagonal, top centre, top diagonal),
 * k l m (bottom diagonal, bottom centre, bottom diagonal) */
static const char *const FONT_AZ[26] = {"abcefgG", "abcdGil", "adef",    "abcdil",  "adefg",  "aefg",   "acdefG",
                                        "bcefgG",  "adil",    "bcde",    "efgjm",   "def",    "bcefhj", "bcefhm",
                                        "abcdef",  "abefgG",  "abcdefm", "abefgGm", "acdfgG", "ail",    "bcdef",
                                        "efkj",    "bcefkm",  "hjkm",    "hjl",     "adjk"};
static const char *const FONT_09[10] = {"abcdefjk", "bcj", "abdegG", "abcdG", "bcfgG", "adfgm", "acdefgG", "abc", "abcdefgG", "abcdfgG"};

static uint16_t seg_mask(char c) {
  const char *s = "";
  if (c >= 'A' && c <= 'Z') s = FONT_AZ[c - 'A'];
  else if (c >= '0' && c <= '9') s = FONT_09[c - '0'];
  else if (c == '_') s = "d";
  else if (c == '-') s = "gG";
  else if (c == '?') s = "abGl";
  else if (c == '\'') s = "i";
  else if (c == '*') s = "hijklmgG";
  else if (c == ',' || c == '.') s = "k";
  uint16_t m = 0;
  static const char ORDER[] = "abcdefgGhijklm";
  for (; *s; s++)
    for (int i = 0; ORDER[i]; i++)
      if (ORDER[i] == *s) m |= (uint16_t)(1u << i);
  return m;
}

static void diag(int x0, int y0, int x1, int y1, uint16_t c) { line(x0, y0, x1, y1, c), line(x0 + 1, y0, x1 + 1, y1, c); }
static void glyph(int x, int y, uint16_t m, uint16_t on, uint16_t off) {
  for (int pass = 0; pass < 2; pass++) /* the dark segments first, the lit ones over them */
    for (int i = 0; i < 14; i++) {
      int lit = m >> i & 1u;
      if (lit != pass) continue;
      uint16_t c = lit ? on : off;
      switch (i) {
        case 0: rect(x + 3, y, 18, 3, c); break;
        case 1: rect(x + 21, y + 3, 3, 16, c); break;
        case 2: rect(x + 21, y + 22, 3, 16, c); break;
        case 3: rect(x + 3, y + 38, 18, 3, c); break;
        case 4: rect(x, y + 22, 3, 16, c); break;
        case 5: rect(x, y + 3, 3, 16, c); break;
        case 6: rect(x + 3, y + 19, 8, 3, c); break;
        case 7: rect(x + 13, y + 19, 8, 3, c); break;
        case 8: diag(x + 4, y + 4, x + 9, y + 17, c); break;
        case 9: rect(x + 11, y + 3, 2, 16, c); break;
        case 10: diag(x + 19, y + 4, x + 14, y + 17, c); break;
        case 11: diag(x + 9, y + 23, x + 4, y + 36, c); break;
        case 12: rect(x + 11, y + 22, 2, 16, c); break;
        default: diag(x + 14, y + 23, x + 19, y + 36, c); break;
      }
    }
}

#define VFD_Y 22
static void draw_vfd(const char *s, int cursor) { /* 8 characters behind red glass; cursor: the one to blink, or -1 */
  rect(0, VFD_Y, SCREEN_W, 58, C_CASE);
  rect(4, VFD_Y + 3, SCREEN_W - 8, 52, C_GLASS);
  int n = 0;
  while (n < 8 && s[n]) n++;
  for (int i = 0; i < 8; i++) {
    char c = i < n ? s[i] : (i == cursor ? '_' : ' ');
    if (c >= 'a' && c <= 'z') c = (char)(c - 32);
    glyph(10 + i * 28, VFD_Y + 8, seg_mask(c), C_SEG, C_SEG_OFF);
  }
}

/* ---------------------------------------------------------------- the knobs */

enum { KN_PITCH, KN_SPEED, KN_CLOCK, KN_TONE, KN_BEND, KN_STUTTER, KN_MONO, KN_CRUSH, KN_COUNT };
static const char *const KN_NAME[KN_COUNT] = {"PITCH", "SPEED", "CLOCK", "TONE", "BEND", "STUTTER", "MONO", "CRUSH"};
static const int16_t KN_LO[KN_COUNT] = {-12, 0, 0, 0, 0, 0, 0, 0}, KN_HI[KN_COUNT] = {12, 127, 127, 127, 127, 127, 127, 5};
static const int16_t KN_INIT[KN_COUNT] = {0, 64, 64, 64, 0, 0, 0, 0};
static int16_t knob[KN_COUNT];
static uint8_t page, bends_on = 1;

static void knob_text(char *d, int k) {
  int32_t v = knob[k];
  if (k == KN_PITCH) put_int(put(d, v > 0 ? "+" : ""), v);
  else if (k == KN_CLOCK) {
    int32_t hz = v <= 64 ? 4000 + v * 4000 / 64 : 8000 + (v - 64) * 6000 / 63;
    d = put_int(d, hz / 1000), *d++ = '.', *d = 0;
    put(put_int(d, hz / 100 % 10), "k");
  } else if (k == KN_CRUSH) put(put_int(d, 8 - v), " BIT");
  else put_int(d, v);
}

static void apply_knobs(void) {
  int on = bends_on;
  SP.pitch = knob[KN_PITCH], SP.speed = knob[KN_SPEED], SP.clock = knob[KN_CLOCK], SP.tone = knob[KN_TONE];
  SP.bend = on ? knob[KN_BEND] : 0, SP.stutter = on ? knob[KN_STUTTER] : 0;
  SP.mono = on ? knob[KN_MONO] : 0, SP.crush = on ? knob[KN_CRUSH] : 0;
}

/* ---------------------------------------------------------------- the games */

enum { M_SPELL, M_LETTER, M_SAYIT, M_CODE, M_MYSTERY, M_SING, M_COUNT };
static const char *const MODE_NAME[M_COUNT] = {"SPELL", "LETTER", "SAY IT", "SECRET CODE", "MYSTERY WORD", "SING"};
static const char *const MODE_FR[M_COUNT] = {"EPELLE", "LETTRE", "DIS-LE", "CODE SECRET", "MOT MYSTERE", "CHANTE"};

enum { AFTER_NONE, AFTER_NEXT_WORD, AFTER_SCORE, AFTER_NEW_ROUND, AFTER_MYSTERY_NEW, AFTER_SAYIT_NEXT };

static uint8_t mode, level, lang, after;
static char input[9], disp[9];
static uint8_t in_len;
static uint8_t list[10], round_i, score, tries, said, reveal, misses, guessed_done;
static uint32_t guessed; /* MYSTERY: letters tried */
static uint8_t sing_word, sing_held, sing_note;
static int8_t oct;
static uint32_t saved_t;

#define ROUND 10
#define MISSES 7

static uint16_t sp_id(int base) { return (uint16_t)(lang * SP_PER_LANG + base); }
static void say(int base) { sp_queue(SPQ_SAY(sp_id(base))); }
static int word_n(int i) { return level * WORDS_PER_LEVEL + list[i]; }
static const char *word_text(int i) { return lang ? WORD_FR[word_n(i)] : WORD_EN[word_n(i)]; }
static void say_word(int i) { say(SP_WORD + word_n(i)); }
static int slen(const char *s) {
  int n = 0;
  while (s[n]) n++;
  return n;
}
static void set_disp(const char *s) {
  int i = 0;
  for (; i < 8 && s[i]; i++) disp[i] = s[i];
  disp[i] = 0;
}
static void clear_input(void) { in_len = 0, input[0] = 0; }

static void tune(const uint8_t *notes, int n, int len) {
  for (int i = 0; i < n; i++) sp_queue(SPQ_TONE(notes[i], i == n - 1 ? len * 2 : len));
}
static const uint8_t T_HELLO[] = {72, 76, 79, 84}, T_PERFECT[] = {67, 72, 76, 79, 76, 79, 84}, T_SCORE[] = {79, 74, 76, 72};
static const uint8_t T_WIN[] = {72, 74, 76, 77, 79, 84}, T_LOSE[] = {60, 59, 58, 57}, T_BUZZ[] = {43};

static void pick_list(void) { /* ten different words of the level */
  for (int i = 0; i < ROUND; i++) {
    int ok;
    do {
      list[i] = (uint8_t)(random32() % WORDS_PER_LEVEL), ok = 1;
      for (int j = 0; j < i; j++) ok &= list[j] != list[i];
    } while (!ok);
  }
}

static void ask_word(int first) { /* SPELL: "spell <word>" */
  clear_input(), tries = 0, reveal = 0;
  set_disp("");
  print(word_text(round_i)); /* the emulator's console */
  say(first == 2 ? SP_PHRASE + PH_NOW_SPELL : SP_PHRASE + PH_SPELL);
  say_word(round_i);
}
static void show_sayit(void) {
  said = 0;
  set_disp(word_text(round_i));
  say(SP_PHRASE + PH_SAY_IT);
}
static void new_mystery(void) {
  list[0] = (uint8_t)(random32() % WORDS_PER_LEVEL);
  guessed = 0, misses = 0, guessed_done = 0;
  print(word_text(0));
  int n = slen(word_text(0));
  for (int i = 0; i < n; i++) disp[i] = '_';
  disp[n] = 0;
}

static void start_mode(int announce, int pre_a, int pre_b) { /* pre: said first (-1: nothing) */
  sp_stop();
  if (pre_a >= 0) say(pre_a);
  if (pre_b >= 0) say(pre_b);
  after = AFTER_NONE, reveal = 0, sing_held = 0, sp_sing = 0;
  clear_input(), set_disp("");
  round_i = 0, score = 0;
  switch (mode) {
    case M_SPELL:
      pick_list();
      ask_word(0);
      break;
    case M_LETTER:
      if (announce) say(SP_PHRASE + PH_LETTER);
      break;
    case M_SAYIT:
      pick_list();
      show_sayit();
      break;
    case M_CODE:
      if (announce) say(SP_PHRASE + PH_SECRET);
      say(SP_PHRASE + PH_TYPE);
      break;
    case M_MYSTERY:
      new_mystery();
      say(SP_PHRASE + PH_MYSTERY), say(SP_PHRASE + PH_GUESS);
      break;
    default:
      if (announce) say(SP_PHRASE + PH_SING);
      set_disp(SP_TEXT[sp_id(sing_word)]);
      break;
  }
}

static void spell_enter(void) {
  if (!in_len) return;
  const char *w = word_text(round_i);
  int ok = 1, n = slen(w);
  if (n != in_len) ok = 0;
  for (int i = 0; ok && i < n; i++) ok = input[i] == w[i];
  sp_stop();
  if (ok) {
    score++;
    say(SP_PHRASE + ((random32() & 1u) ? PH_CORRECT : PH_YOU_CORRECT));
    after = AFTER_NEXT_WORD;
  } else if (!tries) {
    tries = 1;
    tune(T_BUZZ, 1, 6);
    say(SP_PHRASE + PH_WRONG);
    say_word(round_i);
    clear_input(), set_disp("");
  } else { /* the second miss: the right spelling, letter by letter on the display */
    say(SP_PHRASE + PH_INCORRECT), say(SP_PHRASE + PH_SPELLING_OF);
    say_word(round_i);
    say(SP_PHRASE + PH_IS);
    sp_queue(SPQ_MARK(0));
    for (int i = 0; i < n; i++) sp_queue(SPQ_MARK((uint16_t)(i + 1))), say(SP_LETTER + (w[i] - 'A'));
    sp_queue(SPQ_PAUSE(40));
    reveal = 1, after = AFTER_NEXT_WORD;
  }
}

static void next_word(void) {
  reveal = 0;
  if (++round_i >= ROUND && mode == M_SAYIT) { /* SAY IT: then spell them */
    after = AFTER_NEW_ROUND;
    return;
  }
  if (round_i >= ROUND) { /* the score */
    char b[9] = "SCORE  ";
    b[6] = (char)('0' + score % 10);
    if (score >= 10) b[5] = '1', b[6] = '0';
    set_disp(b);
    if (score == ROUND) tune(T_PERFECT, 7, 3), say(SP_PHRASE + PH_PERFECT);
    else {
      tune(T_SCORE, 4, 3);
      say(SP_PHRASE + PH_SCORE), say(SP_NUMBER + score), say(SP_PHRASE + PH_ANSWERS);
    }
    sp_queue(SPQ_PAUSE(80));
    after = AFTER_NEW_ROUND;
    return;
  }
  if (mode == M_SAYIT) show_sayit();
  else ask_word(1);
}

static void letter_key(int L) {
  char c = (char)('A' + L);
  switch (mode) {
    case M_SPELL:
      if (after != AFTER_NONE) return;
      if (in_len < 8) input[in_len++] = c, input[in_len] = 0;
      set_disp(input);
      sp_stop(), say(SP_LETTER + L);
      break;
    case M_LETTER:
      disp[0] = c, disp[1] = 0;
      sp_stop(), say(SP_LETTER + L);
      break;
    case M_CODE:
      if (in_len < 8) input[in_len++] = c, input[in_len] = 0;
      set_disp(input);
      sp_stop(), say(SP_LETTER + L);
      break;
    case M_MYSTERY: {
      if (guessed_done) return;
      const char *w = word_text(0);
      int n = slen(w), hit = 0, left = 0;
      if (guessed >> L & 1u) return;
      guessed |= 1u << L;
      for (int i = 0; i < n; i++) {
        if (w[i] == c) disp[i] = c, hit = 1;
        if (disp[i] == '_') left++;
      }
      sp_stop(), say(SP_LETTER + L);
      if (!hit) tune(T_BUZZ, 1, 4), misses++;
      if (!left) {
        tune(T_WIN, 6, 2), say(SP_PHRASE + PH_YOU_WIN), say(SP_WORD + word_n(0));
        guessed_done = 1, after = AFTER_MYSTERY_NEW;
      } else if (misses >= MISSES) {
        set_disp(w);
        tune(T_LOSE, 4, 4), say(SP_PHRASE + PH_I_WIN), say(SP_WORD + word_n(0));
        guessed_done = 1, after = AFTER_MYSTERY_NEW;
      }
      break;
    }
    default: break;
  }
}

static void enter(void) {
  switch (mode) {
    case M_SPELL:
      if (after == AFTER_NONE) spell_enter();
      break;
    case M_SAYIT:
      if (after != AFTER_NONE) break;
      if (!said) sp_stop(), say_word(round_i), said = 1;
      else next_word();
      break;
    case M_CODE:
      if (in_len) { /* each letter for its mirror: A<->Z, B<->Y ... (coding twice decodes) */
        for (int i = 0; i < in_len; i++) input[i] = (char)('Z' - (input[i] - 'A'));
        set_disp(input);
        sp_stop();
        for (int i = 0; i < in_len; i++) say(SP_LETTER + (input[i] - 'A'));
        clear_input();
      }
      break;
    case M_MYSTERY:
      sp_stop(), new_mystery(), say(SP_PHRASE + PH_GUESS), after = AFTER_NONE;
      break;
    default: break;
  }
}

static void erase(void) {
  if ((mode == M_SPELL || mode == M_CODE) && in_len && after == AFTER_NONE) input[--in_len] = 0, set_disp(input);
}

static void repeat(void) {
  sp_stop();
  if (mode == M_SPELL && after == AFTER_NONE) say(SP_PHRASE + PH_SPELL), say_word(round_i);
  else if (mode == M_SAYIT) say_word(round_i);
  else if (mode == M_MYSTERY) say(SP_PHRASE + PH_GUESS);
  else if (mode == M_SING) say(sing_word);
}

/* what to do once the talking is over */
static void follow_up(void) {
  if (after == AFTER_NONE || sp_busy()) return;
  int a = after;
  after = AFTER_NONE;
  if (a == AFTER_NEXT_WORD) next_word();
  else if (a == AFTER_NEW_ROUND) {
    if (mode == M_SAYIT) { /* "now spell" the same ten */
      mode = M_SPELL, round_i = 0, score = 0;
      ask_word(2);
    } else {
      round_i = 0, score = 0;
      pick_list(), ask_word(0);
    }
  } else if (a == AFTER_MYSTERY_NEW) {
    guessed_done = 1; /* waits for ENTER */
  }
}

/* ---------------------------------------------------------------- the screen */

#define INFO_Y 84
#define KEYS_Y 120
#define KNOB_Y 172
#define FOOT_Y 216

static void draw_title(void) {
  rect(0, 0, SCREEN_W, 18, C_PANEL);
  text(6, 1, "SPELLBOX", C_CASE, FONT_SMALL);
  char b[16], *d = put(b, lang ? "FR  " : "EN  ");
  put_int(put(d, lang ? "NIV " : "LEVEL "), level + 1);
  text(SCREEN_W - 6 - text_width(b, FONT_SMALL), 1, b, C_TEXT, FONT_SMALL);
}

static void draw_info(void) {
  rect(0, INFO_Y, SCREEN_W, 34, C_BG);
  center_text(SCREEN_W / 2, INFO_Y, (lang ? MODE_FR : MODE_NAME)[mode], C_YELLOW, FONT_SMALL);
  char b[40], *d = b;
  b[0] = 0;
  if (mode == M_SPELL || mode == M_SAYIT) {
    d = put_int(put(d, lang ? "MOT " : "WORD "), (round_i < ROUND ? round_i : ROUND - 1) + 1);
    d = put_int(put(put(d, "/10   "), "SCORE "), score);
  } else if (mode == M_MYSTERY) {
    d = put_int(put(d, lang ? "ERREURS " : "MISSES "), misses);
    put(put_int(put(d, "/"), MISSES), "");
  } else if (mode == M_SING) {
    const char *t = SP_TEXT[sp_id(sing_word)];
    int n = slen(t);
    for (int i = 0; i < n && i < 29; i++) b[i] = t[i];
    b[n < 29 ? n : 29] = 0;
  } else if (mode == M_CODE) {
    put(b, lang ? "A=Z  B=Y  C=X ..." : "A=Z  B=Y  C=X ...");
  }
  center_text(SCREEN_W / 2, INFO_Y + 17, b, C_DIM, FONT_SMALL);
}

static const int8_t WHITE_OF[KEY_COUNT] = {0, -1, 1, -1, 2, -1, 3, 4, -1, 5, -1, 6, 7, -1, 8, -1, 9, -1, 10, 11, -1, 12, -1, 13, 14, -1, 15};
static void draw_keys(uint32_t held, uint32_t lit) {
  rect(0, KEYS_Y, SCREEN_W, 48, C_BG);
  for (int k = 0; k < KEY_COUNT; k++) { /* the white keys */
    int w = WHITE_OF[k];
    if (w < 0) continue;
    int x = w * 15;
    uint16_t c = (held >> k & 1u) ? C_YELLOW : (lit >> k & 1u) ? RGB(150, 140, 120) : C_TEXT;
    rect(x, KEYS_Y, 14, 46, c);
    char b[2] = {k == 26 ? '>' : (char)('A' + k), 0};
    if (mode == M_SING) b[0] = 0;
    center_text(x + 7, KEYS_Y + 28, b, C_BG, FONT_SMALL);
  }
  for (int k = 0; k < KEY_COUNT; k++) { /* the black keys */
    if (WHITE_OF[k] >= 0) continue;
    int x = WHITE_OF[k - 1] * 15 + 9;
    uint16_t c = (held >> k & 1u) ? C_YELLOW : (lit >> k & 1u) ? RGB(90, 80, 60) : RGB(28, 26, 24);
    rect(x, KEYS_Y, 11, 26, c);
    char b[2] = {(char)('A' + k), 0};
    if (mode != M_SING) center_text(x + 6, KEYS_Y + 4, b, (held >> k & 1u) ? C_BG : C_TEXT, FONT_SMALL);
  }
}

static void draw_knobs(void) {
  rect(0, KNOB_Y - 2, SCREEN_W, FOOT_Y - KNOB_Y + 1, C_BG);
  for (int i = 0; i < 4; i++) {
    int k = page * 4 + i, cx = i * 60 + 30;
    center_text(cx, KNOB_Y, KN_NAME[k], page && !bends_on ? C_FAINT : C_DIM, FONT_SMALL);
    char b[16];
    knob_text(b, k);
    center_text(cx, KNOB_Y + 16, b, page && !bends_on ? C_FAINT : C_TEXT, FONT_SMALL);
    int w = 46, v = knob[k] - KN_LO[k], r = KN_HI[k] - KN_LO[k];
    rect(cx - w / 2, KNOB_Y + 35, w, 3, C_FAINT);
    rect(cx - w / 2, KNOB_Y + 35, v * w / r, 3, C_CASE);
  }
}

static void draw_foot(uint32_t now) {
  rect(0, FOOT_Y, SCREEN_W, SCREEN_H - FOOT_Y, C_BG);
  line(0, FOOT_Y, SCREEN_W - 1, FOOT_Y, C_FAINT);
  text(6, FOOT_Y + 4, page ? "BEND" : "VOICE", C_CASE, FONT_SMALL);
  if (now - saved_t < 1500 && saved_t) text(SCREEN_W - 6 - text_width("SAVED", FONT_SMALL), FOOT_Y + 4, "SAVED", C_YELLOW, FONT_SMALL);
  else if (mode == M_SING) {
    char b[12], *d = put(b, "OCT");
    d = put(d, oct > 0 ? "+" : oct < 0 ? "" : " ");
    put_int(d, oct);
    text(SCREEN_W - 6 - text_width(b, FONT_SMALL), FOOT_Y + 4, b, C_DIM, FONT_SMALL);
  } else {
    text(SCREEN_W - 6 - text_width(bends_on ? "BENDS ON" : "BENDS OFF", FONT_SMALL), FOOT_Y + 4, bends_on ? "BENDS ON" : "BENDS OFF",
         bends_on ? C_TEXT : C_FAINT, FONT_SMALL);
  }
}

/* ---------------------------------------------------------------- setup and loop */

#define STORE_MAGIC 0x53504C31u /* "SPL1" */
static struct {
  uint32_t magic;
  uint8_t lang, level, bends_on, pad;
  int16_t knob[KN_COUNT];
} store;

static uint32_t sh_title = ~0u, sh_vfd = ~0u, sh_info = ~0u, sh_keys = ~0u, sh_knobs = ~0u, sh_foot = ~0u;
static uint32_t led_k = ~0u, led_b = ~0u;
static uint8_t drawn;

void setup(void) {
  for (int i = 0; i < KN_COUNT; i++) knob[i] = KN_INIT[i];
  if (!settings_load(&store, sizeof store) && store.magic == STORE_MAGIC) {
    lang = store.lang & 1u, level = (uint8_t)(store.level & 3u), bends_on = store.bends_on & 1u;
    for (int i = 0; i < KN_COUNT; i++) knob[i] = (int16_t)clampi(store.knob[i], KN_LO[i], KN_HI[i]);
  }
  apply_knobs();
  sp_init();
  led_backlight(1);
  clear(C_BG);
  sing_word = (uint8_t)(SP_WORD);
  mode = M_SPELL;
  tune(T_HELLO, 4, 3);
  say(SP_PHRASE + PH_HELLO);
  pick_list();
  ask_word(0);
}

static void save_all(void) {
  store.magic = STORE_MAGIC, store.lang = lang, store.level = level, store.bends_on = bends_on, store.pad = 0;
  for (int i = 0; i < KN_COUNT; i++) store.knob[i] = knob[i];
  settings_save(&store, sizeof store);
}

static void sing_key(int note, int on) {
  if (on) {
    sing_note = (uint8_t)clampi(note, 24, 108), sing_held = 1;
    sp_sing = sing_note;
    sp_stop(), sp_queue(SPQ_SAY(sp_id(sing_word)));
  } else if (note == sing_note) {
    sing_held = 0;
    sp_stop();
  }
}

void loop(void) {
  uint32_t now = millis();
  int32_t e;

  /* ---- buttons */
  if (button_pressed(BTN_PLAY)) enter();
  if (button_pressed(BTN_REC)) erase();
  if (button_pressed(BTN_SEQ)) repeat();
  if (button_pressed(BTN_SEL)) start_mode(0, -1, -1);
  if (button_pressed(BTN_EDIT)) mode = (uint8_t)((mode + 1) % M_COUNT), start_mode(1, -1, -1);
  if (button_pressed(BTN_HOME)) page = 0;
  if (button_pressed(BTN_ENV)) page = 1;
  if (button_pressed(BTN_GLO)) bends_on ^= 1;
  if (button_pressed(BTN_SAVE)) save_all(), saved_t = now ? now : 1;
  if (button_pressed(BTN_OCT_DOWN) || button_pressed(BTN_OCT_UP)) {
    if (button_held(BTN_OCT_DOWN) && button_held(BTN_OCT_UP)) oct = 0;
    else oct = (int8_t)clampi(oct + (button_pressed(BTN_OCT_UP) ? 1 : -1), -2, 2);
  }

  /* ---- encoders */
  if ((e = encoder(ENC_PRESETS))) mode = (uint8_t)((mode + M_COUNT * 8 + e) % M_COUNT), start_mode(1, -1, -1);
  if ((e = encoder(ENC_ALGORITHM))) {
    lang ^= 1;
    start_mode(0, SP_PHRASE + PH_LANGUAGE, -1);
  }
  if ((e = encoder(ENC_SELECT))) {
    if (mode == M_SING) {
      sing_word = (uint8_t)((sing_word + SP_PER_LANG * 8 + e) % SP_PER_LANG);
      set_disp(SP_TEXT[sp_id(sing_word)]);
      sp_stop(), sp_sing = 0, say(sing_word);
    } else {
      level = (uint8_t)clampi(level + (e > 0 ? 1 : -1), 0, LEVELS - 1);
      start_mode(0, SP_PHRASE + PH_LEVEL, SP_NUMBER + level + 1);
    }
  }
  for (int i = 0; i < 4; i++)
    if ((e = encoder(ENC_KNOB1 + i))) {
      int k = page * 4 + i, a = e < 0 ? -e : e, r = KN_HI[k] - KN_LO[k];
      knob[k] = (int16_t)clampi(knob[k] + (e > 0 ? 1 : -1) * (r > 20 && a > 1 ? 4 * a : a), KN_LO[k], KN_HI[k]);
    }
  apply_knobs();

  /* ---- keys: letters, or notes when singing */
  uint32_t held = 0;
  for (int k = 0; k < KEY_COUNT; k++) {
    if (key_held(k)) held |= 1u << k;
    if (mode == M_SING) {
      if (key_pressed(k)) sing_key(KEY_FIRST_NOTE + k + oct * 12, 1);
      if (key_released(k)) sing_key(KEY_FIRST_NOTE + k + oct * 12, 0);
    } else if (key_pressed(k)) {
      if (k == 26) enter();
      else letter_key(k);
    }
  }
  uint8_t st, d1, d2;
  while (midi_read(&st, &d1, &d2)) {
    int t = st & 0xF0;
    if (mode != M_SING) continue;
    if (t == 0x90 && d2) sing_key(d1, 1);
    else if (t == 0x80 || (t == 0x90 && !d2)) sing_key(d1, 0);
  }
  if (mode == M_SING && sing_held && !sp_busy()) sp_queue(SPQ_SAY(sp_id(sing_word))); /* held: again */
  follow_up();

  /* the correct spelling appears letter by letter as it is said */
  if (reveal && mode == M_SPELL) {
    const char *w = word_text(round_i);
    int n = clampi(sp_mark, 0, 8), i;
    for (i = 0; i < n && w[i]; i++) disp[i] = w[i];
    disp[i] = 0;
  }

  /* ---- LEDs */
  uint32_t lit = 0, lb = 0;
  if (mode == M_MYSTERY) lit = guessed;
  if (mode == M_SPELL && after == AFTER_NONE && in_len) lb |= 1u << BTN_PLAY;
  if (mode == M_SAYIT || mode == M_CODE || (mode == M_MYSTERY && guessed_done)) lb |= 1u << BTN_PLAY;
  if (in_len) lb |= 1u << BTN_REC;
  lb |= 1u << (page ? BTN_ENV : BTN_HOME);
  if (bends_on) lb |= 1u << BTN_GLO;
  if (sp_busy()) lb |= 1u << BTN_SEQ;
  uint32_t lk = held | lit;
  if (lk != led_k) {
    for (int i = 0; i < KEY_COUNT; i++)
      if ((lk ^ led_k) >> i & 1u) led_key(i, lk >> i & 1u);
    led_k = lk;
  }
  if (lb != led_b) {
    for (int i = 0; i < BTN_COUNT; i++)
      if ((lb ^ led_b) >> i & 1u) led_button(i, lb >> i & 1u);
    led_b = lb;
  }

  /* ---- the screen, only what changed */
  if (!drawn) clear(C_BG), drawn = 1;
  uint32_t h = (uint32_t)lang | (uint32_t)level << 1;
  if (h != sh_title) sh_title = h, draw_title();
  int cursor = (mode == M_SPELL || mode == M_CODE) && after == AFTER_NONE && !reveal && (now / 400) % 2 ? in_len : -1;
  h = (uint32_t)(cursor + 1);
  for (int i = 0; i < 8 && disp[i]; i++) h = h * 131u + (uint8_t)disp[i];
  if (h != sh_vfd) sh_vfd = h, draw_vfd(disp, cursor);
  h = (uint32_t)mode | (uint32_t)lang << 3 | (uint32_t)round_i << 4 | (uint32_t)score << 9 | (uint32_t)misses << 14 | (uint32_t)sing_word << 18;
  if (h != sh_info) sh_info = h, draw_info();
  h = (held * 7u) ^ (lit * 13u) ^ (uint32_t)(mode == M_SING) << 31;
  if (h != sh_keys) sh_keys = h, draw_keys(held, lit);
  h = (uint32_t)page | (uint32_t)bends_on << 1;
  for (int i = 0; i < 4; i++) h = h * 257u + (uint32_t)(knob[page * 4 + i] + 20);
  if (h != sh_knobs) sh_knobs = h, draw_knobs();
  h = (uint32_t)page | (uint32_t)bends_on << 1 | (uint32_t)(mode == M_SING) << 2 | (uint32_t)(oct + 2) << 3 | (uint32_t)(now - saved_t < 1500) << 6;
  if (h != sh_foot) sh_foot = h, draw_foot(now);
}

/* ---------------------------------------------------------------- the sound */

void audio(int32_t *out, uint32_t frames) { sp_render(out, frames); }
