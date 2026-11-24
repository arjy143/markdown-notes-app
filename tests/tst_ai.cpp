#include "FakeAiServer.h"
#include "TestUtil.h"
#include "core/ai/AiClient.h"
#include "core/ai/Assistant.h"
#include "core/ai/FrontmatterMerge.h"
#include "core/ai/Prompts.h"
#include "core/index/VaultService.h"
#include "core/parse/Frontmatter.h"

#include <QDirIterator>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>

#include <functional>

using namespace mn;
using namespace mn::ai;
using namespace Qt::StringLiterals;

namespace {

using test::FakeServer;

QString lastUserMessage(const QJsonObject& chat)
{
    const QJsonArray m = chat.value(u"messages"_s).toArray();
    return m.isEmpty() ? QString() : m.last().toObject().value(u"content"_s).toString();
}

QString systemMessage(const QJsonObject& chat)
{
    const QJsonArray m = chat.value(u"messages"_s).toArray();
    return m.isEmpty() ? QString() : m.first().toObject().value(u"content"_s).toString();
}

bool openIndexed(VaultService& svc, const test::VaultCopy& v)
{
    QSignalSpy spy(&svc, &VaultService::indexingFinished);
    QString err;
    return svc.open(v.root(), &err, v.cacheDir() + u"/index.sqlite"_s) && spy.wait(15000);
}

AiConfig configFor(const FakeServer& server)
{
    AiConfig c;
    c.baseUrl = server.url();
    c.chatModel = u"chat-model"_s;
    c.embedModel = u"embed-model"_s;
    return c;
}

bool waitIndexed(Assistant& a)
{
    return QTest::qWaitFor([&] { return !a.indexing(); }, 15000);
}

QHash<QString, QByteArray> vaultFiles(const QString& root)
{
    QHash<QString, QByteArray> out;
    QDirIterator it(root, QDir::Files | QDir::Hidden, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString p = it.next();
        if (!p.contains(u"/.markdown-notes/"_s)) out.insert(p, test::readBytes(p));
    }
    return out;
}

} // namespace

class TestAi : public QObject {
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

    // --- frontmatter merge (no model involved) ---

    void newFrontmatterAtTheTop()
    {
        const QString note = u"# Title\n\nBody.\n"_s;
        FrontmatterPatch p;
        p.addIfMissing = {{u"primary"_s, u"tech"_s}, {u"summary"_s, u"Notes: about things"_s}};
        p.addTags = {u"#Machine Learning"_s, u"rust"_s, u"1984"_s, u"rust"_s};
        const FrontmatterEdit e = planFrontmatterEdit(note, p);
        QVERIFY(e.ok && e.changed);
        QCOMPARE(e.replaceLines, 0);
        const QString out = applyFrontmatterEdit(note, e);
        QCOMPARE(out, u"---\nprimary: tech\nsummary: \"Notes: about things\"\ntags:\n  - Machine-Learning\n"
                      "  - rust\n---\n# Title\n\nBody.\n"_s);
        const Frontmatter fm = parseFrontmatter(out.toUtf8());
        QVERIFY(fm.valid);
        QCOMPARE(fm.field(u"summary")->value, u"Notes: about things"_s);
        QCOMPARE(fm.tags, (QStringList{u"Machine-Learning"_s, u"rust"_s}));
    }

    void existingValuesAreNeverChanged()
    {
        const QString note =
            u"---\nprimary: projects\n# a comment\nsummary:   keep   my spacing\n---\nBody\n"_s;
        FrontmatterPatch p;
        p.addIfMissing = {{u"primary"_s, u"tech"_s}, {u"summary"_s, u"other"_s}, {u"secondary"_s, u"meta"_s}};
        const QString out = applyFrontmatterEdit(note, planFrontmatterEdit(note, p));
        QCOMPARE(
            out,
            u"---\nprimary: projects\n# a comment\nsummary:   keep   my spacing\nsecondary: meta\n---\nBody\n"_s);
        // Nothing new: no change at all.
        p.addIfMissing = {{u"primary"_s, u"tech"_s}};
        const FrontmatterEdit e = planFrontmatterEdit(note, p);
        QVERIFY(e.ok && !e.changed);
        QCOMPARE(applyFrontmatterEdit(note, e), note);
    }

