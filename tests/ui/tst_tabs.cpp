#include "TestUtil.h"
#include "core/index/VaultService.h"
#include "ui/MainWindow.h"
#include "ui/NoteEditor.h"

#include <QAction>
#include <QApplication>
#include <QScrollBar>
#include <QSettings>
#include <QSignalSpy>
#include <QTabBar>

using namespace mn;
using namespace mn::ui;
using namespace Qt::StringLiterals;

namespace {

bool waitIndexed(MainWindow& w)
{
    QSignalSpy spy(w.vault(), &VaultService::indexingFinished);
    return spy.wait(15000);
}

} // namespace

class TestTabs : public QObject {
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

    void openingSwitchingAndLinks()
    {
        test::VaultCopy v(u"ui-vault"_s);
        MainWindow w;
        w.show();
        QVERIFY(w.openVault(v.root()));
        QVERIFY(waitIndexed(w));
        QTabBar* tabs = w.tabBar();
        QCOMPARE(tabs->tabText(0), u"Home"_s);
        QVERIFY(tabs->tabButton(0, QTabBar::RightSide) == nullptr); // home can't be closed

        QVERIFY(w.openNote(u"Home.md"_s));
        QVERIFY(w.openNote(u"Second.md"_s));
        QCOMPARE(w.openTabs(), (QStringList{u"Home.md"_s, u"Second.md"_s}));
        QCOMPARE(tabs->currentIndex(), 2);
        QVERIFY(w.openNote(u"Home.md"_s)); // already open: its tab is shown
        QCOMPARE(w.openTabs().size(), 2);
        QCOMPARE(tabs->currentIndex(), 1);

        // Following a link stays in the tab.
        Q_EMIT w.editor()->linkActivated(u"Loop"_s, false);
        QCOMPARE(w.currentPath(), u"Loop.md"_s);
        QCOMPARE(w.openTabs(), (QStringList{u"Loop.md"_s, u"Second.md"_s}));

        // Each tab keeps its cursor and reading mode.
        tabs->setCurrentIndex(2);
        QCOMPARE(w.currentPath(), u"Second.md"_s);
        QTextCursor c = w.editor()->textCursor();
        c.setPosition(12);
        w.editor()->setTextCursor(c);
        w.setReadingMode(true);
        tabs->setCurrentIndex(1);
        QCOMPARE(w.currentPath(), u"Loop.md"_s);
        QVERIFY(!w.readingMode());
        tabs->setCurrentIndex(2);
        QVERIFY(w.readingMode());
        QCOMPARE(w.editor()->textCursor().position(), 12);
        w.setReadingMode(false);

        // Switching tabs saves the note.
        w.editor()->moveCursor(QTextCursor::End);
        w.editor()->insertPlainText(u"\nedited"_s);
        tabs->setCurrentIndex(1);
        QVERIFY(test::readBytes(v.path(u"Second.md"_s)).endsWith("edited"));

        // The home tab, and back to the note behind it.
        tabs->setCurrentIndex(0);
        QVERIFY(w.homeShown());
        tabs->setCurrentIndex(1);
        QVERIFY(!w.homeShown());
        QCOMPARE(w.currentPath(), u"Loop.md"_s);

        // Home stays first when tabs are dragged.
        tabs->moveTab(2, 0);
        QCOMPARE(tabs->tabText(0), u"Home"_s);

        // Closing the current tab opens its neighbour; closing them all shows home.
        w.closeTab(tabs->currentIndex());
        QCOMPARE(w.openTabs(), QStringList{u"Second.md"_s});
        QCOMPARE(w.currentPath(), u"Second.md"_s);
        w.closeTab(0); // not closable
        QCOMPARE(tabs->count(), 2);
        w.closeTab(1);
        QVERIFY(w.homeShown());
        QVERIFY(w.openTabs().isEmpty());
    }

    void undoHistoryStaysWithItsTab()
    {
        test::VaultCopy v(u"ui-vault"_s);
        MainWindow w;
        QVERIFY(w.openVault(v.root()));
        QVERIFY(waitIndexed(w));
        QVERIFY(w.openNote(u"Loop.md"_s));
        const QString loop = w.editor()->bufferText();
        w.editor()->moveCursor(QTextCursor::End);
        QTest::keyClicks(w.editor(), u" first"_s);
        QVERIFY(w.openNote(u"Second.md"_s));
        const QString second = w.editor()->bufferText();
        w.editor()->moveCursor(QTextCursor::End);
        QTest::keyClicks(w.editor(), u" other"_s);

        // Back in the first tab: its edit is there, saved, and can be undone.
        w.tabBar()->setCurrentIndex(1);
        QCOMPARE(w.editor()->bufferText(), loop + u" first"_s);
        QVERIFY(test::readBytes(v.path(u"Second.md"_s)).endsWith(" other")); // saved on switching
        QVERIFY(w.editor()->document()->isUndoAvailable());
        w.editor()->undo();
        QCOMPARE(w.editor()->bufferText(), loop);
        w.tabBar()->setCurrentIndex(2);
        w.editor()->undo();
        QCOMPARE(w.editor()->bufferText(), second);
        w.editor()->redo();
        QCOMPARE(w.editor()->bufferText(), second + u" other"_s);

        // Changed by another program while in the background: reloaded on return, undoably.
        QVERIFY(test::writeBytes(v.path(u"Loop.md"_s), "Changed outside.\n"));
        w.tabBar()->setCurrentIndex(1);
        QCOMPARE(w.editor()->bufferText(), u"Changed outside.\n"_s);
        w.editor()->undo();
        QCOMPARE(w.editor()->bufferText(), loop);

        // Closing a tab lets go of its document; the other tab is unaffected.
        w.closeTab(1);
        QCOMPARE(w.currentPath(), u"Second.md"_s);
        QCOMPARE(w.editor()->bufferText(), second + u" other"_s);
        QVERIFY(w.editor()->document()->isUndoAvailable());
    }

    void tabsAreRestored()
    {
        test::VaultCopy v(u"ui-vault"_s);
        {
            MainWindow w;
            QVERIFY(w.openVault(v.root()));
            QVERIFY(waitIndexed(w));
            QVERIFY(w.openNote(u"Loop.md"_s));
            QVERIFY(w.openNote(u"Second.md"_s));
            QVERIFY(w.openNote(u"Home.md"_s));
            w.tabBar()->setCurrentIndex(2);
        }
        QFile::remove(v.path(u"Home.md"_s)); // gone since: its tab isn't restored
        MainWindow w;
        QVERIFY(w.openVault(v.root()));
        QVERIFY(waitIndexed(w));
        QTRY_COMPARE(w.openTabs(), (QStringList{u"Loop.md"_s, u"Second.md"_s}));
        QCOMPARE(w.currentPath(), u"Second.md"_s);
        QCOMPARE(w.tabBar()->currentIndex(), 2);
    }

    void tabsFollowRenames()
    {
        test::VaultCopy v(u"ui-vault"_s);
        MainWindow w;
        QVERIFY(w.openVault(v.root()));
        QVERIFY(waitIndexed(w));
        QVERIFY(w.openNote(u"Loop.md"_s));
        QVERIFY(w.openNote(u"Second.md"_s));
        w.renamePath(u"Loop.md"_s, u"Cycle.md"_s, false);
        QTRY_COMPARE(w.openTabs(), (QStringList{u"Cycle.md"_s, u"Second.md"_s}));
        QCOMPARE(w.tabBar()->tabText(1), u"Cycle"_s);
    }
};

QTEST_MAIN(TestTabs)
#include "tst_tabs.moc"
