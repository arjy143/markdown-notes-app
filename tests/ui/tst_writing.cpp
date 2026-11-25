#include "TestUtil.h"
#include "core/capture/Capture.h"
#include "core/index/VaultService.h"
#include "ui/CalendarPanel.h"
#include "ui/HomePage.h"
#include "ui/HtmlToMarkdown.h"
#include "ui/MainWindow.h"
#include "ui/NoteEditor.h"
#include "ui/Panels.h"
#include "ui/ShortcutsDialog.h"

#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QLineEdit>
#include <QListWidget>
#include <QMimeData>
#include <QPushButton>
#include <QSettings>
#include <QSignalSpy>
#include <QTabWidget>
#include <QTextBlock>
#include <QTreeWidget>

using namespace mn;
using namespace mn::ui;
using namespace Qt::StringLiterals;

namespace {

bool waitIndexed(MainWindow& w)
{
    QSignalSpy spy(w.vault(), &VaultService::indexingFinished);
    return spy.wait(15000);
}

// An editor holding `text` with the cursor at the end of block `line`.
void setUp(NoteEditor& ed, const QString& text, int line)
{
    ed.setBuffer(text);
    QTextCursor c(ed.document()->findBlockByNumber(line));
    c.movePosition(QTextCursor::EndOfBlock);
    ed.setTextCursor(c);
}

void paste(NoteEditor& ed, QMimeData* mime)
{
    QApplication::clipboard()->setMimeData(mime);
    ed.paste();
}

QAction* action(QWidget& w, const QString& text)
{
    for (QAction* a : w.findChildren<QAction*>()) {
        if (a->text() == text) return a;
    }
    return nullptr;
}

} // namespace

