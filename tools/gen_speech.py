#!/usr/bin/env python3
"""SPELLBOX's speech -> src/speech.h

Everything SPELLBOX says comes from espeak-ng (pip install espeakng-loader wordfreq scipy numpy), brought down to
8 kHz and coded the way the 1978 speech chips did it: frames of energy, pitch and ten reflection coefficients
(10th-order LPC), about 1,500 bits a second. The quantising tables are trained here; the decoder is src/speech.c.

  - the game vocabulary: letters, numbers, phrases, 100 words in four levels (25 ms frames)
  - 42 phonemes cut out of espeak's speech (12.5 ms frames), which src/tts.c strings together to say anything
  - a pronouncing dictionary of the commonest English words (from espeak), and letter-to-sound rules for the rest

  python3 tools/gen_speech.py
"""

import os
import re
import sys
import ctypes

import numpy as np
from scipy.signal import resample_poly, lfilter, butter

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FS = 8000
FRAME = 200  # 25 ms (words); the phonemes use FRAME // 2
DICT_WORDS = 12000

# ---------------------------------------------------------------- the vocabulary

PHRASES = [
  ("SPELL", "spell"),
  ("SAY_IT", "say it"),
  ("NOW_SPELL", "now spell"),
  ("CORRECT", "that is correct"),
  ("YOU_CORRECT", "you are correct"),
  ("WRONG", "wrong, try again"),
  ("INCORRECT", "that is incorrect"),
  ("SPELLING_OF", "the correct spelling of"),
  ("IS", "is"),
  ("SCORE", "here is your score"),
  ("ANSWERS", "correct answers"),
  ("PERFECT", "perfect score"),
  ("I_WIN", "I win"),
  ("YOU_WIN", "you win"),
  ("MYSTERY", "mystery word"),
  ("SECRET", "secret code"),
  ("LETTER", "letter"),
  ("SAY", "say"),
  ("LEVEL", "level"),
  ("GUESS", "guess a letter"),
  ("TYPE", "type a word"),
  ("SING", "sing"),
  ("HELLO", "hello, let's spell"),
  ("PLAY", "play the letters"),
  ("SPEAK", "type, and I will speak"),
]
LETTERS = ["ay", "bee", "see", "dee", "ee", "eff", "jee", "aitch", "eye", "jay", "kay", "ell", "em", "en", "oh", "pee", "cue",
           "are", "ess", "tee", "you", "vee", "double you", "ex", "why", "zee"]
NUMBERS = ["zero", "one", "two", "three", "four", "five", "six", "seven", "eight", "nine", "ten"]
WORDS = [  # four levels of 25 words, at most 8 letters (the display)
  "cat dog fish house tree ball milk book bird cake frog hand rain ship star apple green sleep water school chair horse train light happy",
  "answer people friend pretty laugh again many done guess ocean eight enough island listen money often said sure toward young build cousin ghost heart half",
  "believe business calendar foreign guitar height machine neighbor nephew receive rhythm science separate sugar thought tongue villain weird wrinkle yacht biscuit captain cereal circuit dessert",
  "acquire bureau gauge hygiene parallel surgeon vacuum colonel conquer genuine guardian leisure mortgage plateau pursue rhyme sergeant subtle sword tortoise unique zealous plague debris knead",
]
VOICE = dict(voice="en-us", rate=135, pitch=42, rng=70)

# ---------------------------------------------------------------- the phonemes

