#pragma once
#include <gst/gst.h>

// A video filter of the player's own, first in its video sink when frames are converted on the CPU
// (no graphics card): it shrinks the picture by a whole factor and to 8 bits a sample in one cheap
// pass (ShrinkKernel.h), so that GStreamer's scaler and converter get a small 8-bit picture.
//
//   Off        frames pass untouched (frames as decoded, for a graphics card)
//   Depth      the pictures become 8-bit ones at their own size
//   Fit(w, h)  and are shrunk on the way, by the largest whole factor that leaves them at least w × h
//
// Planar YUV pictures with 10 or 12 bits a sample (4:2:0, 4:2:2, 4:4:4), not interlaced; any other
// frame passes untouched and is scaled and converted as before.
GstElement* crt_shrink_new();

enum class ShrinkMode { Off, Depth, Fit };
// From any thread; takes effect with the next frame.
void crt_shrink_set(GstElement* shrink, ShrinkMode mode, int width = 0, int height = 0);

// For the tests: frames it has shrunk so far, the factor in use (0: passing frames untouched), and its output size.
struct ShrinkState { guint64 frames; int factor; int outWidth; int outHeight; double msPerFrame; };
ShrinkState crt_shrink_state(GstElement* shrink);
