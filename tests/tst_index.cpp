#include "TestUtil.h"
#include "core/index/IndexStore.h"
#include "core/index/VaultService.h"
#include "core/vault/TextFile.h"

#include <QSignalSpy>

using namespace mn;
using namespace Qt::StringLiterals;

namespace {

// Runs an async VaultService query and waits for its callback.
template <typename R, typename Call> R await(Call call)
{
    R result{};
    bool done = false;
    call([&](R r) {
        result = std::move(r);
        done = true;
    });
    if (!QTest::qWaitFor([&] { return done; }, 10000)) qWarning("query timed out");
    return result;
}

bool waitIndexed(VaultService& svc)
{
    QSignalSpy spy(&svc, &VaultService::indexingFinished);
    return spy.wait(15000);
}

} // namespace

class TestIndex : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void indexesFixtureVaultWithoutTouchingIt()
    {
        test::VaultCopy v(u"index-vault"_s);
        QVERIFY(v.ok());
        // settings.json is created on first open; everything else must stay byte-identical.
        const auto before = test::snapshotTree(v.root());
        VaultService svc;
        QString err;
        QVERIFY(svc.open(v.root(), &err, v.cacheDir() + u"/index.sqlite"_s));
        QVERIFY(waitIndexed(svc));
        auto after = test::snapshotTree(v.root());
        QVERIFY(after.remove(u".markdown-notes/settings.json"_s) == 1);
        QCOMPARE(after, before);
        // The index lives outside the vault.
        QVERIFY(QFile::exists(v.cacheDir() + u"/index.sqlite"_s));

        QStringList paths;
        for (const VaultFile& f : svc.snapshot().files())
            paths.append(f.path);
        QVERIFY(paths.contains(u"Target.md"_s));
        QVERIFY(paths.contains(u"pics/photo.png"_s));
        QVERIFY(!paths.contains(u".trash/Deleted.md"_s));
        QVERIFY(svc.snapshot().folders.contains(u"empty folder"_s));
    }

    void backlinksAndMentions()
    {
        test::VaultCopy v(u"index-vault"_s);
        VaultService svc;
        QString err;
        QVERIFY(svc.open(v.root(), &err, v.cacheDir() + u"/index.sqlite"_s));
        QVERIFY(waitIndexed(svc));

        const auto links = await<std::vector<Backlink>>([&](auto cb) { svc.backlinks(u"Target.md"_s, cb); });
        QStringList sources;
        for (const Backlink& b : links)
            sources.append(b.sourcePath + u':' + QString::number(b.line));
        // Links inside %%comments%% and hidden folders don't count.
        QCOMPARE(sources, (QStringList{u"Linker one.md:0"_s, u"Linker one.md:1"_s, u"Mentions.md:1"_s,
                                       u"sub/Linker two.md:0"_s, u"Unresolved.md:0"_s}));
        QCOMPARE(links[0].context, u"See [[Target]] for details."_s);
        QVERIFY(links[3].embed);

        QCOMPARE(svc.aliasesOf(u"Target.md"_s), QStringList{u"The Target"_s});
        const auto mentions =
            await<std::vector<Mention>>([&](auto cb) { svc.unlinkedMentions(u"Target.md"_s, cb); });
        QStringList m;
        for (const Mention& x : mentions)
            m.append(x.sourcePath + u':' + QString::number(x.line));
        QCOMPARE(m, (QStringList{u"Mentions.md:0"_s, u"sub/Linker two.md:1"_s}));
    }

    void tagsSearchHeadingsBlocks()
    {
        test::VaultCopy v(u"index-vault"_s);
        VaultService svc;
        QString err;
        QVERIFY(svc.open(v.root(), &err, v.cacheDir() + u"/index.sqlite"_s));
        QVERIFY(waitIndexed(svc));

        QStringList tags;
        for (const TagCount& t : svc.snapshot().tags)
            tags.append(t.tag + u'=' + QString::number(t.notes));
        // "project" (3 notes: Target frontmatter, Linker one, Linker two as #Project), nested tag separate.
        QCOMPARE(tags, (QStringList{u"project=3"_s, u"project/sub=1"_s}));

        const QStringList tagged = await<QStringList>([&](auto cb) { svc.filesWithTag(u"PROJECT"_s, cb); });
        QCOMPARE(tagged, (QStringList{u"Linker one.md"_s, u"Target.md"_s, u"sub/Linker two.md"_s}));

        const auto hits = await<std::vector<SearchHit>>([&](auto cb) { svc.search(u"quick fox"_s, cb); });
        QCOMPARE(hits.size(), std::size_t(1));
        QCOMPARE(hits[0].path, u"Search me.md"_s);
        QVERIFY(hits[0].snippet.contains(QChar(1)));
        const auto prefix = await<std::vector<SearchHit>>([&](auto cb) { svc.search(u"appl"_s, cb); });
        QCOMPARE(prefix.size(), std::size_t(1));
        const auto diacritics = await<std::vector<SearchHit>>([&](auto cb) { svc.search(u"cafe"_s, cb); });
        QCOMPARE(diacritics.size(), std::size_t(1));
        const auto weird =
            await<std::vector<SearchHit>>([&](auto cb) { svc.search(u"\"unbalanced AND ( OR"_s, cb); });
        Q_UNUSED(weird); // must not crash or error out

        const auto heads =
            await<std::vector<HeadingInfo>>([&](auto cb) { svc.headings(u"Target.md"_s, cb); });
        QCOMPARE(heads.size(), std::size_t(2));
        QCOMPARE(heads[1].text, u"Section A"_s);
        QCOMPARE(heads[1].line, 6);
        const auto blocks = await<std::vector<BlockInfo>>([&](auto cb) { svc.blocks(u"Target.md"_s, cb); });
        QCOMPARE(blocks.size(), std::size_t(1));
        QCOMPARE(blocks[0].id, u"blk"_s);

        const auto unresolved = await<std::vector<UnresolvedLink>>([&](auto cb) { svc.unresolvedLinks(cb); });
        // [[alias]] doesn't resolve to the note that has the alias.
        QStringList missing;
        for (const UnresolvedLink& u : unresolved)
            missing.append(u.linkpath);
        missing.sort();
        QCOMPARE(missing, (QStringList{u"Nowhere"_s, u"The Target"_s}));
    }

    void cacheIsReusedAndRebuiltOnSchemaChange()
    {
        test::VaultCopy v(u"index-vault"_s);
        const QString db = v.cacheDir() + u"/index.sqlite"_s;
        {
            VaultService svc;
            QString err;
            QVERIFY(svc.open(v.root(), &err, db));
            QVERIFY(waitIndexed(svc));
        }
        {
            // Second open: the cached snapshot arrives before the scan finishes.
            VaultService svc;
            QSignalSpy snap(&svc, &VaultService::snapshotChanged);
            QString err;
            QVERIFY(svc.open(v.root(), &err, db));
            QVERIFY(waitIndexed(svc));
            QVERIFY(snap.count() >= 1);
        }
        {
            // Bump the schema version: the index is rebuilt from the vault.
            IndexStore store;
            QString err;
            QVERIFY(store.open(db, &err));
            QVERIFY(store.db().exec("PRAGMA user_version=999"));
        }
        {
            VaultService svc;
            QString err;
            QVERIFY(svc.open(v.root(), &err, db));
            QVERIFY(waitIndexed(svc));
            const auto hits = await<std::vector<SearchHit>>([&](auto cb) { svc.search(u"fox"_s, cb); });
            QCOMPARE(hits.size(), std::size_t(1));
        }
        // Deleting the cache loses nothing.
        QVERIFY(QFile::remove(db));
        VaultService svc;
        QString err;
        QVERIFY(svc.open(v.root(), &err, db));
        QVERIFY(waitIndexed(svc));
        QVERIFY(svc.snapshot().files().size() > 5);
    }

    void watcherPicksUpChanges()
    {
        test::VaultCopy v(u"index-vault"_s);
        VaultService svc;
        QString err;
        QVERIFY(svc.open(v.root(), &err, v.cacheDir() + u"/index.sqlite"_s));
        QVERIFY(waitIndexed(svc));

        QSignalSpy changed(&svc, &VaultService::filesChangedOnDisk);
        QVERIFY(test::writeBytes(v.path(u"New note.md"_s), "links to [[Target]] #fresh\n"));
        QTRY_VERIFY_WITH_TIMEOUT(svc.files().find(u"New note.md") >= 0, 10000);
        QTRY_VERIFY_WITH_TIMEOUT(
            [&] {
                for (const TagCount& t : svc.snapshot().tags) {
                    if (t.tag == u"fresh") return true;
                }
                return false;
            }(),
            10000);

        // Replace an existing file atomically (as editors and sync tools do).
        QVERIFY(writeFileAtomic(v.path(u"Search me.md"_s), "now about zebras\n").ok);
        QTRY_VERIFY_WITH_TIMEOUT(changed.count() >= 1, 10000);
        QVERIFY(changed.last().at(0).toStringList().contains(u"Search me.md"_s));
        const auto hits = await<std::vector<SearchHit>>([&](auto cb) { svc.search(u"zebras"_s, cb); });
        QCOMPARE(hits.size(), std::size_t(1));

        // In-place writes don't raise directory events; an explicit rescan finds them.
        QTest::qWait(20);
        QVERIFY(test::writeBytes(v.path(u"Search me.md"_s), "now about giraffes, longer text\n"));
        svc.rescan();
        QTRY_VERIFY_WITH_TIMEOUT(
            !await<std::vector<SearchHit>>([&](auto cb) { svc.search(u"giraffes"_s, cb); }).empty(), 10000);

        // New folder with a file.
        QVERIFY(test::writeBytes(v.path(u"newdir/inner.md"_s), "inner\n"));
        QTRY_VERIFY_WITH_TIMEOUT(svc.files().find(u"newdir/inner.md") >= 0, 10000);
        QTRY_VERIFY_WITH_TIMEOUT(svc.snapshot().folders.contains(u"newdir"_s), 10000);

        // Delete a folder.
        QVERIFY(QDir(v.path(u"sub"_s)).removeRecursively());
        QTRY_VERIFY_WITH_TIMEOUT(svc.files().find(u"sub/Linker two.md") < 0, 10000);
        QTRY_VERIFY_WITH_TIMEOUT(!svc.snapshot().folders.contains(u"sub"_s), 10000);
    }

    void ftsQueryBuilder()
    {
        QCOMPARE(IndexStore::ftsQuery(u"hello world"_s), u"\"hello\" \"world\"*"_s);
        QCOMPARE(IndexStore::ftsQuery(u"\"exact phrase\" xy"_s), u"\"exact phrase\" \"xy\"*"_s);
        QCOMPARE(IndexStore::ftsQuery(u"say \"hi"_s), u"\"say\" \"hi\""_s);
        QCOMPARE(IndexStore::ftsQuery(u"   "_s), QString());
        QCOMPARE(IndexStore::ftsQuery(u"word a"_s), u"\"word\" \"a\""_s);
    }
};

QTEST_GUILESS_MAIN(TestIndex)
#include "tst_index.moc"
