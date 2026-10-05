// ASS / SSA subtitle scripts read from files of their own (src/playback/AssScript.cpp).
#include "playback/AssScript.h"
#include <QString>
#include <cstdio>

static int g_fail = 0;
static void check(const char* name, bool ok, const QString& detail)
{
    std::printf("%s  %s: %s\n", ok ? "PASS" : "FAIL", name, detail.toUtf8().constData());
    if (!ok) ++g_fail;
}
static const qint64 kSec = 1000000000LL;

int main()
{
    const QByteArray plain =
        "[Script Info]\nTitle: t\nScriptType: v4.00+\nPlayResX: 640\n\n[V4+ Styles]\n"
        "Format: Name, Fontname, Fontsize\nStyle: Default,Arial,20\nStyle: Sign,Arial,30\n\n[Events]\n"
        "Format: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n"
        "Dialogue: 0,0:00:02.00,0:00:06.50,Default,Anna,0,0,0,,Hello, world, again\n"
        "Comment: 0,0:00:03.00,0:00:04.00,Default,,0,0,0,,not shown\n"
        "Dialogue: 3,1:02:03.45,1:02:04.00,Sign,,10,20,30,fx,{\\an8}Up top\n"
        "\n[Fonts]\nfontname: x.ttf\nDialogue: this is font data, not a line\n";
    {
        const AssScript s = parseAssScript(plain);
        check("a plain script is recognised, as ASS", s.ok && !s.ssa, QString("ok %1 ssa %2").arg(s.ok).arg(s.ssa));
        check("its two lines are found (not the comment, not what follows the lines)", s.lines.size() == 2, QString("%1 lines").arg(s.lines.size()));
        if (s.lines.size() == 2) {
            check("times: 2 s to 6.5 s", s.lines[0].startNs == 2 * kSec && s.lines[0].endNs == 6 * kSec + kSec / 2,
                  QString("%1 - %2 ns").arg(s.lines[0].startNs).arg(s.lines[0].endNs));
            check("times beyond an hour, hundredths", s.lines[1].startNs == (3723 * kSec + 450000000LL), QString::number(s.lines[1].startNs));
            check("a line as Matroska stores it; commas in the text stay", s.lines[0].chunk == "0,0,Default,Anna,0,0,0,,Hello, world, again", QString::fromUtf8(s.lines[0].chunk));
            check("layer, margins, effect and override tags are kept; the lines are counted", s.lines[1].chunk == "1,3,Sign,,10,20,30,fx,{\\an8}Up top",
                  QString::fromUtf8(s.lines[1].chunk));
        }
        check("the head holds the styles and ends with the lines' format", s.head.contains("Style: Sign,Arial,30") && s.head.contains("[Script Info]") &&
              s.head.endsWith("[Events]\nFormat: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n") && !s.head.contains("Dialogue"),
              QString("%1 bytes").arg(s.head.size()));
    }
    {
        QByteArray crlf = plain;
        crlf.replace("\n", "\r\n");
        const AssScript s = parseAssScript(QByteArray("\xef\xbb\xbf") + crlf);
        check("Windows line ends and a UTF-8 mark at the start", s.ok && s.lines.size() == 2 && s.lines[0].chunk == "0,0,Default,Anna,0,0,0,,Hello, world, again" &&
              s.head.startsWith("[Script Info]"), QString("%1 lines").arg(s.lines.size()));
    }
    {
        const QString text = QString::fromUtf8(plain).replace("Hello", QString::fromUtf8("Gr\xc3\xbc\xc3\x9f" "e \xe6\x97\xa5\xe6\x9c\xac"));
        QByteArray utf16("\xff\xfe", 2);
        utf16.append(reinterpret_cast<const char*>(text.utf16()), text.size() * 2);
        const AssScript s = parseAssScript(utf16);
        check("a UTF-16 script comes out as UTF-8", s.ok && s.lines.size() == 2 && s.lines[0].chunk.contains("Gr\xc3\xbc\xc3\x9f" "e \xe6\x97\xa5\xe6\x9c\xac, world"),
              s.lines.empty() ? QString("no lines") : QString::fromUtf8(s.lines[0].chunk));
    }
    {
        QByteArray latin = plain;
        latin.replace("Hello", "Gr\xfc\xdf" "e");
        const AssScript s = parseAssScript(latin);
        check("a script that is not UTF-8 is taken letter for letter", s.ok && s.lines.size() == 2 && s.lines[0].chunk.contains("Gr\xc3\xbc\xc3\x9f" "e, world"),
              s.lines.empty() ? QString("no lines") : QString::fromUtf8(s.lines[0].chunk));
    }
    {
        const QByteArray ssa =
            "[Script Info]\nScriptType: v4.00\n\n[V4 Styles]\nFormat: Name, Fontname\nStyle: Default,Arial\n\n[Events]\n"
            "Format: Marked, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n"
            "Dialogue: Marked=0,0:00:01.00,0:00:02.00,Default,NTP,0000,0000,0000,!Effect,Old style\n";
        const AssScript s = parseAssScript(ssa);
        check("an SSA script (the older kind)", s.ok && s.ssa && s.lines.size() == 1 && s.lines[0].chunk == "0,0,Default,NTP,0000,0000,0000,!Effect,Old style",
              s.lines.empty() ? QString("no lines") : QString::fromUtf8(s.lines[0].chunk));
    }
    {
        const QByteArray odd =
            "[Script Info]\n[V4+ Styles]\nStyle: Default,Arial,20\n[Events]\n"
            "Format: Start, End, Text\n"
            "Dialogue: 0:00:05.00,0:00:07.00,Only, text\n"
            "Dialogue: 0:00:09.00,0:00:08.00,ends before it begins\n"
            "Dialogue: nonsense\n"
            "Dialogue: 0:00:10.00,0:00:11.00,\n";
        const AssScript s = parseAssScript(odd);
        check("fields in an order of the script's own; lines without sense are left out", s.ok && s.lines.size() == 2 && s.lines[0].chunk == "0,0,Default,,0,0,0,,Only, text" &&
              s.lines[1].chunk == "1,0,Default,,0,0,0,," && s.head.endsWith("Format: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n"),
              QString("%1 lines").arg(s.lines.size()));
    }
    {
        check("a SubRip file is not taken for a script", !parseAssScript("1\n00:00:01,000 --> 00:00:02,000\n[Script Info] is only mentioned\n").lines.size() &&
              !parseAssScript("1\n00:00:01,000 --> 00:00:02,000\nHello\n").ok, "left to GStreamer");
        check("an empty file, and a head without lines", !parseAssScript(QByteArray()).ok && !parseAssScript("[Script Info]\nTitle: x\n").ok, "not scripts");
        const AssScript s = parseAssScript("[Script Info]\n[Events]\nFormat: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n");
        check("a script without any line is a script with no lines", s.ok && s.lines.empty(), QString("ok %1").arg(s.ok));
    }
    std::printf("%s\n", g_fail ? "SOME CHECKS FAILED" : "all checks passed");
    return g_fail ? 1 : 0;
}
