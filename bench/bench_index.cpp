// Indexing, search and backlink benchmarks (core only, no GUI).
#include "BenchUtil.h"
#include "core/index/VaultService.h"
#include "core/parse/NoteMetadata.h"
#include "core/vault/VaultScanner.h"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QRandomGenerator>
#include <QTemporaryDir>
#include <QTimer>

using namespace mn;
using namespace Qt::StringLiterals;

namespace {

template <typename R, typename Call> R await(Call call, double* ms = nullptr)
{
    R result{};
    QEventLoop loop;
    QElapsedTimer t;
    t.start();
    call([&](R r) {
        result = std::move(r);
        loop.quit();
    });
    loop.exec();
    if (ms) *ms = static_cast<double>(t.nsecsElapsed()) / 1e6;
    return result;
}

bool waitSignal(VaultService& svc, double* ms)
{
    QEventLoop loop;
    QElapsedTimer t;
    t.start();
    bool ok = false;
    auto c = QObject::connect(&svc, &VaultService::indexingFinished, &loop, [&] {
        ok = true;
        loop.quit();
    });
    QTimer::singleShot(120000, &loop, &QEventLoop::quit);
    loop.exec();
    QObject::disconnect(c);
    *ms = static_cast<double>(t.nsecsElapsed()) / 1e6;
    return ok;
}

} // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    QCommandLineParser cli;
    cli.addHelpOption();
    cli.addOption({u"verbose"_s, u"Print health-report details."_s});
    cli.addOption(
        {u"vault"_s, u"Vault to benchmark (it is only read, apart from .markdown-notes/)."_s, u"path"_s});
    cli.process(app);
    const QString vault = cli.value(u"vault"_s);
    if (vault.isEmpty()) {
        std::fprintf(stderr, "usage: bench_index --vault <path>\n");
        return 2;
    }
    QTemporaryDir cache;
    const QString db = cache.path() + u"/index.sqlite"_s;

    // Raw parse throughput.
    {
        const ScanResult scan = scanVault(vault);
        qint64 bytes = 0;
        int notes = 0;
        QElapsedTimer t;
        t.start();
        for (const FileEntry& e : scan.files) {
            if (e.kind != FileKind::Note) continue;
            QFile f(vault + u'/' + e.path);
            if (!f.open(QIODevice::ReadOnly)) continue;
            const QByteArray raw = f.readAll();
            bytes += raw.size();
            ++notes;
            const NoteMetadata m = parseNote(raw);
            Q_UNUSED(m);
        }
        const double ms = static_cast<double>(t.nsecsElapsed()) / 1e6;
        std::printf("vault: %d notes, %zu files, %.1f MB of Markdown\n", notes, scan.files.size(),
                    static_cast<double>(bytes) / 1e6);
        bench::report("read+parse all notes (1 thread)", ms, "ms");
    }

    double coldMs = 0;
    {
        VaultService svc;
        QString err;
        svc.open(vault, &err, db);
        if (!waitSignal(svc, &coldMs)) return 1;
        bench::report("first-ever index (background)", coldMs, "ms", 2000, 5000);
    }
    {
        VaultService svc;
        QString err;
        QElapsedTimer t;
        t.start();
        double snapMs = -1;
        QObject::connect(&svc, &VaultService::snapshotChanged, &svc, [&] {
            if (snapMs < 0) snapMs = static_cast<double>(t.nsecsElapsed()) / 1e6;
        });
        svc.open(vault, &err, db);
        double warmMs = 0;
        waitSignal(svc, &warmMs);
        bench::report("cached file list available", snapMs, "ms");
        bench::report("startup rescan (cache present)", warmMs, "ms");

        // Full-text search.
        const QStringList queries{u"alpha"_s,
                                  u"river mountain"_s,
                                  u"tens"_s,
                                  u"coffee garden"_s,
                                  u"query"_s,
                                  u"signal"_s,
                                  u"ocean desert"_s,
                                  u"matrix"_s,
                                  u"summary"_s,
                                  u"research"_s,
                                  u"a"_s,
                                  u"the"_s,
                                  u"proce"_s,
                                  u"\"vector matrix\""_s,
                                  u"kernel"_s};
        std::vector<double> times;
        for (int rep = 0; rep < 3; ++rep) {
            for (const QString& q : queries) {
                double ms = 0;
                await<std::vector<SearchHit>>([&](auto cb) { svc.search(q, cb); }, &ms);
                times.push_back(ms);
            }
        }
        bench::report("full-text search (median)", bench::median(times), "ms", 30, 100);
        bench::report("full-text search (p95)", bench::percentile(times, 0.95), "ms", 30, 100);

        // Backlinks.
        std::vector<double> bl;
        const auto& files = svc.snapshot().files();
        QRandomGenerator rng(7);
        for (int i = 0; i < 30 && !files.empty(); ++i) {
            const VaultFile& f = files[rng.bounded(static_cast<quint32>(files.size()))];
            if (f.kind != FileKind::Note) continue;
            double ms = 0;
            await<std::vector<Backlink>>([&](auto cb) { svc.backlinks(f.path, cb); }, &ms);
            bl.push_back(ms);
        }
        bench::report("backlinks query (median)", bench::median(bl), "ms");

        // Related notes (heuristic TF-IDF + links) for a sample of notes.
        std::vector<double> rel;
        for (int i = 0; i < 20 && !files.empty(); ++i) {
            const VaultFile& f = files[rng.bounded(static_cast<quint32>(files.size()))];
            if (f.kind != FileKind::Note) continue;
            double ms = 0;
            await<std::vector<RelatedNote>>([&](auto cb) { svc.relatedNotes(f.path, cb); }, &ms);
            rel.push_back(ms);
        }
        bench::report("related notes (median)", bench::median(rel), "ms");
        bench::report("related notes (max)", rel.empty() ? 0 : *std::max_element(rel.begin(), rel.end()),
                      "ms");

        double healthMs = 0;
        const HealthReport health = await<HealthReport>([&](auto cb) { svc.healthReport(cb); }, &healthMs);
        bench::report("vault health report", healthMs, "ms");
        std::printf("  broken %zu, orphans %lld, empty %lld, unused attachments %lld, tag groups %zu, "
                    "near-duplicates %zu\n",
                    health.brokenLinks.size(), static_cast<long long>(health.orphanNotes.size()),
                    static_cast<long long>(health.emptyNotes.size()),
                    static_cast<long long>(health.unusedAttachments.size()), health.similarTags.size(),
                    health.duplicateNotes.size());
        if (cli.isSet(u"verbose"_s)) {
            for (const DuplicateNotes& d : health.duplicateNotes)
                std::printf("  duplicate %3d%%  %s  <->  %s\n", qRound(d.similarity * 100), qPrintable(d.a),
                            qPrintable(d.b));
            for (const TagGroup& g : health.similarTags)
                std::printf("  tags  %s\n", qPrintable(g.tags.join(u", "_s)));
            for (const QString& e : health.emptyNotes)
                std::printf("  empty  %s\n", qPrintable(e));
            const auto rel2 =
                await<std::vector<RelatedNote>>([&](auto cb) { svc.relatedNotes(files.front().path, cb); });
            std::printf("  related to %s:\n", qPrintable(files.front().path));
            for (const RelatedNote& r : rel2)
                std::printf("    %.2f  %s   (%s)\n", r.score, qPrintable(r.path), qPrintable(r.reason));
        }
        double tasksMs = 0;
        const auto tasks = await<std::vector<TaskInfo>>([&](auto cb) { svc.tasks(cb); }, &tasksMs);
        bench::report("all tasks query", tasksMs, "ms");
        std::printf("  %zu tasks\n", tasks.size());

        double unresolvedMs = 0;
        const auto unresolved =
            await<std::vector<UnresolvedLink>>([&](auto cb) { svc.unresolvedLinks(cb); }, &unresolvedMs);
        bench::report("unresolved links (count)", static_cast<double>(unresolved.size()), "");
        bench::report("unresolved links scan", unresolvedMs, "ms");
    }
    return 0;
}
