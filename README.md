# Cardputer MIDI Controller / Sequencer

A Bluetooth + USB MIDI controller, sequencer and (later) multitrack synth for the
**M5Stack Cardputer** and **Cardputer ADV**. One firmware runs on both: the board
is detected at boot and the M5Cardputer library reads both keyboards (the
original's GPIO key matrix and the ADV's TCA8418 chip) onto the same 4 × 14 key grid.

It grows out of [Ardutracker](https://github.com/louisng/ardutracker), a tracker and
USB MIDI sequencer for the Arduboy.

## Status: phases 1–5 done

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
- **Built-in synth**: 10-voice polyphonic synth (saw / square / pulse / triangle / sine /
  noise, detuned second oscillator, resonant filter with envelope, ADSR) and a synthesized
  12-piece drum kit, on the speaker and the ADV's headphone jack. Each track has its own
  sound; fn+m turns all built-in sound off when you only want MIDI out
- **MIDI clock out**: Start / clock / Stop so the M8 or a DAW follows the tempo
- **SD card**: keep songs on the card as well as in flash, and copy between them
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
| `` ` `` hold | menu: `1` Live, `2` Step, `3` Tracker, `4` Scenes, `5` Arrange, `6` Songs, `7` Setup, `8` Key test, `9` Synth, `0` Drum MIDI, `c` connect a Bluetooth device, `s` save song, `b` Bluetooth MIDI, `u` USB MIDI, `m` speaker, `p` top rows mode |
| `fn` + `m` / `p` / `b` / `u` / `s` | speaker on/off · top rows piano ↔ drums+chords · Bluetooth MIDI · USB MIDI · save song (asks for a name the first time) |
| `fn` + `,` / `/` · `;` / `.` · `t` | tempo −1 / +1 · +10 / −10 · tap tempo |
| `fn` + `x` | mute / unmute the selected track |
| `fn` + `1` … `8` | mute / unmute track 1–8 directly (track 1 is the drum track by default) |

**Upper piano (fn+p):** the top two rows become a second, higher piano octave in the
classic tracker layout: `q w e r t y u i o p [ ] \` are white keys (C up to A) and
`2 3 5 6 7 9 0 =` are the black keys. Press fn+p again to get drums and chords back.

### Screens

- **Step** (default): `;`/`.` pick the track, `,`/`/` move the cursor.
  - *Synth tracks:* press a piano key or chord button to write it at the cursor; the
    cursor moves to the next step. Press more keys while still holding the first to stack
    them on the same step. `f`/`l` change the length of what you just entered.
  - *Drum tracks:* a 12-lane grid; drum pads toggle hits at the cursor.
  - `'` opens options: pattern (1–8), length (1–128 steps; `f`/`l` ± a bar, `opt` + `,`/`/` snaps to the previous / next whole bar, e.g. 17 → 32), **Double it**
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
- **Synth:** the built-in sound of the selected track. `;`/`.` pick a setting, `,`/`/`
  change it (`shift` = ×16), `f`/`l` switch track. The keyboard keeps playing, so you hear
  every change. The graph shows the envelope shape.
  - *Synth tracks:* Preset (Bass, Pad, Lead, Pluck, Keys, Bell, Organ, Sub: a starting
    point you can then tweak), Sound (on / off = MIDI only), Volume, Wave, Octave, Detune
    (adds a second, slightly detuned oscillator), Attack / Decay / Sustain / Release,
    Cutoff, Resonance, Filter env (how far the envelope opens the filter).
  - *Drum tracks:* Sound, Volume, Tone (brightness), Decay (shorter / longer), Pitch
    (±12 semitones) for the whole kit.
  - Instruments are saved with the song. Older songs get sensible defaults per track.
- **Drum MIDI:** what each drum sends over MIDI. Drum hits are stored per drum, so you
  can change this any time without touching patterns.
  - *Kit* (default): every drum on the track's channel, each on its own note (General MIDI:
    kick 36, snare 38, ...). Right for drum machines and DAW drum racks.
  - *A channel per drum*: each drum on its own channel, e.g. one M8 instrument per drum.
    Default: kick ch 1, snare ch 2, ... cowbell ch 12, all on note 60 (C4).
  - The quick switch is the **Drum MIDI** line in a drum track's `'` options (one press).
    For more, open the Drum MIDI page (menu `0`, or **Each drum...** in the options):
    switch mode, set the kit channel or the first drum's channel (the rest follow in
    order), one-press **GM kit on ch 10** or **ch 1-12 on C4**, one note for all drums, or
    each drum's channel (`,`/`/`) and note (`f`/`l`, `shift` = octave). Hitting a drum pad
    jumps to its row.
  - Saved with the song.
- **Connect** (hold `` ` `` + `c`, or Setup → *Connect to BT device*): for Bluetooth MIDI
  devices that don't connect to you but wait to be connected to.
  - Put the device in discoverable / pairing mode, choose **Scan for devices**, then pick
    it and press `enter`. Devices advertising BLE MIDI are marked `MIDI` and listed first;
    the number is signal strength.
  - The device is remembered: with **Reconnect at power-on** on (default), the Cardputer
    keeps reconnecting to it in the background whenever it's not connected, including
    after a restart. **Disconnect** stops that until the next power-on.
  - Phones / computers can still connect to the Cardputer at the same time; MIDI goes to
    everything connected, and the BLE light shows green if anything is.
