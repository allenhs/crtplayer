#!/usr/bin/env bash
# Generates the verification clips (needs ffmpeg with libx264, libx265, libvpx, libopus).
# Usage: scripts/make-test-media.sh <output-dir>
set -euo pipefail
OUT=${1:?output dir}; HERE=$(cd "$(dirname "$0")/.." && pwd)
mkdir -p "$OUT"; cd "$OUT"
cp "$HERE/tests/automation/en.srt" "$HERE/tests/automation/fr.srt" .
F="-hide_banner -loglevel error -y"
ffmpeg $F -f lavfi -i "testsrc2=size=640x480:rate=30:duration=20" -f lavfi -i "sine=f=1000:beep_factor=4:duration=20" \
  -c:v libx264 -pix_fmt yuv420p -g 60 -c:a aac -shortest sd_4x3_h264.mp4
# Smooth gradients, a detailed moving shape and camera-like noise: for the Sega CD FMV look (palettes, dither, codec blocks).
ffmpeg $F -f lavfi -i "gradients=s=640x480:r=30:c0=0x203060:c1=0xe0a060:c2=0x306030:c3=0xd0d0f0:n=4:speed=0.02,format=yuv420p" \
  -f lavfi -i "mandelbrot=s=320x240:r=30:start_scale=2.2:end_scale=0.4:end_pts=600" \
  -f lavfi -i "sine=f=330:r=44100,volume=0.4" -f lavfi -i "anoisesrc=r=44100:a=0.08:c=pink" \
  -filter_complex "[1:v]format=yuv420p,scale=320:240[m];[0:v][m]overlay=x='160+120*sin(t*1.3)':y='120+60*cos(t*0.9)',drawbox=x='mod(t*90,640)':y=380:w=60:h=40:c=white@0.9:t=fill,noise=alls=10:allf=t,drawtext=text='FMV %{eif\:t*10\:d}':x=30:y=30:fontsize=40:fontcolor=white[v];[2:a][3:a]amix=inputs=2,aformat=channel_layouts=stereo[a]" \
  -map "[v]" -map "[a]" -t 20 -c:v libx264 -crf 30 -pix_fmt yuv420p -g 30 -c:a aac -b:a 128k fmv_natural.mp4
ffmpeg $F -f lavfi -i "smptehdbars=size=1280x720:rate=25:duration=30" -f lavfi -i "sine=f=440:duration=30" -f lavfi -i "sine=f=880:duration=30" \
  -i en.srt -i fr.srt -map 0:v -map 1:a -map 2:a -map 3 -map 4 -c:v libx264 -pix_fmt yuv420p -g 50 -c:a aac -c:s srt \
  -metadata:s:a:0 language=eng -metadata:s:a:0 title="Main 440 Hz" -metadata:s:a:1 language=jpn -metadata:s:a:1 title="Alt 880 Hz" \
  -metadata:s:s:0 language=eng -metadata:s:s:1 language=fre hd_16x9_multitrack.mkv
ffmpeg $F -f lavfi -i "testsrc2=size=720x1280:rate=30:duration=15" -f lavfi -i "sine=f=660:beep_factor=4:duration=15" \
  -c:v libx264 -pix_fmt yuv420p -c:a aac -shortest vertical_9x16_h264.mp4
ffmpeg $F -f lavfi -i "testsrc2=size=1280x720:rate=30:duration=12" -f lavfi -i "sine=f=500:duration=12" \
  -c:v libx264 -pix_fmt yuv420p -c:a aac -shortest tmp_rot.mp4
ffmpeg $F -display_rotation 270 -i tmp_rot.mp4 -c copy rotated_phone_h264.mp4 && rm tmp_rot.mp4
ffmpeg $F -f lavfi -i "testsrc2=size=2560x1080:rate=24:duration=10" -f lavfi -i "sine=f=300:duration=10" \
  -c:v libvpx-vp9 -deadline realtime -cpu-used 8 -b:v 2M -c:a libopus -shortest ultrawide_64x27_vp9.webm
ffmpeg $F -f lavfi -i "smptebars=size=720x480:rate=30000/1001:duration=10" -f lavfi -i "sine=f=700:duration=10" \
  -vf setsar=32/27 -c:v mpeg2video -b:v 5M -c:a ac3 -shortest anamorphic_dvd_mpeg2.mkv
