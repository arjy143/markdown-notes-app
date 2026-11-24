#pragma once

#include "core/links/FileIndex.h"
#include "core/vault/FileTypes.h"

#include <QHash>
#include <QList>
#include <QMetaType>
#include <QString>
#include <QStringList>

#include <memory>
#include <vector>

// Plain value types passed from the indexer (worker thread) to the UI.
namespace mn {

struct TagCount {
    QString tag; // most common spelling
    int notes = 0;
};

// What the UI shows. The file table is shared (read-only) with the indexer
// thread rather than copied: the worker publishes a new FileIndex whenever the
// file set changes and never modifies one it has published.
struct VaultSnapshot {
    std::shared_ptr<const FileIndex> index; // every vault file, sorted by path
    QHash<QString, QStringList> aliases;    // note path -> frontmatter aliases (only notes that have some)
    QStringList folders;                    // sorted
    std::vector<TagCount> tags;             // sorted case-insensitively
    int generation = 0;

    const std::vector<VaultFile>& files() const
    {
        static const std::vector<VaultFile> none;
        return index ? index->files() : none;
    }
};

struct Backlink {
    QString sourcePath;
    int line = 0;
    QString context; // the source line
    bool embed = false;
};

struct Mention {
    QString sourcePath;
    int line = 0;
    QString context;
};

struct SearchHit {
    QString path;
    QString snippet; // with \x01 / \x02 around matches
    double rank = 0;
};

// A passage of a note (a chunk, see Chunker.h): a search hit or a related passage.
struct Passage {
    QString path;
    int startLine = 0; // 0-based, inclusive
    int endLine = 0;
    QString context;   // heading path
    QString snippet;   // with \x01 / \x02 around matches
    QString text;      // the whole passage
    double score = 0;  // search: BM25 (lower is better); related: similarity (higher is better)
    qint64 dateMs = 0; // the date in the file name (daily notes), else the file's modification time
    QString reason;    // related passages: the words they share
};

// Passages waiting to be embedded for semantic search, and how far along it is.
struct EmbeddingJob {
    QByteArray hash; // passageHash()
    QString text;    // passageEmbeddingText()
};
struct EmbeddingBatch {
    std::vector<EmbeddingJob> jobs;
    int done = 0;  // passages with a vector for the model
    int total = 0; // passages in the vault (distinct contents)
};

struct HeadingInfo {
    int level = 1;
    QString text;
    int line = 0;
};

struct BlockInfo {
    QString id;
    int startLine = 0;
    int endLine = 0;
};

struct TaskInfo {
    QString path;
    int line = 0;
    bool done = false;
    QString text;
    QString due; // "YYYY-MM-DD" or empty
};

struct RelatedNote {
    QString path;
    double score = 0;
    QString reason; // e.g. "links here · #project · cosine, similarity"
};

struct TagGroup {
    QStringList tags; // spellings that look like the same tag
    QList<int> notes; // notes per tag, same order
};

struct DuplicateNotes {
    QString a;
    QString b;
    double similarity = 0; // share of overlapping 3-word phrases, 0..1
};

struct UnresolvedLink {
    QString sourcePath;
    QString linkpath;
    int line = 0;
};

struct CategoryProblem {
    QString path;
    QStringList categories; // the category tags the note does have
};

// A suggested tag organisation, detected from frontmatter (see detectTagScheme).
struct TagScheme {
    QStringList properties; // frontmatter keys holding a category, e.g. primary, secondary
    QStringList categories; // the values they use, most common first
};

// One note's tags (case-folded keys).
struct NoteTags {
    QString path;
    QStringList keys;
};

struct HealthReport {
    std::vector<UnresolvedLink> brokenLinks;
    QStringList orphanNotes;       // no links from other notes
    QStringList emptyNotes;        // nothing but frontmatter and whitespace
    QStringList unusedAttachments; // no note links to or embeds them
    std::vector<TagGroup> similarTags;
    std::vector<DuplicateNotes> duplicateNotes;
    std::vector<CategoryProblem> fewCategories; // notes with fewer than minCategories category tags
    int minCategories = 0;                      // 0 = check not configured
    qint64 elapsedMs = 0;
};

struct IndexStats {
    int notes = 0;
    int attachments = 0;
    int indexedNow = 0; // notes (re)parsed in the last pass
    qint64 elapsedMs = 0;
};

} // namespace mn

Q_DECLARE_METATYPE(mn::VaultSnapshot)
Q_DECLARE_METATYPE(mn::IndexStats)