- **Generate** (hold `` ` `` + `g`): endless songs made on the device by the same generator as
  trkr.one (the same seed makes the same song on both), in the genre you pick.
  - `,` `/` genre · `space` start / pause · `enter` a new song now · `del` switch it off.
  - Each song is an ordinary song (scenes + arrangement), so everything works on it: `fn` +
    `1`…`8` mutes a part, the built-in synth and every track's MIDI channel apply, and
    `fn` + `s` saves it (it asks for a name, it never overwrites the song you had open). It
    can be edited on the Step / Tracker screens and synced like any other song.
  - While one song plays the next is built in the background and swapped in exactly when the
    arrangement starts over. Mutes, and channels you changed, carry over to the next song.
  - **Play along:** the piano keys follow the song's chord. `;` `.` choose the rule: chord tones
    only, snapped to the chord, the scale that fits it, or any note.
  - **You choose what stays on.** Nothing is switched off automatically: if the sound and
    Bluetooth MIDI are both on and there is enough RAM, both play. If the chosen genre needs more
    RAM than is free, the screen says so and what to switch off. Those switches really free
    memory: `fn` + `m` (sound off) releases the speaker driver (~18 KB), `fn` + `b` (Bluetooth MIDI
    off) unloads the Bluetooth stack (~56 KB; it isn't loaded at start-up when Bluetooth MIDI was
    left off). Turning either back on loads it again, if there is room (it says if there isn't).
    USB MIDI has no RAM to free. The screen runs in 8-bit colour while Generated mode runs.
    It stops itself when you open a song or start a cloud job.
  - Each genre needs a different amount of free RAM (Disco the most, about 61 KB, Ambient the
    least, about 13 KB); the screen shows what the chosen genre needs if there isn't enough.
    When there isn't room to build the next song beside the playing one, the playing song is
    freed first and there is a short gap.
- **Generate** (hold `` ` `` + `g`): endless songs made on the device by the same generator as
  trkr.one (the same seed makes the same song on both), in the genre you pick.
  - `,` `/` genre · `space` start / pause · `enter` a new song now · `del` switch it off.
  - Each song is an ordinary song (scenes + arrangement), so everything works on it: `fn` +
    `1`…`8` mutes a part, the built-in synth and every track's MIDI channel apply, and
    `fn` + `s` saves it (it asks for a name, it never overwrites the song you had open). It
    can be edited on the Step / Tracker screens and synced like any other song.
  - While one song plays the next is built in the background and swapped in exactly when the
    arrangement starts over. Mutes, and channels you changed, carry over to the next song.
  - **Play along:** the piano keys follow the song's chord. `;` `.` choose the rule: chord tones
    only, snapped to the chord, the scale that fits it, or any note.
  - While it runs the screen drops to 8-bit colour (the next song needs the RAM) and returns to
    full colour when you switch it off. It stops itself when you open a song or start a cloud job.
