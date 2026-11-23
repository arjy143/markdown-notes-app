#pragma once

#include "core/links/FileIndex.h"

#include <QString>

// Per-vault settings, stored in <vault>/.markdown-notes/settings.json. On the
// first open they are imported once from .obsidian/ (compat spec §8), which is
// only ever read.
namespace mn {

enum class NewFileLocation : unsigned char { Root, Current, Folder };

struct VaultSettings {
    QString attachmentFolderPath = QStringLiteral("/"); // "/", "./", "./sub" or "folder/path"
    LinkStyle newLinkFormat = LinkStyle::Shortest;
    bool useMarkdownLinks = false;
    NewFileLocation newFileLocation = NewFileLocation::Root;
    QString newFileFolderPath;
    bool alwaysUpdateLinks = false;
    QString templatesFolder;
    QString dateFormat = QStringLiteral("YYYY-MM-DD");
    QString timeFormat = QStringLiteral("HH:mm");
    bool showAllFiles = false;
    // Reading view: single line breaks are spaces (Obsidian "strict line breaks").
    bool strictLineBreaks = false;
    // Quick capture: appended to `captureNote`, or to today's daily note.
    bool captureToDailyNote = false;
    QString captureNote = QStringLiteral("Inbox.md");
    QString captureFormat = QStringLiteral("- {{time}} {{text}}");
    // Daily notes (imported from .obsidian/daily-notes.json).
    QString dailyNotesFolder;
    QString dailyNoteFormat = QStringLiteral("YYYY-MM-DD");
    QString dailyNoteTemplate; // vault path of a template note, or empty
    // Tag organisation. Values of these frontmatter properties also count as tags
    // (e.g. "primary", "secondary"); category tags are the vault's top-level tags.
    QStringList tagProperties;
    QStringList categoryTags;
    int minCategoryTags = 2; // vault health reports notes with fewer categories
    QStringList pinnedTags;  // shown first in the tag pane, in this order

    // Vault-relative folder where attachments pasted into `notePath` go.
    QString attachmentFolderFor(QStringView notePath) const;
    // Vault-relative folder for a new note created while `currentNotePath` is open.
    QString newNoteFolder(QStringView currentNotePath) const;

    friend bool operator==(const VaultSettings&, const VaultSettings&) = default;
};

struct SettingsLoadResult {
    VaultSettings settings;
    bool imported = false; // read from .obsidian/ because settings.json didn't exist
    QString warning;       // non-fatal problem (e.g. settings.json couldn't be written)
};

QString settingsFilePath(const QString& vaultRoot);

// Reads .obsidian/app.json and templates.json (never writes there).
VaultSettings importObsidianSettings(const QString& vaultRoot);

// Loads settings.json; if missing, imports from .obsidian/ and writes settings.json.
SettingsLoadResult loadVaultSettings(const QString& vaultRoot);

bool saveVaultSettings(const QString& vaultRoot, const VaultSettings& settings, QString* error);

} // namespace mn
