#pragma once

#include <QString>
#include <QStringView>

namespace mn {

// What a vault file is, by extension (compat spec §1).
enum class FileKind : unsigned char { Note, Image, Pdf, Media, Other };

FileKind classifyPath(QStringView path);
bool isAttachmentKind(FileKind kind);
// True for names starting with '.', which are never listed, indexed or linked to.
bool isHiddenName(QStringView name);
// True if the path's last segment ends in an extension we recognise (note or attachment).
bool hasKnownExtension(QStringView path);
// Lower-case extension without the dot, or empty.
QString extensionOf(QStringView path);

// Characters Obsidian doesn't allow in new file names, and characters that break links.
// Returns an empty string if the name is acceptable, otherwise a user-facing reason.
QString checkNewFileName(QStringView name);

// Checks every segment of a vault-relative path the app is about to create
// (folders included): no hidden segments (so never inside .obsidian/), no "..",
// no forbidden characters. Empty string = acceptable.
QString checkNewVaultPath(QStringView path);

} // namespace mn
