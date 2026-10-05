#pragma once
// What the player and its helper (crtplayer-nvfx) agree on: the helper is started with
// `--serve`, its standard input and output are one socket carrying lines of text, and the
// pictures themselves lie in a block of shared memory that the player creates.
//
// Every request is "<id> <command> [values]"; every answer "<id> ok [name=value ...]" or
// "<id> err <what went wrong>". Answers come in the order of the requests, so the player
// may send several requests before reading any answer.
//
//   hello                                   -> ok proto=1 sdk=<version> features=<a,b>
//   open <shm> <srcW> <srcH> <outW> <outH> <quality> <mode>
//                                           super resolution quality (0 = none), frame generation mode (-1 = none)
//                                           -> ok load=<ms> card=<0|1>
//   frame <cut>                             the frame in the block's input area is the video's next frame
//                                           -> ok ms=<ms>
//   get <slot> <t>                          the picture at t into output slot 0 or 1
//                                           -> ok kind=<p|c|g> ms=<ms> run=<ms> down=<ms>
//   close                                   -> ok
//   quit                                    (no answer; the helper also leaves when its input closes)
//
// Pictures are RGBA, 8 bits, rows packed (no padding), the top row first.
#include <cstddef>

constexpr int kNvProtocol = 1;

struct NvShmLayout {
    size_t input = 0;          // srcW x srcH x 4 bytes
    size_t slot[2] = {0, 0};   // outW x outH x 4 bytes each
    size_t total = 0;
};

inline NvShmLayout nvShmLayout(int srcW, int srcH, int outW, int outH)
{
    auto pages = [](size_t n) { return (n + 4095) / 4096 * 4096; };
    const size_t in = pages(size_t(srcW) * size_t(srcH) * 4), out = pages(size_t(outW) * size_t(outH) * 4);
    NvShmLayout l;
    l.input = 0;
    l.slot[0] = in;
    l.slot[1] = in + out;
    l.total = in + 2 * out;
    return l;
}
