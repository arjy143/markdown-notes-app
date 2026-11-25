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

    void propertiesAreSetInPlace_data()
    {
        QTest::addColumn<QString>("before");
        QTest::addColumn<QString>("key");
        QTest::addColumn<QStringList>("values");
        QTest::addColumn<bool>("list");
        QTest::addColumn<bool>("keepEmpty");
        QTest::addColumn<QString>("after");
        const QString note =
            u"---\nprimary: tech # main\nsummary: \"Old: text\"\naliases:\n    - A\n    - B\n"
            "sources: [x, y]\n---\nBody\n"_s;
        QTest::newRow("change a scalar")
            << note << u"primary"_s << QStringList{u"science"_s} << false << false
            << u"---\nprimary: science\nsummary: \"Old: text\"\naliases:\n    - A\n"
               "    - B\nsources: [x, y]\n---\nBody\n"_s;
        QTest::newRow("same value: untouched")
            << note << u"summary"_s << QStringList{u"Old: text"_s} << false << false << note;
        QTest::newRow("block list keeps its indent")
            << note << u"aliases"_s << QStringList{u"A"_s, u"C"_s} << true << false
            << u"---\nprimary: tech # main\nsummary: \"Old: text\"\naliases:\n    - A\n    - C\nsources: [x, "
               u"y]\n"
               "---\nBody\n"_s;
        QTest::newRow("flow list stays flow")
            << note << u"sources"_s << QStringList{u"https://a.b/c?d=1"_s, u"Book, 2nd ed"_s} << true << false
            << u"---\nprimary: tech # main\nsummary: \"Old: text\"\naliases:\n    - A\n    - B\n"
               "sources: [\"https://a.b/c?d=1\", \"Book, 2nd ed\"]\n---\nBody\n"_s;
        QTest::newRow("cleared, kept empty")
            << note << u"aliases"_s << QStringList{} << true << true
            << u"---\nprimary: tech # main\nsummary: \"Old: text\"\naliases:\nsources: [x, y]\n---\nBody\n"_s;
        QTest::newRow("cleared, removed")
            << note << u"aliases"_s << QStringList{} << true << false
            << u"---\nprimary: tech # main\nsummary: \"Old: text\"\nsources: [x, y]\n---\nBody\n"_s;
        QTest::newRow("new, at the end")
            << note << u"status"_s << QStringList{u"draft"_s} << false << false
            << u"---\nprimary: tech # main\nsummary: \"Old: text\"\naliases:\n    - A\n"
               "    - B\nsources: [x, y]\nstatus: draft\n---\nBody\n"_s;
        QTest::newRow("new, empty") << u"---\nx: 1\n---\n"_s << u"status"_s << QStringList{} << false << true
                                    << u"---\nx: 1\nstatus:\n---\n"_s;
        QTest::newRow("new, empty, not kept") << u"---\nx: 1\n---\n"_s << u"status"_s << QStringList{}
                                              << false << false << u"---\nx: 1\n---\n"_s;
        QTest::newRow("empty value filled")
            << u"---\nstatus:\n---\n"_s << u"status"_s << QStringList{u"done"_s} << false << true
            << u"---\nstatus: done\n---\n"_s;
        QTest::newRow("scalar becomes a list")
            << u"---\nsources: one\n---\n"_s << u"sources"_s << QStringList{u"one"_s, u"two"_s} << true
            << false << u"---\nsources:\n  - one\n  - two\n---\n"_s;
        QTest::newRow("no frontmatter yet") << u"Body\n"_s << u"status"_s << QStringList{u"draft"_s} << false
                                            << false << u"---\nstatus: draft\n---\nBody\n"_s;
        QTest::newRow("no frontmatter, nothing to write")
            << u"Body\n"_s << u"status"_s << QStringList{} << false << false << u"Body\n"_s;
        QTest::newRow("windows line endings")
            << u"---\r\nstatus: draft\r\n---\r\nBody\r\n"_s << u"status"_s << QStringList{u"done"_s} << false
            << false << u"---\r\nstatus: done\r\n---\r\nBody\r\n"_s;
    }
    void propertiesAreSetInPlace()
    {
        QFETCH(QString, before);
        QFETCH(QString, key);
        QFETCH(QStringList, values);
        QFETCH(bool, list);
        QFETCH(bool, keepEmpty);
        QFETCH(QString, after);
        FrontmatterPatch p;
        p.set.append({key, values, list, keepEmpty});
        const FrontmatterEdit e = planFrontmatterEdit(before, p);
        QVERIFY2(e.ok, qPrintable(e.error));
        QCOMPARE(e.changed, before != after);
        QCOMPARE(applyFrontmatterEdit(before, e), after);
        QVERIFY(parseFrontmatter(after.toUtf8()).valid);
    }

    void aWholeFormInOneEdit()
    {
        // A new note: every property in form order, blanks kept empty.
        FrontmatterPatch p;
        p.set = {{u"primary"_s, {u"tech"_s}, false, true},
                 {u"secondary"_s, {}, false, true},
                 {u"tags"_s, {u"tech"_s, u"rust"_s}, true, true},
                 {u"aliases"_s, {}, true, true},
                 {u"summary"_s, {u"About: ownership"_s}, false, true},
                 {u"created"_s, {u"2026-11-25"_s}, false, true}};
        QCOMPARE(applyFrontmatterEdit(QString(), planFrontmatterEdit(QString(), p)),
                 u"---\nprimary: tech\nsecondary:\ntags:\n  - tech\n  - rust\naliases:\nsummary: \"About: "
                 u"ownership\"\n"
                 "created: 2026-11-25\n---\n"_s);
        // An existing note: changes, tag edits and new keys together; the rest untouched.
        const QString note = u"---\nprimary: tech\ntertiary: meta\ntags: [tech, old]\n---\nBody\n"_s;
        p = {};
        p.set = {{u"primary"_s, {u"science"_s}, false, true}, {u"status"_s, {}, false, true}};
        p.removeTags = {u"old"_s, u"tech"_s};
        p.addTags = {u"science"_s};
        const FrontmatterEdit e = planFrontmatterEdit(note, p);
        QVERIFY(e.ok && e.changed);
        QCOMPARE(e.replaceLines, 5);
        QCOMPARE(applyFrontmatterEdit(note, e),
                 u"---\nprimary: science\ntertiary: meta\ntags: [science]\nstatus:\n---\nBody\n"_s);
        // Nested values aren't guessed at.
        const QString nested = u"---\nsummary: |\n  long\n  text\n---\n"_s;
        p = {};
        p.set = {{u"summary"_s, {u"short"_s}, false, false}};
        const FrontmatterEdit n = planFrontmatterEdit(nested, p);
        QVERIFY(n.ok && !n.changed);
        QCOMPARE(n.skipped.size(), 1);
    }

    void plainValuesReadBackTheSame()
    {
        const QStringList values{u"https://example.com/a?b=1&c=2#top"_s,
                                 u"C++ notes"_s,
                                 u"Rust, 2nd ed."_s,
                                 u"Note: colon"_s,
                                 u"a #b"_s,
                                 u"true"_s,
                                 u"42"_s,
                                 u"2026-11-25"_s,
                                 u"ends:"_s,
                                 u"\"quoted\""_s,
                                 u"- dash"_s};
        for (const QString& v : values) {
            const QString text = u"---\nk: "_s + yamlScalar(v) + u"\n---\n"_s;
            const Frontmatter fm = parseFrontmatter(text.toUtf8());
            QVERIFY2(fm.valid, qPrintable(text));
            QCOMPARE(fm.field(u"k")->value, v);
        }
        QCOMPARE(yamlScalar(u"https://example.com/x"_s), u"https://example.com/x"_s);
        QCOMPARE(yamlScalar(u"Note: colon"_s), u"\"Note: colon\""_s);
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
