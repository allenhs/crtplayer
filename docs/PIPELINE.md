# Playback and rendering pipeline

## Overview

```
                    host GStreamer registry (installed plugins)
                                   │
 file/URI ─► playbin ─► demuxer ─► decoder (VA-API/NVDEC if available, else libav/dav1d/…)
                │                        │
                │                        ▼
                │            [subtitle overlay, deinterlace]
                │                        │
                │                        ▼
                │          videoconvert (passthrough for NV12/I420/RGBx)
                │                        │
                │                        ▼
                │           appsink  sync=true, system memory
                │                        │  new-sample (streaming thread): keep latest frame
                ▼                        ▼
      autoaudiosink (clock)       Qt GUI thread ─► QOpenGLWidget::paintGL
                                         │
                                         ▼
   ┌───────────────── GPU (OpenGL 3.3 core) ─────────────────────────────────────┐
   │ 1. upload planes      Y(R8)+UV(RG8) / Y+U+V(R8) / RGBA — honours row stride   │
   │ 2. convert.frag       YCbCr→RGB (BT.601/709/2020, limited/full range),         │
   │                       rotation + mirror  ──► oriented image texture (mipmapped)│
   │ 3. downsample + blur  only when Bloom/Glow > 0: long side 480 px, 2× separable │
   │                       Gaussian ──► blur texture (black border)                 │
   │ 4. crt.frag           one full-viewport pass, in signal-chain order:           │
   │                       tube geometry → scanline geometry → SIGNAL (VHS, chroma, │
   │                       composite, bleed, tape/disc noise) → picture controls →  │
   │                       scanlines, mask, glow, bloom, vignette, noise, flicker   │
   └──────────────────────────────────────────────────────────────────────────────┘
```

## Playback and host codecs

- **Codec discovery.** `playbin` autoplugs demuxers and decoders from the host's
  GStreamer registry, so every codec comes from the system install. Nothing is
  bundled.
- **Hardware decoders.** Factories whose class contains `Decoder/Video/Hardware` are
  raised above the software decoders (rank PRIMARY+2, or +3 for the modern `va`
  plugin). They are set to rank NONE when hardware decoding is disabled.
  - Decoders that are disabled by default (rank NONE) are left alone.
  - Vulkan decoders are left alone, because they output Vulkan images rather than
    system memory.
- **Hardware fallback.** If an error comes from a hardware decoder, the file is reopened
  at the same position with hardware decoders disabled, for that file only.
- **A/V sync.**
  - The audio sink provides the pipeline clock.
  - The appsink runs with `sync=true`, so each frame is released at its clock time.
  - `drop=true, max-buffers=2` and QoS let late frames be dropped instead of
    accumulating.
  - The GUI always paints the newest frame and never queues a backlog.
- **Seeking.**
  - Dragging the seek bar issues flushing key-unit seeks. While a seek is in flight,
    only the latest target is remembered, so a drag never builds up a queue of
    flushes.
  - Releasing the bar issues an accurate seek.
  - A watchdog clears a seek that never completes.
- **Missing plugins.** Missing-plugin messages (`gst_missing_plugin_message_get_description`)
  are collected and named in the error or warning.

## Coordinates and aspect

All geometry is computed on the CPU in `render/Geometry.cpp`, which is covered by unit
tests.

- **Display size:**
  `(coded_w × PAR, coded_h)`, with the two values swapped for 90° and 270° rotation.
- **Layout:** `computeLayout(displaySize, mode, crop, videoArea)` returns three things:
  - `imageRect`: the whole picture, which may extend off-screen in Fill mode;
  - `visibleRect`: `imageRect` clipped to the video area;
  - `srcRect`: the part of the picture, in uv coordinates, that `visibleRect` shows.
- **The tube:** the CRT pass receives the tube rectangle, which is:
  - `visibleRect` by default, so bars stay out of the effect;
  - the whole video area when *include bars* is on.

  Everything below is relative to the tube, so the effect adapts to any picture shape.

## Curvature without cropping

