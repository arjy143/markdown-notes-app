#pragma once

#include "IndexStore.h"
#include "IndexTypes.h"
#include "core/links/FileIndex.h"
#include "core/links/RenamePlanner.h"
#include "core/vault/VaultScanner.h"

#include <QHash>
#include <QObject>
#include <QSet>
#include <QStringList>

class QFileSystemWatcher;
class QTimer;

namespace mn {

// Owns the SQLite index and the vault scan. Lives on a worker thread: every
// method must be called on that thread (VaultService takes care of this).
class IndexWorker : public QObject {
    Q_OBJECT
public:
    explicit IndexWorker(QObject* parent = nullptr);
    ~IndexWorker() override;

    // Opens the cache, publishes the cached file list, then scans and indexes changes.
    void openVault(const QString& root, const QString& dbPath, const QStringList& tagProperties = {});
    // Frontmatter properties whose values count as tags. Changing them re-indexes every note.
    void setTagProperties(const QStringList& keys);
    void fullRescan();
    // Re-checks specific files now (e.g. right after the app wrote or renamed them).
    void refreshPaths(const QStringList& vaultPaths);

    std::vector<Backlink> backlinks(const QString& path);
    std::vector<Mention> unlinkedMentions(const QString& path, const QStringList& aliases);
    std::vector<SearchHit> search(const QString& query, int limit);
    std::vector<HeadingInfo> headings(const QString& path) { return store_.headings(path); }
    std::vector<BlockInfo> blocks(const QString& path) { return store_.blocks(path); }
    QStringList filesWithTag(const QString& tag) { return store_.filesWithTag(tag); }
    std::vector<UnresolvedLink> unresolvedLinks();
    std::vector<TaskInfo> tasks() { return store_.tasks(); }
    std::vector<RelatedNote> relatedNotes(const QString& path, int limit);
    // `skipFolder`: notes there (e.g. templates) aren't reported as orphans.
    // `categories` / `minCategories`: report notes with too few category tags (skipped if empty).
    HealthReport healthReport(const QString& skipFolder, const QStringList& categories = {},
                              int minCategories = 2);
    std::vector<NoteTags> noteTags() { return store_.noteTags(); }
    TagScheme detectTagScheme();
    QStringList renameCandidates(const std::vector<Move>& moves);
    const FileIndex& fileIndex() const { return *files_; }
    const QString& root() const { return root_; }

Q_SIGNALS:
    void snapshotReady(const mn::VaultSnapshot& snapshot);
    void indexingFinished(const mn::IndexStats& stats);
    // Files that were modified or removed on disk since the last check.
    void filesChangedOnDisk(const QStringList& paths);
    void errorOccurred(const QString& message);

private:
    struct Change {
        FileEntry entry;
        bool isNew = false;
    };
    void onDirectoryChanged(const QString& absPath);
    void processPendingFolders();
    // Applies a scan of `scope` (a folder subtree, or "" for the vault) to the index.
    void applyScan(const ScanResult& scan, const QString& scope, bool recursive);
    void indexChanges(const std::vector<Change>& changes, QStringList& modified);
    void emitSnapshot();
    void syncWatches();

    QString root_;
    IndexStore store_;
    std::shared_ptr<const FileIndex> files_ = std::make_shared<FileIndex>();
    QHash<QString, StoredFile> stored_; // by path
    QStringList folders_;
    QStringList tagProperties_;
    void forceReparse();
    // Statistics for related notes, reloaded when the index changes.
    void ensureStats();
    QHash<QString, int> docFreq_;
    QHash<QString, int> tagNotes_;
    int noteCount_ = 0;
    int statsGeneration_ = -1;
    QFileSystemWatcher* watcher_ = nullptr;
    QTimer* debounce_ = nullptr;
    QSet<QString> pendingFolders_;
    int generation_ = 0;
};

} // namespace mn
