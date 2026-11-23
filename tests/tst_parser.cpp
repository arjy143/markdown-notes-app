#include "TestUtil.h"
#include "core/parse/MarkdownDoc.h"
#include "core/parse/NoteMetadata.h"

#include <md4c.h>

using namespace mn;
using namespace Qt::StringLiterals;

namespace {

QByteArray load(const QString& name)
{
    return test::readBytes(test::fixture(u"parse/"_s + name));
}

NoteMetadata parseFixture(const QString& name)
{
    const QByteArray raw = load(name);
    return parseNote(raw);
}

QStringList linkpaths(const NoteMetadata& m)
{
    QStringList out;
    for (const LinkRef& l : m.links)
        out.append(l.linkpath);
    return out;
}

QStringList tagList(const NoteMetadata& m)
{
    QStringList out;
    for (const TagRef& t : m.tags)
        out.append(t.tag);
    return out;
}

const BlockRef* block(const NoteMetadata& m, const QString& id)
{
    for (const BlockRef& b : m.blocks) {
        if (b.id == id) return &b;
    }
    return nullptr;
}

} // namespace

class TestParser : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void wikilinks()
    {
        const NoteMetadata m = parseFixture(u"wikilinks.md"_s);
        QCOMPARE(m.links.size(), std::size_t(8));
        QCOMPARE(m.links[0].linkpath, u"Note"_s);
        QVERIFY(m.links[0].alias.isEmpty());
        QCOMPARE(m.links[1].alias, u"Shown text"_s);
        QCOMPARE(m.links[2].subpath, u"Heading"_s);
        QCOMPARE(m.links[3].subpath, u"H1#H2"_s);
        QCOMPARE(m.links[4].subpath, u"^block-id"_s);
        QVERIFY(m.links[5].linkpath.isEmpty());
        QCOMPARE(m.links[5].subpath, u"Local heading"_s);
        QCOMPARE(m.links[6].linkpath, u"folder/Note.md"_s);
        QCOMPARE(m.links[7].linkpath, u"Note with spaces"_s);
        for (const LinkRef& l : m.links) {
            QCOMPARE(l.kind, LinkRef::Kind::Wiki);
            QVERIFY(!l.embed);
        }
        QCOMPARE(m.links[0].line, 2);
        QCOMPARE(m.links[7].line, 5);
    }

    void wikilinkTargetRangesPointAtSource()
    {
        const QByteArray raw = load(u"wikilinks.md"_s);
        const NoteMetadata m = parseNote(raw);
        for (const LinkRef& l : m.links) {
            QVERIFY(l.targetStart >= 0);
            QCOMPARE(QString::fromUtf8(raw.mid(l.targetStart, l.targetEnd - l.targetStart)), l.linkpath);
        }
    }

    void embeds()
    {
        const NoteMetadata m = parseFixture(u"embeds.md"_s);
        QCOMPARE(m.links.size(), std::size_t(7));
        for (int i = 0; i < 6; ++i)
            QVERIFY2(m.links[std::size_t(i)].embed, qPrintable(m.links[std::size_t(i)].linkpath));
        QCOMPARE(m.links[1].alias, u"300"_s);
        QCOMPARE(m.links[2].alias, u"300x200"_s);
        QCOMPARE(m.links[4].subpath, u"Heading"_s);
        QCOMPARE(m.links[5].subpath, u"^id"_s);
        QCOMPARE(m.links[6].linkpath, u"NotAnEmbed"_s);
        QVERIFY(!m.links[6].embed);
    }

    void markdownLinks()
    {
        const QByteArray raw = load(u"markdown-links.md"_s);
        const NoteMetadata m = parseNote(raw);
        QCOMPARE(linkpaths(m), (QStringList{u"Note.md"_s, u"Note with spaces.md"_s, u"Note with spaces.md"_s,
                                            u"img.png"_s, u"img.png"_s, u"Note.md"_s, u"Other.md"_s}));
        QCOMPARE(m.externalLinks.size(), std::size_t(2));
        QVERIFY(m.links[1].angleBrackets);
        QVERIFY(m.links[2].percentEncoded);
        QCOMPARE(
            QString::fromUtf8(raw.mid(m.links[2].targetStart, m.links[2].targetEnd - m.links[2].targetStart)),
            u"Note%20with%20spaces.md"_s);
        QVERIFY(m.links[3].embed);
        QCOMPARE(m.links[4].display, u"alt|300"_s);
        QCOMPARE(m.links[5].subpath, u"Heading"_s);
        // Reference-style link: the range points at the definition.
        QCOMPARE(
            QString::fromUtf8(raw.mid(m.links[6].targetStart, m.links[6].targetEnd - m.links[6].targetStart)),
            u"Other.md"_s);
    }

    void codeIsNeverInterpreted()
    {
        const NoteMetadata m = parseFixture(u"code-exclusion.md"_s);
        QCOMPARE(linkpaths(m), QStringList{u"RealLink"_s});
        QCOMPARE(tagList(m), QStringList{u"realtag"_s});
    }

    void tags()
    {
        const NoteMetadata m = parseFixture(u"tags.md"_s);
        QCOMPARE(tagList(m), (QStringList{u"tag"_s, u"nested/tag/deep"_s, u"tag-with-dash"_s,
                                          u"tag_underscore"_s, u"paren"_s, u"bracket"_s, u"sentence"_s,
                                          u"ünïcödé"_s, u"日本語"_s, u"fire"_s, u"headingtag"_s}));
        QCOMPARE(m.tags[0].line, 0);
        QCOMPARE(m.tags[1].line, 1);
    }

    void commentsHideLinksTagsAndHeadings()
    {
        const NoteMetadata m = parseFixture(u"comments.md"_s);
        QCOMPARE(linkpaths(m), (QStringList{u"Shown"_s, u"AfterCode"_s}));
        QCOMPARE(tagList(m), QStringList{u"showntag"_s});
        QVERIFY(m.headings.empty());
    }

    void blockIds()
    {
        const NoteMetadata m = parseFixture(u"block-ids.md"_s);
        const BlockRef* para = block(m, u"para-id"_s);
        QVERIFY(para);
        QCOMPARE(para->startLine, 0);
        QCOMPARE(para->endLine, 0);
        const BlockRef* li = block(m, u"li-id"_s);
        QVERIFY(li);
        QCOMPARE(li->startLine, 3);
        const BlockRef* list = block(m, u"list-id"_s);
        QVERIFY(list);
        QCOMPARE(list->startLine, 5);
        QCOMPARE(list->endLine, 8);
        const BlockRef* table = block(m, u"table-id"_s);
        QVERIFY(table);
        QCOMPARE(table->startLine, 10);
        QCOMPARE(table->endLine, 12);
        QCOMPARE(m.blocks.size(), std::size_t(4));
    }

    void headings()
    {
        const NoteMetadata m = parseFixture(u"headings.md"_s);
        QCOMPARE(m.headings.size(), std::size_t(4));
        QCOMPARE(m.headings[0].text, u"Top"_s);
        QCOMPARE(m.headings[1].text, u"Bold heading"_s);
        QCOMPARE(m.headings[1].level, 2);
        QCOMPARE(m.headings[2].text, u"Heading with code"_s);
        QCOMPARE(m.headings[3].text, u"Setext heading"_s);
        QCOMPARE(m.headings[3].line, 6);
        QCOMPARE(tagList(m), QStringList{u"NotAHeading"_s});
    }

    void mathIsNotParsedForLinksOrTags()
    {
        const QByteArray raw = load(u"math.md"_s);
        const md::Document doc = md::parse(raw);
        int mathSpans = 0;
        for (const md::Event& e : doc.events) {
            if (e.type == md::Ev::EnterSpan &&
                (e.kind == MD_SPAN_LATEXMATH || e.kind == MD_SPAN_LATEXMATH_DISPLAY))
                ++mathSpans;
        }
        QCOMPARE(mathSpans, 2);
    }

    void headingSubpathMatching()
    {
        const NoteMetadata m = parseFixture(u"heading-links.md"_s);
        QStringList texts;
        for (const Heading& h : m.headings)
            texts.append(h.text);
        QCOMPARE(findHeading(texts, u"intro"), 0);
        QCOMPARE(findHeading(texts, u"Setup: step 1"), 1);
        QCOMPARE(findHeading(texts, u"Setup step 1"),
                 1);                                    // punctuation dropped, as Obsidian writes such links
        QCOMPARE(findHeading(texts, u"Notes"), 2);      // duplicates: the first one
        QCOMPARE(findHeading(texts, u"Notes#Deep"), 4); // nested: the last segment decides
        QCOMPARE(findHeading(texts, u"Missing"), -1);
    }

    void tableWikilinkWithEscapedPipe()
    {
        const NoteMetadata m = parseFixture(u"table-wikilink.md"_s);
        QCOMPARE(m.links.size(), std::size_t(1));
        QCOMPARE(m.links[0].linkpath, u"Target"_s);
        QCOMPARE(m.links[0].alias, u"Alias"_s);
    }

    void longWikilinkTargets()
    {
        const NoteMetadata m = parseFixture(u"long-wikilink.md"_s);
        QCOMPARE(m.links.size(), std::size_t(1));
        QVERIFY(m.links[0].linkpath.size() > 100);
    }

    void footnoteDefinitionsAreParsed()
    {
        const NoteMetadata m = parseFixture(u"footnotes.md"_s);
        QCOMPARE(linkpaths(m), QStringList{u"Footnote target"_s});
        QCOMPARE(m.links[0].line, 2);
    }

    void frontmatterOffsetsKeepLineNumbers()
    {
        const QByteArray raw = "---\ntags: [a]\n---\nLine [[X]] #b\n";
        const NoteMetadata m = parseNote(raw);
        QCOMPARE(m.links.size(), std::size_t(1));
        QCOMPARE(m.links[0].line, 3);
        QCOMPARE(tagList(m), (QStringList{u"a"_s, u"b"_s}));
    }

    void crlfLineNumbers()
    {
        const QByteArray raw = "one\r\ntwo [[X]]\r\nthree #t\r\n";
        const NoteMetadata m = parseNote(raw);
        QCOMPARE(m.links[0].line, 1);
        QCOMPARE(m.tags[0].line, 2);
        QCOMPARE(raw.mid(m.links[0].targetStart, 1), QByteArray("X"));
    }

    void tagCodePoints()
    {
        QVERIFY(md::isTagCodePoint(U'a'));
        QVERIFY(md::isTagCodePoint(U'/'));
        QVERIFY(md::isTagCodePoint(U'é'));
        QVERIFY(!md::isTagCodePoint(U'.'));
        QVERIFY(!md::isTagCodePoint(U' '));
        QVERIFY(!md::isTagCodePoint(U'#'));
    }

    void helpers()
    {
        QVERIFY(hasUrlScheme(u"https://x"));
        QVERIFY(hasUrlScheme(u"mailto:a@b"));
        QVERIFY(!hasUrlScheme(u"Note.md"));
        QVERIFY(!hasUrlScheme(u"C:/x"));
        QCOMPARE(percentDecode(u"a%20b%C3%A9"), u"a bé"_s);
        QCOMPARE(percentDecode(u"bad%FF"), u"bad%FF"_s);
        QCOMPARE(headingKey(u"  Hello   World "), u"hello world"_s);
    }
};

QTEST_GUILESS_MAIN(TestParser)
#include "tst_parser.moc"
