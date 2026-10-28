#!/usr/bin/env python3
"""SPELLBOX's spoken vocabulary -> src/speech.h

Every word and phrase is said by espeak-ng (pip install espeakng-loader scipy numpy), brought down to 8 kHz and
coded the way the 1978 speech chips did it: 25 ms frames of energy, pitch and ten reflection coefficients
(10th-order LPC), about 1,200 bits a second. The quantising tables are trained here on this vocabulary; the
decoder is src/speech.c.

  python3 tools/gen_speech.py            (writes src/speech.h; build/speech-check/*.wav with --wav)
"""

import os
import sys

import numpy as np
from scipy.signal import resample_poly, lfilter, butter

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FS = 8000
FRAME = 200  # 25 ms

# ---------------------------------------------------------------- the vocabulary

PHRASES = [  # key, English, French
  ("SPELL", "spell", "épelle"),
  ("SAY_IT", "say it", "dis-le"),
  ("NOW_SPELL", "now spell", "maintenant, épelle"),
  ("CORRECT", "that is correct", "c'est juste"),
  ("YOU_CORRECT", "you are correct", "bravo, c'est exact"),
  ("WRONG", "wrong, try again", "faux, essaie encore"),
  ("INCORRECT", "that is incorrect", "ce n'est pas ça"),
  ("SPELLING_OF", "the correct spelling of", "on écrit"),
  ("IS", "is", "comme ceci"),
  ("SCORE", "here is your score", "voici ton score"),
  ("ANSWERS", "correct answers", "bonnes réponses"),
  ("PERFECT", "perfect score", "score parfait"),
  ("I_WIN", "I win", "j'ai gagné"),
  ("YOU_WIN", "you win", "tu as gagné"),
  ("MYSTERY", "mystery word", "mot mystère"),
  ("SECRET", "secret code", "code secret"),
  ("LETTER", "letter", "lettre"),
  ("SAY", "say", "dis"),
  ("LEVEL", "level", "niveau"),
  ("LANGUAGE", "english", "français"),
  ("GUESS", "guess a letter", "propose une lettre"),
  ("TYPE", "type a word", "tape un mot"),
  ("SING", "sing", "chante"),
  ("HELLO", "hello, let's spell", "bonjour, on épelle"),
]

LETTERS = {
  "en": ["ay", "bee", "see", "dee", "ee", "eff", "jee", "aitch", "eye", "jay", "kay", "ell", "em", "en", "oh", "pee", "cue",
         "are", "ess", "tee", "you", "vee", "double you", "ex", "why", "zee"],
  "fr": ["a", "bé", "cé", "dé", "eu", "èf", "gé", "ache", "i", "ji", "ka", "èl", "èm", "èn", "o", "pé", "ku", "èr", "èss",
         "té", "u", "vé", "double vé", "ix", "i grec", "zèd"],
}
NUMBERS = {
  "en": ["zero", "one", "two", "three", "four", "five", "six", "seven", "eight", "nine", "ten"],
  "fr": ["zéro", "un", "deux", "trois", "quatre", "cinq", "six", "sept", "huit", "neuf", "dix"],
}
# four levels of 25 words, at most 8 letters (the display), A..Z only
WORDS = {
  "en": [
    "cat dog fish house tree ball milk book bird cake frog hand rain ship star apple green sleep water school chair horse train light happy",
    "answer people friend pretty laugh again many done guess ocean eight enough island listen money often said sure toward young build cousin ghost heart half",
    "believe business calendar foreign guitar height machine neighbor nephew receive rhythm science separate sugar thought tongue villain weird wrinkle yacht biscuit captain cereal circuit dessert",
    "acquire bureau gauge hygiene parallel surgeon vacuum colonel conquer genuine guardian leisure mortgage plateau pursue rhyme sergeant subtle sword tortoise unique zealous plague debris knead",
  ],
  "fr": [
    "chat chien maison arbre pomme lait livre oiseau soleil lune fleur table porte train poisson cheval jardin rouge vert bleu main pied nez lapin ballon",
    "fromage chapeau bonjour merci voiture cuisine fille mouton papillon chocolat orange citron crayon cahier histoire musique salade chemise montagne nuage poule renard souris tortue dimanche",
    "oignon monsieur automne corps doigt poids temps femme sept vingt second fusil gentil outil parfum sirop tabac estomac hiver examen album puits orteil ciseaux semaine",
    "chorale rhume thym aquarium quatorze hymne phoque chaos choeur cygne gymnase myrtille symbole yacht whisky jazz psaume pneu sphinx aiguille abbaye accueil oeillet rythme technic",
  ],
}
WORDS["fr"][3] = WORDS["fr"][3].replace("technic", "huitre")  # no accents on the keyboard
VOICE = {"en": dict(voice="en-us", rate=135, pitch=42, rng=70), "fr": dict(voice="fr", rate=140, pitch=42, rng=70)}
# what espeak is given when the spelling would mislead it
SAY_AS = {("en", "read"): "red", ("fr", "huitre"): "huître", ("fr", "aiguille"): "aiguille", ("fr", "oeillet"): "œillet", ("fr", "choeur"): "chœur",
          ("fr", "accueil"): "accueil", ("fr", "rythme"): "rythme", ("fr", "examen"): "examen", ("fr", "pied"): "pied"}


