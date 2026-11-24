#include "TestUtil.h"
#include "core/index/Chunker.h"
#include "core/index/VaultService.h"

#include <QDateTime>
#include <QFile>
#include <QSignalSpy>

using namespace mn;
using namespace Qt::StringLiterals;

namespace {

template <typename R, typename Call> R await(Call call)
{
    R result{};
    bool done = false;
    call([&](R r) {
        result = std::move(r);
        done = true;
    });
    if (!QTest::qWaitFor([&] { return done; }, 20000)) qWarning("query timed out");
    return result;
}

bool openIndexed(VaultService& svc, const test::VaultCopy& v)
{
    QSignalSpy spy(&svc, &VaultService::indexingFinished);
    QString err;
    return svc.open(v.root(), &err, v.cacheDir() + u"/index.sqlite"_s) && spy.wait(15000);
}

QString readText(const QString& path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
}

QStringList ranges(const std::vector<Chunk>& chunks)
{
    QStringList out;
    for (const Chunk& c : chunks)
        out.append(QString::number(c.startLine) + u'-' + QString::number(c.endLine));
    return out;
}

} // namespace

class TestPassages : public QObject {
    Q_OBJECT
private Q_SLOTS:
    // --- chunker ---

    void differentTopicsAreSeparatePassages()
    {
        const QString text = readText(test::fixture(u"passages-vault/Daily/2024-03-14.md"_s));
        const std::vector<Chunk> c = chunkNote(text, 3); // frontmatter is lines 0-2
        // Sleep, Rust, tomatoes; then the "Reading" section with its code block kept whole.
        QCOMPARE(ranges(c), (QStringList{u"3-3"_s, u"5-5"_s, u"7-7"_s, u"9-17"_s}));
        QVERIFY(c[0].text.contains(u"caffeine"_s));
        QVERIFY(!c[0].text.contains(u"Rust"_s));
        QCOMPARE(c[0].context, QString());
        QCOMPARE(c[3].context, u"Reading"_s);
        QVERIFY(c[3].text.startsWith(u"## Reading"_s));
        QVERIFY(c[3].text.endsWith(u"```"_s));
    }

    void sameTopicParagraphsMerge()
    {
        const QString text = u"Caffeine late in the day ruins my sleep, the coffee keeps me awake.\n"
                             "\n"
                             "Without the afternoon coffee my sleep is deeper and I wake rested.\n"
                             "\n"
                             "The quarterly budget meeting moved to Thursday afternoon at the office.\n"_s;
        QCOMPARE(ranges(chunkNote(text)), (QStringList{u"0-2"_s, u"4-4"_s}));
    }

    void headingsSplitAndGiveContext()
    {
        const QString text = u"# Projects\n"
                             "## Garden\n"
                             "Tomatoes need compost.\n"
                             "### Pests\n"
                             "Aphids on the beans.\n"
                             "## Work\n"
                             "Budget frozen.\n"_s;
        const std::vector<Chunk> c = chunkNote(text);
        QCOMPARE(c.size(), std::size_t(3)); // "# Projects" alone has no text of its own
        QCOMPARE(c[0].context, u"Projects › Garden"_s);
        QCOMPARE(c[1].context, u"Projects › Garden › Pests"_s);
        QCOMPARE(c[2].context, u"Projects › Work"_s);
        QCOMPARE(c[2].text, u"## Work\nBudget frozen."_s);
        // A heading straight above a subheading is no passage of its own.
        const std::vector<Chunk> nested =
            chunkNote(u"# Jenkins\n## Job types\n### Freestyle\nConfigured in the UI.\n"_s);
        QCOMPARE(ranges(nested), (QStringList{u"2-3"_s}));
        QCOMPARE(nested[0].context, u"Jenkins › Job types › Freestyle"_s);
        // "#tag" at the start of a line is not a heading.
        QCOMPARE(chunkNote(u"#idea about tomatoes\n#idea about compost\n"_s).size(), std::size_t(1));
    }

    void shortSectionsUnderAHeadingStayWhole()
    {
        const QString text = u"# Suite 3: content creator tools\n"
                             "- **video to blog post converter**\n"
                             "- **content idea generator**\n"
                             "- **thumbnail A/B tester**\n"
                             "- **script to teleprompter**\n"
                             "- **analytics dashboard** - unify analytics across social media\n"_s;
        QCOMPARE(ranges(chunkNote(text)), (QStringList{u"0-5"_s}));
        // The same items before any heading are split by topic like any other text.
        QVERIFY(chunkNote(text.mid(text.indexOf(u'\n') + 1)).size() > 1);
    }