    void tagsAreAddedInTheExistingStyle_data()
    {
        QTest::addColumn<QString>("before");
        QTest::addColumn<QString>("after");
        QTest::newRow("block list") << u"---\ntags:\n    - a\n    - B\nx: 1\n---\n"_s
                                    << u"---\ntags:\n    - a\n    - B\n    - new\nx: 1\n---\n"_s;
        QTest::newRow("block list at indent 0")
            << u"---\ntags:\n- a\n---\n"_s << u"---\ntags:\n- a\n- new\n---\n"_s;
        QTest::newRow("flow list") << u"---\ntags: [a, b]\n---\n"_s << u"---\ntags: [a, b, new]\n---\n"_s;
        QTest::newRow("empty flow list") << u"---\ntags: []\n---\n"_s << u"---\ntags: [new, a]\n---\n"_s;
        QTest::newRow("string") << u"---\ntags: a, b\n---\n"_s << u"---\ntags: a, b, new\n---\n"_s;
        QTest::newRow("empty") << u"---\ntags:\nx: 1\n---\n"_s
                               << u"---\ntags:\n  - new\n  - a\nx: 1\n---\n"_s;
        QTest::newRow("no tags yet") << u"---\nx: 1\n---\n"_s << u"---\nx: 1\ntags:\n  - new\n  - a\n---\n"_s;
        QTest::newRow("already there, other case")
            << u"---\ntags: [New]\n---\n"_s << u"---\ntags: [New, a]\n---\n"_s;
        QTest::newRow("windows line endings") << u"---\r\ntags:\r\n  - a\r\n---\r\nBody\r\n"_s
                                              << u"---\r\ntags:\r\n  - a\r\n  - new\r\n---\r\nBody\r\n"_s;
    }
    void tagsAreAddedInTheExistingStyle()
    {
        QFETCH(QString, before);
        QFETCH(QString, after);
        FrontmatterPatch p;
        p.addTags = {u"new"_s, u"a"_s};
        const FrontmatterEdit e = planFrontmatterEdit(before, p);
        QVERIFY2(e.ok, qPrintable(e.error));
        QCOMPARE(applyFrontmatterEdit(before, e), after);
        QVERIFY(parseFrontmatter(after.toUtf8()).valid);
    }

    void unreadableFrontmatterIsLeftAlone()
    {
        const QString note = u"---\nkey: [unclosed\n---\nBody\n"_s;
        FrontmatterPatch p;
        p.addTags = {u"x"_s};
        const FrontmatterEdit e = planFrontmatterEdit(note, p);
        QVERIFY(!e.ok);
        QVERIFY(!e.error.isEmpty());
        QCOMPARE(applyFrontmatterEdit(note, e), note);
    }

    void yamlScalarsRoundTrip()
    {
        for (const QString& v : {u"plain words"_s, u"a: b"_s, u"#hash"_s, u"yes"_s, u"42"_s,
                                 u"say \"hi\" \\ there"_s, u"[x]"_s, u"- dash"_s, u"ünïcödé text"_s}) {
            const QString note = u"---\nsummary: "_s + yamlScalar(v) + u"\n---\n"_s;
            const Frontmatter fm = parseFrontmatter(note.toUtf8());
            QVERIFY2(fm.valid, qPrintable(v));
            QCOMPARE(fm.field(u"summary")->value, v);
        }
    }

    // --- prompts and replies ---

    void frontmatterRepliesAreReadDefensively()
    {
        FrontmatterRequest req;
        req.categoryProperties = {u"primary"_s, u"secondary"_s};
        req.categories = {u"tech"_s, u"personal"_s, u"projects"_s};
        req.vaultTags = {u"Machine-Learning"_s, u"rust"_s};
        const QString reply = u"<think>The note is about ML {not json}</think>Sure! Here you "
                              u"go:\n```json\n{\"primary\": \"Tech\", "
                              "\"secondary\": \"tech\", \"tags\": [\"#machine-learning\", \"Neural Nets\", "
                              "\"tech\", \"2024\"], "
                              "\"summary\": \"  How I trained a   model.\"}\n```"_s;
        const FrontmatterSuggestion s = parseFrontmatterSuggestion(reply, req);
        QVERIFY(s.error.isEmpty());
        // Categories in the vault's spelling, each once; unknown ones dropped.
        QCOMPARE(s.properties.value(0), (QPair<QString, QString>{u"primary"_s, u"tech"_s}));
        QCOMPARE(s.properties.value(1), (QPair<QString, QString>{u"summary"_s, u"How I trained a model."_s}));
        // Existing tags keep their spelling; a tag equal to a category already set is dropped.
        QCOMPARE(s.tags, (QStringList{u"Machine-Learning"_s, u"Neural-Nets"_s}));
        QVERIFY(!parseFrontmatterSuggestion(u"I can't do that."_s, req).error.isEmpty());
    }

