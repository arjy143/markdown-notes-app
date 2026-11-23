#pragma once

#include <QByteArray>
#include <QString>

#include <optional>

// Ticking off and deleting tasks from the Tasks view (CLAUDE.md allows these
// single, explicit edits to a note that isn't open). Each edit is planned
// against the note's current bytes, after checking the task is still where the
// index says it is, and touches nothing else.
namespace mn {

enum class TaskAction : unsigned char { MarkDone, MarkOpen, Delete };

// Identifies a task as the index saw it.
struct TaskLocator {
    int line = 0;
    QString text;
};

// A byte-range replacement in the BOM-stripped note content.
struct ByteEdit {
    qsizetype start = 0;
    qsizetype end = 0;
    QByteArray replacement;
};

// Plans the edit. The task is found at its line with the same text, or, if the
// note shifted since indexing, as the only task with that text. Delete removes
// the item's lines including nested items.
std::optional<ByteEdit> planTaskEdit(QByteArrayView body, const TaskLocator& task, TaskAction action,
                                     QString* error);

struct TaskEditResult {
    bool ok = false;
    QString error;
};

// Applies the edit to a file atomically (BOM and line endings kept).
TaskEditResult editTaskInFile(const QString& absPath, const TaskLocator& task, TaskAction action);

} // namespace mn
