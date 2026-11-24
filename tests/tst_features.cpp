#include "TestUtil.h"
#include "core/capture/Capture.h"
#include "core/index/Similarity.h"
#include "core/index/VaultService.h"
#include "core/links/LinkSuggest.h"
#include "core/links/NewLink.h"
#include "core/parse/NoteMetadata.h"
#include "core/tasks/TaskEdit.h"

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

} // namespace

class TestFeatures : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void tasksAreParsed()
    {
        const NoteMetadata m = parseNote(test::readBytes(test::fixture(u"parse/tasks.md"_s)));
        QCOMPARE(m.tasks.size(), std::size_t(4));
        QCOMPARE(m.tasks[0].text, u"open task #work 📅 2025-01-31"_s);
        QVERIFY(!m.tasks[0].done);
        QCOMPARE(m.tasks[0].due, u"2025-01-31"_s);
        QCOMPARE(m.tasks[0].line, 0);
        QVERIFY(m.tasks[1].done);
        QCOMPARE(m.tasks[1].due, u"2024-12-01"_s);
        QCOMPARE(m.tasks[1].text, u"done task due:2024-12-01"_s); // nested items aren't part of the parent
        QCOMPARE(m.tasks[2].text, u"nested child"_s);
        QCOMPARE(m.tasks[2].line, 2);
        QVERIFY(m.tasks[3].done);
        QCOMPARE(m.tasks[3].due, u"2025-02-01"_s);
        QCOMPARE(m.tasks[3].line, 4);
    }

    void taskEdits()
    {
        const QByteArray body = test::readBytes(test::fixture(u"parse/tasks.md"_s));
        auto apply = [&](const QByteArray& b, const TaskLocator& t, TaskAction a) -> QByteArray {
            QString err;
            const auto e = planTaskEdit(b, t, a, &err);
            if (!e) return "ERROR: " + err.toUtf8();
            QByteArray out = b;
            out.replace(e->start, e->end - e->start, e->replacement);
            return out;
        };
        const QByteArray done = apply(body, {0, u"open task #work 📅 2025-01-31"_s}, TaskAction::MarkDone);
        QVERIFY(done.startsWith("- [x] open task #work"));
        QCOMPARE(done.size(), body.size()); // exactly one byte changed
        const QByteArray reopened = apply(body, {1, u"done task due:2024-12-01"_s}, TaskAction::MarkOpen);
        QVERIFY(reopened.contains("- [ ] done task due:2024-12-01\n"));
        // Deleting a task removes its nested items too, nothing else.
        const QByteArray deleted = apply(body, {1, u"done task due:2024-12-01"_s}, TaskAction::Delete);
        QVERIFY(deleted.startsWith("- [ ] open task #work 📅 2025-01-31\n- normal item\n"));
        QVERIFY(!deleted.contains("nested child"));
        // The note moved since indexing: found by its (unique) text.
        const QByteArray shifted = "intro\n\n" + body;
        QVERIFY(
            apply(shifted, {2, u"nested child"_s}, TaskAction::MarkDone).contains("  - [x] nested child"));
        // Unknown or ambiguous tasks are refused.
        QVERIFY(apply(body, {0, u"not a task"_s}, TaskAction::MarkDone).startsWith("ERROR"));
        QVERIFY(apply("- [ ] same\n- [ ] same\n", {5, u"same"_s}, TaskAction::Delete).startsWith("ERROR"));
    }

    void taskEditsInFiles()
    {
        QTemporaryDir dir;
        const QString path = dir.path() + u"/n.md"_s;
        QVERIFY(test::writeBytes(path, "\xEF\xBB\xBFTitle\r\n- [ ] one\r\n- [ ] two\r\n"));
        QVERIFY(editTaskInFile(path, {1, u"one"_s}, TaskAction::MarkDone).ok);
        QCOMPARE(test::readBytes(path), QByteArray("\xEF\xBB\xBFTitle\r\n- [x] one\r\n- [ ] two\r\n"));
        QVERIFY(editTaskInFile(path, {2, u"two"_s}, TaskAction::Delete).ok);
        QCOMPARE(test::readBytes(path), QByteArray("\xEF\xBB\xBFTitle\r\n- [x] one\r\n"));
        const TaskEditResult bad = editTaskInFile(path, {2, u"two"_s}, TaskAction::Delete);
        QVERIFY(!bad.ok);
        QCOMPARE(test::readBytes(path), QByteArray("\xEF\xBB\xBFTitle\r\n- [x] one\r\n"));
    }

    void dueDates()
    {
        QCOMPARE(parseDueDate(u"x 📅2025-03-04"), u"2025-03-04"_s);
        QCOMPARE(parseDueDate(u"x DUE: 2025-03-04"), u"2025-03-04"_s);
        QCOMPARE(parseDueDate(u"overdue:2025-03-04"), QString());
        QCOMPARE(parseDueDate(u"no date"), QString());
    }

    void tasksAreIndexed()
    {
        test::VaultCopy v(u"ui-vault"_s);
        VaultService svc;
        QVERIFY(openIndexed(svc, v));
        const auto tasks = await<std::vector<TaskInfo>>([&](auto cb) { svc.tasks(cb); });
        QCOMPARE(tasks.size(), std::size_t(2));
        QCOMPARE(tasks[0].path, u"Home.md"_s);
        QCOMPARE(tasks[0].text, u"open task"_s);
        QVERIFY(tasks[1].done);
    }

    void tokenizerMatchesFts()
    {
        const std::vector<QString> t = sim::tokenize(u"Café-style, naïve résumé x2!");
        QCOMPARE(QStringList(t.begin(), t.end()),
                 (QStringList{u"cafe"_s, u"style"_s, u"naive"_s, u"resume"_s, u"x2"_s}));
        const QHash<QString, int> tf = sim::termFrequencies(u"the vector and the vector of 2024 at");
        QCOMPARE(tf.value(u"vector"_s), 2);
        QVERIFY(!tf.contains(u"the"_s));
        QVERIFY(!tf.contains(u"2024"_s));
        QCOMPARE(
            sim::contentText(u"---\ntags: [x]\n---\nSee [text](https://example.com/a) and https://x.y/z"),
            u"See [text] and "_s);
    }

    void minhashFindsNearDuplicatesOnly()
    {
        auto words = [](int seed, int n) {
            QString s;
            for (int i = 0; i < n; ++i)
                s += u"w"_s + QString::number((i * 7 + seed * 13) % 997) + u' ';
            return s;
        };
        const QString a = words(1, 200);
        QString b = a;
        b.replace(u"w20 "_s, u"changed "_s);
        const QString c = words(5, 200);
        std::vector<sim::DocShingles> docs{
            {u"a"_s, sim::shingles(a)}, {u"b"_s, sim::shingles(b)}, {u"c"_s, sim::shingles(c)}};
        const auto pairs = sim::nearDuplicates(docs, 0.5);
        QCOMPARE(pairs.size(), std::size_t(1));
        QCOMPARE(pairs[0].a, u"a"_s);
        QCOMPARE(pairs[0].b, u"b"_s);
        QVERIFY(pairs[0].similarity > 0.9);
    }

    void healthReport()
    {
        test::VaultCopy v(u"health-vault"_s);
        const auto before = test::snapshotTree(v.root());
        VaultService svc;
        QVERIFY(openIndexed(svc, v));
        const HealthReport r = await<HealthReport>([&](auto cb) { svc.healthReport(cb); });

        QCOMPARE(r.brokenLinks.size(), std::size_t(1));
        QCOMPARE(r.brokenLinks[0].linkpath, u"Missing"_s);
        QStringList orphans = r.orphanNotes;
        orphans.sort();
        // A and used.png are linked; templates are skipped.
        QCOMPARE(orphans, (QStringList{u"Cooking pasta.md"_s, u"Cosine similarity copy.md"_s,
                                       u"Cosine similarity.md"_s, u"Empty.md"_s, u"Hub.md"_s, u"Orphan.md"_s,
                                       u"Project log.md"_s, u"Vector math.md"_s}));
        QCOMPARE(r.emptyNotes, QStringList{u"Empty.md"_s});
        QCOMPARE(r.unusedAttachments, QStringList{u"unused.png"_s});
        QStringList groups;
        for (const TagGroup& g : r.similarTags) {
            QStringList t = g.tags;
            t.sort(Qt::CaseInsensitive);
            groups.append(t.join(u'|'));
        }
        groups.sort();
        QCOMPARE(groups, (QStringList{u"project|Projects"_s, u"to-do|todo"_s}));
        QCOMPARE(r.duplicateNotes.size(), std::size_t(1));
        QStringList pair{r.duplicateNotes[0].a, r.duplicateNotes[0].b};
        pair.sort();
        QCOMPARE(pair, (QStringList{u"Cosine similarity copy.md"_s, u"Cosine similarity.md"_s}));

        // The report only reads.
        auto after = test::snapshotTree(v.root());
        after.remove(u".markdown-notes/settings.json"_s);
        QCOMPARE(after, before);
    }

    void appendingKeepsExistingBytes()
    {
        QCOMPARE(*appendToNote("a\nb\n", u"- x"_s), QByteArray("a\nb\n- x\n"));
        QCOMPARE(*appendToNote("a\r\nb", u"- x\n  y"_s), QByteArray("a\r\nb\r\n- x\r\n  y\r\n"));
        QCOMPARE(*appendToNote("\xEF\xBB\xBF", u"- x"_s), QByteArray("\xEF\xBB\xBF- x\n"));
        QCOMPARE(*appendToNote("", u"- x"_s), QByteArray("- x\n"));
        QVERIFY(!appendToNote(QByteArray("bin\0ary", 8), u"x"_s));
        const QByteArray mixed = "one\r\ntwo\nthree";
        QVERIFY(appendToNote(mixed, u"x"_s)->startsWith(mixed));
    }

    void captureFormatting()
    {
        VaultSettings s;
        const QDateTime at(QDate(2025, 3, 4), QTime(9, 5));
        QCOMPARE(formatCapture(s, u"  idea  "_s, at), u"- 09:05 idea"_s);
        QCOMPARE(formatCapture(s, u"line one\nline two"_s, at), u"- 09:05 line one\n  line two"_s);
        s.captureFormat = u"{{date}}: {{text}}"_s;
        QCOMPARE(formatCapture(s, u"x"_s, at), u"2025-03-04: x"_s);
        QCOMPARE(captureTargetPath(VaultSettings{}, at), u"Inbox.md"_s);
        s.captureToDailyNote = true;
        s.dailyNotesFolder = u"Journal"_s;
        s.dailyNoteFormat = u"YYYY/MM-DD"_s;
        QCOMPARE(captureTargetPath(s, at), u"Journal/2025/03-04.md"_s);
    }

    void captureToInboxAndDailyNote()
    {
        test::VaultCopy v(u"health-vault"_s);
        const auto before = test::snapshotTree(v.root());
        VaultSettings s;
        const QDateTime at(QDate(2025, 3, 4), QTime(9, 5));
        CaptureResult r = appendCapture(v.root(), s, u"first"_s, at);
        QVERIFY2(r.ok, qPrintable(r.error));
        QVERIFY(r.created);
        r = appendCapture(v.root(), s, u"second"_s, at);
        QVERIFY(r.ok && !r.created);
        QCOMPARE(test::readBytes(v.path(u"Inbox.md"_s)), QByteArray("- 09:05 first\n- 09:05 second\n"));

        s.captureToDailyNote = true;
        s.dailyNotesFolder = u"Daily"_s;
        s.dailyNoteTemplate = u"templates/Daily.md"_s;
        r = appendCapture(v.root(), s, u"in the daily note"_s, at);
        QVERIFY2(r.ok, qPrintable(r.error));
        QCOMPARE(r.path, u"Daily/2025-03-04.md"_s);
        QCOMPARE(test::readBytes(v.path(u"Daily/2025-03-04.md"_s)),
                 QByteArray("# 2025-03-04\n- 09:05 in the daily note\n"));

        // Nothing else was touched.
        auto after = test::snapshotTree(v.root());
        after.remove(u"Inbox.md"_s);
        after.remove(u"Daily/2025-03-04.md"_s);
        QCOMPARE(after, before);

        // Hidden or escaping targets are refused.
        VaultSettings bad;
        bad.captureNote = u".obsidian/inbox.md"_s;
        QVERIFY(!appendCapture(v.root(), bad, u"x"_s).ok);
        bad.captureNote = u"../outside.md"_s;
        QVERIFY(!appendCapture(v.root(), bad, u"x"_s).ok);
        QVERIFY(!QFile::exists(v.path(u".obsidian/inbox.md"_s)));
        QVERIFY(!appendCapture(v.root(), VaultSettings{}, u"   "_s).ok);
    }

    void newFileLinksFollowTheLinkSettings()
    {
        VaultSettings s;
        const QString img = u"Files/a b.png"_s;
        QCOMPARE(newFileLink(s, img, u"Notes/n.md"_s, false, true), u"![[a b.png]]"_s);
        QCOMPARE(newFileLink(s, img, u"Notes/n.md"_s, true, true), u"![[Files/a b.png]]"_s);
        QCOMPARE(newFileLink(s, u"Files/Note.md"_s, u"n.md"_s, false, false), u"[[Note]]"_s);
        s.newLinkFormat = LinkStyle::Relative;
        QCOMPARE(newFileLink(s, img, u"Notes/n.md"_s, false, true), u"![[../Files/a b.png]]"_s);
        s.newLinkFormat = LinkStyle::Absolute;
        QCOMPARE(newFileLink(s, img, u"Notes/n.md"_s, false, true), u"![[Files/a b.png]]"_s);
        s.useMarkdownLinks = true;
        QCOMPARE(newFileLink(s, img, u"Notes/n.md"_s, false, true), u"![a b](Files/a%20b.png)"_s);
        s.newLinkFormat = LinkStyle::Shortest;
        QCOMPARE(newFileLink(s, img, u"Notes/n.md"_s, false, false), u"[a b](a%20b.png)"_s);
    }

    void captureSavesImagesAndEmbedsThem()
    {
        test::VaultCopy v(u"health-vault"_s);
        const auto before = test::snapshotTree(v.root());
        VaultSettings s;
        s.captureNote = u"Notes/Inbox.md"_s;
        s.attachmentFolderPath = u"./assets"_s;
        const QDateTime at(QDate(2025, 3, 4), QTime(9, 5));
        const QString base = u"Pasted image 20250304090500"_s;
        const QList<CaptureFile> two{{base, u"png"_s, "first"}, {base, u"png"_s, "second"}};
        CaptureResult r = appendCapture(v.root(), s, u"look at this"_s, at, two);
        QVERIFY2(r.ok, qPrintable(r.error));
        QCOMPARE(r.attachments, QStringList({u"Notes/assets/Pasted image 20250304090500.png"_s,
                                             u"Notes/assets/Pasted image 20250304090500 1.png"_s}));
        QCOMPARE(test::readBytes(v.path(r.attachments[0])), QByteArray("first"));
        QCOMPARE(test::readBytes(v.path(r.attachments[1])), QByteArray("second"));
        QCOMPARE(test::readBytes(v.path(u"Notes/Inbox.md"_s)),
                 QByteArray("- 09:05 look at this\n"
                            "  ![[Pasted image 20250304090500.png]]\n"
                            "  ![[Pasted image 20250304090500 1.png]]\n"));

        // Only a drawing, no text; a name used elsewhere gets the full path.
        r = appendCapture(v.root(), s, u"  "_s, at, {{u"Drawing 1"_s, u"png"_s, "d"}},
                          [](const QString& name) { return name == u"Drawing 1.png"_s; });
        QVERIFY2(r.ok, qPrintable(r.error));
        QVERIFY(test::readBytes(v.path(u"Notes/Inbox.md"_s))
                    .endsWith("- 09:05 ![[Notes/assets/Drawing 1.png]]\n"));

        // Nothing else was touched.
        auto after = test::snapshotTree(v.root());
        for (const QString& p : {u"Notes/Inbox.md"_s, u"Notes/assets/Drawing 1.png"_s,
                                 u"Notes/assets/Pasted image 20250304090500.png"_s,
                                 u"Notes/assets/Pasted image 20250304090500 1.png"_s})
            after.remove(p);
        QCOMPARE(after, before);
    }

    void captureWithImagesWritesNothingOnFailure()
    {
        test::VaultCopy v(u"health-vault"_s);
        const QDateTime at(QDate(2025, 3, 4), QTime(9, 5));
        const QList<CaptureFile> img{{u"Pasted image x"_s, u"png"_s, "png"}};
        auto attachmentCount = [&] { return QDir(v.path(u"Attachments"_s)).entryList(QDir::Files).size(); };
        VaultSettings s;
        s.attachmentFolderPath = u"Attachments"_s;

        // The target isn't a text note: refused before any image is saved.
        QVERIFY(test::writeBytes(v.path(u"Inbox.md"_s), QByteArray("\xff\xfe\x00\x01", 4)));
        CaptureResult r = appendCapture(v.root(), s, u"x"_s, at, img);
        QVERIFY(!r.ok);
        QCOMPARE(attachmentCount(), 0);
        QCOMPARE(test::readBytes(v.path(u"Inbox.md"_s)), QByteArray("\xff\xfe\x00\x01", 4));

        // A hidden attachment folder is refused, and the note is left alone.
        s.captureNote = u"Project log.md"_s;
        const QByteArray log = test::readBytes(v.path(u"Project log.md"_s));
        s.attachmentFolderPath = u".hidden"_s;
        r = appendCapture(v.root(), s, u"x"_s, at, img);
        QVERIFY(!r.ok);
        QVERIFY(!QFileInfo::exists(v.path(u".hidden"_s)));
        QCOMPARE(test::readBytes(v.path(u"Project log.md"_s)), log);

        // The note can't be written: the images just saved are removed again.
        s.attachmentFolderPath = u"Attachments"_s;
        s.captureNote = u"Locked/Inbox.md"_s;
        QVERIFY(QDir().mkpath(v.path(u"Locked"_s)));
        QVERIFY(test::writeBytes(v.path(u"Locked/Inbox.md"_s), "- old\n"));
        QFile::setPermissions(v.path(u"Locked"_s), QFile::ReadOwner | QFile::ExeOwner);
        if (QFile probe(v.path(u"Locked/probe"_s)); probe.open(QIODevice::WriteOnly)) {
            QFile::setPermissions(v.path(u"Locked"_s),
                                  QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
            QSKIP("Running with permissions that ignore read-only folders.");
        }
        r = appendCapture(v.root(), s, u"x"_s, at, img);
        QFile::setPermissions(v.path(u"Locked"_s), QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
        QVERIFY(!r.ok);
        QCOMPARE(attachmentCount(), 0);
        QCOMPARE(test::readBytes(v.path(u"Locked/Inbox.md"_s)), QByteArray("- old\n"));
    }

    void dailyNotesSettingsImported()
    {
        test::VaultCopy v(u"health-vault"_s);
        QVERIFY(test::writeBytes(v.path(u".obsidian/daily-notes.json"_s),
                                 "{\"folder\": \"Journal/\", \"format\": \"YYYY-MM-DD dddd\", \"template\": "
                                 "\"templates/Daily\"}"));
        const VaultSettings s = importObsidianSettings(v.root());
        QCOMPARE(s.dailyNotesFolder, u"Journal"_s);
        QCOMPARE(s.dailyNoteFormat, u"YYYY-MM-DD dddd"_s);
        QCOMPARE(s.dailyNoteTemplate, u"templates/Daily.md"_s);
    }

    void linkSuggestions()
    {
        std::vector<VaultFile> files{{u"Cosine similarity.md"_s, {}, FileKind::Note},
                                     {u"ml/Vectors.md"_s, {}, FileKind::Note},
                                     {u"The.md"_s, {}, FileKind::Note},
                                     {u"2024.md"_s, {}, FileKind::Note},
                                     {u"a/Dup name.md"_s, {}, FileKind::Note},
                                     {u"b/Dup name.md"_s, {}, FileKind::Note},
                                     {u"img.png"_s, {}, FileKind::Image}};
        const QHash<QString, QStringList> aliases{{u"ml/Vectors.md"_s, {u"Vector space"_s}}};
        const NameTable t = buildNameTable(files, aliases);
        QVERIFY(t.pathByKey.contains(u"cosine similarity"_s));
        QVERIFY(!t.pathByKey.contains(u"the"_s));  // common word
        QVERIFY(!t.pathByKey.contains(u"2024"_s)); // number
        QCOMPARE(t.maxWords, 2);

        auto find = [&](const QString& line, const QString& self = {}) {
            QStringList out;
            for (const LinkSuggestion& s : findLinkSuggestions(line, t, self, {}))
                out.append(line.mid(s.start, s.length) + u"->"_s + s.path);
            return out;
        };
        QCOMPARE(find(u"We use Cosine  Similarity on vectors in a vector space."_s),
                 (QStringList{u"Cosine  Similarity->Cosine similarity.md"_s, u"vectors->ml/Vectors.md"_s}));
        QCOMPARE(find(u"Already [[Cosine similarity]] and `vectors` and #vectors"_s), QStringList());
        QCOMPARE(find(u"See https://x.com/vectors or [vectors](x.md)"_s), QStringList());
        QCOMPARE(find(u"Dup name is ambiguous"_s), QStringList());
        QCOMPARE(find(u"vectors here"_s, u"ml/Vectors.md"_s), QStringList()); // not the note itself
        QCOMPARE(findLinkSuggestions(u"vectors", t, {}, {u"vectors"_s}).size(), std::size_t(0)); // dismissed
        QCOMPARE(phraseKey(u"Café-Style  Notes!"), u"cafe style notes"_s);
    }

    void relatedNotes()
    {
        test::VaultCopy v(u"health-vault"_s);
        VaultService svc;
        QVERIFY(openIndexed(svc, v));
        const auto related = await<std::vector<RelatedNote>>(
            [&](auto cb) { svc.relatedNotes(u"Cosine similarity.md"_s, cb); });
        QStringList paths;
        for (const RelatedNote& r : related)
            paths.append(r.path);
        QVERIFY(paths.size() >= 2);
        QCOMPARE(paths[0], u"Cosine similarity copy.md"_s);
        QCOMPARE(paths[1], u"Vector math.md"_s);
        QVERIFY(!paths.contains(u"Cooking pasta.md"_s));
        QVERIFY(!paths.contains(u"Cosine similarity.md"_s));
        QVERIFY(!related[1].reason.isEmpty());
    }
};

QTEST_GUILESS_MAIN(TestFeatures)
#include "tst_features.moc"
