# Cardputer MIDI Controller / Sequencer

A Bluetooth + USB MIDI controller, sequencer and (later) multitrack synth for the
**M5Stack Cardputer** and **Cardputer ADV**. One firmware runs on both: the board
is detected at boot and the M5Cardputer library reads both keyboards (the
original's GPIO key matrix and the ADV's TCA8418 chip) onto the same 4 × 14 key grid.

It grows out of [Ardutracker](https://github.com/louisng/ardutracker), a tracker and
USB MIDI sequencer for the Arduboy.

## Status: phase 3 (tracker)

- **Sequencer:** 8 tracks × 8 patterns, 1–128 steps (8 bars) per pattern, up to 4 notes per step.
  Every note keeps its velocity, length and a fine timing offset (the tracker in phase 3
  shows and edits all of it). Tracks can have different lengths and loop independently.
- **Tracker screen**: all tracks side by side with every note's velocity, length and
  timing offset, editable in place. It's the same data as the Step screen, so you can
  sequence on one and fine-tune on the other.
- **Step screen** for writing patterns note by note, **live recording** while playing
  (with optional snap-to-step and a metronome click)
- Live play from the whole keyboard: piano, 13 chord buttons, 12 drum pads, or a second
  piano octave on the top rows
- Bluetooth LE MIDI and USB MIDI, each switchable on/off at any time
- Simple speaker preview (switch off with fn+m when you only want MIDI out)
- **Scenes + arrangement** (song mode): scenes 1–99 say which pattern each track plays;
  the arrangement lists scenes in play order (e.g. `1 1 4 3 2 1 1 6`)
- **Named songs**: save as many as fit (about 1.5 MB), with names typed on the keyboard;
  Songs screen to load / save over / delete. The song you had open loads at power-on.
- **Tracker effects**: chance, ratchets, trig conditions, humanize, mod wheel, pitch bend,
  program change

## Keyboard

```
Row 1   `    1  2  3  4  5  6  7  8  9  0  -  =  del     drum pads
Row 2  tab   q  w  e  r  t  y  u  i  o  p  [  ]  \       chord buttons
Row 3  fn sh  a  s  d  f  g  h  j  k  l  ;  '  enter     piano black keys
Row 4  ct op alt z  x  c  v  b  n  m  ,  .  /  space     piano white keys
```

| Key | Function |
|---|---|
| `z x c v b n m` | piano white keys C D E F G A B |
| `s d g h j` | piano black keys C# D# F# G# A# |
| `a` / `k` | octave down / up |
| `q` … `u` | the 7 chords of the current key (I ii iii IV V vi vii° in major) |
| `i o p [ ] \` | borrowed chords (major keys: bIII iv bVI bVII II III; minor keys: IV V bII I ii vii°) |
| hold `shift` / `ctrl` / `opt` / `alt` while pressing a chord | add 7th / add 9th / swap major↔minor / sus4 |
| `tab` | cycle chord inversion (root, 1st, 2nd) |
| `1` … `=` | drum pads: kick, snare, rim, clap, closed hat, open hat, low/mid/high tom, crash, ride, cowbell |
| `;` `.` `,` `/` | cursor up / down / left / right (no fn needed); hold to repeat |
| `opt` + `;` `.` | Tracker: jump 10 rows · `opt` + `,` `/` on Step: jump 10 steps |
| `space` | play / stop (any screen) |
| `enter` | arm / disarm recording (records while playing) |
| `f` / `l` | Step screen: note length for what you just entered (¼, ½, 1, 2, 3… steps); in the options panel, pattern length ± a bar |
| `'` | Step screen: pattern & track options |
| `del` | Step screen: clear the step under the cursor; on an empty step, insert **OFF** (stops held notes) |
| `` ` `` tap | cycle Step → Tracker → Live |
| `` ` `` hold | menu: `1` Live, `2` Step, `3` Tracker, `4` Scenes, `5` Arrange, `6` Songs, `7` Setup, `8` Key test, `s` save song, `b` Bluetooth MIDI, `u` USB MIDI, `m` speaker, `p` top rows mode |
| `fn` + `m` / `p` / `b` / `u` / `s` | speaker on/off · top rows piano ↔ drums+chords · Bluetooth MIDI · USB MIDI · save song (asks for a name the first time) |
| `fn` + `,` / `/` · `;` / `.` · `t` | tempo −1 / +1 · +10 / −10 · tap tempo |

**Upper piano (fn+p):** the top two rows become a second, higher piano octave in the
classic tracker layout: `q w e r t y u i o p [ ] \` are white keys (C up to A) and
`2 3 5 6 7 9 0 =` are the black keys. Press fn+p again to get drums and chords back.

### Screens

- **Step** (default): `;`/`.` pick the track, `,`/`/` move the cursor.
  - *Synth tracks:* press a piano key or chord button to write it at the cursor; the
    cursor moves to the next step. Press more keys while still holding the first to stack
    them on the same step. `f`/`l` change the length of what you just entered.
  - *Drum tracks:* a 12-lane grid; drum pads toggle hits at the cursor.
  - `'` opens options: pattern (1–8), length (1–128 steps; `f`/`l` jump a bar), **Double it**
    (doubles the length and copies the notes into the new half), **Notes end** (after their
    length, or *hold* until the track's next note or an OFF step), MIDI channel, synth/drum
    type, mute, tempo.
  - Each track's pattern has its own length and loops on its own, so a 2-bar bass line runs
    against a 1-bar drum loop in time.
- **Tracker:** one row per step (hex row numbers, bars highlighted), tracks side by side;
  the view scrolls sideways to follow the cursor and follows the playhead while playing.
  Each track shows `NOTE` (one column per chord note) `VL` (velocity, hex) `LEN` (steps)
  `OF` (timing offset in ticks, 12 per step).
  - `;`/`.` move between rows, `,`/`/` move between columns and on into the next track.
  - Piano / chord / drum keys write at the cursor and move down a row (hold one key and
    press more to stack a chord). On an existing chord, a single key replaces the note in
    the column the cursor is on.
  - `f`/`l` turn the value under the cursor down / up: note ±1 semitone, velocity ±1,
    length through ¼ ½ 1 2 3… steps, offset ±1 tick. Hold `shift` for bigger jumps
    (octave, ±16 velocity, ±1 bar of length).
  - `del` clears the value under the cursor (velocity / length / offset go back to
    defaults); on an empty row's note column it inserts OFF.
  - `FX` column: put the cursor on the letter and press `f`/`l` to pick an effect, then move
    to its value and use `f`/`l` (hold `shift` for ±16). A message explains the value.
    `del` removes it. Effects apply to every note on that row:

    | FX | Value | Does |
    |---|---|---|
    | `P` | 00–64 (0–100%) | chance the row plays |
    | `R` | 02–06 | ratchet: that many quick hits inside the step |
    | `T` | xy | trig condition: plays on loop x of every y (`T12` = every other loop, starting with the first) |
    | `H` | 00–7F | humanize: velocity varies randomly by up to ± the value |
    | `M` | 00–7F | sends mod wheel (CC 1) at the row, even without a note |
    | `B` | 00–7F | sends pitch bend (40 = centre) |
    | `G` | 00–7F | sends a program change |

    Rows with an effect show its letter on the Step screen too.
  - `'` opens the same track options as the Step screen.
- **Scenes:** rows are scenes 01–99, columns are tracks; each cell is the pattern that
  track plays in that scene (`1`–`8`) or `--` for silent.
  - `;`/`.` pick a scene (`opt` = 10 at a time), `,`/`/` pick a track.
  - Type `1`–`8` to set the pattern, `0`/`-`/`del` for silent, or `f`/`l` to step through.
  - `enter` applies the scene right now (every track switches to its patterns, so you can
    hear and edit them on the Step / Tracker screens).
  - `c` captures the patterns the tracks are on right now into the scene.
  - Scenes used in the arrangement are bright, the one playing is green.
  - A scene lasts as long as its longest pattern; shorter patterns loop inside it.
- **Arrange:** the order scenes play in.
  - Type a scene number and press `enter`: it fills the row and moves down; on the last
    (empty) row it adds a new one, so `1 enter 1 enter 4 enter 3 enter` builds the list.
  - `f`/`l` change the scene on a row, `del` removes a row (or erases typed digits).
  - `'` switches **song mode** on/off. In song mode, `space` plays the arrangement from the
    selected row, looping back to the top at the end; the status bar shows `S` and the
    row playing. Every scene starts all its patterns from step 1.
  - Each row shows the scene's patterns per track and its length in steps.
  - The Step and Tracker screens follow the patterns of the scene that's playing, so you
    edit what you hear. Pattern mode (song mode off) just loops each track's pattern.
- **Songs:** the saved songs, sorted by name, with the open one in yellow.
  `;`/`.` pick, `enter` loads, `s` saves the open song over the selected one, `del` deletes;
  each asks for a second press to confirm. *Save as new name...* lets you type a name
  (letters, digits, `-`, space; `shift` for capitals; `del` erases; `enter` saves; tap
  `` ` `` to cancel). *New empty song* starts fresh. Quick save (hold `` ` `` + `s`, or
  `fn`+`s`) saves to the open song, or asks for a name if it doesn't have one yet.
- **Live:** shows what you play. `;`/`.` pick the track your keys play into, `,`/`/` change velocity.
- **Setup:** MIDI transports, speaker, velocity, tempo, key & scale, piano mode,
  metronome (off / when recording / always), record quantize.
- **Key test:** see how many keys your unit reads at once.

Piano and chord keys play into the selected track if it's a synth track (otherwise the
first synth track); drum pads play into the selected track if it's a drum track (otherwise
the first drum track). Default tracks: 1 DRUM (ch 10), 2 BASS (ch 2), 3 CHRD (ch 3),
4 LEAD (ch 4), 5–8 (ch 5–8).

