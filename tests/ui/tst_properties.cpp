#include "TestUtil.h"
#include "ui/PropertiesForm.h"
#include "ui/TagPicker.h"

#include <QComboBox>
#include <QPlainTextEdit>

using namespace mn;
using namespace mn::ui;
using namespace Qt::StringLiterals;

namespace {

VaultSettings settings()
{
    VaultSettings s;
    s.tagProperties = {u"primary"_s, u"secondary"_s};
    s.categoryTags = {u"tech"_s, u"science"_s, u"personal"_s};
    return s;
}

const std::vector<TagCount> kTags{{u"tech"_s, 9}, {u"rust"_s, 3}, {u"science"_s, 2}};

QString apply(const QString& note, const PropertiesForm& f, bool writeEmpty)
{
    const Frontmatter fm = parseFrontmatter(note.toUtf8());
    return applyFrontmatterEdit(note, planFrontmatterEdit(note, f.patch(fm, writeEmpty)));
}

} // namespace

class TestProperties : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void defaultFormAndCustomForm()
    {
        PropertiesForm f(settings(), kTags);
        for (const QString& k : {u"primary"_s, u"secondary"_s, u"tags"_s, u"aliases"_s, u"status"_s,
                                 u"summary"_s, u"sources"_s, u"created"_s})
            QVERIFY2(f.field(k) != nullptr, qPrintable(k));
        QCOMPARE(qobject_cast<QComboBox*>(f.field(u"status"_s))->count(), 4); // (none), draft, active, done

        VaultSettings custom = settings();
        custom.noteProperties = {{u"area"_s, PropertyKind::Category, {}},
                                 {u"tags"_s, PropertyKind::Tags, {}}};
        PropertiesForm g(custom, kTags);
        QVERIFY(g.field(u"area"_s) != nullptr);
        QVERIFY(g.field(u"status"_s) == nullptr);
        // No category tags set: the pinned tags are the choices.
        custom.categoryTags.clear();
        custom.pinnedTags = {u"a"_s, u"b"_s};
        PropertiesForm h(custom, kTags);
        QCOMPARE(h.categoryCombo(0)->count(), 3);
    }

    void onlyChangesAreWritten()
    {
        const QString valid =
            u"---\nprimary: tech\ntags: [tech]\nsummary: |\n  long text\nextra: keep\n---\nBody\n"_s;
        PropertiesForm f(settings(), kTags);
        f.load(parseFrontmatter(valid.toUtf8()));
        QVERIFY(!f.field(u"summary"_s)->isEnabled()); // block scalar: read-only here
        QVERIFY(!f.hasChanges(parseFrontmatter(valid.toUtf8())));
        QCOMPARE(apply(valid, f, false), valid);
        f.setText(u"status"_s, u"done"_s);
        QCOMPARE(
            apply(valid, f, false),
            u"---\nprimary: tech\ntags: [tech]\nsummary: |\n  long text\nextra: keep\nstatus: done\n---\nBody\n"_s);
    }

    void categoriesFollowIntoTags()
    {
        PropertiesForm f(settings(), kTags);
        f.load(parseFrontmatter("---\nprimary: tech\ntags:\n  - tech\n  - rust\n---\n"));
        QCOMPARE(f.tagPicker()->tags(), (QStringList{u"tech"_s, u"rust"_s})); // loading doesn't touch tags
        f.categoryCombo(0)->setCurrentIndex(2);                               // science
        QCOMPARE(f.tagPicker()->tags(), (QStringList{u"rust"_s, u"science"_s}));
        f.categoryCombo(1)->setCurrentIndex(3); // personal
        QCOMPARE(f.tagPicker()->tags(), (QStringList{u"rust"_s, u"science"_s, u"personal"_s}));
        // The same category in both: the later one is cleared, the tag stays.
        f.categoryCombo(0)->setCurrentIndex(3);
        QCOMPARE(f.categoryCombo(1)->currentIndex(), 0);
        QCOMPARE(f.tagPicker()->tags(), (QStringList{u"rust"_s, u"personal"_s}));

        VaultSettings off = settings();
        off.categoriesInTags = false;
        PropertiesForm g(off, kTags);
        g.categoryCombo(0)->setCurrentIndex(1);
        QVERIFY(g.tagPicker()->tags().isEmpty());
    }

    void unknownValuesAreKept()
    {
        PropertiesForm f(settings(), kTags);
        f.load(parseFrontmatter("---\nprimary: Humanities\nstatus: someday\n---\n"));
        QCOMPARE(f.text(u"primary"_s), u"Humanities"_s);
        QCOMPARE(f.text(u"status"_s), u"someday"_s);
        QVERIFY(!f.hasChanges(parseFrontmatter("---\nprimary: Humanities\nstatus: someday\n---\n")));
    }

    void prefillAndSuggestions()
    {
        PropertiesForm f(settings(), kTags);
        f.prefillNew(QDate(2026, 11, 25), u"  https://example.com/paper  "_s);
        QCOMPARE(f.text(u"created"_s), u"2026-11-25"_s);
        QCOMPARE(f.text(u"sources"_s), u"https://example.com/paper"_s);
        PropertiesForm g(settings(), kTags);
        g.prefillNew(QDate(2026, 11, 25), u"not a link"_s);
        QVERIFY(g.text(u"sources"_s).isEmpty());

        // Suggestions only fill what's empty; categories must be known choices.
        f.setText(u"summary"_s, u"Mine"_s);
        f.applySuggestion({{u"primary"_s, u"Science"_s},
                           {u"secondary"_s, u"astrology"_s},
                           {u"summary"_s, u"Theirs"_s},
                           {u"unknown"_s, u"x"_s}},
                          {u"rust"_s, u"New Tag"_s});
        QCOMPARE(f.text(u"primary"_s), u"science"_s);
        QCOMPARE(f.text(u"secondary"_s), QString());
        QCOMPARE(f.text(u"summary"_s), u"Mine"_s);
        QCOMPARE(f.tagPicker()->tags(), (QStringList{u"science"_s, u"rust"_s, u"New-Tag"_s}));
    }

    void newNoteContent()
    {
        PropertiesForm f(settings(), kTags);
        f.prefillNew(QDate(2026, 11, 25), QString());
        f.categoryCombo(0)->setCurrentIndex(1); // tech
        f.setText(u"aliases"_s, u"RS\n\nRust lang\nRS"_s);
        QCOMPARE(
            apply(QString(), f, true),
            u"---\nprimary: tech\nsecondary:\ntags:\n  - tech\naliases:\n  - RS\n  - Rust lang\nstatus:\n"
            "summary:\nsources:\ncreated: 2026-11-25\n---\n"_s);
        QCOMPARE(
            apply(QString(), f, false),
            u"---\nprimary: tech\ntags:\n  - tech\naliases:\n  - RS\n  - Rust lang\ncreated: 2026-11-25\n---\n"_s);
    }
};

QTEST_MAIN(TestProperties)
#include "tst_properties.moc"
