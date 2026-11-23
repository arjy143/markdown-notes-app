#include "LinkSuggest.h"

#include "core/index/Similarity.h"
#include "core/vault/Paths.h"

#include <QRegularExpression>

using namespace Qt::StringLiterals;

namespace mn {

namespace {

struct Word {
    int start;
    int end;
    QString folded;
};

QChar fold(QChar c)
{
    if (c.decompositionTag() == QChar::Canonical) {
        const QString d = c.decomposition();
        if (!d.isEmpty()) c = d[0];
    }
    return c.toLower();
}

std::vector<Word> words(QStringView text)
{
    std::vector<Word> out;
    int i = 0;
    const int n = static_cast<int>(text.size());
    while (i < n) {
        while (i < n && !text[i].isLetterOrNumber())
            ++i;
        if (i >= n) break;
        const int s = i;
        QString w;
        while (i < n && (text[i].isLetterOrNumber() || text[i].isMark())) {
            if (!text[i].isMark()) w += fold(text[i]);
            ++i;
        }
        out.push_back({s, i, w});
    }
    return out;
}

bool worthSuggesting(const QString& key)
{
    if (key.isEmpty()) return false;
    const qsizetype spaces = key.count(u' ');
    if (spaces >= 8) return false;
    if (spaces > 0) return true;
    if (key.size() < 4 || sim::isStopWord(key)) return false;
    for (QChar c : key) {
        if (!c.isDigit()) return true;
    }
    return false; // a number
}

} // namespace

QString phraseKey(QStringView text)
{
    QStringList parts;
    for (const Word& w : words(text))
        parts.append(w.folded);
    return parts.join(u' ');
}

NameTable buildNameTable(const std::vector<VaultFile>& files, const QHash<QString, QStringList>& aliases)
{
    NameTable t;
    auto add = [&](const QString& name, const QString& path) {
        const QString key = phraseKey(name);
        if (!worthSuggesting(key)) return;
        // An ambiguous phrase (two notes with the same name) isn't suggested.
        const auto it = t.pathByKey.constFind(key);
        if (it != t.pathByKey.constEnd() && *it != path) {
            t.pathByKey.insert(key, QString());
            return;
        }
        t.pathByKey.insert(key, path);
        t.maxWords = std::max(t.maxWords, static_cast<int>(key.count(u' ')) + 1);
    };
    for (const VaultFile& f : files) {
        if (f.kind != FileKind::Note) continue;
        add(paths::linkName(f.path), f.path);
        for (const QString& a : aliases.value(f.path))
            add(a, f.path);
    }
    return t;
}

std::vector<LinkSuggestion> findLinkSuggestions(QStringView line, const NameTable& names,
                                                QStringView selfPath, const QSet<QString>& dismissedKeys)
{
    std::vector<LinkSuggestion> out;
    if (names.pathByKey.isEmpty()) return out;
    // Spans that must not be suggested inside.
    std::vector<std::pair<int, int>> excluded;
    static const QRegularExpression skip(
        uR"((!?\[\[[^\]]*\]\])|(!?\[[^\]\n]*\]\([^)\n]*\))|(`[^`]*`)|((https?://|www\.)\S+)|((?:^|(?<=\s))#[^\s#]+)|(%%.*?(%%|$))|(\$[^$\s][^$]*\$))"_s);
    for (auto it = skip.globalMatchView(line); it.hasNext();) {
        const auto m = it.next();
        excluded.emplace_back(static_cast<int>(m.capturedStart()), static_cast<int>(m.capturedEnd()));
    }
    auto isExcluded = [&](int s, int e) {
        for (const auto& [a, b] : excluded) {
            if (s < b && e > a) return true;
        }
        return false;
    };
    const std::vector<Word> ws = words(line);
    QSet<QString> suggestedPaths;
    std::size_t i = 0;
    while (i < ws.size()) {
        bool matched = false;
        const std::size_t maxN =
            std::min<std::size_t>(static_cast<std::size_t>(names.maxWords), ws.size() - i);
        for (std::size_t n = maxN; n >= 1; --n) {
            const int s = ws[i].start;
            const int e = ws[i + n - 1].end;
            if (isExcluded(s, e)) continue;
            QString key = ws[i].folded;
            for (std::size_t k = 1; k < n; ++k)
                key += u' ' + ws[i + k].folded;
            const auto it = names.pathByKey.constFind(key);
            if (it == names.pathByKey.constEnd() || it->isEmpty()) continue;
            if (*it == selfPath || dismissedKeys.contains(key) || suggestedPaths.contains(*it)) continue;
            out.push_back({s, e - s, *it, key});
            suggestedPaths.insert(*it);
            i += n;
            matched = true;
            break;
        }
        if (!matched) ++i;
    }
    return out;
}

} // namespace mn