    void questionPromptsNumberTheirSources()
    {
        Passage a;
        a.path = u"Daily/2024-03-14.md"_s;
        a.context = u"Sleep"_s;
        a.text = u"Caffeine after lunch."_s;
        const auto msgs = askPrompt(u"Why can't I sleep?"_s, {a});
        QCOMPARE(msgs.size(), std::size_t(2));
        QVERIFY(msgs[1].content.contains(u"[1] 2024-03-14"_s));
        QVERIFY(msgs[1].content.contains(u"Caffeine after lunch."_s));
        QVERIFY(msgs[1].content.endsWith(u"Question: Why can't I sleep?"_s));
        QCOMPARE(questionTerms(u"What did I decide about the coffee?"_s),
                 (QStringList{u"coffee"_s, u"decide"_s}));
        QCOMPARE(stripThinking(u"<think>hmm\nlong</think>\n\nAnswer"_s), u"Answer"_s);
    }

    // --- client ---

    void clientTalksToAnOpenAiCompatibleServer()
    {
        FakeServer server;
        AiClient client;
        client.setConfig(configFor(server));
        QCOMPARE(client.endpoint(u"embeddings"_s).toString(), server.url() + u"/v1/embeddings"_s);
        AiConfig custom = configFor(server);
        custom.baseUrl = u"localhost:1234/api/v1/"_s;
        AiClient other;
        other.setConfig(custom);
        QCOMPARE(other.endpoint(u"chat/completions"_s).toString(),
                 u"http://localhost:1234/api/v1/chat/completions"_s);

        server.chatReply = [](const QJsonObject&) { return u"<think>x</think>Hello"_s; };
        QString reply;
        bool done = false;
        client.chat({{u"user"_s, u"hi"_s}}, [&](const QString& r, const QString& err) {
            QVERIFY(err.isEmpty());
            reply = r;
            done = true;
        });
        QTRY_VERIFY(done);
        QCOMPARE(reply, u"Hello"_s);
        QCOMPARE(server.lastChat.value(u"model"_s).toString(), u"chat-model"_s);

        std::vector<std::vector<float>> vecs;
        done = false;
        client.embed({u"coffee"_s, u"tomato garden"_s},
                     [&](std::vector<std::vector<float>> v, const QString& err) {
                         QVERIFY(err.isEmpty());
                         vecs = std::move(v);
                         done = true;
                     });
        QTRY_VERIFY(done);
        QCOMPARE(vecs.size(), std::size_t(2));
        QCOMPARE(vecs[1][2], 2.0f);

        // Embeddings can come from another server (llama.cpp runs one model per server).
        AiConfig split = configFor(server);
        split.baseUrl = u"http://127.0.0.1:1"_s; // nothing listens there
        split.embedUrl = server.url();
        AiClient splitClient;
        splitClient.setConfig(split);
        done = false;
        splitClient.embed({u"coffee"_s}, [&](std::vector<std::vector<float>> v, const QString& err) {
            QVERIFY2(err.isEmpty(), qPrintable(err));
            QCOMPARE(v.size(), std::size_t(1));
            done = true;
        });
        QTRY_VERIFY(done);

        server.failStatus = 503;
        QString error;
        done = false;
        client.chat({{u"user"_s, u"hi"_s}}, [&](const QString&, const QString& err) {
            error = err;
            done = true;
        });
        QTRY_VERIFY(done);
        QCOMPARE(error, u"HTTP 503: model not loaded"_s);
    }