# name, espeak's mnemonic, the carrier it is cut from ("X" stands for it), voicing (1 voiced, 0 not, None: listen)
VOWEL = "IY IH EH AE AA AH AO UH UW AX ER AXR EY AY OY AW OW".split()
PHONEMES = [
  ("IY", "i:", "d'Xd@", 1), ("IH", "I", "d'Xd@", 1), ("EH", "E", "d'Xd@", 1), ("AE", "a", "d'Xd@", 1),
  ("AA", "A:", "d'Xd@", 1), ("AH", "V", "d'Xd@", 1), ("AO", "O:", "d'Xd@", 1), ("UH", "U", "d'Xd@", 1),
  ("UW", "u:", "d'Xd@", 1), ("AX", "@", "b'VtXd", 1), ("ER", "3:", "d'Xd@", 1), ("AXR", "3", "b'VtXd", 1),
  ("EY", "eI", "d'Xd@", 1), ("AY", "aI", "d'Xd@", 1), ("OY", "OI", "d'Xd@", 1), ("AW", "aU", "d'Xd@", 1),
  ("OW", "oU", "d'Xd@", 1),
  ("P", "p", "@X'a", 0), ("B", "b", "@X'a", None), ("T", "t", "@X'a", 0), ("D", "d", "@X'a", None),
  ("K", "k", "@X'a", 0), ("G", "g", "@X'a", None), ("F", "f", "@X'a", 0), ("V", "v", "@X'a", None),
  ("TH", "T", "@X'a", 0), ("DH", "D", "@X'a", None), ("S", "s", "@X'a", 0), ("Z", "z", "@X'a", None),
  ("SH", "S", "@X'a", 0), ("ZH", "Z", "@X'a", None), ("HH", "h", "@X'a", 0), ("CH", "tS", "@X'a", 0),
  ("JH", "dZ", "@X'a", None), ("M", "m", "@X'a", 1), ("N", "n", "@X'a", 1), ("NG", "N", "s'IX@", 1),
  ("L", "l", "@X'a", 1), ("R", "r", "@X'a", 1), ("W", "w", "@X'a", 1), ("Y", "j", "@X'a", 1),
  ("DX", "t#", "b'VX@", 1),
]
PH_ID = {p[0]: i for i, p in enumerate(PHONEMES)}
STRESS = 0x40

# espeak's phonemes (en-us) -> ours
ESPEAK = {
  "i:": "IY", "i": "IY", "I": "IH", "I#": "IH", "I2": "IH", "e": "EH", "E": "EH", "a": "AE", "a#": "AX", "A:": "AA",
  "A": "AA", "0": "AA", "V": "AH", "O:": "AO", "O": "AO", "U": "UH", "u:": "UW", "u": "UW", "@": "AX", "@2": "AX",
  "@5": "AX", "3:": "ER", "3": "AXR", "eI": "EY", "aI": "AY", "OI": "OY", "aU": "AW", "oU": "OW", "o@": "OW R",
  "O@": "AO R", "A@": "AA R", "e@": "EH R", "i@": "IY AXR", "i@3": "IY R", "U@": "UH R", "aI@": "AY AXR",
  "aU@": "AW AXR", "aI3": "AY AXR", "@L": "AX L", "@-": "AX", "IR": "IH R", "VR": "ER", "eI@": "EY AXR",
  "p": "P", "b": "B", "t": "T", "d": "D", "k": "K", "g": "G", "f": "F", "v": "V", "T": "TH", "D": "DH", "s": "S",
  "z": "Z", "S": "SH", "Z": "ZH", "h": "HH", "tS": "CH", "dZ": "JH", "m": "M", "n": "N", "N": "NG", "l": "L",
  "r": "R", "w": "W", "j": "Y", "t#": "DX", "d#": "D", "z#": "Z", "n-": "N", "l-": "L", "r-": "R", "x": "K",
  "W": "W", "t2": "T", "d2": "D", "k2": "K",
}
_SYMS = sorted(ESPEAK, key=len, reverse=True)


def to_ours(ph):
  """espeak's phoneme string -> our phoneme bytes (STRESS on stressed vowels)"""
  out, stress, i = [], False, 0
  while i < len(ph):
    c = ph[i]
    if c in "',":
      stress, i = True, i + 1
      continue
    for s in _SYMS:
      if ph.startswith(s, i):
        for p in ESPEAK[s].split():
          b = PH_ID[p]
          if p in VOWEL and stress:
            b |= STRESS
            stress = False
          out.append(b)
        i += len(s)
        break
    else:
      i += 1  # a length mark, a separator ...
  return out


# ---------------------------------------------------------------- letter-to-sound rules (for words not in the dictionary)

