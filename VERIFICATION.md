# Verification report

This report covers the build delivered alongside it (CRT Player 2.10.0).

The final X11 and Wayland suites were run against the exact stripped `crtplayer` binary
that is delivered, and **again against the AppImage with the system's Qt libraries
removed**. The SHA-256 checksums are in the `.sha256` files. Raw summaries are in
`docs/results/`.

## Test environment

This environment differs from Bazzite in important ways.

| | |
|---|---|
| OS | Ubuntu 24.04.4 LTS, x86-64, in a container with **1 to 2 CPU cores and no GPU** |
| Qt / GStreamer | Qt 6.4.2 / GStreamer 1.24.2 (distribution packages) |
| OpenGL | Mesa llvmpipe, a *software* renderer, providing OpenGL 4.5 core |
| X11 | Xvfb 1920×1080 with the Openbox window manager (Qt platform `xcb`). For desk mode also the **picom** compositor, plus a desktop-type wallpaper window standing in for the desktop background |
| Wayland | Weston 13 headless compositor with its GL renderer, 1600×900 (Qt platform `wayland`) |
| Audio | PulseAudio with a null sink. A real audio sink drives the clock (`GstPulseSinkClock`), but no sound device exists |

**The binary has not been run on Bazzite, on a real GPU, or with hardware decoding.**

## Samples tested

All samples were generated with `scripts/make-test-media.sh`.

| File | Container / codecs | Coded size | PAR | Rotation tag | Expected display aspect |
|---|---|---|---|---|---|
| `sd_4x3_h264.mp4` | MP4, H.264 + AAC, 30 fps, 20 s | 640×480 | 1:1 | none | 4:3 (1.3333) |
| `hd_16x9_multitrack.mkv` | MKV, H.264 + 2× AAC (eng, jpn) + 2× SRT (eng, fre), 25 fps | 1280×720 | 1:1 | none | 16:9 (1.7778) |
| `vertical_9x16_h264.mp4` | MP4, H.264 + AAC, 30 fps | 720×1280 | 1:1 | none | 9:16 (0.5625) |
| `rotated_phone_h264.mp4` | MP4, H.264 + AAC, display matrix −90° (phone style) | 1280×720 | 1:1 | rotate-90 | 9:16 (0.5625) |
| `ultrawide_64x27_vp9.webm` | WebM, VP9 + Opus, 24 fps | 2560×1080 | 1:1 | none | 64:27 (2.3704) |
| `anamorphic_dvd_mpeg2.mkv` | MKV, MPEG-2 + AC-3, 29.97 fps | 720×480 | 32:27 | none | 16:9 (1.7778) |
| `fhd_16x9_60fps.mp4` | MP4, H.264 + AAC, 60 fps | 1920×1080 | 1:1 | none | 16:9 (1.7778) |
| `hevc_only.mp4` | MP4, H.265 | 640×360 | 1:1 | none | used for the missing-codec test |
| `composite_card.mp4` | MP4, H.264 + AAC, still test card (stripes with 2–8 px periods, fine text, saturated colour edges) | 640×480 | 1:1 | none | 4:3, used for the composite / VHS effect checks |
| `scanline_card.mp4` | MP4, H.264 4:4:4, still grey ramp (15/35/55/75/100 %) over saturated R/G/B/Y/C | 640×480 | 1:1 | none | 4:3, used to measure how scanline beams respond to brightness |

## Results

The tests fall into four groups:

- **Unit tests** (`ctest`), 2 suites: layout math, orientation matrices, auto scanline
  count, preset JSON, and preset save/rename/delete/import/export. **All pass.**
- **X11 automation** (`scripts/run-verification.sh`), seven runs plus the Jellyfin runs.
  **355 of 355 checks pass.**
- **Effect image checks** (`scripts/check-effects.py`). **76 of 76 pass**, including 54
  pairwise scanline-style comparisons.
- **Desk-mode image checks** (`scripts/check-desk.py`). **17 of 17 pass on X11 and 17
  of 17 on Wayland.**
- **Wayland automation.** **59 of 59 checks pass.**
- **Simulation image checks** (`scripts/check-sim.py`). **16 of 16 pass.**
- **Everyday-playback checks** (`scripts/check-polish.py`). **15 of 15 pass.**
- **Bazzite checks** (gamepad, room backdrop, Game Mode; `scripts/check-bazzite.py`).
  **16 of 16 pass.**
- **MPRIS checks** (`scripts/check-mpris.py`, with `playerctl` on a private session bus).
  **7 of 7 pass.**
- **Portability** (install hints for 9 distributions; `scripts/check-portability.py`).
  **9 of 9 pass.**
- **Keep awake** (`scripts/check-inhibit.py`, with stand-in desktop services). **7 of 7
  pass.**
- **Scenes** (`scripts/check-scene.py`). **8 of 8 pass.**
- **Wall-mounted TV, rooms and camera limits** (`scripts/check-wall.py`). **17 of 17 pass.**
- **Movie theater and film looks** (`scripts/check-theater.py`). **21 of 21 pass.**
- **90s CG room** (`scripts/check-cg.py`). **24 of 24 pass.**
- **Arcade cabinet** (`scripts/check-arcade.py`). **13 of 13 pass.**
- **The look's sound in the player** (`scripts/check-sound.py`). **8 of 8 pass.**
- **Tape sound unit test** (`tests/test_tape.cpp`, run by `ctest`). **11 of 11 pass.**
- **Composited screen** (`scripts/check-composited.py`: what Weston actually shows).
  Passes for the native binary and for the AppImage with its bundled Qt on Wayland.
- **Without the GStreamer "good" plugins** (`scripts/test-without-good-plugins.sh`,
  release runs). **7 of 7 pass.**
- **Jellyfin checks** (`scripts/check-jellyfin.py`). **22 of 22 pass for each server API
  version (10.8 and 10.10),** on X11 and on Wayland.
- **AppImage:** all of the above pass again with the system Qt removed.
- **Manual and visual checks:** real key presses sent through the window manager, and
  inspection of the screenshots.

### Aspect ratio and geometry

The picture rectangle was measured on screen at a 1280×800 window, which gives a
1280×710 video area.

| Sample | Mode | Measured picture | Measured ratio | Expected | Result |
|---|---|---|---|---|---|
| 4:3 | Fit | 947×710, pillarboxed | 1.3333 | 1.3333 | pass |
| 4:3 | Fill | 1280×710 (source rows 12.5–87.5 %) | 1.8028 on screen, picture itself 4:3 | fills area, no distortion | pass |
| 4:3 | Crop (10/5/10/5 %) | 841×710 | 1.1852 | 0.8×0.9 of 4:3 = 1.1852 | pass |
| 4:3 | Original | 640×480, centred | 1.3333 | 1:1 pixels | pass |
| 4:3 | Fit, fullscreen (1920×1080) | 1440×1080 | 1.3333 | 1.3333 | pass |
| 16:9 multitrack | Fit | 1262×710 | 1.7778 | 1.7778 | pass |
| Native vertical | Fit | 399×710 | 0.5625 | 0.5625 | pass |
| Rotated phone clip | Fit | 399×710; rotation reported as 90° | 0.5625 | 0.5625, upright | pass |
| Ultrawide VP9 | Fit | 1280×540, letterboxed | 2.3704 | 2.3704 | pass |
| Anamorphic MPEG-2 | Fit | 1262×710; PAR reported 32:27 | 1.7778 | 1.7778 | pass |
| 1080p60 | Fit | 1262×710 | 1.7778 | 1.7778 | pass |

**Visual checks** (screenshots in `docs/images/`):

- **Rotation direction:** the rotated clip is upright. The source's left (red) column
  appears at the top, which matches a 90° clockwise display rotation.
- **Curvature:** it does not crop content. The test pattern's corner timecode stays
  fully visible under every preset.
- **Bars:** they stay flat and black by default. With *include bars* on, the whole area
  becomes the tube.
- **The original screenshot** is native size: 640×480 for the 4:3 sample.

### Playback features

**Presets**

- All five built-in presets were selected by name.
- The screenshots are visibly distinct.
- Preset cycling with `[` was verified with real key presses.

**Effects bypass and before/after comparison**

- Bypass was toggled through the API and with the B key. The report shows
  `bypass=true/false`.
- The split view was captured with its *Original* and *CRT* labels.

**Audio and subtitle tracks**

- The MKV exposes 2 audio and 2 subtitle tracks, labelled from their tags, e.g.
  "Track 2: Japanese, Alt 880 Hz (MPEG-4 AAC)".
- Switching to audio track 1 and subtitle track 1, then subtitles off, was confirmed in
  the reports.
- The A and J keys cycled both.
- The English subtitle appears in the filtered screenshot.

**Seeking**

- A 25-step scrub from 1 s to 17 s, one step every 40 ms, ended at the target.
- An accurate seek to 5 s landed at 5.0 s plus the elapsed wait time.
- The Right key moved playback forward by 5 s.

**Frame stepping**

Checked by the displayed frame's timestamp and by testsrc2's burned-in frame counter.

- Forward steps went 6366.7 → 6400.0 → 6433.3 ms.
- Back steps went to frames 6400, 6366.7 and 6333.3. The reported timestamps were
  6416.7, 6383.3 and 6350 ms, which are the decoder's timestamps clipped to the seek
  point. Each lies inside the correct frame's interval.
- In an earlier run, the counter read "180 / 00:00:06.000" after three back steps,
  which is exactly right.
- A bug where repeated back steps landed on the same frame was found and fixed during
  this testing.

**Fullscreen and auto-hide**

- **X11:** fullscreen was entered through the API and the F key. Double-click is wired to the same action but was not exercised. After
  about 3.5 s idle while playing, `controlsVisible=false`. After a mouse move,
  `controlsVisible=true`. Esc and F leave fullscreen and restore the docked panels.
- **Wayland:** the same behaviour, with a 1200×900 picture on the 1600×900 output.

**Persistence**

- Preset, scaling mode, bypass and curvature were changed in one process and read back
  from a fresh process.
- Mute (M key) was persisted to the config file.

**Missing codec**

- With every H.265 decoder hidden from GStreamer, opening the HEVC file puts the player
  in the error state.
- The dialog reads *"Missing codec or GStreamer plugin … no plugin for: H.265 (Main
  Profile) decoder"*, followed by Bazzite-specific instructions.
- A control run with the decoders present played the file using `avdec_h265`.

**Decoders actually used:** `avdec_h264`, `vp9dec`, `avdec_mpeg2video`, `avdec_h265`
(control run), `avdec_aac`, `opusdec` and `a52dec`, all from the host's GStreamer
installation.

### A/V synchronisation and responsiveness

**Measurement method.** For each new frame painted while playing, the player records how
far the moment it is drawn trails the frame's scheduled time on the audio-sink clock.

| Sample (CRT on unless noted) | Frames shown per second* | Lateness mean / sd / max (ms) |
|---|---|---|
| 4:3 (30 fps) | 5.7–8.6 | 17.5–18.7 / ~9.5 / 33.4 |
| 16:9 multitrack (25 fps) | 4.2 | 40.3 / 14.2 / 60.6 |
| Vertical (30 fps) | 3.5 | 16.0 / 9.4 / 27.3 |
| Rotated (30 fps) | 4.2 | 15.8 / 10.1 / 30.7 |
| Ultrawide VP9 (24 fps) | 1.9 | 20.4 / 11.3 / 36.7 |
| Anamorphic MPEG-2 (29.97 fps) | 4.5 | 16.9 / 11.5 / 32.9 |
| 1080p60, CRT on | 2.5 | 9.8 / 3.7 / 15.6 |
| 1080p60, bypass | 6.2 | 10.7 / 4.2 / 16.7 |

\* **Frame rate is limited by this environment.** One CPU core does all of the
following at once: software H.264/VP9 decoding, llvmpipe rendering of every GL pass on
the CPU, and the UI. Measured paint time averaged 12–38 ms for SD/HD content and
60–100 ms for 1080p, most of it frame upload, colour conversion and mipmap generation,
which a GPU does in about a millisecond.

**What these numbers show.** The frames that were displayed appeared on average 10–40 ms
after their audio-clock time. Late frames were dropped (QoS) instead of drifting, so the
lateness stayed bounded over whole files instead of accumulating. Whether the audio
itself played smoothly could not be heard, because the sink here is a null device.

**What they do not show.** They are not full-frame-rate results, and they are not
perceptual lip-sync measurements.

**GUI responsiveness.** The longest main-loop stall while scrubbing was 141–241 ms,
mostly single software-GL paints. Stalls of up to about 750 ms happened only when
opening a new file, while the old pipeline shut down.

### UI checks

- **Panel and control-bar layout** was reviewed from window captures at 1280×800 with
  both panels docked.
- This review found two problems, both fixed:
  - the control bar overlapped at narrow widths;
  - the settings panel's slider columns didn't line up and clipped their values.
- The fixed layout was re-captured and inspected (`docs/images/ui.jpg`).

## 1.1 additions: VHS, composite / LaserDisc, scanline styles

All effect measurements were taken on the **same paused frame**, so the effect under
test is the only difference.

**Effect image checks** (`docs/results/effects-checks.txt`):

| Check | Result |
|---|---|
| Rainbow crosstalk adds false colour to the grey stripe area | Chroma level 16.6 → 26.8 (reference → LaserDisc Composite) |
| Dot crawl / rainbow animate on a paused frame | 8–12 % of pixels change between moments. The distinct states match the 4-field sequence |
| Head switching affects only the bottom of the picture | Difference 4.9 in the bottom 4.5 % vs 0.8 in the middle (VHS Home Recording vs reference) |
| Laser rot adds sparse speckles | 0.08 % of pixels |
| All six scanline styles differ from each other | Mean row-profile difference 6–31 levels for every pair (soft vs interlaced is excluded, because they share a beam shape) |
| Interlaced style alternates field position | Row pattern shifts between snapshots |
| Pixel beam adds horizontal pixel structure | Spectral peak 43× the median |
| VGA double-scan gaps are regular | Gap spacing 2–3 px at a 2.96 px line pitch |

**Visual checks** (`docs/images/analog-*.jpg`, `scanline-styles.jpg`):

- Rainbow false colour appears on stripes near the subcarrier frequency.
- A checkered dot pattern appears on vertical colour edges only; flat areas are
  unchanged (luma variation in solid red stays 4.4; at the edge it goes 5.7 → 9.9).
- Head-switching skew and noise appear at the picture's bottom edge.
- On a **vertical** video with Worn VHS TV, the tracking band, dropouts and head
  switching run across and along the *picture*, and the bars stay untouched.
- On an **ultrawide** video, laser rot stays inside the picture.

**Presets:** all 11 are selectable by name, and the new ones are included in the X11
and Wayland suites.

**Controls:** the new sliders and combo boxes appear in the CRT panel, and wheel
scrolling works over them. Checked with real mouse-wheel events on X11.

**Cost on this software renderer** (4:3 SD, playing):

| Preset | Average paint time |
|---|---|
| Consumer Television | 19 ms |
| Worn VHS TV | 33 ms |
| LaserDisc with Laser Rot | 31 ms |

About 25 extra texture reads per pixel are expected with every effect on. The GPU cost
was **not measured**, because there is no GPU in this environment.

