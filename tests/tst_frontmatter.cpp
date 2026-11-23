#include "TestUtil.h"
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
