// Editor, quick-switcher and reading-view benchmarks (needs a display or
// QT_QPA_PLATFORM=offscreen).
#include "BenchUtil.h"
#include "core/index/VaultService.h"
#include "core/vault/TextFile.h"
#include "ui/MathRenderer.h"
#include "ui/NoteEditor.h"
#include "ui/QuickSwitcher.h"
#include "ui/ReadingView.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFileInfo>
#include <QScrollBar>
#include <QTemporaryDir>
#include <QTest>
#include <QTextBlock>
#include <QTimer>

using namespace mn;
using namespace mn::ui;
using namespace Qt::StringLiterals;

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    QCommandLineParser cli;
    cli.addHelpOption();
    cli.addOption({u"vault"_s, u"Vault (from tools/gen_vault.py, or a copy of a real one)."_s, u"path"_s});
    cli.addOption({u"note"_s, u"Big note for the editor benchmark."_s, u"path"_s, u"Big note.md"_s});
    cli.addOption(
        {u"typical"_s, u"Typical note for the reading-view benchmark."_s, u"path"_s, u"Typical note.md"_s});
    cli.addOption({u"png"_s, u"Save screenshots to this folder."_s, u"dir"_s});
    cli.process(app);
    const QString vault = cli.value(u"vault"_s);
    if (vault.isEmpty()) {
        std::fprintf(stderr,
                     "usage: bench_ui --vault <path> [--note <big note>] [--typical <note>] [--png <dir>]\n");
        return 2;
    }
    QTemporaryDir cache;
    VaultService svc;
    QString err;
    svc.open(vault, &err, cache.path() + u"/index.sqlite"_s);
    {
        QEventLoop loop;
        QObject::connect(&svc, &VaultService::indexingFinished, &loop, &QEventLoop::quit);
        loop.exec();
    }

    // --- editor: keystroke to repaint on a big note -----------------------------
    const QString bigPath = cli.value(u"note"_s);
    if (QFileInfo::exists(svc.absolutePath(bigPath))) {
        NoteEditor editor;
        editor.setVault(&svc);
        editor.setNotePath(bigPath);
        editor.resize(1000, 800);
        const LoadedText t = loadTextFile(svc.absolutePath(bigPath));
        QElapsedTimer load;
        load.start();
        editor.setBuffer(t.text);
        editor.show();
        if (!QTest::qWaitForWindowExposed(&editor)) std::printf("(window not exposed)\n");
        QCoreApplication::processEvents();
        bench::report("open big note (setPlainText + layout)", static_cast<double>(load.nsecsElapsed()) / 1e6,
                      "ms");
        std::printf("big note: %d lines\n", editor.document()->blockCount());

        // "Keystroke to repaint": the key event plus the event-loop pass that paints
        // the region Qt marked dirty (what a user sees). Also reported: a forced
        // repaint of the whole viewport, as a worst case.
        std::vector<double> times;
        std::vector<double> fullTimes;
        for (int pos : {editor.document()->blockCount() / 2, 10, editor.document()->blockCount() - 5}) {
            QTextCursor c(editor.document()->findBlockByNumber(pos));
            c.movePosition(QTextCursor::EndOfBlock);
            editor.setTextCursor(c);
            editor.centerCursor();
            QCoreApplication::processEvents();
            for (int i = 0; i < 40; ++i) {
                // Pause between keys (outside the timing), as a fast typist would: a tight
                // loop would measure the compositor's frame throttling (vsync), not our work.
                QTest::qWait(30);
                QElapsedTimer k;
                k.start();
                QTest::keyClick(&editor, static_cast<char>('a' + i % 26));
                QCoreApplication::processEvents();
                times.push_back(static_cast<double>(k.nsecsElapsed()) / 1e6);
            }
            for (int i = 0; i < 20; ++i) {
                QTest::qWait(30);
                QElapsedTimer k;
                k.start();
                QTest::keyClick(&editor, static_cast<char>('a' + i % 26));
                editor.viewport()->repaint();
                fullTimes.push_back(static_cast<double>(k.nsecsElapsed()) / 1e6);
            }
            // A line break re-lays out and re-highlights from the cursor down.
            QTest::qWait(30);
            QElapsedTimer k;
            k.start();
            QTest::keyClick(&editor, Qt::Key_Return);
            QCoreApplication::processEvents();
            times.push_back(static_cast<double>(k.nsecsElapsed()) / 1e6);
        }
        bench::report("keystroke to repaint (median)", bench::median(times), "ms", 8, 16);
        bench::report("keystroke to repaint (p95)", bench::percentile(times, 0.95), "ms", 8, 16);
        bench::report("keystroke + full viewport repaint (p95)", bench::percentile(fullTimes, 0.95), "ms");
        if (cli.isSet(u"png"_s)) {
            QTextCursor c(editor.document()->findBlockByNumber(0));
            editor.setTextCursor(c);
            editor.verticalScrollBar()->setValue(0);
            QCoreApplication::processEvents();
            editor.grab().save(cli.value(u"png"_s) + u"/editor.png"_s);
        }
    } else {
        std::printf("(no %s in the vault: skipping the editor benchmark)\n", qPrintable(bigPath));
    }

    // --- quick switcher ------------------------------------------------------------
    {
        QuickSwitcher sw;
        sw.setFiles(svc.snapshot());
        sw.filterFor(QString()); // builds the list, as opening the switcher does
        std::vector<double> times;
        for (const QString& word : {u"river"_s, u"proj alpha"_s, u"summary 12"_s, u"zzzq"_s, u"coffee"_s}) {
            for (int n = 1; n <= word.size(); ++n) {
                QElapsedTimer t;
                t.start();
                sw.filterFor(word.left(n));
                times.push_back(static_cast<double>(t.nsecsElapsed()) / 1e6);
            }
        }
        bench::report("quick switcher filter (median)", bench::median(times), "ms", 5, 16);
        bench::report("quick switcher filter (max)", *std::max_element(times.begin(), times.end()), "ms", 5,
                      16);
    }

    // --- reading view ----------------------------------------------------------------
    const QString typical = cli.value(u"typical"_s);
    if (QFileInfo::exists(svc.absolutePath(typical))) {
        ReadingView view;
        view.setVault(&svc);
        view.resize(1000, 800);
        view.show();
        if (!QTest::qWaitForWindowExposed(&view)) std::printf("(window not exposed)\n");
        const LoadedText t = loadTextFile(svc.absolutePath(typical));
        QElapsedTimer first;
        first.start();
        view.showNote(typical, t.raw);
        bench::report("reading view, first render (math cold)",
                      static_cast<double>(first.nsecsElapsed()) / 1e6, "ms");
        std::vector<double> times;
        for (int i = 0; i < 10; ++i) {
            QElapsedTimer r;
            r.start();
            view.rerender();
            QCoreApplication::processEvents();
            times.push_back(static_cast<double>(r.nsecsElapsed()) / 1e6);
        }
        bench::report("reading view render (math cached)", bench::median(times), "ms", 50, 150);
        if (cli.isSet(u"png"_s)) view.grab().save(cli.value(u"png"_s) + u"/reading.png"_s);
    } else {
        std::printf("(no %s in the vault: skipping the reading-view benchmark)\n", qPrintable(typical));
    }
    return 0;
}
