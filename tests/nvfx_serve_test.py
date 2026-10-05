#!/usr/bin/env python3
"""The helper's serve mode (crtplayer-nvfx --serve), against the stand-in SDK of tests/nvfx_mock.
Usage: nvfx_serve_test.py HELPER MOCK_SDK"""
import mmap, os, socket, subprocess, sys, time

HELPER, SDK = sys.argv[1], sys.argv[2]
passed = failed = 0

def check(name, ok, detail=""):
    global passed, failed
    if ok: passed += 1
    else: failed += 1
    print(("PASS " if ok else "FAIL ") + name + (("  [" + str(detail) + "]") if detail != "" else ""))

def pages(n): return (n + 4095) // 4096 * 4096

class Helper:
    def __init__(self, env=None, sdk=SDK):
        self.a, b = socket.socketpair()
        e = dict(os.environ)
        for k in list(e):
            if k.startswith("NVFX_MOCK") or k == "CRTPLAYER_NVFX_STARTED": del e[k]
        e.update(env or {})
        self.p = subprocess.Popen([HELPER, "--serve", "--sdk", sdk], stdin=b.fileno(), stdout=b.fileno(), stderr=subprocess.DEVNULL, env=e)
        b.close()
        self.f = self.a.makefile("rw", buffering=1, newline="\n")
        self.n = 0
        self.shm = None
    def send(self, cmd):
        self.n += 1
        self.f.write("%d %s\n" % (self.n, cmd))
        self.f.flush()
        return self.n
    def read(self):
        line = self.f.readline()
        return line.rstrip("\n")
    def ask(self, cmd):
        try:
            i = self.send(cmd)
            r = self.read()
        except (BrokenPipeError, ConnectionResetError):
            return "(closed)"
        assert r.startswith("%d " % i) or r == "", (cmd, r)
        return r.split(" ", 1)[1] if r else "(closed)"
    def open(self, sw, sh, ow, oh, q, mode):
        name = "/crtplayer-nvfx-test-%d-%d" % (os.getpid(), self.n)
        self.inp, o = pages(sw * sh * 4), pages(ow * oh * 4)
        self.slot = [self.inp, self.inp + o]
        total = self.inp + 2 * o
        fd = os.open("/dev/shm" + name, os.O_CREAT | os.O_RDWR, 0o600)
        os.ftruncate(fd, total)
        if self.shm: self.shm.close()
        self.shm = mmap.mmap(fd, total)
        os.close(fd)
        r = self.ask("open %s %d %d %d %d %d %d" % (name, sw, sh, ow, oh, q, mode))
        os.unlink("/dev/shm" + name)
        self.sw, self.sh, self.ow, self.oh = sw, sh, ow, oh
        return r
    def put(self, px): self.shm[0:len(px)] = px
    def out(self, slot): return self.shm[self.slot[slot]:self.slot[slot] + self.ow * self.oh * 4]
    def close(self):
        try: self.f.close(); self.a.close()
        except Exception: pass
        try: return self.p.wait(timeout=5)
        except subprocess.TimeoutExpired: self.p.kill(); return None