ffmpeg $F -f lavfi -i "testsrc2=size=640x360:rate=30:duration=5" -c:v libx265 -pix_fmt yuv420p -x265-params log-level=error hevc_only.mp4
ffmpeg $F -f lavfi -i "testsrc2=size=1920x1080:rate=60:duration=15" -f lavfi -i "sine=f=1000:beep_factor=4:duration=15" \
  -c:v libx264 -preset veryfast -pix_fmt yuv420p -c:a aac -shortest fhd_16x9_60fps.mp4
# Still test card for composite effects: stripes of several frequencies, fine text, saturated edges.
ffmpeg $F -loop 1 -i "$HERE/tests/automation/composite_card.png" -f lavfi -i "sine=f=440:duration=6" \
  -t 6 -r 30 -c:v libx264 -pix_fmt yuv420p -c:a aac -shortest composite_card.mp4
# Grey ramp (15/35/55/75/100 %) over saturated primaries: measures how beams swell with brightness.
ffmpeg $F -loop 1 -i "$HERE/tests/automation/scanline_card.png" -f lavfi -i "sine=f=440:duration=4" \
  -t 4 -r 30 -c:v libx264 -pix_fmt yuv444p -crf 8 -c:a aac -shortest scanline_card.mp4
# Chapters: three named chapters at 0, 10 and 20 s.
printf ';FFMETADATA1\n[CHAPTER]\nTIMEBASE=1/1000\nSTART=0\nEND=10000\ntitle=Intro\n[CHAPTER]\nTIMEBASE=1/1000\nSTART=10000\nEND=20000\ntitle=Middle\n[CHAPTER]\nTIMEBASE=1/1000\nSTART=20000\nEND=30000\ntitle=Finale\n' > chapters.txt
# (Chapters are added in a separate copy step: encoding and adding them in one step
# drops the titles after the first.)
ffmpeg $F -f lavfi -i "testsrc2=size=640x360:rate=30" -f lavfi -i "sine=f=330:duration=30" \
  -t 30 -c:v libx264 -g 30 -pix_fmt yuv420p -c:a aac -shortest chapters_plain.mkv   # 1 s keyframes (previews snap to them)
ffmpeg $F -i chapters_plain.mkv -i chapters.txt -map 0 -map_chapters 1 -c copy chapters.mkv
rm -f chapters.txt chapters_plain.mkv
# Sidecar subtitles: two files next to the clip ("subs_clip.srt" loads first).
ffmpeg $F -f lavfi -i "smptebars=size=640x480:rate=30" -f lavfi -i "sine=f=500:duration=20" \
  -t 20 -c:v libx264 -pix_fmt yuv420p -c:a aac -shortest subs_clip.mp4
printf '1\n00:00:00,000 --> 00:01:00,000\nSIDECAR SUBTITLE TEST\n' > subs_clip.srt
printf '1\n00:00:00,000 --> 00:01:00,000\nSECOND SUBTITLE FILE\n' > subs_clip.en.srt
# 2.11: the same languages as hd_16x9_multitrack.mkv in the other order (Japanese sound first, French subtitles first),
# to see that tracks are picked by language and not by their place.
ffmpeg $F -f lavfi -i "testsrc2=size=640x360:rate=25:duration=30" -f lavfi -i "sine=f=880:duration=30" -f lavfi -i "sine=f=440:duration=30" \
  -i fr.srt -i en.srt -map 0:v -map 1:a -map 2:a -map 3 -map 4 -c:v libx264 -pix_fmt yuv420p -g 25 -c:a aac -c:s srt \
  -metadata:s:a:0 language=jpn -metadata:s:a:1 language=eng -metadata:s:s:0 language=fre -metadata:s:s:1 language=eng multitrack_b.mkv
# 2.13: a short clip with two subtitle tracks, two lines each. The file delivers all four lines
# in its first moments, so a track switched to afterwards has already had its lines (and its end).
printf '1\n00:00:00,300 --> 00:00:02,500\nPREMIERE LIGNE\n\n2\n00:00:03,000 --> 00:00:05,500\nDEUXIEME LIGNE\n' > short_fr.srt
printf '1\n00:00:00,300 --> 00:00:02,500\nFIRST LINE\n\n2\n00:00:03,000 --> 00:00:05,500\nSECOND LINE\n' > short_en.srt
ffmpeg $F -f lavfi -i "color=c=0x203040:size=640x360:rate=25:duration=6" -f lavfi -i "sine=f=440:duration=6" -i short_fr.srt -i short_en.srt \
  -map 0:v -map 1:a -map 2 -map 3 -c:v libx264 -pix_fmt yuv420p -g 12 -c:a aac -c:s srt \
  -metadata:s:s:0 language=fre -metadata:s:s:1 language=eng two_subs_short.mkv
