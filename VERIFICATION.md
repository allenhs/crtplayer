# Verification report

This report covers the build delivered alongside it (CRT Player 2.15.0).

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

**The checks in this report were not run on Bazzite, on a real GPU, or with hardware
decoding.** (The player's user runs it on Bazzite with an NVIDIA card; faults found there
are reported in the sections of the releases that fixed them, and the one part of 2.15
that needs an NVIDIA card was run there and is reported in that section.)

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

## 2.15: NVIDIA AI for Enhance (Video Super Resolution, Video Frame Generation); an A-B loop that stopped at the video's end

Asked for: NVIDIA's AI upscaling and frame generation as an optional part of Enhance, for
an RTX card on Bazzite, with the built-in methods where NVIDIA's are not there.

### What could be tested here, and what could not

**This machine has no NVIDIA card, and NVIDIA's SDK cannot run on it.** Everything below
under "Results" was done with a **stand-in for the SDK** (`tests/nvfx_mock`): two small
libraries with the SDK's names and the functions the helper calls, whose "super
resolution" is a plain enlargement and whose "frame generation" is a plain mix of the two
frames. The player, its helper program, the traffic between them, the shared memory, the
textures and every failure path are the real ones. **What the stand-in cannot show is
anything about NVIDIA's models or NVIDIA's libraries themselves:** that part was run by
the user on the machine it was written for, and is reported under "On the real card".

### What was built

**A helper program, `crtplayer-nvfx`** (`tools/nvfx`, no Qt; in the AppImage and beside
the plain binary). The player does not load NVIDIA's libraries: the helper does, so that a
fault in them cannot take the player down, and so that they meet nothing of the player's
Qt or the AppImage's bundle.

- It opens the SDK at run time from wherever it is installed (`NvProxy.cpp`, as NVIDIA's
  own samples do): nothing of the SDK is needed to build, and nothing of it is in the
  source or the downloads. It starts itself a second time with the SDK's folders on the
  library path. Its C++ runtime is linked in; it needs only the C library.
- `--serve`: at work for the player (below). `--where`: where the SDK is and which
  features it has, without loading it. `--probe`: both effects on test pictures at
  several sizes and settings, with the time for each picture, how close the result is to
  the true picture, and crops to look at.
- **The two effects together** (`NvPipeline` in `NvFx.cpp`): the frame is upscaled first
  and frames are generated at the larger size. Super resolution then sees only real,
  consecutive frames, and a generated picture costs one step instead of two. The upscaled
  frame is handed to frame generation on the card; if the card refuses that, it goes
  through ordinary memory instead (decided by a trial run on blank pictures when the
  effects are opened, which also catches a fault there and not on the first frame).

**Between player and helper** (`src/render/NvEnhancer.*`, `tools/nvfx/NvShm.h`): one
socket carrying lines of text, and a block of shared memory for the pictures (the video's
frame in, two picture slots out). Answers come in the order of the requests, so the
player may ask before it has read the last answer.

- Starting the helper and opening the effects (a few hundred milliseconds) never hold
  the player up: the draw is done with the built-in methods until they are ready.
- A helper that does not answer within a second and a half is stopped. A helper that has
  stopped is started again after three seconds, four times at most. A set of effects that
  could not be opened is tried again after five seconds, three times at most. A new video
  or a changed setting clears these counts.
- A window being resized asks for a new size at every draw: the effects are opened for
  the size it settles at (a quarter of a second without change).
- What the helper and NVIDIA's libraries print goes to `~/.cache/CRTPlayer/CRTPlayer/nvfx.log`.

**In the renderer** (the last part of `src/render/Enhance.cpp`): each of the video's
frames is read back from the picture texture and given to the helper; its pictures are
put into the textures the built-in methods would have filled.

- Effects off and the picture enlarged: the upscaled picture comes from the helper whole,
  at the draw's moment between two frames when smooth motion is on.
- With a look, or with nothing to upscale: frames between come from it at the video's
  size, and everything downstream works from them as from any frame.
- **While the video plays with smooth motion, a draw shows the picture asked for at the
  draw before** and asks for its own, which the next draw shows. The helper then works
  while the player draws instead of the player waiting for it; the price is one refresh
  of the screen of delay (8 ms at 120 Hz), which is not added to the sound's delay. A
  draw on its own (paused, a screenshot, draws more than 50 ms apart) asks and waits.
- Whenever the helper has no picture to give, that draw is done by the built-in methods.
- Super resolution is asked for the size the picture is shown at, by the same factor in
  both directions (at most four times), in even numbers.

**In the settings** (*Playback → Enhance*): *Use NVIDIA AI for both (RTX graphics cards)*,
on by default; *AI upscaling* Low / Medium / High / Ultra (default High); *AI motion*
Fast / Balanced / Best (default Balanced); and a line that says what is going on (not
installed; ready; at work, with the sizes, the milliseconds a picture and a frame cost,
and the pictures drawn a second; or why it could not be used). The same line is in the
**I** overlay. The two boxes above still turn upscaling and smooth motion on and off.

### On the real card

Run by the user: Bazzite (`bazzite-nvidia-open`, KDE, Wayland), RTX 4090, driver
615.71.09, a 3840×2160 screen at 120 Hz, NVIDIA Video Effects SDK 1.3.0 installed in an
Ubuntu box and copied to `~/.local/share/crtplayer/VideoFX`.

**`crtplayer-nvfx --probe`** ran all 22 cases, inside the box and on Bazzite itself with
the same times. A test scene whose true pictures are known; times for one picture, going
in from and coming back to ordinary memory:

| Video Super Resolution | Time | Against the true picture (a plain enlargement) |
|---|---|---|
| 1920×1080 to 3840×2160, High | 5.2 ms | 40.8 dB (36.0) |
| the same, Low / Ultra | 3.8 / 5.9 ms | 38.3 / 40.7 dB |
| the same, NVIDIA's two "streaming" settings | 4.1 / 7.1 ms | 31.2 / 29.9 dB: **worse than plain** on this scene; not offered in the settings |
| 1280×720 to 2560×1440, and to 3840×2160, High | 2.4 / 3.9 ms | 37.0 (30.3) / 35.5 (30.6) dB |
| 720×480 three and four times, 640×360 three times, High | 1.6 / 2.3 / 1.1 ms | 25.2 (26.1) / 25.1 (25.9) / 24.5 (25.1) dB: **slightly below plain** on this scene |

| Video Frame Generation, one generated picture | Fast | Balanced | Best | Against the true in-between (a plain mix 18.1 dB) |
|---|---|---|---|---|
| 1920×1080 | 1.6 ms | 2.2 ms | 7.0 ms | 24.7 / 22.4 / 25.0 dB |
| 1280×720 | | 1.5 ms | 6.4 ms | 22.3 / 25.3 dB |
| 3840×2160 | 5.6 ms | 6.1 ms | 11.1 ms | 24.1 / 22.4 / 24.6 dB |

The crops were looked at: the generated frame has the moving disc cleanly halfway, where
the plain mix shows it twice; the upscaled stripes are sharper than the plain ones.
On this scene the Fast model was closer to the true picture than Balanced; the scene is
synthetic (flat shapes on a moving background), so nothing is concluded from that for
real video, and NVIDIA's default (Balanced) is the player's.

**In the player** (a test build of this release, from its AppImage), the user's report:
"it works". The line in the settings at that moment: *NVIDIA AI at work: upscaling
576×1024 to 1032×1836 (high), smooth motion (fast). 2.8 ms a picture, 0.8 ms a frame of
the video. 107 pictures a second on a 120 Hz screen.* That one report shows, on NVIDIA's
real libraries: the helper found and started from the AppImage on Bazzite; both effects
together, upscaled first and generated at the larger size; an enlargement that is not a
whole number (1.79 times); and pictures taken from one draw to the next.

