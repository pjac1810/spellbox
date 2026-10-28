# SPELLBOX — jeu d'orthographe parlant pour M-VAVE FM-1

Firmware alternatif qui transforme le FM-1 en jeu d'orthographe parlant, sur le modèle du Speak & Spell de Texas Instruments (1978).

On y retrouve :

- la voix de synthèse LPC d'époque : 10 coefficients, 8 kHz, convertisseur 8 bits et petit haut-parleur ;
- l'afficheur fluorescent bleu-vert à 8 caractères derrière sa vitre rouge ;
- les jeux SPELL, LETTER, SAY IT, SECRET CODE et MYSTERY WORD, en anglais, sur 4 niveaux.

En plus :

- **SING** fait chanter les mots au clavier ;
- **LETTER SYNTH** fait du clavier un instrument dont chaque touche chante sa lettre ;
- **SPEAK** prononce ce que tu tapes, comme les synthétiseurs de parole des années 80 ;
- des réglages de « circuit bending » (horloge, pitch, coefficients corrompus, bégaiement…) imitent les Speak & Spell trafiqués.

C'est un projet indépendant, sans lien avec Texas Instruments ni M-VAVE :

- Aucune donnée des ROM d'origine n'est utilisée. Les mots, les phrases et les 42 phonèmes sont prononcés par le synthétiseur libre espeak-ng, puis recodés en trames LPC par `tools/gen_speech.py`.
- Les tables de quantification, l'excitation, les règles de lecture et les listes de mots sont les nôtres.