For a fragment at tube coordinates `c ∈ [-1,1]²`:

- let `h` be the tube's half-size in pixels, `R = |h|`, and `E = h/R`;
- then `P = c·E` is an aspect-aware position, so curvature is uniform per physical
  distance.

The content position is

```
q = c · (1 + k|P|²) / (1 + k·E²)        (per axis)
```

Why this crops nothing:

- The midpoint of each edge maps onto itself.
- Every other point on the tube boundary has `|P|² ≥ E²`, so it samples at or beyond
  the picture edge.
- Therefore the whole picture fits inside the tube. The only black areas are the
  corners outside the bulged picture edge.

Only Overscan (`q × (1 − overscan)`) and Fill mode crop content.

## Lower resolution ("bigger pixels")

When *Resolution* is set, `pixelatedSize()` works out the lowered size.

- **Rows:** as chosen.
- **Columns:** as chosen, or `rows × display aspect` for square pixels. This accounts
  for PAR and rotation.

After each new frame, the renderer downsamples the oriented picture into a texture of
that size and mipmaps it. It reuses the 4-tap downsample pass: sampling the mipmapped
source at the matching level gives an area average.

The CRT pass then binds this small texture as `uImage`, and the full-resolution picture
as `uImageFull`. Picture fetches on the CRT side reshape their texture coordinate before
sampling:

- **Hard:** snap to the texel centre.
- **Sharp:** snap, but blend across a band one *screen* pixel wide at each texel boundary
  (`uTexelPx` = screen pixels per texel), using the bilinear filter.
- **Soft:** plain bilinear.

The lowered row count also drives the automatic scanline count and the beam styles' mip
level, so scanlines line up with pixel rows. The bypass and "original" side of compare
read `uImageFull`, and never snap.

## Deeper simulation (1.7)

**Colour depth and dither** happen in `fetch()`, on the CRT side only.

- **Bit-depth modes:** the sampled colour is quantised per channel to
  `floor(c·L + 0.5 + t)/L` (`L = 2^bits − 1`).
- **Palettes:** Game Boy maps luminance to four greens. CGA and EGA take the nearest
  entry by luma-weighted distance.
- **Dither:** `t` is a Bayer threshold, found by bit-interleaving `x^y` and `y` and
  reversing, taken over the picture's pixel grid (`uGridSize`: the lowered resolution,
  or the image's own). Every effect that reads the picture therefore sees the reduced
  colours, just as a composite TV sees a console's output.

**PAL.** `signalLines()` becomes 576 and `fieldRate()` 50.

- The subcarrier advances ¾ cycle per line, over an 8-field sequence.
- The V component's sign flips every line (`vsw`) in both the demodulation and the crawl
  carrier.
- A residual hue rotation of ±0.22·(rainbow + crawl) on alternate lines gives the
  Hanover bars.

**VCR on-screen display.** The text is drawn with QPainter (white with a dark outline)
into a texture, and composited in the signal stage *after* the tape distortions, at the
unjittered position, with a slight trailing smear. It still receives noise, the
tracking band, dropouts and the whole tube stage.

**Set moments.** They're driven from the regular view (`VideoWidget::decorate`, shared
with desk mode) and follow the effect clock.

- **Power on and off** divide the tube coordinate (`qc`) by a shrinking vertical, then
  horizontal, scale. They add brightness and a white wash, then fade the dot.
- **Static** is per-pixel hash noise with slow rolling bands. It's mixed in before the
  tube stage, so it gets scanlines and the mask.
- **Channel static** holds until the player's frame serial changes (the new file's
  first frame) and then fades over 0.35 s.

**Persistence.** When persistence is on, `CrtRenderer::draw` first renders the CRT image
to an offscreen target. `persist.frag` then writes `max(current, history · decay)` into a
ping-pong history, and that result is copied to the output.

- The decay is `exp(−Δt/τ)` per channel, with τ = 4 ms + persistence × 120 ms, and
  blue's τ × 0.45.
- The history resets when the output size changes.
- Screenshots render without it (`history = false`), and so does the before/after split.