**Not shown by it:** 107 of 120, so about one refresh in nine went without a new picture
there, and why is not known (the settings panel was open; the time per picture was a
third of what a refresh allows). 1080p to 4K at full screen, and the Balanced and Best
models at 4K, were measured only by the probe, not in the player. How NVIDIA's pictures
look on real films was judged by the user's eye on NVIDIA's own sample programs ("sharper
and smoother"), not measured.

### Results

**The helper alone** (`tests/nvfx_serve_test.py`, run by `ctest` as `nvfx`, against the
stand-in): 48 checks, all passed. Among them:

| Check | Result |
|---|---|
| `hello`: the SDK's version and both features | PASS |
| Super resolution alone: the picture at the larger size, channels in place, rows packed, the next slot untouched | PASS |
| Frame generation alone: a quarter of the way between two frames; at 0 the frame before and at 1 the newest, as they are | PASS |
| The pair moves on with each frame; across a cut nothing is generated (the nearer real frame) | PASS |
| Requests sent together are answered in order, each with its own picture | PASS |
| Both: generated between two upscaled frames, on the card; the same through ordinary memory when the card refuses | PASS (`card=1`, `card=0`) |
| An effect that cannot be loaded: refused with the reason, and the helper is still there for what does work | PASS |
| Sizes out of range, a size the effect refuses, shared memory that is not there, an unknown request | PASS: each refused, none fatal |
| The helper dying shows as a closed connection; it leaves when the player's end closes | PASS |
| No SDK, and an SDK that cannot be loaded: it says why | PASS |

**In the player** (`tests/automation/nvidia*.txt`, `scripts/check-nvidia.py`; the stand-in
stamps the top left corner of what it makes: super resolution a red square and a blue
one, frame generation a green one below): 42 checks, all passed in every full run
below. Among them:

| Check | Result |
|---|---|
| A 960×540 video shown at 1920×1080: the effects are opened for exactly that | PASS |
| The picture on screen is the helper's: its stamp in the top left corner, the right way up, in the right colours | PASS |
| Away from the stamp it is the same frame as the built-in upscaler shows | PASS: mean difference 2.6 of 255 |
| Paused, each frame is sent once and its picture taken once | PASS |
| Switched off, the built-in upscaler is back and the helper is gone; on again, it is back | PASS |
| Another quality opens the effects anew; a CRT look is left alone | PASS |
| Between two frames: the stand-in's mix with its stamp; a quarter of the way is a quarter; at 0 and 1 the real frames, exactly | PASS: largest differences 0 |
| Playing, effects off, full screen: upscaled, and frames generated at the larger size | PASS: 960×540 to 1920×1080, about 50 pictures taken, none of the draws without |
| Paused: the real frame, upscaled, not a generated one | PASS |
| Playing with a look, and in a window with upscaling off: frames between at the video's size | PASS |
| Draws that follow one another take the picture asked for at the draw before | PASS: 0.1 ms waited for a picture on average, the helper taking 1.3 ms to make one |
| A 60 frames a second video on a 60 Hz screen: nothing asked of the helper | PASS |
| Switched off while playing, the built-in frame generation carries on; on again, the helper's | PASS |
| The choices are kept from one run to the next | PASS |

Software OpenGL draws too slowly here for draws to count as following one another
quickly, so in the main run they are made to (`CRTPLAYER_NVFX_QUICK_MS=1000`); the fault
runs below use the player as it is.

**Things going wrong** (the same short script, playing full screen with both on, with one
fault each):

| Fault | Result |
|---|---|
| The card cannot hand a picture from one effect to the other | PASS: through ordinary memory; the helper's pictures as before |
| The helper takes 25 ms longer for everything | PASS: still its pictures |
| An effect cannot be loaded; cannot be created | PASS: the reason kept, three tries, then left; the built-in upscaler does the work |
| The helper dies in the middle of the video | PASS: playback carries on with the built-in methods; the helper is started again |
| The helper gets stuck | PASS: stopped after a second and a half; the player's longest pause 2 to 3 s here; playback carries on |
| No SDK; no helper program; `CRTPLAYER_NVFX_OFF=1` | PASS: nothing is started |

No helper process and no shared memory was left behind after any run.

**With AddressSanitizer and UndefinedBehaviorSanitizer** (player, helper and stand-in all
built with them): the helper's 48 checks passed; the player suite ran through with no
report in the new code. One check failed there, *some of them generated*: at that speed
every draw falls on a real frame. Two reports in older code were fixed (below).

**The AppImage's helper** is found and used both with the system's Qt (the player then
runs from another folder of the AppImage) and with the bundled Qt: the suite passes with
each.

**Full suites for 2.15.0:**

| Run | Result |
|---|---|
| Unit tests (`ctest`) | 8 of 8 passed |
| Native X11 | 513 passed, **4 failed** (speed, below) |
| Native X11, the path a graphics card takes (`CRTPLAYER_VIDEO_SURFACE=gl CRTPLAYER_FAST_PATH=never`) | 511 passed, **7 failed** (the same four; two NVIDIA checks that were themselves at fault; one Jellyfin check) |
| Native Wayland | 18 passed, none failed |
| AppImage, X11, the system's Qt removed | 512 passed, **5 failed** (the same four; one subtitle check) |
| AppImage, Wayland, the system's Qt removed | 18 passed, none failed |

43 more checks than 2.14: the 42 NVIDIA checks and the loop check.

**The four that fail in every X11 run are the test machine's speed**, as in 2.14: the
speed checks of the no-graphics-card suite run through the OpenGL widget (software
OpenGL). *1080p plays at full rate* 22.6, 19.8 and 20.5 frames a second in the three
runs, *4K* 15.4, 14.2 and 15.1, *fullscreen 1080p* 12.1, 12.0 and 11.2, *twelve changes
of size and look* 22.8, 21.4 and 22.6; the 2.14 release measured 21.6, 15.8, 11.3 and
21.5, and the 2.13.0 binary run beside it the same. The same checks on the default path
without a graphics card pass at 28 to 30 frames a second in every run.

**Two NVIDIA checks failed once, and the checks were wrong, not the player.** In the fault
runs the video goes round in an A-B loop, and the checks required the player to say
"playing" at the moment of the report; while the loop jumps back it says "paused" for a
moment, and in that run two reports fell on such a moment (*a helper that takes 25 ms
longer*, *no helper program*). The checks now ask whether frames kept coming. Evaluated
again on that run's own recordings, all 42 pass; the later AppImage run used the corrected
checks.

**Two failures that are not explained**, each once, in different runs, neither in code
that 2.15 changes (the playback code, `Player`, is as in 2.14):

- *Jellyfin 10.8: a converted video resumes at its saved position*: 2,135 ms, where 12 s
  was saved. The server's log of that run shows the stream fetched from its beginning
  (segments 0 to 6) and no jump. The same check passed in this release's other runs (17.2,
  16.0 and, for 10.10, 17.0, 14.7 and 15.7 s) and in all 40 runs kept from the releases
  since 2.8. The jump to the saved position is asked for once, when the stream first
  reports ready, and a refusal at that moment would go unnoticed: that is the suspect, not
  a finding.
- *With subtitles on, the sidecar subtitle file is loaded by itself and drawn into the
  picture* (AppImage run): the screenshot taken after a jump to 5 s and a pause had no
  subtitle line in it. It passed in the two native runs here and in the 14 recorded runs
  before. The known limit that a line already on screen at a jump comes back only with
  the next line (README, *No subtitles*) produces exactly this if the jump lands a moment
  late; that, too, is a suspect only.

Both were on a machine that was slower than usual during these runs (the speed checks
above). They are listed so that a second sighting is recognised.

### Faults found on the way, and fixed

- **An A-B loop stopped when it reached the video's end** (since 1.8). The loop is closed
  by a look at the position several times a second; with the loop's end at the video's
  end, or the player busy for a moment near it, the end of the video came first:
  playback stopped, paused at the loop's start. Found because the fault runs with a stuck
  helper kept ending "paused at 200 ms". Now the end of the video inside a loop goes
  round as well. A check was added (`polish.txt`: a loop from 0.5 s to the end of a 3 s
  video is still playing after seven seconds); **the 2.14.0 binary fails it** (*paused at
  500 ms*).
- **Two things the sanitizer pointed at in older code**, neither seen to do harm: the
  settings and playlist panels told the main window they were being hidden while the
  window was already being destroyed (they are now disconnected first); and in desk mode
  the size of the glass texture was computed in whole numbers from a rectangle that has
  no sensible size for a moment while the window is set up (now computed with care for
  that).
- **While building this:** a draw that found no fresh picture from the draw before went
  without one, which with slow draws meant most of them (now it asks and waits, and
  leaves a picture for the next); `CRTPLAYER_NVFX_HELPER` naming a missing file fell back
  to the helper beside the player (now: that file or none); the helper needed zlib only
  to write the probe's pictures (now it writes them uncompressed and needs nothing).

### Known limits and what was not tested

- **NVIDIA's models were not run here**, as said above. One report from one card
  (RTX 4090) on one system (Bazzite, Wayland). Other RTX cards, older drivers, X11, other
  distributions, and a card with little memory were not tried. The SDK's own minimum
  (RTX 20 series, driver 570.26) is NVIDIA's statement, not something measured.
- **Keeping up at 4K and 120 Hz is not established.** By the probe a generated 4K picture
  takes 6 ms on that card with the Balanced model (11 ms with Best), and the player adds
  putting it into a texture; a refresh is 8.3 ms. The helper and the player work side by
  side, so it may fit; the one report in the player (107 of 120, at a smaller size) does
  not settle it. The line in the settings shows the figures.
