#include "TestUtil.h"
#include "core/index/TagTree.h"
#include "core/index/VaultService.h"

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

QStringList names(const QStringList& paths)
{
    QStringList out;
    for (const QString& p : paths)
        out.append(QFileInfo(p).completeBaseName());
    out.sort();
    return out;
}

const QStringList kCategories{u"tech"_s, u"projects"_s, u"research"_s, u"personal"_s, u"meta"_s};

} // namespace

class TestTags : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void categoriesAndPropertyTags()
    {
        test::VaultCopy v(u"tags-vault"_s);
        VaultService svc;
        QVERIFY(openIndexed(svc, v));
        VaultSettings s = svc.settings();
        s.categoryTags = kCategories;
        s.minCategoryTags = 2;
        QString err;
        QVERIFY(svc.setSettings(s, &err));

        // Only `tags:` counts (as in Obsidian): ESP32 and Money lack category tags.
        QCOMPARE(names(await<QStringList>([&](auto cb) { svc.filesWithTag(u"tech"_s, cb); })),
                 (QStringList{u"Bootloader"_s, u"Interrupts"_s, u"Loose"_s, u"NumPy"_s, u"Paging"_s,
                              u"Pandas"_s}));
        HealthReport r = await<HealthReport>([&](auto cb) { svc.healthReport(cb); });
        QCOMPARE(r.minCategories, 2);
        QStringList few;
        for (const CategoryProblem& c : r.fewCategories)
            few.append(c.path);
        QCOMPARE(names(few), (QStringList{u"ESP32"_s, u"Loose"_s, u"Money"_s})); // templates skipped

        // Count the primary/secondary properties as tags: the vault re-indexes.
        QSignalSpy done(&svc, &VaultService::indexingFinished);
        s.tagProperties = {u"primary"_s, u"secondary"_s};
        QVERIFY(svc.setSettings(s, &err));
        QVERIFY(done.wait(15000));
        QVERIFY(
            await<QStringList>([&](auto cb) { svc.filesWithTag(u"tech"_s, cb); }).contains(u"ESP32.md"_s));
        r = await<HealthReport>([&](auto cb) { svc.healthReport(cb); });
        few.clear();
        for (const CategoryProblem& c : r.fewCategories)
            few.append(c.path);
        QCOMPARE(names(few), QStringList{u"Loose"_s});
    }

    void propertyTagsSurviveRestart()
    {
        test::VaultCopy v(u"tags-vault"_s);
        {
            VaultService svc;
            QVERIFY(openIndexed(svc, v));
        }
        // Settings changed while the app was closed: the next open re-parses.
        VaultSettings s = loadVaultSettings(v.root()).settings;
        s.tagProperties = {u"primary"_s};
        QString err;
        QVERIFY(saveVaultSettings(v.root(), s, &err));
        VaultService svc;
        QVERIFY(openIndexed(svc, v));
        QVERIFY(await<QStringList>([&](auto cb) {
                    svc.filesWithTag(u"personal"_s, cb);
                }).contains(u"Money.md"_s));
    }

    void detectsTheScheme()
    {
        test::VaultCopy v(u"tags-vault"_s);
        VaultService svc;
        QVERIFY(openIndexed(svc, v));
        const TagScheme scheme = await<TagScheme>([&](auto cb) { svc.detectTagScheme(cb); });
        QCOMPARE(scheme.properties, (QStringList{u"primary"_s, u"secondary"_s}));
        QCOMPARE(scheme.categories.mid(0, 3), (QStringList{u"tech"_s, u"projects"_s, u"research"_s}));
        QVERIFY(!scheme.categories.contains(u"summary"_s));
    }

    void drillDownTree()
    {
        const std::vector<NoteTags> notes{
            {u"Bootloader.md"_s, {u"tech"_s, u"projects"_s, u"assembly"_s, u"os"_s}},
            {u"Paging.md"_s, {u"tech"_s, u"projects"_s, u"os"_s, u"memory"_s}},
            {u"Interrupts.md"_s, {u"tech"_s, u"projects"_s, u"os"_s}},
            {u"Pandas.md"_s, {u"tech"_s, u"research"_s, u"python"_s, u"data"_s}},
            {u"NumPy.md"_s, {u"tech"_s, u"research"_s, u"python"_s}},
            {u"ESP32.md"_s, {u"tech"_s, u"projects"_s, u"esp32"_s, u"microcontrollers"_s}},
            {u"Loose.md"_s, {u"tech"_s}},
            {u"Money.md"_s, {u"personal"_s, u"meta"_s, u"money"_s}},
        };
        const QHash<QString, int> counts{
            {u"tech"_s, 7},   {u"projects"_s, 4},        {u"os"_s, 3},     {u"research"_s, 2},
            {u"python"_s, 2}, {u"assembly"_s, 1},        {u"memory"_s, 1}, {u"data"_s, 1},
            {u"esp32"_s, 1},  {u"microcontrollers"_s, 1}};
        TagTreeOptions o;
        o.categories = kCategories;
        const TagTreeNode root = buildTagTree(u"Tech"_s, notes, counts, o);
        QCOMPARE(root.noteCount, 7);
        QCOMPARE(root.notes, QStringList{u"Loose.md"_s});
        QCOMPARE(root.children.size(), std::size_t(2));
        const TagTreeNode& projects = root.children[0];
        QCOMPARE(projects.tag, u"projects"_s);
        QCOMPARE(projects.noteCount, 4);
        QCOMPARE(projects.notes, QStringList{u"ESP32.md"_s}); // its own branch had one note: flattened
        QCOMPARE(projects.children.size(), std::size_t(1));
        QCOMPARE(projects.children[0].tag, u"os"_s);
        QCOMPARE(projects.children[0].notes,
                 (QStringList{u"Bootloader.md"_s, u"Interrupts.md"_s, u"Paging.md"_s}));
        const TagTreeNode& research = root.children[1];
        QCOMPARE(research.tag, u"research"_s);
        QCOMPARE(research.children.size(), std::size_t(1));
        QCOMPARE(research.children[0].tag, u"python"_s);
        QCOMPARE(research.children[0].notes, (QStringList{u"NumPy.md"_s, u"Pandas.md"_s}));
        // Each note appears exactly once.
        int total = 0;
        std::function<void(const TagTreeNode&)> count = [&](const TagTreeNode& n) {
            total += static_cast<int>(n.notes.size());
            for (const TagTreeNode& c : n.children)
                count(c);
        };
        count(root);
        QCOMPARE(total, 7);
        // Nested tags: "a" covers "a/b".
        const TagTreeNode nested =
            buildTagTree(u"aws"_s, {{u"x.md"_s, {u"aws/s3"_s}}, {u"y.md"_s, {u"aws"_s}}}, {}, {});
        QCOMPARE(nested.noteCount, 2);
    }

    void noteTagsFromTheIndex()
    {
        test::VaultCopy v(u"tags-vault"_s);
        VaultService svc;
        QVERIFY(openIndexed(svc, v));
        const auto notes = await<std::vector<NoteTags>>([&](auto cb) { svc.noteTags(cb); });
        QCOMPARE(notes.size(), std::size_t(9));
        const TagTreeNode root = buildTagTree(u"tech"_s, notes, {}, {});
        QCOMPARE(root.noteCount, 6);
    }

    void searchWithTagFilters()
    {
        test::VaultCopy v(u"tags-vault"_s);
        VaultService svc;
        QVERIFY(openIndexed(svc, v));
        auto search = [&](const QString& q) {
            QStringList out;
            for (const SearchHit& h : await<std::vector<SearchHit>>([&](auto cb) { svc.search(q, cb); }))
                out.append(h.path);
            return names(out);
        };
        QCOMPARE(search(u"#tech #research"_s), (QStringList{u"NumPy"_s, u"Pandas"_s}));
        QCOMPARE(search(u"paging #research"_s), QStringList{u"Pandas"_s});
        QCOMPARE(search(u"paging"_s), (QStringList{u"Bootloader"_s, u"Paging"_s, u"Pandas"_s}));
        QCOMPARE(search(u"#OS"_s), (QStringList{u"Bootloader"_s, u"Interrupts"_s, u"Paging"_s}));
        QCOMPARE(search(u"#nothing"_s), QStringList());
    }
};

QTEST_GUILESS_MAIN(TestTags)
#include "tst_tags.moc"
