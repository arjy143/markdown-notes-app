#include "TestUtil.h"
#include "core/edit/ListEdit.h"
#include "core/templates/Templates.h"

using namespace mn;
using namespace mn::lists;
using namespace Qt::StringLiterals;

namespace {
QStringList L(const QString& s)
{
    return s.split(u'\n');
}
QString J(const QStringList& l)
{
    return l.join(u'\n');
}
} // namespace

class TestLists : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void parsing()
    {
        auto it = parseItem(u"  12) [x] done");
        QVERIFY(it);
        QVERIFY(it->ordered);
        QCOMPARE(it->number, 12);
        QCOMPARE(it->delimiter, QChar(u')'));
        QCOMPARE(it->checkbox, u"[x] "_s);
        QCOMPARE(it->contentStart, 10);
        QVERIFY(parseItem(u"-"));
        QVERIFY(!parseItem(u"-not a list"));
        QVERIFY(!parseItem(u"1.5 kg"));
    }

    void indentUnitFollowsTheNote()
    {
        QCOMPARE(indentUnit(L(u"- a\n- b"_s)), u"\t"_s);
        QCOMPARE(indentUnit(L(u"- a\n  - b\n    - c"_s)), u"  "_s);
        QCOMPARE(indentUnit(L(u"- a\n\t- b"_s)), u"\t"_s);
        QCOMPARE(indentUnit(L(u"text\n    indented code"_s)), u"\t"_s); // not list lines
    }

    void indentAndOutdent()
    {
        QStringList l = L(u"1. a\n2. b\n3. c\n4. d"_s);
        QVERIFY(indentLines(l, 1, 1, u"\t"_s, false));
        QVERIFY(renumber(l, 1, 1));
        QCOMPARE(J(l), u"1. a\n\t1. b\n2. c\n3. d"_s);
        QVERIFY(indentLines(l, 2, 2, u"\t"_s, false)); // joins the nested list
        renumber(l, 2, 2);
        QCOMPARE(J(l), u"1. a\n\t1. b\n\t2. c\n2. d"_s);
        QVERIFY(indentLines(l, 1, 2, u"\t"_s, true)); // both back out
        renumber(l, 1, 2);
        QCOMPARE(J(l), u"1. a\n2. b\n3. c\n4. d"_s);
        // Spaces, and blank lines untouched.
        l = L(u"- a\n\n- b"_s);
        QVERIFY(indentLines(l, 0, 2, u"  "_s, false));
        QCOMPARE(J(l), u"  - a\n\n  - b"_s);
        QVERIFY(indentLines(l, 0, 2, u"  "_s, true));
        QCOMPARE(J(l), u"- a\n\n- b"_s);
        QVERIFY(!indentLines(l, 0, 0, u"  "_s, true)); // nothing to outdent
    }

    void renumbering()
    {
        // A deleted item: the rest close up; the first number is kept.
        QStringList l = L(u"3. a\n5. c\n9. d"_s);
        QVERIFY(renumber(l, 1, 1));
        QCOMPARE(J(l), u"3. a\n4. c\n5. d"_s);
        // Nested lists and continuation lines don't break the run; each level counts alone.
        l = L(u"1. a\n   more text\n\t1. x\n\t5. y\n4. b\n\n7. c\nend\n9. other"_s);
        QVERIFY(renumber(l, 0, 8));
        QCOMPARE(J(l), u"1. a\n   more text\n\t1. x\n\t2. y\n2. b\n\n3. c\nend\n9. other"_s);
        // Different delimiters are different lists; bullets end a run.
        l = L(u"1. a\n3) b\n- c\n7. d"_s);
        renumber(l, 0, 3);
        QCOMPARE(J(l), u"1. a\n3) b\n- c\n7. d"_s);
        QVERIFY(!renumber(l, 0, 0));
    }

    void checkboxes()
    {
        QCOMPARE(toggleCheckbox(u"- [ ] task"), u"- [x] task"_s);
        QCOMPARE(toggleCheckbox(u"\t- [X] task"), u"\t- [ ] task"_s);
        QCOMPARE(toggleCheckbox(u"- item"), u"- [ ] item"_s);
        QCOMPARE(toggleCheckbox(u"2. item"), u"2. [ ] item"_s);
        QCOMPARE(toggleCheckbox(u"  plain"), u"  - [ ] plain"_s);
        QCOMPARE(toggleCheckbox(u""), u"- [ ] "_s);
    }

    void timestamps()
    {
        qsizetype at = -1;
        qsizetype shift = 0;
        QCOMPARE(timestampLine(u"went for a run", u"14:32"_s, &at, &shift), u"14:32 - went for a run"_s);
        QCOMPARE(at, 0);
        QCOMPARE(shift, 8);
        QCOMPARE(timestampLine(u"- [ ] call Sam", u"09:05"_s, &at), u"- [ ] 09:05 - call Sam"_s);
        QCOMPARE(at, 6);
        // An existing timestamp is replaced, not doubled.
        QCOMPARE(timestampLine(u"- 08:00 - note", u"09:05"_s, nullptr, &shift), u"- 09:05 - note"_s);
        QCOMPARE(shift, 0);
        QCOMPARE(timestampLine(u"  ", u"9:05 pm"_s), u"  9:05 pm - "_s);
    }

    void momentDatesParse()
    {
        QCOMPARE(parseMomentDate(u"2026-11-25", u"YYYY-MM-DD"), QDate(2026, 11, 25));
        QCOMPARE(parseMomentDate(u"2026/11-25", u"YYYY/MM-DD"), QDate(2026, 11, 25));
        QCOMPARE(parseMomentDate(u"Wednesday, November 25th 2026", u"dddd, MMMM Do YYYY"),
                 QDate(2026, 11, 25));
        QCOMPARE(parseMomentDate(u"Day 25 Nov 26", u"[Day] D MMM YY"), QDate(2026, 11, 25));
        QCOMPARE(parseMomentDate(u"2026-02-30", u"YYYY-MM-DD"), QDate());
        QCOMPARE(parseMomentDate(u"Monday, November 25th 2026", u"dddd, MMMM Do YYYY"),
                 QDate()); // wrong weekday
        QCOMPARE(parseMomentDate(u"notes 2026-11-25", u"YYYY-MM-DD"), QDate());
    }
};

QTEST_GUILESS_MAIN(TestLists)
#include "tst_lists.moc"
