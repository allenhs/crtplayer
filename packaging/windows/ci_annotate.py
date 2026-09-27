#!/usr/bin/env python3
"""Posts a log's important lines as GitHub annotations (readable on the run's page and through
the public API without downloading logs). Lines are packed into a few multi-line annotations:
GitHub shows only 10 per step.
Usage: ci_annotate.py LEVEL TITLE LOGFILE|- [PATTERN]   (LEVEL: error | notice)"""
import os, re, sys
level, title, log = sys.argv[1], sys.argv[2], sys.argv[3]
pat = sys.argv[4] if len(sys.argv) > 4 else r'error|Error|fatal|undefined reference|No such file|FAIL|failed'
if log == '-':
    text = sys.stdin.read()
elif os.path.exists(log):
    text = open(log, errors='replace').read()
else:
    sys.exit(0)
lines = [l.rstrip('\r') for l in text.split('\n') if l.strip() and re.search(pat, l)][:120]
chunks, chunk = [], []
for l in lines:
    l = l[:600]
    if chunk and sum(len(x) + 1 for x in chunk) + len(l) > 3500:
        chunks.append(chunk); chunk = []
    chunk.append(l)
if chunk:
    chunks.append(chunk)
enc = lambda s: s.replace('%', '%25').replace('\r', '').replace('\n', '%0A')
n = min(len(chunks), 8)
for i, c in enumerate(chunks[:8]):
    print(f"::{level} title={title} ({i + 1}/{n})::" + enc('\n'.join(c)))
