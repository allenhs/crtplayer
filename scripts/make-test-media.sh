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
ffmpeg $F -f lavfi -i "testsrc2=size=640x360:rate=25:duration=20" -f lavfi -i "sine=f=880:duration=20" -f lavfi -i "sine=f=440:duration=20" \
  -i fr.srt -i en.srt -map 0:v -map 1:a -map 2:a -map 3 -map 4 -c:v libx264 -pix_fmt yuv420p -g 25 -c:a aac -c:s srt \
  -metadata:s:a:0 language=jpn -metadata:s:a:1 language=eng -metadata:s:s:0 language=fre -metadata:s:s:1 language=eng multitrack_b.mkv
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
# 3D models for the 90s CG room (OBJ and STL, binary and text, one broken file).
python3 "$HERE/scripts/make-test-models.py" models
echo "Test media written to $OUT"
