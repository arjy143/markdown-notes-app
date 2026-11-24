#include "NewLink.h"

#include "core/links/RenamePlanner.h"
#include "core/vault/FileTypes.h"
#include "core/vault/Paths.h"

using namespace Qt::StringLiterals;

namespace mn {

QString newFileLink(const VaultSettings& s, const QString& path, const QString& fromNote, bool nameInUse,
                    bool embed)
{
    QString lp;
    switch (s.newLinkFormat) {
    case LinkStyle::Absolute:
        lp = path;
        break;
    case LinkStyle::Relative:
        lp = paths::relativePath(paths::folderOf(fromNote), path);
        break;
    case LinkStyle::Shortest:
        lp = nameInUse ? path : paths::fileName(path);
        break;
    }
    const QString bang = embed ? u"!"_s : QString();
    if (s.useMarkdownLinks)
        return bang + u'[' + paths::completeBaseName(path) + u"]("_s + encodeMarkdownPath(lp) + u')';
    if (classifyPath(path) == FileKind::Note && lp.endsWith(u".md"_s)) lp.chop(3);
    return bang + u"[["_s + lp + u"]]"_s;
}

} // namespace mn
