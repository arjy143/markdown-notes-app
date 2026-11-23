#pragma once

#include "FileIndex.h"

#include <QByteArray>
#include <QString>
#include <QStringList>

#include <functional>
#include <optional>
#include <vector>

// Planning and applying rename/move with link updates (compat spec §7).
//
// Invariant: after the move, every link that resolved before resolves to the
// same file (with the moved files at their new paths). Links that pointed at a
// moved file are rewritten in the style they were written in, keeping alias and
// subpath. Links whose target would otherwise change (for example because the
// new name collides with another file) are rewritten too.
namespace mn {

struct Move {
    QString from; // vault-relative file path before
    QString to;   // after
};

struct LinkEdit {
    qsizetype start = 0; // byte range in the file (BOM-stripped content)
    qsizetype end = 0;
    QByteArray replacement;
    int line = 0;
    QString before; // link path before / after, for the confirmation list
    QString after;
};

struct FileEditPlan {
    QString path;       // vault path the file has before the move
    QString targetPath; // vault path it will have after the move (where it is written)
    QByteArray originalRaw;
    QByteArray newRaw;
    std::vector<LinkEdit> edits;
};

struct RenamePlan {
    std::vector<Move> moves;
    std::vector<FileEditPlan> files; // files whose links change
    QStringList warnings;            // links that couldn't be updated, with reasons
};

using NoteLoader = std::function<std::optional<QByteArray>(const QString& vaultPath)>;

// `candidates` are notes (pre-move paths) that may contain affected links: notes
// linking to any old or new file name, plus every moved note. Extra candidates
// are harmless.
RenamePlan planMoves(const FileIndex& before, const std::vector<Move>& moves, const QStringList& candidates,
                     const NoteLoader& load);

struct RenameResult {
    bool moved = false;  // the files/folder were renamed on disk
    QString moveError;   // why the move failed (then no file was edited)
    QStringList updated; // files whose links were rewritten
    QStringList failed;  // "path: reason" for files that couldn't be rewritten
};

// Executes a plan: renames on disk first (a folder rename if `folderMove` is
// set, otherwise each file), then rewrites each affected file atomically. A
// file whose bytes changed since planning is skipped and reported.
RenameResult executeRenamePlan(const QString& vaultRoot, const RenamePlan& plan,
                               const std::optional<Move>& folderMove = std::nullopt);

// Applies edits to raw file bytes (BOM preserved). Edits must not overlap.
QByteArray applyEdits(const QByteArray& raw, const std::vector<LinkEdit>& edits);

// Encodes a link path for a Markdown link destination.
QString encodeMarkdownPath(QStringView path);

// Folded "name key" for a link path or file path: last segment, minus a final
// ".md". Used to find notes whose links might point at a file.
QString linkNameKey(QStringView linkpathOrFilePath);

} // namespace mn