def flat(w, h, r, g, b): return bytes([r, g, b, 255]) * (w * h)
def ramp(w, h, shift=0):
    row = bytearray()
    for x in range(w): row += bytes([(x * 255 // (w - 1) + shift) % 256, 60, 200, 255])
    return bytes(row) * h
def px(buf, w, x, y): return tuple(buf[(y * w + x) * 4:(y * w + x) * 4 + 3])
def near(a, b, tol=2): return all(abs(x - y) <= tol for x, y in zip(a, b))

# ---- hello
h = Helper()
r = h.ask("hello")
check("hello: the SDK's version and both features", r.startswith("ok proto=1 sdk=1.3.0") and "nvvfxvideosuperres" in r and "nvvfxvideoframegeneration" in r, r)
check("an unknown request is refused, not fatal", h.ask("dance").startswith("err"))
check("a frame before anything is open is refused", h.ask("frame 0").startswith("err"))

# ---- super resolution alone
r = h.open(64, 36, 128, 72, 3, -1)
check("open: super resolution alone", r.startswith("ok load="), r)
h.put(ramp(64, 36))
check("frame", h.ask("frame 1").startswith("ok ms="))
r = h.ask("get 0 1")
check("get: the newest frame", r.startswith("ok kind=c"), r)
o = h.out(0)
check("the result has the larger size's content (a ramp left to right)", px(o, 128, 10, 40)[0] < px(o, 128, 64, 40)[0] < px(o, 128, 120, 40)[0], (px(o, 128, 10, 40), px(o, 128, 120, 40)))
check("the other channels are in place (RGBA)", near(px(o, 128, 64, 40)[1:], (60, 200)), px(o, 128, 64, 40))
check("rows are packed: the last row is written, the next slot untouched", px(o, 128, 127, 71)[1] == 60 and h.out(1)[0:4] == b"\0\0\0\0")
r = h.ask("get 1 0.5")
check("without frame generation every moment is the newest frame", r.startswith("ok kind=c") and h.out(1) == o, r)
check("a third slot is refused", h.ask("get 2 1").startswith("err"))

# ---- frame generation alone
r = h.open(64, 36, 64, 36, 0, 1)
check("open: frame generation alone", r.startswith("ok load="), r)
check("a picture before any frame is refused", h.ask("get 0 1").startswith("err"))
A, B, C = flat(64, 36, 200, 0, 0), flat(64, 36, 0, 100, 0), flat(64, 36, 0, 0, 80)
h.put(A); h.ask("frame 1")
r = h.ask("get 0 0.5")
check("one frame only: that frame, whatever the moment", r.startswith("ok kind=c") and h.out(0) == A, r)
h.put(B); h.ask("frame 0")
r = h.ask("get 0 0.25")
check("between two frames: generated", r.startswith("ok kind=g"), r)
check("... a quarter of the way from the frame before to the newest", near(px(h.out(0), 64, 30, 20), (150, 25, 0)), px(h.out(0), 64, 30, 20))
check("t = 0: the frame before, as it was", h.ask("get 1 0").startswith("ok kind=p") and h.out(1) == A)
check("t = 1: the newest frame, as it is", h.ask("get 1 1").startswith("ok kind=c") and h.out(1) == B)
h.put(C); h.ask("frame 0")
h.ask("get 0 0.5")
check("the pair moves on with each frame", near(px(h.out(0), 64, 5, 5), (0, 50, 40)), px(h.out(0), 64, 5, 5))
h.put(A); h.ask("frame 1")
r1 = h.ask("get 0 0.3"); o1 = h.out(0)
r2 = h.ask("get 1 0.7"); o2 = h.out(1)
check("across a cut nothing is generated: the nearer real frame", "kind=p" in r1 and o1 == C and "kind=c" in r2 and o2 == A, (r1, r2))
# requests sent together are answered in order
h.put(B)
ids = [h.send("frame 0"), h.send("get 0 0.5"), h.send("get 1 1")]
rs = [h.read() for _ in ids]
check("requests sent together are answered in order", [r.split(" ")[0] for r in rs] == [str(i) for i in ids] and all(" ok" in r for r in rs), rs)
check("... each with its own picture", near(px(h.out(0), 64, 9, 9), (100, 50, 0)) and h.out(1) == B, px(h.out(0), 64, 9, 9))
check("close", h.ask("close") == "ok" and h.ask("get 0 1").startswith("err"))
check("the helper leaves when the connection closes", h.close() == 0)

# ---- both: upscaled first, frames generated at the larger size
for label, env, card in (("on the card", {"NVFX_MOCK_MARK": "1"}, "card=1"), ("through ordinary memory when the card cannot", {"NVFX_MOCK_MARK": "1", "NVFX_MOCK_FAIL_CARD": "1"}, "card=0")):
    h = Helper(env)
    h.ask("hello")
    r = h.open(64, 40, 192, 120, 3, 1)
    check("open: both, %s" % label, r.startswith("ok") and card in r, r)
    h.put(flat(64, 40, 200, 200, 200)); h.ask("frame 1")
    h.put(flat(64, 40, 100, 100, 100)); h.ask("frame 0")
    r = h.ask("get 0 0.5"); o = h.out(0)
    side = 120 // 20
    check("  generated between the two upscaled frames", "kind=g" in r and near(px(o, 192, 100, 60), (150, 150, 150)), (r, px(o, 192, 100, 60)))
    check("  super resolution's stamp: red, then blue, top left", near(px(o, 192, 2, 2), (255, 0, 0)) and near(px(o, 192, side + 2, 2), (0, 0, 255)), (px(o, 192, 2, 2), px(o, 192, side + 2, 2)))
    check("  frame generation's stamp: green, below", near(px(o, 192, 2, side + 2), (0, 255, 0)), px(o, 192, 2, side + 2))
    r = h.ask("get 1 1"); o = h.out(1)
    check("  the newest frame: upscaled, not generated", "kind=c" in r and near(px(o, 192, 100, 60), (100, 100, 100)) and near(px(o, 192, 2, 2), (255, 0, 0)) and near(px(o, 192, 2, side + 2), (100, 100, 100)), (r, px(o, 192, 2, side + 2)))
    r = h.ask("get 1 0"); o = h.out(1)
    check("  the frame before: upscaled, not generated", "kind=p" in r and near(px(o, 192, 100, 60), (200, 200, 200)) and near(px(o, 192, 2, side + 2), (200, 200, 200)), r)
    h.close()

# ---- faults
h = Helper({"NVFX_MOCK_FAIL_LOAD": "sr"})
h.ask("hello")
r = h.open(64, 36, 128, 72, 3, 1)
check("an effect that cannot be loaded: open is refused with the reason", r.startswith("err") and "Super Resolution" in r, r)
r = h.open(64, 36, 64, 36, 0, 1)
check("... and the helper is still there for what does work", r.startswith("ok"), r)
h.close()
h = Helper({"NVFX_MOCK_FAIL_SIZE": "100"})
h.ask("hello")
check("a size the effect refuses", h.open(64, 36, 100, 56, 3, -1).startswith("err"))
check("a size out of range", h.open(4, 4, 8, 8, 3, -1).startswith("err"))
check("another size without super resolution", h.open(64, 36, 128, 72, 0, 1).startswith("err"))
check("neither effect", h.open(64, 36, 64, 36, 0, -1).startswith("err"))
r = h.ask("open /crtplayer-nvfx-nonexistent 64 36 128 72 3 -1")
check("shared memory that is not there", r.startswith("err") and "shared memory" in r, r)
h.close()
h = Helper({"NVFX_MOCK_CRASH_AFTER": "4"})
h.ask("hello"); h.open(64, 36, 64, 36, 0, 1)
h.put(A); h.ask("frame 1"); h.ask("frame 0")
rs = [h.ask("get 0 0.5") for _ in range(4)]
check("the helper dying shows as a closed connection", rs[-1] == "(closed)" and h.close() not in (0, None), rs)
h = Helper(sdk="/nonexistent")
r = h.ask("hello")
check("no SDK: the helper says so", r.startswith("err") and "not installed" in r, r)
h.close()
empty = "/tmp/nvfx-empty-sdk"
os.makedirs(empty + "/lib", exist_ok=True)
open(empty + "/lib/libVideoFX.so", "w").close()
h = Helper(sdk=empty)
r = h.ask("hello")
check("an SDK that cannot be loaded: hello and open say why", r.startswith("err") and h.open(64, 36, 128, 72, 3, -1).startswith("err"), r)
h.close()
r = subprocess.run([HELPER, "--where", "--sdk", SDK], capture_output=True, text=True)
check("--where: the folder and the features, without loading anything", r.returncode == 0 and ("sdk=" + SDK) in r.stdout and "nvvfxvideosuperres" in r.stdout, r.stdout)
r = subprocess.run([HELPER, "--where", "--sdk", "/nonexistent"], capture_output=True, text=True)
check("--where: nothing installed", r.returncode == 3 and r.stdout.strip() == "sdk=", r.stdout)
print("\n%d passed, %d failed" % (passed, failed))
sys.exit(1 if failed else 0)