**Recording:** press `enter` (red ring in the status bar), then `space`. Whatever you play
is written into the track it's played on, in time; overdubbing the same pitch on the same
step replaces it. With *Rec quantize* on, notes snap to the nearest step.

The status bar always shows **BLE** and **USB**: grey = off, amber = on but nothing
connected, green = connected.

## Quick flash (no tools to install)

1. Download [`firmware/cardputer-midi.bin`](firmware/cardputer-midi.bin).
2. Put the Cardputer in download mode: power off, hold **G0**, plug in USB-C, power on, release G0.
3. In Chrome or Edge, open <https://espressif.github.io/esptool-js/>, set baud 921600, click **Connect** and pick the ESP32-S3 port.
4. Flash address `0x0`, choose the `.bin`, click **Program**.
5. Power-cycle the Cardputer.

## Building and flashing

Requires [PlatformIO](https://platformio.org/) (VS Code extension or `pip install platformio`).

```sh
pio run                 # build
pio run -t upload       # flash
pio device monitor      # serial log
```

The firmware uses the ESP32-S3's native USB as a USB MIDI device *and* a serial
port. If an upload can't find or reset the board, put it in download mode:
hold **G0** (the button on the side) while plugging in USB, or while pressing
reset, then upload again.

### Connecting over Bluetooth

The device advertises as **Cardputer MIDI**.
- **macOS:** Audio MIDI Setup → Window → Show MIDI Studio → Bluetooth button → Connect.
- **iOS:** pair from inside a MIDI app (GarageBand, AUM, etc.), not from system Bluetooth settings.
- **Android:** use a BLE MIDI app (e.g. "MIDI BLE Connect") or a DAW that supports BLE MIDI.
- **Windows:** Bluetooth MIDI support depends on the app; MIDIberry or loopMIDI + a BLE bridge work.

## Roadmap

1. **Foundation** ✅: hardware layer, key layout, BLE + USB MIDI, live play, key test
2. **Sequencer** ✅: clock, song/pattern data model, step sequencer, live recording with quantize
3. **Tracker** ✅: tracker view of the same data (every note's pitch, velocity, length, timing offset)
4. **Synth engine**: 8-track internal synth + drum synth (speaker; headphones on ADV)
5. **Song mode + storage**: scenes + arrangement ✅, named songs ✅; still to do: SD card, MIDI clock in/out

All views edit one shared song model, so anything entered in the step sequencer
or recorded live shows up in the tracker immediately, and the other way round.

## Code layout

```
src/
  main.cpp            setup / loop
  app/Settings        persisted user settings (NVS)
  input/KeyLayout     key -> role table (edit this to remap the keyboard)
  input/KeyInput      key list -> press/release events
  music/Theory        scales, chord building and naming
  play/LivePlayer     live notes/chords/drums, held-note bookkeeping
  seq/Song            song / track / pattern / effects data model, named save & load
  seq/Engine          clock task, transport, playback, live recording
  midi/MidiOut        sends to every enabled transport
  midi/BleMidi        BLE MIDI peripheral (NimBLE)
  audio/Monitor       speaker preview
  app/BootGuard       startup crash report
  ui/                 screens (Step, Tracker, Live, Scenes, Arrange, Songs, Setup, Key test), track options panel + effect helpers, status bar, menu
```
