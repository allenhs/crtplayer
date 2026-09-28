# Automation scripts

`crtplayer --automation script.txt --automation-log out.json` runs a plain-text script
against the real application. It drives the same `MainWindow` and `Player` API that the
buttons and shortcuts use.

- Each line is a command. `#` starts a comment.
- Every command is appended to the JSON log with a timestamp.
- `report` also includes `rate`, `loopA`/`loopB`, `chapterCount`, `externalSubtitle`,
  `externalSubtitleOffers`, `resumeMs` and `recentFiles` (1.8).
- `report` adds a full state snapshot: geometry, decoders, clock, sync statistics,
  tracks, fullscreen/controls state, and in desk mode the phase, flight progress,
  pivot/shape, set rectangle and input-mask rectangle.
- The exit code is 0 when every `expect` and `waitstate` succeeded.

| Command | Effect |
|---|---|
| `open PATH` | Add to the playlist and play |
| `waitstate playing\|paused TIMEOUT_MS` | Wait for a state. `playing` also requires a presented frame |
| `wait MS` | Sleep |
| `play` / `pause` | Start or pause playback |
| `seek SECONDS [fast]` | Accurate seek, or key-unit seek with `fast` |
| `scrub FROM TO STEPS INTERVAL_MS` | Emulate dragging the seek bar. Logs the worst GUI event-loop stall |
| `step fwd\|back` | Frame step |
| `preset NAME` | Select a preset |
| `param KEY VALUE` | Set one CRT parameter (JSON key, e.g. `curvature 0.2`, `includeBars on`) |
| `bypass on\|off` | Turn effects bypass on or off |
| `compare on\|off [FRACTION]` | Split view, optionally with the divider position |
| `fullscreen on\|off` | Enter or leave fullscreen |
| `mousemove` | Send a mouse-move event to the video (wakes auto-hidden controls) |
| `mode fit\|fill\|original\|crop` | Set the scaling mode |
| `crop L T R B` | Crop fractions |
| `aspect VALUE` | Aspect override (0 = from file) |
| `audio N` / `sub N` | Select a track (`sub -1` turns subtitles off) |
| `hw on\|off` | Hardware decoding for the next `open` |
| `screenshot original\|filtered PATH` | Save a screenshot |
| `grabwindow PATH` | Capture the whole window, including the UI |
| `moment poweron\|poweroff\|static\|none` | Start a set moment now, or end all moments |
| `osd TEXT` | Show VCR on-screen text (when the preset enables it); `osd` alone hides it |
| `gamepad attach\|tap B\|press B\|release B\|axis NAME VALUE` | SDL virtual controller (buttons: a b x y back start lb rb l3 r3 up down left right; axes: lx ly rx ry lt rt) |
| `scene desktop\|desk` | Desk-mode scene |
| `scenemood evening\|night\|dark`, `scenefog off\|light\|thick [STRENGTH]`, `scenewood walnut\|oak\|cherry`, `scenequality low\|medium\|high` | Scene options |
| `scenedialog` | Open the Scene settings window |
| `scene wall`, `scenewall 0..5`, `sceneframes 0..4`, `sceneframestyle black\|wood\|gold`, `scenepicture SLOT PATH\|clear` | Wall-mounted TV scene: wall finish, picture layout, frame style, pictures |
| `scenepreviews DIR` | Save the settings window's scene previews |
| `sceneplace TVHEIGHT PICHEIGHT SPACING SIZE` | Wall scene placement |
| `scene theater`, `theaterlook 0..3`, `theatersnap` | Movie theater; its picture look; finish the curtain / masking / beam moves at once |
| `scene cg`; `cg palette 0..2`, `cg floor checker\|grid`, `cg stand pedestal\|plinth\|floating`, `cg objects\|banding\|reveal\|orbit on\|off`, `cg revealsnap`, `cg set 0..4`, `cg background on\|off`, `cg models PATH\|off`, `cg finish 0..4`; `modelswait MS` | The 90s CG room and its options; your 3D models folder |
| `deskcabinet arcade`, `arcadeart 0..3` | The arcade cabinet and its art (space, sunset, neon, 70s woodgrain) |
| `marqueetitle NAME`, `marqueeimage PATH` | How a file name is tidied for the marquee; save the marquee's lettering |
| `looksound on\|off`, `looksound volume 0..2`, `looksound strength 0..1` | The look's sound; its noise volume and effect strength |
| `settings TAB` | Show the settings panel on a tab (CRT, Display, Playback) |
| `theatermarch ref\|normal` | Near-exact (slow) reference rendering of the seats, for comparisons |
| `deskbackdrop room\|desktop` | 1.9 name: `room` = the desk scene |
| `rate X` | Playback speed (0.25–4) |
| `loop A_MS B_MS` / `loop off` | Set or clear the A–B loop |
| `chapter next\|prev` | Jump to the next / previous chapter |
| `subfile PATH` / `subfile offer N` / `subfile off` | Load an external subtitle file, one of the offered ones, or none |
| `thumb MS PATH` | Request a seek-bar preview at MS and save it |
| `sysreport PATH` | Write the system report to a file |
| `rendertime SECONDS\|off` | Pin the effect clock (noise, flicker, interlaced fields, dot crawl) for deterministic captures |
| `resetsync` | Reset the sync, frame-rate and paint statistics |
| `report [LABEL]` | Log a state snapshot |
| `expect KEY VALUE` | Compare a report field. Numbers pass within 1 %. Text is the rest of the line |
| `desk on\|off` | Enter or leave desk mode |
| `deskpose YAW PITCH [HEIGHT CX CY]` | Set the set's angle; optionally its size (fraction of the window height) and centre (0–1) |
| `deskfly in\|out` | Start a flight into or out of fullscreen |
| `deskwait desk\|flying-in\|full\|flying-out TIMEOUT_MS` | Wait for a desk phase |
| `deskcabinet crt\|flat-crt\|flat-panel\|wood-console\|pvm\|beige-monitor` | Choose the desk-mode set |
| `deskshape follow\|classic` | Tube follows the video, or a classic 4:3 tube |
| `deskgrab PATH` | Save the desk view's framebuffer, including alpha |
| `deskmouse X Y` | Send a mouse-move event to the desk view (wakes the control strip) |
| `jfsignin URL USER PASSWORD` | Sign in to a Jellyfin server (the password is never logged) |
| `jfwait signedin\|signedout\|listing TIMEOUT_MS` | Wait for the Jellyfin sign-in state, or for a listing to finish loading |
| `jfwait count N TIMEOUT_MS` | Wait until the listing holds N items, scrolling to its end meanwhile (pages load as you scroll) |
| `jfopen NAME` | In the current Jellyfin listing, open a folder, or play a video, by name |
| `jfconverted NAME` | Play a video in the listing converted by the server (its context menu's *Play converted by the server*) |
| `jfquality MBPS` | The Jellyfin quality limit in Mbit/s; 0 = the original file |
| `jfscroll end` | Scroll the Jellyfin listing to its end |
| `jfhome` / `jfsignout` | Jellyfin home view / sign out (revokes the token) |
| `quit` | Exit |

**Sync measurement.** For every new frame painted while playing, the player computes:

```
lateness = (pipeline clock now − base time) − running time of the frame's PTS
```

Because the clock comes from the audio sink, this is the video's offset from the audio
timeline at the moment the frame is drawn.

It does not include display scan-out after the buffer swap.
