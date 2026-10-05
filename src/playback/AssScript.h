#pragma once
// An ASS / SSA subtitle script read from a file of its own (2.16).
//
// GStreamer reads these scripts only out of a video file (Matroska), where the head of the script (its
// styles) is stored once and every line by itself, as "ReadOrder,Layer,Style,Name,MarginL,MarginR,MarginV,
// Effect,Text" with its times beside it. A script file is brought into that form here, for the player's
// own store of subtitle lines (SubtitleFeed).
#include <QByteArray>
#include <vector>

struct AssLine {
    qint64 startNs = 0, endNs = 0;
    QByteArray chunk;   // the line as Matroska stores it (UTF-8)
};

struct AssScript {
    bool ok = false;    // it is such a script (false: something else; left to GStreamer's readers of subtitle files)
    bool ssa = false;   // the older SSA ("[V4 Styles]") rather than ASS ("[V4+ Styles]")
    QByteArray head;    // everything before the lines, ending with "[Events]" and its Format line (UTF-8)
    std::vector<AssLine> lines;   // in the file's order (ReadOrder counts them)
};

AssScript parseAssScript(const QByteArray& fileContents);