- **Every picture goes through ordinary memory twice** (from the card to the helper's
  side of the shared memory, and from there into the player's texture). Sharing the
  picture on the card between the two programs would avoid that; it was not attempted.
- **The picture runs one refresh later** while NVIDIA's frame generation is at work, and
  the sound is not held back by that much more.
- **Small videos:** on the probe's scene, super resolution of 480p and 360p pictures was
  slightly further from the true picture than a plain enlargement. Whether that holds for
  real DVDs was not examined; the built-in upscaler is one click away.
- **Only what the built-in Enhance covers:** no NVIDIA upscaling with a CRT look, none in
  desk mode, none of the SDK's other effects (denoising, HDR).
- **8 bits per colour.** The pictures exchanged are 8-bit, as the player's picture
  texture is.
- **The installation instructions in the README** are the steps the user followed on
  Bazzite, written down afterwards; they were not followed a second time from the text.
  NVIDIA's download pages and their names can change.
- **Windows:** the player there is built without this (the helper is for Linux).

## 2.14: 3D models: standing upright; PLY, GLB / glTF and FBX; pictures; large models

Asked for, from real use: an OBJ statue lay on its side in the 90s CG room instead of
standing on its flat base; a PLY model did not load at all; then GLB and FBX as well.

**The cause of the first:** the OBJ format does not say which way is up, and the player
took every OBJ as Y-up. The statue (`Terpsichore_Lyran.obj`, 114,472 triangles, written
by Blender) is Z-up: 1.77 high along Z, with a flat base at the bottom covering 80% of its
footprint. **The second:** only OBJ and STL were read; other files in the folder were not
even listed as skipped.

### What was built

**Which way is up** (`ModelLibrary::load`, `flatShares` in `src/render/ModelLibrary.cpp`):

- For each of the six sides of the model's bounding box, the area of the triangles lying
  flat against it (all three corners within 0.5% of the model's extent of that side) is
  measured as a share of the side. A statue's base gives a large share on one side.
- OBJ and PLY are Y-up by convention, STL Z-up. The other of the two axes is taken
  instead when the model is flat along it (a share of 6% or more, at the bottom or the
  top) and not along the usual one (less than a third of that). A flat side on both, or
  on neither, leaves the convention. A table (flat on top, legs below) is stood on its
  legs: the axis is found from the flat top, and up stays up.
- FBX and glTF say which way is up, and are not second-guessed.
- A flat cut-out with no height along the usual axis is stood along the other.
- *Models stand* in the settings chooses the axis by hand (Y, Z, X, up or down) for the
  folder. All six are rotations; none mirrors the model.

**Which way they face:** nothing in a file says which side is the front (this statue's
front faces +Y; Blender's convention would be −Y). The statues now **turn slowly** by
default (9° a second, each 60° ahead of the one before); *Models face* stands them still,
turned by 0°, 90°, 180° or 270°.

**The readers** (`src/render/ModelFormats.cpp`; a model as read is a `RawModel`,
`ModelRaw.h`):

- **OBJ:** read from the file's bytes (it used to go through text lines and string
  splitting); the `.mtl` file's colours and pictures (`Kd`, `map_Kd`, names with spaces,
  options before the name).
- **PLY:** text, binary little-endian and big-endian; any property types; lists; faces of
  any number of corners; triangle strips; normals; colours (also the base colour of a
  Gaussian-splat file). A text file is read a line per element.
- **glTF 2.0 / GLB:** the scene's node tree with its transforms; accessors of every
  component type, interleaved or not; triangles, strips, fans, points; `COLOR_0`; the
  material's base colour and its picture (inside the file, as a data URI, or beside it);
  the older specular-glossiness materials. Draco- and meshopt-compressed files are
  refused with that reason.
- **FBX:** through **ufbx** 0.23.1 (`third_party/ufbx`, unmodified, MIT or public domain),
  which turns the scene Y-up from the axes the file states. Node transforms, normals,
  vertex colours, each material's colour and picture (embedded or beside the file).
- **TGA pictures** are read by the player itself (true colour, grey, colour table; plain
  or packed in runs). Qt's TGA plugin reads only the newer form of the format.
- **Numbers** are read with `std::from_chars`: the same whatever the system's language
  (a decimal comma does not matter).

**Pictures on the models** (`Picture`, `Painter` in `ModelLibrary.cpp`): the statues are
drawn with a colour per triangle corner (the renderer was not changed). A pictured model
of fewer than 150,000 triangles is divided, every triangle alike, into up to 64 steps a
side, and the picture is sampled at each new corner, from a copy reduced to about one
pixel per sample. Points on a shared side are computed identically from both triangles.

**Point clouds:** a PLY with points and no faces. Thinned to 200,000 points while it is
read; points beyond the middle 98% by half its width again are dropped; each point
becomes a square in the surface, sized from the distance to its fourth-nearest neighbour;
normals from the file, or from the ten nearest neighbours (the direction they vary least
in).

**Large models:** a model of more than 400,000 triangles is simplified by merging the
corners that share a cell of a grid (the grid is made as fine as fits), instead of being
skipped. Up to 8 million triangles are read. Without a graphics card the limits are
120,000 triangles, 60,000 points and 30,000 triangles after dividing.

**Normals** when the file has none (or has empty ones): the faces around a corner are
averaged by area, leaving out those across an edge sharper than 60°. Before, everything
was smoothed, boxes included.

**The settings window** lists each model with its size and the axis used and why, and
each skipped file with the reason.

### Results

**Unit test** (`tests/test_models.cpp`, `ctest`): 55 checks, all passed. It writes its
own models. Among them:

| Check | Result |
|---|---|
| A Z-up OBJ is stood on its flat base, and is then the same statue as the Y-up file | PASS: largest difference 0 |
| A Y-up STL (against its convention) is stood on its flat base | PASS |
| No flat side: an OBJ is Y-up, an STL Z-up; a cube stays as it is | PASS |
| A Z-up table stands on its legs, not on its top | PASS: *Z up (by its flat top)* |
| All six axis choices turn the model; none mirrors it | PASS: the volume keeps its sign |
| A flat cut-out is stood on its edge, whichever way it lies in its file | PASS |
| Numbers are read the same with a decimal-comma language | PASS: `de_DE.UTF-8` |
| PLY: text, little-endian and big-endian give the model the OBJ gives | PASS: differences under 0.000003 |
| PLY: quads; triangle strips with a restart; colours; more on a line than the header says | PASS |
| PLY cut short; without a complete header; a face naming a missing vertex | PASS: each refused with its reason |
| A point cloud: a square per point, lying in the surface | PASS: mean alignment with the true normal 0.990 |
| 300,020 points are thinned; 20 strays far away do not shrink the model | PASS: 150,000 points, 1.4 × 1.4 × 1.4 |
| GLB: a Z-up mesh set upright by its node's turn, scale and move | PASS |
| GLB: colours per vertex; a material's colour; a picture | PASS |
| A cone of 48 triangles with a picture half blue, half red: divided, the change is where the picture has it | PASS: drawn with 145,200 triangles; 146,808 samples away from the change are pure blue or red, none blended |
| The divided triangles meet exactly | PASS: no point that only one of two neighbours uses (hundreds, with the side points computed the plain way: tried) |
| GLB: a first chunk not padded to four bytes | PASS |
| GLB: Draco- and meshopt-compressed files; a damaged file | PASS: each refused with its reason |
| FBX (text): a file that says Z is up, and one that says Y | PASS: both upright |
| FBX: its material's colour | PASS: 0.8 0.1 0.1 |
| TGA: true colour and grey, plain and packed, rows from the bottom or the top | PASS |
| An OBJ's TGA picture is wrapped the right way up | PASS |
| A box keeps its edges; a ball is smooth | PASS: least alignment 0.995 |
| 500,996 triangles are simplified, and keep their shape | PASS: 294,804 triangles, within 0.0001 of the true surface |
| Without a graphics card the limits are lower | PASS: 117,930 triangles; 59,997 points; the pictured cone 30,000 triangles |

**In the player** (`tests/automation/cg.txt`, `scripts/check-cg.py`; test models from
`scripts/make-test-models.py`):

| Check | Result |
|---|---|
| A Z-up OBJ is stood on its flat base | PASS: a_zup.obj: 1,280 triangles, Z up (stands on its flat base) |
| In the room it stands taller than it does lying on its back (the axis chosen wrongly by hand) | PASS: top of the statue at row 484 standing, 585 lying; lying: a_zup.obj: 1,280 triangles, Y up (chosen), 0.98 high |
| Statues turn slowly unless set to stand still | PASS: 7455 pixels change in 10 s turning, 0 standing still |
| PLY models load: text, binary little-endian, binary big-endian (triangle strips) | PASS: b_text.ply: 600 triangles, Y up (stands on its flat base); c_little.ply: 1,280 triangles, Z up (stands on its flat base); d_big.ply: 1,920 triangles, Y up (as such files usually are) |
| Each is stood the right way up (Z-up files on their flat base; a table on its legs) | PASS: a_zup.obj Z, b_text.ply Y, c_little.ply Z, d_big.ply Y, e_points.ply Z, f_table.obj Z |
| A PLY of points alone is shown as a point cloud (thinned, strays dropped) | PASS: e_points.ply: 59,993 points of 300,040, Z up (stands on its flat base); 1.05 wide, 1.05 deep |
| A PLY cut short is skipped, with the reason | PASS: a0_cut.ply: the file is damaged or cut short |
| With "Their own colours" the PLY models show their colours | PASS: 25575 strongly coloured pixels |
| GLB and glTF models load (nodes, a picture inside the file, data in a file beside it) | PASS: a_nodes.glb: 1,280 triangles, Y up (stands on its flat base); b_textured.glb: 600 triangles, Y up (stands on its flat base); c_external.gltf: 14 triangles, Y up (stands on its flat base) |
| FBX models load (text and binary), upright whichever axis the file says is up | PASS: e_text.fbx: 1,280 triangles, Y up (stands on its flat base); f_binary.fbx: 600 triangles, Y up (stands on its flat base) |
| They show their colours (vertex colours, pictures, materials) | PASS: 81342 strongly coloured pixels |
| An OBJ with a picture (a TGA named by its material file) is divided so that the picture shows | PASS: g_crate.obj: 12 triangles, Y up (stands on its flat base); drawn with 30,000 triangles |
| A Draco-compressed GLB is skipped, with the reason; the data file beside a glTF is not taken for a model | PASS: d_draco.glb: Draco-compressed (not supported; export it without compression) |
| A model of 1.2 million triangles is simplified, not skipped | PASS: a_dense.stl: 110,064 triangles (simplified from 1,202,025), Z up (stands on its flat base) (the limit here: 120,000, without a graphics card) |
| A cloud of a million points is thinned | PASS: b_cloud.ply: 60,000 points of 1,000,000, Y up (stands on its flat base) |
| Both load in the background within seconds | PASS: 0.7 s |
| And are drawn | PASS: 5.9% of the view changes |
| The chosen axis and facing are kept | PASS: up Z, facing 3; a_zup.obj: 1,280 triangles, Z up (chosen) |

**The model that was reported:** loads in 0.07 s as *114,472 triangles, Z up (stands on
its flat base)*, 0.68 × 1.50 × 0.53. In the room it stands; with *Y is up* chosen by hand
(what 2.13 did) it lies on its back. Looked at in screenshots.

**Files from elsewhere** (not in the repository; loaded with the same code, outside the
player):

| Files | Result |
|---|---|
| ufbx's own test files: 690 FBX, text and binary, versions 3000 to 7700, from Blender, Maya, 3ds Max, MotionBuilder, Revit and others | 570 load; 76 hold no faces (cameras, curves, animation only), 27 are flat, 12 are deliberately broken or of a version ufbx does not read, 5 hold numbers that are not positions; 2.1 s for all |
| trimesh's test models: 32 OBJ, 26 STL, 25 PLY, 15 GLB, 4 glTF | 97 load; the 5 refused hold no faces or nothing at all |
| Khronos glTF sample assets: 19 (Duck, DamagedHelmet, Fox, Avocado, Corset, WaterBottle, Lantern, ABeautifulGame of 1.5 million triangles, …) | 17 load; the Draco and meshopt variants are refused with that reason |

Two faults were found with these files and fixed (below). Six of the Khronos models and
six others were looked at in the room with *Their own colours*: upright, pictures the
right way round.

**Damaged files** (`tests/fuzz_models.cpp`, built with AddressSanitizer and
UndefinedBehaviorSanitizer): 21,300 files, made from 23 of the test models and pictures (OBJ, STL, PLY, GLB, glTF, FBX,
TGA) by changing bytes, cutting them short, and removing or repeating runs. 7,671 still
loaded, 13,629 were refused, none crashed, and the sanitizers reported nothing in the
player's code. (8 reports inside ufbx; see the limits.)

**Full suites for 2.14.0:**

| Run | Result |
|---|---|
| Unit tests (`ctest`) | 7 of 7 passed |
| Native X11 | 470 passed, **4 failed** (speed, below) |
| Native X11, the path a graphics card takes (`CRTPLAYER_VIDEO_SURFACE=gl CRTPLAYER_FAST_PATH=never`) | 470 passed, **4 failed** (the same four) |
| Native Wayland | 18 passed, none failed |
| AppImage, X11, the system's Qt removed | 470 passed, **4 failed** (the same four) |
| AppImage, Wayland, the system's Qt removed | 18 passed, none failed |

**The four failures are the test machine's speed that day, not this release.** They are
the speed checks of the no-graphics-card suite run through the OpenGL widget (software
OpenGL): *1080p plays at full rate* 21.6 frames a second, *4K* 15.8, *fullscreen 1080p*
11.3, *twelve changes of size and look* 21.5; the 2.13 release measured 29.2, 24.8, 17.0
and 29.9 on the same checks. **The 2.13.0 binary, run again within the hour of the 2.14
runs, gave 22.0, 13.8, 11.4 and 18.0** and failed the same four checks; the comparison
measurements the suite makes of the unchanged older path fell by the same quarter
(13.3 to 10.0 frames a second). The same checks on the default path without a graphics
card (no OpenGL) passed at 29.4 to 30.3 frames a second in every run. Nothing in 2.14
touches video playback.

### Faults found on the way, and fixed

- **A text PLY whose lines hold more than its header says** (`fuze_ascii.ply` from
  trimesh's models: 10 values after each face where the header declares 4) was read as
  one stream of numbers, so every face after the first was garbage. Each element is now
  read from its own line.
- **A GLB whose first chunk is not padded to four bytes** (`cube.glb`, `pins.glb`): the
  reader stepped to the padded position and missed the binary chunk. The chunk's length
  is now taken as it is.
- **A flat cut-out lying in the plane of the "up" axis** was refused as *flat model*; and
  the first version of the flat-base rule laid a square in the XY plane down for the same
  reason. Now stood along the other axis.
- **FBX and glTF were second-guessed** in the first version: 23 of ufbx's test scenes
  (sheared boxes, a wall) were turned on their side. Their stated axis is now kept.
- **The mirror image of a point cloud was three times too bright:** mirror images are
  added onto the floor, and a cloud's squares overlap about three deep. Scaled down.
- **meshopt compression under its newer name** (`KHR_meshopt_compression`) was reported
  as *no faces*.

### Known limits and what was not tested

- **No graphics card here.** The room draws at 4 frames a second at 1080p on this
  machine without models, and at 2 with a million triangles of them (measured before the
  lighter limits were added). How it runs on a real card was not measured. The limits for a graphics card (400,000 triangles, 200,000 points,
  150,000 after dividing) were exercised by the unit test only; in the player here the
  lighter limits apply.
- **The flat-base rule can be wrong.** A Y-up model with a flat back and no flat base (a
  mask, a wall plaque) is laid on its back. A Z-up model without any flat side (a figure
  standing on its feet) is taken as Y-up and lies down. *Models stand* corrects both, for
  the whole folder at once: models that need different choices need different folders.
- **Pictures are sampled, not drawn.** A model of a dozen triangles shows its picture at
  64 samples a side; lettering on it is not readable. Only the base colour picture is
  used: no normal maps, roughness, transparency or emission. Colours and pictures are
  multiplied as glTF says; FBX and OBJ pictures are used as they are.
- **Not read:** Draco- and meshopt-compressed glTF; KTX2 pictures; sparse accessors;
  morph targets, skins and animation (a character stands in its rest pose); a second set
  of texture coordinates; PLY pictures (`comment TextureFile`); OBJ free-form surfaces.
- **Gaussian-splat PLY files** are read as plain coloured points. Their scenes are not
  statue-shaped, and their "up" is anyone's guess.
- **Point clouds without normals** get them from their neighbours; in thin parts (two
  surfaces closer together than the point spacing) the squares can lie askew.
- **Simplifying** merges corners by a grid: fine detail smaller than a cell is lost, thin
  walls can collapse, and the result has between 60% and 100% of the limit. The pictures
  of a model that is simplified become corner colours first.
- **Undefined shifts inside ufbx:** with damaged FBX files the sanitizer reports shifts
  by too many bits in ufbx's decompressor (8 among 2,400 damaged FBX files). No bad read
  or write followed. ufbx was left unmodified.
- **Real FBX and GLB files from modelling programs' current versions** were not tried
  beyond the collections above. **The reported PLY file itself was not available;** PLY
  was tested with generated files and the 25 from trimesh.
- **Windows:** the unit test runs in the Windows build; the room was not looked at there.

## 2.13: Enhance (sharper upscaling, frame generation); a video that never ended

Asked for: something like DLSS upscaling and frame generation for video, on an NVIDIA
card. DLSS itself works only inside games, and NVIDIA's AI video upscaler was not
available on Linux when this was written, so 2.13 builds both as ordinary shaders that
run on any graphics card. Neither is an AI model.

### What was built

**Sharper upscaling** (`shaders/enh_upscale.frag`, `enh_sharpen.frag`,
`CrtRenderer::upscaledPicture` in `src/render/Enhance.cpp`):

- With effects off, when the whole picture is shown at least 15% larger than the video,
  it is rebuilt at the size it is shown at (not more than 4 times the video, nor much more
  than the screen can show): a 16-tap Lanczos-2 reconstruction, its overshoot held to
  80% within what the four nearest source pixels show.
- Then a contrast-adaptive sharpening pass at that size (the method of AMD's CAS, written
  independently): each pixel is sharpened by as much as its neighbourhood leaves room
  for. *Sharpness* 0 switches the pass off.
- Made once per frame and size. A CRT look, an original-frame screenshot and a picture
  that is not enlarged are not touched.

**Frame generation** (`shaders/fi_luma.frag`, `fi_flow.frag`, `fi_blend.frag`,
`computeFlow` / `renderBetween` in `Enhance.cpp`):

- **The frame before is kept.** When a new frame arrives, the two picture textures
  change places; nothing is copied.
- **Motion search,** once for each pair of frames, in both directions: on copies of the
  two frames 480 pixels wide, in colour, coarse to fine over 5 levels. The coarsest level
  (30 wide) searches 3 texels each way (a tenth of the picture's width per frame). Each
  finer level starts from the level before, trying its own vector and its four
  neighbours' (so the edge of a moving thing can take the motion of the side it belongs
  to) and one texel around each; the finest level searches at half texels. Standing
  still is always tried. The match is the mean colour difference over a 3×3 window.
- **The picture in between,** once for every refresh of the screen, at the video's own
  size: for each pixel the motion found from the frame before, the motion found from the
  frame after, standing still, and (up to 1080p) the four surrounding vectors of each
  field one by one are tried; the one whose two fetches (from the frame before, and from
  the frame after) agree best is used, and the two fetches are mixed by the phase.
  Where even the best does not agree, the nearer real frame is shown as it is.
- **A cut:** when the match over the whole picture is poor (measured: 0.005 between
  frames that follow each other, 0.08 to 0.10 across a cut; the threshold is 0.035 to
  0.06), the nearer real frame is shown everywhere.
- **Timing** (`VideoWidget::paintGL`): the phase is how far the pipeline's clock has gone
  from the newest frame's time towards the next, so a draw shows the picture between the
  frame before and the newest frame. The picture therefore runs one frame behind, and
  the sound is delayed by one frame's time (`Player::setPictureLatencyMs`). The widget
  redraws on every buffer swap.
- **Where the generated picture goes:** for one draw it takes the place of the picture
  texture, so the looks, the glow and the upscaler work from it as from any frame.
- **Not generated:** paused; across a jump or a gap of more than 130 ms; when the video
  has as many frames a second as the screen shows (within 25%); in desk mode; while a
  GIF is made; without a graphics card.

### Results

Both are for a graphics card. The test machine has none: the checks force them on with
software OpenGL (`CRTPLAYER_ENHANCE_FORCE=1`), on the path a graphics card takes (the
OpenGL widget, frames as decoded). **What is measured is the pictures. Speed is not.**

| Check (`tests/automation/enhance.txt`, `scripts/check-enhance.py`) | Result |
|---|---|
| The script ran without a failed step | PASS: all ok |
| A 960 × 540 video shown at 1920 × 1080 is upscaled to exactly that | PASS: 1920 × 1080 |
| The plain picture has lost much of the original's fine detail; the enhanced one has most of it back | PASS: 62% of the original's fine detail plain, 97% enhanced |
| And it is closer to the original, not just sharper | PASS: 29.73 dB against 29.20 dB plain (brightness, against the full-size original) |
| Sharpness: at 0 the picture is only rebuilt, at 1 it is sharpened strongly | PASS: 76% at 0, 97% at 0.5, 122% at 1 |
| An original-frame screenshot is the video's own frame, not the upscaled one | PASS: 960 × 540 |
| A CRT look is left alone (the same picture with the setting on and off) | PASS: largest difference 0 |
| A video that is not enlarged is not touched | PASS: 1080p in a 1280-wide window: 0 frames upscaled |
| The two clips are on the frames meant (30 a second: 1.000 s and 1.033 s; 60 a second: 1.017 s) | PASS: 1033.3 ms and 1016.7 ms |
| A frame generated halfway between two frames is close to the true in-between frame | PASS: 33.1 dB; a plain mix of the two frames 22.7 dB; the frame before 20.3 dB, after 20.6 dB |
| Little of it is clearly wrong | PASS: 0.40% of pixels differ by more than 40 of 255 (a plain mix: 7.0%) |
| At phase 0 it is the frame before, at phase 1 the frame after, exactly | PASS: largest differences 0 and 0 |
| A quarter of the way it is nearer the frame before than the halfway frame is | PASS: against the frame before: 22.9 dB at a quarter, 20.5 dB at half |
| Motion is searched on a small copy of the picture | PASS: 480 × 272 for a 960 × 540 video |
| Across a cut nothing is generated: the nearer real frame is shown | PASS: at 0.3 the frame before (largest difference 0), at 0.7 the frame after (0); the two scenes differ by 53 on average |
| While playing, frames are generated between the video's own | PASS: 4 pairs of frames, 9 frames generated (software OpenGL here: far from full rate) |
| The picture runs one frame behind, and the sound is held back to match | PASS: 33 ms; sound output delayed 33 ms (a 30 a second video) |
| Paused, the real frame is shown | PASS: phase 1 |
| It works with a look too | PASS: 2 more frames generated with Clean Broadcast Monitor on |
| A video with as many frames a second as the screen shows gets none generated | PASS: 60 a second on a 60 Hz screen: generating False, sound delay 0 ms |
| Not in desk mode | PASS: generating: False, sound delay 0 ms |
| Switched off, the sound is not held back | PASS: 0 ms |
| Without a graphics card the enhancements stay off, whatever the settings say | PASS: available: False; the picture is the plain one (largest difference 0) |

The test pictures (`scripts/make-enhance-pictures.py`): a detailed scene of gradients,
hard-edged shapes at every angle and fine texture. The upscaling original is 1920×1080
and its copy 960×540. The motion scene is 960×540: the background pans at 14 pixels a
frame, a flat box moves by (8, 4), another by −6, a textured square by −6 the other way,
and a title stands still; the true in-between frame is taken from the same scene made at
60 frames a second.

### A video that never ended (found in 2.12.1, fixed here)

2.12.1 recorded it as "a subtitle track other than the first selected". The cause is
narrower. **A subtitle track that is switched to after the file has already delivered
that track's last line never reports its end.** playbin's track selector had passed on
the end of the track that was selected at the time, and dropped the other track's lines
and its end; after the switch, playbin waits for an end that will not come again. The
picture and the sound finish, and the video stays "playing" on its last frame.

It needs a file that has delivered all its subtitle lines by the time of the switch: a
short clip (the test clips deliver their two lines in the first second), or the last
minutes of a film. The switch can be the viewer's, or the preferred language applied
when the video opens. A jump afterwards makes the file deliver the track again, which is
why the Cable TV checks, which nearly always jump in, had passed.

What was built:

- **The end of a video is recognised without the message** (`Player`, `m_endWatch`): when
  the picture's stream has ended (seen at the video sink) and the position has stood
  still for a second and a half while playing, the video is over.
- **A track picked by hand is read again from where the video is,** so its next lines
  show (and its end comes). Not done for the preferred language at opening: it would be a
  hitch at the start of every such video; the end watch covers that case.

| Check (`tests/automation/everyday.txt`) | Result |
|---|---|
| A short clip with the preferred subtitle track switched to after its lines were delivered still ends | PASS: track 2 while playing; then paused at 6.0 s of 6.0 |
| A track picked by hand shows its next line (the file is read again from there), and the clip ends | PASS: track 1: 1962 bright pixels of text at 3.8 s; then paused |

Reproduced before the fix on 2.12.1 and 2.11.0 (see 2.12.1 below); the same steps now
end the video.

**Full suites for 2.13.0:**

| Run | Result |
|---|---|
| Native X11 | 456 passed, none failed |
| Native X11, the path a graphics card takes (`CRTPLAYER_VIDEO_SURFACE=gl CRTPLAYER_FAST_PATH=never`) | 455 passed, **1 failed** (a test's timing, below) |
| Native Wayland | 18 passed, none failed |
| AppImage, X11, the system's Qt removed | 455 passed, **1 failed** (a test's timing, below) |
| AppImage, Wayland, the system's Qt removed | 18 passed, none failed |

**The two failures, and a third in the reruns.** All three are checks that read the
player's state at a moment that depends on how fast this machine happened to be; none is
a fault in the player. Each check was corrected, and the two suites they belong to
(everyday, Cable TV) were run again on all three X11 configurations with the delivered
binary and AppImage:

| Failure | What happened | The check now | Rerun |
|---|---|---|---|
| Graphics-card path, everyday: *the sound's language carries over* (720 Hz heard instead of 880) | The level meter's half second, read 1.8 s after tuning in, still held the moment of silence before the preferred sound track is switched in (the switch waits until the programme runs, and the path through software OpenGL is slow to start) | Reads the pitch 3 s after tuning in | Passed on all three |
| AppImage, Cable TV: *turning the TV on … with static until the picture arrives* (no static) | The programme had opened and shown its first picture in the moment between the command and the report, so the static was already gone | Accepts static, or a picture that has already arrived | Passed on all three |
| Rerun on the graphics-card path, Cable TV: *programmes follow one another* | The steps before ran 3.7 s faster than usual, and one look caught a programme that had started late playing its last 2 seconds after the schedule had moved on (as designed: the next programme makes the time up) | Accepts that as a hand-over | Passed (27 of 27) on a further rerun |

Reruns: native X11 71 of 71 (everyday 44, Cable TV 27); AppImage 71 of 71; the
graphics-card path everyday 44 of 44, and Cable TV 27 of 27 after the third correction.

### Faults found on the way, and fixed

- **An original-frame screenshot could still lose a subtitle line** (2.12): the player
  keeps the last four frames as decoded, and a picture shown for a while after several
  more frames had passed (a slow redraw before a pause) was no longer among them, so the
  player fell back to seeking for it. The frame as decoded behind the picture on screen
  is now held from the moment the picture is taken. Seen once in the 2.11 subtitle check
  (*V turns them on*).
- **Thin lines broke up in generated frames** in the first version: among the motions
  tried for a pixel, standing still won ties, and for a thin line on a moving background
  the blurred comparison is nearly a tie. Standing still now has to match clearly
  better. The comparison also blurs less (measured best at one mip level). Together:
  31.3 → 36.5 dB on the first test scene (a pan only).
- **A cut was only half recognised** with the first threshold (set by guess at 0.09 to
  0.14): generated frames across it were a mix. The threshold was set from the measured
  values above; frames across the cut are now exactly the nearer real frame.
- **The first motion test scene had no moving objects:** ffmpeg's `drawbox` does not move
  with time. Remade with overlays, in whole pixels per frame (overlays are placed at whole
  pixels, and a rounded position is not halfway between its neighbours).
- **Test faults:** the no-GPU suite's speed measurement could start before the video was
  under way (one frame as decoded, drawn through OpenGL, counted: *no OpenGL is used at
  all* failed once); the everyday suite's Cable TV steps depended on the time of day (the
  TV's clock is now pinned, as in the TV suite); and its two-track clip had 20 seconds of
  picture under 30 seconds of subtitles (now 30).

### Known limits and what was not tested

- **No graphics card here: nothing about speed is known.** Whether frame generation
  keeps up with a 60 Hz or 144 Hz screen, at 1080p or 4K, on which cards, was not
  measured. With software OpenGL it reaches a few frames a second. The motion search
  costs the same for every video (it works 480 wide); the in-between picture costs one
  pass at the video's size per refresh, with about 60 texture fetches per pixel up to
  1080p and 20 above.
- **The shaders were compiled only by Mesa's compiler.** NVIDIA's, AMD's and Intel's
  drivers have their own; the shaders use plain GLSL 3.30, but they were not run there.
- **Pacing on a real screen was not observed:** the phase is computed from the pipeline's
  clock at the time of drawing, not from the time the picture will reach the screen, and
  the redraw is driven by buffer swaps. Judder or tearing from that could not be seen
  here (Xvfb has no refresh). Variable-refresh screens were not considered.
- **The sound's delay** is set through playbin's `av-offset`; that it is one frame was
  read back, not measured against the picture.
- **Quality was measured on one synthetic scene** with whole-pixel motion. Real film,
  camera noise, motion blur, transparency, rotation and zoom were not tried. The halo
  around the title standing over the panning background is the largest error in the
  test scene.
- **Motion faster than about a tenth of the picture's width per frame** is not searched
  for; such areas show the nearer real frame (a visible step).
- **Generated frames are softer than real ones** (they are fetched between pixels). At
  30 frames a second on a 60 Hz screen real and generated frames alternate.
- **Upscaling was measured on one picture.** Against PIL's Lanczos filter on the same
  decoded frame it is 0.25 dB closer to the original; most of what is lost in the
  half-size video (fine texture, and colour detail at a quarter of the size) no upscaler
  of this kind brings back.
- **Desk mode** has its own renderer and gets neither enhancement.
- **Hardware decoding together with the enhancements** was not run (no hardware
  decoder here).
- **Windows:** not run.

## 2.12.1: hardware decoding with a graphics card (from real use)

**The report:** on a Bazzite system with an NVIDIA card, 2.12.0 showed *Hardware decoder
nvh265dec failed; switched to software decoding* as soon as a video opened. Earlier
versions decoded in hardware there.

**The cause** was 2.12.0's own. To keep GStreamer's single-threaded converter out of the
way, the player's video sink answers the question "what do you accept?" with "any raw
video" (see 2.12 below). The answer was taken from the scaler's template, which also
lists raw video *in any kind of memory*: the scaler can pass such frames on untouched.
Offered that, NVIDIA's decoder kept its frames on the graphics card (CUDA or OpenGL
memory), where the scaler and converter cannot read them. The stream failed, and the
player's fallback switched to software decoding.

**The fix:** the sink now answers with raw video in ordinary (system) memory only, as
the sink of 2.11 did in effect. The decoder copies its frames to ordinary memory, as
before 2.12.

| Check | Result |
|---|---|
| On the reporter's system (Bazzite, NVIDIA): the same video with a test build of 2.12.1 | Reported working: no message, hardware decoding as before |
| The video sink asks the decoder for frames in ordinary memory only (`scripts/check-nogpu.py`, both window surfaces) | PASS: 0 other kinds of memory offered |

**Why the 2.12.0 checks did not catch it:** the test machine has no graphics card and no
hardware decoder, so no decoder there could take up the offer. VERIFICATION for 2.12
said "with a real graphics card nothing changes by default"; that was an assumption, and
it was wrong. The new check reads what the sink offers, which can be done without a
graphics card. It does not replace a run on real hardware: hardware decoding itself
remains **not tested here**, and is confirmed for 2.12.1 only by the report above (one
system, NVIDIA, H.265).

**Also in 2.12.1:** the night-mode check measured its second loud level at the very end
of the test clip (position 12.0 s of 12.0 s), where the result depended on timing; it
failed once in a development run (−14.3 dB instead of −12.0). Both measurements now fall
well inside the loud part. A fault in the test, not in the player.

**Full suites for 2.12.1:**

| Run | Result |
|---|---|
| Native X11 | 431 passed, none failed |
| Native X11, the path a graphics card takes (`CRTPLAYER_VIDEO_SURFACE=gl CRTPLAYER_FAST_PATH=never`) | 431 passed, none failed |
| Native Wayland | 18 passed, none failed |
| AppImage, X11, the system's Qt removed | 429 passed, **2 failed** (below) |
| AppImage, Wayland, the system's Qt removed | 18 passed, none failed |

### The two failed checks, and the fault behind them (not fixed in 2.12.1)

Both are in the 2.11 Cable TV checks of the everyday suite, which, unlike the TV suite,
do not pin the TV's clock: what is on when the test tunes in depends on the time of day
the test runs. In this run it tuned in during a programme's first second.

- *The sound's language carries over (721 and 880 Hz heard)*: the level meter's half
  second included the moment of silence before the preferred sound track is switched
  in. A fault in the test's timing.
- *In TV mode, "at the end of this video" waits for the programme to end, then turns the
  TV off*: **the programme never ended.** This one is a real fault in the player, and it
  is older than 2.12:

**A video that is played from its start to its end without a jump, with a subtitle track
other than the first selected, does not end.** It stays on its last picture, "playing":
the playlist does not go on, and a TV channel stays where it is.

| Reproduced (a 30-second clip with two subtitle tracks, subtitles on) | 2.12.1 | 2.11.0 |
|---|---|---|
| Second subtitle track, played from the start | never ends | never ends |
| Second subtitle track, after one jump (a seek) | ends | not run |
| First subtitle track, played from the start | ends | ends |
| Second *sound* track, subtitles off | ends | not run |

In TV mode nearly every tune-in begins with a jump to where the broadcast is, which is
why the TV checks had always passed: only a tune-in within a programme's first 3 seconds
starts it from its top. Plain GStreamer (`gst-launch-1.0 playbin`) ends the same clip
normally with its first subtitle track; the cause inside GStreamer's track selection was
not established.

It is **not fixed in 2.12.1**, which was held to the hardware-decoder fix. Until it is:
a jump anywhere in the video (or *Next*) gets past it. (Fixed in 2.13, where the cause
turned out to be narrower than described here: see above.)


## 2.12: playback without a graphics card

From real use: in a QEMU / virt-manager guest (Virtio video, no 3D acceleration, 12
virtual CPUs with the host's CPU passed through), HD and 4K videos played slowly even
with effects off.

### Where the time went

Without a graphics card, OpenGL runs on the CPU (Mesa's llvmpipe). Timed per stage on the
test machine (2 cores, a 1920×1080 screen, fullscreen, effects off, 2.11):

| Video | A frame took | Colour conversion | Qt putting the window together | Drawing | Upload and smaller copies | Frames a second |
|---|---|---|---|---|---|---|
| 1080p H.264 | 91 ms | 38 ms | 24 ms | 17 ms | 11 ms | 11 |
| 4K H.264 | 310 ms | 190 ms | 24 ms | 17 ms | the rest | 3.2 |
| 4K HEVC 10-bit | 370 ms | — | — | — | — | 2.7 |

Decoding alone ran at 180, 75 and 45 frames a second. So the decoder was never the
problem: the player's own drawing was, and above all the colour conversion, done by a
shader at the video's full size, on a renderer that computes each pixel one by one.
Two more findings:

- Software OpenGL runs **both sides of every branch** of a shader for every pixel. With
  effects off, the big CRT shader still cost as if its effects were on.
- A window that contains an OpenGL widget is put together by Qt through OpenGL, even
  when the widget shows nothing: 24 ms a frame at 1080p here. The same picture painted
  into a plain widget took 4 ms.

### What was built

- **The video pipeline delivers what the screen needs** (`Player::setOutput`,
  `src/app/FastPath.cpp`). The player's video sink is now *scale → convert → size and
  format filter → the player*. GStreamer's scaler and converter run on all CPU cores
  (`n-threads=0`) with vector code. Scaling comes first, in the video's own format, so
  conversion works on the smaller picture (measured: 59 against 37 frames a second for 4K
  shown at 1080p). Three outputs:
  - *as decoded* (with a graphics card, and wherever the full frame is needed);
  - *converted* (a look without a graphics card: no conversion pass in OpenGL);
  - *converted and scaled* (effects off: exactly the size of the picture on screen; with
    a look: when the video is more than 1.6 times larger than its picture).
- **A window surface without OpenGL** (`GlSurfaceWidget`). Without a graphics card the
  video is a plain widget. With effects off it paints the frame as it arrived: a
  straight copy. With a look it draws the look into an off-screen framebuffer and paints
  that. With a graphics card it is the OpenGL widget it always was.
- **A program of its own for effects off** (`shaders/plain.frag`), for the cases that
  still go through OpenGL.
- **Looks at half size** in a large window without a graphics card (*Look detail*:
  Automatic, Full, Half).
- **Frames converted on the CPU are uploaded as they are**; the smaller copies the
  looks need are only made when a look is on.
- **The full frame stays at hand** (`Player::nativeSample`). While frames are delivered
  converted or scaled, the player keeps the last four frames as decoded: references to
  the decoder's buffers, no copies. An original-frame screenshot is made from the one
  behind the picture on screen. A paused picture that is no longer what is asked for
  (scaled for a window that has since gone fullscreen, say) gives way to it. Nothing is
  sought for, so nothing on screen is lost (a subtitle line, see the faults below). Only
  when that frame is gone does the player seek to the frame's own time.
- **GIF recording, rotated video and the split view** get frames as decoded. **Desk
  mode** gets frames converted on the CPU at the video's own size, the same frames as
  the flat view with a look, so that flying in still lands on exactly the fullscreen
  picture.
- **Playsink's own converter is kept out of the way.** It would have converted (single
  threaded) before the player's sink saw the frames; a query probe on the sink's input
  answers with every format, so frames arrive as decoded.

### Results

Speed on the test machine (the same clips and settings as the table above: fullscreen
at 1920×1080, 2 cores, video at 30 frames a second; the last two rows are from the checks
below):

| | 2.11 | 2.12 |
|---|---|---|
| 1080p H.264, effects off | 11 frames a second (91 ms a frame) | **30** (full rate; 2 ms to draw a frame) |
| 4K H.264, effects off | 3.2 | **30** (full rate) |
| 4K HEVC 10-bit, effects off | 2.7 | 9.3 |
| 1080p, Clean Broadcast Monitor look | 7.5 | 24 (the look at half size) |
| 1080p, Consumer Television look | 2.9 | 12.8 (the look at half size) |
| 4K H.264, Clean Broadcast Monitor look | not measured | 15.6 |
| 1080p in a 1280×800 window, effects off | 17.4 | **30** |
| CPU used for that (of 2 cores) | 1.80 cores | 0.45 cores |

| Check (`tests/automation/nogpu.txt`, `scripts/check-nogpu.py`; run on both window surfaces) | Result |
|---|---|
| The script ran without a failed step | PASS: all ok |
| The window surface is the one asked for | PASS: raster |
| Software OpenGL is recognised | PASS: llvmpipe (LLVM 20.1.2, 256 bits), OpenGL 4.5 core (Mesa) |
| Effects off: frames arrive converted, at exactly the size they are shown at | PASS: 1262 × 710 for a picture of 1262 × 710 (the video is 1920 × 1080) |
| 1080p at 30 frames a second plays at full rate | PASS: 30.0 frames a second (17.4 the old way) |
| Drawing a frame takes a fraction of the time | PASS: 0.6 ms a frame against 54.1 ms the old way |
| No OpenGL is used at all with effects off | PASS: 0 OpenGL draws |
| It also costs less CPU | PASS: 0.45 cores busy against 1.80, of 2 |
| The picture is the same as the old way shows (same frame, paused) | PASS: mean difference 1.65 of 255, 0.08% of areas differ visibly |
| An original-frame screenshot is still the full video frame, identical either way | PASS: 1920 × 1080, largest difference 0 |
| After the screenshot the fast frames are asked for again | PASS: rgb scaled |
| With the fast path set to Never, frames stay as decoded | PASS: as decoded, 1920 × 1080 |
| 4K at 30 frames a second | PASS: 30.1 frames a second (4.2 the old way), scaled to 1262 × 710 on the CPU |
| 4K shrunk on the CPU shows the same picture | PASS: mean difference 1.87 from the old way, 0.25% of areas differ visibly |
| And it is shrunk properly (every pixel averaged in: no jagged, noisy fine detail) | PASS: fine detail 2.08; the 4K frame averaged down has 1.74 (the old way showed 1.42) |
| The decoder uses every CPU thread | PASS: 2 decoder threads, 2 CPU threads |
| An anamorphic DVD keeps its 16:9 shape | PASS: 1.778; frames 853 × 480 with square pixels (stored as 720 × 480) |
| And shows the same picture as the old way | PASS: mean difference 2.02 |
| Zoomed to fill the window (the cropped part of the frame is shown) | PASS: mean difference 2.09; picture 1280 × 710 |
| Going fullscreen and back, the frames follow the size of the picture | PASS: 1262 × 710 → 1920 × 1080 → 1262 × 710 |
| Fullscreen 1080p plays at full rate | PASS: 29.9 frames a second |
| Going fullscreen while paused, the frame scaled for the window gives way to the full frame | PASS: 1920 × 1080, paused |
| Twelve changes of size and look in five seconds, while playing: the stream carries on | PASS: playing, 30.2 frames a second, at 8.6 s, no error |
| A rotated phone video is drawn the old way, upright | PASS: as decoded, picture aspect 0.562, difference 0.00 |
| With a look, frames are converted on the CPU at the video's own size (no conversion pass in OpenGL) | PASS: rgb, 1920 × 1080, conversion 0.0 ms |
| The look shows the same picture as the old way | PASS: mean difference 1.11 |
| Look detail: Half draws the look at half size and is faster for it | PASS: 24.9 frames a second against 11.9 at full size (fullscreen, 1080p) |
| The half-size look is the same picture, softer | PASS: mean difference 5.49; fine detail 8.20 against 19.32 |
| Automatic: half in a large window, full in a small one | PASS: fullscreen (1080 high) 0.5, window (710 high) 1 |
| Desk mode gets frames converted on the CPU, at the video's own size | PASS: rgb, 1920 × 1080 |
| 4K with a look: frames come scaled to the picture | PASS: 1262 × 710, 17.0 frames a second |
| Cable TV with effects off: the channel number is painted over the picture | PASS: 3492 pixels of the channel number's green in the top right corner |

On the OpenGL widget (forced onto software OpenGL, a case no real machine gets by
itself) the same checks pass with these figures: 1080p 30.1 frames a second (13.7 the
old way), 4K 26.7 (3.9), fullscreen 1080p 18.9; a frame takes 12.3 ms to draw against
56.3 ms; the look is always drawn at full size there.

**The path a graphics card takes was run as a whole suite** (`CRTPLAYER_VIDEO_SURFACE=gl
CRTPLAYER_FAST_PATH=never`: the OpenGL widget with frames as decoded, as in 2.11):
429 checks, no failures.

**Full suites for 2.12.0:** native X11 (429 checks) and Wayland (18), and the same on
the AppImage with the system's Qt removed (429 and 18). No failures. Results:
`docs/results/nogpu-checks.txt` and `nogpu-checks-appimage.txt`.

Unit tests: `tests/test_fmv.cpp` (how well a palette serves a frame; when the palette
before is kept).

### Faults found on the way, and fixed

- **4K shrunk to a window was jagged and noisy** in the first version: GStreamer's
  default scaling filter reads two source pixels for each output pixel and skips the
  rest. The multi-tap filter averages them all in and costs 6% more time. Check: *it is
  shrunk properly*.
- **An anamorphic picture was a fraction of a line short** (0.2%): the scaler adds
  borders by default when the shapes differ by a rounding. Borders are off. Brightness
  edges now sit in the same place as the old way to a hundredth of a pixel.
- **An anamorphic DVD showed as 4:3** in an early version: playsink scaled it before the
  player's sink, and the "native" format seen was the scaled one. (The first fix,
  playbin's *native video* flag, turned out to disable subtitles; the query probe above
  replaced it.)
- **Cable TV with effects off showed a black picture on the OpenGL widget.** The
  effects-off program with the channel display was loaded from Qt's on-disk program
  cache, which is keyed only by a program's "cacheable" shaders: the changed fragment
  shader was not part of the key, so an old program was served. The shader is now part
  of the key. It showed here because the shader had changed between builds; for users it
  would have appeared with graphics cards too, after a later update that changed that
  shader. Check: *Cable TV with effects off*, on both surfaces.
- **Playback could stop with "Internal data stream error … not negotiated"** when the
  size or the look changed while the video played (going fullscreen, resizing, effects
  on or off): a frame already on its way in the old format met the filter after it had
  been set to the new one. It happened about once in four tries of a dozen quick changes
  (3 of 12 runs), and once in the first release run. The filter now lets frames in the
  previous format through until the new one arrives (`caps-change-mode=delayed`): 0 of
  12 runs. Check: *twelve changes of size and look in five seconds*.
- **An original-frame screenshot lost the subtitle line on screen** in an early version:
  the full frame was fetched by seeking to it, and a file does not send a subtitle line
  again that began before the point sought to. Found by the 2.11 subtitle checks (*V
  turns them on*, *subtitle delay*), which take such screenshots. The full frame is now
  kept at hand instead.
- **A paused picture could change to its neighbour frame** when the frame was fetched
  again (entering desk mode, taking a screenshot): the player's position while paused
  can lie a frame to either side of the one shown. The refresh now asks for the frame on
  screen by its own time. Found by the desk-mode checks (*lands on the exact fullscreen
  frame*).
- **Flying in from desk mode no longer landed on the exact fullscreen picture** in an
  early version (edges of coloured areas a pixel off): desk mode drew from frames as
  decoded and the flat view from frames converted on the CPU. Both now use the same
  frames. The six landing checks pass with a largest difference of 0.
- **The Sega CD look's still areas flickered between two neighbouring colours** from
  frame to frame: the palette was picked afresh for every frame, and a colour on the
  border between two of the console's levels tipped either way with the slightest change.
  It showed when the conversion moved to the CPU (values 2 to 4 steps different), and
  made the check *blocks that hardly change are left as they were* fail (13% of blocks
  unchanged instead of 51%). The palette before is now kept between full frames for as
  long as it serves the frame nearly as well as a fresh one (within 20% of its error).
  The check passes on both paths (55% against 33% with the codec off).

### Known limits and what was not tested

- **Not tested in a real QEMU guest,** and not on more than 2 cores. The test machine has
  no graphics card and uses the same software OpenGL (llvmpipe) a guest without 3D
  acceleration gets, so the measurements are of the same kind; how the speed grows with
  12 cores was not measured.
- **4K HEVC 10-bit is still slow on 2 cores:** 9 frames a second with effects off (2.7
  before). Decoding and converting 10-bit 4K is the limit there, not drawing: the
  pipeline alone (decoding, scaling and converting, nothing drawn) manages 16 on these 2
  cores, and decoding alone 38. More cores should help both; not measured. Turning the
  10-bit picture into 8-bit before scaling measured 24 against 16 in a test outside the
  player; that is not built in yet.
- **Looks are computed by the CPU** and stay slow in a large window: see the table. The
  half-size look is softer; it is a trade for speed.
- **Rotated video, the split view and GIF recording** use the old path and are as slow
  as before without a graphics card. **Desk mode's** 3D scene is drawn by software OpenGL
  and stays slow; only its video frames got cheaper. Its speed was not measured.
- **The CPU's colour conversion is not bit-identical to the shader's:** it differs by 2
  to 4 steps of 255 (mean 1.7 to 2.1 over a picture), and the edges of coloured areas can
  sit up to one screen pixel differently (the two treat the position of the half-size
  colour samples differently). Original-frame screenshots always come from the frame as
  decoded and are identical either way (checked: largest difference 0).
- **A screenshot with the look** shows what the window shows, from the frame at hand:
  without a graphics card that is the CPU-converted frame, and for a 4K video in a small
  window the scaled one.
- **Switching** between the paths takes a few frames (the pipeline renegotiates); a
  video's first frames may arrive as decoded.
- **The four decoded frames kept at hand** hold on to that much more of the decoder's
  memory while the fast path is on (for 4K 10-bit, about 100 MB). Not measured, and not
  tried with a hardware decoder (there is none without a graphics card).
- **With a real graphics card** nothing was meant to change by default (*CPU fast path:
  Automatic*). That was not tested, and it was wrong: 2.12.0 broke hardware decoding
  (see 2.12.1 above). *Always* was not tested on a real graphics card.
- **Windows:** not run. The same code applies there without a graphics card; untested.

## 2.11: subtitles that stay as set, and everyday playback

### What was built

**Subtitles and languages that carry over** (`src/app/Everyday.cpp`, `Player`):

- **Off by default.** Whether subtitles are wanted is a stored setting (off at first).
  Every video opens with it, a TV channel included.
- **The subtitle path is always built** and the overlay is told to stay silent while
  subtitles are off, so they can come on at any moment without reopening the video, and
  not even a first line flashes up when they are off.
- **Languages.** Picking a track stores its language (the tag's short code, `en`, `ja`).
  When a video's tracks become known, the track in that language is chosen; a track
  picked by hand in a video holds for that video. A subtitle file picked by hand wins
  over the language.
- **Jellyfin:** with subtitles on and none in the video, the server's subtitle file in
  the preferred language (else its first) is loaded, once per video.

**A sound track is only switched while the video is running.** Switching one while
paused, or while a video opens, and then seeking, locked GStreamer's playbin up for good
(see the faults below). A track chosen at such a moment waits, counts as the current
one, and is switched in when the video runs. While a video opens with a preferred
language that is not its first track, the sound is held silent until the switch (about
a third of a second), so the other language is never heard.

**Delays:**

- **Sound:** playbin's `av-offset` (its sign is the picture's delay, so the sound's
  delay is its negative). Stored.
- **Subtitles:** a time offset on the overlay's subtitle input, so it holds for every
  kind of subtitle. Reset with each video.

**Subtitle text:** playbin's font description (size) and the text overlay's own colour,
outline, shaded box and position.

**Night mode:** a compressor in the player's own sound element (`TapeAudio`): threshold
−36 dB, 3:1 above it, 14 dB of lift, 4 ms attack, 350 ms release, one gain for all
channels, and a soft limit just under full scale. At 0 the sound is untouched.

**Deinterlacing:** playbin's deinterlace stage (GStreamer's `deinterlace`, automatic:
only interlaced video is touched). The setting takes effect by opening the video again
where it is.

