// Lyrics files from any computer: UTF-8, UTF-16, old Windows text, and Word documents.
#include <catch2/catch_test_macros.hpp>

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