def utterances():
  """(name, lang, text) for everything, in the order of src/speech.h"""
  out = []
  for lang in ("en", "fr"):
    for i, t in enumerate(LETTERS[lang]):
      out.append((f"{lang}_letter_{chr(65 + i)}", lang, t))
    for i, t in enumerate(NUMBERS[lang]):
      out.append((f"{lang}_num_{i}", lang, t))
    for key, en, fr in PHRASES:
      out.append((f"{lang}_ph_{key}", lang, en if lang == "en" else fr))
    for lv, line in enumerate(WORDS[lang]):
      ws = line.split()
      assert len(ws) == 25, (lang, lv, len(ws))
      for w in ws:
        assert w.isalpha() and w.isascii() and len(w) <= 8, w
        out.append((f"{lang}_w{lv}_{w}", lang, SAY_AS.get((lang, w), w)))
  return out


# ---------------------------------------------------------------- speech, through espeak-ng


def espeak():
  import ctypes
  import espeakng_loader as el

  lib = ctypes.CDLL(el.get_library_path())
  CB = ctypes.CFUNCTYPE(ctypes.c_int, ctypes.POINTER(ctypes.c_short), ctypes.c_int, ctypes.c_void_p)
  buf = []

  @CB
  def cb(wav, n, ev):
    if wav and n > 0:
      buf.append(np.ctypeslib.as_array(wav, shape=(n,)).copy())
    return 0

  rate = lib.espeak_Initialize(2, 500, el.get_data_path().encode(), 0)
  lib.espeak_SetSynthCallback(cb)

  def say(text, voice, rate_wpm, pitch, rng):
    lib.espeak_SetVoiceByName(voice.encode())
    lib.espeak_SetParameter(1, rate_wpm, 0)
    lib.espeak_SetParameter(3, pitch, 0)
    lib.espeak_SetParameter(4, rng, 0)
    buf.clear()
    t = text.encode("utf-8") + b"\0"
    lib.espeak_Synth(t, len(t), 0, 0, 0, 1, None, None)
    lib.espeak_Synchronize()
    x = np.concatenate(buf).astype(np.float64) / 32768 if buf else np.zeros(1)
    return resample_poly(x, 160, 441) if rate == 22050 else resample_poly(x, FS, rate)

  say.keep = (cb, lib)  # the callback must outlive this function
  return say


# ---------------------------------------------------------------- analysis


def levinson(r, p):
  a = np.zeros(p + 1)
  a[0] = 1.0
  k = np.zeros(p)
  err = r[0]
  for i in range(1, p + 1):
    acc = r[i] + np.dot(a[1:i], r[i - 1:0:-1])
    ki = -acc / err
    a_new = a.copy()
    for j in range(1, i):
      a_new[j] = a[j] + ki * a[i - j]
    a_new[i] = ki
    a = a_new
    k[i - 1] = ki
    err *= 1 - ki * ki
  return k, err


def trim(x):
  env = np.convolve(np.abs(x), np.ones(80) / 80, "same")
  on = np.nonzero(env > 0.02 * env.max())[0]
  a, b = max(0, on[0] - 80), min(len(x), on[-1] + 240)
  return x[a:b]


LPF = butter(4, 900 / (FS / 2))


