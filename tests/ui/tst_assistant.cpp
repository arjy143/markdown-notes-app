// The assistant's UI: applying reviewed frontmatter, the Ask tab and "Meaning" search.
#include "TestUtil.h"
#include "core/ai/FrontmatterMerge.h"
#include "core/index/VaultService.h"
#include "ui/MainWindow.h"
#include "ui/NoteEditor.h"
#include "ui/Panels.h"

#include <QComboBox>
#include <QLineEdit>
#include <QSettings>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTextBrowser>
#include <QTextDocument>
#include <QTreeWidget>

#ifdef MN_HAVE_AI
#include "FakeAiServer.h"
#include "core/ai/Assistant.h"
#include "ui/AiAskPanel.h"
#endif

using namespace mn;
using namespace mn::ui;
using namespace Qt::StringLiterals;

class TestAssistantUi : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void initTestCase()
    {
        // A fresh settings and cache location per run: nothing reaches the user's config.
        QVERIFY(settingsDir_.isValid());
        QSettings::setPath(QSettings::NativeFormat, QSettings::UserScope, settingsDir_.path());
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDir_.path());
        qputenv("XDG_CACHE_HOME", (settingsDir_.path() + u"/cache"_s).toUtf8());
        qputenv("XDG_CONFIG_HOME", (settingsDir_.path() + u"/config"_s).toUtf8());
    }

    void frontmatterGoesIntoTheEditorAsOneUndoableEdit()
    {
        test::VaultCopy v(u"passages-vault"_s);
        MainWindow w;
        QVERIFY(w.openVault(v.root()));
        QVERIFY(w.openNote(u"Daily/2024-06-02.md"_s));
        const QString before = w.editor()->bufferText();
        FrontmatterPatch p;
        p.addIfMissing = {{u"primary"_s, u"health"_s}, {u"summary"_s, u"Less coffee: better sleep."_s}};
        p.addTags = {u"sleep"_s, u"health"_s};
        QString err;
        QVERIFY2(w.applyFrontmatter(p, &err), qPrintable(err));
        QCOMPARE(
            w.editor()->bufferText(),
            u"---\nprimary: health\nsummary: \"Less coffee: better sleep.\"\ntags:\n  - sleep\n  - health\n---\n"_s +
                before);
        // Saved like typing: the body's bytes are unchanged after the new block.
        QVERIFY(w.saveNow());
        const QByteArray saved = test::readBytes(v.path(u"Daily/2024-06-02.md"_s));
        QVERIFY(saved.endsWith(test::readBytes(test::fixture(u"passages-vault/Daily/2024-06-02.md"_s))));
        // One Ctrl+Z takes it all back.
        w.editor()->document()->undo();
        QCOMPARE(w.editor()->bufferText(), before);

        // An existing block is extended in place.
        QVERIFY(w.openNote(u"Daily/2025-01-20.md"_s));
        QVERIFY(w.applyFrontmatter(p, &err));
        QVERIFY(w.editor()->bufferText().startsWith(u"---\ntags: [journal, sleep, health]\nprimary: "
                                                    u"health\nsummary: \"Less coffee: better sleep.\"\n---\n"
                                                    "Quarterly"_s));
    }

#ifdef MN_HAVE_AI
    void askTabAnswersWithLinkedSources()
    {
        test::VaultCopy v(u"passages-vault"_s);
        test::FakeServer server;
        server.chatReply = [](const QJsonObject&) { return u"You sleep badly after **coffee** [1]."_s; };
        MainWindow w;
        QSignalSpy indexed(w.vault(), &VaultService::indexingFinished);
        QVERIFY(w.openVault(v.root()));
        QVERIFY(indexed.wait(10000));
        ai::AiConfig c;
        c.baseUrl = server.url();
        c.chatModel = u"chat"_s;
        c.embedModel = u"embed"_s;
        w.assistant()->setConfig(c);
        QTRY_VERIFY_WITH_TIMEOUT(!w.assistant()->indexing() && w.assistant()->indexedPassages() > 0, 10000);

        auto* ask = w.findChild<AiAskPanel*>();
        QVERIFY(ask);
        auto* question = ask->findChild<QLineEdit*>();
        question->setText(u"Why can't I sleep?"_s);
        QTest::keyClick(question, Qt::Key_Return);
        auto* answer = ask->findChild<QTextBrowser*>();
        auto* sources = ask->findChild<QTreeWidget*>();
        QTRY_VERIFY_WITH_TIMEOUT(sources->topLevelItemCount() > 0, 5000);
        QVERIFY(answer->toPlainText().contains(u"You sleep badly after coffee"_s));
        QVERIFY(answer->toHtml().contains(u"source:1"_s)); // [1] is a link to the first source
        QVERIFY(sources->topLevelItem(0)->text(0).startsWith(u"[1] "_s));
        QSignalSpy open(ask, &AiAskPanel::openRequested);
        Q_EMIT sources->itemClicked(sources->topLevelItem(0), 0);
        QCOMPARE(open.size(), 1);

        // Search › Meaning finds passages by meaning ("insomnia" is in no note).
        auto* search = w.findChild<SearchPanel*>();
        auto* mode = search->findChildren<QComboBox*>().at(0);
        QCOMPARE(mode->count(), 3);
        mode->setCurrentIndex(2);
        search->focusSearch(u"insomnia"_s);
        auto* results = search->findChild<QTreeWidget*>();
        QTRY_VERIFY_WITH_TIMEOUT(results->topLevelItemCount() >= 3, 5000);
        mode->setCurrentIndex(0);
    }
#endif

private:
    QTemporaryDir settingsDir_;
};

QTEST_MAIN(TestAssistantUi)
#include "tst_assistant.moc"