**Found and fixed during this round:**

1. **Composite effects were invisible at first.** They were applied only within a few
   pixels of edges. I replaced them with a demodulation model (carrier-multiplied luma
   detail, and chroma with its lower bandwidth).
2. **VGA double-scan had an inverted beam profile** and showed mostly dark lines. After
   fixing that, gaps still dropped out at about 3 px per line, so the gap is now held at
   least about 1 px wide.
3. **Dot crawl only flickered in place**, alternating between 2 states. It now travels
   through a 4-field sequence.
4. **Wheel scrolling over the settings panel changed slider values** instead of
   scrolling. Real input showed curvature silently moving from 0.150 to 0.133; the
   scripted tests could not have caught this.
5. **The "Composite & LaserDisc" group title rendered as "Composite _LaserDisc"**,
   because Qt treats `&` as a keyboard-shortcut marker.

**Not tested in 1.1:**

- how natural the effects look in motion on real footage (the checks above are on test
  patterns);
- GPU performance;
- the interlaced style's look at real refresh rates (only its field alternation was
  measured).

## 2.10: GIFs up to 4K, the Sega CD FMV look, Cable TV

### GIF clips at any size up to 4K

2.9 played the section and grabbed the window, so a GIF could be no sharper than the
window and no smoother than the computer could grab. 2.10 replaces that
(`src/edit/GifRecorder`):

- **Stepping, not playing.** The player pauses, seeks exactly to A, and steps forward one
  video frame at a time. One frame is taken for every 1/fps of video time, so the frame
  count is exact on any machine; a slow one only takes longer.
- **Each frame is drawn at the GIF's size,** off screen: the CRT pass runs again for a
  picture of that size (`VideoWidget::renderPictureAt`), or the desk scene is rendered
  into a framebuffer of that size (`DeskView::grabScene`). Nothing is enlarged.
- **Sizes:** 320 to 1024 wide, and the HD frames (1280×720, 1920×1080, 2560×1440,
  3840×2160), which the picture is fitted inside.
- **Timing:** each frame's delay is in GIF hundredths, handed out so the rounding never
  drifts: the GIF plays exactly as long as the section.
- **Memory:** frames wait for the encoder (worker threads); when more than 8 are
  waiting, stepping pauses until it catches up.
- **Afterwards** the player goes back to where it was (position, paused or playing, A–B).

| Check (`tests/automation/edit.txt`, `scripts/check-edit.py`) | Result |
|---|---|
| A–B (2–5 s) at 480 wide, 15 frames a second | PASS: 480×360, 45 frames exactly, plays 3.00 s |
| Original picture, 320 wide, 10 frames a second | PASS: 320×240, 30 frames, 3.00 s |
| With the look the GIF shows the CRT; without, the plain picture | PASS: 4 dark (curved) corners vs 0; texture 6.4 vs 0.0 |
| Without A–B: the next 5 seconds | PASS: from 10000 ms, 75 frames, 5.00 s |
| Full HD: a 4:3 picture is 1440×1080 | PASS: 10 frames, 1.00 s, 3.4 MB |
| 4K: a 4:3 picture is 2880×2160 | PASS: 10 frames, 1.00 s, 12.7 MB |
| The 4K GIF is drawn at 4K, not enlarged | PASS: 476 scanlines counted (4.5 pixels each): the tube shows all of the video's 480 lines. At Full HD, where they would not fit, it shows 239 |
| Afterwards the player is as it was | PASS: paused, same look, A–B 2000–3000 ms |
| 4K: a 16:9 video fills 3840×2160 | PASS: 10 frames, 12.6 MB |
| Desk mode: the whole scene | PASS: 480×270, 30 frames, 2.00 s |
| Desk mode in HD: drawn at the GIF's size | PASS: 1280×720, 20 frames; fine detail 1.24 against 0.66 for the small GIF enlarged |

The scanline count is the evidence that 4K is real: it is read from the GIF itself (the
strongest fine ripple in brightness down a strip of the picture).

**By hand:** a 5-second 4K GIF at 15 frames a second: 75 frames, 250 MB, made in 113 s
with software OpenGL on 2 cores; the player's memory peaked at 501 MB.

### The Sega CD FMV look

What was built (`src/render/FmvPalette`, `shaders/fmv_codec.frag`, `fmv_palette.frag`,
`CrtRenderer::updateFmv`):

1. **The console's screen:** the video is averaged down to 256×224 (340×224 for 16:9;
   the pixel shape stays 8:7). With *Video window* below 1 the video takes the middle
   and the rest is black.
2. **A codec pass in 4×4 blocks,** in the manner of Cinepak. A block with little detail
   keeps one colour and four brightness values (one per 2×2 quarter); a detailed block
   keeps every pixel's brightness and one colour per quarter. A block that differs
   little from the frame before is left as it was. A full frame is sent at the start,
   after a jump, and every 2 seconds.
3. **A palette for each frame:** the frame is read back and reduced to at most *Colours*
   (64) by median cut, on the console's own grid of 512 colours (3 bits a channel, at the
   Mega Drive's output levels 0, 52, 87, 116, 144, 172, 206, 255).
4. **Ordered 4×4 dither** to the nearest palette colour.
5. **Frame hold:** the picture changes *Frame rate* times a second (15), counted in
   video time, while the video and its sound run on.
6. The result goes through the usual composite and tube stages.

**The sound** (`TapeAudio`, parameter *Console PCM*): samples are held (down to
11.025 kHz at full) and rounded to fewer bits (down to 8), with no smoothing. At 0 the
sound is untouched.

This is a look, not an emulation of the real codec: it works from the same ideas (4×4
blocks, colour kept coarser than brightness, skipped blocks, a small per-frame palette).
Its output was checked for the properties below; it was not compared with captures of
real games.

| Check (`tests/automation/console.txt`, `scripts/check-console.py`) | Result |
|---|---|
| A 4:3 video becomes the console's screen | PASS: 256 × 224 |
| At most 64 colours on screen | PASS: 64 in the frame |
| Every colour is one of the console's 512 | PASS: only the 8 output levels appear |
| The picture changes no more than 15 times a second | PASS: 21 codec frames in 3.0 s of video |
| Set to 5 frames a second | PASS: 17 codec frames in 3.4 s (5.0/s) while 24 video frames were shown |
| The codec flattens blocks | PASS: 91% of 4×4 blocks hold one or two colours at full strength, 51% with the codec off |
| Blocks that hardly change are left as they were | PASS: 51% of blocks identical in the next frame, 28% with the codec off |
| The colour count follows the setting (16) | PASS: 16 colours |
| The small-window look | PASS: window 160 × 136 at (48, 44) on the 256 × 224 screen |
| The look brings the console's sound, a clean look takes it away | PASS: crush 0.85 / 0.00 |
| A 16:9 video keeps the console's pixel shape | PASS: 340 × 224, 64 colours |
| It goes on through the TV | PASS: the filtered screenshot shows the tube |

Unit tests: `tests/test_fmv.cpp` (the screen grid for 4:3 and 16:9, the window; the
palette: about as many colours as asked, all on the 512-colour grid, five colours stay
five, a small red detail on a big blue background keeps a red of its own) and
`tests/test_tape.cpp` (the PCM stage: at 0 the sound is bit-exact; at full, samples are
held at about 11 kHz on 256 levels).

### Cable TV

- **The schedule** (`src/tv/TvSchedule`, pure and unit-tested): a channel is a list of
  programmes with their lengths, optionally shuffled, optionally with a bumper after
  every programme. One round of the list repeats for ever, counted from 1 January 2000
  with a per-channel offset. *What is on now* is arithmetic on the wall clock: nothing
  runs in the background, and a channel "carries on" while the player is closed.
  Shuffled channels use a new order each round, fixed by the channel's seed and the
  round's number.
- **The controller** (`src/tv/TvController`): channels, scanning (video lengths are read
  on a worker thread and cached by file size and date in `channels.json`), tuning, and
  the on-screen graphics, painted into an image that the CRT shader lays over the
  picture.
- **Following the schedule:** tuning starts the programme where the broadcast is, plus
  the time files take to open here (measured each time, from the request to the third
  frame on screen). When a programme ends, the next in the schedule follows: a bumper
  plays whole; a programme starts from its top when it is under 3 seconds late and
  otherwise that much in. A bumper whose time is already up is skipped. More than 8
  seconds out (after a long pause), the channel is joined afresh.
- **What it leaves alone:** the playlist, resume positions, and Jellyfin playback
  reports.

| Check (`tests/automation/tv.txt`, `tv-restore.txt`, `scripts/check-tv.py`) | Result |
|---|---|
| Both scripts ran without a failed step | PASS: all ok |
| A folder becomes a channel: its videos and its subfolders', nothing else | PASS: channel 2: 3 programmes, one round is 65.0 s (20 + 30 + 15) |
| Bumpers are read from their folder | PASS: channel 5: 3 programmes, 2 bumpers |
| A Jellyfin folder becomes a channel with its episodes | PASS: channel 3 "Shows": 2 programmes, 20 s |
| An empty folder is a channel with nothing on | PASS: channel 9: 0 programmes |
| Turning the TV on tunes the first channel, with static until the picture arrives | PASS: channel 2, static |
| It comes on partway through, where the broadcast is | PASS: "Another Film": the broadcast is 19.9 s in, the player 19.2 s |
| The channel number and what is on show for a few seconds after tuning, then go | PASS: just after tuning: "CH 02 MOVIES", "Another Film", "NEXT  7:13 AM  Tall Story"; later the number has gone |
| Channel up | PASS: the Jellyfin channel plays, in step with its schedule: "Test Show · S01E01 · Pilot": the broadcast is 7.4 s in, the player 7.0 s |
| Programmes follow one another on their own, as scheduled | PASS: Toon Episode 1 → Toon Episode 1 → Toon Episode 1 → ident-b → Toon Episode 10 (5 of 5 looks in step, 0 during a change-over) |
| Bumpers play between programmes | PASS: 1 bumper(s), 4 programmes played so far |
| W shows the guide over the lower part of the picture, through the tube | PASS: 98% blue in the lower part with the guide, 0% without; the video stays on above (21% blue) |
| Number keys tune a channel, and it has moved on meanwhile | PASS: "Big Movie 1994": the broadcast is 17.4 s in, the player 17.5 s; 42 s after the first visit |
| An empty channel shows static and says so, with the channel number readable over the snow | PASS: "NO PROGRAMMES", static; green lettering in 5.1% of the top-right corner, 0.00% green elsewhere |
| A number with no channel leaves the TV where it was | PASS: "CH 07 NOT IN USE", still channel 9 |
| Leaving for the empty channel while a programme is still opening | PASS: nothing starts playing behind the static: idle, static, "NO PROGRAMMES" |
| Desk mode | PASS: channels change on the set, with the overlay drawn: channel 3, overlay on, 7% of the desk view lit by the set |
| TV off | PASS: the programme stops; no overlay, no static: idle; overlay: False, static: none |
| TV mode adds nothing to the playlist | PASS: playlist empty throughout |
| TV mode saves no resume positions | PASS: no TV programme in the resume file |
| Jellyfin | PASS: the channel's episodes are listed in one recursive query: 1 request(s), ParentId lib-shows |
| Jellyfin | PASS: TV mode reports no playback (resume points and "watched" marks stay as they are): 5 stream requests, 0 playback reports |
| Jellyfin | PASS: streams still carry the token in the header only: 5 requests |
| The channels are stored without any token | PASS: …/CRTPlayer/CRTPlayer/channels.json |
| The channels are back on the next launch, without reading the videos again | PASS: channels [2, 3, 5, 9], ready after 48 ms |
| TV on again goes to the channel last watched | PASS: channel 3; "Test Show · S01E01 · Pilot": the broadcast is 0.8 s in, the player 0.0 s |
| Opening a file of your own turns the TV off | PASS: TV on: False; playing sd_4x3_h264.mp4 |

Unit tests (`tests/test_tv.cpp`, 17 checks): the round's length; one programme at any
moment, with the right offset, and the same answer when asked again; programmes back to
back, in order, wrapping from the last to the first; shuffled rounds hold every
programme once, differ from round to round, repeat exactly when asked again, and never
show the same programme twice in a row; bumpers alternate with programmes; the guide's
window; an empty channel.

**Faults found by the checks, and fixed:**

- **The last picture's glow tinted the static.** With nothing on, the snow showed the
  previous channel's colours faintly. Static now has no picture glow, and the set's own
  lettering is drawn over the snow.
- **The open time was measured too early** (when the file was opened, not when its
  picture arrived), so the player ran 1 to 3 seconds behind the schedule and short
  bumpers were skipped. It is now measured to the third frame on screen.
- **Leaving for an empty channel while a programme was still opening:** the programme
  started playing behind the static a moment later. The player is now closed, and a
  Jellyfin stream still being asked for is dropped. The same when the TV is turned off.
- **An end-of-file reported twice** restarted the next programme. Only the first end of
  the programme that was asked for counts.
- **The channel number could be gone before the picture arrived** on a slow start; it
  now stays for 3 seconds from the first picture.

**Full suites for 2.10.0:** native X11 (327 checks) and Wayland (18), and the same on
the AppImage with the system's Qt removed (327 and 18). No failures. Results:
`docs/results/edit-checks.txt`, `console-checks.txt`, `tv-checks.txt`, and the
`-appimage` versions.

**Known limits and what was not tested:**

- **Speed.** Everything here ran with software OpenGL on 2 cores, where a file takes
  about 2 seconds to open. On a real GPU the TV should follow its schedule within a
  fraction of a second, and big GIFs should take a fraction of the time; neither is
  measured here.
- **Jellyfin channels** were tested against the mock server only, with original files.
  A programme the server has to convert starts at the right place only if the server
  honours the start time; that is not tested.
- **Big libraries.** Reading lengths takes a moment per file the first time (a handful
  of files here). Thousands of files, and network shares, are not tested.
- **The guide** was checked with four channels. Its scrolling, when there are more
  channels than fit, is not tested.
- **Sega CD look:** not compared with real captures, as said above. Each codec frame
  costs one read-back of the 256×224 picture for its palette; on a real GPU that is
  expected to be small, and is not measured.
- **Seeking and pausing in TV mode** work, and the channel rejoins its schedule at the
  next programme. Tried by hand only: after a 6-second pause the player was 8.0 s behind
  its channel, and 0.9 s behind once the next programme had started.
- **A 30-second 4K GIF** (the longest allowed) was not made; from the 5-second one it
  would be over a gigabyte.

## 2.9: cutting without re-encoding, and GIF clips

**Cutting** (`src/edit/LosslessCutter`, the **X** dialog `ui/CutDialog`):

- **Pipeline:** `filesrc ! parsebin ! queue ! <muxer> ! filesink`. parsebin demuxes and
  parses but never decodes, so packets go into the new file as they are.
- **Two passes:**
  1. A quick pass finds the keyframe at or before A (a `KEY_UNIT | SNAP_BEFORE` seek).
  2. The copy pass seeks the same way. Packets are thrown away until every track has
     announced its format and the seek's flush has come by, so the muxer sees nothing
     from before the keyframe. The section's start is moved to the keyframe: some
     demuxers start it at A, and muxers would then drop the frames between.