    void topLevelListItemsAreUnits()
    {
        const QString text =
            u"- Caffeine after noon wrecks my sleep schedule completely, staying awake past midnight.\n"
            "  - nested detail about coffee timing and sleep\n"
            "- The tomato seedlings need repotting with fresh compost before the frost.\n"_s;
        QCOMPARE(ranges(chunkNote(text)), (QStringList{u"0-1"_s, u"2-2"_s}));
    }

    void longRamblesWithSingleLineBreaksAreSplit()
    {
        // No blank lines at all: one paragraph of separate lines, far over maxWords.
        QString text;
        const QString sleep =
            u"Caffeine and coffee after lunch keep wrecking my sleep and I stay awake past midnight again"_s;
        const QString garden =
            u"The tomato seedlings and beans need compost repotting and more sunlight on the windowsill"_s;
        for (int i = 0; i < 20; ++i)
            text += sleep + u'\n';
        for (int i = 0; i < 20; ++i)
            text += garden + u'\n';
        const std::vector<Chunk> c = chunkNote(text);
        QVERIFY(c.size() >= 2);
        for (const Chunk& ch : c) // no passage mixes the two topics
            QVERIFY(!(ch.text.contains(u"Caffeine"_s) && ch.text.contains(u"tomato"_s)));
        QCOMPARE(c.front().startLine, 0);
        QCOMPARE(c.back().endLine, 39);
    }

    void shortTrailingLineJoinsPrevious()
    {
        const QString text =
            u"Caffeine late in the day ruins my sleep, the coffee keeps me awake until two.\n"
            "\n"
            "Anyway.\n"_s;
        QCOMPARE(ranges(chunkNote(text)), (QStringList{u"0-2"_s}));
    }

    void windowsLineEndingsAndBlocks()
    {
        const QString text = u"Intro line about caffeine and "
                             u"sleep.\r\n\r\n$$\r\nx^2\r\n\r\ny\r\n$$\r\n\r\n%%\r\nhidden\r\n\r\n"
                             "comment\r\n%%\r\n"_s;
        const std::vector<Chunk> c = chunkNote(text);
        for (const Chunk& ch : c)
            QVERIFY(!ch.text.contains(u'\r'));
        // The math block (lines 2-6) and the comment (8-12) are each kept whole.
        bool math = false;
        bool comment = false;
        for (const Chunk& ch : c) {
            math = math || (ch.startLine <= 2 && ch.endLine >= 6);
            comment = comment || (ch.startLine <= 8 && ch.endLine >= 12);
        }
        QVERIFY(math && comment);
    }

    void chunkAtLineFindsNearest()
    {
        const QString text = readText(test::fixture(u"passages-vault/Daily/2024-03-14.md"_s));
        const std::vector<Chunk> c = chunkNote(text, 3);
        QCOMPARE(chunkAtLine(c, 1), nullptr); // frontmatter
        QCOMPARE(chunkAtLine(c, 5)->startLine, 5);
        QCOMPARE(chunkAtLine(c, 6)->startLine, 5); // blank line after it
        QCOMPARE(chunkAtLine(c, 12)->startLine, 9);
    }

    // --- index ---

    void searchFindsPassagesNotWholeNotes()
    {
        test::VaultCopy v(u"passages-vault"_s);
        VaultService svc;
        QVERIFY(openIndexed(svc, v));
        const auto hits =
            await<std::vector<Passage>>([&](auto cb) { svc.searchPassages(u"caffeine"_s, cb); });
        QStringList paths;
        for (const Passage& p : hits) {
            paths.append(p.path);
            // Only the passage about it, not the rest of a daily note.
            QVERIFY2(!p.text.contains(u"tomato"_s) && !p.text.contains(u"budget"_s), qPrintable(p.text));
            QVERIFY(p.snippet.contains(QChar(1)));
        }
        paths.sort();
        QCOMPARE(paths, (QStringList{u"Daily/2024-03-14.md"_s, u"Daily/2024-06-02.md"_s,
                                     u"Daily/2025-01-20.md"_s, u"Sleep research.md"_s}));
        for (const Passage& p : hits) {
            if (p.path == u"Daily/2024-03-14.md") {
                QCOMPARE(p.startLine, 3);
                QCOMPARE(p.endLine, 3);
                QCOMPARE(QDateTime::fromMSecsSinceEpoch(p.dateMs).date(), QDate(2024, 3, 14));
            }
        }
        // The note's name counts: "research" only appears in the title.
        const auto byTitle =
            await<std::vector<Passage>>([&](auto cb) { svc.searchPassages(u"research"_s, cb); });
        QCOMPARE(byTitle.size(), std::size_t(1));
        QCOMPARE(byTitle[0].path, u"Sleep research.md"_s);
    }

