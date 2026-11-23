#include "TestUtil.h"
#include "core/links/FileIndex.h"
#include "core/vault/Paths.h"
#include "core/vault/VaultScanner.h"

using namespace mn;
using namespace Qt::StringLiterals;

class TestResolver : public QObject {
    Q_OBJECT

    FileIndex index_;

    QString resolve(const QString& link, const QString& source)
    {
        const Resolution r = index_.resolve(link, source);
        return r.resolved() ? index_.file(r.file).path : QString();
    }

private Q_SLOTS:
    void initTestCase()
    {
        const ScanResult scan = scanVault(test::fixture(u"resolve"_s));
        for (const FileEntry& e : scan.files)
            index_.add(e.path, e.kind);
        index_.rebuild();
    }

    void hiddenFilesAreNeverIndexed()
    {
        QCOMPARE(index_.find(u".hidden/Secret.md"), -1);
        QCOMPARE(resolve(u".hidden/Secret"_s, u"Root note.md"_s), QString());
        QCOMPARE(resolve(u"Secret"_s, u"Root note.md"_s), QString());
    }

    void mdExtensionIsOptional()
    {
        QCOMPARE(resolve(u"Case Test"_s, u"Root note.md"_s), u"Case Test.md"_s);
        QCOMPARE(resolve(u"Case Test.md"_s, u"Root note.md"_s), u"Case Test.md"_s);
    }

    void caseInsensitiveButKeepsRealCase()
    {
        QCOMPARE(resolve(u"case test"_s, u"Root note.md"_s), u"Case Test.md"_s);
        QCOMPARE(resolve(u"ATTACHMENTS/IMAGE.PNG"_s, u"Root note.md"_s), u"attachments/image.png"_s);
    }

    void attachmentsNeedTheirExtension()
    {
        QCOMPARE(resolve(u"image.png"_s, u"Root note.md"_s), u"attachments/image.png"_s);
        QCOMPARE(resolve(u"image"_s, u"Root note.md"_s), QString());
        QCOMPARE(resolve(u"doc.pdf"_s, u"Root note.md"_s), u"attachments/doc.pdf"_s);
    }

    void dotsInNoteNames()
    {
        QCOMPARE(resolve(u"Meeting 2024.01.05"_s, u"Root note.md"_s), u"Meeting 2024.01.05.md"_s);
    }

    void exactPathFromRootWinsOverSameFolder()
    {
        // Step 2 (exact path from root) comes before the same-folder tie-break.
        QCOMPARE(resolve(u"Note"_s, u"x/links-from-x.md"_s), u"Note.md"_s);
        QCOMPARE(resolve(u"x/Note"_s, u"Root note.md"_s), u"x/Note.md"_s);
    }

    void relativePaths()
    {
        QCOMPARE(resolve(u"../Parent"_s, u"folder/sub/Child.md"_s), u"folder/Parent.md"_s);
        QCOMPARE(resolve(u"./Sibling"_s, u"folder/sub/Child.md"_s), u"folder/sub/Sibling.md"_s);
        QCOMPARE(resolve(u"../../../escape"_s, u"folder/sub/Child.md"_s), QString());
        QCOMPARE(index_.resolve(u"../Parent", u"folder/sub/Child.md").step, ResolveStep::Relative);
    }

    void suffixNameLookup()
    {
        QCOMPARE(resolve(u"sub/Sibling"_s, u"Root note.md"_s), u"folder/sub/Sibling.md"_s);
        QCOMPARE(index_.resolve(u"sub/Sibling", u"Root note.md").step, ResolveStep::Name);
    }

    void tieBreakPrefersSameFolderThenShortestThenAlphabetical()
    {
        QCOMPARE(resolve(u"Same"_s, u"a/source-in-a.md"_s), u"a/Same.md"_s);
        QCOMPARE(resolve(u"Same"_s, u"a/deep/source-in-deep.md"_s), u"a/deep/Same.md"_s);
        // a/Same.md and b/Same.md have the same length: alphabetical.
        QCOMPARE(resolve(u"Same"_s, u"c/source-in-c.md"_s), u"a/Same.md"_s);
    }

    void emptyLinkpathIsTheSourceNote()
    {
        const Resolution r = index_.resolve(u"", u"Root note.md");
        QCOMPARE(r.step, ResolveStep::Self);
        QCOMPARE(index_.file(r.file).path, u"Root note.md"_s);
    }

    void unresolved() { QVERIFY(!index_.resolve(u"Nowhere", u"Root note.md").resolved()); }

    void linkpathGeneration()
    {
        QCOMPARE(index_.linkpathFor(u"Case Test.md", u"a/source-in-a.md", LinkStyle::Shortest, false),
                 u"Case Test"_s);
        // Ambiguous names get the full path.
        QCOMPARE(index_.linkpathFor(u"b/Same.md", u"Root note.md", LinkStyle::Shortest, false), u"b/Same"_s);
        QCOMPARE(index_.linkpathFor(u"folder/Parent.md", u"folder/sub/Child.md", LinkStyle::Relative, false),
                 u"../Parent"_s);
        QCOMPARE(index_.linkpathFor(u"attachments/image.png", u"Root note.md", LinkStyle::Absolute, false),
                 u"attachments/image.png"_s);
    }

    void pathHelpers()
    {
        QCOMPARE(paths::normalize(u"a/./b/../c"), std::optional<QString>(u"a/c"_s));
        QVERIFY(!paths::normalize(u"../x"));
        QCOMPARE(paths::relativePath(u"a/b", u"a/c/d.md"), u"../c/d.md"_s);
        QCOMPARE(paths::relativePath(u"", u"x.md"), u"x.md"_s);
        QCOMPARE(paths::linkName(u"f/Note.md"), u"Note"_s);
        QVERIFY(paths::isInside(u"a/b/c.md", u"A/b"));
        QVERIFY(!paths::isInside(u"ab/c.md", u"a"));
    }
};

QTEST_GUILESS_MAIN(TestResolver)
#include "tst_resolver.moc"