## Signal stage: VHS tape and composite / LaserDisc

These run on the *content position* after curvature, so they bend with the glass and
then pass through the scanlines and the mask, like real signal defects would.

Their units come from the picture, not the window. The shader recovers the full
picture's size and origin from `uImg` and `uSrc` (which also covers Fill mode, where
only part of the picture is visible), and then works in:

- **signal lines:** 480 NTSC lines down the picture;
- **samples:** 640 luma samples across the picture.

That's why a tracking band or head switching looks the same on vertical, 4:3 and
ultrawide sources.

- **Timebase jitter:** a per-line horizontal offset (hash of line and field) plus a
  slow sine wave.
- **Tracking error:** a Gaussian band about 4.5 % of the picture tall that rolls
  downward. Inside it, lines are displaced, and white streaks and noise are added.
- **Head switching:** the bottom 4.5 % of the picture skews sideways and gets noisier
  toward the bottom edge.
- **Chroma delay:** I/Q is taken from a position up to 7 samples to the left, so colour
  trails to the right of the detail.
- **Dropouts:** some lines get a short white streak, chosen at random per field.
- **Tape softness:** a Gaussian low-pass on luma. Chroma is low-passed more strongly.
- **Composite model:** the colour subcarrier runs at about 4 samples per cycle. Its
  phase inverts every line and advances a quarter cycle per field, a 4-field sequence.
  - *Rainbow:* luma detail is multiplied by the carrier and low-passed (7 taps). Fine
    patterns near the carrier frequency decode as false colour.
  - *Dot crawl:* local chroma minus wide-band chroma (chroma gets a 3× wider kernel,
    matching its lower bandwidth), modulated by the carrier and added to luma. The dots
    appear only along colour edges and travel between fields.
- **Laser rot:** sparse cells (1/150 of the picture height) light up as coloured
  speckles, re-chosen about 7 times a second.

**Cost:** about 25 extra texture reads per pixel with every effect on, and none for
effects set to 0 (each is skipped by a branch).

## Scanline styles

All six styles share the same line geometry, which follows the curved content
coordinate. What differs is the beam profile across a line:

| Style | Beam profile |
|---|---|
| Soft | Gaussian; sigma grows slightly with luminance |
| Sharp | Flat top with smooth but narrow edges, giving hard gaps |
| Dynamic | Gaussian whose sigma runs from 0.45× (black) to 1.7× (white) |
| Interlaced | Soft profile shifted half a line on every odd field (59.94 Hz) |
| VGA double-scan | Twice the lines when each still gets at least 2.5 px. One thin gap per line, never narrower than about 1 px |
| Pixel beam | Samples the picture once per low-res pixel centre (`lines` × aspect columns), and adds a Gaussian horizontal beam profile |

Brightness is compensated for the gaps, and every style fades out below about 2 px per
line to avoid moiré.

### Beam reconstruction (styles 6–9)

These styles don't darken a continuous image. They rebuild it from beams.

1. **Sample at scanline resolution.** The CPU picks the mip level (`uSrcLod`) whose row
   count matches the number of scanlines over the picture. Picture fetches then use
   `textureLod`, so the source behaves like a signal of that many lines.
2. **Evaluate nearby lines.** For each output pixel, the nearest 2 lines (4 for the
   Lottes style) are evaluated at their centres. Each goes through the complete signal
   stage (`sourceColor`), so VHS, composite and LaserDisc artefacts travel through the
   beams.
3. **Treat each line horizontally:**
   - Lanczos-like sharpening against the neighbouring source pixels; or
   - for the Lottes style, a Gaussian `exp2(-3·dx²)` across the three nearest source
     pixels. The signal-stage result is carried as a per-line difference.
4. **Sum the beams in linear light** (gamma 2.2–2.4). The beam profile is
   `exp(-(d/σ)^shape)`, with σ interpolated *per colour channel* between a dark and a
   bright width by `channel^0.6`. `shape` is 2 (Gaussian), 2.4 (easymode) or 4 (flat
   top, hyllian).
