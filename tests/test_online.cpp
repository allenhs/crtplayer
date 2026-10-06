// Videos from web sites (src/online/OnlineVideo.cpp): what yt-dlp is asked, how its answer is read,
// which addresses are pages, how a web video is kept in the playlist, automatic captions tidied, the
// zip archive Deno comes in.
#include "online/OnlineVideo.h"

#include <QCoreApplication>
#include <QDate>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <cstdio>
#include <zlib.h>

static int g_fail = 0;
static void check(const char* name, bool ok, const QString& detail)
{
    std::printf("%s  %s: %s\n", ok ? "PASS" : "FAIL", name, detail.toUtf8().constData());
    if (!ok) ++g_fail;
}

static QJsonObject format(const QString& url, const QString& vcodec, const QString& acodec, int height, const QString& protocol = QStringLiteral("https"))
{
    QJsonObject f{{"url", url}, {"vcodec", vcodec}, {"acodec", acodec}, {"protocol", protocol},
                  {"http_headers", QJsonObject{{"User-Agent", "UA/1"}, {"Accept-Language", "en"}}}};
    if (height > 0) { f["height"] = height; f["width"] = height * 16 / 9; f["fps"] = 60; }
    return f;
}

// A zip archive with one file in it, stored or deflated.
static QByteArray zipOf(const QByteArray& name, const QByteArray& data, bool deflated)
{
    QByteArray packed = data;
    if (deflated) {
        z_stream z{};
        deflateInit2(&z, 9, Z_DEFLATED, -MAX_WBITS, 8, Z_DEFAULT_STRATEGY);
        packed.resize(int(deflateBound(&z, data.size())));
        z.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(data.constData()));
        z.avail_in = data.size();
        z.next_out = reinterpret_cast<Bytef*>(packed.data());
        z.avail_out = packed.size();
        deflate(&z, Z_FINISH);
        packed.resize(int(z.total_out));
        deflateEnd(&z);
    }
    const quint32 crc = quint32(crc32(crc32(0L, Z_NULL, 0), reinterpret_cast<const Bytef*>(data.constData()), data.size()));
    auto le = [](quint32 v, int n) { QByteArray b; for (int i = 0; i < n; ++i) b.append(char(v >> (8 * i))); return b; };
    const QByteArray common = le(20, 2) + le(0, 2) + le(deflated ? 8 : 0, 2) + le(0, 4) + le(crc, 4) + le(packed.size(), 4) + le(data.size(), 4) + le(name.size(), 2) + le(0, 2);
    QByteArray zip = QByteArray("PK\x03\x04", 4) + common + name + packed;
    const int dir = zip.size();
    zip += QByteArray("PK\x01\x02", 4) + le(20, 2) + common + le(0, 2) + le(0, 2) + le(0, 2) + le(0, 4) + le(0, 4) + name;
    const int dirSize = zip.size() - dir;
    zip += QByteArray("PK\x05\x06", 4) + le(0, 2) + le(0, 2) + le(1, 2) + le(1, 2) + le(dirSize, 4) + le(dir, 4) + le(0, 2);
    return zip;
}

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    using R = OnlineResolver;

    // ---- which addresses are pages
    check("a video site's page is a page", R::isPage("https://www.youtube.com/watch?v=dQw4w9WgXcQ") && R::isPage("https://youtu.be/dQw4w9WgXcQ") && R::isPage("https://vimeo.com/76979871"), "asked of yt-dlp");
    check("a media file's address is not (whatever follows the name)", !R::isPage("https://example.com/a/movie.mkv") && !R::isPage("http://example.com/clip.MP4?token=abc#t=3"), "opened as it is");
    check("a manifest's address is not", !R::isPage("https://example.com/live/master.m3u8") && !R::isPage("https://example.com/v/manifest.mpd"), "opened as it is");
    check("files and other schemes are not", !R::isPage("/home/me/Videos/a.mkv") && !R::isPage("file:///home/me/a") && !R::isPage("jellyfin:///abc#Title") && !R::isPage("rtsp://cam/stream"), "left alone");

    // ---- how a web video is kept in the playlist
    {
        const QString page = QStringLiteral("https://example.com/watch?v=1&t=2#chapter-3"), title = QStringLiteral("Über #1: 100% “Fun” / more");
        const QString ref = R::reference(page, title);
        check("a playlist entry carries the page and the title, and gives both back", R::isReference(ref) && R::referencePage(ref) == page && R::referenceTitle(ref) == title, ref);
        check("an entry without a title", R::referencePage(R::reference(page, QString())) == page && R::referenceTitle(R::reference(page, QString())).isEmpty(), R::reference(page, QString()));
        check("a plain address is not an entry (and is its own page)", !R::isReference(page) && R::referencePage(page) == page && R::referenceTitle(page).isEmpty(), page);
    }

    // ---- what yt-dlp is asked
    {
        OnlineOptions o;
        o.maxHeight = 1440;
        o.videoCodecs = {"h264", "vp9", "av1", "hevc"};
        o.audioCodecs = {"aac", "opus"};
        const QStringList a = R::arguments("https://example.com/watch?v=1", o, {"deno:/opt/deno"});
        const QString f = a.value(a.indexOf("-f") + 1);
        check("one answer as JSON, this video only, nothing downloaded", a.contains("--dump-single-json") && a.contains("--no-playlist") && a.contains("--flat-playlist") && !a.contains("-o"), a.join(' ').left(90));
        check("the address comes last, after \"--\" (an address cannot pass for an option)", a.size() > 2 && a[a.size() - 2] == "--" && a.last() == "https://example.com/watch?v=1", a.mid(a.size() - 2).join(' '));
        check("the picture no larger than asked for", f.contains("[height<=1440]") && !f.contains("[height<=1080]"), f.left(70));
        check("first choice: picture and sound as two plain HTTP(S) files, not HDR", f.startsWith("bv[protocol~='^https?$'][dynamic_range=SDR][height<=1440]+ba[protocol~='^https?$']/"), f.left(80));
        check("last choice: whatever there is", f.endsWith("/bv+ba/b"), f.right(30));
        check("every codec decoded here: none is ruled out", !f.contains("vcodec!") && !f.contains("acodec!") && !a.contains("-S"), "no codec filter");
        check("the JavaScript runtime is named", a.indexOf("--js-runtimes") > 0 && a.value(a.indexOf("--js-runtimes") + 1) == "deno:/opt/deno", "--js-runtimes deno:/opt/deno");
        o.videoCodecs = {"h264", "vp9"};
        o.audioCodecs = {"aac"};
        const QStringList b = R::arguments("https://example.com/x", o, {});
        const QString fb = b.value(b.indexOf("-f") + 1);
        check("no AV1 or HEVC decoder: those versions are not asked for", fb.contains("[vcodec!^=av01]") && fb.contains("[vcodec!^=hev1]") && !fb.contains("[vcodec!^=vp09]"), fb.left(110));
        check("no Opus decoder: not asked for", fb.contains("[acodec!=opus]") && !fb.contains("mp4a"), "acodec!=opus");
        check("... and among pictures of one size, VP9 before H.264", b.value(b.indexOf("-S") + 1) == "res,fps,vcodec:vp9" && !b.contains("--js-runtimes"), b.value(b.indexOf("-S") + 1));
        o.preferH264 = true;
        const QStringList c = R::arguments("https://example.com/x", o, {});
        check("no graphics card: the largest picture, and H.264 among those of that size", c.value(c.indexOf("-S") + 1) == "res,vcodec:h264", c.value(c.indexOf("-S") + 1));
        o.maxHeight = 0;
        check("no size asked for: 1080", R::arguments("u", o, {}).value(7).contains("[height<=1080]"), "height<=1080");
    }

    // ---- yt-dlp's answer: the picture and the sound as two addresses (YouTube)
    {
        QJsonObject root{{"id", "abc"}, {"title", "A video"}, {"channel", "A channel"}, {"extractor_key", "Youtube"}, {"webpage_url", "https://www.youtube.com/watch?v=abc"},
                         {"duration", 212.5}, {"language", "en"}};
        // (the sound first, as it may come: the picture is put first)
        root["requested_formats"] = QJsonArray{format("https://a.example/audio", "none", "opus", 0), format("https://v.example/video", "vp09.00.40.08", "none", 1440)};
        QJsonObject withCookies = root["requested_formats"].toArray()[1].toObject();
        withCookies["cookies"] = "sid=1 2 3; Domain=.example; Path=/; Secure; Expires=1791000000; pref=a=b; Domain=.example; Path=/";
        root["requested_formats"] = QJsonArray{root["requested_formats"].toArray()[0], withCookies};
        root["subtitles"] = QJsonObject{{"en", QJsonArray{QJsonObject{{"ext", "json3"}, {"url", "https://s/en.json3"}}, QJsonObject{{"ext", "srt"}, {"url", "https://s/en.srt"}, {"name", "English"}},
                                                          QJsonObject{{"ext", "vtt"}, {"url", "https://s/en.vtt"}, {"name", "English"}}}},
                                        {"pt-BR", QJsonArray{QJsonObject{{"ext", "vtt"}, {"url", "https://s/pt.vtt"}}}},
                                        {"xx", QJsonArray{QJsonObject{{"ext", "json3"}, {"url", "https://s/xx.json3"}}}},
                                        {"live_chat", QJsonArray{QJsonObject{{"ext", "json"}, {"url", "https://s/chat"}}}}};
        root["automatic_captions"] = QJsonObject{{"en-orig", QJsonArray{QJsonObject{{"ext", "vtt"}, {"url", "https://s/auto.vtt"}, {"name", "English (Original)"}}}},
                                                 {"en", QJsonArray{QJsonObject{{"ext", "vtt"}, {"url", "https://s/auto-en.vtt"}, {"name", "English"}}}},
                                                 {"de", QJsonArray{QJsonObject{{"ext", "vtt"}, {"url", "https://s/auto-de.vtt"}, {"name", "German"}}}}};
        root["chapters"] = QJsonArray{QJsonObject{{"start_time", 0}, {"title", "Intro"}}, QJsonObject{{"start_time", 61.5}, {"title", "Main"}}};
        const OnlineResult r = R::parse(QJsonDocument(root).toJson(), "WARNING: [youtube] something harmless\n", 0, {});
        const OnlineVideo& v = r.video;
        check("read as a video", r.ok && !r.playlist && r.error.isEmpty(), r.error);
        check("its page, title, uploader, site, length", v.page == "https://www.youtube.com/watch?v=abc" && v.title == "A video" && v.uploader == "A channel" && v.site == "Youtube" && v.seconds == 212.5,
              QString("%1 | %2 | %3 | %4 | %5 s").arg(v.page, v.title, v.uploader, v.site).arg(v.seconds));
        check("two streams, the picture first, the sound marked as sound", v.streams.size() == 2 && v.streams[0].url == "https://v.example/video" && !v.streams[0].audioOnly && v.streams[1].audioOnly && v.manifest.isEmpty(),
              QString("%1 streams").arg(v.streams.size()));
        check("what it is, in a few words", v.what == QString::fromUtf8("1440p60 · VP9 + Opus") && v.height == 1440, v.what);
        QByteArray cookie, agent;
        for (const auto& h : v.streams.value(0).headers) { if (h.first == "Cookie") cookie = h.second; if (h.first == "User-Agent") agent = h.second; }
        check("the site's headers go with each stream", agent == "UA/1", QString::fromUtf8(agent));
        check("cookies: the names and values, without their attributes", cookie == "sid=1 2 3; pref=a=b", QString::fromUtf8(cookie));
        QStringList subs;
        for (const OnlineSubtitle& s : v.subtitles) subs << QString("%1:%2:%3%4").arg(s.language, s.ext, s.url.section('/', -1), s.automatic ? "(auto)" : "");
        check("subtitle files: WebVTT before SubRip, a language without a file the player reads left out, no live chat; of the automatic captions only the video's own language",
              subs == QStringList({"en:vtt:en.vtt", "pt-BR:vtt:pt.vtt", "en:vtt:auto.vtt(auto)"}), subs.join(", "));
        check("the site's name for a subtitle file, without \"(Original)\"", v.subtitles.value(0).label == "English" && v.subtitles.value(2).label == "English" && v.subtitles.value(1).label.isEmpty(),
              QString("%1 | %2").arg(v.subtitles.value(0).label, v.subtitles.value(2).label));
        check("chapters", v.chapters.size() == 2 && v.chapters[1].startNs == 61500000000LL && v.chapters[1].title == "Main", QString("%1 chapters").arg(v.chapters.size()));

        // ... automatic captions when the page does not say which language the video is in
        root.remove("language");
        QJsonObject autos = root["automatic_captions"].toObject();
        autos.remove("en-orig");
        root["automatic_captions"] = autos;
        const OnlineResult r2 = R::parse(QJsonDocument(root).toJson(), QByteArray(), 0, {});
        int automatic = 0;
        for (const OnlineSubtitle& s : r2.video.subtitles) automatic += s.automatic;
        check("the video's language unknown: no machine translations on offer", r2.ok && automatic == 0, QString("%1 automatic").arg(automatic));
    }
    // ---- one file with both; a manifest; a live stream
    {
        QJsonObject one = format("https://cdn.example/v.mp4", "avc1.64001e", "mp4a.40.2", 360);
        one["title"] = "One file";
        one["original_url"] = "https://example.com/v/1";
        const OnlineResult r = R::parse(QJsonDocument(one).toJson(), QByteArray(), 0, {});
        check("one file with the picture and the sound", r.ok && r.video.streams.size() == 1 && !r.video.streams[0].audioOnly && r.video.what == QString::fromUtf8("360p60 · H.264 + AAC") && r.video.page == "https://example.com/v/1",
              r.video.what + " | " + r.video.page);
        QJsonObject hls = format("https://cdn.example/v/index.m3u8", "avc1.64001e", "mp4a.40.2", 720, "m3u8_native");
        hls["manifest_url"] = "https://cdn.example/v/master.m3u8";
        hls["is_live"] = true;
        const OnlineResult h = R::parse(QJsonDocument(hls).toJson(), QByteArray(), 0, {});
        check("an HLS stream: the manifest is what is played", h.ok && h.video.streams.isEmpty() && h.video.manifest == "https://cdn.example/v/master.m3u8" && h.video.live, h.video.manifest);
        hls.remove("manifest_url");
        check("... or the stream's own playlist, when no manifest is named", R::parse(QJsonDocument(hls).toJson(), QByteArray(), 0, {}).video.manifest == "https://cdn.example/v/index.m3u8", "index.m3u8");
        QJsonObject dash{{"title", "Segments"}, {"requested_formats", QJsonArray{format("https://cdn/v", "avc1", "none", 1080, "http_dash_segments"), format("https://cdn/a", "none", "mp4a", 0, "http_dash_segments")}}};
        const OnlineResult d0 = R::parse(QJsonDocument(dash).toJson(), QByteArray(), 0, {});
        check("a video in pieces with no manifest named: said, not tried", !d0.ok && d0.error.contains("http_dash_segments"), d0.error);
        QJsonArray fs = dash["requested_formats"].toArray();
        QJsonObject f0 = fs[0].toObject();
        f0["manifest_url"] = "https://cdn/manifest.mpd";
        fs[0] = f0;
        dash["requested_formats"] = fs;
        const OnlineResult d1 = R::parse(QJsonDocument(dash).toJson(), QByteArray(), 0, {});
        check("... with its manifest: that is played", d1.ok && d1.video.manifest == "https://cdn/manifest.mpd" && d1.video.streams.isEmpty(), d1.video.manifest);
    }
    // ---- a playlist
    {
        QJsonObject pl{{"_type", "playlist"}, {"title", "Favourites"},
                       {"entries", QJsonArray{QJsonObject{{"url", "https://example.com/watch?v=1"}, {"title", "One"}, {"duration", 60}},
                                              QJsonObject{{"webpage_url", "https://example.com/watch?v=2"}, {"title", "Two"}},
                                              QJsonObject{{"url", "abcdef"}, {"title", "An id, not an address"}}}}};
        const OnlineResult r = R::parse(QJsonDocument(pl).toJson(), QByteArray(), 0, {});
        check("a playlist: its videos' pages and titles", r.ok && r.playlist && r.playlistTitle == "Favourites" && r.entries.size() == 2 && r.entries[1].url == "https://example.com/watch?v=2" && r.entries[0].title == "One",
              QString("%1 entries").arg(r.entries.size()));
        pl["entries"] = QJsonArray{};
        check("an empty playlist is an error", !R::parse(QJsonDocument(pl).toJson(), QByteArray(), 0, {}).ok, "nothing to play");
    }
    // ---- failures
    {
        const OnlineResult r = R::parse("null\n", "WARNING: [generic] Falling back on generic information extractor\nERROR: Unsupported URL: https://example.com/\n", 1, {});
        check("yt-dlp's own error is what is said", !r.ok && r.error == "Unsupported URL: https://example.com/", r.error);
        const OnlineResult r2 = R::parse(QByteArray(), "Traceback (most recent call last):\n  something\n", 1, {});
        check("no answer and no error line: its exit code", !r2.ok && r2.error.contains("1"), r2.error);
        const OnlineResult r3 = R::parse("{\"title\": \"Nothing chosen\"}", QByteArray(), 0, {});
        check("an answer without a stream", !r3.ok && !r3.error.isEmpty(), r3.error);
        const OnlineResult r4 = R::parse("{ this is not JSON", QByteArray(), 0, {});
        check("an answer that is not JSON", !r4.ok && !r4.error.isEmpty(), r4.error);
        const OnlineResult r5 = R::parse("null", "WARNING: [youtube] No supported JavaScript runtime could be found. YouTube extraction without a JS runtime has been deprecated\nERROR: [youtube] x: Requested format is not available\n", 1, {});
        check("the missing JavaScript runtime is noted beside the error", !r5.ok && r5.notes.size() == 1 && r5.error.contains("Requested format"), r5.notes.value(0).left(60));
    }

    // ---- automatic captions, as YouTube writes them
    {
        const QByteArray rolling =
            "WEBVTT\nKind: captions\nLanguage: en\n\n"
            "00:00:01.000 --> 00:00:03.490 align:start position:0%\n \nhello<00:00:01.400><c> there</c><00:00:02.000><c> world</c>\n\n"
            "00:00:03.490 --> 00:00:03.500 align:start position:0%\nhello there world\n \n\n"
            "00:00:03.500 --> 00:00:06.000 align:start position:0%\nhello there world\nsecond<00:00:04.000><c> line</c>\n\n"
            "00:00:06.000 --> 00:00:06.010 align:start position:0%\nsecond line\n \n\n"
            "01:00:00.000 --> 01:00:02.000 align:start position:0%\nsecond line\nan<01:00:00.500><c> hour</c><01:00:01.000><c> in</c>\n\n";
        const QByteArray tidy = R::tidyCaptions(rolling);
        const QByteArray want = "WEBVTT\n\n00:00:01.000 --> 00:00:03.490\nhello there world\n\n00:00:03.500 --> 00:00:06.000\nsecond line\n\n01:00:00.000 --> 01:00:02.000\nan hour in\n\n";
        check("each line once, whole, for as long as it is spoken", tidy == want, QString::fromUtf8(tidy).replace('\n', '|'));
        const QByteArray plain = "WEBVTT\n\n00:01.000 --> 00:02.000\nA line as people write it\n\n";
        check("subtitles that are not of that kind are left as they are", R::tidyCaptions(plain) == plain, "unchanged");
        check("an empty file stays empty of lines", R::tidyCaptions("WEBVTT\n\n<c></c>\n") == "WEBVTT\n\n", "no lines");
    }

    // ---- what a server calls media; how old a yt-dlp is
    check("content types that are media", R::isMediaType("video/mp4") && R::isMediaType("Audio/MPEG; charset=x") && R::isMediaType("application/vnd.apple.mpegurl") && R::isMediaType("application/dash+xml") && R::isMediaType("application/octet-stream"), "video, audio, manifests, plain bytes");
    check("content types that are not", !R::isMediaType("text/html; charset=utf-8") && !R::isMediaType("application/json") && !R::isMediaType(""), "pages, data, nothing");
    check("a yt-dlp version is a date", R::versionAgeDays("2026.08.19", QDate(2026, 10, 6)) == 48 && R::versionAgeDays("2026.10.06.232815", QDate(2026, 10, 6)) == 0, "48 days, 0 days");
    check("... or not", R::versionAgeDays("?", QDate(2026, 10, 6)) < 0 && R::versionAgeDays("", QDate(2026, 10, 6)) < 0 && R::versionAgeDays("1.2.3", QDate(2026, 10, 6)) < 0, "-1");

    // ---- the archive Deno comes in
    {
        QByteArray program = "#!/bin/sh\necho deno\n";
        for (int i = 0; i < 2000; ++i) program += "some bytes that repeat themselves; ";
        check("a deflated file out of a zip archive", R::unzipOne(zipOf("deno", program, true), "deno") == program, QString("%1 bytes").arg(program.size()));
        check("a stored one", R::unzipOne(zipOf("deno.exe", program, false), "deno.exe") == program, "as it was");
        check("in a folder inside the archive", R::unzipOne(zipOf("bin/deno", program, true), "deno") == program, "bin/deno");
        check("another name: nothing", R::unzipOne(zipOf("other", program, true), "deno").isEmpty(), "empty");
        QByteArray broken = zipOf("deno", program, true);
        broken[60] = char(broken[60] ^ 0x55);
        check("a damaged archive: nothing (its checksum does not match)", R::unzipOne(broken, "deno").isEmpty(), "empty");
        check("cut short, or not an archive at all: nothing", R::unzipOne(zipOf("deno", program, true).left(200), "deno").isEmpty() && R::unzipOne("PK\x05\x06 rubbish", "deno").isEmpty() && R::unzipOne(QByteArray(), "deno").isEmpty(), "empty");
        QByteArray lying = zipOf("deno", program, true);
        const int end = lying.lastIndexOf(QByteArray("PK\x05\x06", 4));
        lying[end + 16] = char(0xf0); lying[end + 17] = char(0xff); lying[end + 18] = char(0xff); lying[end + 19] = char(0x7f);
        check("an archive whose list points outside it: nothing", R::unzipOne(lying, "deno").isEmpty(), "empty");
    }
    std::printf("%s\n", g_fail ? "SOME CHECKS FAILED" : "all checks passed");
    return g_fail ? 1 : 0;
}
