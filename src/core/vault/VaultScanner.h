#pragma once

#include "FileTypes.h"

#include <QString>
#include <QStringList>

#include <vector>

namespace mn {

struct FileEntry {
    QString path; // vault-relative, '/' separators
    FileKind kind = FileKind::Other;
    qint64 size = 0;
    qint64 mtimeMs = 0;
};

struct ScanResult {
    std::vector<FileEntry> files; // every non-hidden file, sorted by path
    QStringList folders;          // every non-hidden folder (vault-relative), sorted
};

// Recursively lists the vault, skipping hidden files and folders (compat spec §1).
// Symlinked folders are followed once; cycles are skipped. Runs on a worker thread.
ScanResult scanVault(const QString& root);

// Recursively lists `folder` (vault-relative) and everything below it.
ScanResult scanTree(const QString& root, const QString& folder);

// Lists one folder (non-recursive). `folder` is vault-relative ("" for the root).
ScanResult scanFolder(const QString& root, const QString& folder);

} // namespace mn
