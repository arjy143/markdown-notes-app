#pragma once

#include <QList>
#include <QPair>
#include <QString>
#include <QStringList>

// Changes a note's frontmatter property by property: sets values, adds missing
// ones, adds and removes tags, without touching anything else. Other properties
// are never changed or reformatted, every line keeps its own line ending, the
// body is never touched, and a frontmatter block that doesn't parse is left alone. Used to
// apply the assistant's suggestions to the open note (in the editor, as one
// undoable edit).
namespace mn {

// Sets a property to `values` (one for a single value, any number for a list).
// No values: the property is written empty ("key:") when `keepEmpty`, else removed.
struct PropertyChange {
    QString key;
    QStringList values;
    bool list = false;
    bool keepEmpty = false;
};

struct FrontmatterPatch {
    QList<PropertyChange> set;                   // applied first, in order; new keys are appended
    QStringList addTags;                         // added to `tags` if not already there (any case)
    QStringList removeTags;                      // removed from `tags` (any case, with or without '#')
    QList<QPair<QString, QString>> addIfMissing; // key, scalar value; skipped if the key exists
};

struct FrontmatterEdit {
    bool ok = false; // false: nothing can be changed (see error)
    QString error;
    bool changed = false; // false: everything was already there
    // The note's first replaceLines lines are replaced by `text` (0: `text` is inserted at the top).
    int replaceLines = 0;
    QString text;        // the new frontmatter block, ending with a line break
    QStringList skipped; // what couldn't be added, e.g. tags in a nested value
};

// `noteText`: the note without a BOM, '\n' or "\r\n" line endings.
FrontmatterEdit planFrontmatterEdit(QStringView noteText, const FrontmatterPatch& patch);

// The note with the edit applied (for tests and callers without an editor).
QString applyFrontmatterEdit(QStringView noteText, const FrontmatterEdit& edit);

// A tag as Obsidian accepts it (letters, digits, _ - /, not only digits), or empty.
// "#Machine Learning" -> "Machine-Learning".
QString cleanTag(QStringView tag);

// Key under which tags that are probably the same compare equal: case, '-', '_',
// spaces and a plural 's' ignored ("Machine-Learning" ~ "machine_learnings").
QString tagLookAlikeKey(QStringView tag);

// A YAML scalar: plain when that's unambiguous, otherwise double-quoted.
QString yamlScalar(const QString& value);

} // namespace mn
