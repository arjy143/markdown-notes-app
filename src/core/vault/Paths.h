#pragma once

#include <QString>
#include <QStringView>

#include <optional>

// Helpers for vault-relative paths. Internally every path is relative to the
// vault root, uses '/' separators and keeps the case it has on disk.
namespace mn::paths {

// "a/b/c.md" -> "a/b"; "c.md" -> "".
QString folderOf(QStringView path);
// "a/b/c.md" -> "c.md".
QString fileName(QStringView path);
// "a/b/c.md" -> "c" (strips only a final ".md"; attachments keep their extension).
QString linkName(QStringView path);
// "a/b/c.md" -> "c"; "x/img.png" -> "img".
QString completeBaseName(QStringView path);
// Joins folder and name, handling an empty folder.
QString join(QStringView folder, QStringView name);
// Resolves "." and ".." segments and collapses repeated slashes. Returns nullopt
// if the path climbs above the root.
std::optional<QString> normalize(QStringView path);
// Relative path from `fromFolder` to `toPath`, e.g. ("a/b", "a/c/d.md") -> "../c/d.md".
QString relativePath(QStringView fromFolder, QStringView toPath);
// Case-insensitive key used for every path comparison.
QString foldKey(QStringView s);
// Converts an absolute filesystem path inside `root` to a vault-relative path.
QString toVaultPath(const QString& root, const QString& absolute);
// Converts a vault-relative path to an absolute filesystem path.
QString toAbsolute(const QString& root, QStringView vaultPath);
// True if `path` is inside `folder` (or equal to it), case-insensitively.
bool isInside(QStringView path, QStringView folder);
// The first of "folder/base.ext", "folder/base 1.ext", "folder/base 2.ext"… that
// doesn't exist under `root`. Empty if none is free.
QString uniqueVaultPath(const QString& root, QStringView folder, QStringView base, QStringView ext);

} // namespace mn::paths