class TestWriting : public QObject {
    Q_OBJECT
    QTemporaryDir settingsDir_;

private Q_SLOTS:
    void initTestCase()
    {
        QVERIFY(settingsDir_.isValid());
        QSettings::setPath(QSettings::NativeFormat, QSettings::UserScope, settingsDir_.path());
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDir_.path());
        qputenv("XDG_CACHE_HOME", (settingsDir_.path() + u"/cache"_s).toUtf8());
        qputenv("XDG_CONFIG_HOME", (settingsDir_.path() + u"/config"_s).toUtf8());
    }

    // --- smart lists -------------------------------------------------------------

    void enterContinuesAndRenumbers()
    {
        NoteEditor ed;
        setUp(ed, u"1. a\n2. b\n3. c"_s, 0);
        QTest::keyClick(&ed, Qt::Key_Return);
        QTest::keyClicks(&ed, u"new"_s);
        QCOMPARE(ed.bufferText(), u"1. a\n2. new\n3. b\n4. c"_s);
        // Enter on an empty nested item moves it up a level instead of ending the list.
        setUp(ed, u"- a\n\t- "_s, 1);
        QTest::keyClick(&ed, Qt::Key_Return);
        QCOMPARE(ed.bufferText(), u"- a\n- "_s);
        QTest::keyClick(&ed, Qt::Key_Return);
        QCOMPARE(ed.bufferText(), u"- a\n"_s);
    }

    void tabIndentsListItems()
    {
        NoteEditor ed;
        setUp(ed, u"1. a\n2. b\n3. c"_s, 1);
        QTest::keyClick(&ed, Qt::Key_Tab);
        QCOMPARE(ed.bufferText(), u"1. a\n\t1. b\n2. c"_s);
        QCOMPARE(ed.textCursor().positionInBlock(), ed.textCursor().block().length() - 1); // still at the end
        QTest::keyClick(&ed, Qt::Key_Backtab);
        QCOMPARE(ed.bufferText(), u"1. a\n2. b\n3. c"_s);
        // The note's own indent (two spaces here) is used.
        setUp(ed, u"- a\n  - b\n- c"_s, 2);
        QTest::keyClick(&ed, Qt::Key_Tab);
        QCOMPARE(ed.bufferText(), u"- a\n  - b\n  - c"_s);
        // Outside a list, Tab still types a tab.
        setUp(ed, u"text"_s, 0);
        QTest::keyClick(&ed, Qt::Key_Tab);
        QCOMPARE(ed.bufferText(), u"text\t"_s);
        // One undo step per indent.
        setUp(ed, u"1. a\n2. b"_s, 1);
        QTest::keyClick(&ed, Qt::Key_Tab);
        ed.undo();
        QCOMPARE(ed.bufferText(), u"1. a\n2. b"_s);
    }

    void removingAnItemRenumbers()
    {
        NoteEditor ed;
        setUp(ed, u"1. a\n2. b\n3. c"_s, 1);
        QTextCursor c = ed.textCursor();
        c.movePosition(QTextCursor::StartOfBlock);
        c.movePosition(QTextCursor::Up, QTextCursor::MoveAnchor);
        c.movePosition(QTextCursor::EndOfBlock);
        c.movePosition(QTextCursor::Down, QTextCursor::KeepAnchor);
        c.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
        ed.setTextCursor(c);
        QTest::keyClick(&ed, Qt::Key_Backspace); // deletes "\n2. b"
        QCOMPARE(ed.bufferText(), u"1. a\n2. c"_s);
    }

    void checkboxesAndTimestamps()
    {
        NoteEditor ed;
        setUp(ed, u"- [ ] one\n- two\nthree"_s, 0);
        QTextCursor c = ed.textCursor();
        c.setPosition(0);
        c.movePosition(QTextCursor::End, QTextCursor::KeepAnchor);
        ed.setTextCursor(c);
        ed.toggleCheckboxes();
        QCOMPARE(ed.bufferText(), u"- [x] one\n- [ ] two\n- [ ] three"_s);
        ed.undo();
        QCOMPARE(ed.bufferText(), u"- [ ] one\n- two\nthree"_s);

        setUp(ed, u"- went running"_s, 0);
        ed.insertTimestamp(u"07:15"_s);
        QCOMPARE(ed.bufferText(), u"- 07:15 - went running"_s);
        QCOMPARE(ed.textCursor().positionInBlock(), ed.textCursor().block().length() - 1);
        ed.insertTimestamp(u"07:20"_s); // replaces, doesn't stack
        QCOMPARE(ed.bufferText(), u"- 07:20 - went running"_s);
    }

    // --- smart paste -------------------------------------------------------------

    void urlOverSelectionMakesALink()
    {
        NoteEditor ed;
        setUp(ed, u"see the docs here"_s, 0);
        QTextCursor c = ed.textCursor();
        c.setPosition(8);
        c.setPosition(12, QTextCursor::KeepAnchor); // "docs"
        ed.setTextCursor(c);
        auto* m = new QMimeData;
        m->setText(u"https://doc.qt.io/qt-6/qtextdocument.html"_s);
        paste(ed, m);
        QCOMPARE(ed.bufferText(), u"see the [docs](https://doc.qt.io/qt-6/qtextdocument.html) here"_s);
        // Without a selection, the URL is pasted as it is.
        auto* m2 = new QMimeData;
        m2->setText(u"https://x.y"_s);
        paste(ed, m2);
        QVERIFY(ed.bufferText().endsWith(u")https://x.y here"_s));
    }

    void richTextBecomesMarkdown()
    {
        NoteEditor ed;
        setUp(ed, QString(), 0);
        auto* m = new QMimeData;
        m->setHtml(u"<h2>Plan</h2><p>Read <b>this</b> and <a href=\"https://a.b\">that</a>.</p>"
                   "<ol><li>one</li><li>two</li></ol>"_s);
        m->setText(u"Plan\nRead this and that.\none\ntwo"_s);
        paste(ed, m);
        QCOMPARE(ed.bufferText(), u"## Plan\n\nRead **this** and [that](https://a.b).\n\n1. one\n2. two"_s);
        // Ctrl+Shift+V pastes the plain text.
        setUp(ed, QString(), 0);
        QApplication::clipboard()->setMimeData(m = new QMimeData);
        m->setHtml(u"<b>bold</b>"_s);
        m->setText(u"bold"_s);
        ed.pastePlainText();
        QCOMPARE(ed.bufferText(), u"bold"_s);
        // An editor's coloured code stays plain text.
        setUp(ed, QString(), 0);
        auto* code = new QMimeData;
        code->setHtml(u"<div><span style=\"color:#f00\">int</span> x = a*b;</div>"_s);
        code->setText(u"int x = a*b;"_s);
        paste(ed, code);
        QCOMPARE(ed.bufferText(), u"int x = a*b;"_s);
    }

    void htmlConversion()
    {
        QCOMPARE(htmlToMarkdown(u"<p>a <i>b</i> <s>c</s> <code>d*e</code></p>"_s), u"a *b* ~~c~~ `d*e`"_s);
        QCOMPARE(htmlToMarkdown(u"<ul><li>x<ul><li>y</li></ul></li></ul>"_s), u"- x\n\t- y"_s);
        QCOMPARE(
            htmlToMarkdown(u"<table><tr><th>A</th><th>B</th></tr><tr><td>1</td><td>2|3</td></tr></table>"_s),
            u"| A | B |\n| --- | --- |\n| 1 | 2\\|3 |"_s);
        QCOMPARE(htmlToMarkdown(u"<pre>line 1\n  line 2</pre>"_s), u"```\nline 1\n  line 2\n```"_s);
        QCOMPARE(htmlToMarkdown(u"<p><a href=\"https://a.b\">https://a.b</a></p>"_s), u"<https://a.b>"_s);
        QCOMPARE(htmlToMarkdown(u"<p>A long paragraph that should certainly not be wrapped at eighty "
                                "columns because Obsidian would show the breaks.</p>"_s)
                     .count(u'\n'),
                 0);
        QVERIFY(!htmlHasFormatting(u"<p>just <span>text</span></p>"_s));
        QVERIFY(htmlHasFormatting(u"<p><strong>x</strong></p>"_s));
    }

    // --- calendar, dashboard, export ---------------------------------------------

    void calendarShowsJournalNotes()
    {
        test::VaultCopy v(u"ui-vault"_s);
        QVERIFY(test::writeBytes(v.path(u"Daily/2026-11-20.md"_s), "---\ntags: [journal]\n---\nDay.\n"));
        QVERIFY(
            test::writeBytes(v.path(u"Trip.md"_s), "---\ncreated: 2026-11-21\ntags:\n  - journal\n---\n"));
        QVERIFY(test::writeBytes(v.path(u"2026-11-22 untagged.md"_s), "No tag.\n"));
        MainWindow w;
        QVERIFY(w.openVault(v.root()));
        QVERIFY(waitIndexed(w));
        auto* cal = w.findChild<CalendarPanel*>();
        QVERIFY(cal);
        QSignalSpy refreshed(cal, &CalendarPanel::refreshed);
        cal->refresh();
        QVERIFY(refreshed.wait(5000));
        QCOMPARE(cal->calendar()->dates(), (QSet<QDate>{QDate(2026, 11, 20), QDate(2026, 11, 21)}));
        QCOMPARE(cal->notesOn(QDate(2026, 11, 21)), QStringList{u"Trip.md"_s});

        // A day with a note opens it.
        cal->calendar()->setSelectedDate(QDate(2026, 11, 20));
        Q_EMIT cal->calendar()->clicked(QDate(2026, 11, 20));
        QCOMPARE(w.currentPath(), u"Daily/2026-11-20.md"_s);

        // An empty day offers a new note, created as a tagged daily note.
        VaultSettings s = w.vault()->settings();
        s.dailyNotesFolder = u"Daily"_s;
        QString err;
        QVERIFY(w.vault()->setSettings(s, &err));
        cal->calendar()->setSelectedDate(QDate(2026, 11, 23));
        QVERIFY(cal->createButton()->isVisibleTo(cal));
        cal->createButton()->click();
        QCOMPARE(w.currentPath(), u"Daily/2026-11-23.md"_s);
        QCOMPARE(test::readBytes(v.path(u"Daily/2026-11-23.md"_s)),
                 QByteArray("---\ntags:\n  - journal\n---\n"));
    }

    void homePageSections()
    {
        test::VaultCopy v(u"ui-vault"_s);
        const QDate today = QDate::currentDate();
        const QDate yearAgo = today.addYears(-1);
        // A multi-note project (hub + linked notes), a one-note project, a finished one.
        QVERIFY(test::writeBytes(
            v.path(u"Audio visualiser.md"_s),
            "---\nprimary: tech\nstatus: active\nsummary: FFT bars.\ntags: [tech, audio]\n---\n"
            "See [[FFT notes]] and [[Shaders]].\n"));
        QVERIFY(test::writeBytes(v.path(u"FFT notes.md"_s),
                                 "---\ntags: [tech, audio]\n---\n- [ ] window function\n"));
        QVERIFY(test::writeBytes(v.path(u"Shaders.md"_s), "Back to [[Audio visualiser]].\n- [x] done\n"));
        QVERIFY(test::writeBytes(v.path(u"Thesis.md"_s),
                                 "---\nstatus: draft\nprimary: personal\n---\n- [ ] read\n"));
        QVERIFY(test::writeBytes(v.path(u"Old.md"_s), "---\nstatus: done\n---\n"));
        QVERIFY(
            test::writeBytes(v.path(u"Bills.md"_s),
                             "- [ ] pay rent 📅 " + today.addDays(-1).toString(Qt::ISODate).toUtf8() + "\n"));
        QVERIFY(test::writeBytes(v.path(u"Daily/"_s + yearAgo.toString(Qt::ISODate) + u".md"_s),
                                 "---\ntags: [journal]\n---\n"));
        VaultSettings st = loadVaultSettings(v.root()).settings;
        st.tagProperties = {u"primary"_s};
        st.categoryTags = {u"tech"_s, u"personal"_s};
        QString err;
        QVERIFY(saveVaultSettings(v.root(), st, &err));

        MainWindow w;
        w.resize(1100, 800);
        w.show();
        QVERIFY(w.openVault(v.root()));
        QVERIFY(waitIndexed(w));
        action(w, u"&Home"_s)->trigger();
        QVERIFY(w.homeShown());
        auto* home = w.findChild<HomePage*>();
        QSignalSpy refreshed(home, &HomePage::refreshed);
        home->refresh();
        QVERIFY(refreshed.wait(5000));
        const HomeData& d = home->data();

        QStringList active;
        for (const NoteCard& c : d.active)
            active.append(c.title);
        active.sort();
        QCOMPARE(active, (QStringList{u"Audio visualiser"_s, u"Thesis"_s})); // "done" isn't active
        const NoteCard& hub = *std::find_if(d.active.begin(), d.active.end(),
                                            [](const NoteCard& c) { return c.title == u"Audio visualiser"; });
        QCOMPARE(hub.linkedNotes, 2); // FFT notes (linked) and Shaders (links back)
        QCOMPARE(hub.openTasks, 1);
        QCOMPARE(hub.doneTasks, 1);
        QCOMPARE(hub.summary, u"FFT bars."_s);
        QCOMPARE(home->tiles().size(), 2);

        QCOMPARE(d.due.size(), std::size_t(1));
        QCOMPARE(d.due.front().path, u"Bills.md"_s);
        QVERIFY(!d.recent.empty());
        QCOMPARE(d.categories.size(), std::size_t(2));
        QCOMPARE(d.categories[0].tag, u"tech"_s);
        QCOMPARE(d.categories[0].notes, 2);
        QCOMPARE(d.categories[0].topTags, QStringList{u"audio"_s});

        // Needs attention: notes without primary or summary, not journal notes.
        QStringList attention;
        for (const AttentionNote& a : d.attention)
            attention.append(a.path);
        // Home, Loop, Loop B, Second, FFT notes, Shaders, Thesis (no summary), Old, Bills; the 8 latest
        // listed.
        QCOMPARE(d.attentionTotal, 9);
        QCOMPARE(d.attention.size(), std::size_t(8));
        QVERIFY(!attention.contains(u"Audio visualiser.md"_s));
        for (const QString& p : attention)
            QVERIFY(!p.startsWith(u"Daily/"_s));
        if (yearAgo.day() == today.day()) {
            QCOMPARE(d.onThisDay.size(), std::size_t(1));
            QCOMPARE(d.onThisDay.front().date, yearAgo);
        }

        // The capture box appends to the inbox.
        home->captureBox()->setText(u"call the bank"_s);
        QTest::keyClick(home->captureBox(), Qt::Key_Return);
        QVERIFY(home->captureBox()->text().isEmpty());
        QVERIFY(test::readBytes(v.path(u"Inbox.md"_s)).contains("call the bank"));

        // A category link opens the tag pane on it; a tile opens its note.
        home->activate(u"tag:tech"_s);
        QCOMPARE(w.findChild<QTabWidget*>()->currentWidget(), w.findChild<TagPanel*>());
        for (NoteTile* t : home->tiles()) {
            if (t->path() == u"Thesis.md") t->click();
        }
        QCOMPARE(w.currentPath(), u"Thesis.md"_s);
        QVERIFY(!w.homeShown());

        // Sections can be turned off.
        st = w.vault()->settings();
        st.homeSections.removeAll(u"recent"_s);
        QVERIFY(w.vault()->setSettings(st, &err));
        w.showHome();
        QVERIFY(!home->section(u"recent"_s)->isVisible());
        QVERIFY(home->section(u"active"_s)->isVisible());
        QCOMPARE(HomePage::editedText(QDateTime(QDate(2026, 11, 20), QTime(9, 0)),
                                      QDateTime(QDate(2026, 11, 23), QTime(9, 0))),
                 u"edited 3 days ago"_s);
    }

    void exportToPdfAndHtml()
    {
        test::VaultCopy v(u"health-vault"_s);
        MainWindow w;
        QVERIFY(w.openVault(v.root()));
        QVERIFY(waitIndexed(w));
        QVERIFY(w.openNote(u"Hub.md"_s)); // embeds used.png and links to notes
        const auto before = test::snapshotTree(v.root());
        QTemporaryDir out;
        QString err;
        QVERIFY2(w.exportNote(out.path() + u"/hub.pdf"_s, true, &err), qPrintable(err));
        QVERIFY(test::readBytes(out.path() + u"/hub.pdf"_s).startsWith("%PDF"));
        QVERIFY2(w.exportNote(out.path() + u"/hub.html"_s, false, &err), qPrintable(err));
        const QByteArray html = test::readBytes(out.path() + u"/hub.html"_s);
        QVERIFY2(html.contains("data:image/png;base64,"), html.constData());
        QVERIFY(!html.contains("mn-img:"));
        QVERIFY(!html.contains("href=\"mn-"));
        QVERIFY(html.contains("<title>Hub</title>"));
        // Never into the vault's hidden folders; and the vault itself is untouched.
        QVERIFY(!w.exportNote(v.path(u".obsidian/hub.html"_s), false, &err));
        QCOMPARE(test::snapshotTree(v.root()), before);
    }

    void shortcutCheatsheet()
    {
        MainWindow w;
        ShortcutsDialog d(w.menuBar());
        auto find = [&d](const QString& action) -> QString {
            for (int i = 0; i < d.list()->topLevelItemCount(); ++i) {
                QTreeWidgetItem* g = d.list()->topLevelItem(i);
                for (int j = 0; j < g->childCount(); ++j) {
                    if (g->child(j)->text(0) == action && !g->child(j)->isHidden())
                        return g->child(j)->text(1);
                }
            }
            return {};
        };
        // Read from the menus, so these are the real shortcuts.
        QCOMPARE(find(u"Quick switcher"_s), QKeySequence(u"Ctrl+O"_s).toString(QKeySequence::NativeText));
        QCOMPARE(find(u"Insert timestamp"_s),
                 QKeySequence(u"Alt+Shift+T"_s).toString(QKeySequence::NativeText));
        QCOMPARE(find(u"Keyboard shortcuts"_s), u"F1"_s);
        QCOMPARE(find(u"Indent / outdent list item"_s), u"Tab / Shift+Tab"_s);
        // The filter matches actions and keys.
        d.filter()->setText(u"checkbox"_s);
        QVERIFY(!find(u"Toggle checkbox"_s).isEmpty());
        QVERIFY(find(u"Quick switcher"_s).isEmpty());
    }

    void timestampAndCheckboxMenuEntries()
    {
        test::VaultCopy v(u"ui-vault"_s);
        MainWindow w;
        QVERIFY(w.openVault(v.root()));
        QVERIFY(w.openNote(u"Second.md"_s));
        QTextCursor c(w.editor()->document()->findBlockByNumber(3)); // "One."
        w.editor()->setTextCursor(c);
        action(w, u"Insert time&stamp"_s)->trigger();
        QVERIFY(QRegularExpression(u"^\\d\\d:\\d\\d - One\\.$"_s)
                    .match(w.editor()->document()->findBlockByNumber(3).text())
                    .hasMatch());
        action(w, u"Toggle chec&kbox"_s)->trigger();
        QVERIFY(w.editor()->document()->findBlockByNumber(3).text().startsWith(u"- [ ] "_s));
    }
};

QTEST_MAIN(TestWriting)
#include "tst_writing.moc"