5. **Compensate brightness** by dividing by 60 % of the beam's mean over a line, then
   convert back to gamma. Full compensation would clip merged bright beams and erase
   their structure.

| Style | σ dark → bright | Shape | Sharpening | Lines |
|---|---|---|---|---|
| 6 geom | 0.22 → 0.37 | 2 | 0.45 | 2 |
| 7 lottes | 0.29 (constant) | 2 | pixel Gaussian | 4 |
| 8 easymode | 0.13 → 0.36 | 2.4 | 0.8 | 2 |
| 9 hyllian | 0.27 → 0.40 | 4 | 1.2 | 2 |

Beam width scales these values by 0.8–1.25.

**Style 10 (MAME HLSL-style)** multiplies the picture by
`0.15 + 0.85·|cos(π·f)|^h`, where the exponent `h` (0.6–2.2) comes from Beam width, and
compensates by the average of that curve.

**Cost:** each contributing line costs one signal-stage evaluation plus two fetches (for
the Lottes style, four: one for the signal difference and three pixel taps). With the
Lottes style's four lines, that's four signal evaluations and sixteen extra fetches per
pixel. Styles 0–5 are unaffected.

## Other effects

- **Tube outline.** Corner rounding is an anti-aliased rounded-box signed distance
  function in curved tube space.
- **Scanlines** follow the curved content coordinate.
  - The beam is Gaussian, and its width grows with luminance.
  - Brightness is compensated for the dark gaps.
  - The effect fades out when a line would be smaller than about 2 pixels, which
    prevents moiré.
- **The phosphor mask** is evaluated in curved glass pixels divided by *mask scale*, so
  it bends with the tube.
  - Patterns: aperture grille (RGB stripes), shadow mask (staggered triads), slot mask
    (stripes with staggered gaps).
- **Colour bleed** mixes a 6-tap trailing average of I/Q (YIQ) into the pixel while
  keeping Y sharp.
- **Chromatic aberration** offsets red and blue radially. The offset grows toward the
  edges, plus a small horizontal misconvergence.
- **Bloom and glow** sample the blur texture:
  - glow adds halation that fills the mask and scanline gaps;
  - bloom adds a bright-pass halo.

  The blur is computed at a fixed size relative to the picture, so its radius looks the
  same for SD, 4K, ultrawide and vertical sources.
- **Bypass** draws the picture with the same layout and no effects.
- **Compare** evaluates both paths in the same pass: the pixels left of the divider use
  the bypass path.
- **Animation.** Noise, flicker, the VHS and composite effects, laser rot and the
  interlaced style are animated by a timer capped at 60 Hz, which runs only while
  playing.

## Screenshots

- **Original** renders the oriented image texture through the bypass path into an
  off-screen framebuffer at the PAR-corrected native size.
- **Filtered** renders exactly the current draw parameters into an off-screen
  framebuffer the size of the video area.

## Threads

| Thread | Work |
|---|---|
| GStreamer streaming threads | Decode, then store the newest `GstSample` under a mutex and post one queued `frameReady` signal (coalesced) |
| GUI thread | Bus messages (via a sync handler that forwards to the Qt event loop), UI, and all GL work |

## Desk mode (3D set)

```
 same CRT pipeline ──► renderToTexture: glass-shaped CRT image (mipmapped)
      │                  (flat curvature/corners off: the glass mesh provides them)
      │
      ├─► blur texture ──► light spill on the tunnel and bezel
      ▼
 desk.vert/desk.frag: procedural cabinet + curved glass + contact shadow
      │   premultiplied alpha, transparent background
      ▼
 transparent fullscreen window; input region = the set's silhouette
```

**Window.** Desk mode uses one borderless, *fullscreen*, translucent window from the
start. The set is drawn inside it, and the window's mask (the input region on Wayland,
the window shape on X11) is the convex hull of the projected cabinet, its shadow, and
the control strip. Clicks anywhere else go to the windows underneath.

Because the window never changes size or position, the fly-in is purely a camera move.
Nothing asks the compositor to resize or move a window, which Wayland clients can't do
anyway. When the flight ends, the mask is removed and the window is an ordinary opaque
fullscreen player.