    void tagFiltersApplyPerPassage()
    {
        test::VaultCopy v(u"passages-vault"_s);
        VaultService svc;
        QVERIFY(openIndexed(svc, v));
        // An inline tag selects the passage it's written in.
        auto hits = await<std::vector<Passage>>([&](auto cb) { svc.searchPassages(u"#health"_s, cb); });
        QCOMPARE(hits.size(), std::size_t(1));
        QCOMPARE(hits[0].path, u"Daily/2024-06-02.md"_s);
        QCOMPARE(hits[0].startLine, 0);
        hits = await<std::vector<Passage>>([&](auto cb) { svc.searchPassages(u"#health compost"_s, cb); });
        QVERIFY(hits.empty());
        // A frontmatter tag covers every passage of its note.
        hits = await<std::vector<Passage>>([&](auto cb) { svc.searchPassages(u"#journal"_s, cb); });
        QCOMPARE(hits.size(), std::size_t(2));
        hits = await<std::vector<Passage>>([&](auto cb) { svc.searchPassages(u"#journal budget"_s, cb); });
        QCOMPARE(hits.size(), std::size_t(1));
    }

    void relatedPassagesForTheParagraphAtTheCursor()
    {
        test::VaultCopy v(u"passages-vault"_s);
        VaultService svc;
        QVERIFY(openIndexed(svc, v));
        const QString text = readText(v.path(u"Daily/2024-03-14.md"_s));
        auto related = await<std::vector<Passage>>(
            [&](auto cb) { svc.relatedPassages(text, 3, u"Daily/2024-03-14.md"_s, cb); });
        QVERIFY(!related.empty());
        for (const Passage& p : related) {
            QVERIFY(p.path != u"Daily/2024-03-14.md"_s);
            QVERIFY2(p.text.contains(u"sleep"_s, Qt::CaseInsensitive), qPrintable(p.text));
            QVERIFY(!p.reason.isEmpty());
        }
        // The cursor on the Rust paragraph finds the Rust note instead.
        related = await<std::vector<Passage>>(
            [&](auto cb) { svc.relatedPassages(text, 5, u"Daily/2024-03-14.md"_s, cb); });
        QVERIFY(!related.empty());
        QCOMPARE(related[0].path, u"Rust notes.md"_s);
        // In the frontmatter: nothing.
        related = await<std::vector<Passage>>(
            [&](auto cb) { svc.relatedPassages(text, 1, u"Daily/2024-03-14.md"_s, cb); });
        QVERIFY(related.empty());
    }

    void editedAndDeletedNotesLeaveNoStalePassages()
    {
        test::VaultCopy v(u"passages-vault"_s);
        VaultService svc;
        QVERIFY(openIndexed(svc, v));
        QVERIFY(test::writeBytes(v.path(u"Sleep research.md"_s), "# Sleep research\n\nMelatonin timing.\n"));
        QVERIFY(QFile::remove(v.path(u"Daily/2025-01-20.md"_s)));
        svc.refresh({u"Sleep research.md"_s, u"Daily/2025-01-20.md"_s});
        svc.waitForIdle();
        auto hits = await<std::vector<Passage>>([&](auto cb) { svc.searchPassages(u"caffeine"_s, cb); });
        QStringList paths;
        for (const Passage& p : hits)
            paths.append(p.path);
        paths.sort();
        QCOMPARE(paths, (QStringList{u"Daily/2024-03-14.md"_s, u"Daily/2024-06-02.md"_s}));
        hits = await<std::vector<Passage>>([&](auto cb) { svc.searchPassages(u"melatonin"_s, cb); });
        QCOMPARE(hits.size(), std::size_t(1));
        QCOMPARE(hits[0].startLine, 0);
        QCOMPARE(hits[0].endLine, 2);
    }

    void searchingNeverWritesTheVault()
    {
        test::VaultCopy v(u"passages-vault"_s);
        const QByteArray before = test::readBytes(v.path(u"Daily/2024-03-14.md"_s));
        VaultService svc;
        QVERIFY(openIndexed(svc, v));
        await<std::vector<Passage>>([&](auto cb) { svc.searchPassages(u"caffeine"_s, cb); });
        await<std::vector<Passage>>([&](auto cb) {
            svc.relatedPassages(QString::fromUtf8(before), 3, u"Daily/2024-03-14.md"_s, cb);
        });
        QCOMPARE(test::readBytes(v.path(u"Daily/2024-03-14.md"_s)), before);
    }
};

QTEST_GUILESS_MAIN(TestPassages)
#include "tst_passages.moc"
