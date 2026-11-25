#include "TestUtil.h"
#include "core/ai/FrontmatterMerge.h"
#include "core/parse/Frontmatter.h"

using namespace mn;
using namespace Qt::StringLiterals;

namespace {
Frontmatter parseFixture(const QString& name)
{
    return parseFrontmatter(test::readBytes(test::fixture(u"frontmatter/"_s + name)));
}
} // namespace

class TestFrontmatter : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void blockListTagsAndFlowAliases()
    {
        const Frontmatter fm = parseFixture(u"list-tags.md"_s);
        QVERIFY(fm.present);
        QVERIFY(fm.valid);
        QCOMPARE(fm.tags, (QStringList{u"alpha"_s, u"beta"_s}));
        QCOMPARE(fm.aliases, (QStringList{u"First alias"_s, u"Second alias"_s}));
        QCOMPARE(fm.cssclasses, QStringList{u"wide"_s});
        QCOMPARE(fm.closingLine, 6);
    }

    void stringTagsAreSplitOnCommasAndSpaces()
    {
        const Frontmatter fm = parseFixture(u"string-tags.md"_s);
        QVERIFY(fm.valid);
        // " #four" is a YAML comment in a plain scalar.
        QCOMPARE(fm.tags, (QStringList{u"one"_s, u"two"_s, u"three"_s}));
        QCOMPARE(fm.aliases, QStringList{u"Only alias"_s});
    }

    void legacyTagKeys()
    {
        const Frontmatter fm = parseFixture(u"legacy-tag-key.md"_s);
        QCOMPARE(fm.tags, (QStringList{u"legacy"_s, u"Upper"_s}));
    }

    void invalidYamlIsReportedNotFixed()
    {
        const Frontmatter fm = parseFixture(u"invalid.md"_s);
        QVERIFY(fm.present);
        QVERIFY(!fm.valid);
        QVERIFY(!fm.error.isEmpty());
        QVERIFY(fm.tags.isEmpty());
    }

    void tagsAreRemovedInTheExistingStyle_data()
    {
        QTest::addColumn<QString>("before");
        QTest::addColumn<QString>("after");
        QTest::newRow("block list") << u"---\ntags:\n  - a\n  - Old\n  - b\nx: 1\n---\nBody\n"_s
                                    << u"---\ntags:\n  - a\n  - b\nx: 1\n---\nBody\n"_s;
        QTest::newRow("block list, quoted and #")
            << u"---\ntags:\n- \"#old\"\n- a\n---\n"_s << u"---\ntags:\n- a\n---\n"_s;
        QTest::newRow("last item") << u"---\ntags:\n  - old\ntitle: t\n---\n"_s
                                   << u"---\ntags:\ntitle: t\n---\n"_s;
        QTest::newRow("flow list") << u"---\ntags: [a, old, 'b']\n---\n"_s << u"---\ntags: [a, 'b']\n---\n"_s;
        QTest::newRow("flow list, only item") << u"---\ntags: [old]\n---\n"_s << u"---\ntags: []\n---\n"_s;
        QTest::newRow("comma string") << u"---\ntags: a, old, b\n---\n"_s << u"---\ntags: a, b\n---\n"_s;
        QTest::newRow("space string") << u"---\ntags: old a\n---\n"_s << u"---\ntags: a\n---\n"_s;
        QTest::newRow("string, only tag") << u"---\ntags: old\n---\n"_s << u"---\ntags:\n---\n"_s;
        QTest::newRow("not there") << u"---\ntags: [a]\n---\n"_s << u"---\ntags: [a]\n---\n"_s;
        QTest::newRow("no frontmatter") << u"Body\n"_s << u"Body\n"_s;
        QTest::newRow("windows line endings") << u"---\r\ntags:\r\n  - old\r\n  - a\r\n---\r\nBody\r\n"_s
                                              << u"---\r\ntags:\r\n  - a\r\n---\r\nBody\r\n"_s;
    }
    void tagsAreRemovedInTheExistingStyle()
    {
        QFETCH(QString, before);
        QFETCH(QString, after);
        FrontmatterPatch p;
        p.removeTags = {u"OLD"_s};
        const FrontmatterEdit e = planFrontmatterEdit(before, p);
        QVERIFY2(e.ok, qPrintable(e.error));
        QCOMPARE(applyFrontmatterEdit(before, e), after);
        QVERIFY(parseFrontmatter(after.toUtf8()).valid);
        QVERIFY(!parseFrontmatter(after.toUtf8()).tags.contains(u"old"_s, Qt::CaseInsensitive));
    }

    void tagsAreAddedAndRemovedInOneEdit()
    {
        const QString note = u"---\ntitle: x\ntags:\n  - old\n  - keep\n---\nBody\n"_s;
        FrontmatterPatch p;
        p.removeTags = {u"old"_s};
        p.addTags = {u"new"_s};
        const FrontmatterEdit e = planFrontmatterEdit(note, p);
        QVERIFY(e.ok && e.changed);
        QCOMPARE(e.replaceLines, 6);
        QCOMPARE(applyFrontmatterEdit(note, e), u"---\ntitle: x\ntags:\n  - keep\n  - new\n---\nBody\n"_s);
        // Removing every block item, then adding: still a block list.
        p.removeTags = {u"old"_s, u"keep"_s};
        QCOMPARE(applyFrontmatterEdit(note, planFrontmatterEdit(note, p)),
                 u"---\ntitle: x\ntags:\n  - new\n---\nBody\n"_s);
        // A nested value can't be edited: reported, not guessed.
        const QString nested = u"---\ntags:\n  key: value\n---\n"_s;
        p = {};
        p.removeTags = {u"value"_s};
        const FrontmatterEdit n = planFrontmatterEdit(nested, p);
        QVERIFY(n.ok && !n.changed);
    }

    void lookAlikeTags()
    {
        QCOMPARE(tagLookAlikeKey(u"#Machine-Learning"), tagLookAlikeKey(u"machine_learnings"));
        QCOMPARE(tagLookAlikeKey(u"Project"), tagLookAlikeKey(u"projects"));
        QVERIFY(tagLookAlikeKey(u"gas") != tagLookAlikeKey(u"ga"));
        QVERIFY(tagLookAlikeKey(u"rust") != tagLookAlikeKey(u"trust"));
    }

    void notFrontmatter()
    {
        QVERIFY(!parseFixture(u"no-closing.md"_s).present);
        QVERIFY(!parseFixture(u"not-first-line.md"_s).present);
    }

    void crlfAndEmpty()
    {
        const Frontmatter crlf = parseFixture(u"crlf.md"_s);
        QVERIFY(crlf.present);
        QCOMPARE(crlf.tags, QStringList{u"crlf"_s});
        QCOMPARE(crlf.bodyOffset, qsizetype(24));
        const Frontmatter empty = parseFixture(u"empty.md"_s);
        QVERIFY(empty.present);
        QVERIFY(empty.valid);
        QCOMPARE(empty.bodyOffset, qsizetype(8));
    }

    void nestedValuesAreToleratedAndSkipped()
    {
        const Frontmatter fm = parseFixture(u"nested.md"_s);
        QVERIFY(fm.valid);
        QCOMPARE(fm.tags, QStringList{u"after-nested"_s});
        QVERIFY(fm.field(u"meta")->complex);
        QCOMPARE(fm.field(u"title")->value, u"Nested"_s);
    }

    void scalarsAndQuotes()
    {
        const Frontmatter fm =
            parseFrontmatter("---\na: 'it''s'\nb: \"q \\\"x\\\"\"\nc: plain # comment\n---\n");
        QVERIFY(fm.valid);
        QCOMPARE(fm.field(u"a")->value, u"it's"_s);
        QCOMPARE(fm.field(u"b")->value, u"q \"x\""_s);
        QCOMPARE(fm.field(u"c")->value, u"plain"_s);
    }

    void duplicateKeysAreInvalid() { QVERIFY(!parseFrontmatter("---\na: 1\na: 2\n---\n").valid); }

    void splitTagString()
    {
        QCOMPARE(mn::splitTagString(u"#a, b  c,#d"), (QStringList{u"a"_s, u"b"_s, u"c"_s, u"d"_s}));
    }
};

QTEST_GUILESS_MAIN(TestFrontmatter)
#include "tst_frontmatter.moc"
