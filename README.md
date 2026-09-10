# MIDI Humanizer

A VST3 drum-MIDI humanizer. Drop a MIDI clip from your DAW onto the plugin, dial in how
much timing and velocity variation you want, and read the result off a Cubase-style drum
grid with a velocity lane. The plugin plays the humanized clip back in sync with the host
transport.

- **Per-drum settings.** Every row has its own timing and velocity, so the snare can be
  loose and late while the kick stays tight. Rows can be linked and edited as one.
- **Bias and spread**, not amount times range. Where a drum sits on average, and how far
  each hit wanders from it, are two independent numbers.
- **Learn a groove** from a played take: the plugin measures its feel and applies it.
- **Presets**, A/B snapshots, undo, per-lane mute and solo, tooltips on every control.
- **An honest preview.** The grid draws the same data the player emits, not an
  approximation of it.

Built against **JUCE 8.0.12**, C++20, macOS (Apple Silicon + Intel).
Licensed under the [GNU AGPL v3 or later](LICENSE).

---

## Contents

- [Install](#install)
- [Build from source](#build-from-source)
- [Which plugin to load in which DAW](#which-plugin-to-load-in-which-daw)
- [Getting MIDI in](#getting-midi-in)
- [Drum maps](#drum-maps)
- [The viewer](#the-viewer)
- [Presets](#presets)
- [Learning a groove from a performance](#learning-a-groove-from-a-performance)
- [A/B, undo and mute/solo](#ab-undo-and-mutesolo)
- [Keyboard](#keyboard)
- [Humanization controls](#humanization-controls)
- [How the engine is designed](#how-the-engine-is-designed)
- [Tests and tools](#tests-and-tools)
- [Known limitations](#known-limitations)
- [Source layout](#source-layout)
- [Licence](#licence)

---

## Install

Grab the latest [release](https://github.com/salvolm84/MIDI-Humanizer/releases), unzip,
and copy the `.vst3` bundle into `~/Library/Audio/Plug-Ins/VST3/`.

The builds are **ad-hoc signed**, not notarised, so macOS will quarantine them after
download and the plugin will not appear in your DAW. Clear the flag once after copying:

```bash
xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/VST3/"MIDI Humanizer Inst.vst3"
```

Then rescan plugins in your host. In Cubase that is **Studio → VST Plug-in Manager**, and
note that Cubase caches its scan — quit it before replacing a bundle.

---

## Build from source

```bash
cmake -S . -B build -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release
cmake --build build -j8
```

JUCE is expected at `~/JUCE`. Point elsewhere with `-DJUCE_DIR=/path/to/JUCE`.

For a universal binary add `-DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"`; leave it off for a
much faster arm64-only development build.

`COPY_PLUGIN_AFTER_BUILD` is on, so each build installs to
`~/Library/Audio/Plug-Ins/VST3/`.

Useful options:

| Option | Default | Effect |
| --- | --- | --- |
| `MIDIHUMANIZER_BUILD_INSTRUMENT_VARIANT` | `ON` | Also build the Cubase-friendly instrument-slot VST3 |
| `MIDIHUMANIZER_BUILD_TOOLS` | `ON` | Build the console test binaries and the offscreen renderer |

Targets:

| Target | Artefact |
| --- | --- |
| `MidiHumanizer_VST3` | `MIDI Humanizer.vst3` — true MIDI effect, no audio buses |
| `MidiHumanizer_Standalone` | `MIDI Humanizer.app` — for UI work without a host |
| `MidiHumanizerInst_VST3` | `MIDI Humanizer Inst.vst3` — instrument-slot variant |
| `MidiHumanizerTests` | Engine and parser checks |
| `MidiHumanizerHostTest` | Transport + hosted-VST3 checks |
| `MidiHumanizerShot` | Renders the editor to a PNG |

---

## Which plugin to load in which DAW

The same code builds two shapes, because DAWs disagree about how a MIDI-only VST3 should
be hosted.

**`MIDI Humanizer.vst3`** is a real MIDI effect: it declares no audio buses at all. Load
it in a MIDI FX slot ahead of your drum instrument.

- **Reaper** — drop it in the track FX chain above the drum instrument. MIDI flows down
  the chain, so this just works.
- **Bitwig / Studio One / FL Studio** — use their note-FX or MIDI-FX slot.

**`MIDI Humanizer Inst.vst3`** presents itself as an instrument with a silent stereo
output and a routable MIDI output. **This is the build to use in Cubase** (confirmed
working in Cubase 15):

1. **Studio → VST Instruments**, load `MIDI Humanizer Inst`.
2. On the drum instrument track holding your VSTi, set its **MIDI input** to
   `MIDI Humanizer Inst`.
3. Drop your clip into the plugin. Cubase processes instruments continuously, so both
   **Audition** and normal playback work, and no dummy MIDI part is needed.

### Why Cubase needs the instrument build

The MIDI-effect build *does* appear in Cubase's MIDI Insert slots, and Cubase *does* call
it — the plugin's own read-out confirms it is processing blocks and emitting notes. But
Cubase discards those notes: nothing sounds, and arming the track to record captures an
empty part. Cubase's MIDI Inserts are built to *transform an incoming event stream*, so a
plugin that generates notes from nothing has nowhere to put them.

This was established by elimination rather than assumed. Generated notes provably leave
the VST3 bundle (see the test-note check in `Tests/HostTest.cpp`), and the same track
plays drawn MIDI parts correctly, so the events are being dropped on the host side. No
change to the plugin can work around it.

### No AU or AUv3

Logic's MIDI FX slot needs an AUv3 app extension, which means a separate bundle,
entitlements and code signing. Not built here.

---

## Getting MIDI in

Three routes, in order of convenience:

1. **Drag a MIDI item from the DAW onto the plugin window.** Most DAWs export a real
   `.mid` file when you drag a part out (Reaper media items, Cubase parts, Live clips,
   Studio One), and the plugin accepts that as a normal file drop.
2. **Drag a `.mid` file from Finder.** Always works.
3. **`Load MIDI…`** — a plain file chooser.

Note that the **LEARN GROOVE** zone in the preset row is the one place where a dropped MIDI
file is *not* loaded as the clip; see
[Learning a groove](#learning-a-groove-from-a-performance).

All tracks in the file are flattened into one lane set. Tempo and time signature are read
from the file, and the clip length is taken from the end-of-track marker or the last note,
rounded up to whole bars.

> **Caveat worth testing in your own DAW.** Some hosts implement drag-out as a macOS
> *promised file* (`NSFilesPromise`) rather than a plain file drag. JUCE's
> `FileDragAndDropTarget` never receives those, so the drop will simply not register. The
> plugin also accepts dropped *text* paths, which covers a few more hosts, but if your DAW
> uses promised files you will need routes 2 or 3. This is a JUCE-level limitation, not
> something the plugin can work around from inside the editor.

### Getting MIDI back out

**Drag out MIDI** in the header writes the humanized clip to a temp `.mid` and starts a
system file drag. Drag it onto a track to commit the humanization as real notes. This is
the most reliable path in Cubase, and it is how you would print the result rather than
keep the plugin live.

---

## Drum maps

Row labels come from General MIDI percussion names by default. Load your DAW's own map to
get the real kit-piece names, either via **Load drum map…** or by dropping the file onto
the plugin. Two formats are recognised, detected by sniffing the contents rather than the
extension:

- **Cubase drum maps (`.drm`)** — the XML `Map` list is read for `Name` / `INote` /
  `ONote` / `Channel`, and the optional `Order` list is honoured, so rows appear
  top-to-bottom exactly as they do in Cubase.
- **Reaper MIDI note-name files (`.txt`)** — one `<note number> <name>` per line, with
  `#` comments skipped.

Notes not present in the map keep their GM name and sort below every mapped row, so
nothing ever disappears. `Assets/` contains one example of each.

A loaded map is stored inside the plugin state, so it survives closing and reopening the
project even if the original file moves.

---

## The viewer

The grid is **read-only by design** — a reference view of what the humanizer produced, not
an editor. There is no way to add, delete or move notes; edit the source in your DAW and
drop it in again.

Notes are drawn as **diamonds** rather than bars, as in Cubase's drum mode, because a drum
hit's length carries no useful information. By default each drum gets its own hue with
brightness carrying velocity; see **Colour** below.

- **Show source** draws each note's original position as a hollow diamond joined to the
  humanized one by a hairline, so timing shifts are directly readable. Generated ghost
  notes are drawn as outlines so they are never mistaken for source material.
- **Show** picks which drum rows the list contains:

  | Mode | Shows |
  | --- | --- |
  | `All rows` | Every row a loaded drum map defines, plus any note the clip uses that the map does not cover. With no map, the full pitch range the clip spans |
  | `Hide unused` | Only the drums the clip actually plays |
  | `Hide unlabelled` | Only rows with a real instrument name — with a drum map loaded, exactly the rows your kit defines. Falls back to the General MIDI table when no map is loaded, so it does not hide everything |

  `Hide unlabelled` can hide a row that *contains notes* — a clip playing a drum your map
  does not define, say. That is deliberate, since otherwise the mode would do nothing in
  exactly the case it is for, but it means the status line reports `N rows with notes
  hidden` whenever it happens. `Hide unused` cannot hide notes by definition. No mode ever
  leaves the list empty; a blank grid reads as a broken plugin, so an empty result falls
  back to showing the candidate rows.
- **Order** picks the row order: the drum map's own order, descending pitch, or kit order
  (cymbals → hats → toms → snare → kick).
- **Grid** sets the subdivision lines, from `1/1` to `1/64` including triplets.
- **Row h**, `−` / `+` / `Fit` control zoom. Mouse wheel scrolls vertically,
  Shift+wheel horizontally, Cmd+wheel zooms, and dragging pans.
- Hovering a note shows its lane, velocity, timing shift in ms and ticks, and its
  velocity delta from the source.
- The **velocity lane** shares the grid's timeline exactly, with a faint tick at each
  note's original velocity. Selecting drums in the name list narrows it to just those, which
  is the only way to read one drum's dynamics when several share the same stretch of bar;
  with nothing selected it shows every lane.
- **Colour** picks how notes are tinted. `By lane` gives every drum its own hue with
  brightness and saturation carrying velocity, so rows are easy to tell apart and linked
  drums share a colour. `By velocity` uses one blue-to-red ramp for every drum, which makes
  loudness directly comparable between rows at the cost of every drum looking alike.
- **Tips** turns the explanatory tooltips on and off. Every control has one; hover to read
  what it does and, where it matters, why.

The status line under the velocity lane reports what the transform did: notes out, ghosts
added, flams created, notes dropped by the min-gap rule, tails choked, and peak timing
shift.

### When you hear nothing

The bottom-right of the window reports what the host is actually handing the plugin,
because in a DAW silence has several unrelated causes that look identical from outside:

| Read-out | Meaning |
| --- | --- |
| `never called this plugin` | The host is not processing the plugin at all |
| `no playhead` | Called, but with no transport information; use **Audition** |
| `playing, bar-locked` | Fully synced to the host's musical position |
| `playing, position inferred` | Rolling, but the host gave no musical position, so the clip is not bar-locked |
| `N notes out on ch C` | Notes are leaving the plugin, on channel `C` |

**Test note** fires a repeating GM kick (note 36) straight out, ignoring the clip, the
humanizer and the transport. It separates a routing problem from an empty clip or a
mis-set channel in one click. If the read-out shows notes going out and the test note is
still inaudible, the host is discarding the plugin's MIDI output — see the Cubase note
above.

Channel mismatches are the other common cause: a drum clip dragged out of a DAW is often
on channel 10 while the destination expects channel 1. Set **Out ch** explicitly rather
than leaving it on `As-is`.

### Auditioning

The plugin follows the host transport. In the standalone app, or with the host stopped,
switch on **Audition** to run an internal transport at the **Tempo** setting. **Loop**
loops the clip; **Pass input thru** additionally forwards incoming MIDI, which is off by
default because this is a clip player, not an insert effect.

---

## Presets

A preset holds the master values and every per-lane override — the *feel*, not the clip —
so it applies to whatever you drop in next. Links, mutes, solos and per-lane seeds are
session state and are deliberately left alone by a preset load.

Factory presets come first in the list and double as worked examples: `Tight Studio` shows
what the per-lane Bias control is *for* by putting the hat hand slightly ahead of the snare
hand, and `Drunk Drummer` shows how far the controls go. `Level The Kit` demonstrates the
Velocity Target control rather than a feel. User presets live in
`~/Library/Application Support/SLM Audio/MIDI Humanizer/Presets`.

## Learning a groove from a performance

Drop a played MIDI take onto the **LEARN GROOVE** zone in the preset row — or click it to
browse — and each lane's controls are set from that performance.

The zone exists because a dropped MIDI file already means something else everywhere in this
editor: it becomes the clip. Nothing about the file itself distinguishes "play this" from
"measure this", so the two meanings get two places. Dropping onto the zone learns from the
file; dropping anywhere else still loads it as the clip. Non-MIDI files are declined by the
zone and fall through, so a drum map dropped on it still loads as a drum map. This is
the natural inverse of the bias-and-spread model: a lane's mean deviation from the grid *is*
its Bias, and the spread of those deviations *is* its Spread. Rather than guessing at
numbers, they can be measured off a take that already feels right.

Four values are written per lane — Bias, Spread, Velocity Target and Velocity Spread — being
the ones the analysis can actually measure. Velocity Dynamics is left alone, because how
much of the *target* clip's own accenting to keep is a taste decision the reference cannot
answer. Lanes with fewer than two hits are measured but not applied, since a single hit says
nothing about spread.

Deviations are measured against the Tighten grid setting, and the standard deviation is
converted to the peak value the Spread controls expect (a triangular distribution's standard
deviation is `peak / sqrt(6)`), so the humanized result varies about as much as the reference
actually did.

Swing is not extracted. A swung reference shows up as a large spread because its off-beats
sit far from the straight grid, and separating genuine swing from looseness needs more than
one clip's worth of evidence — analyse against a triplet grid if the reference swings.

## A/B, undo and mute/solo

- **A/B** holds two complete settings snapshots. Switching stores what is on screen into the
  slot you are leaving, so both always hold what you last had in them. `Copy to other` seeds
  a comparison from a shared starting point.
- **Undo / Redo** (`Cmd+Z`, `Cmd+Shift+Z`) covers per-lane values, links, mute, solo,
  per-lane re-rolls and preset loads. A knob drag is one step, not one per mouse move. It
  does **not** cover loading a clip or a drum map. Loading a project starts a fresh history.
- **Mute and Solo** are per lane and separate from `Humanize`, because they answer different
  questions: `Humanize` is "vary this drum", mute and solo are "let me hear it". Muted and
  non-soloed notes still appear in the grid, drawn hollow, so nothing appears to vanish.
  `M` and `S` markers show in the name list, and clicking the second gutter column mutes a
  lane directly.
- **Re-roll** in the scope bar deals a new random hand for the selected drums only, leaving
  the rest of the kit's performance exactly as it is. The one in the header re-rolls
  everything.

## Keyboard

| Key | Action |
| --- | --- |
| Up / Down | Move the lane selection |
| Shift + Up / Down | Extend the selection |
| Escape | Back to master scope |
| `M` / `S` | Mute or solo the selected drums |
| Space | Toggle Audition |
| `Cmd+Z` / `Cmd+Shift+Z` | Undo / redo |

## Humanization controls

Every control is either **scoped** or **global**. Scoped controls edit either the master
values that all lanes inherit, or one drum lane's own override; global ones always apply
to the whole kit. Scoped and global controls never share a section box, and each scoped
section heading names the drum it is currently editing.

### Per-lane editing

Click a drum in the name list to scope the Timing and Velocity sections to it. The panel
heading changes to that drum's name, and the scope bar gives you:

| Control | Effect |
| --- | --- |
| `MASTER` | Scope back to the values every lane inherits |
| `Override` | Off, the lane follows the master. On, it uses its own values — switching it on seeds them from the master so nothing jumps |
| `Lane on` | Off, the lane passes through with its written timing and velocity untouched. Physical rules (min gap, choke groups) still apply |
| `From master` | Copy the master values onto this lane |
| `To all lanes` | Copy this lane's values onto every other lane — the quick way to set up a kit |
| `Reset` | Drop the override and go back to inheriting |

The name list shows the state of each lane at a glance: a filled blue dot means the lane
has its own values, a grey dot means it inherits the master, and a hollow dot means
humanization is off for that drum. Overriding lanes also show their timing bias and
velocity target, so you can see which drums you have touched. The leftmost dot is a click
target that toggles a lane on and off directly.

### Linking lanes

Drums can be linked so that several rows behave as a single one — all the toms sharing one
feel, say. **Groups are entirely yours to define; nothing is grouped for you.** Any set of
drums can be linked, related or not: kick and closed hat, or snare and one rack tom, are as
valid as a tidy set of toms.

Select the rows you want, then press `Link`:

| Gesture | Selects |
| --- | --- |
| Click | One drum. If it is already linked, its whole group |
| Cmd-click | Adds or removes one drum from the selection |
| Shift-click | The range from the last click to here |
| Double-click | Every drum of the same kind — the quick way to grab all the toms. A selection shortcut only; it does not create a group by itself |

Once linked, the members share one set of values: editing any of them edits all of them,
clicking any of them selects the whole group, and they share a hue in the grid so the group
reads as one instrument. Group members carry a coloured letter badge in the name list, and
you can have as many independent groups as you like. `Unlink` breaks a group up again.

Two things are deliberately *not* shared: each lane keeps its own on/off state, so you can
mute one tom out of a linked set; and linking never changes the notes themselves, only how
the settings are edited.

Under the scope name, a read-out reports what actually came out for the drums in scope —
hit count, mean timing shift and the velocity range with its average — measured on the
finished clip. It is how you confirm a Velocity Target really landed where you asked rather
than taking it on trust.

A multi-selection works the same way without linking anything — handy for a one-off change
to a few drums. Linking is for when you want the relationship to persist.

> Per-lane overrides are stored in the plugin state and saved with the project, but they
> are **not** host-automatable. 128 lanes of controls would mean well over a thousand
> parameters and an unusable automation list, so only the master values are exposed to the
> host.

### Timing  *(scoped)*

| Control | Effect |
| --- | --- |
| Bias | Where the lane sits on average, in ms. Negative plays ahead of the beat, positive behind it. This is the "feel" control — a drummer's hi-hat hand sits consistently early while the snare hand sits late |
| Spread | How far individual hits wander either side of the bias. Deviations are triangular, so they cluster near the bias like real playing rather than spreading evenly |
| Swing | −100 to +100 %. At +100 % the off-beat lands on a triplet. Per lane, so you can swing the hats while the kick stays straight. Notes that are not really on the swing grid (triplet fills, drags) are left alone |
| Min gap | This drum cannot be struck twice inside this many ms. Violations drop the quieter hit; flam pairs are exempt |

Bias and spread are independent, which is the point: "6 ms late and quite loose" is two
numbers, not an amount multiplied by a range.

### Velocity  *(scoped)*

| Control | Effect |
| --- | --- |
| Target | `Off` leaves the clip's own levels alone. Any value from 1 to 127 retargets the lane so its **average** velocity becomes that value |
| Dynamics | How much of the written accenting survives a retarget. At 0 % every hit sits on the target; at 100 % your programmed dynamics are kept intact and merely re-centred on it. Only relevant when Target is set |
| Spread | How far each hit wanders either side of its level |
| Accents | Lifts hits on strong grid positions and drops weak ones. Measured from where a note was *written*, before swing and spread move it |

So "snare averaging 100 with a bit of variation" is Target 100, Dynamics 0, Spread 10.
If you would rather keep the accents you programmed and just move the lane's overall
level, raise Dynamics. Note that a high target with full dynamics will saturate the
loudest hits at 127 — that is unavoidable, not a bug.

### Grid and limits  *(global)*

| Control | Effect |
| --- | --- |
| Tighten | Pulls notes toward the grid *before* any deviation is added, so a sloppy source can be made tight-but-human |
| Tighten grid / Swing grid | Which subdivisions those two work against |
| Enforce limits | Master switch for the min-gap and choke rules |
| Choke groups | A closed or pedal hi-hat shortens a ringing open hat's tail |

### Realism  *(global)*

| Control | Effect |
| --- | --- |
| Flams | A doubled hit on one drum is physically a flam, so one stick is moved earlier and made quieter |
| Spread | How far apart the flam pair sits, in ms |
| Ghosts / Ghost vel | Generates quiet snare notes on empty sixteenths near existing snare hits. Never on a beat, and never in a bar with no snare nearby |
| Variation | Breaks up runs of identical repeated hits so they stop sounding machine-gunned |
| Hands | Alternating strong/weak hands in fast runs: the weaker hand lands slightly late and slightly softer |

### Output

`Out ch` forces a MIDI channel (or leaves each note's own), `Transp` transposes the
output, and `Seed` with `Re-roll` change the random field.

---

## How the engine is designed

Three decisions shape everything else.

**The whole clip is transformed offline, not note-by-note in the audio callback.** Any
parameter change re-runs the humanizer over the entire clip on the message thread, and the
result is what *both* the grid and the player consume. That is what makes the preview
honest: the grid is not an approximation of what you will hear, it is the same data.

**A lane's identity is never inherited.** Whether a drum is linked, muted, soloed, excluded
from humanization or sitting on its own seed is a property of that drum, not a value it
borrows from the master. Both inheritance paths go through one `copyLaneIdentity` helper,
because resolving those fields by hand is exactly what let mutes and group membership go
missing on lanes that had no override.

**Linking is an editing relationship, not an engine concept.** Linked lanes each keep their
own copy of the values; the link only means an edit is broadcast to every member. So the
humanizer needs no notion of groups at all, and a project whose links are lost would still
sound identical.

**Per-lane inheritance is resolved before the engine sees it.** The processor flattens
master-plus-overrides into one settings struct per note number, so the humanizer indexes
straight into `lane[n]` and has no notion of masters, overrides or inheritance. That keeps
the transform a pure function of (clip, settings) and makes per-lane behaviour directly
testable without a plugin host.

**Randomness is a deterministic hash, not a running generator.** Every random decision is
`hash(seed, note identity, stream)`. Two consequences the design leans on:

- The humanized clip is reproducible — same settings always give the same performance, so
  a saved project sounds like it did when you saved it.
- Widening a *Spread* scales a fixed random field instead of dealing a fresh hand, so the
  grid morphs smoothly as you turn a knob rather than reshuffling. `Re-roll` is the only
  thing that deals new cards. There is a test asserting no note ever flips direction when
  a spread is widened.

**Handover to the audio thread is lock-free and allocation-free.** A four-slot ring plus
one atomic index; the writer never touches the published slot, and the reader latches the
index once per block. Recomputes are coalesced onto a 30 Hz timer, keeping the publish rate
far below the block rate. `processBlock` only reads.

Stage order inside the transform matters and is deliberate: tighten → velocity retarget
and accents → swing → timing bias → ghosts → hand alternation and variation → spread →
flams → wrap into the loop → playability limits → clamp. The velocity retarget measures
each lane's average on the **source** clip, so accents and spread cannot feed back into
the average they are being measured against; accents run while notes are still on the
grid; and flams run *after* spread so a deliberate pair is not undone by it.

Notes nudged before the clip start wrap round to the end rather than piling up on the
downbeat, because the clip is a loop and that is where they musically belong.

The player derives its position from the host's PPQ every block rather than from an
internal sample counter, so locates, loops and tempo changes stay locked to the
arrangement. Sounding notes are tracked separately from the clip, which is what guarantees
a note-off even when the clip is replaced mid-playback, the transport stops, or a note
disappears because you changed a setting.

---

## Tests and tools

Three console binaries, no host required. Together they run **215 checks**.

```bash
# 114 checks: the humanizer, groove extraction, drum map parsers, lane linking,
# undo history, the clip player's note bookkeeping, MIDI file round trip
./build/MidiHumanizerTests_artefacts/Release/MidiHumanizerTests

# 101 checks: the processor over a fake transport, presets, A/B, undo, row
# visibility, then the built VST3 loaded through JUCE's own VST3 host
./build/MidiHumanizerHostTest_artefacts/Release/MidiHumanizerHostTest \
    "build/MidiHumanizer_artefacts/Release/VST3/MIDI Humanizer.vst3" \
    Assets/demo-rock-beat.mid
```

The renderer draws the editor to a PNG offscreen, which is how the UI gets checked without
a running host:

```bash
./build/MidiHumanizerShot_artefacts/Release/MidiHumanizerShot out.png \
    Assets/demo-funk-16ths.mid Assets/example-cubase-map.drm \
    timeSpread=14 velSpread=18 accent=45 \
    lane38.timeBias=12 lane42.timeBias=-8 \
    link=41,45,47,48 select=38 mute=45 ui.laneHeight=22
```

Arguments after the drum map are, in any order: `<paramID>=<value>` for a host parameter,
`lane<NN>.<field>=<value>` for one lane's override, `link=<notes>`, `select=<notes>`,
`mute=<notes>`, `solo=<notes>`, `preset=<name>`, and `ui.<key>=<value>` for a view option.
Naming the output file `*groovedrag*` renders the groove drop zone mid-drag.

### What is actually pinned down

Beyond the obvious round trips, the checks assert the properties the design leans on:

- **Determinism** — the same settings always produce a bit-identical clip, and widening a
  spread never flips a note's direction of travel, it only deepens it.
- **Per-lane independence** — a lane given no settings comes out bit-identical to the
  source, and a disabled lane survives a hard global humanize untouched.
- **Velocity target semantics** — at 0% Dynamics every hit lands exactly on the target; at
  100% the written accent spacing is preserved and merely re-centred, saturating at 127.
- **Lane identity is never inherited** — mute, solo, link and per-lane seed all survive on
  a lane that has no override. This one exists because getting it wrong twice is what
  prompted the `copyLaneIdentity` helper.
- **Groove extraction** — a synthetic take built with a known 12 ms bias measures back as
  12 ms, and a quantized clip measures as exactly zero bias and zero spread.
- **No stuck notes** — every note-on is matched across loop wraps, timeline jumps and a
  transport stop.
- **The VST3 really emits** — the test-note path proves notes the plugin invents leave the
  built bundle, which no other hosted check could establish.

`Assets/` holds two demo clips and one drum map of each format.

---

## Known limitations

- **DAW drag-out via promised files is not received.** See the caveat under
  [Getting MIDI in](#getting-midi-in).
- **Cubase needs the `Inst` variant**, not the MIDI-effect build.
- **No AU / AUv3**, so no Logic MIDI FX slot yet.
- **Per-lane overrides and links are not host-automatable.** See the note under
  [Per-lane editing](#per-lane-editing).
- **Undo does not cover loading a clip or a drum map**, only the settings.
- **Groove extraction does not measure swing.** See
  [Learning a groove](#learning-a-groove-from-a-performance).
- **Factory presets can only speak General MIDI.** Their per-lane entries simply do nothing
  for a kit mapped elsewhere; the master values still apply.
- **Ghost notes are snare-only.** They target the busiest snare-family lane in the clip.
- **The grid cannot be edited.** Intentional — see [The viewer](#the-viewer).
- **One tempo per recompute.** Millisecond values are converted at the current host tempo;
  a tempo *ramp* inside the clip is not followed, though a tempo change triggers a
  re-humanize.
- **Multi-track MIDI files are flattened** into a single lane set.

---

## Licence

**GNU Affero General Public License v3.0 or later** ([`LICENSE`](LICENSE)).

This is not an arbitrary choice. The plugin links **JUCE**, which is dual-licensed
[AGPLv3](https://www.gnu.org/licenses/agpl-3.0.en.html) or under a
[commercial JUCE licence](https://juce.com/legal/juce-8-licence/). Taking the open-source
path means the combined work has to be AGPLv3, so that is what this is. The project's own
files are marked `AGPL-3.0-or-later`; the binary as a whole is effectively AGPLv3 because
of JUCE.

What that means in practice:

- You may use, modify and redistribute this, including commercially.
- Anything you distribute that is built from it must also be AGPLv3, with source made
  available to whoever receives the binary. This repository serves that purpose for the
  builds on the releases page.
- The AGPL's distinguishing clause (§13) covers making a modified version available to
  users over a network. A desktop plugin does not do that, so in practice it behaves like
  the GPL here.
- No warranty, per §15–16.

If you would rather ship a closed-source product built on this code, the route is a
commercial JUCE licence plus your own agreement with every other copyright holder in the
combined work — the AGPL grant here does not extend to that.

### Third-party components

| Component | Licence |
| --- | --- |
| [JUCE 8](https://juce.com) | AGPLv3 or commercial — the AGPL option is taken here |
| Steinberg VST3 SDK (bundled with JUCE) | MIT |

VST3 is a trademark of Steinberg Media Technologies GmbH.

The distributed builds are **ad-hoc signed, not notarised**. See [Install](#install) for
the quarantine flag that has to be cleared after downloading.

---

## Source layout

```
Source/
  PluginProcessor.*     AudioProcessor: parameters, recompute scheduling, state
  PluginEditor.*        Editor shell, drag-and-drop in and out, layout
  ParamIDs.h            Parameter id constants
  Model/
    NoteSequence.*      The clip: notes in PPQ, plus state serialisation
    MidiClipIO.*        Standard MIDI file read and write
    DrumMap.*           General MIDI percussion names, families, choke groups
    DrumMapping.*       Cubase .drm and Reaper note-name parsers
    GridDivision.h      Subdivision table shared by grid, snap, tighten and swing
  Engine/
    LaneSettings.*      Per-lane values, the lane table, and its state serialisation
    GrooveExtractor.*   Measures a played performance into bias and spread
    PresetStore.*       User presets on disk
    FactoryPresets.*    The built-in setups, as plain data
    EditHistory.*       Undo stack of state snapshots
    HumanizeParams.h    Plain parameter snapshot with inheritance already resolved
    Humanizer.*         The deterministic transform
    ClipPlayer.*        Playhead-synced emission + the lock-free clip store
  UI/
    Theme.*             Cubase-style dark palette, velocity ramp, LookAndFeel
    TimelineView.*      Shared horizontal transform + bar/beat ruler
    LaneHeader.*        Shared vertical transform + drum name list
    GridEditor.*        The read-only diamond grid
    VelocityLane.*      Velocity stems
    GridToolbar.*       Grid and view settings strip
    ControlPanel.*      Humanization controls
    Widgets.h           Parameter-bound and scope-bound control helpers
Tests/
  EngineTests.cpp       Pure-logic checks
  HostTest.cpp          Transport and hosted-VST3 checks
  Screenshot.cpp        Offscreen editor renderer
Assets/                 Demo clips and example drum maps
```