    // --- the assistant over a vault ---

    void semanticIndexAndSearch()
    {
        test::VaultCopy v(u"passages-vault"_s);
        const auto before = vaultFiles(v.root());
        FakeServer server;
        VaultService svc;
        QVERIFY(openIndexed(svc, v));
        Assistant a(&svc);
        QSignalSpy progress(&a, &Assistant::indexProgress);
        a.setConfig(configFor(server));
        QVERIFY(waitIndexed(a));
        QVERIFY(!progress.isEmpty());
        const int total = progress.last().at(1).toInt();
        QVERIFY(total > 5);
        QCOMPARE(progress.last().at(0).toInt(), total);
        QCOMPARE(a.indexedPassages(), total);

        // "insomnia" is in no note: keyword search finds nothing, meaning finds the sleep passages.
        std::vector<Passage> keyword;
        bool done = false;
        svc.searchPassages(u"insomnia"_s, [&](std::vector<Passage> p) {
            keyword = std::move(p);
            done = true;
        });
        QTRY_VERIFY(done);
        QVERIFY(keyword.empty());
        std::vector<Passage> found;
        done = false;
        a.search(u"insomnia"_s, [&](std::vector<Passage> p, const QString& err) {
            QVERIFY(err.isEmpty());
            found = std::move(p);
            done = true;
        });
        QTRY_VERIFY(done);
        QVERIFY(found.size() >= 3);
        for (std::size_t i = 0; i < 3; ++i)
            QVERIFY2(found[i].text.contains(u"sleep"_s, Qt::CaseInsensitive) ||
                         found[i].text.contains(u"slept"_s, Qt::CaseInsensitive),
                     qPrintable(found[i].text));

        // The vectors live in the cache, next to the index; the vault is untouched.
        QVERIFY(QFile::exists(v.cacheDir() + u"/embeddings.sqlite"_s));
        QCOMPARE(vaultFiles(v.root()), before);
    }

    void vectorsAreReusedAcrossRestartsAndRenames()
    {
        test::VaultCopy v(u"passages-vault"_s);
        FakeServer server;
        {
            VaultService svc;
            QVERIFY(openIndexed(svc, v));
            Assistant a(&svc);
            a.setConfig(configFor(server));
            QVERIFY(waitIndexed(a));
        }
        const int calls = server.embedCalls;
        // A note moved to another folder has the same passages: nothing to embed again.
        QVERIFY(QDir().mkpath(v.path(u"Archive"_s)));
        QVERIFY(QFile::rename(v.path(u"Rust notes.md"_s), v.path(u"Archive/Rust notes.md"_s)));
        VaultService svc;
        QVERIFY(openIndexed(svc, v));
        Assistant a(&svc);
        QSignalSpy progress(&a, &Assistant::indexProgress);
        a.setConfig(configFor(server));
        QVERIFY(waitIndexed(a));
        QCOMPARE(server.embedCalls, calls);
        QCOMPARE(progress.last().at(0).toInt(), progress.last().at(1).toInt());
    }

    void serverErrorsStopIndexingWithAMessage()
    {
        test::VaultCopy v(u"passages-vault"_s);
        FakeServer server;
        server.failStatus = 500;
        VaultService svc;
        QVERIFY(openIndexed(svc, v));
        Assistant a(&svc);
        QSignalSpy errors(&a, &Assistant::indexError);
        a.setConfig(configFor(server));
        QTRY_COMPARE(errors.size(), 1);
        QVERIFY(errors[0][0].toString().contains(u"500"_s));
        QVERIFY(!a.indexing());
        // Search still works, from keywords.
        bool done = false;
        a.search(u"caffeine"_s, [&](std::vector<Passage> p, const QString&) {
            QVERIFY(!p.empty());
            done = true;
        });
        QTRY_VERIFY(done);
    }

