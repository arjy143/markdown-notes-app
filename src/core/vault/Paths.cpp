#include "Paths.h"

#include <QDir>
#include <QStringList>

namespace mn::paths {

QString folderOf(QStringView path)
{
    const qsizetype slash = path.lastIndexOf(u'/');
    return slash < 0 ? QString() : path.left(slash).toString();
}

QString fileName(QStringView path)
{
    const qsizetype slash = path.lastIndexOf(u'/');
    return path.mid(slash + 1).toString();
}

QString linkName(QStringView path)
{
    QString name = fileName(path);
    if (name.endsWith(u".md", Qt::CaseInsensitive)) name.chop(3);
    return name;
}

QString completeBaseName(QStringView path)
{
    QString name = fileName(path);
    const qsizetype dot = name.lastIndexOf(u'.');
    if (dot > 0) name.truncate(dot);
    return name;
}

QString join(QStringView folder, QStringView name)
{
    if (folder.isEmpty()) return name.toString();
    return folder.toString() + u'/' + name;
}

std::optional<QString> normalize(QStringView path)
{
    QStringList out;
    for (QStringView seg : path.split(u'/')) {
        if (seg.isEmpty() || seg == u".") continue;
        if (seg == u"..") {
            if (out.isEmpty()) return std::nullopt;
            out.removeLast();
            continue;
        }
        out.append(seg.toString());
    }
    return out.join(u'/');
}

QString relativePath(QStringView fromFolder, QStringView toPath)
{
    const QList<QStringView> from = fromFolder.isEmpty() ? QList<QStringView>() : fromFolder.split(u'/');
    const QList<QStringView> to = toPath.split(u'/');
    qsizetype common = 0;
    while (common < from.size() && common < to.size() - 1 &&
           from[common].compare(to[common], Qt::CaseInsensitive) == 0) {
        ++common;
    }
    QStringList parts;
    for (qsizetype i = common; i < from.size(); ++i)
        parts.append(QStringLiteral(".."));
    for (qsizetype i = common; i < to.size(); ++i)
        parts.append(to[i].toString());
    return parts.join(u'/');
}

QString foldKey(QStringView s)
{
    return s.toString().toCaseFolded();
}

QString toVaultPath(const QString& root, const QString& absolute)
{
    QString rel = QDir(root).relativeFilePath(absolute);
    rel.replace(u'\\', u'/');
    if (rel == u".") return {};
    return rel;
}

QString toAbsolute(const QString& root, QStringView vaultPath)
{
    if (vaultPath.isEmpty()) return root;
    return root + u'/' + vaultPath;
}

bool isInside(QStringView path, QStringView folder)
{
    if (folder.isEmpty()) return true;
    if (path.size() < folder.size()) return false;
    if (path.left(folder.size()).compare(folder, Qt::CaseInsensitive) != 0) return false;
    return path.size() == folder.size() || path[folder.size()] == u'/';
}

} // namespace mn::paths
