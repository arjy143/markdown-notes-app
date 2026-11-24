#include "TaskEdit.h"

#include "core/parse/NoteMetadata.h"
#include "core/vault/TextFile.h"

#include <vector>

using namespace Qt::StringLiterals;

namespace mn {

namespace {

constexpr char kBom[] = "\xEF\xBB\xBF";

std::vector<qsizetype> lineStarts(QByteArrayView body)
{
    // Same line breaks as the parser: '\n', or a '\r' not followed by '\n'.
    std::vector<qsizetype> starts{0};
    for (qsizetype i = 0; i < body.size(); ++i) {
        if (body[i] == '\n' || (body[i] == '\r' && (i + 1 >= body.size() || body[i + 1] != '\n')))
            starts.push_back(i + 1);
    }
    return starts;
}

} // namespace

std::optional<ByteEdit> planTaskEdit(QByteArrayView body, const TaskLocator& want, TaskAction action,
                                     QString* error)
{
    auto fail = [&](const QString& why) -> std::optional<ByteEdit> {
        if (error) *error = why;
        return std::nullopt;
    };
    const NoteMetadata meta = parseNote(body);
    const TaskItem* found = nullptr;
    for (const TaskItem& t : meta.tasks) {
        if (t.line == want.line && t.text == want.text) found = &t;
    }
    if (found == nullptr) {
        int matches = 0;
        for (const TaskItem& t : meta.tasks) {
            if (t.text == want.text) {
                found = &t;
                ++matches;
            }
        }
        if (matches != 1) return fail(u"The task has changed. The list was refreshed."_s);
    }
    if (action == TaskAction::Delete) {
        const std::vector<qsizetype> starts = lineStarts(body);
        if (found->line < 0 || found->line >= static_cast<int>(starts.size()))
            return fail(u"The task couldn't be located."_s);
        const qsizetype start = starts[static_cast<std::size_t>(found->line)];
        const std::size_t after = static_cast<std::size_t>(found->endLine) + 1;
        const qsizetype end = after < starts.size() ? starts[after] : body.size();
        return ByteEdit{start, end, {}};
    }
    const qsizetype m = found->markOffset;
    if (m < 0 || m >= body.size() || (body[m] != ' ' && body[m] != 'x' && body[m] != 'X'))
        return fail(u"The task's checkbox couldn't be located."_s);
    return ByteEdit{m, m + 1, action == TaskAction::MarkDone ? QByteArray("x") : QByteArray(" ")};
}

TaskEditResult editTaskInFile(const QString& absPath, const TaskLocator& task, TaskAction action)
{
    TaskEditResult r;
    const LoadedText file = loadTextFile(absPath);
    if (!file.editable()) {
        r.error = file.error;
        return r;
    }
    const bool bom = file.raw.startsWith(QByteArrayView(kBom, 3));
    const QByteArrayView body = bom ? QByteArrayView(file.raw).sliced(3) : QByteArrayView(file.raw);
    const std::optional<ByteEdit> edit = planTaskEdit(body, task, action, &r.error);
    if (!edit) return r;
    QByteArray out = file.raw;
    const qsizetype base = bom ? 3 : 0;
    out.replace(base + edit->start, edit->end - edit->start, edit->replacement);
    const WriteResult w = writeFileAtomic(absPath, out);
    if (!w.ok) {
        r.error = w.error;
        return r;
    }
    r.ok = true;
    return r;
}

} // namespace mn