**Geometry.** The cabinet is generated in code from matched rounded-rectangle outlines,
each with 44 points, so any two can be lofted point-to-point:

- the bezel face, as a ring between the outer outline and the screen opening;
- the tunnel from the opening down to the glass rim;
- the lip, the body, and a taper to the tube housing, then a back cap;
- a 56×42 glass grid.

The glass bulge `b·(1−(2x/A)²)·(1−(2y)²)` is applied in the vertex shader. It is zero
along the whole rim, so the glass meets the tunnel exactly. `b` follows the preset's
Curvature.

**Sets.** One builder makes all six sets. The wood console and the PVM are boxy lofts;
the wood console adds knobs (front-face cylinders) and legs, and the beige monitor adds a
deep taper and a tilt-swivel foot. Legs and feet are built upright in the rolled frame,
like the panel's stand. Wood grain is procedural (noise-perturbed rings along x).

- **CRT television and flat-face CRT** share the cabinet. Only the television passes a
  non-zero glass bulge.
- **The flat panel** uses 0.03–0.045 bezels, a 0.03-deep frame, and a tapered rear
  housing to z = −0.10. Its neck and base are built upright in the *rolled* frame and
  mapped back with the inverse roll, `(x, y) → (y, −x)`, so a pivoted panel still
  stands on its base. Bounds (for the shadow, the floor and the click outline) are
  computed from the generated vertices.
- **Shading** switches on `uCabinet`: glossy black plastic, a small white LED, a deep
  LCD black level, and a matte screen (weak, soft room reflection) for the panel.

**Screen shape:**

- **Follow video:** a tube with the picture's aspect, up to 2.6:1. Portrait pictures use
  the landscape tube rolled 90° (`uPivot` rotates the glass uv).
- **Classic:** always a 4:3 tube, with the CRT layout letterboxing the picture inside it.

**Camera.** A 30° perspective camera, with a *lens shift* (a translation in clip space)
so the set can sit anywhere on screen without looking skewed.

| Pose | How it's set up |
|---|---|
| Desk | yaw/pitch from the user, rotating around the cabinet's centre; distance chosen so the set is *height* × window tall |
| Full | yaw = pitch = 0, glass plane at the pivot, distance chosen so the glass exactly covers the fitted glass rectangle, no lens shift |
| In between | yaw, pitch, pivot and shift interpolated linearly; distance interpolated in log space (constant apparent zoom speed); smootherstep over 950 ms |

**Handoff.** Over the flight:

- the background alpha rises from 0 to 1 between 15 % and 85 %;
- from 80 % onwards the regular flat CRT pass is drawn on top, with increasing constant
  alpha;
- at 100 % only the flat pass remains, exactly as the regular fullscreen player draws it
  (the test suite checks this pixel-for-pixel).

In classic mode the flat pass is confined to the head-on 4:3 glass rectangle.

**Shading:**

| Surface | Model |
|---|---|
| Plastic | Lambert key and fill light, Blinn-Phong specular, a faint cool rim light |
| Grille, buttons, LED | Procedural, from the model-space position |
| Tunnel and bezel | Receive the blurred picture colour at the nearest glass point, falling off with distance from the glass |
| Glass | Emits the CRT texture, plus a dim unlit-phosphor tone and a Schlick-Fresnel reflection of a procedural room with one soft window |
| Floor shadow | A premultiplied black quad with an exponential falloff from the rolled cabinet's footprint |

**Interaction:**

- the silhouette hull is also used for hit-testing;
- drag rotates, with flick inertia decaying at 0.90 per 16 ms tick;
- Shift/Alt/middle-drag moves the set by changing its screen position (lens shift);
- the wheel scales the set's height fraction.

**Per-frame cost** on top of the flat pipeline:

- the CRT pass at glass size (a texture of at most 2160 px high, sized to the glass on
  screen);
- one blur, only when the frame changed;
- about 5,200 triangles (4,704 of them the glass grid).