- **The end** is cut in decode order: video packets pass while decoded at or before B,
  plus the B-frames shown before the last kept frame. So every frame up to B, and every
  frame it refers to, is kept, and the picture has no gap. For AVI, whose packets carry
  no display times, B-frames are recognised from the bitstream (H.264 slice type,
  MPEG-4 VOP type). Other tracks end by time; a subtitle still showing at B ends at B.
- **The container** is the source's own when GStreamer can write it, otherwise Matroska.
  - H.264/HEVC is re-framed by a parser where the container needs another form
    (MPEG-TS, AVI).
  - Matroska's SRT text is un-escaped back to plain text.
- **Never overwriting:**
  - the cut is written as a hidden `.part` file and renamed when complete, to a name not
    yet taken (*(2)*, *(3)*…);
  - a cut onto the source or an existing file is refused;
  - whether the folder can be written is found by really creating a file there, and
    otherwise the cut goes to `~/Videos`.
- **Keyframe stepping** (**Shift+←/→**) looks the keyframe up on the file with a
  demuxer of its own, then seeks the player there exactly.

**GIF clips** (`src/edit/GifEncoder`, `GifRecorder`, the **G** dialog `ui/GifDialog`):

- **Recording:** the section is played once and grabbed as shown: the filtered frame
  cropped to the picture, the original frame, or the desk view's framebuffer. Each frame
  is given the time the video really took to reach the next one, so the GIF runs at the
  true speed when grabbing falls behind.
- **Encoding:** the player's own GIF89a writer. Per-frame 256-colour palettes (median
  cut over a 15-bit histogram), Floyd–Steinberg dithering at ¾ strength, LZW. Frames are
  encoded on worker threads and written in order.
- **Shrinking:** frames are softened in proportion to the reduction first. Without that,
  the scanlines and shadow mask alias into moiré rings (seen in the first test GIF).
- **Never overwriting:** the file is opened with "new only".