rm -f short_fr.srt short_en.srt
# A subtitle that is on screen from 4 to 6 seconds only, on a plain picture (subtitle delay, subtitle style).
printf '1\n00:00:04,000 --> 00:00:06,000\nTIMED LINE\n' > timed.srt
ffmpeg $F -f lavfi -i "color=c=0x305070:size=640x480:rate=25:duration=12" -f lavfi -i "sine=f=440:duration=12" -i timed.srt \
  -map 0:v -map 1:a -map 2 -c:v libx264 -pix_fmt yuv420p -g 25 -c:a aac -c:s srt -metadata:s:s:0 language=eng timed_subs.mkv
rm -f timed.srt
# Interlaced video (MPEG-2, top field first): a bar that moves between the two fields of every frame, so it combs.
ffmpeg $F -f lavfi -i "color=c=black:size=720x480:rate=60000/1001" -f lavfi -i "color=c=white:size=80x240:rate=60000/1001" \
  -f lavfi -i "sine=f=440:duration=8" -t 8 -filter_complex "[0:v][1:v]overlay=x='mod(t*600,640)':y=120,interlace=scan=tff,setsar=8/9[v]" \
  -map "[v]" -map 2:a -c:v mpeg2video -q:v 3 -flags +ildct+ilme -top 1 -c:a ac3 -shortest interlaced_mpeg2.mkv
# Quiet, then loud (night mode): a tone at -40 dB for 6 seconds, then at -6 dB.
ffmpeg $F -f lavfi -i "testsrc2=size=320x240:rate=25:duration=12" \
  -f lavfi -i "sine=f=300:r=48000:duration=12,volume='if(lt(t,6),0.08,4.0)':eval=frame,aformat=channel_layouts=stereo" \
  -c:v libx264 -pix_fmt yuv420p -c:a aac -b:a 160k -shortest quiet_loud.mp4
# Short clips for playlists (shuffle, repeat, carrying on in a folder): each a different colour.
for c in a:c03030 b:30c030 c:3030c0 d:c0c030 e:c030c0; do
  ffmpeg $F -f lavfi -i "color=c=0x${c#*:}:size=320x240:rate=25:duration=3" -f lavfi -i "sine=f=440:duration=3" \
    -c:v libx264 -pix_fmt yuv420p -c:a aac -shortest "short_${c%%:*}.mp4"
done
# Resume: long enough that a position in the middle is remembered (first/last 30 s are not).
ffmpeg $F -f lavfi -i "testsrc2=size=320x240:rate=15" -f lavfi -i "sine=f=440:duration=90" \
  -t 90 -c:v libx264 -pix_fmt yuv420p -c:a aac -shortest resume_clip.mp4
# 2.12: playback without a graphics card. Big frames with film-like grain, so that decoding,
# colour conversion and scaling all have real work to do.
ffmpeg $F -f lavfi -i "testsrc2=size=1920x1080:rate=30:duration=20" -f lavfi -i "sine=f=520:duration=20" \
  -vf "noise=alls=6:allf=t" -c:v libx264 -preset veryfast -crf 22 -pix_fmt yuv420p -g 30 -c:a aac -shortest fhd_plain_30.mp4
ffmpeg $F -f lavfi -i "testsrc2=size=3840x2160:rate=30:duration=10" -f lavfi -i "sine=f=540:duration=10" \
  -vf "noise=alls=6:allf=t" -c:v libx264 -preset veryfast -crf 24 -pix_fmt yuv420p -g 30 -c:a aac -shortest uhd_h264.mp4
# 2.17: 4K HEVC 10-bit. One that is easy to decode (flat colours) and one that is hard (grain all over the
# picture, 20 Mbit/s): a computer with two cores cannot decode all of its pictures in time.
ffmpeg $F -f lavfi -i "testsrc2=size=3840x2160:rate=30:duration=10" -f lavfi -i "sine=f=560:duration=10" \
  -vf "format=yuv420p10le" -c:v libx265 -preset ultrafast -x265-params "log-level=error:keyint=60" -b:v 5M -c:a aac -shortest uhd_hevc10_light.mkv
