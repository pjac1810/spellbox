#!/bin/sh
# Host tests of SPELLBOX: the vocabulary and some text to speech to build/render-out/*.wav, then a fuzz (UBSan).
set -e
cd "$(dirname "$0")/.."
mkdir -p build/render-out
F="-O1 -g -fsanitize=undefined -fno-sanitize-recover -Wall -Wextra -Isrc -Iplatform"
${CC:-cc} $F tests/talk.c src/speech.c -o build/talk
${CC:-cc} $F tests/fuzz.c src/speech.c src/tts.c -o build/fuzz
${CC:-cc} $F tests/say.c src/speech.c src/tts.c -o build/say
build/talk build/render-out/letters.wav 0-25
build/talk build/render-out/phrases.wav 37-61
build/talk build/render-out/words.wav 62-161
build/say build/render-out/tts.wav "HELLO I AM SPELLBOX" "THE QUICK BROWN FOX JUMPS OVER THE LAZY DOG" \
  "WOULD YOU LIKE TO PLAY A GAME" "PHILIPPE PLAYS THE ACCORDION IN GROISY"
echo "HELLO WORLD RUNNING JUMPED BOXES QUICKLY ZORGLUB" | tr ' ' '\n' | build/say words
build/fuzz