def analyse(x):
  """frames of (rms of the residual, period or 0, k[10])"""
  x = trim(x)
  x = x / (np.abs(x).max() + 1e-9) * 0.9
  pre = lfilter([1, -0.9], [1], x)
  lo = lfilter(*LPF, x)
  n = (len(x) + FRAME - 1) // FRAME
  pad = 400
  xp = np.pad(pre, (pad, pad + FRAME))
  lp = np.pad(lo, (pad, pad + FRAME))
  xr = np.pad(x, (pad, pad + FRAME))
  win = np.hamming(320)
  lagw = np.exp(-0.5 * (2 * np.pi * 60 * np.arange(11) / FS) ** 2)
  frames = []
  for f in range(n):
    c = pad + f * FRAME + FRAME // 2
    seg = xp[c - 160:c + 160] * win
    r = np.array([np.dot(seg[: 320 - i], seg[i:]) for i in range(11)]) * lagw
    r[0] *= 1.0001
    if r[0] < 1e-7:
      frames.append((0.0, 0, np.zeros(10)))
      continue
    k, err = levinson(r, 10)
    rms = np.sqrt(max(err, 0) / np.dot(win, win))
    # pitch: autocorrelation of the low band, 50 .. 400 Hz
    s = lp[c - 200:c + 200]
    s = s - s.mean()
    clip = 0.3 * np.abs(s).max()
    s = np.where(s > clip, s - clip, np.where(s < -clip, s + clip, 0))
    best, lag = 0.0, 0
    e0 = np.dot(s, s) + 1e-12
    for L in range(20, 161):
      v = np.dot(s[:-L], s[L:]) / np.sqrt(e0 * np.dot(s[L:], s[L:]) + 1e-12)
      if v > best:
        best, lag = v, L
    raw = xr[c - 100:c + 100]
    full = np.sqrt(np.mean(raw**2)) + 1e-9
    low = np.sqrt(np.mean(lp[c - 100:c + 100] ** 2)) / full  # how much of it is under 900 Hz
    voiced = best > 0.45 and low > 0.25 and full > 0.012
    frames.append((rms, lag if voiced else 0, k))
  # a lone voiced or unvoiced frame between two of the other kind is a mistake
  for i in range(1, len(frames) - 1):
    a, b, c = (frames[j][1] > 0 for j in (i - 1, i, i + 1))
    if a == c != b:
      p = frames[i - 1][1] if a else 0
      frames[i] = (frames[i][0], p, frames[i][2])
  return frames


# ---------------------------------------------------------------- quantising


def train(vals, n, iters=12):
  vals = np.sort(np.asarray(vals))
  t = np.quantile(vals, (np.arange(n) + 0.5) / n)
  for _ in range(iters):
    idx = np.abs(vals[:, None] - t[None, :]).argmin(1)
    for i in range(n):
      if np.any(idx == i):
        t[i] = vals[idx == i].mean()
    t = np.sort(t)
  return np.clip(t, -0.985, 0.985)


KBITS = [5, 5, 4, 4, 4, 4, 4, 3, 3, 3]
PERIODS = np.round(np.geomspace(20, 160, 31)).astype(int)  # pitch codes 1..31


def chirp():
  """the excitation of a voiced period: a 5 ms sweep from 3.6 kHz down, nearly flat in spectrum (this one is ours)"""
  n = np.arange(41)
  ph = 2 * np.pi * np.cumsum(0.45 - 0.44 * n / 40)
  c = np.sin(ph) * np.exp(-n / 40.0)
  return np.round(c / np.abs(c).max() * 127).astype(int)


class Bits:
  def __init__(self):
    self.bits = []

  def put(self, v, n):
    for i in range(n - 1, -1, -1):
      self.bits.append((v >> i) & 1)

  def bytes(self):
    b = self.bits + [0] * (-len(self.bits) % 8)
    return bytes(int("".join(map(str, b[i:i + 8])), 2) for i in range(0, len(b), 8))


