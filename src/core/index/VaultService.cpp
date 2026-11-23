#include "VaultService.h"

#include "IndexWorker.h"
#include "core/vault/Paths.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSemaphore>

using namespace Qt::StringLiterals;

namespace mn {

VaultService::VaultService(QObject* parent) : QObject(parent)
{
    qRegisterMetaType<mn::VaultSnapshot>();
    qRegisterMetaType<mn::IndexStats>();
}

VaultService::~VaultService()
{
    close();
}

bool VaultService::open(const QString& rootIn, QString* error, const QString& dbPath)
{
    close();
    const QFileInfo fi(rootIn);
    if (!fi.exists() || !fi.isDir()) {
        if (error) *error = u"\"%1\" isn't a folder."_s.arg(rootIn);
        return false;
    }
    root_ = QDir::cleanPath(fi.absoluteFilePath());
    const SettingsLoadResult s = loadVaultSettings(root_);
    settings_ = s.settings;
    settingsWarning_ = s.warning;

    worker_ = new IndexWorker;
    worker_->moveToThread(&thread_);
    connect(&thread_, &QThread::finished, worker_, &QObject::deleteLater);
    connect(worker_, &IndexWorker::snapshotReady, this, &VaultService::onSnapshot, Qt::QueuedConnection);
    connect(worker_, &IndexWorker::indexingFinished, this, &VaultService::indexingFinished,
            Qt::QueuedConnection);
    connect(worker_, &IndexWorker::filesChangedOnDisk, this, &VaultService::filesChangedOnDisk,
            Qt::QueuedConnection);
    connect(worker_, &IndexWorker::errorOccurred, this, &VaultService::errorOccurred, Qt::QueuedConnection);
    thread_.setObjectName(u"indexer"_s);
    thread_.start(QThread::LowPriority);

    const QString db = dbPath.isEmpty() ? IndexStore::defaultPath(root_) : dbPath;
    QMetaObject::invokeMethod(
        worker_,
        [w = worker_, r = root_, db, props = settings_.tagProperties]() { w->openVault(r, db, props); },
        Qt::QueuedConnection);
    return true;
}

void VaultService::close()
{
    if (thread_.isRunning()) {
        thread_.quit();
        thread_.wait();
    }
    worker_ = nullptr;
    root_.clear();
    snapshot_ = {};
}

QString VaultService::absolutePath(QStringView vaultPath) const
{
    return paths::toAbsolute(root_, vaultPath);
}

bool VaultService::setSettings(const VaultSettings& s, QString* error)
{
    if (!saveVaultSettings(root_, s, error)) return false;
    const bool reindex = s.tagProperties != settings_.tagProperties;
    settings_ = s;
    if (reindex && worker_ != nullptr) {
        QMetaObject::invokeMethod(
            worker_, [w = worker_, props = s.tagProperties]() { w->setTagProperties(props); },
            Qt::QueuedConnection);
    }
    return true;
}

QStringList VaultService::aliasesOf(const QString& path) const
{
    return snapshot_.aliases.value(path);
}

const FileIndex& VaultService::files() const
{
    static const FileIndex empty;
    return snapshot_.index ? *snapshot_.index : empty;
}

void VaultService::onSnapshot(const VaultSnapshot& snap)
{
    snapshot_ = snap;
    Q_EMIT snapshotChanged();
}

template <typename R, typename Work> void VaultService::query(Work work, Callback<R> cb)
{
    if (worker_ == nullptr) return;
    QPointer<VaultService> self(this);
    QMetaObject::invokeMethod(
        worker_,
        [w = worker_, work = std::move(work), cb = std::move(cb), self]() mutable {
            R result = work(w);
            if (self.isNull()) return;
            QMetaObject::invokeMethod(
                self.data(),
                [cb = std::move(cb), result = std::move(result)]() mutable { cb(std::move(result)); },
                Qt::QueuedConnection);
        },
        Qt::QueuedConnection);
}

void VaultService::backlinks(const QString& path, Callback<std::vector<Backlink>> cb)
{
    query<std::vector<Backlink>>([path](IndexWorker* w) { return w->backlinks(path); }, std::move(cb));
}

void VaultService::unlinkedMentions(const QString& path, Callback<std::vector<Mention>> cb)
{
    const QStringList aliases = aliasesOf(path);
    query<std::vector<Mention>>(
        [path, aliases](IndexWorker* w) { return w->unlinkedMentions(path, aliases); }, std::move(cb));
}

void VaultService::search(const QString& q, Callback<std::vector<SearchHit>> cb)
{
    query<std::vector<SearchHit>>([q](IndexWorker* w) { return w->search(q, 200); }, std::move(cb));
}

void VaultService::headings(const QString& path, Callback<std::vector<HeadingInfo>> cb)
{
    query<std::vector<HeadingInfo>>([path](IndexWorker* w) { return w->headings(path); }, std::move(cb));
}

void VaultService::blocks(const QString& path, Callback<std::vector<BlockInfo>> cb)
{
    query<std::vector<BlockInfo>>([path](IndexWorker* w) { return w->blocks(path); }, std::move(cb));
}

void VaultService::filesWithTag(const QString& tag, Callback<QStringList> cb)
{
    query<QStringList>([tag](IndexWorker* w) { return w->filesWithTag(tag); }, std::move(cb));
}

void VaultService::unresolvedLinks(Callback<std::vector<UnresolvedLink>> cb)
{
    query<std::vector<UnresolvedLink>>([](IndexWorker* w) { return w->unresolvedLinks(); }, std::move(cb));
}

void VaultService::tasks(Callback<std::vector<TaskInfo>> cb)
{
    query<std::vector<TaskInfo>>([](IndexWorker* w) { return w->tasks(); }, std::move(cb));
}

void VaultService::relatedNotes(const QString& path, Callback<std::vector<RelatedNote>> cb, int limit)
{
    query<std::vector<RelatedNote>>([path, limit](IndexWorker* w) { return w->relatedNotes(path, limit); },
                                    std::move(cb));
}

void VaultService::healthReport(Callback<HealthReport> cb)
{
    const QString skip = settings_.templatesFolder;
    const QStringList cats = settings_.categoryTags;
    const int min = settings_.minCategoryTags;
    query<HealthReport>([skip, cats, min](IndexWorker* w) { return w->healthReport(skip, cats, min); },
                        std::move(cb));
}

void VaultService::noteTags(Callback<std::vector<NoteTags>> cb)
{
    query<std::vector<NoteTags>>([](IndexWorker* w) { return w->noteTags(); }, std::move(cb));
}

void VaultService::detectTagScheme(Callback<TagScheme> cb)
{
    query<TagScheme>([](IndexWorker* w) { return w->detectTagScheme(); }, std::move(cb));
}

void VaultService::renameCandidates(const std::vector<Move>& moves, Callback<QStringList> cb)
{
    query<QStringList>([moves](IndexWorker* w) { return w->renameCandidates(moves); }, std::move(cb));
}

void VaultService::planRename(const std::vector<Move>& moves, Callback<RenamePlan> cb)
{
    query<RenamePlan>(
        [moves](IndexWorker* w) {
            const QStringList candidates = w->renameCandidates(moves);
            const QString root = w->root();
            return planMoves(w->fileIndex(), moves, candidates,
                             [root](const QString& p) -> std::optional<QByteArray> {
                                 QFile f(paths::toAbsolute(root, p));
                                 if (!f.open(QIODevice::ReadOnly)) return std::nullopt;
                                 return f.readAll();
                             });
        },
        std::move(cb));
}

void VaultService::refresh(const QStringList& vaultPaths)
{
    if (worker_ == nullptr) return;
    QMetaObject::invokeMethod(
        worker_, [w = worker_, vaultPaths]() { w->refreshPaths(vaultPaths); }, Qt::QueuedConnection);
}

void VaultService::rescan()
{
    if (worker_ == nullptr) return;
    QMetaObject::invokeMethod(worker_, [w = worker_]() { w->fullRescan(); }, Qt::QueuedConnection);
}

void VaultService::waitForIdle()
{
    if (worker_ == nullptr) return;
    QSemaphore sem;
    QMetaObject::invokeMethod(worker_, [&sem]() { sem.release(); }, Qt::QueuedConnection);
    sem.acquire();
}

} // namespace mn
