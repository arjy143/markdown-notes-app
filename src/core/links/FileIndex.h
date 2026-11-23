#pragma once

#include "core/vault/FileTypes.h"

#include <QHash>
#include <QString>
#include <QStringView>

#include <vector>

namespace mn {

struct VaultFile {
    QString path;   // vault-relative, real case
    QString folded; // case-folded path, for lookups
    FileKind kind = FileKind::Other;
};

enum class ResolveStep : unsigned char { None, Exact, Relative, Name, Self };

struct Resolution {
    int file = -1;
    ResolveStep step = ResolveStep::None;
    bool resolved() const { return file >= 0; }
};

enum class LinkStyle : unsigned char { Shortest, Relative, Absolute };

// In-memory table of every vault file, used for link resolution and
// autocomplete (compat spec §3). Ids are indices and change when files are
// removed, so don't keep them across modifications.
class FileIndex {
public:
    void clear();
    void add(const QString& path, FileKind kind);
    void remove(QStringView path);
    void rename(QStringView from, const QString& to);
    void rebuild(); // recompute lookup tables after bulk add()

    int size() const { return static_cast<int>(files_.size()); }
    const VaultFile& file(int id) const { return files_[static_cast<std::size_t>(id)]; }
    const std::vector<VaultFile>& files() const { return files_; }
    int find(QStringView path) const; // case-insensitive exact path, or -1

    // Resolves a link path written in `sourcePath` (compat spec §3, steps 1–5).
    Resolution resolve(QStringView linkpath, QStringView sourcePath) const;

    // Files whose name (last segment) matches case-insensitively.
    const std::vector<int>* filesNamed(QStringView fileName) const;
    // True if more than one file has this file name (last segment, with extension).
    bool isNameAmbiguous(QStringView fileName) const;

    // Builds the link path for `targetPath` as seen from `sourcePath`.
    // Notes drop ".md" unless keepExtension. Never URL-encodes.
    QString linkpathFor(QStringView targetPath, QStringView sourcePath, LinkStyle style,
                        bool keepExtension) const;

private:
    std::vector<int> nameCandidates(QStringView candidateFolded) const;
    int pickBest(const std::vector<int>& ids, QStringView sourcePath) const;

    std::vector<VaultFile> files_;
    QHash<QString, int> byPath_;
    QHash<QString, std::vector<int>> byName_;
    bool dirty_ = false;
};

} // namespace mn
