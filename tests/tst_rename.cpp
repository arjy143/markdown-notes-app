#include "TestUtil.h"
#include "core/links/RenamePlanner.h"
#include "core/parse/NoteMetadata.h"
#include "core/vault/Paths.h"
#include "core/vault/VaultScanner.h"

using namespace mn;
using namespace Qt::StringLiterals;

namespace {

FileIndex indexOf(const QString& root)
{
    FileIndex idx;
    for (const FileEntry& e : scanVault(root).files)
        idx.add(e.path, e.kind);
    idx.rebuild();
    return idx;
}

NoteLoader loaderFor(const QString& root)
{
    return [root](const QString& p) -> std::optional<QByteArray> {
        QFile f(root + u'/' + p);
        if (!f.open(QIODevice::ReadOnly)) return std::nullopt;
        return f.readAll();
    };
}

QStringList allNotes(const FileIndex& idx)
{
    QStringList out;
    for (const VaultFile& f : idx.files()) {
        if (f.kind == FileKind::Note) out.append(f.path);
    }
    return out;
}

// Every link resolved before the move resolves to the same (moved) file after it.
struct LinkTargets {
    QHash<QString, QStringList> bySource; // source -> resolved target per link ("" if unresolved)
};

LinkTargets targets(const QString& root)
{
    LinkTargets t;
    const FileIndex idx = indexOf(root);
    for (const QString& note : allNotes(idx)) {
        QByteArray raw = test::readBytes(root + u'/' + note);
        if (raw.startsWith("\xEF\xBB\xBF")) raw = raw.mid(3);
        QStringList list;
        for (const LinkRef& l : parseNote(raw).links) {
            const Resolution r = idx.resolve(l.linkpath, note);
            list.append(r.resolved() ? idx.file(r.file).path : QString());
        }
        t.bySource.insert(note, list);
    }
    return t;
}

void checkInvariant(const LinkTargets& before, const LinkTargets& after, const std::vector<Move>& moves)
{
    auto map = [&](const QString& p) {
        for (const Move& m : moves) {
            if (m.from == p) return m.to;
        }
        return p;
    };
    for (auto it = before.bySource.cbegin(); it != before.bySource.cend(); ++it) {
        const QString src = map(it.key());
        const QStringList& b = it.value();
        const QStringList a = after.bySource.value(src);
        QCOMPARE(a.size(), b.size());
        for (qsizetype i = 0; i < b.size(); ++i) {
            if (b[i].isEmpty()) continue;
            QVERIFY2(a[i] == map(b[i]),
                     qPrintable(src + u": link "_s + QString::number(i) + u" -> "_s + a[i]));
        }
    }
}

} // namespace

