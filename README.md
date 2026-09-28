# CRT Player

A desktop video player for Linux (built for Bazzite) whose defining feature is a
configurable, GPU-rendered CRT presentation that respects every aspect ratio:
4:3, 16:9, ultrawide, vertical, anamorphic and rotation-tagged phone video.

![Player with the CRT and playlist panels open](docs/images/ui.jpg)

- **Uses your system's codecs.** Playback runs on the host's GStreamer and whatever
  plugins are installed. Nothing is bundled.
- **Never distorts by default.** It reads display aspect ratio, pixel aspect ratio and
  rotation metadata. The Original, Fit, Fill and Crop modes all preserve proportions.
- **CRT effects apply to the picture, not the window.** By default the tube hugs the
  picture and letterbox or pillarbox bars stay flat and black. Curvature and masks scale
  with the displayed picture. Curvature never crops content; only Overscan and Fill do,
  and only when you choose them.
- **Analog source simulation**: VHS tape distortion (jitter, tracking error, head
  switching, chroma delay, dropouts, tape softness) and composite / LaserDisc
  artifacts (dot crawl, rainbow crosstalk, laser rot). They are applied to the signal
  *before* the tube, so they bend with the curvature and pass through the scanlines
  and mask like real content.
- **Everyday playback:** resume where you left off, recent files, external subtitle
  files, playback speed (pitch preserved), A–B loop, chapters, and preview thumbnails on
  the seek bar.
- **Made for Bazzite:** game controllers (Xbox, PlayStation, Switch, Steam Deck), media
  keys and KDE's media widget / KDE Connect (MPRIS), and Steam Game Mode, with a room
  backdrop for desk mode.
- **Jellyfin.** Sign in to your Jellyfin server, browse libraries with posters,
  search, continue watching, and stream videos in their original format (decoded
  locally, with every CRT effect). Watch progress syncs back to the server.
- **The look's sound:** looks change the sound as well as the picture.
  - **VHS** hisses, wobbles (wow and flutter), saturates warmly, loses its treble, and
    plays through a small TV speaker. The tape's dropouts dip the sound too.
  - **TV looks** sound like a TV's small, boxy speaker (mono when full).
  - **Film prints** crackle and pop, with a softer top end.
  - **Adjusting it:** *Settings → CRT → Sound* has a slider for each.
  - **Levels:** *Settings → Playback → Sound* has two controls that work on top of any
    look, and are remembered:
    - **Noise volume** (0–200 %): how loud the tape hiss and film crackle are.
    - **Effect strength** (0–100 %): how strongly the TV speaker, treble loss, pitch
      wobble, saturation and dropouts change the sound.
  - **Switching it off:** *Settings → Playback → Sound → The look's sound* turns the sound
    effects off while keeping the picture.
  - **Clean looks** leave the sound untouched, bit for bit.
