#include "TestUtil.h"
#include "core/templates/Templates.h"

using namespace mn;
using namespace Qt::StringLiterals;

class TestTemplates : public QObject {
    Q_OBJECT

    const QDateTime when{QDate(2024, 3, 1), QTime(14, 5, 9)};

private Q_SLOTS:
    void momentTokens()
    {
        QCOMPARE(formatMoment(when, u"YYYY-MM-DD"), u"2024-03-01"_s);
        QCOMPARE(formatMoment(when, u"YY M D"), u"24 3 1"_s);
        QCOMPARE(formatMoment(when, u"MMMM MMM"), u"March Mar"_s);
        QCOMPARE(formatMoment(when, u"dddd ddd"), u"Friday Fri"_s);
        QCOMPARE(formatMoment(when, u"HH:mm:ss"), u"14:05:09"_s);
        QCOMPARE(formatMoment(when, u"H:m:s"), u"14:5:9"_s);
        QCOMPARE(formatMoment(when, u"hh h A a"), u"02 2 PM pm"_s);
        QCOMPARE(formatMoment(when, u"Do"), u"1st"_s);
        QCOMPARE(formatMoment(QDateTime(QDate(2024, 3, 22), QTime(0, 0)), u"Do"), u"22nd"_s);
        QCOMPARE(formatMoment(QDateTime(QDate(2024, 3, 13), QTime(0, 0)), u"Do"), u"13th"_s);
        QCOMPARE(formatMoment(QDateTime(QDate(2024, 3, 1), QTime(0, 30)), u"h A"), u"12 AM"_s);
    }

    void literalsAndUnknownTokens()
    {
        QCOMPARE(formatMoment(when, u"[Week of] YYYY"), u"Week of 2024"_s);
        QCOMPARE(formatMoment(when, u"YYYY Q x"), u"2024 Q x"_s);
    }

    void substitution()
    {
        TemplateContext ctx;
        ctx.title = u"My Note"_s;
        ctx.now = when;
        ctx.dateFormat = u"DD/MM/YYYY"_s;
        ctx.timeFormat = u"h:mm A"_s;
        QCOMPARE(
            substituteTemplate(u"# {{title}} {{date}} {{time}} {{date:YYYY}} {{time:HH}} {{unknown}}", ctx),
            u"# My Note 01/03/2024 2:05 PM 2024 14 {{unknown}}"_s);
    }

    void fixtureTemplate()
    {
        TemplateContext ctx;
        ctx.title = u"Daily"_s;
        ctx.now = when;
        const QString tpl =
            QString::fromUtf8(test::readBytes(test::fixture(u"templates/Templates/Daily.md"_s)));
        const QString out = substituteTemplate(tpl, ctx);
        QVERIFY(out.contains(u"created: 2024-03-01"_s));
        QVERIFY(out.contains(u"Today is Friday, March 1st 2024 at 14:05."_s));
    }

    void insertIntoNoteWithoutFrontmatter()
    {
        const TemplateInsertion r = planTemplateInsertion(u"body\n", u"---\ntags: [x]\n---\nInserted\n");
        QVERIFY(r.frontmatterChanged);
        QCOMPARE(r.replaceLength, 0);
        QCOMPARE(r.frontmatter, u"---\ntags: [x]\n---\n"_s);
        QCOMPARE(r.body, u"Inserted\n"_s);
    }

    void mergeKeepsNoteValues()
    {
        const QString note = u"---\ntags: [mine]\ntitle: Kept\n---\nbody\n"_s;
        const QString tpl = u"---\ntags: [theirs]\ncreated: today\nlist:\n  - a\n  - b\n---\nT\n"_s;
        const TemplateInsertion r = planTemplateInsertion(note, tpl);
        QVERIFY(r.frontmatterChanged);
        QCOMPARE(r.replaceLength, int(u"---\ntags: [mine]\ntitle: Kept\n---\n"_s.size()));
        QCOMPARE(r.frontmatter,
                 u"---\ntags: [mine]\ntitle: Kept\ncreated: today\nlist:\n  - a\n  - b\n---\n"_s);
        QCOMPARE(r.body, u"T\n"_s);
    }

    void templateWithoutFrontmatter()
    {
        const TemplateInsertion r = planTemplateInsertion(u"---\na: 1\n---\n", u"just text");
        QVERIFY(!r.frontmatterChanged);
        QCOMPARE(r.body, u"just text"_s);
    }

    void invalidNoteFrontmatterIsLeftAlone()
    {
        const TemplateInsertion r = planTemplateInsertion(u"---\na: [x\n---\n", u"---\nb: 1\n---\nT");
        QVERIFY(!r.frontmatterChanged);
        QVERIFY(!r.warning.isEmpty());
        QCOMPARE(r.body, u"T"_s);
    }
};

QTEST_GUILESS_MAIN(TestTemplates)
#include "tst_templates.moc"
