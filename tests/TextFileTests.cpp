// Lyrics files from any computer: UTF-8, UTF-16, old Windows text, and Word documents.
#include <catch2/catch_test_macros.hpp>

#include "cc/Lyrics.h"
#include "cc/TextFile.h"

TEST_CASE ("Text files in every encoding come out the same")
{
    const std::string want = "Don\xE2\x80\x99t stop\nnow\n";
    CHECK (cc::textFromBytes ("Don\xE2\x80\x99t stop\r\nnow\r\n") == want);               // UTF-8, Windows line ends
    CHECK (cc::textFromBytes ("\xEF\xBB\xBF" "Don\xE2\x80\x99t stop\nnow\n") == want);     // with the marker
    CHECK (cc::textFromBytes ("Don\x92t stop\rnow\r") == want);                          // Windows-1252, old Mac line ends
    const std::string utf16le ("\xFF\xFE" "D\0o\0n\0\x19\x20t\0 \0s\0t\0o\0p\0\r\0\n\0n\0o\0w\0\n\0", 34);
    CHECK (cc::textFromBytes (utf16le) == want);
    const std::string utf16be ("\xFE\xFF" "\0D\0o\0n\x20\x19\0t\0 \0s\0t\0o\0p\0\n\0n\0o\0w\0\n", 32);
    CHECK (cc::textFromBytes (utf16be) == want);
    CHECK (cc::textFromBytes ("caf\xE9") == "caf\xC3\xA9");
    CHECK (cc::isUtf8 ("caf\xC3\xA9"));
    CHECK_FALSE (cc::isUtf8 ("caf\xE9"));
    CHECK_FALSE (cc::isUtf8 ("cut \xE2\x80"));
}

TEST_CASE ("A Word document's text, a line per paragraph")
{
    const char* xml =
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
        "<w:document xmlns:w=\"http://schemas.openxmlformats.org/wordprocessingml/2006/main\"><w:body>"
        "<w:p><w:pPr><w:tabs><w:tab w:val=\"left\" w:pos=\"720\"/></w:tabs></w:pPr>"
        "<w:r><w:t>[Verse 1]</w:t></w:r></w:p>"
        "<w:p><w:r><w:t xml:space=\"preserve\">I was lost </w:t></w:r><w:r><w:rPr><w:b/></w:rPr><w:t>in the dark</w:t></w:r></w:p>"
        "<w:p/>"
        "<w:p><w:r><w:t>Don&apos;t &amp; won&#8217;t</w:t><w:br/><w:t>next</w:t><w:tab/><w:t>line</w:t></w:r></w:p>"
        "<w:tbl><w:tr><w:tc><w:p><w:r><w:t>in a table</w:t></w:r></w:p></w:tc></w:tr></w:tbl>"
        "</w:body></w:document>";
    CHECK (cc::textFromWordXml (xml) == "[Verse 1]\nI was lost in the dark\n\nDon't & won\xE2\x80\x99t\nnext\tline\nin a table\n");
}