# left[match]right=phonemes. In the contexts: # one or more vowels, : any consonants (or none), ^ one consonant,
# . a voiced consonant, + E I or Y, % a suffix (E ES ED ER ING ELY), space: the word's edge. Tried in order.
RULES = """
 [A] =AX
[ARE] =AA R

[AR]=AA R
[AIR]=EH R
[AU]=AO
[AW]=AO
[AI]=EY
[AY]=EY
[ALL]=AO L
[ALK]=AO K
[ALT]=AO L T
[WAY]=W EY
#:^[ANCE] =AX N S
#:^[AL] =AX L
[A]^%=EY
[A]^E =EY
[A] =AX
[AGE] =IH JH
[A]=AE
[B] =B
M[B] =
[BB]=B
[B]=B
S[CH]=K
[CH]=CH
[CK]=K
[CIOUS]=SH AX S
[CIAL]=SH AX L
[CC]+=K S
[C]+=S
[C]=K
[DGE]=JH
[DD]=D
[D]=D
[EAU]=OW
[EAR]=IH R
[EE]=IY
[EA]=IY
C[EI]=IY
[EI]=EY
[EY] =IY
[EY]=EY
[EW]=Y UW
#:[ER]=AXR
[ER]=ER
#:^[E]LY =
#:[E] =
 :[E] =IY
S[ES] =IH Z
Z[ES] =IH Z
H[ES] =IH Z
X[ES] =IH Z
C[ES] =IH Z
G[ES] =IH Z
K[ED] =T
P[ED] =T
S[ED] =T
H[ED] =T
F[ED] =T
X[ED] =T
T[ED] =IH D
D[ED] =IH D
#:[ES] =Z
#:[ED] =D
[E]^%=IY
[E]^E =IY
 [EX]#=IH G Z
#:^[ENT]=AX N T
[EN] =AX N
[E]=EH
[FF]=F
[F]=F
 [GH]=G
[GH]=
[GG]=G
 [GN]=N
[GN] =N
[G]+=JH
[G]=G
[H]#=HH
[H]=
#:[IVE] =IH V
[IGH]=AY
 :[IE] =AY
[IE]=IY
[IR]=ER
[IND]=AY N D
[ILD]=AY L D
[I]^%=AY
[I]^E =AY
 :[I] =AY
[I]=IH
[J]=JH
 [KN]=N
[K]=K
#^[LE] =AX L
[LL]=L
[L]=L
[MM]=M
[M]=M
[NG]=NG
[NK]=NG K
[NN]=N
[N]=N
[OOK]=UH K
[OOD]=UH D
[OO]=UW
[OA]=OW
[OE] =OW
[OI]=OY
[OY]=OY
[OUR]=AO R
#:[ORS] =AXR Z
#:[OR] =AXR
#:^[ON] =AX N
[OUGHT]=AO T
[OUGH]=AH F
[OUS]=AX S
[OU]=AW
[OW] =OW
[OW]=AW
[OR]=AO R
[OLD]=OW L D
[OF] =AX V
[O]^%=OW
[O]^E =OW
 :[O] =OW
[O]=AA
[PH]=F
[PP]=P
[P]=P
[QU]=K W
[Q]=K
[RH]=R
[RR]=R
[R]=R
[SUAL]=ZH UW AX L
[SH]=SH
[SSION]=SH AX N
#[SION]=ZH AX N
[SION]=SH AX N
[SS]=S
#[S]#=Z
.[S] =Z
#[S] =Z
[S]=S
 [THE] =DH AX
[TH]=TH
[TUAL]=CH UW AX L
[TION]=SH AX N
[TIA]=SH AX
[TURE]=CH AXR
[TCH]=CH
[TT]=T
[T]=T
[UE] =UW
[UI]=UW
[UR]=ER
 [UN]=AH N
[U]^%=Y UW
[U]^E =Y UW
[U]=AH
[V]=V
 [WH]=W
[WR]=R
[WOR]=W ER
[WA]=W AA
[W]=W
[XC]+=K S
 [X]=Z
[X]=K S
 [Y]#=Y
 :[Y] =AY
#:[Y] =IY
[Y]^E =AY
[Y]=IH
[ZZ]=Z
[Z]=Z
"""


