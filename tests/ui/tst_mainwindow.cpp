#include "TestUtil.h"
#include "core/index/VaultService.h"
#include "core/vault/TextFile.h"
#include "ui/Appearance.h"
#include "ui/HealthDialog.h"
#include "ui/HtmlRenderer.h"
#include "ui/MainWindow.h"
#include "ui/MarkdownHighlighter.h"
#include "ui/MathRenderer.h"
#include "ui/NewNoteDialog.h"
#include "ui/NoteEditor.h"
#include "ui/Panels.h"
#include "ui/QuickSwitcher.h"
#include "ui/ReadingView.h"
#include "ui/Theme.h"

#include <QAbstractTextDocumentLayout>
#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QFontDatabase>
#include <QLineEdit>
#include <QMimeData>
#include <QSettings>
#include <QSignalSpy>
#include <QSplitter>
#include <QStandardPaths>
#include <QTextBlock>
#include <QTextDocument>
#include <QTextLayout>
#include <QTimer>
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

} // namespace

class TestMainWindow : public QObject {
    Q_OBJECT
    QTemporaryDir settingsDir_;
    QPalette desktopPalette_;

private Q_SLOTS:
    void initTestCase()
    {
        // A fresh settings location per run: nothing leaks between runs or into the user's config.
        QVERIFY(settingsDir_.isValid());
        QSettings::setPath(QSettings::NativeFormat, QSettings::UserScope, settingsDir_.path());
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDir_.path());
        desktopPalette_ = QApplication::palette();
#ifdef Q_OS_LINUX
        // Index caches of the temporary vaults go to the per-run directory too.
        qputenv("XDG_CACHE_HOME", (settingsDir_.path() + u"/cache"_s).toUtf8());
        qputenv("XDG_CONFIG_HOME", (settingsDir_.path() + u"/config"_s).toUtf8());
#else
        QStandardPaths::setTestModeEnabled(true);
#endif
        QCoreApplication::setOrganizationName(u"mn-tests"_s);
        QCoreApplication::setApplicationName(u"mn-tests"_s);
    }

    void openingNotesNeverWrites()
    {
        test::VaultCopy v(u"encoding"_s);
        MainWindow w;
        QVERIFY(w.openVault(v.root()));
        QVERIFY(waitIndexed(w));
        auto before = test::snapshotTree(v.root());
        before.remove(u".markdown-notes/settings.json"_s);
        for (const QString& n :
             {u"bom.md"_s, u"crlf.md"_s, u"mixed.md"_s, u"no-trailing-newline.md"_s, u"nbsp.md"_s,
              u"empty.md"_s, u"double-bom.md"_s, u"lone-cr.md"_s, u"invalid-utf8.md"_s, u"binary.md"_s}) {
            QVERIFY2(w.openNote(n), qPrintable(n));
            QVERIFY2(!w.isDirty(), qPrintable(n));
            w.setReadingMode(true);
            w.setReadingMode(false);
            QVERIFY(w.saveNow());
        }
        w.close();
        auto after = test::snapshotTree(v.root());
        after.remove(u".markdown-notes/settings.json"_s);
        QCOMPARE(after, before);
    }

    void editorRoundTripsSpecialCharacters()
    {
        test::VaultCopy v(u"encoding"_s);
        MainWindow w;
        QVERIFY(w.openVault(v.root()));
        QVERIFY(w.openNote(u"nbsp.md"_s));
        QVERIFY(!w.isReadOnly());
        QVERIFY(w.editor()->bufferText().contains(QChar(0xA0)));
        QVERIFY(w.editor()->bufferText().contains(QChar(0x2028)));
        // Lone CR, invalid UTF-8 and binary files open read-only.
        QVERIFY(w.openNote(u"lone-cr.md"_s));
        QVERIFY(w.isReadOnly());
        QVERIFY(w.openNote(u"invalid-utf8.md"_s));
        QVERIFY(w.isReadOnly());
        QVERIFY(w.openNote(u"binary.md"_s));
        QVERIFY(w.isReadOnly());
    }

    void typingAutosavesKeepingFormat()
    {
        test::VaultCopy v(u"encoding"_s);
        MainWindow w;
        QVERIFY(w.openVault(v.root()));
        QVERIFY(w.openNote(u"crlf.md"_s));
        QTextCursor c = w.editor()->textCursor();
        c.movePosition(QTextCursor::End);
        w.editor()->setTextCursor(c);
        QTest::keyClicks(w.editor(), u"added"_s);
        QVERIFY(w.isDirty());
        QTRY_VERIFY_WITH_TIMEOUT(!w.isDirty(), 5000);
        QCOMPARE(test::readBytes(v.path(u"crlf.md"_s)), QByteArray("# CRLF\r\nLine two\r\nadded"));

        QVERIFY(w.openNote(u"bom.md"_s));
        c = w.editor()->textCursor();
        c.movePosition(QTextCursor::End);
        w.editor()->setTextCursor(c);
        QTest::keyClicks(w.editor(), u"x"_s);
        QVERIFY(w.saveNow());
        QCOMPARE(test::readBytes(v.path(u"bom.md"_s)), QByteArray("\xEF\xBB\xBF# BOM\nText\nx"));

        // Undoing back to the original means nothing is written.
        QVERIFY(w.openNote(u"no-trailing-newline.md"_s));
        const FileStamp before = statFile(v.path(u"no-trailing-newline.md"_s));
        QTest::keyClicks(w.editor(), u"z"_s);
        w.editor()->undo();
        QVERIFY(!w.isDirty());
        QVERIFY(w.saveNow());
        QCOMPARE(statFile(v.path(u"no-trailing-newline.md"_s)), before);
    }

    void shiftEnterInsertsARealNewline()
    {
        test::VaultCopy v(u"encoding"_s);
        MainWindow w;
        QVERIFY(w.openVault(v.root()));
        QVERIFY(w.openNote(u"empty.md"_s));
        QTest::keyClicks(w.editor(), u"a"_s);
        QTest::keyClick(w.editor(), Qt::Key_Return, Qt::ShiftModifier);
        QTest::keyClicks(w.editor(), u"b"_s);
        QCOMPARE(w.editor()->bufferText(), u"a\nb"_s);
    }

    void listContinuation()
    {
        test::VaultCopy v(u"encoding"_s);
        MainWindow w;
        QVERIFY(w.openVault(v.root()));
        QVERIFY(w.openNote(u"empty.md"_s));
        QTest::keyClicks(w.editor(), u"- [ ] one"_s);
        QTest::keyClick(w.editor(), Qt::Key_Return);
        QTest::keyClicks(w.editor(), u"two"_s);
        QTest::keyClick(w.editor(), Qt::Key_Return);
        QTest::keyClick(w.editor(), Qt::Key_Return);
        QCOMPARE(w.editor()->bufferText(), u"- [ ] one\n- [ ] two\n"_s);
    }

    void externalChangeReloadsCleanBuffer()
    {
        test::VaultCopy v(u"ui-vault"_s);
        MainWindow w;
        QVERIFY(w.openVault(v.root()));
        QVERIFY(waitIndexed(w));
        QVERIFY(w.openNote(u"Second.md"_s));
        QTest::qWait(50);
        QVERIFY(writeFileAtomic(v.path(u"Second.md"_s), "# Changed elsewhere\n").ok);
        QTRY_COMPARE_WITH_TIMEOUT(w.editor()->bufferText(), u"# Changed elsewhere\n"_s, 5000);
        QVERIFY(!w.isDirty());
    }

    void writeFailureKeepsTheBuffer()
    {
#ifdef Q_OS_UNIX
        test::VaultCopy v(u"ui-vault"_s);
        MainWindow w;
        QVERIFY(w.openVault(v.root()));
        QVERIFY(w.openNote(u"Second.md"_s));
        // Make the folder read-only so QSaveFile can't create its temp file.
        QFile::setPermissions(v.root(), QFile::ReadOwner | QFile::ExeOwner);
        QTimer::singleShot(200, [] {
            for (QWidget* top : QApplication::topLevelWidgets()) {
                if (top->inherits("QMessageBox")) top->close();
            }
        });
        w.editor()->moveCursor(QTextCursor::End);
        QTest::keyClicks(w.editor(), u"typed"_s);
        QVERIFY(!w.saveNow());
        QVERIFY(w.isDirty());
        QVERIFY(w.editor()->bufferText().endsWith(u"typed"_s));
        QFile::setPermissions(v.root(), QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
        QVERIFY(w.saveNow());
        QVERIFY(test::readBytes(v.path(u"Second.md"_s)).endsWith("typed"));
#endif
    }

    void followingUnresolvedLinkCreatesNote()
    {
        test::VaultCopy v(u"ui-vault"_s);
        MainWindow w;
        QVERIFY(w.openVault(v.root()));
        QVERIFY(waitIndexed(w));
        QVERIFY(w.openNote(u"Home.md"_s));
        Q_EMIT w.editor()->linkActivated(u"Missing note"_s, false);
        QCOMPARE(w.currentPath(), u"Missing note.md"_s);
        QVERIFY(QFile::exists(v.path(u"Missing note.md"_s)));
        QCOMPARE(test::readBytes(v.path(u"Missing note.md"_s)), QByteArray());
        Q_EMIT w.editor()->linkActivated(u"Second#Part two"_s, false);
        QCOMPARE(w.currentPath(), u"Second.md"_s);
        QCOMPARE(w.editor()->textCursor().blockNumber(), 5);
    }

    void creatingNotesNeverWritesIntoHiddenFolders()
    {
        test::VaultCopy v(u"ui-vault"_s);
        MainWindow w;
        QVERIFY(w.openVault(v.root()));
        QVERIFY(waitIndexed(w));
        QVERIFY(w.openNote(u"Home.md"_s));
        auto closeBoxes = [] {
            for (QWidget* top : QApplication::topLevelWidgets()) {
                if (top->inherits("QMessageBox")) top->close();
            }
        };
        QTimer::singleShot(200, closeBoxes);
        Q_EMIT w.switcher()->createRequested(u".obsidian/evil"_s);
        QTimer::singleShot(200, closeBoxes);
        Q_EMIT w.switcher()->createRequested(u"../outside"_s);
        QVERIFY(!QFile::exists(v.path(u".obsidian/evil.md"_s)));
        QVERIFY(!QFile::exists(v.root() + u"/../outside.md"_s));
        QCOMPARE(w.currentPath(), u"Home.md"_s);
    }

    void renameUpdatesLinksAndFollowsOpenNote()
    {
        test::VaultCopy v(u"ui-vault"_s);
        MainWindow w;
        QVERIFY(w.openVault(v.root()));
        QVERIFY(waitIndexed(w));
        QVERIFY(w.openNote(u"Second.md"_s));
        w.renamePath(u"Second.md"_s, u"Renamed/Third.md"_s, false);
        QTRY_COMPARE_WITH_TIMEOUT(w.currentPath(), u"Renamed/Third.md"_s, 5000);
        const QByteArray home = test::readBytes(v.path(u"Home.md"_s));
        QVERIFY(home.contains("[[Third]], [[Missing note]], [[Third#Part two|part two]]"));
        QVERIFY(home.contains("![[Third#Part two]]"));
    }

    void readingViewRendersEverything()
    {
        test::VaultCopy v(u"ui-vault"_s);
        MainWindow w;
        w.resize(1000, 800);
        w.show();
        QVERIFY(w.openVault(v.root()));
        QVERIFY(waitIndexed(w));
        QVERIFY(w.openNote(u"Home.md"_s));
        w.setReadingMode(true);
        QVERIFY(w.readingMode());
        const QString text = w.readingView()->toPlainText();
        QVERIFY(text.contains(u"A tip"_s));
        QVERIFY(text.contains(u"Folded"_s));
        QVERIFY(!text.contains(u"Hidden body"_s));
        QVERIFY(!text.contains(u"hidden comment"_s));
        QVERIFY(text.contains(u"Two with a block."_s)); // heading embed
        QVERIFY(!text.contains(u"One."_s));
        QVERIFY(text.contains(u"☐ open task"_s));
        QVERIFY(text.contains(u"☑ done task"_s));
        QVERIFY(!text.contains(u"[!tip]"_s));
        QVERIFY(!text.contains(u"$$"_s)); // math became images
        QVERIFY(w.readingView()->lastRenderMs() < 1000);
        QVERIFY(MathRenderer::instance().cacheSize() >= 2);

        // Embeds detect cycles instead of recursing forever.
        QVERIFY(w.openNote(u"Loop.md"_s));
        QVERIFY(w.readingView()->toPlainText().contains(u"embeds itself"_s));
    }

    void headingFontSizesInPlainTextEdit()
    {
        // M0 spike: QPlainTextEdit lays out blocks with larger heading fonts.
        test::VaultCopy v(u"ui-vault"_s);
        MainWindow w;
        w.resize(1000, 800);
        w.show();
        QVERIFY(w.openVault(v.root()));
        QVERIFY(w.openNote(u"Second.md"_s));
        QVERIFY(QTest::qWaitForWindowExposed(&w));
        QTextDocument* doc = w.editor()->document();
        const QTextBlock h1 = doc->findBlockByNumber(0);
        const QTextBlock body = doc->findBlockByNumber(3);
        const qreal hh = w.editor()->document()->documentLayout()->blockBoundingRect(h1).height();
        const qreal bh = w.editor()->document()->documentLayout()->blockBoundingRect(body).height();
        QVERIFY2(hh > bh * 1.3, qPrintable(QString::number(hh) + u" vs "_s + QString::number(bh)));
    }

    void typingRepaintsOnlyTheEditedLine()
    {
        // Regression: QSyntaxHighlighter used to make every keystroke relayout and
        // repaint the whole viewport (see NoteEditor.cpp, BlockLocalLayout).
        test::VaultCopy v(u"ui-vault"_s);
        MainWindow w;
        w.resize(1000, 800);
        w.show();
        QVERIFY(w.openVault(v.root()));
        QVERIFY(w.openNote(u"Home.md"_s));
        QVERIFY(QTest::qWaitForWindowExposed(&w));
        QTextCursor c(w.editor()->document()->findBlockByNumber(6)); // "Links: [[Second]], ..."
        c.movePosition(QTextCursor::EndOfBlock);
        w.editor()->setTextCursor(c);
        QCoreApplication::processEvents();
        int fullUpdates = 0;
        auto conn = connect(w.editor()->document()->documentLayout(), &QAbstractTextDocumentLayout::update,
                            this, [&](const QRectF& r) {
                                if (r.height() > 1e6) ++fullUpdates;
                            });
        QTest::keyClicks(w.editor(), u"abc"_s);
        QCoreApplication::processEvents();
        disconnect(conn);
        QCOMPARE(fullUpdates, 0);
        // Structural edits still lay out correctly.
        const int blocks = w.editor()->document()->blockCount();
        QTest::keyClick(w.editor(), Qt::Key_Return, Qt::ShiftModifier);
        QCOMPARE(w.editor()->document()->blockCount(), blocks + 1);
        QTest::keyClick(w.editor(), Qt::Key_Backspace);
        QCOMPARE(w.editor()->document()->blockCount(), blocks);
        QVERIFY(w.editor()->bufferText().contains(u"part two]].abc"_s));
    }

    void pastingAnImageSavesItToTheAttachmentFolder()
    {
        test::VaultCopy v(u"ui-vault"_s);
        MainWindow w;
        QVERIFY(w.openVault(v.root()));
        QVERIFY(waitIndexed(w));
        VaultSettings s = w.vault()->settings();
        s.attachmentFolderPath = u"./assets"_s;
        QString err;
        QVERIFY(w.vault()->setSettings(s, &err));
        QVERIFY(w.openNote(u"Second.md"_s));
        w.editor()->moveCursor(QTextCursor::End);
        QImage img(8, 8, QImage::Format_RGB32);
        img.fill(Qt::red);
        auto* mime = new QMimeData;
        mime->setImageData(img);
        QApplication::clipboard()->setMimeData(mime);
        w.editor()->paste();
        const QStringList pasted = QDir(v.path(u"assets"_s)).entryList({u"Pasted image *.png"_s});
        QCOMPARE(pasted.size(), 1);
        QVERIFY(w.editor()->bufferText().contains(u"![["_s + pasted[0] + u"]]"_s));
        QVERIFY(!QImage(v.path(u"assets/"_s + pasted[0])).isNull());
    }

    void appearanceRoundTripsAndApplies()
    {
        Appearance a;
        a.theme = Appearance::ThemeMode::Dark;
        a.accent = QColor(0x12, 0x80, 0x40);
        a.editorSize = 15;
        a.monoFamily = QFontDatabase::systemFont(QFontDatabase::FixedFont).family();
        a.readableLineLength = true;
        a.lineLengthChars = 40;
        a.save();
        QCOMPARE(Appearance::load(), a);

        const QPalette before = desktopPalette_;
        MainWindow w;
        w.resize(1400, 800);
        w.show();
        QVERIFY(QTest::qWaitForWindowExposed(&w));
        QVERIFY(QApplication::palette().color(QPalette::Base).lightness() < 128);
        QCOMPARE(Theme::fromPalette(QApplication::palette()).accent, a.accent);
        QCOMPARE(w.editor()->font().pointSize(), 15);
        // A narrowed column: the text area is well below the editor's width.
        QVERIFY(w.editor()->viewport()->width() < w.editor()->width() - 200);

        // Back to defaults: the desktop's palette comes back and the full width is used.
        w.applyLook(Appearance{});
        QCoreApplication::processEvents();
        QCOMPARE(QApplication::palette().color(QPalette::Base), before.color(QPalette::Base));
        QVERIFY(!Theme::fromPalette(QApplication::palette()).accent.isValid() ||
                Theme::fromPalette(QApplication::palette()).accent != a.accent);
        QVERIFY(w.editor()->viewport()->width() > w.editor()->width() - 60);
        Appearance{}.save();
    }

    void linkSuggestionsInTheEditor()
    {
        test::VaultCopy v(u"ui-vault"_s);
        MainWindow w;
        QVERIFY(w.openVault(v.root()));
        QVERIFY(waitIndexed(w));
        QVERIFY(w.openNote(u"Home.md"_s));
        w.editor()->moveCursor(QTextCursor::End);
        QTest::keyClicks(w.editor(), u"Remember the second idea"_s);
        w.editor()->scanLinkSuggestions();
        QCOMPARE(w.editor()->suggestionCount(), 1);
        QVERIFY(w.editor()->acceptLinkSuggestion());
        QVERIFY(w.editor()->bufferText().endsWith(u"Remember the [[Second|second]] idea"_s));
        QTest::keyClicks(w.editor(), u" and Second again"_s);
        w.editor()->scanLinkSuggestions();
        QCOMPARE(w.editor()->suggestionCount(), 1);
        QTest::keyClick(w.editor(), Qt::Key_Escape); // dismissed for this note
        w.editor()->scanLinkSuggestions();
        QCOMPARE(w.editor()->suggestionCount(), 0);
        w.editor()->setLinkSuggestions(false);
        QTest::keyClicks(w.editor(), u" Loop"_s);
        w.editor()->scanLinkSuggestions();
        QCOMPARE(w.editor()->suggestionCount(), 0);
    }

    void healthDialogAndRelatedNotes()
    {
        test::VaultCopy v(u"health-vault"_s);
        MainWindow w;
        QVERIFY(w.openVault(v.root()));
        QVERIFY(waitIndexed(w));
        HealthDialog dlg(w.vault());
        dlg.run();
        QTRY_VERIFY_WITH_TIMEOUT(dlg.sectionCount(0) >= 0, 10000);
        QCOMPARE(dlg.sectionCount(0), 1); // broken links
        QCOMPARE(dlg.sectionCount(3), 1); // unused attachments
        QCOMPARE(dlg.sectionCount(4), 2); // tag groups
        QCOMPARE(dlg.sectionCount(5), 1); // near-duplicate notes
    }

    void tickingAndDeletingTasks()
    {
        test::VaultCopy v(u"ui-vault"_s);
        MainWindow w;
        QVERIFY(w.openVault(v.root()));
        QVERIFY(waitIndexed(w));
        // Note not open: the file is edited in place.
        QVERIFY(w.openNote(u"Second.md"_s));
        TaskInfo open{u"Home.md"_s, 15, false, u"open task"_s, {}};
        QVERIFY(w.editTask(open, TaskAction::MarkDone, false));
        QVERIFY(test::readBytes(v.path(u"Home.md"_s)).contains("- [x] open task\n- [x] done task"));
        // Note open: the edit goes through the editor (undoable) and is saved.
        QVERIFY(w.openNote(u"Home.md"_s));
        TaskInfo done{u"Home.md"_s, 16, true, u"done task"_s, {}};
        QVERIFY(w.editTask(done, TaskAction::Delete, false));
        QVERIFY(!w.editor()->bufferText().contains(u"done task"_s));
        QVERIFY(!test::readBytes(v.path(u"Home.md"_s)).contains("done task"));
        w.editor()->undo();
        QVERIFY(w.editor()->bufferText().contains(u"- [x] done task"_s));
        // A stale task is refused, nothing is written.
        const QByteArray before = test::readBytes(v.path(u"Second.md"_s));
        QTimer::singleShot(200, [] {
            for (QWidget* top : QApplication::topLevelWidgets()) {
                if (top->inherits("QMessageBox")) top->close();
            }
        });
        QVERIFY(!w.editTask({u"Second.md"_s, 1, false, u"no such task"_s, {}}, TaskAction::MarkDone, false));
        QCOMPARE(test::readBytes(v.path(u"Second.md"_s)), before);
    }

    void panesCanBeHiddenAndRestored()
    {
        MainWindow w;
        w.resize(1300, 800);
        w.show();
        QVERIFY(QTest::qWaitForWindowExposed(&w));
        w.resetLayout();
        QVERIFY(w.paneVisible(0) && w.paneVisible(2));
        w.setPaneVisible(2, false);
        QVERIFY(!w.paneVisible(2));
        w.setPaneVisible(2, true);
        QVERIFY(w.paneVisible(2));
        // Collapsed by dragging the splitter: the View menu still brings it back.
        auto* split = w.findChild<QSplitter*>();
        QList<int> sizes = split->sizes();
        sizes[1] += sizes[2];
        sizes[2] = 0;
        split->setSizes(sizes);
        QVERIFY(!w.paneVisible(2));
        w.setPaneVisible(2, true);
        QVERIFY(split->sizes().value(2) >= 160);
        w.setPaneVisible(0, false);
        QVERIFY(!w.paneVisible(0));
        w.resetLayout();
        QVERIFY(w.paneVisible(0) && w.paneVisible(2));
    }

    void tagPaneSorting()
    {
        QTemporaryDir dir;
        QVERIFY(test::writeBytes(dir.path() + u"/a.md"_s, "#alpha #gamma\n"));
        QVERIFY(test::writeBytes(dir.path() + u"/b.md"_s, "#alpha #gamma\n"));
        QVERIFY(test::writeBytes(dir.path() + u"/c.md"_s, "#alpha #beta\n"));
        VaultService svc;
        QSignalSpy spy(&svc, &VaultService::indexingFinished);
        QString err;
        QTemporaryDir cache;
        QVERIFY(svc.open(dir.path(), &err, cache.path() + u"/index.sqlite"_s));
        QVERIFY(spy.wait(10000));
        TagPanel panel(&svc);
        panel.refresh();
        auto* tree = panel.findChild<QTreeWidget*>();
        auto* sort = panel.findChild<QComboBox*>();
        auto order = [&] {
            QStringList out;
            for (int i = 0; i < tree->topLevelItemCount(); ++i)
                out.append(tree->topLevelItem(i)->text(0));
            return out;
        };
        sort->setCurrentIndex(0);
        QCOMPARE(order(), (QStringList{u"#alpha"_s, u"#beta"_s, u"#gamma"_s}));
        sort->setCurrentIndex(1);
        QCOMPARE(order(), (QStringList{u"#alpha"_s, u"#gamma"_s, u"#beta"_s}));
        sort->setCurrentIndex(2);
        QCOMPARE(order(), (QStringList{u"#beta"_s, u"#gamma"_s, u"#alpha"_s}));
        sort->setCurrentIndex(0);
    }

    void pinnedTagsComeFirst()
    {
        QTemporaryDir dir;
        QVERIFY(test::writeBytes(dir.path() + u"/a.md"_s, "#alpha #gamma #tech/ai\n"));
        QVERIFY(test::writeBytes(dir.path() + u"/b.md"_s, "#alpha #gamma\n"));
        QVERIFY(test::writeBytes(dir.path() + u"/c.md"_s, "#alpha #beta\n"));
        VaultService svc;
        QSignalSpy spy(&svc, &VaultService::indexingFinished);
        QString err;
        QTemporaryDir cache;
        QVERIFY(svc.open(dir.path(), &err, cache.path() + u"/index.sqlite"_s));
        QVERIFY(spy.wait(10000));
        TagPanel panel(&svc);
        panel.refresh();
        auto* tree = panel.findChild<QTreeWidget*>();
        auto* sort = panel.findChild<QComboBox*>();
        auto order = [&] {
            QStringList out;
            for (int i = 0; i < tree->topLevelItemCount(); ++i)
                out.append(tree->topLevelItem(i)->text(0));
            return out;
        };
        sort->setCurrentIndex(0);
        panel.setPinned(u"gamma"_s, true);
        panel.setPinned(u"Beta"_s, true);
        QCOMPARE(order(), (QStringList{u"#gamma"_s, u"#beta"_s, u"#alpha"_s, u"#tech"_s}));
        // Pins stay on top in pin order whichever way the rest is sorted.
        sort->setCurrentIndex(1);
        QCOMPARE(order(), (QStringList{u"#gamma"_s, u"#beta"_s, u"#alpha"_s, u"#tech"_s}));
        sort->setCurrentIndex(2);
        QCOMPARE(order(), (QStringList{u"#gamma"_s, u"#beta"_s, u"#tech"_s, u"#alpha"_s}));
        sort->setCurrentIndex(0);
        panel.movePinned(u"beta"_s, -1);
        QCOMPARE(order().mid(0, 2), (QStringList{u"#beta"_s, u"#gamma"_s}));
        // A nested tag gets its own top-level entry; its place in the tree stays.
        panel.setPinned(u"tech/ai"_s, true);
        QCOMPARE(order(), (QStringList{u"#beta"_s, u"#gamma"_s, u"#tech/ai"_s, u"#alpha"_s, u"#tech"_s}));
        QCOMPARE(tree->topLevelItem(4)->child(0)->text(0), u"#ai"_s);
        panel.setPinned(u"gamma"_s, false);
        QCOMPARE(order(), (QStringList{u"#beta"_s, u"#tech/ai"_s, u"#alpha"_s, u"#gamma"_s, u"#tech"_s}));
        // Saved in the vault's settings, so they survive a restart.
        QCOMPARE(loadVaultSettings(dir.path()).settings.pinnedTags, (QStringList{u"Beta"_s, u"tech/ai"_s}));
    }

    void searchByPassage()
    {
        test::VaultCopy v(u"passages-vault"_s);
        VaultService svc;
        QSignalSpy spy(&svc, &VaultService::indexingFinished);
        QString err;
        QVERIFY(svc.open(v.root(), &err, v.cacheDir() + u"/index.sqlite"_s));
        QVERIFY(spy.wait(10000));
        SearchPanel panel(&svc);
        const auto combos = panel.findChildren<QComboBox*>();
        QCOMPARE(combos.size(), 2);
        QComboBox* mode = combos[0];
        QComboBox* sort = combos[1];
        mode->setCurrentIndex(1);
        sort->setCurrentIndex(2); // oldest first
        auto* results = panel.findChild<QTreeWidget*>();
        QSignalSpy open(&panel, &SearchPanel::openRequested);
        panel.focusSearch(u"caffeine"_s);
        QTRY_COMPARE(results->topLevelItemCount(), 4);
        // A timeline: the daily notes by the date in their names.
        QCOMPARE(results->topLevelItem(0)->toolTip(0), u"Daily/2024-03-14.md"_s);
        QCOMPARE(results->topLevelItem(1)->toolTip(0), u"Daily/2024-06-02.md"_s);
        QCOMPARE(results->topLevelItem(2)->toolTip(0), u"Daily/2025-01-20.md"_s);
        // Clicking opens the note at the passage.
        Q_EMIT results->itemClicked(results->topLevelItem(2), 0);
        QCOMPARE(open.size(), 1);
        QCOMPARE(open[0][1].toInt(), 5); // after the frontmatter and the budget paragraph
        sort->setCurrentIndex(1);        // newest first
        QCOMPARE(results->topLevelItem(results->topLevelItemCount() - 1)->toolTip(0),
                 u"Daily/2024-03-14.md"_s);
        mode->setCurrentIndex(0);
        sort->setCurrentIndex(0);
    }

    void relatedPassagesFollowTheCursor()
    {
        test::VaultCopy v(u"passages-vault"_s);
        MainWindow w;
        w.show();
        QVERIFY(QTest::qWaitForWindowExposed(&w));
        QVERIFY(w.openVault(v.root()));
        QVERIFY(w.openNote(u"Daily/2024-03-14.md"_s));
        auto* panel = w.findChild<BacklinksPanel*>();
        auto* tree = panel->findChild<QTreeWidget*>();
        auto section = [&]() -> QTreeWidgetItem* {
            for (int i = 0; i < tree->topLevelItemCount(); ++i) {
                if (tree->topLevelItem(i)->text(0).startsWith(u"Related to this paragraph"_s))
                    return tree->topLevelItem(i);
            }
            return nullptr;
        };
        QTextCursor c(w.editor()->document()->findBlockByNumber(5)); // the Rust paragraph
        w.editor()->setTextCursor(c);
        QTRY_VERIFY_WITH_TIMEOUT(section() && section()->childCount() > 0 &&
                                     section()->child(0)->data(0, Qt::UserRole + 1).toString() ==
                                         u"Rust notes.md"_s,
                                 5000);
        c = QTextCursor(w.editor()->document()->findBlockByNumber(3)); // the sleep paragraph
        w.editor()->setTextCursor(c);
        QTRY_VERIFY_WITH_TIMEOUT(section()->childCount() > 0 &&
                                     section()->child(0)->data(0, Qt::UserRole + 1).toString() !=
                                         u"Rust notes.md"_s,
                                 5000);
        // Looking never changes the note.
        QCOMPARE(test::readBytes(v.path(u"Daily/2024-03-14.md"_s)),
                 test::readBytes(test::fixture(u"passages-vault/Daily/2024-03-14.md"_s)));
    }

    void tagFilter()
    {
        QTemporaryDir dir;
        QVERIFY(test::writeBytes(dir.path() + u"/a.md"_s, "#alpha #gamma #tech/ai #tech/web\n"));
        QVERIFY(test::writeBytes(dir.path() + u"/b.md"_s, "#beta\n"));
        VaultService svc;
        QSignalSpy spy(&svc, &VaultService::indexingFinished);
        QString err;
        QTemporaryDir cache;
        QVERIFY(svc.open(dir.path(), &err, cache.path() + u"/index.sqlite"_s));
        QVERIFY(spy.wait(10000));
        TagPanel panel(&svc);
        panel.refresh();
        auto* tree = panel.findChild<QTreeWidget*>();
        auto shown = [&] {
            QStringList out;
            for (QTreeWidgetItemIterator it(tree, QTreeWidgetItemIterator::NotHidden); *it; ++it)
                out.append((*it)->data(0, Qt::UserRole + 1).toString());
            return out;
        };
        panel.setFilter(u"ai"_s);
        QCOMPARE(shown(), (QStringList{u"tech"_s, u"tech/ai"_s})); // the parent stays, opened
        QVERIFY(tree->topLevelItem(3)->isExpanded());
        panel.setFilter(u"#ALP"_s);
        QCOMPARE(shown(), (QStringList{u"alpha"_s}));
        panel.setFilter(u"zzz"_s);
        QVERIFY(shown().isEmpty());
        panel.setFilter(QString());
        QCOMPARE(shown().size(), 6);
        // The filter survives a refresh (the index changing underneath).
        panel.setFilter(u"bet"_s);
        panel.refresh();
        QCOMPARE(shown(), (QStringList{u"beta"_s}));
        panel.setFilter(QString());
    }

    void newNoteAsksForCategories()
    {
        test::VaultCopy v(u"ui-vault"_s);
        VaultSettings st = loadVaultSettings(v.root()).settings;
        st.tagProperties = {u"primary"_s, u"secondary"_s};
        st.categoryTags = {u"tech"_s, u"personal"_s, u"projects"_s};
        QString err;
        QVERIFY(saveVaultSettings(v.root(), st, &err));
        MainWindow w;
        QVERIFY(w.openVault(v.root()));
        QAction* action = nullptr;
        for (QAction* a : w.findChildren<QAction*>())
            if (a->text() == u"&New note"_s) action = a;
        QVERIFY(action);
        bool checked = false;
        QTimer::singleShot(0, &w, [&] {
            auto* dlg = qobject_cast<NewNoteDialog*>(QApplication::activeModalWidget());
            QVERIFY(dlg);
            dlg->findChild<QLineEdit*>()->setText(u"Idea"_s);
            QCOMPARE(dlg->combo(0)->count(), 4); // (none) + the three categories
            dlg->combo(1)->setCurrentIndex(1);   // tech
            dlg->combo(0)->setCurrentIndex(1);   // tech too: the later one is cleared
            QCOMPARE(dlg->combo(1)->currentIndex(), 0);
            dlg->combo(1)->setCurrentIndex(2); // personal
            checked = true;
            dlg->accept();
        });
        action->trigger();
        QVERIFY(checked);
        QCOMPARE(w.currentPath(), u"Idea.md"_s);
        QCOMPARE(test::readBytes(v.path(u"Idea.md"_s)),
                 QByteArray("---\nprimary: tech\nsecondary: personal\n---\n"));

        // Cancel creates nothing.
        const int before = static_cast<int>(test::snapshotTree(v.root()).size());
        QTimer::singleShot(0, &w, [] { QApplication::activeModalWidget()->close(); });
        action->trigger();
        QCOMPARE(static_cast<int>(test::snapshotTree(v.root()).size()), before);
    }

    void headingsUseTheNormalTextColour()
    {
        // Regression: the theme was computed while the editor was disabled (no note
        // open yet), so headings got the greyed-out disabled colour.
        test::VaultCopy v(u"ui-vault"_s);
        MainWindow w;
        QVERIFY(w.openVault(v.root()));
        QVERIFY(w.openNote(u"Second.md"_s));
        const QTextBlock h = w.editor()->document()->findBlockByNumber(0);
        const QList<QTextLayout::FormatRange> formats = h.layout()->formats();
        QVERIFY(!formats.isEmpty());
        QCOMPARE(formats.last().format.foreground().color(),
                 w.editor()->palette().color(QPalette::Active, QPalette::Text));
    }

    void quickSwitcherFuzzy()
    {
        QVERIFY(fuzzyScore(u"sec", u"second", 0) > fuzzyScore(u"sec", u"some/expected", 5));
        QCOMPARE(fuzzyScore(u"xyz", u"second", 0), -1);
        QVERIFY(fuzzyScore(u"hm", u"home", 0) >= 0);
    }

    void strictLineBreaks()
    {
        RenderContext ctx;
        QVERIFY(renderMarkdown("one\ntwo\n", ctx).contains(u"<br/>"_s));
        ctx.strictLineBreaks = true;
        QVERIFY(!renderMarkdown("one\ntwo\n", ctx).contains(u"<br/>"_s));
    }

    void htmlEscapingAndSanitising()
    {
        RenderContext ctx;
        const QString html =
            renderMarkdown("<script>alert(1)</script> <b>ok</b> <img src=x onerror=y>\n", ctx);
        QVERIFY(!html.contains(u"<script"_s));
        QVERIFY(!html.contains(u"<img src=x"_s));
        QVERIFY(html.contains(u"&lt;script&gt;"_s));
        QVERIFY(html.contains(u"<b>ok</b>"_s));
        const QString inlineHtml =
            renderMarkdown("Text <span style=\"color:red\" onclick=\"x\">red</span>\n", ctx);
        QVERIFY(inlineHtml.contains(u"<span style=\"color:red\">red</span>"_s));
    }
};

QTEST_MAIN(TestMainWindow)
#include "tst_mainwindow.moc"
