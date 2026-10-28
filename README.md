# SPELLBOX — jeu d'orthographe parlant pour M-VAVE FM-1

Firmware alternatif qui transforme le FM-1 en jeu d'orthographe parlant, sur le modèle du Speak & Spell de Texas Instruments (1978) et de sa version française, La Dictée Magique.

On y retrouve :

- la voix de synthèse LPC d'époque : 10 coefficients, 8 kHz, environ 1 200 bits par seconde, convertisseur 8 bits et petit haut-parleur ;
- l'afficheur fluorescent bleu-vert à 8 caractères derrière sa vitre rouge ;
- les jeux SPELL, LETTER, SAY IT, SECRET CODE et MYSTERY WORD, en anglais et en français, sur 4 niveaux.

En plus, un mode SING fait chanter les mots au clavier, et des réglages de « circuit bending » (horloge, pitch, coefficients corrompus, bégaiement…) imitent les célèbres Speak & Spell trafiqués.

C'est un projet indépendant, sans lien avec Texas Instruments ni M-VAVE :

- Aucune donnée des ROM d'origine n'est utilisée. Les 322 mots et phrases sont prononcés par le synthétiseur libre espeak-ng, puis recodés en trames LPC par `tools/gen_speech.py`.
- Les tables de quantification et l'excitation sont les nôtres.
- Les listes de mots sont originales.

Il est bâti sur [foma](https://github.com/konsumer/foma) (plateforme Felucca), avec installeur web, mode secours et retour au firmware d'origine.

## Jouer

**Le clavier est alphabétique**, comme sur le jouet : A est la touche la plus grave (F3), puis B, C… jusqu'à Z. La touche la plus aiguë (G5) fait ENTRÉE. L'écran montre la lettre de chaque touche.

| Jeu | Déroulement |
|---|---|
| SPELL / ÉPELLE | « Spell… » / « Épelle… » + un mot. Tape-le puis ENTRÉE. Juste : « That is correct ». Faux : « Wrong, try again ». Faux deux fois : l'appareil épelle le bon mot, qui apparaît lettre par lettre. Après 10 mots, il annonce le score (« Perfect score » et fanfare à 10/10). |
| LETTER / LETTRE | chaque touche dit et affiche sa lettre |
| SAY IT / DIS-LE | le mot s'affiche, « Say it ! » : dis-le à voix haute, ENTRÉE pour l'entendre, ENTRÉE pour le suivant. Après 10 mots : « Now spell », et on épelle les mêmes. |
| SECRET CODE / CODE SECRET | tape un mot, ENTRÉE : il est codé lettre pour lettre (A↔Z, B↔Y…). Recoder le message codé le décode. |
| MYSTERY WORD / MOT MYSTÈRE | le pendu : propose des lettres, 7 erreurs permises. Les lettres déjà tentées restent allumées sur le clavier. ENTRÉE pour un nouveau mot. |
| SING / CHANTE | les touches deviennent des notes : le mot choisi (SELECT parcourt tout le vocabulaire) est chanté à la hauteur de la touche, en boucle tant qu'elle est tenue. Fonctionne aussi en MIDI. OCT− / OCT+ : octave. |

| Commande FM-1 | Fonction |
|---|---|
| PRESETS (ou EDIT) | choix du jeu |
| SELECT | niveau 1 à 4 (en SING : le mot à chanter) |
| ALGORITHM | langue : anglais / français |
| PLAY | ENTRÉE |
| REC | EFFACE la dernière lettre |
| SEQ | RÉPÈTE la consigne |
| SEL | nouvelle partie |
| HOME | VOICE : PITCH (±12 demi-tons), SPEED (débit), CLOCK (horloge de la puce, 4 à 14 kHz : tout bouge ensemble), TONE (haut-parleur) |
| ENV | BEND : BEND (coefficients corrompus), STUTTER (trames rejouées), MONO (intonation aplatie), CRUSH (convertisseur 8 à 3 bits) |
| GLO | active ou coupe les effets BEND d'un coup |
| SAVE | garde langue, niveau et réglages |
| MASTER | volume |

## Les mots

100 mots par langue, répartis en 4 niveaux de 25, de 8 lettres au plus (la taille de l'afficheur) et sans accents (le clavier n'en a pas). Ils vont de CAT, CHAT, POMME jusqu'à SERGEANT, MORTGAGE, CHOEUR ou MYRTILLE.

Pour en changer, il suffit d'éditer les listes en tête de `tools/gen_speech.py`, puis de lancer `pip install espeakng-loader scipy numpy` et `python3 tools/gen_speech.py`.

## Obtenir et installer le firmware

- **GitHub** : pousse ce dossier sur la branche `main` d'un dépôt. Le workflow compile tout.
  - Le `.fwsc` est dans l'artefact `spellbox-fwsc` et sur la branche `firmware`.
  - Avec Settings → Pages → Source : GitHub Actions, l'émulateur et l'installeur sont publiés sur GitHub Pages.
- **Docker Desktop** : `./fm1 build` ou `./fm1 dev` (http://localhost:8000).

L'installation se fait depuis la page Install (Chrome ou Edge, FM-1 branché en USB).

**Secours** : maintiens OCT− à l'allumage ; l'onglet Rescue remet le firmware M-VAVE V15. Installer un firmware non officiel se fait à tes risques.

## Développement

- `tools/gen_speech.py` : vocabulaire, synthèse espeak-ng, analyse LPC (Levinson, détection de hauteur et de voisement), quantification entraînée, flux de bits → `src/speech.h`
- `src/speech.c` : la puce vocale en entiers (treillis à 10 étages, interpolation 8 fois par trame, chirp ou bruit, convertisseur 8 bits, haut-parleur), les bips et la file de ce qu'il faut dire
- `src/main.c` : les jeux, l'afficheur 14 segments, le clavier, les LEDs, la sauvegarde
- `tests/run.sh` : dit tout le vocabulaire dans des WAV, puis fait un fuzz de la puce et de tous les réglages (UBSan)
- Identité du paquet : `FM-1_57` (dans `firmware.json`)

## Licence

GPL-3.0-only. Crédits : [LICENSING.md](LICENSING.md). La voix vient d'espeak-ng (GPL-3.0), utilisé seulement pour fabriquer les données.
