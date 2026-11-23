#pragma once

#include "IndexTypes.h"
#include "core/links/FileIndex.h"
#include "core/links/RenamePlanner.h"
#include "core/settings/VaultSettings.h"

#include <QObject>
#include <QPointer>
#include <QString>
#include <QThread>

#include <functional>

namespace mn {

class IndexWorker;

// UI-thread facade over the background indexer. All vault-wide work runs on
// the worker thread; results come back as queued callbacks on the UI thread.
class VaultService : public QObject {
    Q_OBJECT
public:
    template <typename R> using Callback = std::function<void(R)>;

    explicit VaultService(QObject* parent = nullptr);
    ~VaultService() override;

    // Opens a vault folder. `dbPath` defaults to the per-user cache location.
    bool open(const QString& root, QString* error, const QString& dbPath = {});
    void close();
    bool isOpen() const { return !root_.isEmpty(); }

    const QString& root() const { return root_; }
    QString absolutePath(QStringView vaultPath) const;
    const VaultSettings& settings() const { return settings_; }
    bool setSettings(const VaultSettings& s, QString* error);
    QString settingsWarning() const { return settingsWarning_; }

    const VaultSnapshot& snapshot() const { return snapshot_; }
    // UI-thread copy of the file table, for resolving links while typing.
    const FileIndex& files() const;
    QStringList aliasesOf(const QString& path) const;

    void backlinks(const QString& path, Callback<std::vector<Backlink>> cb);
    void unlinkedMentions(const QString& path, Callback<std::vector<Mention>> cb);
    void search(const QString& query, Callback<std::vector<SearchHit>> cb);
    void headings(const QString& path, Callback<std::vector<HeadingInfo>> cb);
    void blocks(const QString& path, Callback<std::vector<BlockInfo>> cb);
    void filesWithTag(const QString& tag, Callback<QStringList> cb);
    void unresolvedLinks(Callback<std::vector<UnresolvedLink>> cb);
    void tasks(Callback<std::vector<TaskInfo>> cb);
    void relatedNotes(const QString& path, Callback<std::vector<RelatedNote>> cb, int limit = 5);
    void healthReport(Callback<HealthReport> cb);
    void noteTags(Callback<std::vector<NoteTags>> cb);
    void detectTagScheme(Callback<TagScheme> cb);
    void renameCandidates(const std::vector<Move>& moves, Callback<QStringList> cb);
    // Plans a rename/move on the worker thread (reads the affected notes there).
    void planRename(const std::vector<Move>& moves, Callback<RenamePlan> cb);

    // Ask the indexer to re-check these files now (after the app wrote them).
    void refresh(const QStringList& vaultPaths);
    void rescan();

    // Blocks until the worker has processed everything queued so far (tests, benchmarks).
    void waitForIdle();

Q_SIGNALS:
    void snapshotChanged();
    void indexingFinished(const mn::IndexStats& stats);
    void filesChangedOnDisk(const QStringList& paths);
    void errorOccurred(const QString& message);

private:
    template <typename R, typename Work> void query(Work work, Callback<R> cb);
    void onSnapshot(const VaultSnapshot& snap);

    QString root_;
    VaultSettings settings_;
    QString settingsWarning_;
    VaultSnapshot snapshot_;
    QThread thread_;
    IndexWorker* worker_ = nullptr;
};

} // namespace mn