def main():
  say = espeak()
  utts = utterances()
  print(f"{len(utts)} utterances", file=sys.stderr)
  analysed = []
  for name, lang, text in utts:
    v = VOICE[lang]
    x = say(text, v["voice"], v["rate"], v["pitch"], v["rng"])
    analysed.append(analyse(x))
  # train the tables
  allf = [f for fr in analysed for f in fr]
  vf = [f for f in allf if f[0] > 0]
  ktab = []
  for i in range(10):
    vals = [f[2][i] for f in vf if (i < 4 or f[1] > 0)]
    ktab.append(train(vals, 1 << KBITS[i]))
  # energy: 14 levels, log, over the frames that sound; under the lowest third: silence
  ch = chirp().astype(float) / 127
  def exc_rms(p):  # the excitation's rms for a unit amplitude
    return np.sqrt(np.sum(ch[: min(p, 41)] ** 2) / p) if p else 1.0
  amps = np.array([f[0] / exc_rms(f[1]) for f in vf])
  lo, hi = np.quantile(amps, 0.03), np.quantile(amps, 0.995)
  etab = np.geomspace(lo, hi, 14)
  # code
  bits = Bits()
  at = []
  stats = dict(frames=0, rep=0)
  for fr in analysed:
    at.append(len(bits.bits))
    prev = None
    for rms, per, k in fr:
      a = rms / exc_rms(per) if rms > 0 else 0
      e = 0 if a < lo * 0.55 else int(np.abs(np.log(etab) - np.log(a)).argmin()) + 1
      stats["frames"] += 1
      bits.put(e, 4)
      if e == 0:
        prev = None
        continue
      p = 0 if per == 0 else int(np.abs(np.log(PERIODS) - np.log(per)).argmin()) + 1
      ki = [int(np.abs(ktab[i] - k[i]).argmin()) for i in range(10)]
      if not p:
        ki[4:] = [None] * 6
      rep = prev is not None and (prev[0] > 0) == (p > 0) and all(abs(ki[i] - prev[1][i]) <= (0 if i < 2 else 1) for i in range(4))
      bits.put(1 if rep else 0, 1)
      bits.put(p, 5)
      if rep:
        stats["rep"] += 1
        continue
      for i in range(4 if not p else 10):
        bits.put(ki[i], KBITS[i])
      prev = (p, ki)
    bits.put(15, 4)  # stop
  data = bits.bytes()
  print(f"{stats['frames']} frames ({stats['rep']} repeated), {len(data)} bytes, {len(data) * 8 / (stats['frames'] * 0.025):.0f} bit/s", file=sys.stderr)

  # ---- src/speech.h
  q = lambda t: ", ".join(str(int(round(v * 16384))) for v in t)
  L = []
  L.append("/* SPDX-License-Identifier: GPL-3.0-only */")
  L.append("/* Generated by tools/gen_speech.py: SPELLBOX's vocabulary, coded as 10th-order LPC frames (do not edit) */")
  L.append("#ifndef SPEECH_H\n#define SPEECH_H\n#include <stdint.h>\n")
  L.append(f"#define SP_KBITS {{{', '.join(map(str, KBITS))}}}")
  for i in range(10):
    L.append(f"static const int16_t SP_K{i + 1}[{len(ktab[i])}] = {{{q(ktab[i])}}};")
  L.append("static const int16_t *const SP_KTAB[10] = {SP_K1, SP_K2, SP_K3, SP_K4, SP_K5, SP_K6, SP_K7, SP_K8, SP_K9, SP_K10};")
  escale = 1600 / etab[-1]
  L.append(f"static const uint16_t SP_ENERGY[16] = {{0, {', '.join(str(int(round(v * escale))) for v in etab)}, 0}};")
  L.append(f"static const uint8_t SP_PERIOD[32] = {{0, {', '.join(map(str, PERIODS))}}};")
  L.append(f"static const int8_t SP_CHIRP[41] = {{{', '.join(map(str, chirp()))}}};")
  L.append(f"\n#define SP_COUNT {len(utts)}")
  L.append(f"static const uint32_t SP_AT[SP_COUNT] = {{{', '.join(map(str, at))}}};")
  L.append(f"static const uint8_t SP_BITS[{len(data)}] = {{")
  for i in range(0, len(data), 24):
    L.append("  " + ", ".join(str(b) for b in data[i:i + 24]) + ",")
  L.append("};\n")
  # the ids
  per_lang = len(utts) // 2
  L.append(f"#define SP_PER_LANG {per_lang} /* English first, then French, the same order */")
  L.append("#define SP_LETTER 0 /* + 0..25 */\n#define SP_NUMBER 26 /* + 0..10 */")
  L.append(f"#define SP_PHRASE 37 /* + PH_* */\nenum {{ {', '.join('PH_' + p[0] for p in PHRASES)}, PH_COUNT }};")
  L.append(f"#define SP_WORD {37 + len(PHRASES)} /* + level * 25 + n */\n#define WORDS_PER_LEVEL 25\n#define LEVELS 4")
  for lang in ("en", "fr"):
    ws = [w.upper() for line in WORDS[lang] for w in line.split()]
    L.append(f"static const char WORD_{lang.upper()}[LEVELS * WORDS_PER_LEVEL][9] = {{" + ", ".join(f'"{w}"' for w in ws) + "};")
  import unicodedata
  def ascii_up(t):
    t = unicodedata.normalize("NFKD", t.replace("œ", "oe")).encode("ascii", "ignore").decode()
    return t.upper().replace('"', "")
  disp = [ascii_up(t if not n.split("_")[1].startswith("w") else n.split("_", 2)[2]) for n, _, t in utts]
  disp = [d if not n.split("_")[1] == "letter" else n[-1] for d, (n, _, _) in zip(disp, utts)]
  L.append("static const char *const SP_TEXT[SP_COUNT] = {" + ", ".join(f'"{d}"' for d in disp) + "};")
  L.append("\n#endif")
  open(os.path.join(ROOT, "src", "speech.h"), "w").write("\n".join(L) + "\n")
  print(f"src/speech.h: {len(data) // 1024} KiB of speech", file=sys.stderr)


if __name__ == "__main__":
  main()
