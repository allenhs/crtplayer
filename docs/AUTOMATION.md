# Automation scripts

`crtplayer --automation script.txt --automation-log out.json` runs a plain-text script
against the real application. It drives the same `MainWindow` and `Player` API that the
buttons and shortcuts use.

- Each line is a command. `#` starts a comment.
- Every command is appended to the JSON log with a timestamp.
- `report` also includes `rate`, `loopA`/`loopB`, `chapterCount`, `externalSubtitle`,
  `externalSubtitleOffers`, `resumeMs` and `recentFiles` (1.8).
- `report` has `lookListOpen`, and while it is open `lookListRect` (where the look selector's list is on the
  screen), with `screenRect` and `screenScale` (2.16.1). On Windows it also has `nativeWindow` and, in desk mode,
  `deskNativeWindow`: the window as Windows has it (`border`, `layered`, `topmost`, `insideRect`, `monitorRect`,
  `insideIsWholeScreen`, `shaped`, `openGl`, `alphaAsked`, `alphaGot`).
- `report` has `subtitleFeed` (2.16): the subtitle lines the player keeps itself: what kind the showing
  track is (`text`, `unsupported` for picture subtitles, `reading`, `failed`, `none`), how many lines are
  known and for which stretches of the video, how often the first picture after a jump waited for its
  lines and for how long, and how often the video was opened (`videoOpens`).
- `report` adds a full state snapshot: geometry, decoders, clock, sync statistics,
  tracks, fullscreen/controls state, and in desk mode the phase, flight progress,
  pivot/shape, set rectangle and input-mask rectangle.
- `report` has `videoPath` (2.12): `software` (no graphics acceleration), `surface`
  (`raster` or `gl widget`), `output` (`as decoded`, `rgb`, `rgb scaled`), `fastSize`,
  `frameSize`, `frameDirect` (drawn without a conversion pass), `lookScale`,
  `decoderThreads` and `cpuThreads`.
- `report` has `enhance` (2.13): `available`, `upscale`, `sharpness`, `motion`,
  `upscaledFrames`, `upscaledWidth` / `Height`, `framePairs`, `framesGenerated`,
  `motionWidth` / `Height`, `draws`, `drawsBetween`, `lastPhase`, `running`, `useful`,
  `screenHz`, `pictureLatencyMs`.