class TestRename : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void renameInPlaceRewritesEveryStyle()
    {
        test::VaultCopy v(u"rename"_s);
        QVERIFY(v.ok());
        const LinkTargets before = targets(v.root());
        const FileIndex idx = indexOf(v.root());
        const std::vector<Move> moves{{u"Old.md"_s, u"New.md"_s}};
        const RenamePlan plan = planMoves(idx, moves, allNotes(idx), loaderFor(v.root()));
        QVERIFY2(plan.warnings.isEmpty(), qPrintable(plan.warnings.join(u'\n')));

        QStringList changed;
        for (const FileEditPlan& f : plan.files)
            changed.append(f.path);
        changed.sort();
        QCOMPARE(changed, (QStringList{u"Linker.md"_s, u"bom-linker.md"_s, u"crlf-linker.md"_s,
                                       u"folder/Deep linker.md"_s}));

        const RenameResult r = executeRenamePlan(v.root(), plan);
        QVERIFY(r.moved);
        QVERIFY(r.failed.isEmpty());
        QCOMPARE(QString::fromUtf8(test::readBytes(v.path(u"Linker.md"_s))),
                 u"Shortest [[New]], alias [[New|Alias]], heading [[New#Old]], block [[New#^b1]].\n"
                 "With extension [[New.md]], embed ![[New]].\n"
                 "Markdown [md](New.md) and [md heading](New.md#Old).\n"
                 "In code `[[Old]]` stays.\n"
                 "Unrelated [[Sibling]].\n"_s);
        QCOMPARE(test::readBytes(v.path(u"folder/Deep linker.md"_s)),
                 QByteArray("Relative [[../New]] and absolute [[New]] and [rel md](../New.md).\n"));
        QCOMPARE(test::readBytes(v.path(u"crlf-linker.md"_s)),
                 QByteArray("Line one [[New]]\r\nLine two\r\n"));
        QCOMPARE(test::readBytes(v.path(u"bom-linker.md"_s)), QByteArray("\xEF\xBB\xBF"
                                                                         "BOM [[New]]\n"));
        QVERIFY(!QFile::exists(v.path(u"Old.md"_s)));
        QVERIFY(QFile::exists(v.path(u"New.md"_s)));
        checkInvariant(before, targets(v.root()), moves);
    }

    void moveToFolderOnlyTouchesLinksThatWouldBreak()
    {
        test::VaultCopy v(u"rename"_s);
        const LinkTargets before = targets(v.root());
        const FileIndex idx = indexOf(v.root());
        const std::vector<Move> moves{{u"Old.md"_s, u"archive/Old.md"_s}};
        const RenamePlan plan = planMoves(idx, moves, allNotes(idx), loaderFor(v.root()));
        QVERIFY(plan.warnings.isEmpty());
        QStringList changed;
        for (const FileEditPlan& f : plan.files)
            changed.append(f.path);
        changed.sort();
        // Shortest links still resolve by name; relative links (and the moved
        // note's own relative link) are rewritten.
        QCOMPARE(changed, (QStringList{u"Old.md"_s, u"folder/Deep linker.md"_s}));
        const RenameResult r = executeRenamePlan(v.root(), plan);
        QVERIFY(r.moved && r.failed.isEmpty());
        QCOMPARE(test::readBytes(v.path(u"folder/Deep linker.md"_s)),
                 QByteArray(
                     "Relative [[../archive/Old]] and absolute [[Old]] and [rel md](../archive/Old.md).\n"));
        QCOMPARE(test::readBytes(v.path(u"archive/Old.md"_s)),
                 QByteArray("# Old\n\nLinks to [[Sibling]] and [[../Sibling|rel]].\n"));
        checkInvariant(before, targets(v.root()), moves);
    }

    void ambiguousNewNameGetsAPathAndCollateralLinksAreKept()
    {
        test::VaultCopy v(u"rename"_s);
        const LinkTargets before = targets(v.root());
        const FileIndex idx = indexOf(v.root());
        const std::vector<Move> moves{{u"Old.md"_s, u"other/Same name.md"_s}};
        const RenamePlan plan = planMoves(idx, moves, allNotes(idx), loaderFor(v.root()));
        QVERIFY(plan.warnings.isEmpty());
        const RenameResult r = executeRenamePlan(v.root(), plan);
        QVERIFY(r.moved && r.failed.isEmpty());
        // [[Old]] -> full path, because "Same name" is now ambiguous.
        QVERIFY(test::readBytes(v.path(u"Linker.md"_s)).startsWith("Shortest [[other/Same name]]"));
        // other/Linker to same.md pointed at folder/Same name.md; the new
        // same-folder file would steal it, so it gets a path.
        QCOMPARE(test::readBytes(v.path(u"other/Linker to same.md"_s)), QByteArray("[[folder/Same name]]\n"));
        checkInvariant(before, targets(v.root()), moves);
    }

    void attachmentRename()
    {
        test::VaultCopy v(u"rename"_s);
        const LinkTargets before = targets(v.root());
        const FileIndex idx = indexOf(v.root());
        const std::vector<Move> moves{{u"img/pic.png"_s, u"img/picture.png"_s}};
        const RenamePlan plan = planMoves(idx, moves, allNotes(idx), loaderFor(v.root()));
        const RenameResult r = executeRenamePlan(v.root(), plan);
        QVERIFY(r.moved && r.failed.isEmpty());
        QCOMPARE(test::readBytes(v.path(u"Uses image.md"_s)),
                 QByteArray("![[picture.png]] and ![img](img/picture.png)\n"));
        checkInvariant(before, targets(v.root()), moves);
    }

    void markdownPathsAreEncoded()
    {
        test::VaultCopy v(u"rename"_s);
        const FileIndex idx = indexOf(v.root());
        const std::vector<Move> moves{{u"Old.md"_s, u"New name.md"_s}};
        const RenamePlan plan = planMoves(idx, moves, allNotes(idx), loaderFor(v.root()));
        QVERIFY(executeRenamePlan(v.root(), plan).moved);
        const QByteArray linker = test::readBytes(v.path(u"Linker.md"_s));
        QVERIFY(linker.contains("[md](New%20name.md)"));
        QVERIFY(linker.contains("[[New name|Alias]]"));
    }

    void changedFilesAreNotOverwritten()
    {
        test::VaultCopy v(u"rename"_s);
        const FileIndex idx = indexOf(v.root());
        const std::vector<Move> moves{{u"Old.md"_s, u"New.md"_s}};
        const RenamePlan plan = planMoves(idx, moves, allNotes(idx), loaderFor(v.root()));
        QVERIFY(test::writeBytes(v.path(u"Linker.md"_s), "edited meanwhile [[Old]]\n"));
        const RenameResult r = executeRenamePlan(v.root(), plan);
        QVERIFY(r.moved);
        QCOMPARE(r.failed.size(), 1);
        QVERIFY(r.failed[0].startsWith(u"Linker.md"_s));
        QCOMPARE(test::readBytes(v.path(u"Linker.md"_s)), QByteArray("edited meanwhile [[Old]]\n"));
    }

    void existingTargetAbortsBeforeAnyEdit()
    {
        test::VaultCopy v(u"rename"_s);
        const auto before = test::snapshotTree(v.root());
        const FileIndex idx = indexOf(v.root());
        const std::vector<Move> moves{{u"Old.md"_s, u"Sibling.md"_s}};
        const RenamePlan plan = planMoves(idx, moves, allNotes(idx), loaderFor(v.root()));
        const RenameResult r = executeRenamePlan(v.root(), plan);
        QVERIFY(!r.moved);
        QVERIFY(!r.moveError.isEmpty());
        QCOMPARE(test::snapshotTree(v.root()), before);
    }

    void folderMove()
    {
        test::VaultCopy v(u"rename"_s);
        const LinkTargets before = targets(v.root());
        const FileIndex idx = indexOf(v.root());
        std::vector<Move> moves;
        for (const VaultFile& f : idx.files()) {
            if (paths::isInside(f.path, u"folder")) moves.push_back({f.path, u"renamed/"_s + f.path.mid(7)});
        }
        const RenamePlan plan = planMoves(idx, moves, allNotes(idx), loaderFor(v.root()));
        const RenameResult r = executeRenamePlan(v.root(), plan, Move{u"folder"_s, u"renamed"_s});
        QVERIFY(r.moved);
        QVERIFY(r.failed.isEmpty());
        QVERIFY(QFile::exists(v.path(u"renamed/Deep linker.md"_s)));
        checkInvariant(before, targets(v.root()), moves);
    }

    void applyEditsKeepsBom()
    {
        const QByteArray raw = "\xEF\xBB\xBF"
                               "ab[[X]]";
        const QByteArray out = applyEdits(raw, {{4, 5, "Y", 0, {}, {}}});
        QCOMPARE(out, QByteArray("\xEF\xBB\xBF"
                                 "ab[[Y]]"));
    }
};

QTEST_GUILESS_MAIN(TestRename)
#include "tst_rename.moc"
