#include "TestUtil.h"
#include "core/settings/VaultSettings.h"

using namespace mn;
using namespace Qt::StringLiterals;

class TestSettings : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void importsFromObsidianOnceAndNeverWritesThere()
    {
        test::VaultCopy v(u"settings-vault"_s);
        const auto obsidianBefore = test::snapshotTree(v.path(u".obsidian"_s));
        const SettingsLoadResult r = loadVaultSettings(v.root());
        QVERIFY(r.imported);
        QVERIFY(r.warning.isEmpty());
        const VaultSettings& s = r.settings;
        QCOMPARE(s.attachmentFolderPath, u"./assets"_s);
        QCOMPARE(s.newLinkFormat, LinkStyle::Relative);
        QVERIFY(s.useMarkdownLinks);
        QCOMPARE(s.newFileLocation, NewFileLocation::Folder);
        QCOMPARE(s.newFileFolderPath, u"Inbox"_s);
        QVERIFY(s.alwaysUpdateLinks);
        QVERIFY(s.strictLineBreaks);
        QCOMPARE(s.templatesFolder, u"Templates"_s);
        QCOMPARE(s.dateFormat, u"DD/MM/YYYY"_s);
        QCOMPARE(s.timeFormat, u"h:mm A"_s);
        QVERIFY(QFile::exists(settingsFilePath(v.root())));
        QCOMPARE(test::snapshotTree(v.path(u".obsidian"_s)), obsidianBefore);

        // Second load reads settings.json, even if .obsidian changes.
        QVERIFY(test::writeBytes(v.path(u".obsidian/app.json"_s), "{\"newLinkFormat\": \"absolute\"}"));
        const SettingsLoadResult again = loadVaultSettings(v.root());
        QVERIFY(!again.imported);
        QCOMPARE(again.settings, s);
    }

    void defaultsWithoutObsidianFolder()
    {
        test::VaultCopy v(u"settings-empty-vault"_s);
        const SettingsLoadResult r = loadVaultSettings(v.root());
        QVERIFY(r.imported);
        QCOMPARE(r.settings, VaultSettings{});
        QVERIFY(!QFile::exists(v.path(u".obsidian"_s)));
    }

    void derivedFolders()
    {
        VaultSettings s;
        QCOMPARE(s.attachmentFolderFor(u"a/b/note.md"), QString());
        s.attachmentFolderPath = u"./"_s;
        QCOMPARE(s.attachmentFolderFor(u"a/b/note.md"), u"a/b"_s);
        s.attachmentFolderPath = u"./assets"_s;
        QCOMPARE(s.attachmentFolderFor(u"a/b/note.md"), u"a/b/assets"_s);
        QCOMPARE(s.attachmentFolderFor(u"note.md"), u"assets"_s);
        s.attachmentFolderPath = u"Attachments/Images"_s;
        QCOMPARE(s.attachmentFolderFor(u"a/note.md"), u"Attachments/Images"_s);

        QCOMPARE(s.newNoteFolder(u"x/y.md"), QString());
        s.newFileLocation = NewFileLocation::Current;
        QCOMPARE(s.newNoteFolder(u"x/y.md"), u"x"_s);
        s.newFileLocation = NewFileLocation::Folder;
        s.newFileFolderPath = u"/Inbox/"_s;
        QCOMPARE(s.newNoteFolder(u"x/y.md"), u"Inbox"_s);
    }

    void roundTrip()
    {
        QTemporaryDir dir;
        VaultSettings s;
        s.showAllFiles = true;
        s.dateFormat = u"YYYY"_s;
        s.tagProperties = {u"primary"_s, u"secondary"_s};
        s.categoryTags = {u"tech"_s, u"projects"_s};
        s.minCategoryTags = 3;
        s.pinnedTags = {u"projects"_s, u"tech/ai"_s};
        QString err;
        QVERIFY(saveVaultSettings(dir.path(), s, &err));
        QCOMPARE(loadVaultSettings(dir.path()).settings, s);
    }
};

QTEST_GUILESS_MAIN(TestSettings)
#include "tst_settings.moc"