ffmpeg $F -f lavfi -i "testsrc2=size=3840x2160:rate=30:duration=10" -f lavfi -i "sine=f=580:duration=10" \
  -vf "noise=alls=12:allf=t,format=yuv420p10le" -c:v libx265 -preset ultrafast -x265-params "log-level=error:keyint=60" -b:v 20M -c:a aac -shortest uhd_hevc10_heavy.mkv
ffmpeg $F -f lavfi -i "testsrc2=size=1920x1080:rate=30:duration=10" -f lavfi -i "sine=f=600:duration=10" \
  -vf "noise=alls=5:allf=t,format=yuv420p10le" -c:v libx265 -preset ultrafast -x265-params "log-level=error:keyint=60" -b:v 4M -c:a aac -shortest fhd_hevc10.mkv
# 2.13: Enhance. A detailed picture at full size (the original) and at half size (what is
# upscaled); a scene with a panning background, three objects moving their own ways and a
# title that stands still, at 60 frames a second (the truth) and at 30 (what frames are
# generated between); and a cut between two scenes. All motion is in whole pixels per frame.
python3 "$HERE/scripts/make-enhance-pictures.py" .
ffmpeg $F -loop 1 -framerate 25 -i enh_detail.png -f lavfi -i "sine=f=300:duration=4" -t 4 -c:v libx264 -crf 8 -pix_fmt yuv420p -g 25 -c:a aac -shortest enh_detail_hd.mp4
ffmpeg $F -loop 1 -framerate 25 -i enh_detail.png -f lavfi -i "sine=f=300:duration=4" -t 4 -vf "scale=960:540:flags=area" -c:v libx264 -crf 8 -pix_fmt yuv420p -g 25 -c:a aac -shortest enh_detail_half.mp4
for r in 60 30; do
  ffmpeg $F -loop 1 -framerate $r -i enh_pan.png -f lavfi -i "color=c=0xe04020:s=90x70:r=$r" -f lavfi -i "color=c=0x20c0e0:s=60x110:r=$r" \
    -loop 1 -framerate $r -i enh_other.png -f lavfi -i "sine=f=440:duration=4" \
    -filter_complex "[0:v]crop=960:540:x='60+420*t':y=0[bg];[3:v]crop=140:140:400:200[tex];[bg][1:v]overlay=x='100+240*t':y='60+120*t'[o1];[o1][2:v]overlay=x='820-180*t':y=380[o2];[o2][tex]overlay=x='500':y='380-180*t',drawtext=text='CH 7':x=40:y=40:fontsize=44:fontcolor=white:box=1:boxcolor=black@0.6[v]" \
    -map "[v]" -map 4:a -t 4 -r $r -c:v libx264 -crf 10 -pix_fmt yuv420p -g $r -c:a aac -shortest enh_motion_$r.mp4
done
ffmpeg $F -loop 1 -framerate 30 -i enh_pan.png -loop 1 -framerate 30 -i enh_other.png -f lavfi -i "sine=f=440:duration=4" \
  -filter_complex "[0:v]crop=960:540:x='60+420*t':y=0,trim=duration=2,setpts=PTS-STARTPTS[a];[1:v]trim=duration=2,setpts=PTS-STARTPTS[b];[a][b]concat=n=2:v=1[v]" \
  -map "[v]" -map 2:a -t 4 -r 30 -c:v libx264 -crf 10 -pix_fmt yuv420p -g 30 -c:a aac -shortest enh_motion_cut.mp4
rm -f enh_detail.png enh_pan.png enh_other.png
# Base plugins only (Ogg, Theora, Vorbis): must play even without gst-plugins-good.
ffmpeg $F -f lavfi -i "smptebars=size=640x480:rate=25" -f lavfi -i "sine=f=440:duration=8" \
  -t 8 -c:v libtheora -q:v 6 -c:a libvorbis -shortest base_only.ogv