Il est bâti sur [foma](https://github.com/konsumer/foma) (plateforme Felucca), avec installeur web, mode secours et retour au firmware d'origine.

## Jouer

**Le clavier est alphabétique**, comme sur le jouet : A est la touche la plus grave (F3), puis B, C… jusqu'à Z. La touche la plus aiguë (G5) fait ENTRÉE. L'écran montre la lettre de chaque touche.

| Preset | Déroulement |
|---|---|
| SPELL | « Spell… » + un mot. Tape-le puis ENTRÉE. Juste : « That is correct ». Faux : « Wrong, try again ». Faux deux fois : l'appareil épelle le bon mot, qui apparaît lettre par lettre. Après 10 mots, il annonce le score (« Perfect score » et fanfare à 10/10). |
| LETTER | chaque touche dit et affiche sa lettre |
| SAY IT | le mot s'affiche, « Say it ! » : dis-le à voix haute, ENTRÉE pour l'entendre, ENTRÉE pour le suivant. Après 10 mots : « Now spell », et on épelle les mêmes. |
| SECRET CODE | tape un mot, ENTRÉE : il est codé lettre pour lettre (A↔Z, B↔Y…). Recoder le message codé le décode. |
| MYSTERY WORD | le pendu : propose des lettres, 7 erreurs permises. Les lettres déjà tentées restent allumées. ENTRÉE pour un nouveau mot. |
| SING | les touches deviennent des notes : le mot choisi (SELECT parcourt tout le vocabulaire) est chanté à la hauteur de la touche, et sa voyelle tient tant que la touche est enfoncée. Fonctionne aussi en MIDI. |
| LETTER SYNTH | le clavier devient un instrument de lettres : chaque touche chante sa propre lettre (A, B, C…) à sa hauteur, et la voyelle tient tant qu'on garde la touche. Avec SELECT, une seule lettre peut aussi être mise sur toutes les touches (« B » partout, par exemple) pour jouer une mélodie. L'afficheur montre la lettre et la note. Fonctionne aussi en MIDI. |
| SPEAK | tape un ou plusieurs mots (OCT+ = espace, OCT− ou REC = effacer, SEL = tout effacer), puis ENTRÉE : SPELLBOX les prononce. SEQ répète. |

**Comment SPEAK lit un mot** :

1. Il cherche le mot dans un dictionnaire de prononciation de 12 000 mots anglais courants.
2. Sinon, il cherche un mot connu suivi d'une terminaison : -S, -ES, -ED, -ING, -LY, -ER, -EST, -NESS (« RUNNING » = RUN + ING).
3. Sinon, il applique des règles de lecture, comme les puces des années 80. Les noms propres et les mots inventés sont donc lus à l'anglaise.

Les phonèmes s'enchaînent sans coupure, sur une voix grave (autour de 80 Hz), avec une intonation qui monte sur les syllabes accentuées et retombe en fin de phrase. SPEED et PITCH (page VOICE) l'ajustent encore.

| Commande FM-1 | Fonction |
|---|---|
| PRESETS (ou EDIT) | choix du preset |
| SELECT / ALGORITHM | niveau 1 à 4 (SING : le mot à chanter ; LETTER SYNTH : A-Z ou une seule lettre) |
| PLAY | ENTRÉE |
| REC | EFFACE la dernière lettre |
| SEQ | RÉPÈTE la consigne (SPEAK : redit le texte) |
| SEL | nouvelle partie (SPEAK : efface le texte) |
| HOME | VOICE : PITCH (±12 demi-tons), SPEED (débit), CLOCK (horloge de la puce, 4 à 14 kHz : tout bouge ensemble), TONE (haut-parleur) |
| ENV | BEND : BEND (coefficients corrompus), STUTTER (trames rejouées), MONO (intonation aplatie), CRUSH (convertisseur 8 à 3 bits) |
| GLO | active ou coupe les effets BEND d'un coup |
| SAVE | garde niveau, réglages et choix de lettre |
| OCT− / OCT+ | SING, LETTER SYNTH : octave du clavier ; SPEAK : effacer / espace |
| MASTER | volume |

## Les mots

100 mots en 4 niveaux de 25, de 8 lettres au plus (la taille de l'afficheur), de CAT et APPLE jusqu'à SERGEANT ou MORTGAGE.

Pour en changer, il suffit d'éditer les listes en tête de `tools/gen_speech.py`, puis de lancer `pip install espeakng-loader wordfreq scipy numpy` et `python3 tools/gen_speech.py`.

## Obtenir et installer le firmware

- **GitHub** : pousse ce dossier sur la branche `main` d'un dépôt. Le workflow compile tout.
  - Le `.fwsc` est dans l'artefact `spellbox-fwsc` et sur la branche `firmware`.
  - Avec Settings → Pages → Source : GitHub Actions, l'émulateur et l'installeur sont publiés sur GitHub Pages.
- **Docker Desktop** : `./fm1 build` ou `./fm1 dev` (http://localhost:8000).

L'installation se fait depuis la page Install (Chrome ou Edge, FM-1 branché en USB).

**Secours** : maintiens OCT− à l'allumage ; l'onglet Rescue remet le firmware M-VAVE V15. Installer un firmware non officiel se fait à tes risques.

## Développement

- `tools/gen_speech.py` génère `src/speech.h`. Il contient :
  - le vocabulaire et les 42 phonèmes coupés dans la parole d'espeak-ng ;
  - l'analyse LPC (Levinson, détection de hauteur et de voisement), la quantification entraînée et le flux de bits ;
  - le dictionnaire compressé et les règles lettre → son. Hors dictionnaire, les règles donnent environ 83 % de phonèmes justes.
- `src/speech.c` : la puce vocale en entiers. Elle comprend :
  - un treillis à 10 étages, interpolé 8 fois par trame ;
  - l'excitation par chirp ou par bruit, le convertisseur 8 bits et le haut-parleur ;
  - l'enchaînement des phonèmes et la tenue des voyelles ;
  - les bips et la file de ce qu'il faut dire.
- `src/tts.c` : la lecture d'un texte, par le dictionnaire, les terminaisons puis les règles, avec l'intonation.
- `src/main.c` : les jeux, l'afficheur 14 segments, le clavier, les LEDs et la sauvegarde.
- `tests/run.sh` : dit le vocabulaire et des phrases dans des WAV, puis fait un fuzz de la puce, du texte → parole et de tous les réglages (UBSan).
- Identité du paquet : `FM-1_57` (dans `firmware.json`).

## Licence

GPL-3.0-only. Crédits : [LICENSING.md](LICENSING.md).

- La voix vient d'espeak-ng (GPL-3.0), utilisé seulement pour fabriquer les données.
- Le choix des mots du dictionnaire vient des listes de fréquence de [wordfreq](https://github.com/rspeer/wordfreq) (CC BY-SA 4.0).
