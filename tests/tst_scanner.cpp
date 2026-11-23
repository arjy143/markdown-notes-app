#include "TestUtil.h"
#include "core/vault/FileTypes.h"
#include "core/vault/VaultScanner.h"

using namespace mn;
using namespace Qt::StringLiterals;

class TestScanner : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void skipsHiddenAndClassifies()
    {
        const ScanResult r = scanVault(test::fixture(u"index-vault"_s));
        QStringList paths;
        for (const FileEntry& e : r.files)
            paths.append(e.path);
        QVERIFY(paths.contains(u"Target.md"_s));
        QVERIFY(paths.contains(u"pics/photo.png"_s));
        for (const QString& p : paths) {
            QVERIFY2(!p.contains(u"/."_s) && !p.startsWith(u'.'), qPrintable(p));
        }
        // Empty folders still appear (its only file is hidden).
        QVERIFY(r.folders.contains(u"empty folder"_s));
        QVERIFY(!r.folders.contains(u".obsidian"_s));
        QVERIFY(!r.folders.contains(u".trash"_s));
    }

    void scanningNeverWrites()
    {
        const auto before = test::snapshotTree(test::fixture(u"index-vault"_s));
        scanVault(test::fixture(u"index-vault"_s));
        QCOMPARE(test::snapshotTree(test::fixture(u"index-vault"_s)), before);
    }

    void fileKinds()
    {
        QCOMPARE(classifyPath(u"a/b.MD"), FileKind::Note);
        QCOMPARE(classifyPath(u"x.jpeg"), FileKind::Image);
        QCOMPARE(classifyPath(u"x.avif"), FileKind::Image);
        QCOMPARE(classifyPath(u"x.pdf"), FileKind::Pdf);
        QCOMPARE(classifyPath(u"x.mkv"), FileKind::Media);
        QCOMPARE(classifyPath(u"x.csv"), FileKind::Other);
        QCOMPARE(classifyPath(u".md"), FileKind::Other);
        QVERIFY(isHiddenName(u".obsidian"));
    }

    void newFileNames()
    {
        QVERIFY(checkNewFileName(u"Good name").isEmpty());
        QVERIFY(!checkNewFileName(u"bad:name").isEmpty());
        QVERIFY(!checkNewFileName(u"bad#name").isEmpty());
        QVERIFY(!checkNewFileName(u"a|b").isEmpty());
        QVERIFY(!checkNewFileName(u"  ").isEmpty());
        QVERIFY(!checkNewFileName(u".hidden").isEmpty());
    }

    void newPathsNeverEnterHiddenFolders()
    {
        QVERIFY(checkNewVaultPath(u"a/b/Note").isEmpty());
        QVERIFY(!checkNewVaultPath(u".obsidian/x").isEmpty());
        QVERIFY(!checkNewVaultPath(u"a/.trash/x").isEmpty());
        QVERIFY(!checkNewVaultPath(u"../outside").isEmpty());
        QVERIFY(!checkNewVaultPath(u"a/../b").isEmpty());
        QVERIFY(!checkNewVaultPath(u"/abs").isEmpty());
        QVERIFY(!checkNewVaultPath(u"a//b").isEmpty());
        QVERIFY(!checkNewVaultPath(u"").isEmpty());
    }

    void singleFolder()
    {
        const ScanResult r = scanFolder(test::fixture(u"index-vault"_s), u"sub"_s);
        QCOMPARE(r.files.size(), std::size_t(1));
        QCOMPARE(r.files[0].path, u"sub/Linker two.md"_s);
    }
};

QTEST_GUILESS_MAIN(TestScanner)
#include "tst_scanner.moc"