    void askAnswersFromTheNotes()
    {
        test::VaultCopy v(u"passages-vault"_s);
        const auto before = vaultFiles(v.root());
        FakeServer server;
        server.chatReply = [](const QJsonObject&) { return u"Caffeine after noon keeps you awake [1]."_s; };
        VaultService svc;
        QVERIFY(openIndexed(svc, v));
        Assistant a(&svc);
        a.setConfig(configFor(server));
        QVERIFY(waitIndexed(a));
        Assistant::Answer answer;
        bool done = false;
        a.ask(u"Why do I keep waking up at night?"_s, [&](Assistant::Answer r) {
            answer = std::move(r);
            done = true;
        });
        QTRY_VERIFY(done);
        QVERIFY2(answer.error.isEmpty(), qPrintable(answer.error));
        QCOMPARE(answer.text, u"Caffeine after noon keeps you awake [1]."_s);
        QVERIFY(!answer.sources.empty());
        // The question went out with the passages about sleep (found by meaning: "waking" is in no note).
        const QString sent = lastUserMessage(server.lastChat);
        QVERIFY2(sent.contains(u"[1] "_s) && sent.contains(u"sleep"_s, Qt::CaseInsensitive),
                 qPrintable(sent));
        QVERIFY(sent.contains(u"Question: Why do I keep waking up at night?"_s));
        QCOMPARE(vaultFiles(v.root()), before);

        // No chat model: a clear message, nothing sent.
        AiConfig noChat = configFor(server);
        noChat.chatModel.clear();
        a.setConfig(noChat);
        const int chats = server.chatCalls;
        done = false;
        a.ask(u"x"_s, [&](Assistant::Answer r) {
            QVERIFY(!r.error.isEmpty());
            done = true;
        });
        QTRY_VERIFY(done);
        QCOMPARE(server.chatCalls, chats);
    }

    void frontmatterSuggestionUsesTheVaultsConventions()
    {
        test::VaultCopy v(u"passages-vault"_s);
        QVERIFY(test::writeBytes(
            v.path(u"Sleep log.md"_s),
            "---\nprimary: health\nsecondary: personal\ntags:\n  - sleep\nsummary: Sleep and "
            "caffeine experiments.\n---\nCaffeine after noon ruins my sleep; coffee keeps me "
            "awake.\n"));
        VaultSettings settings;
        settings.tagProperties = {u"primary"_s, u"secondary"_s};
        settings.categoryTags = {u"health"_s, u"personal"_s, u"tech"_s};
        QString err;
        QVERIFY(saveVaultSettings(v.root(), settings, &err));
        const auto before = vaultFiles(v.root());
        FakeServer server;
        server.chatReply = [](const QJsonObject&) {
            return uR"({"primary": "Health", "secondary": "personal", "tags": ["sleep", "Caffeine"], "summary": "Less coffee, better sleep."})"_s;
        };
        VaultService svc;
        QVERIFY(openIndexed(svc, v));
        Assistant a(&svc);
        a.setConfig(configFor(server));
        const QString text = QString::fromUtf8(test::readBytes(v.path(u"Daily/2024-06-02.md"_s)));
        FrontmatterSuggestion s;
        bool done = false;
        a.suggestFrontmatter(u"Daily/2024-06-02.md"_s, text, [&](FrontmatterSuggestion r) {
            s = std::move(r);
            done = true;
        });
        QTRY_VERIFY(done);
        QVERIFY2(s.error.isEmpty(), qPrintable(s.error));
        QCOMPARE(s.properties.value(0), (QPair<QString, QString>{u"primary"_s, u"health"_s}));
        QCOMPARE(s.properties.value(1), (QPair<QString, QString>{u"secondary"_s, u"personal"_s}));
        QCOMPARE(s.tags, (QStringList{u"sleep"_s, u"Caffeine"_s}));
        // The prompt carried the categories, the vault's tags and a similar note's frontmatter.
        const QString system = systemMessage(server.lastChat);
        QVERIFY(system.contains(u"Categories: health, personal, tech"_s));
        QVERIFY(system.contains(u"health"_s) && system.contains(u"journal"_s));
        QVERIFY2(system.contains(u"summary: Sleep and caffeine experiments."_s), qPrintable(system));
        QVERIFY(lastUserMessage(server.lastChat).contains(u"Cut out coffee after noon"_s));
        // Suggesting never writes anything.
        QCOMPARE(vaultFiles(v.root()), before);
    }

private:
    QTemporaryDir settingsDir_;
};

QTEST_GUILESS_MAIN(TestAi)
#include "tst_ai.moc"