- **Film projection:** grain, gate weave (the frame drifting in the projector's gate),
  projector flicker, and dust and scratches, at 24 film frames per second.
  - **Presets:** *Film Print (35mm)*, *Worn Film (16mm)* and *Drive-in Movie*.
  - **In desk mode,** the movie theater scene uses them.
- **Deeper simulation.** Phosphor persistence (afterglow trails), reduced colour depth
  (15-bit down to 3-bit, plus Game Boy, CGA and EGA palettes) with ordered dithering,
  PAL mode, and set moments: a power-on/off animation, static between playlist items,
  and a VCR on-screen display.
- **Lower resolution / bigger pixels.** Render the video at 240, 224, 160… rows (or
  exact sizes like 256×224 and 320×200), with hard, sharp or soft pixels. Scanlines lock
  to one line per pixel row.
- **Eleven scanline styles.** Six original ones: Soft, Sharp, Dynamic, Interlaced,
  VGA double-scan and Pixel beam. Five emulator looks modelled on well-known RetroArch
  and MAME shaders: Geom-, Lottes-, Easymode-, Hyllian- and MAME HLSL-style. These are
  independent implementations, not ports; see
  [Emulator scanline looks](#emulator-scanline-looks).
- **Desk mode** (optional, press **T**): the video plays on a 3D set that stands on your
  desktop: a CRT television, a flat-face CRT, a flat panel, an 80s wood-grain console, a
  broadcast monitor, a beige PC monitor or an upright arcade cabinet, with no window around it. Turn it and move it with the mouse,
  then double-click to fly into the screen; the flight lands exactly on the normal
  fullscreen image. The regular player window is unchanged and comes back when you
  leave desk mode.
- **Hardware decoding when available**, with automatic fallback to software if a
  hardware decoder fails.
- **Wayland and X11**, through Qt 6.

---

## Contents

1. [Run on Bazzite](#run-on-bazzite) (AppImage or binary)
   - [Windows 10 and 11 (preview)](#windows-10-and-11-preview)
2. [Build from source](#build-from-source)
3. [Dependencies](#dependencies)
4. [Controls](#controls)
5. [Scaling and aspect ratio](#scaling-and-aspect-ratio)
6. [CRT presets and parameters](#crt-presets-and-parameters)
7. [Screenshots, frame stepping, playlist](#screenshots-frame-stepping-playlist)
8. [Desk mode: a 3D TV on your desktop](#desk-mode-a-3d-tv-on-your-desktop)
9. [Jellyfin](#jellyfin)
10. [Where settings are stored](#where-settings-are-stored)
11. [Troubleshooting](#troubleshooting)
12. [Code layout](#code-layout)
13. [Verification](#verification)

The rendering pipeline is explained in [docs/PIPELINE.md](docs/PIPELINE.md).
The scripted test driver is documented in [docs/AUTOMATION.md](docs/AUTOMATION.md).

---

## Windows 10 and 11 (preview)

**What it is:** a self-contained folder for 64-bit Windows 10 and 11. It carries its own
Qt and GStreamer (with all the usual codecs), so there is nothing else to install.

**Getting it:** each build on GitHub produces `CRT_Player-<version>-windows-x64.zip`
(under *Actions*, or *Releases* for tagged versions).

1. Unzip it anywhere.
2. Run `crtplayer.exe` from the `CRT Player` folder.
3. Windows may warn about an unrecognised app the first time. Choose *More info → Run
   anyway*: the build isn't code-signed.

**What's different on Windows:**

- **Hardware decoding** uses Direct3D 11 (`d3d11h264dec` and friends).
- **Keeping the screen awake** while a video plays uses Windows' own setting.
- **Game controllers** work through SDL, which ships in the folder.
- **Settings** are stored in the registry (`HKEY_CURRENT_USER\Software\CRTPlayer`).
- **Needs OpenGL 3.3**, which every graphics driver from the last decade provides. A PC
  running on Windows' basic display driver (no graphics driver installed) gets a message
  saying so instead of a picture: install the driver from the card's maker.
- **No sound device** (no speakers, audio service stopped): the video still plays, silently.
- **Not yet:** media keys and Windows' media overlay (the Linux version uses MPRIS). Steam
  Game Mode is Linux-only.

**How it's built:** on GitHub's Windows machines with MSYS2
(`.github/workflows/windows.yml`). The packaged folder is then run on its own, with
software OpenGL because those machines have no graphics card (and no sound device): it
plays a video, applies a look, draws a subtitle and opens desk mode (`packaging/windows/`).

## Run on Bazzite

There are two ready-made options. Both decode with **your system's GStreamer codecs**;
neither bundles any codecs.

### AppImage (easiest)

```bash
chmod +x CRT_Player-2.7.0-x86_64.AppImage
./CRT_Player-2.7.0-x86_64.AppImage                    # or double-click it in Dolphin
./CRT_Player-2.7.0-x86_64.AppImage ~/Videos/clip.mkv
```

**Which Qt it uses:**

- **Your system's Qt (preferred):** used when it's version 6.4 or newer and has
  everything the player needs, which is true on Bazzite, KDE desktops and most others.
  That's the same setup as the standalone binary, and it matches your compositor and
  graphics driver best.
- **The AppImage's own Qt 6.4 (fallback):** used on systems without a usable Qt. On a
  Wayland session it then runs through XWayland.
- **To force either,** start it with `CRTPLAYER_QT=system` or `CRTPLAYER_QT=bundled`.
  *Settings → Playback → Copy system report* shows which one is in use.

**What comes from your system:**

- GStreamer and all its plugins (your codecs and hardware decoders);
- GLib;
- the Wayland client libraries;
- your GPU's OpenGL driver;
- the C/C++ runtime.

**Host requirements:** glibc 2.38 or newer, a GCC 12+ C++ runtime, and GStreamer 1.18+
with plugins-base. Bazzite and Fedora 39 or newer meet all of these.

**If it won't start** with a FUSE error, run it with `--appimage-extract-and-run`.

**To get a menu entry,** use an AppImage manager such as Gear Lever, or run
`scripts/install-local.sh CRT_Player-2.7.0-x86_64.AppImage`.

To build it yourself, run `scripts/build-appimage.sh`. It needs the build dependencies
below, plus `patchelf`, `curl` and network access, which it uses to fetch linuxdeploy
and appimagetool once.

### Other Linux distributions

The AppImage runs on any x86-64 distribution with glibc 2.38 or newer, including Arch
and its derivatives, Fedora, Ubuntu 24.04+, Debian 13 and openSUSE Tumbleweed. It uses
your system's GStreamer for decoding.

If something is missing, the player still starts. It shows **what** won't work and the
**exact command for your distribution**, with a *Copy command* button. It detects Arch,
Fedora (and Fedora Atomic such as Bazzite), Debian/Ubuntu and openSUSE families,
including derivatives such as Manjaro, EndeavourOS, CachyOS, Nobara, Mint and Pop!_OS.

For everything to play (MP4/MKV, H.264/H.265, AAC, Jellyfin streaming, natural-sounding
speed changes), install:

| Distribution | Command |
|---|---|
| Arch, Manjaro, EndeavourOS, CachyOS | `sudo pacman -S --needed gst-plugins-good gst-plugins-bad gst-plugins-ugly gst-libav` |
| Ubuntu, Debian, Mint, Pop!_OS | `sudo apt install gstreamer1.0-plugins-good gstreamer1.0-plugins-bad gstreamer1.0-plugins-ugly gstreamer1.0-libav` |
| Fedora | `sudo dnf install gstreamer1-plugins-good gstreamer1-plugins-bad-free gstreamer1-plugins-ugly-free gstreamer1-plugin-libav`, plus RPM Fusion's `gstreamer1-plugins-bad-freeworld gstreamer1-plugins-ugly` for H.265 and other patented codecs |
| Bazzite, Nobara | Nothing to install: the full set is included |
| openSUSE | `sudo zypper install gstreamer-plugins-good gstreamer-plugins-bad gstreamer-plugins-ugly gstreamer-plugins-libav` (Packman's versions for the complete codec set) |

**Subtitles** are drawn by GStreamer's pango plugin. It comes with the base plugins
everywhere except Debian and Ubuntu, where it is in a separate package:
`sudo apt install gstreamer1.0-x`. Without it, the player says so at startup.

**What works without these:**

- **The player always starts, if GStreamer itself is installed.**
- **Ogg/Theora/Vorbis files play with only the base plugins.**
- **Without `gst-plugins-good`:**
  - audio goes to PipeWire, PulseAudio or ALSA directly;
  - MP4 and MKV files explain what to install.

**To check a system from a terminal:**

```bash
./CRT_Player-2.7.0-x86_64.AppImage --check-gstreamer
```

It lists anything missing and prints the install command.

### Plain binary

The standalone `crtplayer` binary is x86-64. It needs:

- glibc 2.34 or newer;
- Qt 6.4 or newer, public API only;
- the GStreamer 1.x base libraries.

Bazzite's KDE and GNOME images already contain all of these, so no extra packages are
needed.

```bash
chmod +x crtplayer
./crtplayer                       # opens an empty player; drop files onto it
./crtplayer ~/Videos/clip.mkv     # or pass files / URIs
```

To install it for your user, with a menu entry and icon:

```bash
scripts/install-local.sh ./crtplayer
```

Before relying on the binary, it's worth a quick check:

```bash
ldd ./crtplayer | grep "not found"     # should print nothing
./crtplayer --check-gstreamer          # GStreamer version, missing essentials, hardware decoders (works without a display)
```

> Both files were built on Ubuntu 24.04 and tested there (see
> [VERIFICATION.md](VERIFICATION.md)), including the AppImage with the system's own Qt
> removed. They have **not** been run on Bazzite itself. If `ldd` reports a missing
> library, use the AppImage, or build natively as below. The native build takes about
> two minutes.

### Native build on Bazzite

```bash
scripts/build-bazzite.sh
./build-fedora/crtplayer
```

The script works like this:

1. It creates a `distrobox` that matches your host's Fedora release.
2. It installs the development packages inside that box only.
3. It builds the player and runs the unit tests.

Run the resulting binary **on the host**, not inside the box. That way it uses the
host's GStreamer plugins, codecs and GPU drivers.

Command-line options:

| Option | Meaning |
|---|---|
| `-f`, `--fullscreen` | Start in fullscreen |
| `--preset "Worn VHS TV"` | Select a CRT preset |
| `--no-hw` | Disable hardware decoding for this session |
| `--check-gstreamer` | Print GStreamer capabilities and exit |
| `--automation FILE --automation-log OUT.json` | Run a verification script |

To force a display platform, set `QT_QPA_PLATFORM=wayland` or `QT_QPA_PLATFORM=xcb`.

---

## Build from source

Requirements:

- CMake 3.16+
- a C++17 compiler
- Qt 6.2+ (Core, Gui, Widgets, OpenGL, OpenGLWidgets)
- GStreamer 1.18+ development files (core, app, video, audio, pbutils, tag)

**Fedora / Bazzite distrobox**

```bash
sudo dnf install cmake gcc-c++ pkgconf-pkg-config qt6-qtbase-devel SDL2-devel \
                 gstreamer1-devel gstreamer1-plugins-base-devel
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build
```

**Ubuntu / Debian** (this is how the delivered binary was built)

```bash
sudo apt install build-essential cmake pkg-config qt6-base-dev libgl-dev libsdl2-dev \
                 libgstreamer1.0-dev libgstreamer-plugins-base1.0-dev
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build
```

Add `-DCRTPLAYER_BUILD_TESTS=OFF` to skip the unit tests.

SDL2's headers are needed to build (for game controllers). SDL2 itself is loaded when the
player runs, and is optional: without it, controllers are simply off.

---

## Dependencies

**Runtime libraries** (linked):

- `libQt6Core`, `libQt6Gui`, `libQt6Widgets`, `libQt6OpenGL`, `libQt6OpenGLWidgets`
  (Fedora package: `qt6-qtbase`, `qt6-qtbase-gui`)
- `libgstreamer-1.0` (Fedora: `gstreamer1`)
- `libgstapp`, `libgstvideo`, `libgstaudio`, `libgstpbutils`, `libgsttag`
  (Fedora: `gstreamer1-plugins-base`)
- glib2 and the C/C++ runtimes

**GStreamer plugins used at runtime** (not linked; discovered through the registry):

- **Required:**
  - `playbin`, `appsink`, `videoconvert`, `audioconvert`, `audioresample`, `typefind`
    (from plugins-base)
  - `autoaudiosink` (from plugins-good)

  If any of these are missing, the player says which one at startup.
- **Codecs:** whatever is installed. Typical providers are `gstreamer1-plugin-libav`,
  `gstreamer1-plugins-good`, `-bad-free`, `-bad-freeworld`, `-ugly`, and `dav1d`.
- **Subtitles:** `textoverlay` / `subtitleoverlay` from plugins-base, which renders
  through Pango.
- **Hardware decoding:** the `va` plugin (VA-API, in plugins-bad) or `nvcodec` (NVDEC),
  if present.

**GPU:** an OpenGL 3.3 core profile, which Mesa and NVIDIA both provide on Wayland and
X11.

**Building the AppImage** additionally needs `qt6-wayland` development files, `patchelf`
and `curl`.

---

## Controls

Hover any button to see its shortcut.

| Key | Action |
|---|---|
| Space / K | Play / pause |
| ← / → | Seek −5 s / +5 s |
| Ctrl+← / Ctrl+→ | Seek −30 s / +30 s |
| Home | Back to the start |
| , / . | Previous / next frame (pauses) |
| ↑ / ↓ | Volume ±5 % |
| M | Mute |
| F, F11, double-click | Fullscreen toggle |
| Esc | Leave fullscreen |
| B | CRT effects on/off (bypass) |
| C | Before/after split view (drag the amber divider) |
| [ / ] | Previous / next CRT preset |
| Z | Cycle scaling: Original → Fit → Fill → Crop |
| A | Cycle audio track |
| J | Cycle subtitle track (…→ off) |
| V | Subtitles on/off |
| S | Screenshot of the default type |
| Shift+S | Screenshot of the original frame |
| Ctrl+S | Screenshot of the filtered frame |
| Page Down / N | Next playlist item |
| Page Up / P | Previous playlist item |
| Ctrl+O | Open files |
| E | CRT & picture panel |
| L | Playlist panel |
| I | Technical info overlay: codec, decoder, PAR/DAR, rotation, sync |
| T | Desk mode on/off (3D TV on the desktop) |
| − / + (or =) | Slower / faster (0.25× steps, 0.25×–4×); Backspace: normal speed |
| R | A–B loop: set the start, then the end, then off |
| Shift+PgUp / Shift+PgDn | Previous / next chapter |
| Ctrl+J | Jellyfin panel |
| Ctrl+Q | Quit |

**Mouse and drag-and-drop:**

- Drop files or folders anywhere on the window. They are added to the playlist and the
  first one plays.
- Click anywhere on the seek bar to jump there. Drag it to scrub.

**Fullscreen:** the control bar floats over the video. It fades out after about 2
seconds without mouse movement, along with the cursor, and returns as soon as the mouse
moves. It stays visible while paused, while you are using a menu, or while you are
scrubbing.

**Narrow windows:** the control bar hides lower-priority buttons in this order: preset
box, frame step, volume slider, previous/next, and so on. Every hidden control is still
available through its shortcut and the side panels.

---

## Scaling and aspect ratio

The player computes the displayed shape as:

```
display width  = coded width × PAR        (then swapped if rotated 90°/270°)
display height = coded height
```

The pixel aspect ratio (PAR) comes from the decoder caps. The rotation comes from the
`image-orientation` tag, which covers MP4/MOV display matrices, including mirroring.

![Aspect ratio handling](docs/images/aspect-ratios.jpg)

| Mode | Behaviour |
|---|---|
| **Original size** | 1 display pixel = 1 screen pixel. Shrinks, without distortion, if the picture is larger than the window. |
| **Fit** (default) | Whole picture visible, with bars where needed. |
| **Fill** | No bars. Edges are cropped symmetrically. |
| **Crop** | Per-edge crop sliders (0–45 %) plus a *Crop to aspect* list (4:3, 16:9, 1.85, 2.39, 21:9, 1:1, 4:5, 9:16). The cropped picture is then fitted. |

**Aspect override** (Display tab) is for files with wrong metadata only. It is the one
setting that deliberately stretches the picture, and it defaults to *From file*.

![Scaling modes, subtitles and include-bars](docs/images/modes.jpg)

---

## CRT presets and parameters

![Four of the presets on the same 4:3 frame](docs/images/presets.jpg)

**Built-in presets** (read-only). Press `[` and `]` to cycle through them.

| Preset | Character |
|---|---|
| **Clean Broadcast Monitor** | Sharp scanlines, fine aperture grille, almost flat, no signal artefacts |
| **Consumer Television** | Soft scanlines, shadow mask, visible curvature, mild bleed, noise and flicker |
| **Arcade Display** | Dynamic 240-line scanlines, slot mask at 2×, strong bloom and saturation |
| **Worn VHS TV** | A tired tape on an old set: jitter, a rolling tracking band, heavy head switching, chroma delay, dropouts, soft and desaturated |
| **VHS Home Recording** | A decent SP recording: mild jitter, head switching at the bottom, chroma delay, tape softness |
| **LaserDisc Composite** | A clean disc over a composite cable: dot crawl on colour edges, rainbow on fine detail |
| **LaserDisc with Laser Rot** | The same, plus flickering laser-rot speckles and more noise |
| **Interlaced Broadcast Monitor** | Alternating-field scanlines on a flat studio monitor |
| **VGA Double-Scan Monitor** | Every line drawn with a thin gap, fine shadow mask |
| **Low-Res Console** | The picture rendered at 240 rows; each pixel drawn as a soft beam dot (pixel-beam style) |
| **Geom Look** | Geom-style beams, aperture grille, noticeable curvature |
| **Lottes Look** | Lottes-style soft pixels and beams, shadow mask, a touch of bloom |
| **Easymode Look** | Easymode-style thin dark lines that swell when bright, flat screen |
| **Hyllian Look** | Hyllian-style flat beams and crisp horizontals, flat screen |
| **MAME HLSL Look** | Sine-shaped scanlines, shadow mask, bloom, slight convergence error |
| **16-bit Console (256×224)** | Wide 256×224 pixels, sharp filter, geom-style beams, shadow mask |
| **Home Computer (320×200)** | 320×200 hard pixels, sharp scanlines, fine shadow mask |
| **PAL Television** | Consumer set on a PAL signal: 50 Hz fields, diagonal colour crawl, Hanover bars |
| **Handheld (4 greens)** | 160×144 pixels in four greens, no scanlines, slow-LCD ghosting |
| **Sharp PC Monitor** | Light aperture grille, barely curved |

![Analog-source presets on a composite test card](docs/images/analog-presets.jpg)

![Close-ups: rainbow crosstalk, dot crawl, head switching](docs/images/analog-closeups.jpg)

All built-in presets keep Overscan at 0, so no preset crops the picture unless you
raise it.

**Parameters** (*Picture → CRT* tab; every control has a tooltip):

| Group | Controls |
|---|---|
| Resolution | Rows, columns (Auto = square pixels), pixel filter, colours and dither, with quick picks |
| Scanlines | Style (11, see below), strength, beam width, line count (Auto, or a fixed number) |
| Set behaviour | Power on/off animation, static between items, VCR on-screen display |
| Phosphor mask | Pattern (none / aperture grille / shadow mask / slot mask), strength, scale |
| Geometry | Curvature, corner rounding, overscan, *apply the tube to black bars too* |
| Light | Bloom, phosphor glow |
| Signal | Chromatic aberration, colour bleed, noise, flicker |
| VHS tape | Timebase jitter, tracking error, head switching, chroma delay, dropouts, tape softness |
| Composite & LaserDisc | Dot crawl, rainbow crosstalk, laser rot |
| Picture | Vignette, brightness, contrast, saturation, colour temperature |

**Scanline styles**

![The six scanline styles, zoomed](docs/images/scanline-styles.jpg)

| Style | What it simulates |
|---|---|
| Soft (Gaussian beam) | A round beam with soft gaps; bright areas widen slightly. The classic look |
| Sharp (PVM/BVM) | A flat-topped beam with hard dark gaps, like a studio monitor |
| Dynamic | Thin lines in dark areas, fat lines that nearly close the gaps in bright ones |
| Interlaced | The line pattern moves half a line every field (59.94 Hz), giving interlace twitter while playing |
| VGA double-scan | Every line drawn with a thin gap, kept at least about one screen pixel wide |
| Pixel beam | The picture is sampled once per low-res pixel and each pixel drawn as a soft beam dot. Pair it with a fixed line count such as 240 |

### Deeper simulation

**Persistence** (*Light* group). Phosphors keep glowing briefly after the beam passes, so
bright moving objects leave a short trail. Red and green fade more slowly than blue, as
with real P22 phosphors. The decay is based on elapsed time, so it looks the same at any
frame rate. It's switched off while the before/after view is on, and screenshots never
include it.

**Colours and Dither** (*Resolution* group):

- **Colour depth:** 15-bit (32 768 colours), 12-bit, 9-bit (512), 8-bit (3-3-2),
  6-bit, or 3-bit (8 colours). Or a fixed palette: **Game Boy** (4 greens), **CGA**
  (black, cyan, magenta, white) or **EGA** (16 colours).
- **Dither:** ordered 4×4 or 8×8. It hides banding with a regular pattern, aligned to
  the picture's pixels, so with *bigger pixels* each block gets one dither cell.

**Standard** (*Signal* group): **NTSC** or **PAL**. PAL changes three things:

- the scale of the tape artefacts (576 lines);
- the field rate (50 Hz), which affects interlacing, colour crawl and noise;
- the composite pattern: PAL's diagonal crawl, and the **Hanover bars** (fine hue
  stripes that alternate line by line) when rainbow or dot crawl is on.

**Set behaviour** (its own group):

- **Power on/off animation:** the raster opens from a bright line when playback starts,
  and collapses to a line, then a dot, when the playlist ends. The screen stays dark
  until you play again.
- **Static between items:** switching playlist items shows static until the new file's
  first picture, then fades out.
- **VCR on-screen display:** PLAY ▶, PAUSE ❚❚, ◀◀ REW, ▶▶ FF and STOP ■ in the corner.
  It's inserted into the video *signal*, like a real VCR: steady while the tape picture
  wobbles, but it bleeds and gets scanlines on the TV.

Several presets use these: Consumer Television, the VHS presets, Arcade, the console and
computer presets, PAL Television and Handheld.

![Colour depths, dithering, power-on/off, static, VCR text, Hanover bars, persistence](docs/images/simulation.jpg)

### Lower resolution (bigger pixels)

*CRT → Resolution* renders the picture at a lower resolution, so every pixel becomes a
visible block, the way a 240p console or a 320×200 computer looks on a TV.

- **Resolution:** a quick pick (240 lines, 224, 160, 256×224, 320×200, 160×144, …), or
  set **Rows** and **Columns** exactly.
  - Rows count down the picture; for portrait video, along its height.
  - Columns *Auto* keeps pixels square. A 16:9 video at 240 rows becomes 427×240, and an
    anamorphic DVD becomes 427×240 too.
  - A fixed column count gives wide or narrow pixels (256×224 is the classic 16-bit
    console shape). The picture's shape on screen never changes.
- **Pixel filter:**
  - **Hard:** pure blocks.
  - **Sharp:** blocks with a one-screen-pixel smoothed edge. This avoids uneven,
    shimmering blocks when a pixel isn't a whole number of screen pixels; it's the
    default.
  - **Soft:** smooth, like a blurry analog source.
- **Downsampling averages areas properly,** so fine detail blends into blocks rather
  than flickering.
- **Everything follows the lowered picture.** With *Line count: Auto*, scanlines lock to
  exactly one line per pixel row. Every scanline style, the VHS/LaserDisc effects and
  desk mode all see the lowered picture.
- **What stays at full resolution:** Bypass, the *original* side of the before/after
  view, and *original frame* screenshots.

In desk mode the blocks are drawn on the tilted 3D glass, so their edges soften
slightly with perspective. After flying into fullscreen they're exact.

![Native, and 60 rows with Hard, Sharp and Soft pixels](docs/images/bigger-pixels.jpg)

### Emulator scanline looks

The last five styles reproduce the *look* of shaders that many people know from
RetroArch and MAME.

- **Geom-, Lottes-, Easymode- and Hyllian-style** use *beam reconstruction*. The picture
  is read at scanline resolution; every screen pixel sums the light of the nearest
  scanlines; and each line's beam widens with its brightness, *per colour channel*, with
  the sum done in linear light. That's why bright lines swell into the gaps while dark
  lines stay thin.
- **MAME HLSL-style** is a sine-shaped scanline modulation with a brightness offset,
  so the gaps never go fully black.

![How the emulator looks respond to brightness and colour](docs/images/emulator-looks.jpg)

| Style | Look it's modelled on | What characterises it |
|---|---|---|
| Geom-style | RetroArch **crt-geom** | Beams rebuilt from the two nearest lines in linear light; widths grow with brightness; mild horizontal sharpening |
| Lottes-style | **crt-lottes** | Constant beam width over four lines, plus a Gaussian across neighbouring source pixels, giving soft round "pixels" |
| Easymode-style | **crt-easymode** | Very thin lines in dark areas that fatten strongly when bright; sharp horizontals |
| Hyllian-style | **crt-hyllian** | Flat-topped beams with hard gaps; strong horizontal sharpening |
| MAME HLSL-style | MAME's **HLSL / BGFX** post-processing | Sine-shaped lines with a brightness offset; the same structure at every brightness |

**These are not ports.** I wrote the implementations from scratch, using the published
general techniques: Gaussian beam reconstruction, brightness-dependent beam width,
Lanczos-style sharpening, linear-light blending and sine modulation. They were tuned by
eye and by measurement toward each shader's characteristic appearance. They were
**not** compared side by side with the originals, so expect a family resemblance, not
identical output. Credit for the looks goes to the authors of crt-geom (cgwg and
contributors), crt-lottes (Timothy Lottes), crt-easymode (EasyMode), crt-hyllian
(Hyllian) and to the MAME team.

**Tips:**

- The beam styles treat the video as a signal with as many lines as *Line count*. Auto
  gives about 240–270 lines, the classic look. Set 480 or more for a finer, less
  pixelated result on HD video.
- They look best with at least 4 screen pixels per line, for example a 240-line setting
  on a 1080p display.
- All VHS, composite and LaserDisc effects still apply. They pass through the beams like
  real signal content.

**VHS and composite effects** are measured against the *picture*, not the window:
480 NTSC lines down it and 640 samples across it. So a tracking band, head switching
at the bottom edge, or dot crawl looks the same on 4:3, 16:9, ultrawide and vertical
video, and never spills into letterbox bars. Everything except Tape softness and Chroma
delay animates while playing. The animation stops when you pause.

**Auto line count** uses the number of visible source lines. It halves that number
until each line spans at least 2.5 screen pixels, which prevents moiré. For example, a
480-line source becomes 240 lines at 720p; a 1080-line source becomes 270 lines in a
1080-pixel-tall window.

**Managing presets:**

- **Save as new…** saves the current settings as a new preset.
- **Save changes** overwrites the selected user preset.
- **Revert** discards unsaved changes.
- **Rename…** and **Delete** work on user presets only.
- **Import…** reads a preset file. If the name is already taken, it is renamed
  "Name (2)".
- **Export…** writes the settings as currently shown, including unsaved tweaks.

When the current settings differ from the selected preset, the panel shows
*Modified*.

**Preset file format.** User presets live in `~/.local/share/CRTPlayer/presets/`, one
JSON file each:

```json
{
  "format": "crtplayer-preset",
  "version": 1,
  "name": "My Trinitron",
  "params": {
    "scanStrength": 0.3, "scanWidth": 0.55, "scanLines": 0,
    "maskType": 1, "maskStrength": 0.2, "maskScale": 1,
    "curvature": 0.04, "cornerRadius": 0.02, "overscan": 0, "includeBars": false,
    "bloom": 0.1, "glow": 0.1, "chroma": 0, "bleed": 0, "noise": 0, "flicker": 0,
    "vignette": 0.1, "brightness": 1.05, "contrast": 1.05, "saturation": 1, "warmth": 0
  }
}
```

Missing keys take defaults, and out-of-range values are clamped. The 1.1 keys are
`scanType` (0–5), `vhsJitter`, `vhsTracking`, `vhsHeadSwitch`, `vhsChromaDelay`,
`vhsDropouts`, `vhsSoftness`, `dotCrawl`, `rainbow` and `laserRot`. Presets from 1.0
load unchanged, with all of these off. `scanType` 6–10 are the emulator looks (1.3). `pixelHeight` (0 = native),
`pixelWidth` (0 = square pixels) and `pixelFilter` (0 hard, 1 sharp, 2 soft) set the
lowered resolution (1.4). New in 1.7: `persistence`, `colorDepth`, `dither`,
`videoStandard`, `powerEffects`, `channelStatic` and `vcrOsd`.

---

## Screenshots, frame stepping, playlist

**Screenshots** use the camera button, S, Shift+S or Ctrl+S. There are two kinds:

- **Filtered:** exactly what the video area shows, at its current pixel size, with
  effects and bars.
- **Original:** the decoded frame with no effects. It is PAR-corrected and rotated, at
  native size, for example 853×480 for an anamorphic DVD frame.

Files go to `~/Pictures/CRT Player/` as `<video>_<hh-mm-ss.mmm>_<crt|original>.png`.
You can change the folder and the default type in *Picture → Playback*.

**Frame stepping** with `.` and `,` pauses first and moves exactly one frame. The
backward step seeks accurately to the previous frame's interval, so it lands correctly
even after GStreamer clips timestamps.

**Playlist** (L):

- Add files with the + button, drag-and-drop, or the Open dialog.
- Reorder by dragging.
- Double-click an item to play it.
- Playback advances automatically at the end of each file.
- The playlist is saved between sessions.

---

## Everyday playback

**Resume.** Local files continue where you left them; the message says so, and **Home**
starts over.

- The position is saved when you switch files, every 15 s while playing, and on quit.
- Positions in the first or last 30 seconds aren't kept, and playing to the end clears
  the entry.
- Positions for the 300 most recently used files live in
  `~/.local/share/CRTPlayer/resume.json`.
- Jellyfin videos resume from the server's own position instead.

**Recent files.** Click the small arrow on the **Open** button, or in desk mode
right-click → *Recent files*. Each entry shows where you'd resume.

**Subtitle files.** Subtitle files next to the video load automatically: `Movie.srt`
first, then others such as `Movie.en.srt`. The subtitle button's menu lists them, along
with *Load subtitle file…* and *No subtitle file*.

- For Jellyfin videos, the server's external subtitle files appear in the same menu.
- Subtitles are drawn into the picture, so they get the CRT look (and appear in desk
  mode).
- SRT, ASS/SSA, WebVTT and SUB are supported.
- Short SRT files are handled too. GStreamer can't recognise very short SRT files on its
  own, so the player plays subtitles from a padded local copy.

**Speed.** **−** and **+** change the speed in 0.25× steps (0.25× to 4×), and
**Backspace** returns to normal. Voices keep their natural pitch.

**A–B loop.** Press **R** at the start of the part to repeat, then **R** again at its
end. The range shows on the seek bar, and a third **R** turns it off.

**Chapters.** For files with chapters (e.g. MKV), **Shift+PgDn** / **Shift+PgUp** jump
between them. The seek bar shows a tick at each chapter, and hovering shows the chapter
name.

**Seek-bar previews.** Hovering over the seek bar shows a small frame from that point,
for local files. Previews use the nearest keyframe (fast, and they never disturb
playback), so they can be a few seconds off in files with sparse keyframes. Jellyfin
streams show the time and chapter only.

**Keeping the screen awake.** While a video plays, the player asks the desktop not to
dim, lock or sleep. It's released as soon as you pause or stop, so the normal power
settings apply again.

- **KDE:** it uses `org.freedesktop.ScreenSaver` and PowerManagement.
- **GNOME and others:** it uses the desktop portal.
- **To turn it off:** *Settings → Playback → Keep the screen awake while playing*.

**System report.** *Settings → Playback → Copy system report* puts a summary on the
clipboard for bug reports. It covers:

- the OS, session, desktop and whether it's gamescope;
- the Qt version, GPU/OpenGL and screens;
- the GStreamer version, available hardware decoders and missing plugins;
- the current video's format and decoder;
- the look, desk mode, and whether you're signed in to Jellyfin.

It contains no file names, paths, user names or server addresses.

---

## Controllers, media keys and Steam Game Mode

**Game controllers.** Any controller SDL2 knows works: Xbox, PlayStation, Switch Pro,
the Steam Deck, and Steam Input's virtual pad.

| Button | Action |
|---|---|
| A | Play / pause |
| B | Back: fly out of fullscreen, leave fullscreen, close a panel |
| X | Next subtitle track |
| Y | Fly in / out (desk mode), or fullscreen |
| View (Select / Back) | Desk mode on / off |
| Menu (Start) | Show the controls (desk mode: next scene) |
| LB / RB | Previous / next CRT preset |
| LT / RT | Previous / next playlist item |
| D-pad ← / → | Seek −10 s / +10 s (hold to keep seeking) |
| D-pad ↑ / ↓ | Volume |
| L3 / R3 (stick press) | Previous / next chapter |
| Left stick | Desk mode: turn the set |
| Right stick (up/down) | Desk mode: bigger / smaller |

**How it behaves:**

- **Menus and panels:** when one has focus (e.g. the Jellyfin browser), the D-pad, **A**
  and **B** work as arrow keys, Enter and Escape.
- **Focus:** the controller only acts while CRT Player is the active app, so it won't
  interfere with a game you're playing (in Game Mode it always acts).
- **SDL2 is optional:** it's loaded when the player starts (Bazzite includes it). If it's
  missing, controller support is simply off.

**Media keys, KDE's media widget and KDE Connect.** The player registers on D-Bus as a
media player (MPRIS, `org.mpris.MediaPlayer2.crtplayer`). The following all control it:

- your keyboard's play/pause/next keys;
- the Plasma media widget and lock screen;
- KDE Connect on your phone;
- `playerctl`.

It shares the title and length. For network streams (Jellyfin) no address is shared.

**Steam Game Mode.** Add CRT Player as a non-Steam game (the AppImage works) and launch
it from Game Mode:

- It starts fullscreen and takes controller input.
- Game Mode can't show a desktop through a window, so desk mode uses the **desk scene**
  (see [Scenes](#scenes)).

![Desk mode with the room backdrop](docs/images/room-backdrop.jpg)

---

## Desk mode: a 3D TV on your desktop

Press **T**, or choose *Open the regular player window* from the desk menu to go back.
The regular window hides, and the video appears on a 90s-style CRT television that
stands on your desktop. There is no window frame: everything around the set is
transparent, and clicks beside it go to whatever is behind.

![The 3D set on a desktop](docs/images/desk.jpg)

| On the set | Action |
|---|---|
| Drag | Turn it (with a little inertia when you flick) |
| Shift+drag, Alt+drag or middle-drag | Move it around the screen |
| Scroll | Make it bigger or smaller |
| Double-click, or F | Fly into fullscreen |
| Double-click or Esc (in fullscreen) | Fly back out to the desk |
| Right-click | Menu: fly in/out, open files, playlist, recent files, Jellyfin continue watching, CRT preset, set, scene (and its mood, fog, wood, quality), screen shape, keep on top, reset position, back to the regular window |
| Hover | Control strip under the set (hides after 2.5 s) |

**Flying in.** The set turns to face you and comes forward while the desktop dims,
until the glass fills the screen. The flight then dissolves into the normal fullscreen
player image, which is *pixel-identical* to the regular fullscreen view (it's measured
in the tests). Flying out reverses this, and the set returns to exactly where it was.

![The flight into fullscreen](docs/images/desk-flight.jpg)

**Set** (right-click → *Set*):

- **CRT television** (default): the 90s set. Its glass bulges according to the preset's
  Curvature.
- **Flat-face CRT:** the same set with a flat glass front, like the late-90s "flat
  screen" tube TVs.
- **Flat-panel screen:** a slim display with thin bezels on a neck and base, a small
  white power LED, and a matte anti-glare screen that shows a soft sheen instead of
  sharp reflections. Portrait video pivots the panel, and its stand stays upright on
  the desk.
- **80s wood-grain console TV:** a boxy walnut-veneer cabinet on four tapered legs,
  with a fabric speaker grille, a chrome trim line, and two knobs (metal flange, fluted
  body, brushed-aluminium cap with an indicator line).
- **Broadcast monitor (PVM):** a boxy metal studio monitor with a row of front-panel
  buttons.
- **Beige PC monitor:** the classic deep beige CRT on a tilt-swivel foot.
- **Arcade cabinet:** an upright cabinet.
  - **Its parts:** a backlit marquee, a speaker panel, the monitor behind a black bezel,
    a two-player control panel (joysticks with ball tops, six lit buttons each and a
    start button), a coin door with glowing coin returns, and side art edged with
    T-molding.
  - **The marquee** shows the video's title, tidied up: `Oblivion.2013.1080p.BluRay.x264`
    becomes **OBLIVION**.
  - **Four art styles** (*Set → Arcade art*): **Space** (stars, a ringed planet, a neon
    grid), **Sunset** (a striped sun over mountains), **Neon** (stripes and triangles)
    and **70s woodgrain** (with a bold stripe).
  - **Vertical games:** a portrait video turns the monitor inside the cabinet, as in a
    vertical shoot-'em-up cabinet; the cabinet stays upright.
  - **In the scenes:** the cabinet always stands on the floor. In the wall-mounted scene
    it stands against the wall; in the 90s CG room the objects spread out around it.

The tube sets (all except the flat-face CRT and the flat panel) have glass that bulges
with the preset's Curvature.

![Wood console, broadcast monitor and beige PC monitor](docs/images/desk-sets-2.jpg)

![The arcade cabinet's four art styles](docs/images/set-arcade.jpg)

Your CRT effects still apply to the picture on every set; press **B** for a clean
picture on the flat panel. Every set flies into fullscreen the same way, and lands on
the same exact fullscreen image.

![The three sets, a flat panel from the side, and a portrait flat panel](docs/images/desk-sets.jpg)

### Scenes

Right-click the set → **Scene** (or *Scene settings…* for a window with every option):

- **Your desktop:** the set on your transparent desktop, as before.
- **Desk:** the set on a wooden floor in a room, against a plastered wall with a skirting
  board.
- **Wall-mounted TV:** the set hangs on a wall, with your own pictures beside it.
- **Movie theater:** a big screen with curtains, in a cinema, with a film look.
- **90s CG room:** the set in an early-90s ray-traced world ("The Mind's Eye" style).

The *Scene settings* window shows a small preview of each scene, rendered from your
current view.

**What the desk scene does:**

- **The picture lights the room.** Its colour and brightness fall on the desk in front of
  the set, and bounce softly onto the wall. The desk's satin clear-coat reflects the
  screen at the right angles.
- **Mood:**
  - *Evening:* a warm lamp.
  - *Night:* the lamp dimmed.
  - *Lights off:* only the screen lights the room.

  The set's own lighting follows the mood.
- **Fog:** *light haze* or *thick fog*, with a strength slider. The fog drifts slowly, is
  denser near the desk, and glows in the picture's colours around the set.
- **Desk wood:** walnut, oak or cherry. The boards are ~15 cm planks with seams and end
  joints.
- **Quality:** fog detail. Lower it if desk mode stutters with fog on.

**The desk and wall-mounted scenes are real rooms:**

- side walls in the same finish as the back wall, with the skirting board running round;
- a plaster ceiling with a cornice;
- softly shaded corners;
- the lamp, the TV and the picture lights light them all.

The camera stays inside the room: not through a side wall, not above the ceiling.

![The desk and wall-mounted scenes as rooms](docs/images/scene-rooms.jpg)

**The wall-mounted TV scene:**

- **The mount:**
  - CRT sets sit on a hotel-style tray with an articulating arm to a wall plate.
  - The flat panel hangs on a slim bracket.
  - The set casts a soft shadow on the wall, and its light spills around it.
- **Pictures:** choose up to four images from your files (*Scene settings → Wall and
  pictures*).
  - **Layout:** none, one on each side, two on the left, two on the right, or two on each
    side.
  - **Frames:** black, wood or gold moulding with a mat. Each frame takes its picture's
    shape.
  - **Beside a deep CRT,** the pictures hang further out, so they stay visible from an
    angle.
  - **Brass picture lights** above each frame wash the pictures in warm light (in the
    evening and night moods).
- **Wall:** warm white, sage green, navy or charcoal paint, or pinstripe or damask
  wallpaper.
- **The same moods and fog** as the desk scene. The floor is far below and uses the wood
  setting.
- **Placement:** *Scene settings → Wall and pictures* has sliders for:
  - **TV height** on the wall;
  - **picture height** (relative to the screen), **picture spacing** from the set, and
    **picture size**.

  The mount is dark metal whatever the set is made of.
- **Your pictures** are remembered; a picture that has been moved or deleted is skipped.

**The 90s CG room:**

- **The world:** an infinite **mirror checkerboard** (or a **neon grid**) under a gradient
  sky, fading into haze at the horizon, with a sun that casts hard shadows and a lens
  flare when you look towards it.
- **Objects**, in five sets (*Scene settings → 90s CG room → Objects*). All of them
  reflect in the floor and cast shadows.
  - **Chrome and marble** (the default): floating chrome spheres, a glass cube, a chrome
    ring and a marble column. They reflect the sky, the floor, each other and your
    picture.
  - **Toybox:** glossy plastic in saturated colours: a striped capsule, a cone, a twisted
    ring, a checked cube, an octahedron.
  - **Organic:** bumpy spotted and marbled forms, a glowing translucent jelly flower, and
    iridescent pearls.
  - **Wooden mannequins:** a troupe of jointed wooden figures walking a loop behind the
    set. Their feet stay planted and their hips rise and fall with each stride.
  - **Mixed:** some of each.
- **Background crowd** (on by default, whichever objects you choose): larger shapes of
  every kind floating in the distance, and wooden figures walking around behind the set.
  Switch it off in *Scene settings → 90s CG room → Background crowd* for the minimal
  look.
- **Your own 3D models:** *Scene settings → 90s CG room → Your 3D models → Choose
  folder…*.
  - **What's read:** OBJ and STL (binary or text) files, up to 6 models of up to 400,000
    triangles each. They load in the background.
  - **How they stand:** each becomes a statue on a marble plinth around the set, lit by
    the scene, casting a shadow and showing in the mirror floor.
  - **Finishes:** marble, bronze, chrome, candy plastic, or their own colours (OBJ vertex
    colours).
  - **Sizing:** STL files are treated as Z-up (the 3D-printing convention). Models are
    centred, stood on their base and scaled to statue size.
  - **Files that can't be read are skipped,** and the settings window says why.
  - **One limit:** they are not seen in the chrome spheres' reflections.
- **The TV** stands on a **chrome pedestal** or a **marble plinth**, or **floats**. Its
  picture glows on what is around it.
- **Palettes:** *Workstation* (teal), *Sunset* (orange and purple, with the sun in view),
  *Deep space* (stars and a ringed planet on the horizon).
- **Period touches:**
  - **Tile reveal:** when a video starts, or when you resume after a pause of 10 seconds
    or more, the picture resolves tile by tile, as if being ray traced. Skipping, scrubbing
    and short pauses don't trigger it.
  - **Demo-reel orbit:** once playback has been stopped for 10 seconds (and you haven't
    skipped or scrubbed), the camera slowly circles the set. It never turns while you
    scrub, skip or pause briefly.
  - **90s colour banding** (off by default).
- **The fly-in** is a sweeping camera move that arcs round into the screen.
- **The camera** can go all the way round, above the floor.
- **Quality** sets how many reflections of reflections are traced, and turns the shadows
  on or off.

![The 90s CG room: Workstation, Sunset, Deep space, neon grid](docs/images/scene-cg.jpg)

**The movie theater:**

- **The screen:** your picture on a big screen in a stage frame (a proscenium) with gold
  trim, under a scalloped velvet valance.
- **Curtains:** red velvet curtains **open when playback starts**, stay open while you
  pause part-way through, and **close at the end**.
- **House lights** dim as the curtains open. A pair of sconces either side of the stage
  stays faintly lit.
- **Masking:** black masking closes in to the film's shape (widescreen, 16:9, 4:3), and
  glides to the new shape when the next video is different.
- **You sit in the auditorium:** twenty rows of 3D stadium seating, each row a step
  higher than the one in front, with two aisles and step lights on the stairs. It is all
  one room with the screen, walls and sconces.
  - **Scrolling** moves you between rows, from row 3 to the back row.
  - **Dragging sideways** moves you along the row, inside the walls.
  - Your eye stays at seated height for the row you're in.
  - The theater uses a wide, human field of view, so you see the rows in front and the
    room.
- **Haze:** with fog on, the projector's beam shows in the air, carrying the picture's
  colours, with dust drifting in it.
- **The film look.** The theater shows the film look (*Film Print (35mm)*), and leaving it
  brings your own look back. Choose another in *Scene settings → Theater → Picture*:
  35mm print, worn 16mm print, drive-in, or keep my current look.
- The set you chose for the other scenes is kept; the theater has its screen instead.

![The movie theater from a middle row](docs/images/scene-theater.jpg)

**Also:**

- **In the scenes, the camera stays above the floor.** Dragging, the gamepad stick and
  saved positions can't take it below the boards. On the transparent desktop the set can
  still be seen from below.
- The gamepad's **Menu** button switches scenes in desk mode.
- In Steam Game Mode the desk scene is always used, since there's no desktop to show.
- All scene settings are remembered.

![The desk scene: evening, night, lights off, thick fog](docs/images/scene-desk.jpg)

![The wall-mounted TV scene: pictures, frames, walls, picture lights](docs/images/scene-wall.jpg)

**Screen shape** (right-click → *Screen shape*):

- **Follow the video** (default): the tube takes the video's shape. Widescreen video
  gets a widescreen set, and portrait video gets the same set pivoted 90°, like a
  vertical arcade monitor. Very wide video (wider than 2.6:1) is letterboxed inside a
  2.6:1 tube.
- **Classic 4:3 tube:** always a 4:3 set, with other shapes letterboxed inside it, the
  way widescreen really looked on an old TV. Flying in then ends on that 4:3 picture.

![Screen shapes](docs/images/desk-shapes.jpg)

**How it looks.** The glass is real curved geometry, so its bulge follows the preset's
Curvature. All the CRT and analog effects play on it. The picture lights the tunnel
around the glass and faintly reflects on the bezel. The glass reflects a dim room
(stronger at grazing angles), and the set casts a soft shadow on the "desk" beneath it.
When you tilt the set until you're looking up from below that desk, the shadow fades
out, since it lies on the desk's upper side. Its click area disappears with it.

![Right-click menu](docs/images/desk-menu.jpg)

**What's remembered:** the position, angle, size, set, screen shape and *keep on top*.

**Where it works:**

- **KDE Plasma on Wayland** (Bazzite's default) and on X11, or any desktop with a
  compositor.
- On **Wayland** the set behaves like a normal window: other windows can go in front of
  it. Turn on *Keep on top* to stop that. Apps can't place themselves *behind* other
  windows on Wayland; a KDE window rule ("Keep below") can do it.
- **Steam Game Mode (gamescope)** has no desktop to show through, so desk mode
  automatically uses the room backdrop there.

---

## Jellyfin

Open the **Jellyfin** panel with **Ctrl+J**, or with the server button in the control
bar.

**Signing in.** Enter your server's address (for example `http://192.168.1.20:8096`, or
your `https://` address), your user name and password.

- The player stores **only the access token Jellyfin issues**, never your password. It's
  kept in `~/.config/CRTPlayer/CRTPlayer/jellyfin.json`, readable only by you (0600).
- The player stays signed in across restarts.
- **Sign out** revokes the token on the server and removes it from disk.

**Browsing.** The home view shows **Continue watching** (with progress bars) and your
libraries. Folders, series and seasons open with a double-click; the search field
searches all libraries.

Big libraries load **100 items at a time**: the next 100 arrive as you scroll near the
end, and the heading shows how many are loaded (*Movies — 300 of 1,234*). There's no upper
limit.

**Playing:**

| Action | Result |
|---|---|
| Double-click a video | Plays it, resuming where you left off |
| Right-click → *Play from the beginning* | Plays from the start |
| Right-click → *Add to playlist* | Queues it; Jellyfin items and local files can be mixed |
| Right-click → *Play converted by the server* | Has the server convert it, even though this computer could play the original |

**Original file or converted by the server.** Before each video the player asks the
server how to play it. It sends the list of formats this computer's GStreamer can open
and decode.

- **Original file** ("direct play") whenever this computer can decode it. Everything
  works on it: every CRT effect, lower resolution, desk mode, screenshots, all audio and
  subtitle tracks in the file, and seeking (via HTTP range requests).
- **Converted by the server** when this computer can't decode the file (a codec it
  lacks), or when the file is over the **Quality** limit at the bottom of the Jellyfin
  panel. The server sends H.264 video with AAC audio as HLS, which plays and seeks
  normally, with every CRT effect. Subtitles in the file are offered as files from the
  server (*Subtitles* menu). Only one audio track comes with a conversion.
- **Quality:** *Original file* (the default) never limits. *Up to 40 … 1 Mbit/s* has the
  server convert anything above the limit: for a slow connection, or a server away
  from home.
- **If an original won't play here** (a codec GStreamer reports only once it opens the
  file, or a damaged file), the player asks the server for a conversion instead, once,
  and carries on from the same point.
- The technical info overlay (**I**) shows which it is: *Jellyfin — original file* or
  *converted by the server: video format*.

**The token:**

- For original files it travels in a request header, not in the address.
- A conversion's HLS segments are fetched separately, without the player's headers, so
  their addresses carry the token, as with Jellyfin's own apps.
- Either way it never shows up in the window title, playlist, info overlay, screenshots or
  logs. Playlist entries store only the item's ID and title, and become an authenticated
  stream when played.
- The server's conversion is ended when playback moves on.
- **Progress syncs back to the server.** Start, pause/resume, position every 10 seconds,
  and stop are all reported. Your resume points and *watched* marks stay in step with
  Jellyfin's other apps.

**Server versions.** The player reads the server's version and uses the matching API
routes. Jellyfin 10.9 moved several endpoints; both older and current servers are
supported.

**Current limitations:**

- **A conversion carries one audio track** (the file's default) and no embedded image
  subtitles (PGS/DVD). Text subtitles are offered as files.
- **HTTPS needs a certificate your system trusts.** A certificate from a public authority
  (for example Let's Encrypt behind a reverse proxy) works. Untrusted self-signed
  certificates are refused. Plain `http://` works on a home network.

---

## Where settings are stored

| What | Where |
|---|---|
| Window, volume, scaling, crop, current CRT settings, playlist, screenshot options, desk position | `~/.config/CRTPlayer/CRTPlayer.conf` |
| User presets | `~/.local/share/CRTPlayer/presets/*.json` |
| Jellyfin sign-in (server, user, access token, device ID; never the password) | `~/.config/CRTPlayer/CRTPlayer/jellyfin.json` (owner-only) |
| Resume positions (local files) | `~/.local/share/CRTPlayer/resume.json` |
| Recent files | in `CRTPlayer.conf` |

Settings are saved whenever the player exits: window close, Ctrl+Q, or logout.

---

## Troubleshooting

**"Missing codec or GStreamer plugin: H.265 (Main Profile) decoder"** (or similar)

- The message names exactly what GStreamer could not find.
- On Bazzite most codecs are preinstalled. Check with
  `gst-inspect-1.0 | grep -i -E 'dec|demux'`.
- Proprietary codecs usually come from `gstreamer1-plugin-libav`,
  `gstreamer1-plugins-bad-freeworld` or `gstreamer1-plugins-ugly`. Layer them with
  `rpm-ostree install …` and reboot.
- If only the audio decoder is missing, the video still plays and a notice names the
  missing audio decoder.

**"OpenGL 3.3 is required" or a black video area**

- Check the driver with `glxinfo -B` (X11) or `eglinfo` (Wayland).
- On NVIDIA, make sure the proprietary driver is active (Bazzite's `-nvidia` images).
- Try the other platform: `QT_QPA_PLATFORM=xcb ./crtplayer` or
  `QT_QPA_PLATFORM=wayland ./crtplayer`.

**Hardware decoding**

- *Picture → Playback* lists the decoder in use and which hardware decoders GStreamer
  knows about. Press I to see it live.
- If a hardware decoder errors, the player reopens the file in software at the same
  position and says so.
- To rule out hardware decoding entirely, uncheck it or start with `--no-hw`.
- VA-API needs the `va` GStreamer plugin plus Mesa's VA drivers
  (`mesa-va-drivers-freeworld` on Fedora-based systems for H.264/HEVC).
- Vulkan video decoders are deliberately not prioritised, because they cannot hand
  frames to this renderer in system memory.

**Stutter or dropped frames**

- The composite and tape effects are the most expensive part of the shader, at about
  25 extra texture reads per pixel. They're cheap on any real GPU, but on an old
  integrated GPU at 4K, turn off Dot crawl, Rainbow and Tape softness first.
- Press I and look at *A/V sync*. It shows how late each frame reaches the screen
  relative to the audio clock.
- If it stays high, try Bypass (B). If Bypass helps, lower Bloom/Glow or use a smaller
  window.
- Pixel-format conversion happens on the CPU only for unusual formats such as 10-bit.
  NV12/I420 go straight to the GPU.

**`error while loading shared libraries`** when running the prebuilt binary

- Your system lacks a Qt or GStreamer library, or has an older one.
- Use `scripts/build-bazzite.sh` to build against your own system.

**HDR (PQ/HLG) video looks washed out**

- The renderer works in SDR and does not tone-map. 10-bit sources are converted to
  8-bit.

**Subtitles look like they are "on the TV"**

- They are. Subtitles are drawn into the frame before the CRT pass, so they receive the
  same curvature and scanlines as the picture.

**The settings panel doesn't react to the mouse wheel**

- That's deliberate. Scrolling over the panel scrolls it, rather than changing
  whichever slider is under the pointer. Click a control first if you want to adjust it
  with the wheel.

**Desk mode: black around the TV instead of the desktop**

- Your session has no compositor, or it's gamescope. On KDE, check
  *System Settings → Display → Compositor* ("Enable on startup").
- On **X11**, some window managers stop compositing for fullscreen windows. KDE's
  "Allow applications to block compositing" option is one such trigger. Desk mode uses
  a fullscreen transparent window.

**Desk mode: clicks near the TV don't reach the window behind**

- The set, its soft shadow and the control strip (while visible) take clicks. On
  **X11** the clickable area includes the shadow, because there the window's shape
  also limits what can be drawn. On Wayland only the set's outline and the strip are
  clickable.

**The AppImage opens, but the video area stays empty or see-through**

- 1.9.2 and later use your system's Qt when it's suitable, which fixes this on Bazzite.
- If it still happens, try `CRTPLAYER_QT=bundled ./CRT_Player-…AppImage`, and send the
  system report (*Settings → Playback → Copy system report*).

**The screen dims while watching**

- Check that *Settings → Playback → Keep the screen awake while playing* is on.
- The system report's *Keep awake* line shows whether the desktop accepted the request.

**"Some videos won't play yet" / "GStreamer is incomplete"**

- CRT Player decodes with your system's GStreamer plugins, and the message lists what's
  missing and the command for your distribution. See
  [Other Linux distributions](#other-linux-distributions).
- On Arch, the usual missing piece is `gst-plugins-good` (plus `gst-libav` for H.264 and
  AAC).

**Jellyfin: "connection refused", "not found" or "did not answer in time"**

- Check the address and port. Jellyfin uses 8096 for `http://` and 8920 for `https://`
  by default; the Jellyfin web page in your browser shows the right address.
- If the server is on another computer, make sure its firewall allows the port.

**Jellyfin: "Secure connection failed"**

- The server's certificate isn't trusted by this system (usually a self-signed one).
- Use `http://` on the home network, or a certificate from a trusted authority.
  Alternatively, add your own certificate authority to the system trust store, which
  both parts of the player (the API and the video stream) use.

**Jellyfin: a video won't play, but browsing works**

- The file's codec may be missing on this computer. The error names it, as it would for
  a local file.
- Streams use GStreamer's HTTP source (`souphttpsrc`, in `gstreamer1-plugins-good`),
  which Bazzite includes.

**Resetting everything**

```bash
rm ~/.config/CRTPlayer/CRTPlayer.conf
```

This keeps your user presets.

---

## Code layout

```
src/
  main.cpp                 entry point, OpenGL format, CLI
  playback/Player.*        GStreamer playbin + appsink, seeking, tracks, decoder policy, errors
  render/Geometry.*        pure aspect/scaling/orientation math (unit-tested)
  render/CrtRenderer.*     GL resources: frame upload, conversion, blur, CRT pass, offscreen capture
  render/VideoWidget.*     QOpenGLWidget: layout, compare divider, screenshots, sync statistics
  settings/CrtParams.*     parameter model, parameter table, built-in presets, preset JSON
  settings/PresetManager.* user presets on disk (save/rename/delete/import/export)
  settings/AppSettings.*   persistent settings (QSettings)
  ui/                      control bar, CRT/Display/Playback/Playlist panels, theme, vector icons,
                           WheelGuard (wheel scrolls panels, not sliders)
  app/MainWindow.*         wiring, shortcuts, fullscreen and auto-hide, overlays
  app/Automation.*         scripted driver used for verification
  app/DeskWindow.*         desk mode: transparent screen-sized window, input mask, control strip
  jellyfin/JellyfinClient.* Jellyfin API: sign-in, browsing, images, stream URLs, playback reporting
  ui/JellyfinPanel.*       sign-in form and poster browser
  render/DeskRenderer.*    procedural 3D cabinet, projection, silhouette, shading
  render/DeskView.*        desk mode view: poses, flight, crossfade to flat, mouse interaction
shaders/                   quad.vert, convert.frag, downsample.frag, blur.frag, crt.frag,
                           desk.vert, desk.frag (3D cabinet, glass, shadow)
tests/                     unit tests + automation scripts
scripts/                   build-bazzite.sh, install-local.sh, make-test-media.sh, run-verification.sh,
                           check-effects.py, check-desk.py (measure the captures),
                           run-jellyfin-tests.sh + check-jellyfin.py (with tests/jellyfin_mock.py),
                           build-appimage.sh
```

---

## Verification

[VERIFICATION.md](VERIFICATION.md) lists the samples and aspect ratios that were
actually tested and what was measured. It also separates tested behaviour from
behaviour that is only intended.

To reproduce the checks:

```bash
scripts/make-test-media.sh /tmp/crt-media           # needs ffmpeg
scripts/run-verification.sh build/crtplayer /tmp/crt-media /tmp/crt-results
```
