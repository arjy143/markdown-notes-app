#include "FileTypes.h"

#include <QList>

using namespace Qt::StringLiterals;

namespace mn {

QString extensionOf(QStringView path)
{
    const qsizetype slash = path.lastIndexOf(u'/');
    const QStringView name = slash >= 0 ? path.mid(slash + 1) : path;
    const qsizetype dot = name.lastIndexOf(u'.');
    if (dot <= 0 || dot == name.size() - 1) return {};
    return name.mid(dot + 1).toString().toLower();
}

FileKind classifyPath(QStringView path)
{
    const QString ext = extensionOf(path);
    if (ext.isEmpty()) return FileKind::Other;
    if (ext == u"md") return FileKind::Note;
    static const QStringView images[] = {u"png", u"jpg", u"jpeg", u"gif", u"bmp", u"svg", u"webp", u"avif"};
    for (QStringView e : images) {
        if (ext == e) return FileKind::Image;
    }
    if (ext == u"pdf") return FileKind::Pdf;
    static const QStringView media[] = {u"mp3",  u"wav", u"m4a", u"ogg", u"flac",
                                        u"webm", u"mp4", u"mkv", u"mov"};
    for (QStringView e : media) {
        if (ext == e) return FileKind::Media;
    }
    return FileKind::Other;
}

bool isAttachmentKind(FileKind kind)
{
    return kind == FileKind::Image || kind == FileKind::Pdf || kind == FileKind::Media;
}

bool isHiddenName(QStringView name)
{
    return name.startsWith(u'.');
}

bool hasKnownExtension(QStringView path)
{
    return classifyPath(path) != FileKind::Other;
}

QString checkNewFileName(QStringView name)
{
    const QString trimmed = name.trimmed().toString();
    if (trimmed.isEmpty()) return u"The name can't be empty."_s;
    if (trimmed.startsWith(u'.')) return u"Names starting with '.' are hidden from the vault."_s;
    static const QString forbidden = u"*\"\\/<>:|?"_s;
    for (QChar c : trimmed) {
        if (forbidden.contains(c))
            return u"File names can't contain any of these characters: * \" \\ / < > : | ?"_s;
    }
    static const QString linkBreaking = u"#^[]|"_s;
    for (QChar c : trimmed) {
        if (linkBreaking.contains(c)) return u"File names can't contain # ^ [ ] or |."_s;
    }
    return {};
}

QString checkNewVaultPath(QStringView path)
{
    if (path.isEmpty()) return u"The path can't be empty."_s;
    if (path.startsWith(u'/') || path.contains(u'\\')) return u"The path must be inside the vault."_s;
    for (QStringView seg : path.split(u'/')) {
        const QString why = checkNewFileName(seg);
        if (!why.isEmpty()) return why;
    }
    return {};
}

} // namespace mn