def parse_rules():
  out = []
  for line in RULES.strip("\n").split("\n"):
    if not line.strip():
      continue
    m = re.fullmatch(r"(.*)\[(.+)\](.*)=(.*)", line)
    assert m, line
    left, match, right, ph = m.groups()
    out.append((left, match, right, [PH_ID[p] for p in ph.split()]))
  return out


VOW = set("AEIOUY")


def _ctx_ok(word, i, ctx, step):
  """the context ctx (read in the direction step) at word[i]: the letter positions are 1..len, 0 and len+1 are spaces"""
  for c in (ctx if step > 0 else reversed(ctx)):
    ch = word[i] if 0 <= i < len(word) else " "
    if c == " ":
      if ch != " ":
        return False
      i += step
    elif c == "#":
      if ch not in "AEIOU":
        return False
      while 0 <= i < len(word) and word[i] in "AEIOU":
        i += step
    elif c == ":":
      while 0 <= i < len(word) and word[i] not in VOW and word[i] != " ":
        i += step
    elif c == "^":
      if ch in VOW or ch == " ":
        return False
      i += step
    elif c == ".":
      if ch not in "BDVGJLMNRWZ":
        return False
      i += step
    elif c == "+":
      if ch not in "EIY":
        return False
      i += step
    elif c == "%":  # forward only
      for s in ("ING", "ELY", "ER", "ES", "ED", "E"):
        if word[i:i + len(s)] == s and (i + len(s) >= len(word) or word[i + len(s)] == " "):
          i += len(s)
          break
      else:
        return False
    else:
      if ch != c:
        return False
      i += step
  return True


def apply_rules(rules, text):
  word = " " + text + " "
  out, i = [], 1
  while i < len(word) - 1:
    for left, match, right, ph in rules:
      if word.startswith(match, i) and _ctx_ok(word, i + len(match), right, 1) and _ctx_ok(word, i - 1, left, -1):
        out += ph
        i += len(match)
        break
    else:
      i += 1
  for j, b in enumerate(out):  # the first vowel takes the stress
    if PHONEMES[b][0] in VOWEL:
      out[j] |= STRESS
      break
  return out


# ---------------------------------------------------------------- espeak-ng


