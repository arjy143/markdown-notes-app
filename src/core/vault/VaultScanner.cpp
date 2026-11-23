#include "VaultScanner.h"

#include "Paths.h"

#include <QDir>
#include <QFileInfo>
#include <QSet>

#include <algorithm>

namespace mn {

namespace {

void scanInto(const QString& root, const QString& folder, ScanResult& out, QSet<QString>* visited,
              bool recurse)
{
    const QString abs = paths::toAbsolute(root, folder);
    if (visited != nullptr) {
        const QString canonical = QFileInfo(abs).canonicalFilePath();
        if (canonical.isEmpty() || visited->contains(canonical)) return;
        visited->insert(canonical);
    }
    const QDir dir(abs);
    const QFileInfoList entries = dir.entryInfoList(
        QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System, QDir::NoSort);
    for (const QFileInfo& fi : entries) {
        const QString name = fi.fileName();
        if (isHiddenName(name)) continue;
        const QString rel = paths::join(folder, name);
        if (fi.isDir()) {
            out.folders.append(rel);
            if (recurse) scanInto(root, rel, out, visited, true);
        } else if (fi.isFile()) {
            out.files.push_back({rel, classifyPath(name), fi.size(), fi.lastModified().toMSecsSinceEpoch()});
        }
    }
}

void sortResult(ScanResult& r)
{
    std::sort(r.files.begin(), r.files.end(),
              [](const FileEntry& a, const FileEntry& b) { return a.path < b.path; });
    r.folders.sort();
}

} // namespace

ScanResult scanVault(const QString& root)
{
    ScanResult out;
    QSet<QString> visited;
    scanInto(root, QString(), out, &visited, true);
    sortResult(out);
    return out;
}

ScanResult scanTree(const QString& root, const QString& folder)
{
    ScanResult out;
    QSet<QString> visited;
    scanInto(root, folder, out, &visited, true);
    sortResult(out);
    return out;
}

ScanResult scanFolder(const QString& root, const QString& folder)
{
    ScanResult out;
    scanInto(root, folder, out, nullptr, false);
    sortResult(out);
    return out;
}

} // namespace mn
