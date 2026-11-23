#include "Capture.h"

#include "core/templates/Templates.h"
#include "core/vault/FileTypes.h"
#include "core/vault/Paths.h"
#include "core/vault/TextFile.h"

#include <QDir>
#include <QFileInfo>

using namespace Qt::StringLiterals;

namespace mn {

QString captureTargetPath(const VaultSettings& s, const QDateTime& now)
{
    if (s.captureToDailyNote) {
        const QString name =
            formatMoment(now, s.dailyNoteFormat.isEmpty() ? u"YYYY-MM-DD"_s : s.dailyNoteFormat);
        return paths::normalize(paths::join(s.dailyNotesFolder, QString(name + u".md"_s)))
            .value_or(QString());
    }
    QString p = s.captureNote.trimmed();
    while (p.startsWith(u'/'))
        p.remove(0, 1);
    if (p.isEmpty()) p = u"Inbox.md"_s;
    if (!p.endsWith(u".md"_s, Qt::CaseInsensitive)) p += u".md"_s;
    return paths::normalize(p).value_or(QString());
}

QString formatCapture(const VaultSettings& s, const QString& textIn, const QDateTime& now)
{
    QString text = textIn.trimmed();
    text.replace(u"\r\n"_s, u"\n"_s);
    text.replace(u'\r', u'\n');
    TemplateContext ctx;
    ctx.now = now;
    ctx.dateFormat = s.dateFormat;
    ctx.timeFormat = s.timeFormat;
    QString line = substituteTemplate(s.captureFormat.isEmpty() ? u"- {{text}}"_s : s.captureFormat, ctx);
    // Continuation lines are indented so they stay inside a list item.
    const bool listItem = line.trimmed().startsWith(u"- "_s) || line.trimmed().startsWith(u"* "_s);
    if (listItem) text.replace(u'\n', u"\n  "_s);
    if (line.contains(u"{{text}}"_s))
        line.replace(u"{{text}}"_s, text);
    else
        line += u' ' + text;
    return line;
}

std::optional<QByteArray> appendToNote(const QByteArray& raw, const QString& line)
{
    const LoadedText t = decodeText(raw);
    if (!t.editable()) return std::nullopt;
    const QByteArray eol = t.format.eol == LineEnding::CRLF ? QByteArray("\r\n") : QByteArray("\n");
    QByteArray out = raw;
    const bool onlyBom = raw.size() == 3 && t.format.bom;
    if (!raw.isEmpty() && !onlyBom && !raw.endsWith('\n') && !raw.endsWith('\r')) out += eol;
    QByteArray add = line.toUtf8();
    add.replace("\n", eol);
    out += add;
    out += eol;
    return out;
}

CaptureResult appendCapture(const QString& vaultRoot, const VaultSettings& s, const QString& text,
                            const QDateTime& now)
{
    CaptureResult r;
    if (text.trimmed().isEmpty()) {
        r.error = u"Nothing to capture."_s;
        return r;
    }
    r.path = captureTargetPath(s, now);
    const QString why = checkNewVaultPath(r.path);
    if (r.path.isEmpty() || !why.isEmpty()) {
        r.error = u"The capture note \"%1\" isn't allowed: %2"_s.arg(r.path, why);
        return r;
    }
    const QString abs = paths::toAbsolute(vaultRoot, r.path);
    QByteArray raw;
    if (QFileInfo::exists(abs)) {
        const LoadedText existing = loadTextFile(abs);
        if (!existing.editable()) {
            r.error = u"Couldn't add to \"%1\": %2"_s.arg(r.path, existing.error);
            return r;
        }
        raw = existing.raw;
    } else {
        r.created = true;
        if (s.captureToDailyNote && !s.dailyNoteTemplate.isEmpty()) {
            const LoadedText tpl = loadTextFile(paths::toAbsolute(vaultRoot, s.dailyNoteTemplate));
            if (tpl.editable()) {
                TemplateContext ctx;
                ctx.title = paths::linkName(r.path);
                ctx.now = now;
                ctx.dateFormat = s.dateFormat;
                ctx.timeFormat = s.timeFormat;
                raw = substituteTemplate(tpl.text, ctx).toUtf8();
            }
        }
        if (!QDir().mkpath(QFileInfo(abs).absolutePath())) {
            r.error = u"Couldn't create the folder for \"%1\"."_s.arg(r.path);
            return r;
        }
    }
    const std::optional<QByteArray> out = appendToNote(raw, formatCapture(s, text, now));
    if (!out) {
        r.error = u"\"%1\" isn't a text note."_s.arg(r.path);
        return r;
    }
    const WriteResult w = writeFileAtomic(abs, *out);
    if (!w.ok) {
        r.error = u"Couldn't save \"%1\": %2"_s.arg(r.path, w.error);
        return r;
    }
    r.ok = true;
    return r;
}

} // namespace mn