class Espeak:
  def __init__(self):
    import espeakng_loader as el

    class EV(ctypes.Structure):
      _fields_ = [("type", ctypes.c_int), ("uid", ctypes.c_uint), ("text_position", ctypes.c_int), ("length", ctypes.c_int),
                  ("audio_position", ctypes.c_int), ("sample", ctypes.c_int), ("user_data", ctypes.c_void_p), ("id", ctypes.c_char * 8)]

    self.lib = lib = ctypes.CDLL(el.get_library_path())
    CB = ctypes.CFUNCTYPE(ctypes.c_int, ctypes.POINTER(ctypes.c_short), ctypes.c_int, ctypes.POINTER(EV))
    self.buf, self.ev = [], []

    def cb(wav, n, ev):
      if wav and n > 0:
        self.buf.append(np.ctypeslib.as_array(wav, shape=(n,)).copy())
      i = 0
      while ev and ev[i].type != 0:
        if ev[i].type == 7:
          self.ev.append((ev[i].sample, ev[i].id.decode("latin1")))
        i += 1
      return 0

    self.cb = CB(cb)  # kept alive as long as this object
    self.rate = lib.espeak_Initialize(2, 500, el.get_data_path().encode(), 1)
    lib.espeak_SetSynthCallback(self.cb)
    lib.espeak_SetVoiceByName(b"en-us")
    lib.espeak_TextToPhonemes.restype = ctypes.c_char_p
    lib.espeak_TextToPhonemes.argtypes = [ctypes.POINTER(ctypes.c_char_p), ctypes.c_int, ctypes.c_int]

  def say(self, text, rate=135, pitch=42, rng=70, phonemes=False):
    """8 kHz float audio, and the phoneme events (sample at 8 kHz, name)"""
    L = self.lib
    L.espeak_SetParameter(1, rate, 0)
    L.espeak_SetParameter(3, pitch, 0)
    L.espeak_SetParameter(4, rng, 0)
    self.buf.clear(), self.ev.clear()
    t = (f"[[{text}]]" if phonemes else text).encode("utf-8") + b"\0"
    L.espeak_Synth(t, len(t), 0, 0, 0, 1 | (0x100 if phonemes else 0), None, None)
    L.espeak_Synchronize()
    x = np.concatenate(self.buf).astype(np.float64) / 32768 if self.buf else np.zeros(1)
    ev = [(s * FS // self.rate, n) for s, n in self.ev]
    return resample_poly(x, 160, 441) if self.rate == 22050 else resample_poly(x, FS, self.rate), ev

  def phonemes(self, word):
    p = ctypes.c_char_p(word.encode())
    return (self.lib.espeak_TextToPhonemes(ctypes.byref(p), 0, 0) or b"").decode("latin1")


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


def analyse(x, a=0, b=None, frame=FRAME, voiced=None):
  """frames of (rms of the residual, period or 0, k[10]) over x[a:b] (x already scaled)"""
  b = len(x) if b is None else b
  pre = lfilter([1, -0.9], [1], x)
  lo = lfilter(*LPF, x)
  pad = 400
  xp, lp, xr = (np.pad(v, (pad, pad + frame)) for v in (pre, lo, x))
  win = np.hamming(320)
  lagw = np.exp(-0.5 * (2 * np.pi * 60 * np.arange(11) / FS) ** 2)
  frames = []
  for f in range((b - a + frame // 2) // frame or 1):
    c = pad + a + f * frame + frame // 2
    seg = xp[c - 160:c + 160] * win
    r = np.array([np.dot(seg[: 320 - i], seg[i:]) for i in range(11)]) * lagw
    r[0] *= 1.0001
    if r[0] < 1e-7:
      frames.append((0.0, 0, np.zeros(10)))
      continue
    k, err = levinson(r, 10)
    rms = np.sqrt(max(err, 0) / np.dot(win, win))
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
    raw = xr[c - frame // 2:c + frame // 2]
    full = np.sqrt(np.mean(raw**2)) + 1e-9
    low = np.sqrt(np.mean(lp[c - frame // 2:c + frame // 2] ** 2)) / full  # how much of it is under 900 Hz
    v = best > 0.45 and low > 0.25 and full > 0.012
    if voiced is not None:
      v = voiced and full > 0.008
    frames.append((rms, (lag or 80) if v else 0, k))
  if voiced is None:  # a lone voiced or unvoiced frame between two of the other kind is a mistake
    for i in range(1, len(frames) - 1):
      p, q, r_ = (frames[j][1] > 0 for j in (i - 1, i, i + 1))
      if p == r_ != q:
        frames[i] = (frames[i][0], frames[i - 1][1] if p else 0, frames[i][2])
  return frames


# ---------------------------------------------------------------- quantising and coding


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


def c_bytes(name, data, kind="uint8_t"):
  L = [f"static const {kind} {name}[{len(data)}] = {{"]
  for i in range(0, len(data), 24):
    L.append("  " + ", ".join(str(b) for b in data[i:i + 24]) + ",")
  L.append("};")
  return L


def main():
  es = Espeak()
  # ---- the vocabulary
  utts = [(f"letter_{chr(65 + i)}", t) for i, t in enumerate(LETTERS)]
  utts += [(f"num_{i}", t) for i, t in enumerate(NUMBERS)]
  utts += [(f"ph_{k}", t) for k, t in PHRASES]
  for lv, line in enumerate(WORDS):
    ws = line.split()
    assert len(ws) == 25, (lv, len(ws))
    for w in ws:
      assert w.isalpha() and w.isascii() and len(w) <= 8, w
      utts.append((f"w{lv}_{w}", w))
  analysed = []
  for name, text in utts:
    x, _ = es.say(text, VOICE["rate"], VOICE["pitch"], VOICE["rng"])
    x = trim(x)
    analysed.append(analyse(x / (np.abs(x).max() + 1e-9) * 0.9))
  # ---- the phonemes, cut from monotone espeak speech
  allo = []
  for name, sym, carrier, voiced in PHONEMES:
    x, ev = es.say(carrier.replace("X", sym), rate=180, pitch=42, rng=0, phonemes=True)
    x = x / (np.abs(x).max() + 1e-9) * 0.9
    idx = {"d'Xd@": 1, "b'VtXd": 3, "@X'a": 1, "s'IX@": 2, "b'VX@": 2}[carrier]  # where X is among its phonemes
    real = [(s, n.lstrip("',")) for s, n in ev if n.lstrip("',") not in ("_", "_:", ";", "")]
    if real[idx][1] != sym:
      print(f"  {name}: espeak said {[n for _, n in real]}", file=sys.stderr)
    s0 = real[idx][0]
    s1 = real[idx + 1][0] if idx + 1 < len(real) else len(x)
    if name in VOWEL:  # the steady middle, not the transitions into it
      d = s1 - s0
      s0, s1 = s0 + d // 8, s1 - d // 8
    allo.append(analyse(x, s0, s1, FRAME // 2, voiced))
  # ---- tables, trained on everything
  allf = [f for fr in analysed + allo for f in fr]
  vf = [f for f in allf if f[0] > 0]
  ktab = []
  for i in range(10):
    ktab.append(train([f[2][i] for f in vf if (i < 4 or f[1] > 0)], 1 << KBITS[i]))
  ch = chirp().astype(float) / 127

  def exc_rms(p):
    return np.sqrt(np.sum(ch[: min(p, 41)] ** 2) / p) if p else 1.0

  amps = np.array([f[0] / exc_rms(f[1]) for f in vf])
  lo, hi = np.quantile(amps, 0.03), np.quantile(amps, 0.995)
  etab = np.geomspace(lo, hi, 14)
  bits = Bits()
  at = []
  stats = dict(frames=0, rep=0)
  for fr in analysed + allo:
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
  print(f"{stats['frames']} frames ({stats['rep']} repeated), {len(data)} bytes of speech", file=sys.stderr)

  # ---- the dictionary: the commonest words, as espeak says them
  from wordfreq import top_n_list

  words = []
  for w in top_n_list("en", 60000):
    if re.fullmatch(r"[a-z]{1,15}", w) and (len(w) > 1 or w in "ai"):
      words.append(w)
    if len(words) >= DICT_WORDS:
      break
  for line in WORDS:
    words += [w for w in line.split() if w not in words]
  entries = sorted({w.upper(): to_ours(es.phonemes(w)) for w in words}.items())
  dd, buckets = bytearray(), []
  for i, (w, ph) in enumerate(entries):
    if i % 16 == 0:
      buckets.append(len(dd))
      shared = 0
    else:
      prevw = entries[i - 1][0]
      shared = 0
      while shared < min(len(w), len(prevw), 15) and w[shared] == prevw[shared]:
        shared += 1
    suf = w[shared:]
    assert len(suf) <= 15 and len(ph) < 32
    dd.append(shared << 4 | len(suf))
    dd += suf.encode()
    dd.append(len(ph))
    dd += bytes(ph)
  print(f"dictionary: {len(entries)} words, {len(dd) + 4 * len(buckets)} bytes", file=sys.stderr)

  # ---- the rules, and how well they do on words they never saw
  rules = parse_rules()
  test = [w for w in top_n_list("en", 60000) if re.fullmatch(r"[a-z]{3,12}", w)][DICT_WORDS:DICT_WORDS + 2000]
  dist = lambda a, b: np.array([[0]])  # noqa
  ok = 0
  for w in test:
    want = [p & ~STRESS for p in to_ours(es.phonemes(w))]
    got = [p & ~STRESS for p in apply_rules(rules, w.upper())]
    ok += want == got
  print(f"rules: {ok / len(test):.0%} of 2000 unseen words exactly right", file=sys.stderr)

  # ---- src/speech.h
  q = lambda t: ", ".join(str(int(round(v * 16384))) for v in t)
  L = []
  L.append("/* SPDX-License-Identifier: GPL-3.0-only */")
  L.append("/* Generated by tools/gen_speech.py: SPELLBOX's speech as 10th-order LPC frames, its phonemes, its pronouncing")
  L.append(" * dictionary and letter-to-sound rules (do not edit) */")
  L.append("#ifndef SPEECH_H\n#define SPEECH_H\n#include <stdint.h>\n")
  L.append(f"#define SP_KBITS {{{', '.join(map(str, KBITS))}}}")
  for i in range(10):
    L.append(f"static const int16_t SP_K{i + 1}[{len(ktab[i])}] = {{{q(ktab[i])}}};")
  L.append("static const int16_t *const SP_KTAB[10] = {SP_K1, SP_K2, SP_K3, SP_K4, SP_K5, SP_K6, SP_K7, SP_K8, SP_K9, SP_K10};")
  escale = 1600 / etab[-1]
  L.append(f"static const uint16_t SP_ENERGY[16] = {{0, {', '.join(str(int(round(v * escale))) for v in etab)}, 0}};")
  L.append(f"static const uint8_t SP_PERIOD[32] = {{0, {', '.join(map(str, PERIODS))}}};")
  L.append(f"static const int8_t SP_CHIRP[41] = {{{', '.join(map(str, chirp()))}}};")
  L.append(f"\n#define SP_COUNT {len(at)}")
  L.append(f"static const uint32_t SP_AT[SP_COUNT] = {{{', '.join(map(str, at))}}};")
  L += c_bytes("SP_BITS", data)
  L.append("\n#define SP_LETTER 0 /* + 0..25 */\n#define SP_NUMBER 26 /* + 0..10 */")
  L.append(f"#define SP_PHRASE 37 /* + PH_* */\nenum {{ {', '.join('PH_' + p[0] for p in PHRASES)}, PH_COUNT }};")
  L.append(f"#define SP_WORD {37 + len(PHRASES)} /* + level * 25 + n */\n#define WORDS_PER_LEVEL 25\n#define LEVELS 4")
  L.append(f"#define SP_PHONEME {len(analysed)} /* + a phoneme (12.5 ms frames) */")
  ws = [w.upper() for line in WORDS for w in line.split()]
  L.append("static const char WORD_EN[LEVELS * WORDS_PER_LEVEL][9] = {" + ", ".join(f'"{w}"' for w in ws) + "};")
  L.append("static const char *const SP_TEXT[SP_COUNT] = {" + ", ".join(
    f'"{(n[-1] if n.startswith("letter") else t.upper()).replace(chr(34), "")}"' for n, t in utts) + ", " +
    ", ".join(f'"{p[0]}"' for p in PHONEMES) + "};")
  L.append(f"\n/* the phonemes: {', '.join(p[0] for p in PHONEMES)}; vowels first ({len(VOWEL)}); | 0x40 stressed */")
  L.append(f"#define PH_VOWELS {len(VOWEL)}\n#define PH_STRESS 0x40\n#define PH_N {len(PHONEMES)}")
  L.append("enum { " + ", ".join(f"P_{p[0]}" for p in PHONEMES) + " };")
  L.append("static const char *const PH_NAME[PH_N] = {" + ", ".join(f'"{p[0]}"' for p in PHONEMES) + "};")
  L.append(f"\n/* the dictionary: {len(entries)} words, sorted, in buckets of 16. An entry: (shared letters << 4 | new")
  L.append(" * letters), the new letters, the number of phonemes, the phonemes. A bucket's first word shares nothing */")
  L.append(f"#define DICT_BUCKETS {len(buckets)}\n#define DICT_SIZE {len(dd)}")
  L += c_bytes("DICT_AT", buckets, "uint32_t")
  L += c_bytes("DICT", list(dd))
  L.append("\n/* letter-to-sound: left[match]right -> phonemes (see tools/gen_speech.py), tried in order */")
  L.append("typedef struct {\n  const char *left, *match, *right;\n  uint8_t n, ph[7];\n} rule_t;")
  L.append(f"#define RULES {len(rules)}\nstatic const rule_t RULE[RULES] = {{")
  for left, match, right, ph in rules:
    assert len(ph) <= 7
    L.append(f'  {{"{left}", "{match}", "{right}", {len(ph)}, {{{", ".join(map(str, ph)) or "0"}}}}},')
  L.append("};")
  L.append("\n#endif")
  open(os.path.join(ROOT, "src", "speech.h"), "w").write("\n".join(L) + "\n")
  size = len(data) + len(dd) + 4 * len(buckets)
  print(f"src/speech.h: {size // 1024} KiB", file=sys.stderr)


if __name__ == "__main__":
  main()
