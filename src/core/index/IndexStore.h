#pragma once

#include "IndexTypes.h"
#include "Sqlite.h"
#include "core/parse/NoteMetadata.h"

#include <QByteArray>
#include <QHash>
#include <QString>

#include <functional>
#include <memory>
#include <vector>

// The SQLite index: a disposable cache of note metadata plus an FTS5 table.
// Lives in QStandardPaths::CacheLocation/<vault-id>/index.sqlite, never inside
// the vault. Owned by the worker thread.
namespace mn {

struct StoredFile {
    qint64 id = 0;
    QString path;
    FileKind kind = FileKind::Other;
    qint64 size = 0;
    qint64 mtimeMs = 0;
    QByteArray hash;
};

struct StoredLink {
    QString sourcePath;
    QString linkpath;
    QString subpath;
    int line = 0;
    bool embed = false;
};

class IndexStore {
public:
    static constexpr int kSchemaVersion = 3;

    static QString vaultId(const QString& vaultRoot);
    static QString defaultPath(const QString& vaultRoot);

    bool open(const QString& dbPath, QString* error);
    void close()
    {
        st_.reset();
        db_.close();
    }
    bool isOpen() const { return db_.isOpen(); }
    sql::Database& db() { return db_; }

    std::vector<StoredFile> loadFiles();
    QString meta(const QString& key);
    void setMeta(const QString& key, const QString& value);

    // Bulk mode for large batches (first index, big external changes): a large page
    // cache and no fsync. The index is a disposable cache, so a crash mid-batch only
    // costs a rebuild. endBulk() restores the small cache and frees the memory.
    void beginBulk();
    void endBulk();

    // Inserts or replaces one file's row and all its metadata. Call inside a transaction.
    // `known` = the path may already be in the index (its old rows are removed first).
    bool putNote(const QString& path, qint64 size, qint64 mtimeMs, const QByteArray& hash,
                 const NoteMetadata& meta, const QString& body, bool known = true);
    bool putAttachment(const QString& path, FileKind kind, qint64 size, qint64 mtimeMs, bool known = true);
    bool updateStamp(const QString& path, qint64 size, qint64 mtimeMs);
    bool removeFile(const QString& path);

    // Queries
    std::vector<StoredLink> linksWithNameKeys(const QStringList& keys);
    std::vector<StoredLink> allLinks();
    QStringList notesLinkingToNames(const QStringList& keys);
    QHash<QString, QStringList> allAliases();
    std::vector<TagCount> tagCounts();
    QStringList filesWithTag(const QString& tagKey);
    std::vector<HeadingInfo> headings(const QString& path);
    std::vector<BlockInfo> blocks(const QString& path);
    QString body(const QString& path);
    std::vector<TaskInfo> tasks();
    QStringList tagKeysOf(const QString& path);
    std::vector<NoteTags> noteTags(); // every note, with its tag keys (notes without tags included)
    // Number of notes containing an (FTS-tokenised) term.
    int documentFrequency(const QString& term);
    // Every FTS term with the number of notes containing it.
    QHash<QString, int> allDocumentFrequencies();
    int noteCount();
    // Notes matching any of the terms, best BM25 first.
    QStringList notesMatchingAny(const QStringList& terms, int limit);
    // Every note body, for whole-vault analysis (calls fn(path, body)).
    void forEachBody(const std::function<void(const QString&, const QString&)>& fn);
    std::vector<SearchHit> search(const QString& userQuery, int limit);
    QStringList ftsCandidates(const QString& phrase, int limit);

    // Builds an FTS5 MATCH expression from free text (prefix match on the last term).
    static QString ftsQuery(const QString& userQuery);

private:
    bool createSchema(QString* error);
    void prepareStatements();
    qint64 idFor(const QString& path);

    sql::Database db_;
    // Prepared once per connection: indexing runs these tens of thousands of times.
    struct Statements {
        sql::Statement selectId, insertFile, insertLink, insertTag, insertHeading, insertBlock, insertAlias,
            insertFts, updateStamp, insertTask, docFreq;
        sql::Statement deletes[8];
    };
    std::unique_ptr<Statements> st_;
};

} // namespace mn