- The Enhance checks need `CRTPLAYER_ENHANCE_FORCE=1` on a machine without a graphics
  card (the enhancements are otherwise unavailable with software OpenGL).
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
| `grabwindow PATH [dialog]` | Capture the whole window, including the UI; with `dialog`, the dialog that is open |
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
| `scene cg`; `cg palette 0..2`, `cg floor checker\|grid`, `cg stand pedestal\|plinth\|floating`, `cg objects\|banding\|reveal\|orbit on\|off`, `cg revealsnap`, `cg set 0..4`, `cg background on\|off`, `cg models PATH\|off`, `cg finish 0..4`, `cg up auto\|y\|z\|x\|-y\|-z\|-x`, `cg face 0..4`; `modelswait MS` | The 90s CG room and its options; your 3D models folder, which way is up in them, and how they face (0 turning, 1 as in the file, 2 to 4 turned by 90°, 180°, 270°). `report` then lists `modelsInfo` (per model: triangles drawn and in the file, points, colours, the axis used and why, its size as placed), `modelsSkipped`, `modelsLight` (the limits without a graphics card) and `deskPaints` (frames drawn so far) |
| `deskcabinet arcade`, `arcadeart 0..3` | The arcade cabinet and its art (space, sunset, neon, 70s woodgrain) |
| `marqueetitle NAME`, `marqueeimage PATH` | How a file name is tidied for the marquee; save the marquee's lettering |
| `looksound on\|off`, `looksound volume 0..2`, `looksound strength 0..1` | The look's sound; its noise volume and effect strength |
| `settings TAB` | Show the settings panel on a tab (CRT, Display, Playback) |
| `infooverlay on\|off` | The technical info overlay (I) |
| `enhance nvidia on\|off`, `enhance nvquality 1..4`, `enhance nvmode 0..2` | NVIDIA AI for Enhance: the method, Video Super Resolution's quality, Video Frame Generation's model |
| `enhance nvwait STATE [TIMEOUT_MS]` | Wait until NVIDIA's helper is `ready` (also `off`, `idle`, `starting`, `opening`, `stopped`, `failed`) |
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
| `keyframe next\|prev` | Jump to the next / previous keyframe (Shift+→ / Shift+←) |
| `cut [OUT_PATH]` | Save A–B without re-encoding (the X dialog's *Save cut*); waits, and logs the result |
| `dialog cut\|gif` | Open the Cut or GIF dialog |
| `gifopts WIDTH FPS LOOK` | GIF options: width in pixels (320–1024; 1280, 1920, 2560 and 3840 are the HD, Full HD, 1440p and 4K frames), frames a second, 1 = with the CRT look, 0 = original picture |
| `gif [OUT_PATH]` | Save A–B (or the next 5 s) as a GIF; waits, and logs the result (size, frames, frames a second, how long it took) |
| `waitpos MS [TIMEOUT_MS]` | Wait until the video has played to MS |
| `waitshown SECONDS [TIMEOUT_MS]` | Wait until no jump is under way and the picture delivered last is the one of that place (2.16) |
| `looklist open\|close` | Open or close the look selector's list on the bar in use (the desk's in desk mode) (2.16.1) |
| `screengrab PATH` | Save the screen as it is shown, with every window on it: what is really seen, not the player's own drawing (X11 and Windows; a Wayland compositor does not allow it) (2.16.1) |
| `backdrop COLOUR\|off` | A plain window in one colour over the whole screen, behind the player: what a see-through window should show (2.16.1) |
| `deskontop on\|off` | Desk mode's "Keep on top of other windows" (2.16.1) |
| `subs on\|off` | Subtitles wanted or not (the V key): it holds from video to video |
| `subtrack N` / `audiotrack N` | Pick a subtitle track (−1: off) or a sound track as the menus do: its language is remembered |
| `subdelay MS` / `audiodelay MS` | Subtitle delay (this video) / sound delay (remembered) |
| `substyle SIZE COLOUR BEHIND POSITION` | Subtitle text: size 0–3, colour 0 white / 1 yellow, 0 outline / 1 dark box, 0 bottom / 1 raised / 2 top |
| `night on\|off`, `deinterlace on\|off` | Night mode; deinterlacing |
| `shuffle on\|off`, `repeat off\|all\|one`, `autonext on\|off` | Playlist modes; carrying on with the next video in the folder |
| `enqueue PATH`, `item next\|prev` | Add to the playlist without playing; next / previous item |
| `playlist save PATH\|clear\|play N\|show\|click shuffle\|click repeat` | Save as .m3u8, empty it, play entry N, show the panel, press its buttons |
| `sleep MINUTES\|end\|off\|seconds N` | Sleep timer |
| `videopath auto\|always\|never` | The CPU fast path setting (2.12): frames converted and scaled on the CPU's cores |
| `lookdetail auto\|full\|half` | Without a graphics card: the size the look is drawn at |
| `profile on\|off` | Time the drawing stages. `report` then has `profile` (per frame: `uploadMs`, `convertMs`, `mipmapMs`, `blurMs`, `drawMs`, `paintMs`, `composeMs`, `frameIntervalMs`, and `cpuCoresBusy`: the CPU time the whole player used per second since `resetsync`) |
| `enhance upscale on\|off`, `enhance sharp 0..1`, `enhance motion on\|off` | Enhance (2.13): sharper upscaling, its sharpness, smooth motion (frame generation) |
| `enhance grab T PATH [mix\|flow\|flowback]` | Save the picture at phase T (0..1) between the frame before and the frame shown, at the video's own size. `mix`: a plain mix of the two frames; `flow` / `flowback`: the motion found, as colours |
| `fmvgrab PATH` | Save the Sega CD FMV look's console screen (the 256×224 picture before the TV) as an image |
| `tvadd NUMBER order\|shuffle FOLDER` | Cable TV: a channel from a folder. Channels made by a script get a fixed place in their rounds, so runs repeat |
| `tvbumpers NUMBER FOLDER` | Short clips to play between that channel's programmes |
| `tvjf NAME` | The folder NAME in the current Jellyfin listing becomes a channel |
| `tvwait TIMEOUT_MS` | Wait until every channel's videos have been read |
| `tvclock MS` | The TV's wall clock reads MS (since the Unix epoch) now, and runs on from there |
| `tv on\|off\|up\|down\|ch N\|digit N\|guide on\|guide off` | TV mode on/off, channel up/down, tune channel N, press a number key, the guide |
| `jfwait count N TIMEOUT_MS` | Wait until the listing holds N items, scrolling to its end meanwhile (pages load as you scroll) |
| `jfopen NAME` | In the current Jellyfin listing, open a folder, or play a video, by name |
| `jfconverted NAME` | Play a video in the listing converted by the server (its context menu's *Play converted by the server*) |
| `jfquality MBPS` | The Jellyfin quality limit in Mbit/s; 0 = the original file |
| `jfscroll end` | Scroll the Jellyfin listing to its end |
| `jfhome` / `jfsignout` | Jellyfin home view / sign out (revokes the token) |
| `governor on\|off` | Leaving pictures out before decoding when the computer cannot keep up (2.17); `report` has `videoPath.governor` and `videoPath.shrink` |
| `openlink ADDRESS` | Open a link as Ctrl+L does: a page with a video on it (yt-dlp is asked), or a media address (2.17) |
| `pastelink TEXT` | TEXT goes to the clipboard, then Ctrl+V |
| `ytdlp path FILE\|-`, `ytdlp fetch`, `ytdlp height N`, `ytdlp dialog get\|cancel` | The yt-dlp to use (`-`: whichever is found); the settings' button; the largest picture asked for (0: the screen's); the buttons of the "yt-dlp is needed" dialog |
| `waitonline KEY VALUE [TIMEOUT_MS]` | Wait until the `online` part of the report has KEY = VALUE (`*`: anything but empty; a number: at least that) |
| `closedialogs` | Close whatever message boxes are up |
| `openpair VIDEO_URL [AUDIO_URL]` | Two addresses played as the picture and the sound of one video, without yt-dlp (2.17) |
| `browse open\|close` | The browser of web videos (2.18), as Ctrl+B |
| `browse key NAME [NAME...]` | Keys to the browser, in turn: `Left Right Up Down Return Escape Backspace PageUp PageDown Tab Home W F R U Slash BracketLeft BracketRight` |
| `browse section new\|channels\|later\|history\|search` | Go to a section |
| `browse search WORDS` | Search for WORDS |
| `browse type TEXT` | TEXT into the line the browser shows (a search, a channel's link), then Enter |
| `browse import FILE` | Google Takeout's `subscriptions.csv`: its channels followed |
| `waitbrowse KEY VALUE [TIMEOUT_MS]` | Wait until the `browse` part of the report has KEY = VALUE (as `waitonline`) |
| `quit` | Exit |

`report` carries `browse` (2.18): `open`, `section`, `focus` (tabs, grid, tray, detail, input), `page`, `pages`,
`selected`, `count`, `titles` (of the page shown), `loading`, `error`, `detail`, `detailTitle`, `detailButtons`,
`channel`, `input`, `message`, `ytInstalled`, `ytNewest`, `updating`, `cells` (the tiles' places, in the window),
`laterTitles`, `historyItems`, and `data` (the channels followed; how many searches, channel pages, feeds, feeds read
through yt-dlp instead, and pictures were fetched).

**Sync measurement.** For every new frame painted while playing, the player computes:

```
lateness = (pipeline clock now − base time) − running time of the frame's PTS
```

Because the clock comes from the audio sink, this is the video's offset from the audio
timeline at the moment the frame is drawn.

It does not include display scan-out after the buffer swap.

## NVIDIA AI (2.15)

`report` carries `enhance.nvidia`: what was found (`helper`, `sdk`, `sdkVersion`, `superRes`, `frameGen`, `usable`),
the helper's `state`, `error` (why it is not ready now) and `lastError`, the effects open (`srcWidth` … `outHeight`,
`quality`, `mode`, `onCard`), what the last draw used (`kind`: 0 not NVIDIA's, 1 frames between at the video's size,
2 the upscaled picture), counts (`frames` sent, `pictures` taken, `generated`, `upscaledPictures`, `betweenPictures`,
`passing`: taken at the draw after the one that asked, `missed`: draws that went without, `opens`, `failures`,
`restarts`) and running means in milliseconds (`frameMs`, `pictureMs`: the helper's work; `waitMs`, `readMs`,
`uploadMs`: the player's).

Environment, for the checks:

| Variable | Effect |
|---|---|
| `CRTPLAYER_NVFX_SDK=FOLDER` | Where the SDK is (that folder or none) |
| `CRTPLAYER_NVFX_HELPER=FILE` | The helper program (that file or none) |
| `CRTPLAYER_NVFX_OFF=1` | NVIDIA's methods are not looked for at all (set by `scripts/run-verification.sh` for every suite but `nvidia`) |
| `CRTPLAYER_NVFX_QUICK_MS=N` | Draws up to N ms apart count as following one another quickly (default 50): they take the picture asked for at the draw before instead of waiting for their own |
| `NVFX_MOCK_*` | Read by the stand-in SDK in `tests/nvfx_mock` (stamps, delays and faults; listed at the top of `mock_vfx.cpp`) |

The `nvidia` suite needs the stand-in SDK that the normal build makes (`build/nvfx-mock-sdk`; elsewhere:
`NVFX_MOCK_SDK=FOLDER`). `tests/nvfx_serve_test.py` (run by `ctest` as `nvfx`) tests the helper alone.

## Videos from web sites (2.17)

`report` carries `online`: the yt-dlp in use (`program`, `ownCopy`, `version`, `jsRuntime`, `lastCommand`, `runs`), what
the settings show (`status`, `button`), the video (`page`, `site`, `what`, `live`, `title`, `resumeKey`; `player.streams`:
2 for the picture and the sound as two addresses, 1 for one file, 0 for a manifest GStreamer plays itself), what the site
offers (`subtitles`, `subtitlesAutomatic`, `subtitleLoaded`, `chapters`), the playlist (`playlist`, `playlistLabels`,
`playlistIndex`), counts (`resolves`, `askedAgain`: addresses renewed, `failures`, `fetches`, `fetchFailures`), the
message boxes that are up (`dialogs`), `hint` (the words in the empty picture) and `wallClock`.

The `online` suite (`RUN_ONLY=online`) starts `tests/web_mock.py`, a small video site, and uses `tests/fake_ytdlp.py`
in place of yt-dlp: it answers the way the real one answers for YouTube. Where the computer has a real yt-dlp, that
one is run against the mock site's pages too (`ONLINE_NO_REAL=1` leaves that out). The clips say where they are:
a bar in the picture, and the sound (ten seconds of tone, ten of silence, in turn), so every picture and every
report is judged by itself (`scripts/check-online.py`).

| Variable | Effect |
|---|---|
| `CRTPLAYER_TOOLS_PATH=FOLDERS` | Where yt-dlp and the JavaScript runtimes are looked for (these folders and no others) |
| `CRTPLAYER_YTDLP_RELEASE=URL`, `CRTPLAYER_DENO_RELEASE=URL` | Where "Get yt-dlp" fetches from |
| `FAKE_YTDLP_LOG=FILE` | Read by the stand-in: every call is written there |
| `CRTPLAYER_WEB_CONNECT_DELAY_MS=N` | The decoders are connected to the web source N ms late, when the server's first bytes are long there (the start of a web video must not depend on which comes first) |
| `CRTPLAYER_SHRINK_OFF=1`, `CRTPLAYER_GOVERNOR_OFF=1` | Without a graphics card: 10-bit pictures are not made 8-bit and smaller in one pass; no pictures are left out before decoding (as before 2.17) |