# Solid colours: how the desk scene is lit by the picture.
for c in black:000000 white:ffffff red:ff0000; do
  ffmpeg $F -f lavfi -i "color=c=0x${c#*:}:size=640x480:rate=25" -f lavfi -i "anullsrc=r=48000:cl=stereo" \
    -t 6 -c:v libx264 -pix_fmt yuv420p -c:a aac -shortest "solid_${c%%:*}.mp4"
done
# Pictures for the wall scene's frames (distinct colours, so the checks can find them).
mkdir -p pictures
python3 - <<'PY'
from PIL import Image, ImageDraw
a = Image.new('RGB', (900, 560)); d = ImageDraw.Draw(a)
for y in range(560): d.line([(0, y), (900, y)], fill=(255, 140 - y // 6, 40 + y // 8) if y < 330 else (60, 40, 70))
d.ellipse([380, 200, 520, 340], fill=(255, 230, 120)); d.polygon([(0, 380), (250, 300), (520, 390), (900, 320), (900, 560), (0, 560)], fill=(40, 25, 50))
a.save('pictures/sunset.jpg', quality=92)
b = Image.new('RGB', (560, 780)); d = ImageDraw.Draw(b)
for y in range(780): d.line([(0, y), (560, y)], fill=(40 + y // 10, 90 + y // 10, 200))
d.polygon([(0, 780), (280, 260), (560, 780)], fill=(70, 80, 110))
b.save('pictures/mountain.jpg', quality=92)
c = Image.new('RGB', (600, 600), (20, 140, 70)); d = ImageDraw.Draw(c)
for i in range(6): d.ellipse([60 + i * 40, 60 + i * 40, 540 - i * 40, 540 - i * 40], outline=(230, 240, 120), width=8)
c.save('pictures/circles.png')
e = Image.new('RGB', (900, 540), (170, 30, 140)); d = ImageDraw.Draw(e)
for x in range(0, 900, 60): d.rectangle([x, 0, x + 28, 540], fill=(210, 70, 180))
e.save('pictures/stripes.png')
PY
# 2.16: subtitles after jumps. Two minutes of a plain picture with a white bar along the top that grows with the time
# (5 pixels a second: the picture itself says where in the video it is), sparse keyframes, and subtitle lines
# that can be told apart by counting: line N is N letters "O" set wide apart.
python3 - <<'PY'
cues = [(2, 6, 1), (10, 20, 2), (25, 28, 3), (30, 45, 4), (50, 55, 5), (60, 110, 6), (112, 118, 7)]
other = [(0, 58, 8), (62, 119, 9)]
def text(n): return '     '.join('O' * 1 for _ in range(n))
def ts(s, sep): return '%02d:%02d:%02d%s000' % (s // 3600, s // 60 % 60, s % 60, sep)
for name, lines in (('jump_a', cues), ('jump_b', other)):
    with open(name + '.srt', 'w') as f:
        for i, (a, b, n) in enumerate(lines):
            f.write('%d\n%s --> %s\n%s\n\n' % (i + 1, ts(a, ','), ts(b, ','), text(n)))
    with open(name + '.vtt', 'w') as f:
        f.write('WEBVTT\n\n')
        for i, (a, b, n) in enumerate(lines):
            f.write('%s --> %s\n%s\n\n' % (ts(a, '.'), ts(b, '.'), text(n)))
    with open(name + '.ass', 'w') as f:
        f.write('[Script Info]\nScriptType: v4.00+\nPlayResX: 640\nPlayResY: 360\n\n[V4+ Styles]\n'
                'Format: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, OutlineColour, BackColour, Bold, Italic, Underline, StrikeOut, '
                'ScaleX, ScaleY, Spacing, Angle, BorderStyle, Outline, Shadow, Alignment, MarginL, MarginR, MarginV, Encoding\n'
                'Style: Default,DejaVu Sans,26,&H0000FFFF,&H000000FF,&H00000000,&H00000000,0,0,0,0,100,100,0,0,1,1,0,2,10,10,20,1\n\n'
                '[Events]\nFormat: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n')
        for a, b, n in lines:
            f.write('Dialogue: 0,%d:%02d:%02d.00,%d:%02d:%02d.00,Default,,0,0,0,,%s\n' % (a // 3600, a // 60 % 60, a % 60, b // 3600, b // 60 % 60, b % 60, text(n)))
PY
ffmpeg $F -f lavfi -i "color=c=0x203860:size=640x360:rate=25:duration=120" -f lavfi -i "color=c=white:size=600x16:rate=25:duration=120" \
  -f lavfi -i "sine=f=330:duration=120" -i jump_a.srt -i jump_b.srt -filter_complex "[0:v][1:v]overlay=x='t*5-600':y=0,format=yuv420p[v]" \
  -map "[v]" -map 2:a -map 3 -map 4 -c:v libx264 -g 250 -c:a aac -b:a 64k -c:s srt \
  -metadata:s:s:0 language=eng -metadata:s:s:1 language=fre jump_srt.mkv
ffmpeg $F -i jump_srt.mkv -i jump_a.ass -i jump_b.ass -map 0:v -map 0:a -map 1 -map 2 -c:v copy -c:a copy -c:s ass \
  -metadata:s:s:0 language=eng -metadata:s:s:1 language=fre jump_ass.mkv
ffmpeg $F -i jump_srt.mkv -i jump_a.srt -i jump_b.srt -map 0:v -map 0:a -map 1 -map 2 -c:v copy -c:a copy -c:s mov_text \
  -metadata:s:s:0 language=eng -metadata:s:s:1 language=fre jump_text.mp4
# ... and beside the video: a SubRip file, an ASS file, and loose files for picking by hand (the WebVTT one as a server would send it).
ffmpeg $F -i jump_srt.mkv -map 0:v -map 0:a -c copy jump_side.mp4
cp jump_a.srt jump_side.srt
ffmpeg $F -i jump_srt.mkv -map 0:v -map 0:a -c copy jump_sideass.mkv
cp jump_a.ass jump_sideass.ass
# The same video with its times not beginning at zero (a broadcast recording).
ffmpeg $F -i jump_srt.mkv -map 0:v -map 0:a -c copy -output_ts_offset 600 -muxdelay 0 -muxpreload 0 -f mpegts jump_offset.ts
cp jump_a.srt jump_offset.srt
# ... and with picture subtitles (Blu-ray's kind) as its first subtitle track: line N is N white blocks.
python3 "$HERE/scripts/make-pgs.py" jump_pgs.sup
ffmpeg $F -i jump_srt.mkv -i jump_pgs.sup -map 0:v -map 0:a -map 1 -map 0:s:0 -c copy -metadata:s:s:0 language=eng jump_pgs.mkv
rm -f jump_pgs.sup
# 2.17: videos as web sites serve them (tests/web_mock.py hands them out). The same picture as the jump_* clips (the
# white bar says where in the video a picture is); the sound says it too: ten seconds of tone, ten of silence, in turn.
# The picture and the sound as two files (fragmented MP4 with its index in front, WebM), one file with both, an
# HLS stream, a DASH manifest naming the two files, a larger file than the player keeps in memory, subtitle
# files (WebVTT; one of them the way YouTube writes automatic captions: every line twice, words timed one by one).
mkdir -p web/hls
WEBV=(-f lavfi -i "color=c=0x203860:size=640x360:rate=25:duration=120" -f lavfi -i "color=c=white:size=600x16:rate=25:duration=120")
WEBA=(-f lavfi -i "sine=f=440:duration=120")
WEBVF="[0:v][1:v]overlay=x='t*5-600':y=0,format=yuv420p[v]"
WEBAF="volume='if(lt(mod(t,20),10),1,0)':eval=frame"
FRAG="-movflags +frag_keyframe+empty_moov+default_base_moof+global_sidx"
ffmpeg $F "${WEBV[@]}" -filter_complex "$WEBVF" -map "[v]" -c:v libx264 -g 125 $FRAG web/v_h264.mp4
ffmpeg $F "${WEBA[@]}" -af "$WEBAF" -c:a aac -b:a 96k $FRAG -frag_duration 5000000 web/a_aac.m4a
ffmpeg $F "${WEBV[@]}" -filter_complex "$WEBVF" -map "[v]" -c:v libvpx-vp9 -b:v 150k -g 125 -deadline realtime -cpu-used 8 -cues_to_front 1 web/v_vp9.webm
ffmpeg $F "${WEBA[@]}" -af "$WEBAF" -c:a libopus -b:a 64k -cues_to_front 1 web/a_opus.webm
ffmpeg $F -i web/v_h264.mp4 -i web/a_aac.m4a -c copy -movflags +faststart web/muxed.mp4
ffmpeg $F -i web/muxed.mp4 -c copy -f hls -hls_time 10 -hls_playlist_type vod -hls_segment_filename web/hls/seg%03d.ts web/hls/index.m3u8
printf '#EXTM3U\n#EXT-X-STREAM-INF:BANDWIDTH=400000,RESOLUTION=640x360,CODECS="avc1.64001e,mp4a.40.2"\nindex.m3u8\n' > web/hls/master.m3u8
# (two minutes at 6 Mbit/s: about 90 MB; noise below, the plain blue and the bar above)
ffmpeg $F -f lavfi -i "color=c=0x203860:size=1280x720:rate=25:duration=120" -f lavfi -i "color=c=white:size=1200x32:rate=25:duration=120" \
  -f lavfi -i "testsrc2=size=1280x400:rate=25:duration=120" \
  -filter_complex "[2:v]noise=alls=70:allf=t[n];[0:v][n]overlay=x=0:y=320[b];[b][1:v]overlay=x='t*10-1200':y=0,format=yuv420p[v]" -map "[v]" \
  -c:v libx264 -preset ultrafast -g 125 -b:v 6M -minrate 6M -maxrate 6M -bufsize 6M -x264-params nal-hrd=cbr $FRAG web/v_big.mp4
cp jump_a.vtt web/en.vtt
cp jump_b.vtt web/fr.vtt
python3 - <<'PY'
import struct
cues = [(2, 6, 1), (10, 20, 2), (25, 28, 3), (30, 45, 4), (50, 55, 5), (60, 110, 6), (112, 118, 7)]
def ts(s): return '%02d:%02d:%06.3f' % (s // 3600, s // 60 % 60, s % 60)
with open('web/auto.vtt', 'w') as f:
    f.write('WEBVTT\nKind: captions\nLanguage: en\n\n')
    before = ' '
    for a, b, n in cues:
        words = ['O'] + ['<%s><c>     O</c>' % ts(a + 0.3 * i) for i in range(1, n)]
        f.write('%s --> %s align:start position:0%%\n%s\n%s\n\n' % (ts(a), ts(b - 0.01), before, ''.join(words)))
        line = '     '.join('O' for _ in range(n))
        f.write('%s --> %s align:start position:0%%\n%s\n \n\n' % (ts(b - 0.01), ts(b), line))
        before = line
# The DASH manifest: each file whole, with where its header ends and its index lies.
def index(path):
    data = open(path, 'rb').read(1 << 20); at = 0; found = None
    while at + 8 <= len(data):
        size, kind = struct.unpack('>I4s', data[at:at + 8])
        if kind == b'sidx': found = (at, at + size - 1)
        if kind == b'moof' or size < 8: break
        at += size
    return found
def one(name, attrs, path):
    i = index(path)
    return ('  <AdaptationSet %s subsegmentAlignment="true">\n   <Representation id="%s" %s>\n    <BaseURL>%s</BaseURL>\n'
            '    <SegmentBase indexRange="%d-%d"><Initialization range="0-%d"/></SegmentBase>\n   </Representation>\n  </AdaptationSet>\n'
            % (attrs[0], name, attrs[1], path.split('/')[-1], i[0], i[1], i[0] - 1))
mpd = ('<?xml version="1.0" encoding="UTF-8"?>\n<MPD xmlns="urn:mpeg:dash:schema:mpd:2011" type="static" mediaPresentationDuration="PT120S" '
       'minBufferTime="PT2S" profiles="urn:mpeg:dash:profile:isoff-on-demand:2011">\n <Period>\n'
       + one('v', ('mimeType="video/mp4"', 'codecs="avc1.64001e" width="640" height="360" frameRate="25" bandwidth="100000"'), 'web/v_h264.mp4')
       + one('a', ('mimeType="audio/mp4" lang="en"', 'codecs="mp4a.40.2" audioSamplingRate="44100" bandwidth="96000"'), 'web/a_aac.m4a')
       + ' </Period>\n</MPD>\n')
open('web/dash.mpd', 'w').write(mpd)
PY
# 3D models for the 90s CG room: OBJ, STL, PLY, GLB / glTF and FBX; Z-up files, point
# clouds, very large models, damaged files (models, models-upright, -scenes, -large, -single).
python3 "$HERE/scripts/make-test-models.py" models
echo "Test media written to $OUT"
