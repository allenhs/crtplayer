#pragma once
#include <gst/gst.h>

#include <QByteArray>
#include <QJsonObject>
#include <QList>
#include <QPair>
#include <QString>

// A video from a web site, as yt-dlp finds it (2.17): one address, or two that belong together (the
// picture at one, the sound at the other: how YouTube serves everything above 360p).
//
// playbin opens one address. So the player has a source element of its own, behind the address
// "crtweb://N": inside it, each of the real addresses has a reader of its own (RangeSource.h, 2.18;
// before, GStreamer's HTTP source and a ring buffer), and comes out of a pad of its own. playbin's
// decoding bin then finds a demuxer and decoder for each, and plays them as the picture and the sound
// of one video: started together, sought together.
//
// The readers give the demuxers random access (reading any part of the file, over HTTP by byte
// ranges). GStreamer's MP4 demuxer needs that to seek in the fragmented MP4 files such sites serve:
// read front to back only, it could not jump ahead at all (measured).
struct WebStream {
    QString url;
    QList<QPair<QByteArray, QByteArray>> headers;   // as the site wants them (User-Agent, Referer, Cookie, ...)
    bool audioOnly = false;                         // (a smaller buffer)
    int kbps = 0;                                   // its bit rate, where the site says (how much to have at hand before playing on)
    qint64 chunkBytes = 0;                          // the most to ask for at once (yt-dlp's "http_chunk_size"; 0: 10 MiB)
};

// Registers the element with GStreamer (once; safe to call again).
void crt_web_init();
// The address playbin is given for these streams. It stays valid until forgotten.
QString crt_web_register(const QList<WebStream>& streams);
void crt_web_forget(const QString& uri);
bool crt_web_is(const QString& uri);
// For the tests: what was read so far, per stream (bytes, HTTP requests are counted by the test server).
QJsonObject crt_web_report(const QString& uri);

// How each stream of a playing source stands (the element playbin made for a "crtweb://" address):
// how much is read ahead of the place being played, whether the file's end has arrived, how long ago the
// server last sent anything.
struct WebLevel {
    bool audioOnly = false;
    quint64 ahead = 0;        // bytes
    quint64 room = 0;         // how much may be read ahead: a reader that has filled it is not sending because it need not
    quint64 received = 0;     // bytes, in all
    bool ended = false;
    qint64 quietMs = 0;
    int kbps = 0;
    int requests = 0;         // HTTP requests made
};
bool crt_web_is_source(GstElement* element);
QList<WebLevel> crt_web_levels(GstElement* source);
