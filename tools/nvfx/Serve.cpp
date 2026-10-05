// crtplayer-nvfx --serve: the helper at work for the player (the protocol is in NvShm.h).
#include "NvFx.h"
#include "NvShm.h"

#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <sstream>
#include <string>
#include <sys/mman.h>
#include <sys/prctl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

namespace {
using Clock = std::chrono::steady_clock;
double msSince(Clock::time_point t0) { return std::chrono::duration<double, std::milli>(Clock::now() - t0).count(); }

int gOut = 1;   // where answers go (the libraries' own chatter on standard output is sent to the log)

bool writeAll(const std::string& s)
{
    size_t done = 0;
    while (done < s.size()) {
        const ssize_t n = write(gOut, s.data() + done, s.size() - done);
        if (n < 0) { if (errno == EINTR) continue; return false; }
        done += size_t(n);
    }
    return true;
}

std::string oneLine(std::string s)
{
    for (char& c : s) if (c == '\n' || c == '\r') c = ' ';
    return s;
}

struct Shm {
    uint8_t* base = nullptr;
    size_t size = 0;
    NvShmLayout layout;
    void close() { if (base) munmap(base, size); base = nullptr; size = 0; }
    bool open(const std::string& name, const NvShmLayout& l, std::string* error)
    {
        close();
        const int fd = shm_open(name.c_str(), O_RDWR, 0);
        if (fd < 0) { *error = "the shared memory " + name + " could not be opened: " + std::strerror(errno); return false; }
        struct stat st;
        if (fstat(fd, &st) != 0 || size_t(st.st_size) < l.total) { ::close(fd); *error = "the shared memory is smaller than the pictures need"; return false; }
        void* p = mmap(nullptr, l.total, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        ::close(fd);
        if (p == MAP_FAILED) { *error = std::string("the shared memory could not be mapped: ") + std::strerror(errno); return false; }
        base = static_cast<uint8_t*>(p);
        size = l.total;
        layout = l;
        return true;
    }
};
} // namespace

int nvServe(const std::string& sdk)
{
    // Answers on a descriptor of our own; whatever a library prints to standard output goes to the log instead.
    gOut = fcntl(1, F_DUPFD_CLOEXEC, 10);
    if (gOut < 0) gOut = dup(1);
    dup2(2, 1);
    signal(SIGPIPE, SIG_IGN);
    prctl(PR_SET_PDEATHSIG, SIGTERM);   // (the player gone: so are we)

    std::string loadError = "it is not installed (no SDK folder was found)";
    const bool loaded = !sdk.empty() && nvInit(sdk, &loadError);
    fprintf(stderr, "crtplayer-nvfx: serving; SDK %s %s%s\n", sdk.c_str(), loaded ? nvVersion().c_str() : "NOT LOADED: ", loaded ? "" : loadError.c_str());

    NvPipeline pipe;
    Shm shm;
    std::string buf;
    char chunk[4096];
    for (;;) {
        size_t nl;
        while ((nl = buf.find('\n')) == std::string::npos) {
            const ssize_t n = read(0, chunk, sizeof chunk);
            if (n < 0 && errno == EINTR) continue;
            if (n <= 0) return 0;   // the player closed the connection (or is gone)
            buf.append(chunk, size_t(n));
            if (buf.size() > 65536) return 2;
        }
        std::istringstream line(buf.substr(0, nl));
        buf.erase(0, nl + 1);
        std::string id, cmd;
        line >> id >> cmd;
        if (id.empty()) continue;
        std::string answer, error;
        char num[256];
        if (cmd == "quit") return 0;
        if (cmd == "hello") {
            if (!loaded) error = "the NVIDIA Video Effects SDK could not be loaded: " + loadError;
            else {
                std::string feats;
                for (const std::string& f : nvFeatures(sdk)) feats += (feats.empty() ? "" : ",") + f;
                answer = "proto=" + std::to_string(kNvProtocol) + " sdk=" + nvVersion() + " features=" + (feats.empty() ? "-" : feats);
            }
        } else if (cmd == "open") {
            std::string name;
            NvPipelineConfig c;
            line >> name >> c.srcW >> c.srcH >> c.outW >> c.outH >> c.srQuality >> c.fgMode;
            pipe.close();
            shm.close();
            const Clock::time_point t0 = Clock::now();
            if (line.fail()) error = "open: values missing";
            else if (!loaded) error = "the NVIDIA Video Effects SDK could not be loaded: " + loadError;
            else if (!pipe.open(c, &error)) {}
            else if (!shm.open(name, nvShmLayout(c.srcW, c.srcH, c.outW, c.outH), &error)) pipe.close();
            else {
                snprintf(num, sizeof num, "load=%.0f card=%d", msSince(t0), pipe.onCard() ? 1 : 0);
                answer = num;
                fprintf(stderr, "crtplayer-nvfx: open %dx%d -> %dx%d quality %d mode %d: %s\n", c.srcW, c.srcH, c.outW, c.outH, c.srQuality, c.fgMode, num);
            }
            if (!error.empty()) fprintf(stderr, "crtplayer-nvfx: open %dx%d -> %dx%d quality %d mode %d FAILED: %s\n", c.srcW, c.srcH, c.outW, c.outH, c.srQuality, c.fgMode, error.c_str());
        } else if (cmd == "frame") {
            int cut = 0;
            line >> cut;
            const Clock::time_point t0 = Clock::now();
            if (!pipe.isOpen() || !shm.base) error = "nothing is open";
            else if (pipe.frame(shm.base + shm.layout.input, cut != 0, &error)) { snprintf(num, sizeof num, "ms=%.2f", msSince(t0)); answer = num; }
        } else if (cmd == "get") {
            int slot = -1;
            float t = 1.f;
            line >> slot >> t;
            const Clock::time_point t0 = Clock::now();
            char kind = '?';
            NvTiming tm;
            if (line.fail() || slot < 0 || slot > 1) error = "get: which slot?";
            else if (!pipe.isOpen() || !shm.base) error = "nothing is open";
            else if (pipe.get(t, shm.base + shm.layout.slot[slot], &kind, &tm, &error)) {
                snprintf(num, sizeof num, "kind=%c ms=%.2f run=%.2f down=%.2f", kind, msSince(t0), tm.runMs, tm.downloadMs);
                answer = num;
            }
        } else if (cmd == "close") {
            pipe.close();
            shm.close();
        } else error = "unknown request: " + cmd;
        if (!error.empty() && cmd != "open" && cmd != "hello") fprintf(stderr, "crtplayer-nvfx: %s failed: %s\n", cmd.c_str(), error.c_str());
        const std::string out = id + (error.empty() ? " ok" + (answer.empty() ? "" : " " + answer) : " err " + oneLine(error)) + "\n";
        if (!writeAll(out)) return 0;
    }
}
