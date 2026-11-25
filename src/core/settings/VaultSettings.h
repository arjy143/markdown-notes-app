#pragma once

#include "core/links/FileIndex.h"

#include <QList>
#include <QString>
#include <QStringList>

// Per-vault settings, stored in <vault>/.markdown-notes/settings.json. On the
// first open they are imported once from .obsidian/ (compat spec §8), which is
// only ever read.
namespace mn {

enum class NewFileLocation : unsigned char { Root, Current, Folder };

// A property in the note form (Ctrl+N, Ctrl+T): what it holds decides how it's filled in.
enum class PropertyKind : unsigned char {
    Category, // one of the category tags, from a dropdown
    Choice,   // one of the property's own options, from a dropdown (e.g. status)
    Tags,     // tags, picked from the vault's tags
    Text,     // one line of text (e.g. a summary)
    List,     // several values, one per line (e.g. aliases, sources)
    Date,     // YYYY-MM-DD; today on a new note
};

struct PropertySpec {
    QString key;
    PropertyKind kind = PropertyKind::Text;
    QStringList options; // for Choice
    friend bool operator==(const PropertySpec&, const PropertySpec&) = default;
};

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
    // Tags a new daily note starts with (in its frontmatter).
    QStringList dailyNoteTags{QStringLiteral("journal")};
    // Tag organisation. Values of these frontmatter properties also count as tags
    // (e.g. "primary", "secondary"); category tags are the vault's top-level tags.
    QStringList tagProperties;
    QStringList categoryTags;
    int minCategoryTags = 2; // vault health reports notes with fewer categories
    QStringList pinnedTags;  // shown first in the tag pane, in this order
    // The properties the note form offers, in this order. Empty: the default set
    // (see formProperties).
    QList<PropertySpec> noteProperties;
    // Choosing a category also adds it to the note's tags (so Obsidian's tag pane shows it).
    bool categoriesInTags = true;
    // The form writes properties left blank as empty ("status:"), so every note has the same keys.
    bool writeEmptyProperties = true;

    // noteProperties, or when it's empty: the tag properties as categories, then
    // tags, aliases, status (draft / active / done), summary, sources and created.
    QList<PropertySpec> formProperties() const;
    // The keys of the category properties in the form.
    QStringList categoryProperties() const;
    // What a category property can be: the category tags, or the pinned tags when none are set.
    QStringList categoryChoices() const { return categoryTags.isEmpty() ? pinnedTags : categoryTags; }

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