- **Cloud** (hold `` ` `` + `w`): WiFi and [trkr.one](https://trkr.one) song sync.
  - *WiFi:* choose a network from the scan (`space` rescans; "Other network" is for a hidden
    one). WiFi and your login are kept in the settings file, so flashing doesn't erase them.
  - *Sign in:* with your email and password, or "link with a code": type the shown code at
    trkr.one/link while signed in. Only a token is kept, never your password.
  - *Sync songs now:* songs you made on the website come down, songs made on the device go up
    and the website title is kept. Change a song on one side and it follows; change it on
    **both** and nothing is lost: your device copy goes up as a new song ("... (device)") and
    the website's version comes down as a separate copy. A song you delete on the device stays
    online and isn't fetched again unless it changes there. Songs with no notes aren't sent.
  - Website songs are converted to the device's own format (scenes + arrangement). The device
    keeps up to **24 patterns per track**; a song with more different bits of music on one track
    than that is skipped and reported ("too big"). Swing, key, instrument names and track names
    the device doesn't use are kept aside and sent back unchanged.
  - A job frees RAM first: the speaker driver is released, the screen drops to 8-bit colour and
    Bluetooth is unloaded; all of it comes back afterwards. `h` on the Cloud screen shows where
    memory goes.
- **Songs:** the saved songs, sorted by name, with the open one in yellow.
  `;`/`.` pick, `enter` loads, `s` saves the open song over the selected one, `del` deletes;
  each asks for a second press to confirm. `'` switches between the **FLASH** and
  **SD CARD** lists, and `x` copies the selected song to the other one. On the card, songs
  live in `/cardputer-midi/songs/*.cms`. Quick save goes back to wherever the open song
  came from. *Save as new name...* lets you type a name
  (letters, digits, `-`, space; `shift` for capitals; `del` erases; `enter` saves; tap
  `` ` `` to cancel). *New empty song* starts fresh. Quick save (hold `` ` `` + `s`, or
  `fn`+`s`) saves to the open song, or asks for a name if it doesn't have one yet.
- **Live:** shows what you play. `;`/`.` pick the track your keys play into, `,`/`/` change velocity.
- **Setup:** MIDI transports, speaker on/off and volume, velocity, tempo, key & scale,
  piano mode, metronome (off / when recording / always), record quantize, **MIDI clock
  out** (Start, 24 clocks per beat and Stop while playing; on by default; set the
  receiving device to sync to external MIDI clock).
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

Flashing an update keeps your songs **and** settings (including which song to reopen at
power-on): both live in the storage partition, which the firmware image doesn't touch.

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
4. **Synth engine** ✅: per-track synth instruments + drum kit (speaker; headphones on ADV)
5. **Song mode + storage** ✅: scenes + arrangement, named songs, SD card, MIDI clock out
6. Ideas: MIDI clock *in* (follow another device), per-lane drum channels, CC automation lanes

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
  seq/Song            song / tracks / patterns / effects / scenes / instruments, save & load
  seq/Storage         SD card mount
  seq/Engine          clock task, transport, playback, live recording
  midi/MidiOut        sends to every enabled transport
  midi/BleMidi        BLE MIDI peripheral (NimBLE)
  audio/Synth         built-in synth + drum kit (own task, streams to the speaker)
  app/BootGuard       startup crash report
  ui/                 screens (Step, Tracker, Live, Scenes, Arrange, Songs, Setup, Key test, Synth), track options panel + effect helpers, status bar, menu
```
