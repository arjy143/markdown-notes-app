#include "RenamePlanner.h"

#include "core/parse/NoteMetadata.h"
#include "core/vault/Paths.h"
#include "core/vault/TextFile.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QSet>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace mn {

namespace {

constexpr char kBom[] = "\xEF\xBB\xBF";

LinkStyle inferStyle(const LinkRef& link, const Resolution& r)
{
    const QString& lp = link.linkpath;
    if (lp.startsWith(u"./"_s) || lp.startsWith(u"../"_s)) return LinkStyle::Relative;
    if (lp.contains(u'/')) return r.step == ResolveStep::Relative ? LinkStyle::Relative : LinkStyle::Absolute;
    return LinkStyle::Shortest;
}

} // namespace

QString linkNameKey(QStringView p)
{
    QString name = paths::fileName(p);
    if (name.endsWith(u".md"_s, Qt::CaseInsensitive)) name.chop(3);
    return paths::foldKey(name);
}

QString encodeMarkdownPath(QStringView path)
{
    QString out;
    out.reserve(path.size() + 8);
    for (QChar c : path) {
        switch (c.unicode()) {
        case ' ':
            out += u"%20"_s;
            break;
        case '%':
            out += u"%25"_s;
            break;
        case '<':
            out += u"%3C"_s;
            break;
        case '>':
            out += u"%3E"_s;
            break;
        case '(':
            out += u"%28"_s;
            break;
        case ')':
            out += u"%29"_s;
            break;
        case '[':
            out += u"%5B"_s;
            break;
        case ']':
            out += u"%5D"_s;
            break;
        default:
            out += c;
            break;
        }
    }
    return out;
}

QByteArray applyEdits(const QByteArray& raw, const std::vector<LinkEdit>& edits)
{
    const bool bom = raw.startsWith(QByteArrayView(kBom, 3));
    QByteArray body = bom ? raw.mid(3) : raw;
    std::vector<LinkEdit> sorted = edits;
    std::sort(sorted.begin(), sorted.end(),
              [](const LinkEdit& a, const LinkEdit& b) { return a.start > b.start; });
    for (const LinkEdit& e : sorted)
        body.replace(e.start, e.end - e.start, e.replacement);
    return bom ? QByteArray(kBom, 3) + body : body;
}

RenamePlan planMoves(const FileIndex& before, const std::vector<Move>& moves, const QStringList& candidates,
                     const NoteLoader& load)
{
    RenamePlan plan;
    plan.moves = moves;

    FileIndex after = before;
    QHash<QString, QString> moved; // folded old path -> new path
    for (const Move& m : moves) {
        after.rename(m.from, m.to);
        moved.insert(paths::foldKey(m.from), m.to);
    }
    auto mapped = [&](const QString& path) { return moved.value(paths::foldKey(path), path); };

    QSet<QString> seen;
    for (const QString& notePath : candidates) {
        const QString key = paths::foldKey(notePath);
        if (seen.contains(key)) continue;
        seen.insert(key);
        if (classifyPath(notePath) != FileKind::Note) continue;
        const std::optional<QByteArray> raw = load(notePath);
        if (!raw) {
            plan.warnings.append(u"Couldn't read \"%1\", so its links weren't checked."_s.arg(notePath));
            continue;
        }
        const bool bom = raw->startsWith(QByteArrayView(kBom, 3));
        const QByteArrayView body = bom ? QByteArrayView(*raw).sliced(3) : QByteArrayView(*raw);
        const NoteMetadata meta = parseNote(body);
        const QString sourceAfter = mapped(notePath);

        FileEditPlan fp;
        fp.path = notePath;
        fp.targetPath = sourceAfter;
        fp.originalRaw = *raw;
        QHash<qsizetype, QByteArray> byStart; // dedupe shared ranges (reference-style links)

        for (const LinkRef& link : meta.links) {
            if (link.linkpath.isEmpty()) continue; // [[#Heading]]: same note, unaffected
            const Resolution rb = before.resolve(link.linkpath, notePath);
            if (!rb.resolved()) continue;
            const QString expected = mapped(before.file(rb.file).path);
            const Resolution ra = after.resolve(link.linkpath, sourceAfter);
            if (ra.resolved() && after.file(ra.file).path == expected) continue;

            const bool keepExt = link.linkpath.endsWith(u".md"_s, Qt::CaseInsensitive);
            QString newPath = after.linkpathFor(expected, sourceAfter, inferStyle(link, rb), keepExt);
            if (link.linkpath.startsWith(u"./"_s) && !newPath.startsWith(u"../"_s)) newPath.prepend(u"./"_s);
            auto resolvesRight = [&](const QString& lp) {
                const Resolution r = after.resolve(lp, sourceAfter);
                return r.resolved() && after.file(r.file).path == expected;
            };
            if (!resolvesRight(newPath)) {
                newPath = after.linkpathFor(expected, sourceAfter, LinkStyle::Absolute, true);
                if (!resolvesRight(newPath)) {
                    plan.warnings.append(
                        u"%1, line %2: couldn't find a link path that still points at \"%3\"."_s.arg(notePath)
                            .arg(link.line + 1)
                            .arg(expected));
                    continue;
                }
            }
            if (link.targetStart < 0) {
                plan.warnings.append(
                    u"%1, line %2: the link to \"%3\" uses escapes, so it must be updated by hand."_s
                        .arg(notePath)
                        .arg(link.line + 1)
                        .arg(link.linkpath));
                continue;
            }
            QString written = newPath;
            if (link.kind == LinkRef::Kind::Markdown) {
                if (link.angleBrackets)
                    written = newPath;
                else
                    written = encodeMarkdownPath(newPath);
            }
            const QByteArray replacement = written.toUtf8();
            if (byStart.contains(link.targetStart)) {
                if (byStart.value(link.targetStart) != replacement)
                    plan.warnings.append(
                        u"%1, line %2: conflicting updates for one link definition."_s.arg(notePath).arg(
                            link.line + 1));
                continue;
            }
            byStart.insert(link.targetStart, replacement);
            const QString oldText =
                QString::fromUtf8(body.sliced(link.targetStart, link.targetEnd - link.targetStart));
            fp.edits.push_back({link.targetStart, link.targetEnd, replacement, link.line, oldText, written});
        }
        if (!fp.edits.empty()) {
            fp.newRaw = applyEdits(fp.originalRaw, fp.edits);
            plan.files.push_back(std::move(fp));
        }
    }
    return plan;
}

