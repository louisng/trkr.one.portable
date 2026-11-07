# Cardputer MIDI Controller / Sequencer

A Bluetooth + USB MIDI controller, sequencer and (later) multitrack synth for the
**M5Stack Cardputer** and **Cardputer ADV**. One firmware runs on both: the board
is detected at boot and the M5Cardputer library reads both keyboards (the
original's GPIO key matrix and the ADV's TCA8418 chip) onto the same 4 × 14 key grid.

It grows out of [Ardutracker](https://github.com/louisng/ardutracker), a tracker and
USB MIDI sequencer for the Arduboy.

## Status: phase 1 (foundation)

- Play live from the whole keyboard: one-octave piano, 13 chord buttons, 12 drum pads
- Bluetooth LE MIDI and USB MIDI, each switchable on/off at any time
- Simple speaker preview so you can hear what you play without a host
- Key-test screen to see how many keys your unit can read at once (ghosting)
- Settings saved to flash
- Songs from trkr.one: sync over WiFi, play over MIDI and the speaker (see below)

## Songs and trkr.one sync

The Cardputer can play songs made on [trkr.one](https://trkr.one) — in its
tracker, or by its generator — and keeps them on its flash.

- **Hold `` ` `` → 5 Cloud**: set up WiFi (type the network name and password),
  then **Link trkr.one account**. The screen shows a code like `BKDT-7Q2M`;
  open **trkr.one/link** on a phone or computer, sign in and type it. The
  device links itself a few seconds later.
- **Sync songs now** downloads new and changed songs, removes ones you deleted
  online, and uploads songs changed on the device (tempo for now). If a song
  changed in both places, both versions are kept.
- **Hold `` ` `` → 4 Songs**: `;`/`.` select, `enter` load and play, `space`
  play/stop, `,`/`/` tempo, `del` twice to delete from the device. The keyboard
  still plays live on top, and `space` on the Live screen starts/stops the
  loaded song too.

Songs play over USB/BLE MIDI on each track's channel and through the speaker
preview. Device links can be removed on trkr.one → Account.

HTTPS uses the root certificates built into ESP-IDF, so the device checks it
is talking to the real trkr.one. Songs are read and written in the `TRKR 1`
line format (`src/song/Song.*`), which trkr.one serves to devices and can also
export as a `.trk` file. `test/host/run.sh song.trk` checks the parser on a PC.

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
| `;` `.` `,` `/` | cursor up / down / left / right (no fn needed) |
| `` ` `` tap | back to Live |
| `` ` `` hold | menu: `1` Live, `2` Setup, `3` Key test, `4` Songs, `5` Cloud, `b` toggle Bluetooth MIDI, `u` toggle USB MIDI, `m` toggle speaker |
| `space` | play / stop the song loaded on the Songs screen |
| `f` `l` `'` | free for now |

On the **Live** screen: `;`/`.` change velocity, `,`/`/` change the key's root note.
Setup lets you set channels (piano 1, chords 2, drums 10 by default), key, scale and
**piano mode**. In *scale* mode the seven white keys play the seven notes of the
current scale, so you can't hit a wrong note.

The status bar always shows **BLE** and **USB**: grey = off, amber = on but nothing
connected, green = connected.

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
2. **Sequencer**: clock, song/pattern data model, step sequencer, live recording with quantize
3. **Tracker**: tracker view of the same data (edit every note's value, velocity, length, timing offset, effect)
4. **Synth engine**: 8-track internal synth + drum synth (speaker; headphones on ADV)
5. **Song mode + storage**: pattern arranger, save/load on SD card, MIDI clock in/out

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
  midi/MidiOut        sends to every enabled transport
  midi/BleMidi        BLE MIDI peripheral (NimBLE)
  audio/Monitor       speaker preview
  play/Sequencer      plays a song: arrangement, micro-timing, swing, lengths
  song/Song           song model + TRKR 1 line-format parser/writer
  song/SongStore      songs on flash (LittleFS) + sync bookkeeping
  net/Cloud           WiFi, account linking (device code), two-way sync
  ui/                 screens (Live, Setup, Key test, Songs, Cloud), status bar, menu
```
