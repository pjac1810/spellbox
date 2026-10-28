#!/bin/sh
# Host tests of SPELLBOX's speech chip: the whole vocabulary to build/render-out/*.wav, then a fuzz (UBSan).
set -e
cd "$(dirname "$0")/.."
mkdir -p build/render-out
F="-O1 -g -fsanitize=undefined -fno-sanitize-recover -Wall -Wextra -Isrc -Iplatform"
${CC:-cc} $F tests/talk.c src/speech.c -o build/talk
${CC:-cc} $F tests/fuzz.c src/speech.c -o build/fuzz
build/talk build/render-out/en-letters.wav 0-25
build/talk build/render-out/en-phrases.wav 37-60
build/talk build/render-out/en-words.wav 61-160
build/talk build/render-out/fr-letters.wav 161-186
build/talk build/render-out/fr-phrases.wav 198-221
build/talk build/render-out/fr-words.wav 222-321
build/fuzz
