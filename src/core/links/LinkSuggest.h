#pragma once

#include "FileIndex.h"

#include <QHash>
#include <QSet>
#include <QString>
#include <QStringList>

#include <vector>

// "Link suggestions as you type": finds note names (and aliases) mentioned in a
// line of text that isn't already a link, code, a URL, a tag or a comment.
namespace mn {

// Case- and accent-insensitive phrase key: words joined by single spaces.
QString phraseKey(QStringView text);

struct NameTable {
    QHash<QString, QString> pathByKey; // phrase key -> note path
    int maxWords = 0;
};

// Note names and aliases worth suggesting: multi-word names, or single words of
// at least 4 letters that aren't common English words or numbers.
NameTable buildNameTable(const std::vector<VaultFile>& files, const QHash<QString, QStringList>& aliases);

struct LinkSuggestion {
    int start = 0; // in the line (UTF-16 index)
    int length = 0;
    QString path; // note to link to
    QString key;  // phrase key (for dismissing)
};

// Longest matches first, one suggestion per target note, left to right.
std::vector<LinkSuggestion> findLinkSuggestions(QStringView line, const NameTable& names,
                                                QStringView selfPath, const QSet<QString>& dismissedKeys);

} // namespace mn