namespace
{
    // What Song Rider Pro's Export button writes (newer layout): a section is a table with an underlined
    // heading, lyric lines in dark text, chords in red (over a lyric, as a two-row table cut at each chord).
    const char* songRiderPage =
        "<!doctype html>\n<html>\n<head>\n<meta charset=\"utf-8\">\n<title>Brand New Start - Full Preview</title>\n"
        "<style>.srp-export-title{font-size:30px}</style>\n</head>\n<body>\n"
        "<div class=\"srp-export-header\"><div class=\"srp-export-title\">Brand New Start</div><div class=\"srp-export-meta\">BPM: 92   Key: G</div></div>\n"
        "<table role=\"presentation\" border=\"0\" cellspacing=\"0\" cellpadding=\"0\" width=\"100%\" style=\"width:100%;border:0;\"><tbody><tr><td style=\"border:0;padding:0;vertical-align:top;\">"
        "<div style=\"font-family:Arial,Helvetica,sans-serif;font-size:18px;line-height:1.2;font-weight:900;text-decoration:underline;margin:18px 0 8px 0;\">Verse 1</div>"
        "<div style=\"font-family:'Courier New',Courier,monospace;font-size:16px;line-height:1.38;font-weight:900;color:#c00000;white-space:nowrap;\">G&nbsp;&nbsp;&nbsp;D</div>\n"
        "<table role=\"presentation\" border=\"0\" cellspacing=\"0\" cellpadding=\"0\" style=\"border-collapse:collapse;\"><tbody>"
        "<tr><td nowrap style=\"padding:0 8px 0 0;font-weight:900;color:#c00000;\">G</td><td nowrap style=\"padding:0 8px 0 0;font-weight:900;color:#c00000;\">Em7</td></tr>"
        "<tr><td nowrap style=\"padding:0;font-weight:700;color:#111;\">I&nbsp;was&nbsp;lost&nbsp;</td><td nowrap style=\"padding:0;font-weight:700;color:#111;\">in&nbsp;the&nbsp;dark</td></tr>"
        "</tbody></table>\n"
        "<div style=\"height:.72em;line-height:.72em;\">&nbsp;</div>\n"
        "<div style=\"font-family:'Courier New',Courier,monospace;font-size:16px;line-height:1.38;font-weight:700;color:#111;white-space:pre-wrap;\">but&nbsp;You found me &amp; gave me life</div>"
        "</td></tr></tbody></table>\n"
        "<table role=\"presentation\" width=\"100%\"><tbody><tr><td>"
        "<div style=\"font-family:Arial,Helvetica,sans-serif;font-size:18px;font-weight:900;text-decoration:underline;\">Chorus</div>"
        "<div style=\"font-weight:700;color:#111;white-space:pre-wrap;\">I&#39;m alive, I&#39;m alive</div>"
        "</td></tr></tbody></table>\n</body>\n</html>";

    // The older layout: bold headings, each section's lines in one block with the chord rows mixed in.
    const char* oldSongRiderPage =
        "<!doctype html><html><head><meta charset=\"utf-8\"><title>Song Rider Pro - Full Preview</title></head>"
        "<body style=\"margin:24px;color:#111;\">"
        "<div style=\"display:flex;align-items:baseline;gap:14px;\"><div style=\"font-family:Arial,Helvetica,sans-serif;font-weight:900;font-size:34px;line-height:1.1;\">Brand New Start</div>"
        "<div style=\"font-family:'Courier New',Courier,monospace;font-weight:400;font-size:16px;\">BPM: 92</div></div>\n"
        "<div style=\"font-family:Arial,Helvetica,sans-serif;font-weight:900;margin:20px 0 8px;letter-spacing:.3px;\">VERSE 1</div>\n"
        "<pre style=\"margin:0 0 10px 0;font-family:'Courier New',Courier,monospace;color:#111;white-space:pre;\">G        D/F#\nI was lost in the dark\n\nbut You found me</pre>\n"
        "</body></html>";
}

TEST_CASE ("A song page from Song Rider Pro becomes lyrics with sections")
{
    const auto text = cc::textFromSongRiderPage (songRiderPage);
    CHECK (text == "[Verse 1]\nI was lost in the dark\nbut You found me & gave me life\n\n[Chorus]\nI'm alive, I'm alive\n");

    const auto old = cc::textFromSongRiderPage (oldSongRiderPage);
    CHECK (old == "[VERSE 1]\nG        D/F#\nI was lost in the dark\n\nbut You found me\n");

    // the chord row is left out when the lyrics are read
    const auto lyrics = cc::Lyrics::parse (old);
    REQUIRE (lyrics.lines.size() == 2);
    CHECK (lyrics.sections[0].name == "Verse 1");
    CHECK (lyrics.lines[0].text == "I was lost in the dark");
}
