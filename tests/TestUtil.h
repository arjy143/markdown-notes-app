#pragma once

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QString>
#include <QTemporaryDir>
#include <QTest>

// Helpers shared by the tests. Tests that write always work on a copy of a
// fixture inside a QTemporaryDir, never on the fixture itself or a real vault.
namespace mn::test {

inline QString fixture(const QString& name)
{
    return QStringLiteral(MN_FIXTURES_DIR) + u'/' + name;
}

// Recursively copies `src` into `dst` (including hidden folders).
inline bool copyTree(const QString& src, const QString& dst)
{
    QDir().mkpath(dst);
    QDirIterator it(src, QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden,
                    QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString path = it.next();
        const QString rel = QDir(src).relativeFilePath(path);
        const QString target = dst + u'/' + rel;
        if (it.fileInfo().isDir()) {
            if (!QDir().mkpath(target)) return false;
        } else {
            QDir().mkpath(QFileInfo(target).absolutePath());
            if (!QFile::copy(path, target)) return false;
            QFile::setPermissions(target,
                                  QFile::ReadOwner | QFile::WriteOwner | QFile::ReadGroup | QFile::ReadOther);
        }
    }
    return true;
}

// A temporary copy of a fixture vault.
class VaultCopy {
public:
    explicit VaultCopy(const QString& fixtureName)
    {
        ok_ = dir_.isValid() && copyTree(fixture(fixtureName), dir_.path() + QStringLiteral("/vault"));
    }
    bool ok() const { return ok_; }
    QString root() const { return dir_.path() + QStringLiteral("/vault"); }
    QString path(const QString& rel) const { return root() + u'/' + rel; }
    QString cacheDir() const { return dir_.path() + QStringLiteral("/cache"); }

private:
    QTemporaryDir dir_;
    bool ok_ = false;
};

inline QByteArray readBytes(const QString& path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return {};
    return f.readAll();
}

inline bool writeBytes(const QString& path, const QByteArray& data)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
    return f.write(data) == data.size();
}

// Snapshot of every file's bytes under a root, for "nothing was modified" checks.
inline QHash<QString, QByteArray> snapshotTree(const QString& root)
{
    QHash<QString, QByteArray> out;
    QDirIterator it(root, QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString p = it.next();
        out.insert(QDir(root).relativeFilePath(p), readBytes(p));
    }
    return out;
}

} // namespace mn::test
