#include "TestUtil.h"
#include "ui/TagPicker.h"

#include <QLineEdit>
#include <QListWidget>
#include <QSignalSpy>

using namespace mn;
using namespace mn::ui;
using namespace Qt::StringLiterals;

namespace {

std::vector<TagCount> vaultTags()
{
    return {{u"personal"_s, 7}, {u"physics"_s, 5}, {u"Project"_s, 10},
            {u"projects"_s, 3}, {u"python"_s, 20}, {u"tech/rust"_s, 4}};
}

QStringList shown(const TagPicker& p)
{
    QStringList out;
    for (int i = 0; i < p.results()->count(); ++i)
        out.append(p.results()->item(i)->data(Qt::UserRole).toString());
    return out;
}

QString current(const TagPicker& p)
{
    return p.results()->currentItem() ? p.results()->currentItem()->data(Qt::UserRole).toString() : QString();
}

} // namespace

class TestTagPicker : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void mostUsedFirstWhenEmpty()
    {
        TagPicker p;
        p.setVaultTags(vaultTags());
        QCOMPARE(shown(p).first(), u"python"_s);
        QCOMPARE(shown(p).size(), 6);
    }

    void fuzzyAndLookAlikes()
    {
        TagPicker p;
        p.setVaultTags(vaultTags());
        p.searchBox()->setText(u"pyt"_s);
        QCOMPARE(shown(p), (QStringList{u"python"_s, u"pyt"_s})); // and "pyt" as a new tag
        // Look-alikes come first, and no new tag is offered when the tag exists in any case.
        p.searchBox()->setText(u"PROJECT"_s);
        QCOMPARE(shown(p).mid(0, 2), (QStringList{u"Project"_s, u"projects"_s}));
        QVERIFY(!shown(p).contains(u"PROJECT"_s));
        // A near-duplicate spelling still finds them first.
        p.searchBox()->setText(u"project_s"_s);
        QCOMPARE(shown(p).mid(0, 2), (QStringList{u"Project"_s, u"projects"_s}));
        // Nested tags match on their last part too.
        p.searchBox()->setText(u"rust"_s);
        QCOMPARE(current(p), u"tech/rust"_s);
        // Nothing like it: only the new tag, cleaned.
        p.searchBox()->setText(u"#Deep Work"_s);
        QCOMPARE(shown(p), QStringList{u"Deep-Work"_s});
    }

    void keyboard()
    {
        TagPicker p;
        p.setVaultTags(vaultTags());
        QSignalSpy changed(&p, &TagPicker::changed);
        QSignalSpy submitted(&p, &TagPicker::submitted);
        QTest::keyClicks(p.searchBox(), u"pyth"_s);
        QTest::keyClick(p.searchBox(), Qt::Key_Return);
        QCOMPARE(p.tags(), QStringList{u"python"_s});
        QVERIFY(p.searchBox()->text().isEmpty());
        QVERIFY(!shown(p).contains(u"python"_s)); // already chosen
        // The vault's spelling is used, and there are no duplicates in another case.
        p.addTag(u"PYTHON"_s);
        p.addTag(u"#project"_s);
        QCOMPARE(p.tags(), (QStringList{u"python"_s, u"Project"_s}));
        // Down picks the next match.
        QTest::keyClicks(p.searchBox(), u"p"_s);
        const QString second = shown(p).value(1);
        QVERIFY(!second.isEmpty());
        QTest::keyClick(p.searchBox(), Qt::Key_Down);
        QTest::keyClick(p.searchBox(), Qt::Key_Return);
        QCOMPARE(p.tags().size(), 3);
        QCOMPARE(p.tags().last(), second);
        // Backspace on an empty search removes the last tag.
        QTest::keyClick(p.searchBox(), Qt::Key_Backspace);
        QCOMPARE(p.tags(), (QStringList{u"python"_s, u"Project"_s}));
        QCOMPARE(p.chosen()->item(0)->data(Qt::UserRole).toString(), u"python"_s);
        p.removeTag(u"Python"_s);
        QCOMPARE(p.tags(), QStringList{u"Project"_s});
        QCOMPARE(changed.size(), 5);
        // Enter on an empty search is "done".
        QTest::keyClick(p.searchBox(), Qt::Key_Return);
        QCOMPARE(submitted.size(), 1);
    }

    void mostUsedSpellingFirst()
    {
        TagPicker p;
        p.setVaultTags({{u"project"_s, 2}, {u"projects"_s, 88}, {u"prose"_s, 50}});
        p.searchBox()->setText(u"proj"_s);
        QCOMPARE(shown(p).mid(0, 2), (QStringList{u"projects"_s, u"project"_s}));
        QCOMPARE(p.results()->item(0)->text(), u"#projects   88 notes"_s);
    }

    void dialogReportsTheChanges()
    {
        NoteTagsDialog d(u"Note"_s, {u"old"_s, u"keep"_s}, vaultTags());
        d.picker()->removeTag(u"OLD"_s);
        d.picker()->addTag(u"python"_s);
        d.picker()->addTag(u"brand new"_s);
        QCOMPARE(d.added(), (QStringList{u"python"_s, u"brand-new"_s}));
        QCOMPARE(d.removed(), QStringList{u"old"_s});
    }
};

QTEST_MAIN(TestTagPicker)
#include "tst_tagpicker.moc"
