#include "IndexWorker.h"

#include "Similarity.h"
#include "core/parse/NoteMetadata.h"
#include "core/vault/Paths.h"
#include "core/vault/TextFile.h"

#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QFile>
#include <QFileSystemWatcher>
#include <QRegularExpression>
#include <QThreadPool>
#include <QTimer>

#include <algorithm>
#include <atomic>
#include <cmath>

#if defined(__GLIBC__)
#include <malloc.h>
#endif

using namespace Qt::StringLiterals;

namespace mn {

namespace {

constexpr char kBom[] = "\xEF\xBB\xBF";

// Indexing allocates a lot transiently (on several threads); give the freed
// memory back to the OS so idle RSS stays low.
void releaseFreedMemory()
{
#if defined(__GLIBC__)
    malloc_trim(0);
#endif
}

struct ParsedNote {
    QByteArray hash;
    NoteMetadata meta;
    QString body;
    std::vector<Chunk> chunks;
    qint64 size = 0;
    qint64 mtimeMs = 0;
    bool ok = false;
    bool unchanged = false;
};

QByteArray readAll(const QString& abs, bool* ok)
{
    QFile f(abs);
    if (!f.open(QIODevice::ReadOnly)) {
        *ok = false;
        return {};
    }
    *ok = true;
    return f.readAll();
}

ParsedNote parseFile(const QString& abs, const QByteArray& knownHash, const QStringList& tagProperties)
{
    ParsedNote p;
    bool ok = false;
    const QByteArray raw = readAll(abs, &ok);
    if (!ok) return p;
    p.ok = true;
    p.hash = QCryptographicHash::hash(raw, QCryptographicHash::Sha1);
    if (!knownHash.isEmpty() && p.hash == knownHash) {
        p.unchanged = true;
        return p;
    }
    const QByteArrayView body =
        raw.startsWith(QByteArrayView(kBom, 3)) ? QByteArrayView(raw).sliced(3) : QByteArrayView(raw);
    p.meta = parseNote(body);
    addPropertyTags(p.meta, tagProperties);
    p.body = QString::fromUtf8(body);
    p.chunks = chunkNote(p.body, p.meta.frontmatter.present ? p.meta.frontmatter.closingLine + 1 : 0);
    return p;
}

QString lineAt(const QString& text, int line)
{
    if (line < 0) return {};
    qsizetype start = 0;
    for (int i = 0; i < line; ++i) {
        start = text.indexOf(u'\n', start);
        if (start < 0) return {};
        ++start;
    }
    qsizetype end = text.indexOf(u'\n', start);
    if (end < 0) end = text.size();
    QString l = text.mid(start, end - start);
    if (l.endsWith(u'\r')) l.chop(1);
    return l.trimmed();
}

} // namespace

IndexWorker::IndexWorker(QObject* parent) : QObject(parent) {}

IndexWorker::~IndexWorker() = default;

void IndexWorker::openVault(const QString& root, const QString& dbPath, const QStringList& tagProperties)
{
    root_ = root;
    tagProperties_ = tagProperties;
    QString err;
    if (!store_.open(dbPath, &err)) {
        Q_EMIT errorOccurred(
            u"Couldn't open the index cache (%1). Search and backlinks are unavailable."_s.arg(err));
        return;
    }
    // Publish the cached state immediately so the UI can show the vault fast.
    stored_.clear();
    auto index = std::make_shared<FileIndex>();
    QSet<QString> folderSet;
    for (StoredFile& f : store_.loadFiles()) {
        index->add(f.path, f.kind);
        QString folder = paths::folderOf(f.path);
        while (!folder.isEmpty() && !folderSet.contains(folder)) {
            folderSet.insert(folder);
            folder = paths::folderOf(folder);
        }
        const QString path = f.path;
        stored_.emplace(path, std::move(f));
    }
    index->rebuild();
    files_ = std::move(index);
    folders_ = QStringList(folderSet.begin(), folderSet.end());
    folders_.sort();
    if (!stored_.isEmpty()) emitSnapshot();
    // The index was built with other tag properties: parse every note again.
    if (store_.meta(u"tagProperties"_s) != tagProperties_.join(u'\n')) forceReparse();

    watcher_ = new QFileSystemWatcher(this);
    connect(watcher_, &QFileSystemWatcher::directoryChanged, this, &IndexWorker::onDirectoryChanged);
    debounce_ = new QTimer(this);
    debounce_->setSingleShot(true);
    debounce_->setInterval(150);
    connect(debounce_, &QTimer::timeout, this, &IndexWorker::processPendingFolders);

    fullRescan();
}

void IndexWorker::forceReparse()
{
    for (StoredFile& f : stored_) {
        if (f.kind != FileKind::Note) continue;
        f.size = -1; // seen as changed by the next scan
        f.hash.clear();
    }
    store_.setMeta(u"tagProperties"_s, tagProperties_.join(u'\n'));
}

void IndexWorker::setTagProperties(const QStringList& keys)
{
    if (keys == tagProperties_) return;
    tagProperties_ = keys;
    forceReparse();
    fullRescan();
}

void IndexWorker::fullRescan()
{
    QElapsedTimer t;
    t.start();
    const ScanResult scan = scanVault(root_);
    if (qEnvironmentVariableIsSet("MN_INDEX_PROFILE"))
        qInfo("index: scan %lld ms (%zu files)", static_cast<long long>(t.elapsed()), scan.files.size());
    const int before = generation_;
    applyScan(scan, QString(), true);
    if (generation_ == before) emitSnapshot(); // first publish even if nothing changed
    syncWatches();
    IndexStats stats;
    for (const auto& f : files_->files()) {
        if (f.kind == FileKind::Note)
            ++stats.notes;
        else
            ++stats.attachments;
    }
    stats.elapsedMs = t.elapsed();
    releaseFreedMemory();
    Q_EMIT indexingFinished(stats);
}

void IndexWorker::onDirectoryChanged(const QString& absPath)
{
    pendingFolders_.insert(paths::toVaultPath(root_, absPath));
    debounce_->start();
}

void IndexWorker::processPendingFolders()
{
    const QSet<QString> pending = std::exchange(pendingFolders_, {});
    for (const QString& folder : pending) {
        const QString abs = paths::toAbsolute(root_, folder);
        if (!QFile::exists(abs)) {
            // The folder itself is gone: its parent will report the removal too.
            applyScan(ScanResult{}, folder, true);
            continue;
        }
        applyScan(scanFolder(root_, folder), folder, false);
    }
    syncWatches();
}

void IndexWorker::refreshPaths(const QStringList& vaultPaths)
{
    QSet<QString> folders;
    for (const QString& p : vaultPaths)
        folders.insert(paths::folderOf(p));
    for (const QString& folder : std::as_const(folders)) {
        if (QFile::exists(paths::toAbsolute(root_, folder)))
            applyScan(scanFolder(root_, folder), folder, false);
    }
    syncWatches();
}

void IndexWorker::applyScan(const ScanResult& scanIn, const QString& scope, bool recursive)
{
    ScanResult scan = scanIn;
    auto inScope = [&](const QString& path) {
        if (recursive) return paths::isInside(path, scope) && path != scope;
        return paths::folderOf(path) == scope;
    };

    // Folders: in a non-recursive scan, new subfolders are scanned in full and
    // vanished subfolders drop everything below them.
    QStringList removedFolders;
    QSet<QString> scannedFolders(scan.folders.begin(), scan.folders.end());
    for (const QString& f : std::as_const(folders_)) {
        if (inScope(f) && !scannedFolders.contains(f)) removedFolders.append(f);
    }
    if (!recursive) {
        const QSet<QString> known(folders_.begin(), folders_.end());
        for (const QString& f : std::as_const(scan.folders)) {
            if (!known.contains(f)) {
                ScanResult sub = scanTree(root_, f);
                scan.files.insert(scan.files.end(), sub.files.begin(), sub.files.end());
                for (const QString& sf : std::as_const(sub.folders))
                    scannedFolders.insert(sf);
            }
        }
    }

    std::vector<Change> changes;
    QSet<QString> present;
    for (const FileEntry& e : scan.files) {
        present.insert(e.path);
        const auto it = stored_.constFind(e.path);
        if (it == stored_.constEnd()) {
            changes.push_back({e, true});
        } else if (it->size != e.size || it->mtimeMs != e.mtimeMs) {
            changes.push_back({e, false});
        }
    }
    QStringList removed;
    for (auto it = stored_.cbegin(); it != stored_.cend(); ++it) {
        const QString& p = it.key();
        if (present.contains(p)) continue;
        bool gone = inScope(p);
        for (const QString& rf : std::as_const(removedFolders))
            gone = gone || paths::isInside(p, rf);
        if (!recursive && !gone) continue;
        if (gone) removed.append(p);
    }

    // Update the folder list.
    QSet<QString> folderSet(folders_.begin(), folders_.end());
    for (const QString& rf : std::as_const(removedFolders)) {
        for (auto it = folderSet.begin(); it != folderSet.end();) {
            if (paths::isInside(*it, rf))
                it = folderSet.erase(it);
            else
                ++it;
        }
    }
    bool foldersChanged = !removedFolders.isEmpty();
    for (const QString& f : std::as_const(scannedFolders)) {
        if (!folderSet.contains(f)) {
            folderSet.insert(f);
            foldersChanged = true;
        }
    }

    if (changes.empty() && removed.isEmpty() && !foldersChanged) return;

    QStringList modified;
    const bool bulk = changes.size() + static_cast<std::size_t>(removed.size()) > 200;
    if (bulk) store_.beginBulk();
    {
        sql::Transaction tx(store_.db());
        for (const QString& p : std::as_const(removed)) {
            store_.removeFile(p);
            stored_.remove(p);
            modified.append(p);
        }
        indexChanges(changes, modified);
        tx.commit();
    }
    if (bulk) store_.endBulk();

    if (foldersChanged) {
        folders_ = QStringList(folderSet.begin(), folderSet.end());
        folders_.sort();
    }
    const bool fileSetChanged = !removed.isEmpty() || std::any_of(changes.begin(), changes.end(),
                                                                  [](const Change& c) { return c.isNew; });
    if (fileSetChanged) {
        // Publish a new table; the old one may still be in use by the UI.
        auto index = std::make_shared<FileIndex>();
        for (auto it = stored_.cbegin(); it != stored_.cend(); ++it)
            index->add(it.key(), it->kind);
        index->rebuild();
        files_ = std::move(index);
    }
    ++generation_;
    emitSnapshot();
    if (!modified.isEmpty()) Q_EMIT filesChangedOnDisk(modified);
}

void IndexWorker::indexChanges(const std::vector<Change>& changes, QStringList& modified)
{
    std::vector<const Change*> notes;
    for (const Change& c : changes) {
        if (!c.isNew) modified.append(c.entry.path);
        if (c.entry.kind == FileKind::Note) {
            notes.push_back(&c);
        } else {
            store_.putAttachment(c.entry.path, c.entry.kind, c.entry.size, c.entry.mtimeMs, !c.isNew);
            stored_.insert(c.entry.path, {0, c.entry.path, c.entry.kind, c.entry.size, c.entry.mtimeMs, {}});
        }
    }
    if (notes.empty()) return;

    // Read, hash and parse in parallel; write to SQLite on this thread.
    std::vector<ParsedNote> parsed(notes.size());
    auto work = [&](std::size_t i) {
        const Change& c = *notes[i];
        const auto it = stored_.constFind(c.entry.path);
        const QByteArray known = it == stored_.constEnd() ? QByteArray() : it->hash;
        parsed[i] = parseFile(paths::toAbsolute(root_, c.entry.path), known, tagProperties_);
    };
    const bool profile = qEnvironmentVariableIsSet("MN_INDEX_PROFILE");
    QElapsedTimer phase;
    phase.start();
    if (notes.size() < 16) {
        for (std::size_t i = 0; i < notes.size(); ++i)
            work(i);
    } else {
        QThreadPool pool;
        const int threads = std::max(1, QThread::idealThreadCount());
        pool.setMaxThreadCount(threads);
        std::atomic<std::size_t> next{0};
        for (int t = 0; t < threads; ++t) {
            pool.start([&]() {
                for (std::size_t i = next++; i < notes.size(); i = next++)
                    work(i);
            });
        }
        pool.waitForDone();
    }
    if (profile)
        qInfo("index: read+hash+parse %zu notes %lld ms", notes.size(),
              static_cast<long long>(phase.restart()));

    for (std::size_t i = 0; i < notes.size(); ++i) {
        const FileEntry& e = notes[i]->entry;
        ParsedNote& p = parsed[i];
        if (!p.ok) continue; // unreadable right now; the next scan retries
        if (p.unchanged) {
            store_.updateStamp(e.path, e.size, e.mtimeMs);
            StoredFile& sf = stored_[e.path];
            sf.size = e.size;
            sf.mtimeMs = e.mtimeMs;
            continue;
        }
        store_.putNote(e.path, e.size, e.mtimeMs, p.hash, p.meta, p.body, p.chunks, !notes[i]->isNew);
        stored_.insert(e.path, {0, e.path, FileKind::Note, e.size, e.mtimeMs, p.hash});
    }
    if (profile) qInfo("index: database writes %lld ms", static_cast<long long>(phase.restart()));
}

void IndexWorker::emitSnapshot()
{
    VaultSnapshot snap;
    snap.generation = generation_;
    snap.index = files_;
    snap.aliases = store_.allAliases();
    snap.folders = folders_;
    snap.tags = store_.tagCounts();
    Q_EMIT snapshotReady(snap);
}

void IndexWorker::syncWatches()
{
    if (watcher_ == nullptr) return;
    QStringList wanted;
    wanted.reserve(folders_.size() + 1);
    wanted.append(root_);
    for (const QString& f : std::as_const(folders_))
        wanted.append(paths::toAbsolute(root_, f));
    const QStringList current = watcher_->directories();
    const QSet<QString> want(wanted.begin(), wanted.end());
    const QSet<QString> have(current.begin(), current.end());
    QStringList toRemove;
    for (const QString& d : current) {
        if (!want.contains(d)) toRemove.append(d);
    }
    QStringList toAdd;
    for (const QString& d : std::as_const(wanted)) {
        if (!have.contains(d)) toAdd.append(d);
    }
    if (!toRemove.isEmpty()) watcher_->removePaths(toRemove);
    if (!toAdd.isEmpty()) watcher_->addPaths(toAdd);
}

std::vector<Backlink> IndexWorker::backlinks(const QString& path)
{
    std::vector<Backlink> out;
    QHash<QString, QString> bodies;
    for (const StoredLink& l : store_.linksWithNameKeys({linkNameKey(path)})) {
        const Resolution r = files_->resolve(l.linkpath, l.sourcePath);
        if (!r.resolved() || files_->file(r.file).path != path) continue;
        auto it = bodies.find(l.sourcePath);
        if (it == bodies.end()) it = bodies.insert(l.sourcePath, store_.body(l.sourcePath));
        out.push_back({l.sourcePath, l.line, lineAt(*it, l.line), l.embed});
    }
    std::sort(out.begin(), out.end(), [](const Backlink& a, const Backlink& b) {
        if (a.sourcePath != b.sourcePath) return a.sourcePath.compare(b.sourcePath, Qt::CaseInsensitive) < 0;
        return a.line < b.line;
    });
    return out;
}

std::vector<Mention> IndexWorker::unlinkedMentions(const QString& path, const QStringList& aliases)
{
    std::vector<Mention> out;
    QStringList names{paths::linkName(path)};
    names += aliases;
    static const QRegularExpression linkSyntax(uR"((!?\[\[[^\]\n]*\]\])|(!?\[[^\]\n]*\]\([^)\n]*\)))"_s);
    QSet<QString> seen;
    for (const QString& name : std::as_const(names)) {
        if (name.trimmed().size() < 2) continue;
        const QRegularExpression word(
            u"(?<![\\p{L}\\p{N}_])"_s + QRegularExpression::escape(name) + u"(?![\\p{L}\\p{N}_])"_s,
            QRegularExpression::CaseInsensitiveOption | QRegularExpression::UseUnicodePropertiesOption);
        for (const QString& src : store_.ftsCandidates(name, 1000)) {
            if (src == path) continue;
            const QString body = store_.body(src);
            const QStringList lines = body.split(u'\n');
            for (qsizetype ln = 0; ln < lines.size(); ++ln) {
                const QString& line = lines[ln];
                auto m = word.globalMatch(line);
                bool hit = false;
                while (m.hasNext() && !hit) {
                    const auto match = m.next();
                    bool inLink = false;
                    auto lm = linkSyntax.globalMatch(line);
                    while (lm.hasNext()) {
                        const auto l = lm.next();
                        if (match.capturedStart() >= l.capturedStart() &&
                            match.capturedEnd() <= l.capturedEnd()) {
                            inLink = true;
                            break;
                        }
                    }
                    hit = !inLink;
                }
                if (!hit) continue;
                const QString key = src + u'\n' + QString::number(ln);
                if (seen.contains(key)) continue;
                seen.insert(key);
                QString ctx = line.trimmed();
                if (ctx.endsWith(u'\r')) ctx.chop(1);
                out.push_back({src, static_cast<int>(ln), ctx});
            }
        }
    }
    std::sort(out.begin(), out.end(), [](const Mention& a, const Mention& b) {
        if (a.sourcePath != b.sourcePath) return a.sourcePath.compare(b.sourcePath, Qt::CaseInsensitive) < 0;
        return a.line < b.line;
    });
    return out;
}

std::vector<SearchHit> IndexWorker::search(const QString& query, int limit)
{
    return store_.search(query, limit);
}

std::vector<UnresolvedLink> IndexWorker::unresolvedLinks()
{
    std::vector<UnresolvedLink> out;
    for (const StoredLink& l : store_.allLinks()) {
        if (!files_->resolve(l.linkpath, l.sourcePath).resolved())
            out.push_back({l.sourcePath, l.linkpath, l.line});
    }
    return out;
}

void IndexWorker::ensureStats()
{
    if (statsGeneration_ == generation_) return;
    statsGeneration_ = generation_;
    docFreq_ = store_.allDocumentFrequencies();
    noteCount_ = std::max(1, store_.noteCount());
    tagNotes_.clear();
    for (const TagCount& t : store_.tagCounts())
        tagNotes_.insert(t.tag.toCaseFolded(), t.notes);
}

QHash<QString, double> IndexWorker::termWeights(const QString& text) const
{
    const int n = noteCount_;
    QHash<QString, double> w;
    const QHash<QString, int> tf = sim::termFrequencies(sim::contentText(text));
    for (auto it = tf.cbegin(); it != tf.cend(); ++it) {
        const int df = docFreq_.value(it.key());
        // Terms in more than a third of the notes say little about a topic.
        if (df <= 0 || df > n / 3 + 1) continue;
        w.insert(it.key(), (1.0 + std::log(it.value())) * std::log((1.0 + n) / df));
    }
    return w;
}

namespace {
// "jobs" and "job" count as one word when comparing passages (FTS terms aren't stemmed).
QHash<QString, double> foldPlurals(const QHash<QString, double>& w)
{
    QHash<QString, double> out;
    out.reserve(w.size());
    for (auto it = w.cbegin(); it != w.cend(); ++it) {
        const QString& t = it.key();
        const bool plural = t.size() > 3 && t.endsWith(u's') && !t.endsWith(u"ss"_s);
        out[plural ? t.chopped(1) : t] += it.value();
    }
    return out;
}

double weightNorm(const QHash<QString, double>& w)
{
    double s = 0;
    for (double v : w)
        s += v * v;
    return std::sqrt(s);
}
} // namespace

std::vector<Passage> IndexWorker::relatedPassages(const QString& noteText, int line, const QString& path,
                                                  int limit)
{
    std::vector<Passage> out;
    const Frontmatter fm = parseFrontmatter(noteText.toUtf8());
    const std::vector<Chunk> chunks = chunkNote(noteText, fm.present ? fm.closingLine + 1 : 0);
    const Chunk* here = chunkAtLine(chunks, line);
    if (here == nullptr) return out;
    ensureStats();
    const QHash<QString, double> weights = termWeights(here->context + u'\n' + here->text);
    const QHash<QString, double> mine = foldPlurals(weights);
    const double myNorm = weightNorm(mine);
    if (mine.size() < 3 || myNorm <= 0) return out;

    // Candidates: passages sharing this passage's most distinctive words.
    std::vector<std::pair<double, QString>> top;
    for (auto it = weights.cbegin(); it != weights.cend(); ++it) {
        if (docFreq_.value(it.key()) >= 2) top.emplace_back(it.value(), it.key());
    }
    std::sort(top.begin(), top.end(), std::greater<>());
    QStringList terms;
    for (std::size_t i = 0; i < top.size() && i < 12; ++i)
        terms.append(top[i].second);
    std::vector<Passage> candidates = store_.passagesMatchingAny(terms, path, 60);

    for (Passage& p : candidates) {
        const QHash<QString, double> theirs = foldPlurals(termWeights(p.context + u'\n' + p.text));
        const double theirNorm = weightNorm(theirs);
        if (theirNorm <= 0) continue;
        double dot = 0;
        std::vector<std::pair<double, QString>> shared;
        for (auto it = mine.cbegin(); it != mine.cend(); ++it) {
            const auto t = theirs.constFind(it.key());
            if (t == theirs.constEnd()) continue;
            dot += it.value() * *t;
            shared.emplace_back(it.value() * *t, it.key());
        }
        p.score = dot / (myNorm * theirNorm);
        // One shared word is a coincidence more often than a topic.
        if (p.score < 0.08 || shared.size() < 2) continue;
        std::sort(shared.begin(), shared.end(), std::greater<>());
        QStringList words;
        for (std::size_t i = 0; i < shared.size() && i < 3; ++i)
            words.append(shared[i].second);
        p.reason = words.join(u", "_s);
        out.push_back(std::move(p));
    }
    std::stable_sort(out.begin(), out.end(),
                     [](const Passage& a, const Passage& b) { return a.score > b.score; });
    // At most two passages per note, so one long note doesn't fill the list.
    QHash<QString, int> perNote;
    std::vector<Passage> picked;
    for (Passage& p : out) {
        if (static_cast<int>(picked.size()) >= limit) break;
        if (++perNote[p.path] > 2) continue;
        picked.push_back(std::move(p));
    }
    return picked;
}

std::vector<RelatedNote> IndexWorker::relatedNotes(const QString& path, int limit)
{
    std::vector<RelatedNote> out;
    const QString body = store_.body(path);
    if (body.isEmpty()) return out;
    ensureStats();
    const int n = noteCount_;
    auto weights = [this](const QString& text) { return termWeights(text); };
    auto norm = weightNorm;

    const QHash<QString, double> mine = weights(body);
    const double myNorm = norm(mine);
    if (myNorm <= 0) return out;

    // Candidates: notes sharing this note's most distinctive terms (FTS5), plus link neighbours.
    std::vector<std::pair<double, QString>> top;
    for (auto it = mine.cbegin(); it != mine.cend(); ++it) {
        if (docFreq_.value(it.key()) >= 2) top.emplace_back(it.value(), it.key());
    }
    std::sort(top.begin(), top.end(), std::greater<>());
    QStringList terms;
    for (std::size_t i = 0; i < top.size() && i < 20; ++i)
        terms.append(top[i].second);
    QStringList candidates = store_.notesMatchingAny(terms, 30);

    QSet<QString> linkedTo;
    QSet<QString> linkedFrom;
    {
        QByteArray raw = body.toUtf8();
        for (const LinkRef& l : parseNote(raw).links) {
            const Resolution r = files_->resolve(l.linkpath, path);
            if (r.resolved()) linkedTo.insert(files_->file(r.file).path);
        }
        for (const Backlink& b : backlinks(path))
            linkedFrom.insert(b.sourcePath);
    }
    for (const QString& p : std::as_const(linkedTo))
        candidates.append(p);
    for (const QString& p : std::as_const(linkedFrom))
        candidates.append(p);
    candidates.removeDuplicates();
    candidates.removeAll(path);

    const QStringList myTags = store_.tagKeysOf(path);
    for (const QString& cand : std::as_const(candidates)) {
        if (classifyPath(cand) != FileKind::Note) continue;
        const QHash<QString, double> theirs = weights(store_.body(cand));
        const double theirNorm = norm(theirs);
        double dot = 0;
        std::vector<std::pair<double, QString>> shared;
        for (auto it = mine.cbegin(); it != mine.cend(); ++it) {
            const auto t = theirs.constFind(it.key());
            if (t == theirs.constEnd()) continue;
            dot += it.value() * *t;
            shared.emplace_back(it.value() * *t, it.key());
        }
        const double cosine = theirNorm > 0 ? dot / (myNorm * theirNorm) : 0;
        double score = cosine;
        QStringList why;
        if (linkedTo.contains(cand) || linkedFrom.contains(cand)) {
            score += 0.1;
            why.append(linkedFrom.contains(cand) ? u"links here"_s : u"linked from here"_s);
        }
        const QStringList theirTags = store_.tagKeysOf(cand);
        int sharedTags = 0;
        for (const QString& t : myTags) {
            // Tags on a quarter of the vault or more (e.g. #tech) don't make notes related.
            if (tagNotes_.value(t) * 4 >= n) continue;
            if (theirTags.contains(t) && sharedTags < 3) {
                ++sharedTags;
                why.append(u'#' + t);
            }
        }
        score += 0.03 * sharedTags;
        std::sort(shared.begin(), shared.end(), std::greater<>());
        QStringList words;
        for (std::size_t i = 0; i < shared.size() && i < 3; ++i)
            words.append(shared[i].second);
        if (!words.isEmpty()) why.append(words.join(u", "_s));
        if (score >= 0.05) out.push_back({cand, score, why.join(u" · "_s)});
    }
    std::sort(out.begin(), out.end(),
              [](const RelatedNote& a, const RelatedNote& b) { return a.score > b.score; });
    if (static_cast<int>(out.size()) > limit) out.resize(static_cast<std::size_t>(limit));
    return out;
}

HealthReport IndexWorker::healthReport(const QString& skipFolder, const QStringList& categories,
                                       int minCategories)
{
    QElapsedTimer timer;
    timer.start();
    HealthReport r;
    // Links: what resolves where.
    QSet<QString> linkedNotes; // notes linked from some other note
    QSet<QString> usedFiles;   // any file linked or embedded
    for (const StoredLink& l : store_.allLinks()) {
        const Resolution res = files_->resolve(l.linkpath, l.sourcePath);
        if (!res.resolved()) {
            r.brokenLinks.push_back({l.sourcePath, l.linkpath, l.line});
            continue;
        }
        const QString& target = files_->file(res.file).path;
        usedFiles.insert(target);
        if (target != l.sourcePath) linkedNotes.insert(target);
    }
    // Bodies: empty notes and near-duplicates.
    std::vector<sim::DocShingles> docs;
    store_.forEachBody([&](const QString& path, const QString& body) {
        const QString content = sim::contentText(body);
        if (content.trimmed().isEmpty()) r.emptyNotes.append(path);
        docs.push_back({path, sim::shingles(content)});
    });
    for (const sim::DuplicatePair& d : sim::nearDuplicates(docs, 0.5))
        r.duplicateNotes.push_back({d.a, d.b, d.similarity});
    docs.clear();
    for (const VaultFile& f : files_->files()) {
        if (f.kind == FileKind::Note) {
            if (!linkedNotes.contains(f.path) &&
                !(!skipFolder.isEmpty() && paths::isInside(f.path, skipFolder)))
                r.orphanNotes.append(f.path);
        } else if (isAttachmentKind(f.kind) && !usedFiles.contains(f.path)) {
            r.unusedAttachments.append(f.path);
        }
    }
    r.emptyNotes.sort();
    // Tags that are probably the same: equal after ignoring case, '-', '_' and a plural 's'.
    QHash<QString, TagGroup> groups;
    for (const TagCount& t : store_.tagCounts()) {
        QString key;
        for (QChar c : t.tag.toCaseFolded()) {
            if (c != u'-' && c != u'_' && c != u' ') key += c;
        }
        if (key.size() > 3 && key.endsWith(u's')) key.chop(1);
        TagGroup& g = groups[key];
        g.tags.append(t.tag);
        g.notes.append(t.notes);
    }
    for (const TagGroup& g : std::as_const(groups)) {
        if (g.tags.size() > 1) r.similarTags.push_back(g);
    }
    std::sort(r.similarTags.begin(), r.similarTags.end(), [](const TagGroup& a, const TagGroup& b) {
        return a.tags.first().compare(b.tags.first(), Qt::CaseInsensitive) < 0;
    });
    // The vault's category convention: every note has at least N category tags.
    if (!categories.isEmpty() && minCategories > 0) {
        r.minCategories = minCategories;
        QSet<QString> cats;
        for (const QString& c : categories)
            cats.insert(c.toCaseFolded());
        for (const NoteTags& n : store_.noteTags()) {
            if (!skipFolder.isEmpty() && paths::isInside(n.path, skipFolder)) continue;
            QStringList has;
            for (const QString& k : n.keys) {
                if (cats.contains(k)) has.append(k);
            }
            if (has.size() < minCategories) r.fewCategories.push_back({n.path, has});
        }
    }
    releaseFreedMemory();
    r.elapsedMs = timer.elapsed();
    return r;
}

TagScheme IndexWorker::detectTagScheme()
{
    // A frontmatter property holds a category if most notes set it to one of a few
    // short values (e.g. "primary: tech").
    struct Stat {
        int notes = 0;
        QHash<QString, int> values;
    };
    QHash<QString, Stat> stats;
    int notes = 0;
    store_.forEachBody([&](const QString&, const QString& body) {
        ++notes;
        const Frontmatter fm = parseFrontmatter(body.toUtf8());
        if (!fm.present || !fm.valid) return;
        for (const FrontmatterField& f : fm.fields) {
            if (f.isList || f.complex || f.key == u"tags" || f.key == u"tag" || f.key == u"aliases") continue;
            const QString v = f.value.trimmed();
            if (v.isEmpty() || v.contains(u' ') || v.size() > 40) continue;
            Stat& st = stats[f.key];
            ++st.notes;
            ++st.values[v.toCaseFolded()];
        }
    });
    TagScheme scheme;
    QHash<QString, int> categoryCounts;
    std::vector<std::pair<int, QString>> keys;
    for (auto it = stats.cbegin(); it != stats.cend(); ++it) {
        // Most notes set it; a few values, each shared by several notes (summaries aren't).
        if (it->notes * 2 < notes || it->values.size() < 2 || it->values.size() > 15 ||
            it->values.size() * 2 > it->notes)
            continue;
        keys.emplace_back(it->notes, it.key());
        for (auto v = it->values.cbegin(); v != it->values.cend(); ++v) {
            if (v.value() >= 2) categoryCounts[v.key()] += v.value();
        }
    }
    std::sort(keys.begin(), keys.end(), [](const auto& a, const auto& b) {
        return a.first != b.first ? a.first > b.first : a.second < b.second;
    });
    for (const auto& k : keys)
        scheme.properties.append(k.second);
    std::vector<std::pair<int, QString>> cats;
    for (auto it = categoryCounts.cbegin(); it != categoryCounts.cend(); ++it)
        cats.emplace_back(it.value(), it.key());
    std::sort(cats.begin(), cats.end(), [](const auto& a, const auto& b) {
        return a.first != b.first ? a.first > b.first : a.second < b.second;
    });
    for (const auto& c : cats)
        scheme.categories.append(c.second);
    return scheme;
}

QStringList IndexWorker::renameCandidates(const std::vector<Move>& moves)
{
    QStringList keys;
    QStringList out;
    for (const Move& m : moves) {
        keys.append(linkNameKey(m.from));
        keys.append(linkNameKey(m.to));
        if (classifyPath(m.from) == FileKind::Note) out.append(m.from);
    }
    keys.removeDuplicates();
    out += store_.notesLinkingToNames(keys);
    out.removeDuplicates();
    return out;
}

} // namespace mn
