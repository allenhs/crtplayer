#!/usr/bin/env python3
"""Summarises automation logs in a directory into summary.md (and stdout)."""
import json, pathlib, sys

out = pathlib.Path(sys.argv[1])
lines, total, failed = [], 0, 0
for log in sorted(list(out.glob("*.json")) + list(out.glob("jellyfin-*/jellyfin*.json"))):
    try:
        entries = json.loads(log.read_text())
    except Exception:
        continue
    if not isinstance(entries, list):
        continue
    lines.append(f"\n### {log.stem}\n")
    for e in entries:
        if "ok" in e:
            total += 1
            failed += 0 if e["ok"] else 1
            if not e["ok"]:
                lines.append(f"- FAIL `{e['cmd']}` (actual: {e.get('actual', e.get('state'))})")
        if e["cmd"] == "report":
            pr, s = e["pictureRect"], e["sync"]
            lines.append(
                f"- **{e['label']}** [{e['platform']}] {e['state']}, {e['file']}: coded {e['codedWidth']}×{e['codedHeight']}, "
                f"PAR {e['par']}, rotation {e['rotation']}°, DAR {e['displayAspect']:.4f}; on screen "
                f"{pr['w']:.0f}×{pr['h']:.0f} px (ratio {e['pictureAspectOnScreen']:.4f}), {e['scaleMode']}, "
                f"{e['scanlines']:.0f} scanlines; decoders {e['videoDecoder'] or '-'} / {e['audioDecoder'] or '-'}; "
                f"clock {e['clock'] or '-'}; frame pts {e['framePtsMs']:.1f} ms; presented {s['presentedFps']:.1f} fps, "
                f"lateness mean {s['meanMs']:.1f} ms (sd {s['stddevMs']:.1f}, max {s['maxAbsMs']:.1f}, n={s['frames']}); "
                f"paint {s['paintMsAvg']:.1f} ms avg; GUI stall max {e.get('maxEventLoopStallMs', 0):.0f} ms; "
                f"fullscreen {e['fullscreen']}, controls {'shown' if e['controlsVisible'] else 'hidden'}"
                + (f"; missing: {', '.join(e['missingPlugins'])}" if e['missingPlugins'] else "")
                + (f"; audio {e['audioTracks']} (current {e['currentAudio']}), subtitles {e['subtitleTracks']} (current {e['currentSubtitle']})" if e['audioTracks'] and len(e['audioTracks']) > 1 else ""))
        if e["cmd"] == "finished" and e.get("failures"):
            lines.append(f"- run reported {e['failures']} failure(s)")
head = f"# Automation summary\n\nChecks: {total}, failed: {failed}\n"
text = head + "\n".join(lines) + "\n"
(out / "summary.md").write_text(text)
print(text)
if total == 0:
    print("ERROR: no checks ran - the player probably failed to start (see the *.log files)")
sys.exit(1 if failed or total == 0 else 0)
