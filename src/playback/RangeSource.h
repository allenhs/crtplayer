#pragma once
#include <gst/gst.h>

#include "WebSource.h"

// 2.18: the reader of one web stream (the picture's, or the sound's). It replaces GStreamer's HTTP
// source and its ring buffer (souphttpsrc + queue2) inside "crtweb://".
//
// Why: it asked the server for "everything from this byte on", once per jump. YouTube's video
// servers send such a request slowly (about as fast as the video plays); yt-dlp, for that reason,
// asks for 10 MiB at a time ("http_chunk_size"), and so does this reader now. Every jump waited
// for the throttled request: measured against a server that behaves that way, 1.1 to 1.5 s a jump
// instead of 0.1 to 0.2 s.
//
// It keeps what it has read in blocks around the place being played (a jump back to where it has
// been costs nothing), reads ahead of that place, and lets the demuxer read any part of the file
// (pull mode), fetching a missing part by a byte range. A network that goes away is waited for (and
// asked again, for half a minute); a server that refuses (an address that has run out) is an error
// at once, which the player answers by asking yt-dlp again.
//
// The network work is done by Qt (one thread for all readers), which also speaks HTTP/2: a jump
// then reuses the connection to the server instead of opening a new one.

GstElement* crt_range_src_new(const WebStream& stream, quint64 aheadBytes, quint64 keepBytes);
bool crt_range_src_is(GstElement* element);

struct RangeStats {
    quint64 ahead = 0;       // bytes at hand from the place being read on
    quint64 room = 0;        // how much is read ahead at most
    quint64 received = 0;    // bytes, in all
    qint64 size = -1;
    bool ended = false;      // everything from the place being read to the end is at hand
    qint64 quietMs = 0;      // since the server last sent anything (0 while nothing is wanted)
    int requests = 0;
};
RangeStats crt_range_src_stats(GstElement* element);