**Unit test** (`tests/test_gif.cpp`, read back with Qt's GIF reader):

| Check | Result |
|---|---|
| 256-colour noise frames decode pixel for pixel (LZW, with dictionary resets) | PASS |
| A smooth gradient stays close (4×4 averages within 2 of 255) | PASS |
| Frame delays, frame count | PASS |
| Refuses to write over an existing file | PASS |

**Format matrix** (`build/cut_tool` + `scripts/check-copy.py`, with ffmpeg as an
independent reference; `docs/results/cut-formats.txt`). Three sections (3.3–7.7 s,
0–3 s, 12 s to the end) of each file. "Lossless" means every decoded video frame of the
cut is bit-identical to a contiguous run of the source's, and every audio packet is
byte-identical and contiguous.

| Source | Result |
|---|---|
| H.264 + AAC in MP4, MOV | PASS: lossless, same container |
| H.264 + 2× AAC + 2× SRT in Matroska; with chapters | PASS: lossless, all five tracks kept |
| VP9 + Opus in WebM | PASS |
| MPEG-2 + AC-3 in Matroska | PASS |
| HEVC in MP4 | PASS |
| Theora + Vorbis in Ogg | PASS |
| H.264 + AAC in MPEG-TS | PASS (re-framed to byte-stream by the parser; decoded frames identical) |
| H.264 + AAC in AVI; Xvid + MP3 in AVI (B-frames, no display times) | PASS |

36 of 36 cuts passed.

The MPEG-2 clip is a still picture (9 distinct frames, repeating every 12), so for it the
comparison proves the frames are identical but not which 12-frame group they came from;
its frame count (156 for 0.80–6.00 s in the player run) confirms the range.

**In the player** (`tests/automation/edit.txt`, `scripts/check-edit.py`,
`docs/results/edit-checks.txt`), on copies of the media:

| Check | Result |
|---|---|
| The original files are unchanged (SHA-256 before and after) | PASS: 5 files identical |
| No unfinished `.part` files are left behind | PASS |
| Shift+→ / Shift+← step between keyframes (3.3 s → 4 → 2 → 0) | PASS: 4000, 2000, 0 ms |
| A–B (3.3–7.7 s) is cut from the keyframe at or before A | PASS: starts at 2000 ms |
| The cut is the original, frame for frame | PASS: source frames 60–233, audio packets 245/245 |
| It ends at B | PASS: last frame 233 = 7.77 s (B = frame 231, plus two B-frames' reference) |
| Cutting the same section again makes a new file | PASS: *… (2).mp4* |
| Matroska: every track kept; subtitles timed from the cut and ending at B | PASS: 5 streams; file 8.52 s for an 8.50 s section |
| WebM and MPEG-2/AC-3: lossless, in their own container | PASS |
| A video in a read-only folder is cut into `~/Videos` | PASS |
| A–B (2–5 s) as an animated, looping GIF at the chosen size | PASS: 480×358, plays 3.0 s |
| Original picture option: 320 wide, 10 frames a second | PASS: 320×240, 29 frames |
| With the look the GIF shows the CRT; without, the plain picture | PASS: 4 dark (curved) corners vs 0; texture 1.2 vs 0.0 |
| Without A–B: the next 5 seconds | PASS: plays 4.9 s |
| In desk mode, the whole scene is recorded | PASS: 480×270 |

**Full suites for 2.9.0:** native X11 (281 checks) and Wayland (18), and the same on the
AppImage with the system's Qt removed. No failures. The cut and GIF checks are in
`docs/results/edit-checks.txt` and `edit-checks-appimage.txt`.

**A checker fault found on the way:** `check-copy.py` at first skipped the video of the
MPEG-2 cut (it misread ffprobe's stream list), so that row passed on its audio alone.
The checker now fails when a track of the source has no counterpart in the cut. The
saved results are from the corrected checker, re-run on the same cuts.

**Known limits:**

- **Grab rate here:** with software OpenGL this machine grabs about 9 filtered frames
  a second, so the 15 fps GIF holds 27 frames, at the right speed. A real GPU should
  reach the chosen rate; that is not measured here.
- **Subtitles at the start:** a subtitle already on screen at the keyframe is not
  carried into the cut.
- **Not tested:** files with open-GOP or broken-link keyframes, DVD/PGS picture
  subtitles in a cut, very large files (the copy is linear in the section's size), and
  network shares.

## 2.8: Jellyfin: big libraries and conversion on the server

**Big libraries:**

- **Before:** a folder showed at most its first 500 items.
- **Now:** folders load 100 at a time (`StartIndex`, `Limit`, `TotalRecordCount`). The next
  page is requested when the list is scrolled within two rows of its end, or when the
  loaded items don't fill the list yet. The heading shows *Movies — 300 of 1,234*.
- A stale page is ignored: one for a folder you have left, or one already loaded.

**Original file or converted** (`JellyfinClient::requestPlayback`):

- **Asking the server:** every Jellyfin play first posts `/Items/{id}/PlaybackInfo` with a
  device profile.
  - The profile's direct-play list is what this computer's GStreamer can open and decode
    (`Player::localFormats`: demuxers present, and decoders accepting each codec's caps).
  - Its transcoding profile is H.264 + AAC in HLS.
  - It also carries the *Quality* limit.
- **The server's answer decides:**
  - *SupportsDirectPlay* → the original file, as before.
  - Otherwise its `TranscodingUrl` (HLS). Its playlists and segments carry `api_key`,
    because GStreamer's HLS reader fetches segments without the player's headers.
  - Reasons are read from the source (10.9+) or the URL (10.8).
- **Fallbacks:**
  - If an original fails to play here, the player asks again once with direct play
    turned off, from the same position.
  - If PlaybackInfo itself fails (an old or unusual server), the original file plays as
    before.
- **Reporting:** start, progress and stop use the server's play session and
  `PlayMethod: Transcode`. The conversion is ended (`DELETE /Videos/ActiveEncodings`) when
  playback moves on, including for a conversion that never started playing.
- **Subtitles:** a converted video's embedded text subtitles are offered as files the
  server extracts.
- **Where it shows:** the technical info overlay and the report show which method is in
  use and why. The address is never shown or logged.

**The mock server** (`tests/jellyfin_mock.py`) now pages listings and makes a
PlaybackInfo decision from the posted profile. Items declare their format:

- *Studio Master* claims ProRes;
- *Multitrack HD* claims 25 Mbit/s;
- *Damaged Upload* serves a broken file, with a good one for the server to convert.

Conversions are real HLS made with ffmpeg (H.264/AAC, 2-second segments). The mock
refuses them without the `api_key`. A *Big Library* holds 1,234 items. The new script
`tests/automation/jellyfin-transcode.txt` runs against 10.8 and 10.10 APIs, after the
existing two.

| Check (both 10.8.13 and 10.10.3) | Result |
|---|---|
| A big library opens with its first page only | PASS: 100 shown of 1,234 |
| Scrolling loads the rest a page at a time, each page once | PASS: 1,234 items from 13 requests (StartIndex 0..1200, Limit 100) |
| The last item plays (beyond the old 500 limit) | PASS: *Clip 1234* |
| Every play asks the server how, with this computer's formats | PASS: 10 PlaybackInfo requests; H.264, HEVC, VP8/9, AV1, MPEG-2/4, VC-1… (no ProRes) |
| A codec this computer can't decode is converted | PASS: Transcode, `VideoCodecNotSupported` |
| The conversion streams as HLS segments, authenticated by their address | PASS: ~50 segments, all 200 |
| Seeking works in a converted video | PASS: 15.1 s after seeking to 12 s |
| An original that fails here is retried converted, once | PASS: "the original didn't play here"; one forced request; no error dialog |
| Over the quality limit is converted; the limit reaches the server | PASS: `ContainerBitrateExceedsLimit`, MaxStreamingBitrate 4,000,000 |
| A converted video resumes at its saved position | PASS: 16–18 s after its 2 s wait (saved 12 s) |
| Its embedded subtitle is offered as a file from the server | PASS: *English (embedded)* |
| *Play converted by the server* converts a playable file; back at *Original file* it plays directly | PASS |
| Converted playback is reported as such, with the server's play session | PASS: 4 `Transcode` start reports |
| Each conversion is ended on the server | PASS: 4 ended for 4 |
| The token: only in HLS and its subtitle addresses, never on original streams, never in the logs | PASS |

The earlier Jellyfin checks still pass (40 checks per server version; `docs/results/jellyfin-10.8-checks.txt`,
`jellyfin-10.10-checks.txt`). The header check now leaves out HLS playlists and segments, for the reason above.
The full suites passed for 2.8.0 with no failures: native X11 (261) and Wayland (18), and the same on the AppImage
with the system's Qt removed.

**Not tested here:** a real Jellyfin server's ffmpeg conversions, hardware transcoding
on the server, and conversions over a slow link. The mock follows the documented API
and real HLS, but it is not Jellyfin.

## 2.7: Windows 10 and 11 (preview)

**What it is:** the same player built for 64-bit Windows, as a self-contained folder
(`CRT_Player-2.7.0-windows-x64.zip`).

- **Built** on GitHub's Windows Server machines with MSYS2 (UCRT64: Qt 6, GStreamer 1.28)
  by `.github/workflows/windows.yml`.
- **Packaged** by `packaging/windows/package.sh`:
  - `windeployqt` for Qt;
  - 219 GStreamer plugins, the plugin scanner and GIO's TLS module;
  - every library they need, found by following `ldd` until nothing new turns up;
  - SDL2 for game controllers.
- **Bundled GStreamer:** on start the player points GStreamer at its own folder, so an
  installed GStreamer (or none) makes no difference (`main.cpp`,
  `useBundledGStreamer`).
- **Platform parts:**
  - keeping the screen awake uses `SetThreadExecutionState`;
  - SDL is loaded with `LoadLibrary`;
  - MPRIS and D-Bus are left out;
  - the install hints say to reinstall CRT Player rather than name a Linux package.
- **Graceful failures, found by the test machine:**
  - **OpenGL below 3.3** (Windows' own 1.1 renderer, used when no graphics driver is
    installed) crashed the player. Both renderers now check the version first and the
    player shows *"OpenGL 3.3 is required"* with what the system offers.
  - **No sound device** stopped playback with an error from Windows' audio sinks, which
    open without a device and fail only once playing starts. The player now asks
    GStreamer's device monitor for outputs and, with none, plays silently but still
    timed. The Linux version also checks that the automatic output can open.

**The smoke test** (`packaging/windows/smoke-test.sh`, `smoke.txt`, `check-smoke.py`)
runs the packaged folder on its own:

- **Isolation:** with only `C:\Windows\System32` on `PATH`, so nothing from MSYS2 can
  stand in for a missing file.
- **Graphics:** Mesa's CPU renderer (llvmpipe), supplied as Qt's software OpenGL
  (`opengl32sw.dll`), since the machine has no graphics card. This is for the test only;
  it is not shipped.
- **If it fails:** a crash is re-run under gdb for a backtrace. The results reach this
  report through the run's annotations.

Results (run 36353664992, commit b30b82c; `docs/results/windows-smoke-checks.txt`):

| Check | Result |
|---|---|
| The packaged player starts on its own | PASS: `CRTPlayer 2.7.0` |
| Its own GStreamer is complete | PASS: GStreamer 1.28.7, nothing missing, "Everything for common formats is installed" |
| It plays (H.264 video, Vorbis audio) | PASS: playing at 4.8 s, `avdec_h264`, `vorbisdec`, audio output `none` (no sound device) |
| The picture is drawn with the CRT look | PASS: mean 108, contrast 120 |
| A subtitle file is drawn (text plugin bundled) | PASS: 22.3% white pixels in the bottom quarter |
| Keep awake while playing / released when paused | PASS: `SetThreadExecutionState` / `none` |
| The look's sound switches on | PASS |
| Desk mode draws the arcade cabinet | PASS: 13% of the view drawn |

**Linux is unaffected:** the full suites passed again for 2.7.0 (native X11 261 checks,
native Wayland 18, and the same on the AppImage with the system's Qt removed), with no
failures.

**Not tested here** (for a real Windows PC):

- a real graphics driver;
- Direct3D 11 hardware decoding (the test machine registers no hardware decoders);
- real audio output;
- game controllers;
- high-DPI scaling;
- fullscreen on multiple monitors;
- the "unrecognised app" warning.

## 2.6: the arcade cabinet

**What it is:** set 6 (`DeskRenderer::buildArcade`), an upright cabinet built in the
upright frame like the stands, so a pivoted (portrait) picture turns only the monitor.

- **The body:** the side panels are the cabinet's silhouette, ear-clipped and extruded,
  with T-molding on their edges.
- **The front, between them:**
  - kick plate, lower front and coin door;
  - the control panel, with joysticks (base, chrome shaft, ball top), six buttons each,
    and a start button;
  - the black glass bezel with the screen tunnel;
  - the speaker panel, the backlit marquee, and the top and back.
- **Players:** two on a wide cabinet, one on a narrow (vertical-game) one.
- **Materials** (`desk.frag`, `arcadeSurface`), in four art styles (space, sunset, neon,
  70s woodgrain):
  - printed side art and control-panel overlay;
  - an emissive marquee with the title composited over the art;
  - lit buttons and coin returns, which glow whatever the room's light.
- **The marquee's title** is the video's title, tidied (`DeskView::marqueeTitle`): words
  split on dots and underscores, and everything from the year or the first quality or
  source tag dropped. It is drawn by QPainter (heavy italic capitals, glow, outline,
  gradient) into a texture.
- **In the scenes:** the floor is always at the cabinet's base.
  - The wall scene stands it against the wall, with no mount.
  - The 90s CG room scales its layout with the set (`layoutScale()`), which is exactly 1
    for every TV-sized set, so those are unchanged.
  - The theater still uses its own screen.
- **Numbering:** the theater's internal screen moved from 6 to 7, so the sets you can
  choose stay numbered 0–6 in the settings file.

**Measurements** (`tests/automation/arcade.txt`, `docs/results/arcade-checks.txt`):

| Check | Result |
|---|---|
| Titles | `Oblivion.2013.1080p.BluRay.x264` → OBLIVION; `The_Matrix_(1999)_720p` → THE MATRIX; `Some.Show.S01E02.HDTV.XviD` → SOME SHOW; `sd_4x3_h264` → SD 4X3; `Street Fighter II` unchanged; none → CRT PLAYER |
| The fly-in lands on the exact fullscreen frame | max diff 0 |
| Upright (front view, on screen) | 775 × 1050 px; the screen in the upper half |
| Four art styles | the least different pair differs over 5.8 % of the view |
| Wall, desk and CG scenes | standing on the floor (0.00 below it), drawn as the arcade cabinet |
| Theater | its own screen |
| Portrait video | screen 198 × 351 px inside an upright cabinet of 594 × 1012 px |
| The marquee follows the video | 19 % of the lettering texture covered; the next video reads VERTICAL 9X16 |
| Remembered | the arcade cabinet and its art after leaving desk mode |

### Found and fixed while building it

1. **The space art's ringed planet** was set up but never drawn.
2. **The CG room with a set this big:** the camera, pulled back to frame a tall cabinet,
   ended up inside the background crowd's ring. A huge checked cube filled the view, and
   the foreground objects crowded the cabinet. The layout now scales with the set; for
   TV-sized sets the scale is exactly 1.
3. **Test mistakes:**
   - the title command dropped the title's first word;
   - the checker looked for a log field that doesn't exist;
   - the "upright" threshold was too strict: seen close, the control panel juts towards
     the camera and widens the outline.

### A small finding

With the default look (*Consumer Television*) and the 4:3 test video, the fly-in's
landing differs from the fullscreen image by 3 steps of 255 on a handful of pixels (19
for the CRT set, 3 for the arcade cabinet) at colour-bar edges. With a clean look it is
exact (max diff 0), as every landing check uses. This concerns all sets equally, not the
arcade cabinet.

### Not tested

- GPU speed of the cabinet. Counted from the code it is 6,770 triangles: 4,704 for the
  screen grid (as on every set), 1,908 for two players' controls, 156 for the body and 2
  for the shadow. That is light next to the scenes' per-pixel shading.
- How the art styles look on a real display (judged from renders).

## 2.5.3: no orbit while scrubbing (from real use); subtitle plugin notice

### The demo-reel orbit

**The report:** in the 90s CG room the scene rotated while scrubbing. It should only
rotate after being paused for more than 10 seconds.

**The cause:** the idle orbit ran whenever playback wasn't running, and every seek briefly
stops it.

**The fix:** the orbit waits until playback has been stopped and untouched for 10 s
(`DeskView`, an idle timer).

- Every seek restarts the count: the player reports each finished seek, whether from the
  timeline, the keys or the automation.
- Holding the timeline stops it at once.
- While playing it never runs.

**Measurements** (`docs/results/cg-checks.txt`):

| Check | Result |
|---|---|
| Paused briefly (5.0 s idle, as the player measured it) | +0.0° |
| Scrubbing while paused (23 seeks, then 0.6 s idle) | +0.0° |
| Playing | +0.0° |
| Stopped and untouched for 10 s | +6.0° over the next 10.5 s |

**Found while testing it:**

- **Scripted waits ran behind real time.** Under slow software rendering the script's
  waits ran behind the real clock. The first version of the check assumed a 3 s pause was
  under 10 s, but the player had in fact been idle longer. The check now uses the idle
  time the player itself measured.
- **The automated scrub skipped a step.** It didn't mark the view as being scrubbed, as
  holding the real seek bar does. Now it does.

### The subtitle text plugin

**What happened:** an environment reset gave a fresh test machine without
`gstreamer1.0-x`. On Debian and Ubuntu that package holds GStreamer's pango plugin
(`textoverlay`), which draws subtitle text. The two sidecar-subtitle checks failed:
nothing was drawn.

**What that showed:** the player didn't warn about it. Subtitles would silently not
appear on such a system.

**The fix:** the startup check now lists "subtitles (drawing their text)" when
`textoverlay` is missing, with each family's package:

| Family | Package |
|---|---|
| Debian, Ubuntu | `gstreamer1.0-x` |
| Fedora | `gstreamer1-plugins-base` |
| Arch | `gst-plugins-base` |
| openSUSE | `gstreamer-plugins-base` |

**Checked on that machine:** `--check-gstreamer` listed it, with the right package for
the Ubuntu, Fedora and Arch stand-in `os-release` files. After installing
`gstreamer1.0-x` it no longer did.

### The test runner

- **A crashed checker used to drop out of the count.** On the fresh machine the
  keep-awake checker's D-Bus helper failed to start. The machine's default Python (3.11)
  is not the one Ubuntu's `python3-dbus` is built for (3.12). The checker printed a
  traceback and no verdict, and the run's totals still read "0 failed".
  - Every checker now runs through a wrapper that reports a FAIL when the checker exits
    unsuccessfully without one.
  - The keep-awake checker starts its helper with a Python that can load the D-Bus
    bindings.
- **The release run's keep-awake checks** were run again after the repair: 7 of 7 pass
  for the binary and for the AppImage (`docs/results/inhibit-checks*.txt`).
- **One subtitle check failed once in the AppImage X11 run** of the repaired machine (no
  text in the first captured frame). Two repeat runs of that suite with the AppImage,
  system Qt hidden as in the release run, passed: bottom-third change 1.74, the same as
  the native run, and all polish checks passed. This is a first-use delay on the freshly
  reset machine (cold caches), not a player fault.

## 2.5.2: no tile reveal when skipping (from real use)

**The report:** in the 90s CG room, clicking on the timeline redrew the picture tile by
tile. It should happen only when a video starts or after a long pause, never for
skipping back or forward.

**The cause:** the reveal started whenever playback went from stopped to playing. A seek
briefly drops the pipeline out of *playing* while it flushes and jumps, so every skip
looked like a new start.

**The fix** (`DeskView::setTheaterState`):

- **When it plays:** only for a new video (the player reports each one loaded,
  including the next in a playlist), or when playback resumes after being stopped at
  least 10 s.
- **When it doesn't:** while the timeline is being dragged. A seek's interruption lasts a
  fraction of a second, so skips never count.
- **Entering desk mode or the CG room** while a video plays no longer counts as a start
  either.

**Measurements** (`docs/results/cg-checks.txt`): reveal progress just after each action,
where 1 means no reveal.

| After the first reveal | Result |
|---|---|
| Seek forward (to 20 s) | 1.00 |
| Seek back (to 6 s) | 1.00 |
| Scrub from 8 s to 25 s (40 seeks, 3 s) | 1.00 |
| Pause 2 s, then play | 1.00 |
| Pause 10.5 s, then play | 0.20 (the reveal plays) |

**Also:**

- The CG room suite now takes about 280 s, so the per-suite time limit went from 400 s
  to 600 s.
- An environment reset interrupted the release run after its native X11 stage. The
  remaining stages were run on their own, after checking that the release binary was
  unchanged.

## 2.5.1: the sound effects' volume (from real use)

**The request:** a way to control the volume of the sound effects.

**What it is:** a *Sound* section in *Settings → Playback*, with the on/off switch and two
levels. Both apply on top of any look, live, and are remembered.

- **Noise volume (0–200 %)** scales the loudness of the added noise. It is a gain in the
  sound element applied to the hiss and to the crackle's clicks, so it changes how loud
  they are, not how often they happen.
- **Effect strength (0–100 %)** scales the amounts of the TV speaker, treble loss, wow and
  flutter, saturation and dropouts.

**Unit test** (`docs/results/tape-sound-checks.txt`):

| Check | Result |
|---|---|
| Hiss at 200 % noise volume | +6.0 dB (twice the amplitude) |
| Hiss at 0 % | silent (peak 0) |
| Crackle at 50 % | the loudest pop 0.197, against 0.394 at 100 % |

**In the player** (`docs/results/sound-checks.txt`), with *Worn VHS TV*:

| Check | Result |
|---|---|
| Noise volume 200 % | noise gain 2.00; the hiss (0.55) and speaker (0.65) amounts unchanged |
| Noise volume 0 % | noise gain 0.00 |
| Effect strength 50 % | wow 0.450 → 0.225, speaker 0.650 → 0.325; treble loss and saturation halved; noise gain unchanged |

**Checked by hand:**

- Both levels survive quitting and reopening: 150 % and 40 % were restored, and 0.65 ×
  0.4 = 0.26 was sent for the speaker.
- The panel was looked at. The sound controls first sat under the *Screenshots* heading;
  they now have their own *Sound* section.

## 2.5: the look's sound

**What it is:** `crttape`, a GStreamer audio filter inside the player
(`src/playback/TapeAudio.cpp`). It works on 32-bit float audio at any rate and channel
count, so it needs no extra plugins. It sits in the audio chain after `scaletempo`:
`scaletempo ! audioconvert ! crttape ! audioconvert`.

Its stages, in the order of a VHS deck played through a TV:

1. **Wow and flutter:** a delay line read at a wobbling position (0.55 Hz wow, 7.3 Hz
   flutter, random drift).
2. **Treble loss:** a low-pass filter.
3. **Hiss:** high-passed white noise.
4. **Saturation:** a tanh curve.
5. **Dropout dips:** driven by the look's VHS dropouts.
6. **Crackle:** decaying clicks and ticks.
7. **The small speaker:** high-pass, a 2.4 kHz bump and low-pass (RBJ biquads), then
   mono.

With every amount at 0, buffers are not touched. The amounts are six new look settings
(*Sound*). The VHS, TV, arcade and film presets set them; the clean presets don't. A
master switch in *Playback* turns them off.

**Tape sound unit test** (`docs/results/tape-sound-checks.txt`): generated audio through
the element, measured.

| Check | Result |
|---|---|
| Every amount at 0 | bit-exact (96,000 samples) |
| Hiss on silence | −31.4 dBFS at full, −41.8 dBFS at 0.3 |
| TV speaker | 100 Hz −18.9 dB, 1 kHz +0.9 dB, 10 kHz −13.1 dB; mono (L/R identical) |
| Treble loss | 1 kHz −0.0 dB, 10 kHz −19.3 dB |
| Wow and flutter (1 kHz tone) | pitch spread 2.09 % (0.000 % without) |
| Saturation (0.9 amplitude tone) | 3rd harmonic 14 dB below the tone |
| Crackle on silence | 27 pops in 3 s |
| Dropouts | level dips occur |

**In the player** (`docs/results/sound-checks.txt`):

- Clean looks leave the sound alone.
- The VHS and film looks switch it on, and playback runs on normally.
- The master switch turns it off.
- The no-good-plugins suite still passes with the new chain (`audioconvert` is a base
  plugin).

### Found and fixed while building it

1. **Wow and flutter at full spread the pitch by 4.9 %,** a seasick warble; a worn tape
   is about 1–2 %. Reduced to 2.1 % at full.
2. **Build mistakes:** a missing include; a function defined in a header where its
   panel's type was not known; stray access markers that made an automation-used
   function private.
3. **A pointer left behind:** the player keeps a pointer to the element, which belongs to
   the pipeline. It is now cleared whenever the pipeline is torn down.

### Not tested

- How it sounds: there are no speakers here. The measurements are above; the presets'
  amounts are judgement.
- The dropout rate. One dip was seen in 5.5 s at full; the expected number is about
  five, so the rate may be lower than intended.

**Release run note:** an environment reset killed the first release run. It was run again
from the start and completed.

## 2.4.1: walking figures and a background crowd (from real use)

**The report:** the mannequins stood side by side, hovering with a walking animation.
They should walk around in the background, and there should always be a mixture of
shapes beyond the default set, in the background as well as around the set.

**The causes:**

- **The hovering:** the walk swung the legs from hips at a fixed height, so at the ends
  of each stride both feet left the floor. The figures also walked on the spot.
- **No mixture:** only the chosen object set was drawn.

**Fixes:**

- **A walk:**
  - The body rides on the hips, whose height follows the legs, so the planted foot
    stays on the floor. The forward-swinging leg bends at the knee and lifts its foot,
    and the arms swing opposite.
  - The figures walk a circle behind the set (radius 5), facing their direction of
    travel.
  - The walk cycle advances with the distance walked (1.3 units per cycle at 0.42 per
    second), so the feet don't slide.
  - *Wooden mannequins* is now six walkers on that loop.
- **A background crowd**, with every object set including the default: eight larger
  shapes of the other kinds floating in a ring 10–14 away, and two walkers. It's on by
  default, with a switch to turn it off.
- **Layout:** the walkers' path keeps clear of the marble column and the six statue
  places (≥ 0.9 away).

**Found while building it:** the crowd first did not appear with the default set. The
shaped objects were still traced only for the non-default sets. They are now traced
whenever there are any to draw.

**Measurements** (`docs/results/cg-checks.txt`):

| Check | Result |
|---|---|
| The background crowd with the default set | 2.2 % of the view changes |
| The walkers move (0.7 s apart) | 3.52 % of the view changes |
| The same moment again (pinned clock) | max diff 0 |

## 2.4: CG room object sets, your 3D models, round stars (from real use)

### Round stars

**The report:** the deep-space stars looked like squares. Each lit up a whole cell of a
grid across the sky.

**The fix:** now each chosen cell holds one small, round, soft point at a random spot
inside it, of varying size and brightness, twinkling slowly.

**Check:** 56 stars, up to 17 px, median width/height 1.00.

### Object sets

The chrome-and-marble objects stay the default. The other sets are drawn as distance
fields, each marched inside its bounding sphere, with period shading:

- **Toybox:** glossy plastic with white highlights and rims, stripes and checks.
- **Organic:** bump-perturbed normals with noise spots and marbling, glowing translucent
  jelly (the ray continues through it, tinted), and iridescent pearls.
- **Wooden mannequins:** jointed figures built from capsules and ellipsoids, walking in
  place.
- **Mixed:** some of each, plus one chrome sphere.

All of them take part in shadows and reflections.

**Check:** each set changes the scene against the default (6.8%, 7.6%, 5.6%, 6.7% of the view changes).

### Your 3D models

- **Loading** (`src/render/ModelLibrary.cpp`): OBJ (with or without normals; vertex
  colours) and STL (binary or text; Z-up turned to Y-up), on a worker thread. Up to 6
  models of up to 400,000 triangles each. Models are centred, stood on their base and
  scaled to statue size, with smooth normals computed when the file has none.
- **Drawing** (`shaders/model.vert/.frag`): a real mesh on a marble plinth, depth-tested
  against the ray-traced scene.
  - **Shadow:** flattened along the sun onto the floor, with a stencil so each floor pixel
    darkens once.
  - **Floor reflection:** the CG pass leaves alpha 0 on floor pixels, the mirror images
    are added only there, and full opacity is restored afterwards.
- **Finishes:** marble, bronze, chrome (reflecting the palette's sky and floor), candy
  plastic, or their own colours.

**Test models** (`scripts/make-test-models.py`): a bust (OBJ), a twisted column (binary
STL), an obelisk (text STL), a broken OBJ and a text file.

| Check | Result |
|---|---|
| Loading | 3 shown; `d_broken.obj` skipped ("face refers to a missing vertex"); the text file ignored |
| In the scene | 3.7% of the view changes |
| Shadows and mirror images on the floor | 15102 floor pixels darker, 68966 brighter |
| Bronze vs marble | warm pixels 6333 (marble) -> 24821 (bronze) |
| The scene stays opaque (floor mask reset) | 100 % |
| The folder is remembered across leaving desk mode | 3 shown again |

### Found and fixed while building it

1. **The first object sets had problems:**
   - they were too small next to the chrome set;
   - the jelly flower was seen edge-on (a flat smear);
   - the mannequins overlapped in a row pointing away from the camera, and the nearest
     stood in the set's shadow.

   The shapes are now scaled up by a third, the flower is turned to face the viewer, and
   the mannequins stand in a row across the view.
2. **The models' mirror images were too bright:** white marble left glowing streaks in
   the floor. Dimmed.
3. **An automation bug:** the `cg models` command read the folder from the wrong word,
   so no folder was set. Fixed, then a stray comment broke the line and the build; fixed
   again.
4. **`slots` is a reserved word in Qt code,** again (as in 2.1).

### Not tested

- Your models inside the chrome spheres' reflections (not drawn there, by design).
- Models from other tools; very large or unusual files.
- GPU speed.

## 2.3: the 90s CG room

**What it is.** Scene 4 is ray traced in one full-screen pass (`shaders/cg90.frag`):

- **Surfaces:** an infinite plane (mirror checkerboard, box-filtered against flicker; or a
  neon grid), a gradient sky with a sun, stars and a ringed planet.
- **Objects:** analytic spheres, a rotated box (the glass cube), a torus ray-marched
  inside its bounding sphere, capped cylinders and boxes (the column and the stands).
- **The TV** is a box with its picture on the front. It appears only in reflections and
  shadows; the real 3D set is drawn over the first hit.
- **Light:** hard sun shadows, and reflections to a depth set by quality (1–3 bounces).
- **Depth.** The pass writes depth (`gl_FragDepth` from the hit point), so the real set
  and the traced objects hide each other correctly.
- **Lens flare:** a separate additive pass (`shaders/flare.frag`) when the sun is in view.
- **Tile reveal:** in the glass (`desk.frag`, `uReveal`).
- **The sweeping fly-in:** a yaw and pitch arc (`sin(πe)`) added to the flight, zero at
  both ends.
- **The demo-reel orbit** turns the camera while nothing plays.

**Measurements** (`tests/automation/cg.txt`, `docs/results/cg-checks.txt`):

| Check | Result |
|---|---|
| The sweeping fly-in lands on the exact fullscreen frame | max diff 0 |
| Palette skies (mean colour, top of the view) | Workstation (34, 116, 135), Sunset (223, 123, 87), Deep space (13, 14, 29) |
| Neon grid vs checkerboard (floor) | mean difference 60.1 |
| Objects off | 4.6 % of the view changes |
| Plinth vs pedestal | mean difference 37.4 |
| Hard shadows (quality medium vs low, floor) | difference 6.49 |
| Camera asked for pitch −40° | stays 0.15 above the floor |
| All the way round | yaw 170° accepted |
| Demo-reel orbit | +22° while paused, 0° while playing |
| The picture reflected in the scene (red vs white picture) | 17,797 pixels outside the screen turn red |
| Tile reveal | 0.30 just after playback starts, then complete |

### Found and fixed while building it

1. **The deep-space planet came out far too large** and high in the sky, with its ring
   across the corner. It's now smaller and rests on the horizon.
2. **One check was wrong:** the objects-off check averaged over the whole view (4.2),
   hiding a real change. The objects are a small part of the view, so it now counts
   changed pixels (4.6 % of the view).

### Changed from the plan

The idle demo-reel motion is the camera slowly orbiting the set, not the set turning on
its own. From the viewer's seat it looks the same, and the stand, shadows and reflections
stay consistent.

### Not tested

- GPU speed. This is the heaviest scene: ray-traced reflections and shadows for every
  pixel, with the torus marched. Quality lowers the bounces and turns the shadows off.

**Release run note:** an environment reset killed the release run as its AppImage stages
began. The system Qt, left hidden, was restored, and the two AppImage stages were run
again on their own; both passed.

## 2.2.3: real rooms for the desk and wall scenes; the theater's sconce light (from real use)

### Real rooms

**The report:** looking sideways in the desk and wall-mounted scenes showed empty
darkness. They had only a floor and one back wall.

**The fix:** both scenes are now closed rooms (`shaders/scene_desk.frag`):

- **Side walls:** at ±3.4 (desk) and ±3.8 (wall scene) from the set, in the same paint or
  wallpaper as the back wall, with the skirting board.
- **A plaster ceiling:** 4.6 above the floor (desk), or 5.0 (wall scene; raised when the
  TV hangs high), with a cornice.
- **Corners and edges:** softly darkened where surfaces meet.
- **Light:** the lamp, the screen and the picture lights reach the new surfaces.
- **The camera stays inside the room.** Turning is eased back before a side wall, and
  tilting before the ceiling. The room's size lives in one place
  (`DeskRenderer::roomHalfWidth`, `ceilingHeight`), shared by the drawing and the camera
  limits.

**Measurements** (`docs/results/wall-checks.txt`):

| Check | Result |
|---|---|
| Turned 70° to the side | camera x −3.49 (walls at ±3.8) |
| From far above (pitch 45°) | 4.69 above the floor (ceiling 5.0) |
| The edges of a turned view | walls, not darkness (darkest edge strip 20.6) |
| Looking up | the ceiling (top strip 18.9) |

The desk scene's measurements are unchanged, except one: the mood check's wall sample now
includes the lit side wall (29.8 > 9.9 > 1.2; the order still holds).

### The theater's sconce light

**The report:** in the theater, the sconces' light stopped abruptly at a line. The glow
was drawn only above each lamp (a hard step), on the proscenium and on the side walls.
It now fades softly below the lamp as well. Nothing else in the theater changed.

**Check:** vertical lines through the sconces show no hard step; the largest step between
neighbouring pixels is 5.0, from the fabric and trim.

## 2.2.2: jagged seats when turned (from real use)

**The report:** seen from the side (turned along the row), the theater seats showed
jagged, saw-tooth edges.

**The cause:** the seats are ray-marched. Each step asked only the seat directly in the
ray's path how far away it was. A ray crossing the rows at an angle passes beside other
seats, so it could overshoot into a neighbouring seat. That gave saw-tooth edges,
broken-looking backrests, and comb-like stripes under the seats.

**The fix:** each step now also asks the seats on both sides (three per row instead of
one). A "no seat" shortcut in the aisles, which reported a made-up distance, went too.

**How it's measured:** a hidden reference mode (`theatermarch ref`) draws the seats in
tiny, careful steps, which is slow but near-exact. The normal rendering is compared with
it from 30° to the side, paused, with the effect clock pinned
(`docs/results/theater-checks.txt`):

| Pixels of the seat area noticeably off (> 12) from the reference | Result |
|---|---|
| Before the fix | 1.19 % (the reference itself also showed the stripes) |
| After the fix | 0.02 % |

**Found while testing:** in the full suite this check first read 25.8 %, then 5.3 %, but
not because of the seats:

- the video was playing, and the seats are lit by the picture, so they changed between
  the two captures;
- after pausing, the projector beam was still fading out.

The test now pauses and settles the theater (`theatersnap`) before capturing.

**Not tested:** the GPU cost of evaluating three seats per step, on real hardware.

**Release run note:** an environment reset killed the release run during its last stage
(the AppImage on Wayland without the system Qt). The system Qt, left hidden by that
stage, was restored, and the stage was run again on its own; it passed like the others.

## 2.2.1: the theater's seats and room as one space (from real use)

**The report:** the seats and the room did not feel like the same space; zooming in and
out caused problems; the seats should be 3D.

**The cause:** the seats were flat silhouettes placed at fixed distances in front of the
**camera**. Zooming moved the camera relative to the screen and walls, but the seats came
along. Zoomed out, they stayed large while the room shrank away.

**Fixes:**

- **A real auditorium in the room.** There are 20 rows of stadium seating: each row
  0.07 higher than the one in front, 0.22 deep, starting 1.2 from the screen. Two aisles
  run through it, with step lights on the risers.
- **3D seats.** Each seat is ray-marched: a reclined backrest, a cushion, armrests on
  posts, staggered between rows, and the stepped carpeted floor. The seats are lit by the
  screen, with the light wrapping round and bouncing, and by the house lights.
- **The camera sits in a row.**
  - Zoom moves between rows 3 and 19.
  - Sideways moves along the row, inside the walls.
  - Your eye stays at seated height, on a smooth ramp over the rows.
- **The projector** is fixed in a booth at the back wall.
- **A wide, human field of view** (58° vertical) in the theater, so the rows in front and
  the room are in view. The other scenes keep 30°.

**Measurements** (`docs/results/theater-checks.txt`):

| Check | Result |
|---|---|
| Zoomed out: row, eye height | row 17.2, eye 1.05 (seated 1.05) |
| Default: row, eye height | row 11.6, eye 0.66 (seated 0.66) |
| Zoomed in: row, eye height | row 3.0, eye 0.06 (seated 0.06) |
| Turned 30° from the back | pulled in to stay inside the walls (x −1.97) |
| 3D seats below the screen (default view) | red fabric over most of the lower quarter, with row-to-row variation |
| Fly-in landing (film look, pinned clock) | max diff 0 |

### Found and fixed while building it

1. **The 3D seats were first invisible, for two reasons:**
   - **Out of frame:** desk mode's narrow 30° lens put the rows in front below the frame
     from a seated eye height. The theater now uses a wide field of view.
   - **Too dark:** a seat back faces away from the screen, so a strict facing-the-light
     rule left it black. The screen's light now wraps round and bounces.
2. **Zoom meant "how much of the window the screen fills".** With the wide lens, the
   default put the camera in the front row, looking up at a keystoned screen. The default
   is now a middle row, and zoom spans the rows.
3. **In the front rows, the camera stayed too high.** The general "stay above the floor"
   rule from 2.1 treated the screen's bottom edge as the floor (eye 0.13 instead of
   0.06). The theater now uses only its own seat rule, and the front limit is row 3,
   where row 1 would crane the neck.

### Not tested

- GPU cost of ray-marching the seats (up to 110 steps per pixel, in the lower part of the
  view), on real hardware.

## 2.2: the movie theater, film projection looks, wall placement

**What it is:**

- **The theater** (scene 3) draws the set as a bare, matte screen (a new *theater screen*
  cabinet). Two passes surround it:
  - **behind it** (`shaders/theater.frag`): the room, with stage, screen fabric, side
    walls, ceiling, carpet and haze;
  - **after it** (`shaders/theater_front.frag`): the proscenium and its sconces, black
    masking, the valance, the curtains, rows of seats with aisle step lights, the
    projector beam and haze in front.
- **The desk view animates** the curtains (~3 s), the house lights (following the
  curtains), the masking (gliding to the picture's shape) and the beam. The curtains
  follow playback.
- **Film projection** is four new picture settings: grain, gate weave, projector flicker,
  and dust and scratches, at 24 film frames per second. Three presets use them, and the
  theater switches to one while it is shown.
- **Wall scene placement:** TV height, picture height, spacing and size. The mount is now
  dark metal.

**Measurements** (`tests/automation/theater.txt`, `docs/results/theater-checks.txt`):

| Check | Result |
|---|---|
| Before the film: curtains, house lights | closed (0), house lights 1 |
| The theater draws its screen, and the film look | `theater-screen`, *Film Print (35mm)* |
| Closed curtains cover the screen | centre (53, 4, 5): red velvet |
| While playing | curtains open (1), house lights 0, picture shows (colourfulness 188) |
| Thick haze: light in the air above the screen | 3.2 → 54.9 |
| Masking for a 4:3 film | 1.333 × 1.000 |
| Fly-in landing with the film look (effect clock pinned) | max diff 0 |
| At the end of the film | curtains closed (0) |
| Leaving desk mode | your own look back (*Consumer Television*) |
| Film grain from one film frame to the next / replayed | differs (mean 3.65) / identical (max 0) |
| Wall placement: pictures higher, further, larger | centre (509, 445) → (407, 373), area 1366 → 6752 |

### Found and fixed while building it

1. **The first theater looked wrong:**
   - the seats filled the bottom 40 % of the view;
   - the camera sat too close (valance cut off);
   - closed curtains left a strip of picture showing.

   The rows now start further out and lower, the seat is further back, and closed
   curtains overlap.
2. **The projector beam was invisible in the first check, then far too strong:**
   - **Invisible:** the test paused before grabbing, and pausing fades the beam out.
   - **Hidden haze:** from the seats the proscenium covers the whole view around the
     screen, so the room's haze (drawn behind it) could never show. Haze is now drawn in
     front as well.
   - **Too strong:** the beam then bleached the picture white in thick haze. The beam
     was weakened and the haze thinned over the picture, and the dust sparkles are fainter.
3. **Curtain moves took ~20 s on this slow software renderer,** because each step was
   capped at 0.1 s per drawn frame. Raised to 0.25 s. The tests use `theatersnap` to be
   independent of machine speed.
4. **The sconces were hidden** behind the gathered curtains; they moved outward.
5. **The desk view kept its own effect clock.** Time-based effects (grain, flicker, VHS
   jitter, static) therefore ran on a different clock from the regular view, and pinning
   the clock in tests did not reach desk mode. Both views now share one clock; the
   landing check with the film look went from mismatched grain to max diff 0.
6. **Two test mistakes in the landing check:**
   - pinning the clock before reopening the file froze the channel-change static;
   - seeking while playing captured different frames (6.0 s vs 7.4 s).

   The reference is now taken with the clock unpinned while the file opens, and the
   test pauses before seeking.
7. **The wall mount took the cabinet's colour,** which read as a beige stub under the
   beige monitor (seen in a user's screenshot). It has its own dark metal material now.

### Not tested

- GPU speed of the theater passes with haze.
- Curtain timing on real hardware (checked by snapping).
- How the film looks compare with real projection (judged by eye).

## 2.1: the wall-mounted TV scene, pictures, scene previews, camera floor limit

**What it is:**

- **The mount.** A wall-mounted set has no stand, legs or base. It gets a mount instead:
  a tray, an arm and a wall plate for the CRT sets, or a slim bracket for the flat panel.
- **The scene pass** now ray-casts the wall, the floor below and up to four framed
  pictures. Each frame is a box on the wall with a moulding, a mat, the picture under
  glass, and a brass picture light.
- **Picture placement.** Frames are placed from the set's actual bounds, and each takes
  its picture's shape.
- **Scene previews.** The settings window shows a preview of each scene, rendered from
  the current view.

**Measurements** (`tests/automation/wall.txt`, `docs/results/wall-checks.txt`):

| Check | Result |
|---|---|
| Camera below the floor in a scene (asked for pitch −30°) | raised to −5.0°, 0.16 above the floor |
| … on the transparent desktop | stays at −30° (no floor there) |
| Frames per layout (one each side / two each side) | 2 / 4 |
| First picture on the left (sunset, found by hue) | 1366 pixels left, 0 right |
| Second picture on the right (mountain) | 13166 pixels right, 0 left |
| Four pictures: the third and fourth on the right | green 1330, magenta 8703 pixels |
| Gold vs black moulding | gold (64, 49, 32) vs black (34, 26, 17) |
| A missing picture file | skipped: 3 of 4 loaded, 3 frames shown |
| Pictures remembered across leaving desk mode | wall scene, 4 pictures |
| Wall scene opaque; fly-in landing | 100 %; max diff 0 |
| Scene previews | all three render, and differ |

### Found and fixed while building it

1. **Pictures hid behind a wall-mounted CRT from an angle.** The CRT sticks out about 1.2
   units from the wall. The gap beside the set now grows with its depth; the flat
   panel's pictures stay close.
2. **The picture on the side away from the lamp was nearly black** (average 20/255, vs.
   87 for the lit side). Brass picture lights above each frame now light the pictures
   (evening and night moods).
3. **The first picture lights shone upward,** lighting the wall above the fixtures, not
   the pictures below (a sign error in the light's cone). Fixed; the pictures are now
   lit top-down.
4. **The checker's colour matching was wrong.** It matched the pictures' file colours,
   but on the wall they are darker and warmer under the room's light. It now finds
   pictures by hue, outside the set's own screen.
5. **`slots` is a reserved word in Qt code** (a Qt macro), so a variable of that name
   would not compile.

### Not tested

- GPU speed with four pictures and fog.
- Very large or unusual image files (images are scaled to at most 1024 px).

## 2.0: desk-mode scenes (the desk scene, the screen lighting the room, fog)

**What it is.** Desk mode's backdrop is now a **scene**: *your desktop* (transparent) or
*desk*. The desk scene is drawn in one full-screen pass behind the set
(`shaders/scene_desk.frag`). It is ray-cast from the same camera as the set, so the
floor is the plane the set's shadow lies on. After the set, a haze pass
(`shaders/haze.frag`) draws the fog in front of it.

**Measurements** (`tests/automation/scene.txt`, `docs/results/scene-checks.txt`):

| Check | Result |
|---|---|
| Desk grain / plank period vs. the picture's height (the report: "the grain is too large") | 1.9.2: **0.123** → 2.0: **0.055** (walnut), 0.043 (oak) |
| Moods, wall brightness | evening 14.1 > night 5.9 > lights off 1.0 |
| The picture lights the desk in front of the set (lights off) | black picture 1.1 → white picture 49.8 |
| … in the picture's colour | red picture: R 80, G 1, B 1 |
| Thick fog around the set | surroundings 15.8 → 42.0 |
| Scenes are opaque | 100 % |
| Flying in with the desk scene and thick fog | lands on the exact fullscreen frame (max diff 0) |
| Scene settings remembered across leaving and re-entering desk mode | desk, lights off, thick fog |

### Found and fixed while building it

1. **The screen's light was there but far too faint.** A solid white picture lit the desk
   in front to only 30/255 in a dark room, and with normal content (about half as
   bright) it was nearly invisible. I measured it with solid black, white and red
   pictures instead of judging from colour bars. It's now 2.8× stronger (80/255 with
   white at that pose).
2. **The first fog was grey and flat:** the room's ambient light dominated the
   scattering. The screen's light now dominates, with a stronger glow around the set.
3. **The Scene settings window opened centred over the picture,** since dialogs centre on
   their parent, here the TV. It now opens at the top right, with a proper width.
4. **The picture's reflection in the desk** is only visible from some angles. At low
   viewing angles the desk in front of the set correctly reflects the cabinet's lower
   front, not the screen. This is physically right, so it is left as is.

### Not tested

- Fog performance on a real GPU (only software rendering here). The Quality setting
  (8/16/32 fog samples) is the safety valve.
- Scene preview images in the settings window (planned with the wall-mount scene).

## 1.9.2: AppImage display on Bazzite, keep the screen awake (from real use)

### The AppImage showed an empty, see-through video area

On Bazzite (KDE Plasma, Wayland), every AppImage so far opened with the video area empty
and the desktop visible through it, with no errors. The standalone binary (using
Bazzite's own Qt) worked on Bazzite and on Arch.

**What I could and could not reproduce:**

- **None of my tests could see this.** They all checked the image the *player renders*,
  never what the *compositor shows*.
- **I now screenshot the real composited output**, with Weston's screenshooter. My first
  attempt seemed to reproduce it, but the screenshot came at a fixed 7 s while the
  AppImage was still unpacking itself. With a proper wait, the AppImage shows the picture
  on Weston: bundled Qt, system Qt, and every mixture of bundled libraries and plugins.
- **The difference on Bazzite** is therefore in what I don't have here: KWin, a real GPU
  driver, and a much newer system Qt than the AppImage's Qt 6.4.

**Fix: the AppImage now prefers the system's Qt.**

- The AppImage carries a second copy of the binary with no library path into the bundle.
- The launcher uses it when the system Qt is 6.4 or newer and every library resolves
  (checked with `ldd`). That's the same setup as the standalone binary that works on
  Bazzite and Arch.
- Otherwise the bundled Qt is used, through XWayland on a Wayland session.
- `CRTPLAYER_QT=system|bundled` forces either. The system report and the automation
  report show which one is in use.

| Situation | Qt used | Route | Plays |
|---|---|---|---|
| System Qt available | System | X11 | yes |
| Forced bundled, with Wayland and X11 both available | Bundled | XWayland | yes |
| No system Qt | Bundled (automatic) | X11 | yes |

**New check:** `scripts/check-composited.py` runs the player in Weston and screenshots
the composited output until a colourful picture shows in the video area. It passes for
the native binary and for the AppImage with its bundled Qt on Wayland.

**Not verified:** the fix on Bazzite itself. It rests on the fact that the same system
Qt works there.

### The screen dimmed during playback

While a video plays, the player now inhibits idle and sleep, through:

- `org.freedesktop.ScreenSaver` (dimming, blanking, locking) and
  `org.freedesktop.PowerManagement.Inhibit` (automatic suspend), on KDE and others;
- the desktop portal's `Inhibit` with flags idle + suspend, when there is no ScreenSaver
  service (GNOME and others).

It is released on pause, stop, the end of the playlist and quit, and there's a setting to
turn it off.

**How it's tested:** stand-in services on a private session bus
(`tests/inhibit_mock.py`) log every call (`docs/results/inhibit-checks.txt`):

| Setup | Result |
|---|---|
| KDE-like | Inhibit while playing (cookie 1001); UnInhibit(1001) on pause; again on play; released on quit; PowerManagement mirrors it; app "CRT Player", reason "Playing a video" |
| Portal-only | Inhibit with flags 12 (idle + suspend); `Request.Close` on the same handle on pause and on quit |

**Not tested:** real KDE PowerDevil and gamescope. In Game Mode it depends on what Steam
provides on the session bus.

## 1.9.1: other distributions (from an Arch report)

**The report.** On an Arch install, 1.9.0 refused to start. It said `autoaudiosink
(gstreamer1-plugins-good)` was missing.

**Two faults:**

1. **Every install hint used Fedora package names.** On Arch the package is
   `gst-plugins-good`.
2. **The player treated the automatic audio output as essential.** It isn't: audio can go
   to PipeWire, PulseAudio or ALSA directly, and many files need no "good" plugins at all.

**Fixes:**

- **Distribution detection.** The family comes from `/etc/os-release` (`ID`, `ID_LIKE`,
  and `/run/ostree-booted` for atomic Fedora). Every message gives that family's package
  names and command.
- **Only GStreamer's core is fatal** (`playbin`, `appsink` and the converters, all part of
  the base set).
- **Anything else is a non-blocking notice.** It lists what won't work, gives the command
  with a *Copy command* button, and offers "don't show again" until the list changes.
- **Audio falls back** to `pipewiresink`, then `pulsesink`, then `alsasink`. Each is tested
  before use, with silent (but correctly timed) playback as the last resort.
- **`--check-gstreamer`** now lists what won't work and prints the command.

**Install hints for 9 systems** (`tests/os-release/`, with `--print-distro`):

- Arch, Manjaro and CachyOS → `sudo pacman -S --needed gst-plugins-good …`
- Fedora → `sudo dnf install gstreamer1-plugins-good …`
- Bazzite → `rpm-ostree install …`
- Ubuntu and Mint → `sudo apt install gstreamer1.0-plugins-good …`
- openSUSE → `sudo zypper install gstreamer-plugins-good …`
- An unknown distribution → a generic hint.

All 9 are correct.

**Recreating the report.** `scripts/test-without-good-plugins.sh` moves all 74 "good"
plugins away and presents the system as Arch. It checks that:

- `--check-gstreamer` finds nothing essential missing and gives the `pacman` command;
- the player starts;
- an Ogg/Theora/Vorbis file plays (frames presented);
- audio uses a fallback output (here "none": the test container has no sound device,
  while a real system reaches PipeWire via ALSA);
- the missing formats are listed;
- an MP4 fails with the `pacman` fix in the message.

It passes on the native binary and the AppImage, and restores the plugins afterwards
(verified: 74 of 74 back).

**The AppImage on minimal systems.** Qt Network needs `libproxy.so.1`, which the AppImage
deliberately takes from the system (1.5). A system without it could not start the
AppImage at all. The AppImage now carries a small stand-in (three functions that answer
"no proxy", with the matching `LIBPROXY_0.4.16` symbol version). A start-up hook uses it
**only** when the system has no libproxy.

Tested with the system's libproxy hidden:

| Case | Result |
|---|---|
| The bare binary (control) | Fails: `libproxy.so.1: cannot open` |
| The AppImage | Starts |

**Found while doing this:**

1. **linuxdeploy leaves `AppRun` as a plain link to the binary, so hooks never run.** The
   new build check failed on it. The AppImage now has its own small launcher script.
2. **My first no-libproxy test hid only the `libproxy.so.1` link.** `ldconfig` recreated
   it from the real file, so the test proved nothing. The test now hides the real file,
   and the control fails as expected.
3. **Game Mode could fall out of fullscreen on a slow start.** The window was shown normal
   and then switched, and the window manager's late "normal" state cleared the flag. It
   failed once, in the AppImage run. Fullscreen is now requested as the window is first
   shown; three repeat runs and both release runs passed.

**Not tested:** a real Arch, Debian or openSUSE installation; the distributions were
simulated by their os-release files, with plugins removed.

## 1.9: media keys (MPRIS), game controllers, Steam Game Mode

**MPRIS** (`org.mpris.MediaPlayer2.crtplayer`). Tested with `playerctl` on a private
session bus, the same interface KDE's media widget and KDE Connect use:

- the player is found;
- status, title and length (20 s) are read correctly;
- pause and play work;
- setting the position to 10 s reads back 11.3 s (playback continued in between);
- volume 0.3 reads back 0.3;
- next and previous switch titles;
- changes are announced: 14 `PropertiesChanged` signals, including status and metadata.

**Game controllers** (SDL2, loaded at run time, so it's no build or run dependency). Tested
with SDL's virtual controller, which uses the same event path as a real pad:

| Input | Result |
|---|---|
| A | Paused, then resumed |
| D-pad → | +10 s |
| Holding D-pad → | Kept seeking (+53 s in 1.5 s) |
| RB | Next preset |
| D-pad ↑ | Volume +5 % |
| View | Desk mode on, and later off |
| Left stick | Turned the set (+87–104° in 1 s) |
| Y / B | Flew in / flew out |

The system report lists the controller.

**Room backdrop.** It's a walnut desk on the shadow's floor plane, a lamp pool, and a dim
wall:

- it's fully opaque (100 %);
- flying into fullscreen still lands on the exact frame (max difference 0).

**Steam Game Mode** was simulated with `GAMESCOPE_WAYLAND_DISPLAY`: it was detected, the
player started fullscreen, desk mode used the room backdrop, and the system report notes
gamescope.

**Found and fixed:**

1. **The stick turned the set too slowly under heavy rendering** (8.8° in a second). The
   speed was per poll; it is now per second. The time step's cap was then raised from
   0.1 to 0.5 s, after a release run measured only 27° with the lower cap.
2. **The first room backdrop was too dark** to read as a desk.
3. **Build errors:** duplicate accessors (the class already had them), and SDL's
   `SDL_zero` macro, which calls SDL's own `memset` and would have made SDL a link
   dependency.

**Not tested:** real gamescope or Steam, real controllers, and KDE's own media widget.

## 1.8: everyday playback

**Test media.** `scripts/make-test-media.sh` adds:

- a 30 s MKV with three named chapters and 1 s keyframes;
- a clip with two sidecar subtitle files;
- a 90 s clip for resume.

**How it's tested.** `tests/automation/polish*.txt` runs three launches that share
settings and data, and `scripts/check-polish.py` checks the results
(`docs/results/polish-checks.txt`):

| Feature | Check | Result |
|---|---|---|
| Chapters | next, next, previous from 2 s | 10.000 s, 20.000 s, 10.000 s (all three titles read from the file) |
| Speed | position vs. real time at 2× and 0.5× | 2.00× and 0.50× |
| A–B loop | position while looping 3–5 s | stays inside (3.4 s, 3.4 s) |
| Seek preview | size, and match with the real frame at 20 s | 240×135; difference 9.1 vs 17.3 for the 5 s preview |
| Sidecar subtitle | loads automatically; drawn into the picture | bottom-third change 1.58, top half 0.00 |
| Second sidecar file | different text | loaded `subs_clip.en.srt`, bottom-third change 1.58 |
| Resume | next launch after leaving the file at 45 s | continued at 46.6 s |
| Resume near the end | left in the last 30 s | started from 0.2 s |
| Recent files | after playing | `resume_clip.mp4, subs_clip.mp4, chapters.mkv` |
| System report | contents; privacy | 14 lines with every section; no paths, file names or tokens |
| Jellyfin external subtitle | offered, fetched, loaded | one authenticated request for `…/Subtitles/2/0/Stream.srt`; loaded (both API versions) |

### Found and fixed during this round

1. **External SRT subtitles didn't show up at all.** GStreamer could not identify short
   SRT files; the log said "Could not determine type of stream". I tested it directly:
   files of 54 and 110 bytes failed, while a realistic 20-cue file (1.4 KB) was
   detected. ASS and WebVTT files are detected even when tiny, because they start with a
   header.

   External subtitles are now played from a local copy, padded with trailing blank lines
   (which subtitle formats ignore) when shorter than 4 KB. This also means Jellyfin
   subtitles are downloaded once, with the session's authentication.
2. **Seek previews snapped to keyframes 8 s apart in the first test clip,** so the 20 s
   preview showed ~16.7 s. Previews deliberately use fast keyframe seeks (decoding to
   the exact frame on every hover is too slow in software). The README says so, and the
   test clip now has 1 s keyframes, so the test checks the mechanism.
3. **The chapter test clip lost two of its three chapter titles** when encoding and
   adding chapters in one ffmpeg step. The chapters are now added in a separate
   copy-only step.
4. **The Jellyfin check counted the new subtitle download as a video stream** and
   required `static=true` on it. It now checks only `/stream` requests.
5. **Compile errors along the way:** a duplicate `glInfo()` (the view already had one),
   a guessed name for the frame statistics, a mistyped variable in the chapter-message
   handler, and missing includes.

### Not tested

- Pitch preservation at other speeds (`scaletempo`) was not measured; only the speed was.
- Previews on very large or remote files, and how fast they are on real hardware.
- Chapters in MP4 files (only MKV was tested).

## 1.7.2: solid shadow under raised sets (from real-screen feedback)

A screenshot of 1.7.1 showed the console's shadow as an outline on the floor, with the
floor under the TV looking lit.

**Cause.** The contact shadow faded *out* towards the middle of the set's footprint.
That was written for the original TV, which sits on the desk and always hides the floor
under itself. Sets on legs or a stand (the wood console, the flat panel, the beige
monitor) show that floor, and it appeared as a bright hole.

**Fix.**

- The shadow is solid across the footprint, a little darker towards the middle, and
  fades outward.
- Raised sets get a softer edge.
- The console gets a darker contact spot under each leg.
- Viewed from below the floor, the shadow still disappears.

**New check.** From a normal viewing angle, the floor seen below the console runs from
behind the set, under it, to in front of it. The check requires the shadow's middle to
stay dark there. Validation:

| Build | Shadow opacity profile below the cabinet (behind → under → in front) | Middle | Result |
|---|---|---|---|
| **1.7.1** (the screenshot's version) | 0.41 … **0.00 0.00 0.00 0.00 0.00** … 0.42 | 0.00 | **fails**, as it should |
| **1.7.2** | 0.41 0.45 … **0.55 0.55 0.55** … 0.43 | 0.46 | passes |

**Found while writing this check:** the first two versions of the check measured the
wrong part of the image. From a raised camera, the floor just below the cabinet's edge
is *behind* the set, not under it. Both versions were caught because they passed 1.7.1
and failed the fix. The check was only accepted once it failed the old build and passed
the new one.

**Unchanged:** the CRT television's shadow (still 8.3 % of pixels), since that set rests
on the desk and hides the inside of its footprint. (`docs/images/shadow-fix.png`: top
1.7.1, bottom 1.7.2.)

**Release runs:** the native binary on X11 (213/213) and Wayland (59/59), and the
AppImage without the system Qt on both. All pass, and the new shadow check reads 0.46
in all four.

## 1.7.1: wood console fixes (from real-screen feedback)

Screenshots of 1.7.0 on KDE showed three problems with the 80s wood-grain console:

- **Floating back legs.** The cabinet tapered towards the back, so its underside rose
  above the back legs' tops, leaving a visible gap.
- **The wood grain looked like wavy zebra stripes.** The lines ran as vertical bands, far
  too contrasty and wavy.
- **The knobs looked like flat grey discs.**

**Fixes:**

- The console is now a straight box with a flat underside. Its four legs taper and sink
  into the body.
- The walnut veneer was redone:
  - fine, low-contrast lines with irregular spacing and varying strength;
  - lines run along the boards (horizontal on the front and sides, front-to-back on
    the top, vertical on the legs);
  - broad gentle figure and pores;
  - fine detail fades below a pixel, so it doesn't shimmer;
  - a satin finish.
- The knobs are modelled as a metal flange, a fluted bakelite body with a bevel, and a
  brushed-aluminium cap with an indicator line.

**New check:** from a low side view, every pixel column through the set must be one
unbroken opaque run. Validation:

| Build | Pixel columns with a gap under the legs |
|---|---|
| **1.7.0** (the version in the screenshots) | **41** — the check fails, as it should |
| **1.7.1** | **0** |

The wood and the knobs were judged by eye (`docs/images/wood-console.jpg`).

**Release runs:** the native binary on X11 and Wayland, and the AppImage without the
system Qt on both. All pass, including the new leg check, which has 0 gap columns in
every run.

**Environment note:** the AppImage's Wayland run was interrupted once by a reset of the
test environment, which killed the processes outright. It was rerun on its own and
passed.

## 1.7: deeper simulation and more desk-mode sets

### Simulation (`tests/automation/sim.txt`, `docs/results/sim-checks.txt`)

Every capture uses the pinned effect clock (`rendertime`), so the results are
deterministic. Measured on a paused 1080p test pattern with the other effects off:

| Feature | Check | Result |
|---|---|---|
| 9-bit colour | Distinct colours in the picture | 314 (limit 512; full colour ~44 000) |
| 3-bit colour | Distinct colours | 8 (limit 8) |
| Game Boy palette | Distinct colours; green-tinted | 4; G ≥ R and G ≥ B everywhere |
| CGA / EGA | Distinct colours | 4 / 15 (limits 4 / 16) |
| Ordered dither (3-bit) | Brightness error of 8×8 averages vs. the original | 2.2 with dither vs 8.6 without |
| PAL field rate | 10.000 s vs 10.017 s | NTSC: different fields (43.4); PAL: same field (0.00) |
| PAL Hanover bars (rainbow on) | Row-to-row hue change | 1.40 PAL vs 0.86 NTSC |
| Persistence | Live picture vs. the same moment without afterglow | 1.4 % of pixels brighter, **0.000 % darker** |
| Persistence off | Live picture vs. screenshot | identical (max difference 0) |
| Power on (at 20 %) | Lit rows | 30 % (100 % when done) |
| Power off | Lit rows at 11 %, lit columns at 44 %, level at the end | 69 %, 19 %, max 0 (dark) |
| Static | Pixel-to-pixel change | 48 vs 0.6 for the picture |
| VCR text | Where the picture changes | 100 % in the top-left quarter |

### Desk mode

- **All six sets** render, are transparent around themselves (76–83 %), and differ from
  each other; the closest pair, CRT vs flat-face CRT, differs by a mean of 0.73.
- **Flying in from the beige monitor** lands on the regular fullscreen frame (max
  difference 0), like the CRT and the flat panel.
- **Desk menu, real input on composited X11:** *Open files…*, *Playlist* and *Jellyfin:
  continue watching* all appear. The Jellyfin list was filled from the mock server
  ("Multitrack HD (2023)").

### Found and fixed during this round

1. **`param` in the automation sent every value as a number.** The new on/off settings
   read that as "off", so the power-on/off test was silently testing nothing. Boolean
   keys are now sent as booleans.
2. **The Hanover bars were too faint to matter** (PAL only 1.26× NTSC). They were
   strengthened to a visible level.
3. **The persistence check compared two separate step sequences,** which can land on
   different frames. It now compares the live picture against a screenshot of the same
   moment (screenshots never include the afterglow). It also excludes the Qt toast
   overlay, which is in window captures but isn't part of the video.
4. **A "static" test left static running into the next test.** Static holds until a new
   frame arrives, and the video was paused. `moment none` now ends all moments.
5. **The final power-off state repainted continuously,** as did an untimed "PAUSE"
   message. Only moments that are actually changing now keep the view repainting.
6. **The Open button stopped compiling** after `openDialog` gained a parent argument
   (a signal/slot mismatch).

### Not tested

- The new looks compared with real hardware: PAL sets, a Game Boy, CGA/EGA monitors,
  real phosphor decay.
- How persistence and the moments perform on a GPU.
- The console TV's pivoted (portrait) layout was not reviewed visually.

## 1.6: flat-panel and flat-face CRT sets (desk mode)

Desk-mode image checks (`docs/results/desk-checks-*.txt`), on X11 and Wayland:

| Check | Result |
|---|---|
| Flat panel: flying in lands on the regular fullscreen frame | max pixel difference **0** |
| Flat panel is slimmer than the CRT set at the same angle | 11.4 % of the screen opaque vs 15.3 % |
| Flat-face CRT differs from the curved-glass CRT | mean difference 0.73 (flat vs bulging glass) |
| The CRT set is unchanged after bounds are computed from geometry | identical bounding box and shadow fraction as before |

**Visual check** (`docs/images/desk-sets.jpg`): the three sets, a flat panel seen from
the side, and a portrait (pivoted) flat panel whose stand stays upright on the floor.

**Not tested:** how the matte flat-panel screen compares with a real display; its
appearance was judged by eye only.

## 1.5.1: shadow hidden when viewed from below the floor

In desk mode, the contact shadow is now drawn only when the camera is above the floor
plane it lies on. It fades out over the last few degrees before the crossing, and the
click-through outline stops including it at the same point.

| Check (desk suite, X11 and Wayland) | Result |
|---|---|
| Shadow pixels when tilted below the floor (pitch −30°) | **0.000 %** (8.3 % at the default +9°) |
| Reported shadow visibility (the same test the click outline uses) | below: off, above: on |

**Tilt sweep from +9° to −30°** (manual measurement, X11):

- Shadow area shrinks steadily: 7.4 % → 3.6 % (0°) → 1.3 % (−4°).
- Its opacity drops from 0.056 to 0.035 at −4°.
- It's gone from −8° on.
- There's no sudden jump.

**Found while doing this:**

- The first version of the click-area check compared outlines at two different tilt
  angles, and the tilt alone changes the outline. It was replaced by the direct
  visibility check above.
- The interlaced-field check failed once more, even with staggered snapshot intervals.
  Timing-based captures of animated effects can't be made reliable, so a test-only
  `rendertime` command now pins the effect clock. The interlaced and dot-crawl checks
  capture exact consecutive fields, and three back-to-back runs gave identical
  measurements.
- A killed test job once left the system Qt libraries moved aside. Long jobs now run in
  the background with a guaranteed restore.

## 1.5 additions: Jellyfin

### How it was tested, and what that means

There is **no real Jellyfin server** in this environment. The client was tested end to
end against `tests/jellyfin_mock.py`, a mock I wrote from the public Jellyfin API. It
enforces the parts that matter:

- **Authentication.** Every call except public info and sign-in requires the
  `Authorization: MediaBrowser … Token="…"` header; otherwise it answers 401.
- **Version-specific routes.** A mock started as 10.8 serves only the old routes
  (`/Users/{id}/Views`, `/Users/{id}/Items`, …). As 10.10 it serves only the new ones
  (`/UserViews`, `/Items?userId=`, `/UserItems/Resume`).
- **Range requests.** Static streams support HTTP ranges, so seeking is exercised for
  real.
- **Server-side state.** Progress reports update resume points, and sign-out revokes
  the token.

Passing against the mock shows the client follows the documented API. **It does not
prove compatibility with every real server release.**

### Results (`docs/results/jellyfin-*-checks.txt`)

These are identical for both API versions, on X11 and on Wayland, for the native binary
and the AppImage.

| Check | Result |
|---|---|
| Wrong password | Rejected (401), player stays signed out |
| Correct password | Signed in; home shows Continue watching + 2 libraries |
| Authorization header on every authenticated request | ~47 requests per run, all with the header |
| Token in any URL | Never (no `api_key`/token query parameters) |
| API route set | Matches the server version in every request |
| Streaming | Original file (`static=true`); seeks produce mid-file HTTP range requests |
| Seek | Landed at ~14.5 s after a seek to 12 s (the extra time was playback during the 2.5 s wait) |
| Continue watching | Resumed at 12.5–12.7 s (saved position 12 s) |
| Reporting | Start ×3, pause and unpause events, stop with the position (16 s for the first movie) |
| TV shows | Shows → series → season → episode ("S01E01 · Pilot", DAR 2.37) |
| Restart | Session restored without the password |
| Sign out | `/Sessions/Logout` called (204); token cleared from disk |
| Session file | Mode 0600; contains no password field or value |
| Automation logs | Never contain the password |
| Window title | "Test Pattern 4x3 (2024) — CRT Player" (no URL, no token) |

**Streams closed mid-download.** When the player switches videos, it closes the old
stream mid-download. The mock logs that as 499, and the check accepts it, but only if
that stream had started successfully first.

### HTTPS

I created a local certificate authority, added it to the system trust store, and
served the mock over `https://`.

- **Trusted certificate:** sign-in, browsing, streaming and a mid-file seek all
  succeeded over TLS, with both the native binary and the AppImage. In the AppImage,
  Qt's bundled TLS plugin loaded the host's OpenSSL, and GStreamer validated the
  stream against the system trust store.
- **Untrusted self-signed certificate:** refused before any request completed. The
  player stayed signed out and showed "Secure connection failed … Self-signed
  certificates are not accepted …".

### AppImage

- **Bundled:** Qt Network and its TLS plugins.
- **From the host:** OpenSSL, libproxy, GnuTLS and curl. The build script enforces
  this.
- **Orphan pruning:** a new step removes libraries nothing in the bundle uses (23 were
  pruned). The AppImage is 50 libraries and 25 Qt plugins, 27 MB.
- **Test without the system Qt:** all X11 suites (187/187, 76/76 effects, 7/7 desk,
  21/21 Jellyfin ×2), the Wayland suite (39/39, 7/7 desk) and HTTPS passed.

### Found and fixed

1. **A second GnuTLS in the AppImage.** Ubuntu's Qt Network links libproxy, whose
   backend pulled in curl and GnuTLS. That would have put a second GnuTLS in the process,
   next to the host's one that GStreamer uses for HTTPS streams. libproxy now comes from
   the host, and unused libraries are pruned.
2. **The home screen loaded twice after sign-in** (duplicate requests).
3. **"Connection refused" gave no guidance.** Network errors now point at the address,
   port, firewall or name.
4. **Two flaky test checks.** The interlaced-field check could sample the same field
   twice, the same timing aliasing as the dot-crawl check before; its snapshots are now
   staggered. The Jellyfin check counted streams the player closed on purpose as errors.

### Not tested

- **A real Jellyfin server** (any version).
- **Jellyfin behind a reverse proxy with a public certificate.** This is the same TLS
  path as the trusted test CA, but the real setup wasn't tested.
- **Large libraries.** Listings load up to 500 items, with no paging beyond that.
- **Features that don't exist yet:** server-side transcoding and external subtitle
  files.

## 1.4 additions: lower resolution ("bigger pixels")

**Unit tests** of `pixelatedSize()`:

| Source | Setting | Result |
|---|---|---|
| 1920×1080 | 240 rows | 427×240, square pixels |
| 1920×1080 | 224 rows, 256 columns | 256×224 |
| 720×480 anamorphic (32:27 PAR) | 240 rows | 427×240, square on screen |
| 1080×1920 portrait | 240 rows | 135×240 |
| 1920×1080 with a rotate-90 tag | 240 rows | 135×240 |
| 1920×1080 | a 4:3 aspect override | 320×240 |

"Native" and "not lower than the source" return no change, and out-of-range values are
clamped.

**Scripted and image checks on 1080p content** (X11 and Wayland suites, native binary
and AppImage):

- **Resolution and scanlines.** The reported resolution and automatic scanline count
  were 107×60 / 60 lines, 427×240 / 240, 256×224 / 224, and 320×200 / 200. The
  scanlines lock to the pixel rows.
- **Filters.** Measured over the 107×60 block grid, the mean standard deviation inside
  each block was:

  | Filter | In-block std |
  |---|---|
  | Hard | 0.00 |
  | Sharp | 0.00 |
  | Soft | 2.50 |
  | Native picture | 5.44 |

  So Hard and Sharp blocks are perfectly uniform inside, and Soft is smooth.
- **The original-frame screenshot** stays at the full 1920×1080.
- **The compare view's original side** is identical to the same view without lowered
  resolution (maximum difference 0).
- **Desk mode:** the lowered picture appears on the 3D glass, with 128×72 reported.
  Visually, block edges are aligned to the rows but softened by perspective resampling.

**Not tested:** GPU cost. The extra work is one small downsample per new frame, plus
coordinate snapping in the shader, so it is expected to be negligible, but it wasn't
measured.

## 1.3 additions: emulator scanline looks

There are five new scanline styles, modelled on the looks of crt-geom, crt-lottes,
crt-easymode, crt-hyllian and MAME's HLSL post-processing. **They are written from
scratch, not ported.** No shader source from those projects was used, and no
side-by-side comparison with the originals was possible here.

### Measured behaviour (`docs/results/effects-checks.txt`)

The measurements were taken on the grey-ramp card at 160 lines (4.4 px per line). Gap
depth is `1 − gap brightness / line brightness`.

| Style | Gap depth at 15 / 35 / 55 / 75 / 100 % grey | Saturated R, G, B, Y, C | Intended character |
|---|---|---|---|
| Geom-style | 0.65 / 0.53 / 0.46 / 0.39 / 0.18 | 0.18–0.20 | swells with brightness ✔ |
| Lottes-style | 0.57 / 0.56 / 0.57 / 0.57 / 0.47 | 0.47–0.48 | constant beam ✔ |
| Easymode-style | 0.95 / 0.78 / 0.64 / 0.51 / 0.25 | 0.25–0.26 | thinnest when dark, swells strongly ✔ |
| Hyllian-style | 0.81 / 0.67 / 0.56 / 0.47 / 0.27 | 0.27–0.29 | swells; flat beams keep a gap at white ✔ |
| MAME HLSL-style | 0.75 / 0.76 / 0.76 / 0.75 / 0.65 | 0.65–0.66 | constant sine modulation ✔ |

**Other checks:**

- All 11 styles differ measurably from each other: 54 of 54 pairwise comparisons
  (soft vs interlaced is excluded because they share a beam shape).
- All five new presets are selectable, on both X11 and Wayland.

**No regressions.** Styles 0–5 and the Low-Res Console preset are pixel-identical to
the 1.2.0 captures (maximum difference 0), after the signal stage was moved into a
function. The Consumer TV, VHS Home Recording and LaserDisc-with-Laser-Rot presets
differ between runs, but they differ just as much between two earlier runs of
unchanged code: they contain time-animated noise and jitter.

### Found and fixed while tuning

1. **Beam width from overall luminance was wrong for saturated colours.** A bright blue
   beam didn't swell, and my first measurement even showed shallower gaps in "dark"
   areas than in bright ones. The measurement region was also flawed (it overlapped a
   bright yellow shape). Now the width is set per colour channel, and it's measured on
   a dedicated grey ramp.
2. **Brightness compensation clipped merged bright beams,** erasing the scanlines in
   saturated colours (Easymode measured 0.00). The compensation is now partial, and
   Easymode's bright beam is narrower. Every beam style now keeps at least 0.18 depth in
   saturated colours.

### Not tested

- **A side-by-side comparison with RetroArch or MAME** running the original shaders.
  The resemblance is to their documented and well-known characteristics, not a
  pixel-level match.
- **GPU cost.** The beam styles cost several signal-stage evaluations per pixel (up to
  four for the Lottes style); their GPU cost was not measured.

## 1.2 additions: desk mode (3D set) and AppImage

### Desk mode, scripted (`tests/automation/desk.txt`, X11 and Wayland)

All 24 expectations pass on both X11 and Wayland. The image checks
(`docs/results/desk-checks-*.txt`) measured:

| Check | X11 | Wayland |
|---|---|---|
| Landing frame equals the regular player's fullscreen frame | max pixel difference **0** | max pixel difference **0** |
| Fullscreen phase fully opaque | 100 % | 100 % |
| Transparent around the set (desk phase) | 77.3 % transparent, 15.3 % set, 7.4 % soft shadow | same |
| Set returns to exactly the same pixels after flying out | identical bounding box | identical bounding box |
| Portrait video gives a pivoted (tall) set | cabinet w/h 0.71 | 0.71 |
| 16:9 follow-video set is wider than the classic 4:3 set | 1.31 vs 1.08 | same |

**The state checks also confirmed:**

- the regular window hides in desk mode and returns afterwards;
- the phase sequence desk → full → desk;
- the input mask is dropped in fullscreen and restored on return;
- a new file opened in desk mode replaces the previous picture, reports the new source,
  and re-shapes the set.

### Desk mode, real input on a composited desktop (X11 only)

These tests used real X11 mouse and keyboard events (xdotool), with picom compositing
and a desktop-type background window.

| Action | Observed |
|---|---|
| Transparency | A screen capture of the composited output shows the desktop around and behind the set, with no box (`docs/images/desk.jpg`) |
| Click beside the set | Went to the application behind it (xlogo became the active window) |
| Click on the set | The desk window became active |
| Drag across the set | Yaw −24° → 46°, pitch 9° → 16.5° |
| Scroll over the set | Set height 785 → 1096 px |
| Double-click the glass | Flew into fullscreen |
| Esc | Flew back to the desk |
| T | Left desk mode; the regular window returned and became active |
| Right-click | Context menu shown over the set (`docs/images/desk-menu.jpg`) |

### AppImage

**Where libraries came from.** While the AppImage played an H.264/AAC file, I read the
running process's memory map:

- **From the AppImage:** `libQt6Core`, `libQt6Gui` and the `qxcb` platform plugin.
- **From the host:** `libgstreamer-1.0`, `libgstvideo`, the `libav` and `playback`
  GStreamer plugins, `libavcodec`, `libglib-2.0`, the Mesa GLX driver and
  `libstdc++`.

**Without the system Qt.** All of the system's `libQt6*.so.6` files were moved away, so
the native binary failed to start (missing `libQt6OpenGL`). With them gone:

- the AppImage passed the full X11 suite (104/104 checks, 21/21 effect checks, 7/7
  desk checks);
- it passed the Wayland suite (39/39 checks, 7/7 desk checks).

**Bundled:** 50 libraries and 21 Qt plugins (`docs/results/bundled-*.txt`).

- No GStreamer, GLib, Wayland client, GL/EGL, DRM or GBM library is in the bundle. The
  build script enforces this.
- The highest symbol versions required are GLIBC_2.38, GLIBCXX_3.4.30 and CXXABI_1.3.13.
  Bazzite and Fedora 39+ meet them; Ubuntu 22.04 does not.

### Found and fixed during this round

1. **Stale picture in desk mode.** With the regular view hidden, opening a new file kept
   showing the previous file's last frame, and the automation read frame information
   from the hidden view.
2. **Shadow drew as a hard-edged slab.** Two causes: a short, dense falloff, and, on
   X11, the window shape clipping it. The shadow is now soft, and the X11 mask includes
   it (on Wayland the mask only affects input).
3. **AppImage: no picture at all on Wayland.** Qt's Wayland EGL client integration
   plugin wasn't deployed. It's now copied in explicitly, and the build fails if it's
   missing.
4. **AppImage: GLib and Wayland libraries slipped past the exclusion list,** along with
   GLib's own dependencies. A post-deploy sweep now removes them, and the build fails if
   GStreamer is ever bundled.
5. **Wrong glibc requirement.** I first stated glibc 2.34 for the AppImage. The measured
   requirement is 2.38.
6. **Test-harness problems.** A `pkill -f` pattern matched its own shell and killed it.
   A normal window stood in for the desktop and rose above the set when clicked, which
   invalidated the first real-input run. The harness also reported "0 checks, 0 failed"
   as success when the player never started; that now fails.

### Not tested in 1.2

- **KDE Plasma / KWin itself,** including how KWin treats a fullscreen translucent window
  with an input region. Tested with Weston (Wayland) and Openbox + picom (X11).
- **Real input on Wayland.** It was tested on X11 only; xdotool can't drive Wayland.
- **Seeing transparency on Wayland.** On Wayland I checked the window's alpha channel,
  not a composited screenshot; headless Weston has no screenshot tool here.
- **Steam Game Mode (gamescope),** multi-monitor setups and HiDPI scaling.
- **GPU frame times,** for both desk mode and the flight. Only software rendering is
  available here.
- **The AppImage on a non-Ubuntu host.** Portability was checked by removing the system
  Qt and inspecting the required symbol versions, not by running it on Fedora or
  Bazzite.

## Tested vs. intended

| Behaviour | Status |
|---|---|
| 4:3, 16:9, vertical, rotation-tagged, ultrawide and anamorphic playback with correct aspect | **Tested** (numbers above) |
| Original / Fit / Fill / Crop, crop to aspect, bars excluded or included | **Tested** |
| Named presets (11), bypass, before/after split | **Tested** |
| VHS tape, composite / LaserDisc effects, six scanline styles | **Tested** on test patterns (measurements above); not judged on real footage |
| Five emulator scanline looks (geom/lottes/easymode/hyllian/MAME HLSL-style) | **Tested** for their intended beam behaviour (measurements above). **Not compared** with the original shaders |
| Lower resolution (bigger pixels): rows/columns, hard/sharp/soft, scanline lock, full-resolution bypass/original | **Tested** (measurements above); desk-mode blocks checked visually only |
| Resume, recent files, external subtitles (local + Jellyfin), speed, A–B loop, chapters, seek previews, system report | **Tested** (measurements above) |
| MPRIS, game controllers, Steam Game Mode, room backdrop | **Tested** with playerctl, SDL's virtual controller and a simulated gamescope. **Not tested** with real gamescope, pads or KDE's widget |
| Arcade cabinet: marquee with the video's title, controls, coin door, four art styles, vertical games | **Tested** (measurements above); look judged by eye |
| The look's sound: tape hiss, wow and flutter, saturation, treble loss, TV speaker, film crackle | **Tested** (measured in a unit test; switched by looks in the player); not listened to |
| CG room object sets, your 3D models (OBJ/STL statues with shadows and floor reflections), round stars | **Tested** (measurements above); not in the spheres' reflections |
| 90s CG room (mirror floor, palettes, ray-traced objects and reflections, shadows, stands, tile reveal, sweeping fly-in, orbit) | **Tested** (measurements above); look judged by eye; GPU speed not measured |
| Movie theater (curtains, house lights, masking, seats, beam, haze), film projection looks, wall placement | **Tested** (measurements above); look judged by eye |
| Wall-mounted TV scene: mounts, wall finishes, framed pictures with picture lights, scene previews, camera floor limit | **Tested** (measurements above); look judged by eye |
| Desk-mode scenes: desk (planks, wall, moods, the picture lighting the room, clear-coat reflection), fog, scene settings | **Tested** (measurements above); look judged by eye; GPU speed not measured |
| AppImage Qt selection (system Qt preferred); composited-screen check; keep the screen awake | **Tested** here (Weston, stand-in D-Bus services). **Not verified** on Bazzite/KWin |
| Other distributions (install hints, missing plugins, audio fallback, AppImage without libproxy) | **Tested** by simulation (os-release files, plugins removed, libproxy hidden). **Not tested** on real installations |
| Persistence, colour depth + dither, PAL, set moments (power, static, VCR text) | **Tested** (measurements above); not compared with real hardware |
| Jellyfin: sign-in, browse, search, stream, seek, resume, progress sync, sign-out, HTTPS | **Tested against a mock server** built from the public API (10.8 and 10.10 routes). **Not tested against a real Jellyfin server** |
| Desk mode: 3D set, transparency, click-through, rotate/move/zoom, fly in/out, pixel-identical landing, screen shapes, context menu | **Tested:** scripted on X11 and Wayland; real input and visible transparency on X11 with picom only. **Not tested on KDE/KWin or gamescope** |
| AppImage: own Qt, host GStreamer/GLib/drivers | **Tested** on Ubuntu 24.04 with the system Qt removed (X11 + Wayland suites). **Not run on Bazzite/Fedora** |
| Seeking, scrubbing, frame step forward and back | **Tested** |
| Audio and subtitle track selection | **Tested** (MKV with SRT) |
| Fullscreen with auto-hiding controls, X11 and Wayland | **Tested** (Xvfb + Openbox, headless Weston) |
| Persistent settings, preset save/rename/delete/import/export logic | **Tested** (preset operations through unit tests, not through their dialogs) |
| Missing-codec error naming the capability | **Tested** (H.265) |
| Screenshots, original and filtered | **Tested** |
| Keyboard shortcuts | **Tested** with real key events (Space, B, [, E, L, F, Esc, →, M, A, J, Z, C) on a build from shortly before the final layout fixes. The shortcut code did not change afterwards |
| Playlist auto-advance, next/previous | **Partly tested**: the playlist fills and plays; end-of-file auto-advance was not run through a whole sequence |
| Drag-and-drop from a file manager | **Intended, not tested.** The drop handler is written, but no real drag was performed; Xvfb has no file manager |
| File dialogs for Open, Import and Export | **Intended, not tested** (interactive dialogs) |
| **Hardware decoding** (VA-API/NVDEC) and the automatic software fallback | **Intended, not tested.** No GPU here. The rank policy ran, and only Vulkan decoders were registered, which it deliberately leaves alone |
| Full-frame-rate playback and sync on a real GPU | **Intended, not tested** (see the numbers above) |
| Running the prebuilt binary on Bazzite | **Intended, not tested.** The binary needs only glibc ≥ 2.34 and Qt 6.4 public symbols (checked with `objdump`), and uses no private Qt API. `scripts/build-bazzite.sh` was written but also not run here |
| Audible output, volume slider | **Not verifiable** (null sink). The volume path is GStreamer's standard `GstStreamVolume` |
| HDR tone mapping, interlaced sources, network streams | **Not implemented / not tested.** HDR is shown as SDR without tone mapping. Deinterlacing relies on playbin's deinterlace flag and is untested |

## Reproducing

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build && ctest --test-dir build
scripts/make-test-media.sh /tmp/crt-media
scripts/run-verification.sh build/crtplayer /tmp/crt-media /tmp/crt-results      # X11 or current session
QT_QPA_PLATFORM=wayland RUN_WAYLAND_ONLY=1 scripts/run-verification.sh build/crtplayer /tmp/crt-media /tmp/crt-wl
```

The logs and a `summary.md` are written to the results directory.