RenameResult executeRenamePlan(const QString& vaultRoot, const RenamePlan& plan,
                               const std::optional<Move>& folderMove)
{
    RenameResult result;
    auto abs = [&](const QString& p) { return paths::toAbsolute(vaultRoot, p); };
    auto readFile = [](const QString& path) -> std::optional<QByteArray> {
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly)) return std::nullopt;
        return f.readAll();
    };

    // Refuse to proceed if a file to rewrite changed since planning.
    std::vector<const FileEditPlan*> toWrite;
    for (const FileEditPlan& fp : plan.files) {
        const std::optional<QByteArray> now = readFile(abs(fp.path));
        if (!now || *now != fp.originalRaw) {
            result.failed.append(
                u"%1: it changed after the update was planned, so it wasn't touched."_s.arg(fp.path));
            continue;
        }
        toWrite.push_back(&fp);
    }

    if (folderMove) {
        const QString from = abs(folderMove->from);
        const QString to = abs(folderMove->to);
        if (QFileInfo::exists(to)) {
            result.moveError = u"\"%1\" already exists."_s.arg(folderMove->to);
            return result;
        }
        QDir().mkpath(QFileInfo(to).absolutePath());
        if (!QDir().rename(from, to)) {
            result.moveError = u"Couldn't rename \"%1\"."_s.arg(folderMove->from);
            return result;
        }
    } else {
        std::vector<const Move*> done;
        for (const Move& m : plan.moves) {
            const QString to = abs(m.to);
            const bool caseOnly = paths::foldKey(m.from) == paths::foldKey(m.to);
            if (!caseOnly && QFileInfo::exists(to)) {
                result.moveError = u"\"%1\" already exists."_s.arg(m.to);
            } else {
                QDir().mkpath(QFileInfo(to).absolutePath());
                bool ok = false;
                if (caseOnly) {
                    // Case-only renames need a detour on case-insensitive file systems.
                    const QString tmp = abs(m.from) + u".mn-rename-tmp"_s;
                    ok = QFile::rename(abs(m.from), tmp) && QFile::rename(tmp, to);
                } else {
                    ok = QFile::rename(abs(m.from), to);
                }
                if (!ok) result.moveError = u"Couldn't rename \"%1\"."_s.arg(m.from);
            }
            if (!result.moveError.isEmpty()) {
                // Roll back the moves already made so nothing is half-done.
                for (std::size_t k = done.size(); k-- > 0;)
                    QFile::rename(abs(done[k]->to), abs(done[k]->from));
                return result;
            }
            done.push_back(&m);
        }
    }
    result.moved = true;

    for (const FileEditPlan* fp : toWrite) {
        const WriteResult w = writeFileAtomic(abs(fp->targetPath), fp->newRaw);
        if (w.ok)
            result.updated.append(fp->targetPath);
        else
            result.failed.append(u"%1: %2"_s.arg(fp->targetPath, w.error));
    }
    return result;
}

} // namespace mn