**Playlist:** shuffle (every entry once a round, a way back through what was played),
repeat (off, all, one), `.m3u` / `.m3u8` reading and writing, and the next video in the
folder in natural name order.

**Sleep timer:** a deadline, a warning a minute before, a fade over the last 8 seconds,
then pause (or TV off) with the volume restored. *At the end of this video* waits for
the end instead.

**For the checks,** the sound element measures what leaves it, half a second at a time:
its level, and the pitch of a plain tone (zero crossings). The test videos' sound tracks
are tones of different pitch (440 Hz and 880 Hz), so a check can tell which track is
actually heard, not only which is selected.

### Results

| Check (`tests/automation/everyday.txt`, `everyday-restore.txt`, `scripts/check-everyday.py`) | Result |
|---|---|
| Both scripts ran without a failed step | PASS: all ok |
| Subtitles are off by default (a video with two subtitle tracks) | PASS: track -1 of 2 |
| V turns them on: the text is drawn into the picture | PASS: track 0; bottom of the picture changed by 6.15, the top half by 0.00 |
| Picking a track remembers its language | PASS: after French: fr; then English subtitles, Japanese sound: en / ja |
| The sound heard is the track picked (its 880 Hz tone, not the other track's 440 Hz) | PASS: 880 Hz |
| Changing the sound track while paused, then seeking, no longer freezes the player | PASS: playing at 8.9 s, track 2, 880 Hz |
| The next video | PASS: the same languages, wherever they are in the file: subtitles: track 2 (en) of ['fr', 'en']; sound: track 1 (ja) of ['ja', 'en'] |
| A video without subtitles in between changes nothing | PASS: back at the first video: subtitles en, sound track 2 (ja, 880 Hz heard) |
| Turned off, they stay off in the next video | PASS: track -1; the sound is still ja |
| A subtitle file next to the video is not shown while subtitles are off, and is there as soon as they are on | PASS: subs_clip.srt: off, then on (bottom of the picture changed by 6.10) |
| Subtitle delay −1.5 s: the line is on screen before its stored time | PASS: at 3.2 s the line shows (stored from 4 s; at 3 s without the delay: not shown) |
| Subtitle delay +1.5 s: the line comes later and stays later | PASS: at 4.7 s not yet shown (without the delay it is, at 4.5 s); at 6.5 s still shown (stored until 6 s) |
| Size: small, normal, very large | PASS: the text is 2.7%, 4.0% and 6.5% of the picture's height |
| Colour: yellow | PASS: 97% of the bright text pixels are yellow (0% when white) |
| Behind the text: a dark box | PASS: 71% of the text's rectangle is dark with the box, 14% with the outline |
| Position: bottom, raised, top | PASS: the text starts 89%, 77% and 6% of the way down |
| Sound delay +400 ms: the sound is held back | PASS: sound output delayed 400 ms, picture 0 ms |
| Sound delay −400 ms: the picture is held back instead (measured on the frames) | PASS: picture output delayed 400 ms; frames arrive 400 ms later against the sound's clock than before |
| The sound delay carries over to the next video; a subtitle delay does not | PASS: sound 100 ms; subtitles 700 ms -> 0 ms in the next video |
| Interlaced video is deinterlaced | PASS: combing -0.12 deinterlaced, 2.98 as stored |
| Switched off, the frames are shown as stored | PASS: deinterlacing: False |
| Progressive video is never touched | PASS: setting on, deinterlacing: False |
| Night mode lifts the quiet part and holds down the loud part | PASS: quiet -46.0 -> -32.0 dB, loud -13.6 -> -15.8 dB: 16.2 dB apart instead of 32.3 |
| Shuffle: every video once, then the end | PASS: a → d → e → c → b; then paused |
| Repeat the playlist: after the last video, the first | PASS: e → a |
| Repeat this video: the same one again | PASS: a → a → a |
| The playlist saves as an .m3u8 file and opens again | PASS: 5 entries loaded, playing short_a.mp4 |
| Videos in the list's own folder are written relative to it | PASS: Episode 1.mp4, Episode 2.mp4, Episode 10.mp4 |
| Carrying on in the folder: Episode 1, 2, 10 in that order, and it stops at the last | PASS: Episode 1.mp4 → Episode 2.mp4 → Episode 10.mp4; then paused |
| Switched off (the default), one video is one video | PASS: ['Episode 1.mp4'], paused |
| Sleep timer: counts down while the video plays | PASS: 10 s left of 12, volume 80% |
| The sound fades out over the last seconds | PASS: 3 s left: volume 10% |
| Then it stops: paused, the volume back, the screen free to sleep | PASS: paused, volume 80%, keeping the screen awake: False |
| "at the end of this video": it stops there instead of going on with the playlist | PASS: last played short_a.mp4 of ['short_a.mp4', 'short_b.mp4']; paused |
| Cable TV: subtitles as set, from channel to channel | PASS: channel 2: track 1 (en); channel 3: track 2 (en) |
| … and the sound's language too | PASS: channel 2: track 2, channel 3: track 1 (both ja; 880 and 880 Hz heard) |
| Turned off on one channel, they are off on the others; and on again | PASS: channel 2: -1, channel 3: -1; on again: en |
| The sleep timer turns the TV off | PASS: TV on: False, idle |
| In TV mode, "at the end of this video" waits for the programme to end, then turns the TV off | PASS: TV on: False, idle |
| Everything is as it was left on the next launch | PASS: subtitles on (en), sound ja, sound delay 100 ms, style [2, 1, 0, 0], night mode, shuffle, repeat, next-in-folder |
| … and the first video opened gets them | PASS: subtitles en, sound ja, sound delayed 100 ms |
| The sleep timer is not carried over | PASS: off |

Unit tests (`tests/test_tape.cpp`): night mode lifts a −43 dB tone by 14 dB and holds a
−4 dB tone down by 9 dB (16 dB apart instead of 39); a sudden full-level bang after quiet
stays within full scale; left and right keep their balance exactly; with night mode off
the sound is bit-exact.

Jellyfin (`scripts/check-jellyfin.py`, both API versions): with subtitles on, a video
without its own gets the server's subtitle file by itself.

**Full suites for 2.11.0:** native X11 (369 checks) and Wayland (18), and the same on
the AppImage with the system's Qt removed (369 and 18). No failures. Results:
`docs/results/everyday-checks.txt` and `everyday-checks-appimage.txt`.

### Faults found on the way, and fixed

- **The player could freeze for good** when the sound track was changed while paused and
  the video was then moved (a seek). This was in every earlier version: GStreamer's
  playbin deadlocks when a track switch is still waiting to happen and a seek arrives.
  It showed up as Cable TV hanging on a channel change once a preferred sound language
  existed (the switch happened while the channel opened, then the channel seeked to
  where the broadcast was). Sound tracks are now switched only while the video runs.
  Check: *changing the sound track while paused, then seeking, no longer freezes the
  player*.
- **Turning subtitles on did nothing in a video opened with them off.** The first
  version left the subtitle path unbuilt while off. It is now always built and silenced.
- **The subtitle delay had no effect on subtitles stored inside the video.** playbin's
  own `text-offset` only reaches subtitle files that pass through a parser. The offset
  is now set on the overlay's input.
- **The sound delay went the wrong way** in the first version (playbin's value is the
  picture's delay). Found by reading back which output was held.
- **Episode 10 came before Episode 2.** The system's collation did not sort numbers in
  this environment's locale; the order is now worked out by hand.
- **The sleep timer's "end of this video" did nothing in TV mode.** It now waits for the
  programme to end.
- **Cable TV's first tune could start seconds behind its schedule** (a slow first open
  that the player had no estimate for). The measured open time is now kept between
  sessions. The TV checks' timing allowances were also widened to what the design
  permits (a programme up to 3 s late starts from its top rather than lose its
  beginning).
- **A test clip that was not what it claimed:** the first interlaced clip did not move
  between its fields, so it could not comb and the deinterlacing check could not tell on
  from off. The clip was remade (combing 2.98 as stored, −0.12 deinterlaced).
- **`polish.txt`** now turns subtitles on before its sidecar-file check (it relied on
  them being on by default).

### Known limits and what was not tested

- **A subtitle line already on screen at the point you jump to** is not shown until the
  next line: the file does not send it again. This is as before.
- **A new subtitle delay** holds from the next line on. With a large delay, the first
  line after a jump can be mistimed.
- **Subtitle style** applies to text drawn by GStreamer's text overlay. Picture
  subtitles (DVD, Blu-ray) and styled ASS subtitles drawn by another renderer keep their
  own look; neither was tested.
- **The sound delay's "later" direction** is checked by reading the delay the sound
  output holds, not by measuring the sound against the picture. "Earlier" is measured on
  the frames (400 ms for 400 in the final run).
- **Night mode** is measured with tones. How it sounds on real film sound is for your
  ears.
- **Deinterlacing** uses GStreamer's default method. It was checked on one MPEG-2 clip;
  real DVD and broadcast material, and video from a hardware decoder, were not tested.
- **A converted Jellyfin stream** carries one sound track, so the language preference
  cannot apply to it.
- **Playlist files from other players** were not tested beyond the format itself
  (comments, relative and absolute paths, addresses).
- **Speed.** As always here: software OpenGL, no GPU, no real sound device.

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
| Enhance: sharper upscaling and frame generation (shaders, for a graphics card) | **Tested** for what they draw (measurements above), forced on with software OpenGL. **Not tested:** any real graphics card, speed, pacing on a real screen, real film |
| Playback without a graphics card (frames converted and scaled on the CPU's cores, painted without OpenGL; looks at half size) | **Tested** on software OpenGL with 2 cores (measurements above), on both window surfaces, and the graphics-card path as a whole suite. **Not tested** in a real virtual machine or on more cores |
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
