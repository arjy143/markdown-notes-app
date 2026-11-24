#include "Similarity.h"

#include "core/parse/Frontmatter.h"

#include <QRegularExpression>
#include <QSet>

#include <algorithm>
#include <array>
#include <limits>

using namespace Qt::StringLiterals;

namespace mn::sim {

namespace {

QChar foldChar(QChar c)
{
    if (c.decompositionTag() == QChar::Canonical) {
        const QString d = c.decomposition();
        if (!d.isEmpty()) c = d[0];
    }
    return c.toLower();
}

std::uint64_t fnv1a(QStringView s)
{
    std::uint64_t h = 1469598103934665603ULL;
    for (QChar c : s) {
        h ^= c.unicode();
        h *= 1099511628211ULL;
    }
    return h;
}

std::uint64_t splitmix(std::uint64_t x)
{
    x += 0x9E3779B97F4A7C15ULL;
    x = (x ^ (x >> 30U)) * 0xBF58476D1CE4E5B9ULL;
    x = (x ^ (x >> 27U)) * 0x94D049BB133111EBULL;
    return x ^ (x >> 31U);
}

constexpr int kHashes = 63; // 21 bands of 3 rows: ~94% recall at Jaccard 0.5
constexpr int kRows = 3;
constexpr int kBands = kHashes / kRows;
constexpr std::size_t kMaxBucket = 60; // guards against quadratic blow-up on boilerplate

} // namespace

std::vector<QString> tokenize(QStringView text)
{
    std::vector<QString> out;
    QString cur;
    for (qsizetype i = 0; i < text.size(); ++i) {
        const QChar c = text[i];
        if (c.isLetterOrNumber() || c.isMark()) {
            if (!c.isMark()) cur += foldChar(c);
        } else if (!cur.isEmpty()) {
            out.push_back(cur);
            cur.clear();
        }
    }
    if (!cur.isEmpty()) out.push_back(cur);
    return out;
}

QString contentText(QStringView noteText)
{
    QString text = noteText.toString();
    const Frontmatter fm = parseFrontmatter(text.toUtf8());
    if (fm.present) text = QString::fromUtf8(text.toUtf8().mid(fm.bodyOffset));
    static const QRegularExpression url(uR"((https?://|www\.)\S+)"_s);
    static const QRegularExpression mdTarget(uR"(\]\([^)\n]*\))"_s); // keep link text, drop the target
    text.replace(mdTarget, u"]"_s);
    text.remove(url);
    return text;
}

const QStringList& stopWords()
{
    static const QStringList list = [] {
        const char* words =
            "a about above after again against all also am an and any are as at be because been before being "
            "below "
            "between both but by can could did do does doing down during each few for from further had has "
            "have "
            "having he her here hers herself him himself his how however i if in into is it its itself just "
            "let "
            "like may me might more most must my myself no nor not now of off on once one only or other our "
            "ours "
            "ourselves out over own same she should so some such than that the their theirs them themselves "
            "then "
            "there these they this those through to too under until up upon us use used using very via was "
            "we "
            "were what when where which while who whom why will with within without would yes yet you your "
            "yours "
            "yourself yourselves get got make made see also etc e g ie eg new two first just way well";
        return QString::fromLatin1(words).split(u' ', Qt::SkipEmptyParts);
    }();
    return list;
}

bool isStopWord(QStringView w)
{
    static const QSet<QString> stop(stopWords().cbegin(), stopWords().cend());
    return stop.contains(w.toString());
}

QHash<QString, int> termFrequencies(QStringView content)
{
    QHash<QString, int> tf;
    for (const QString& t : tokenize(content)) {
        if (t.size() < 3 || isStopWord(t)) continue;
        bool digits = true;
        for (QChar c : t)
            digits = digits && c.isDigit();
        if (digits) continue;
        ++tf[t];
    }
    return tf;
}

std::vector<std::uint64_t> shingles(QStringView content)
{
    const std::vector<QString> words = tokenize(content);
    std::vector<std::uint64_t> out;
    if (words.size() < 3) return out;
    out.reserve(words.size());
    for (std::size_t i = 0; i + 2 < words.size(); ++i) {
        const std::uint64_t h =
            fnv1a(words[i]) ^ (fnv1a(words[i + 1]) * 31ULL) ^ (fnv1a(words[i + 2]) * 1009ULL);
        out.push_back(splitmix(h));
    }
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

double jaccard(const std::vector<std::uint64_t>& a, const std::vector<std::uint64_t>& b)
{
    if (a.empty() || b.empty()) return 0;
    std::size_t i = 0;
    std::size_t j = 0;
    std::size_t both = 0;
    while (i < a.size() && j < b.size()) {
        if (a[i] == b[j]) {
            ++both;
            ++i;
            ++j;
        } else if (a[i] < b[j]) {
            ++i;
        } else {
            ++j;
        }
    }
    return static_cast<double>(both) / static_cast<double>(a.size() + b.size() - both);
}

std::vector<DuplicatePair> nearDuplicates(const std::vector<DocShingles>& docs, double threshold,
                                          std::size_t minShingles)
{
    std::array<std::uint64_t, kHashes> seeds{};
    for (int k = 0; k < kHashes; ++k)
        seeds[static_cast<std::size_t>(k)] = splitmix(static_cast<std::uint64_t>(k) + 1);
    std::vector<int> ids;
    std::vector<std::array<std::uint64_t, kHashes>> sigs;
    for (int d = 0; d < static_cast<int>(docs.size()); ++d) {
        const auto& sh = docs[static_cast<std::size_t>(d)].shingles;
        if (sh.size() < minShingles) continue;
        std::array<std::uint64_t, kHashes> sig;
        sig.fill(std::numeric_limits<std::uint64_t>::max());
        for (std::uint64_t x : sh) {
            for (int k = 0; k < kHashes; ++k) {
                // x is already well mixed; xor with a per-hash seed and an odd multiply
                // are both bijections, so each k is a different permutation.
                const std::uint64_t h = (x ^ seeds[static_cast<std::size_t>(k)]) * 0x9E3779B97F4A7C15ULL;
                if (h < sig[static_cast<std::size_t>(k)]) sig[static_cast<std::size_t>(k)] = h;
            }
        }
        ids.push_back(d);
        sigs.push_back(sig);
    }
    QSet<quint64> seen;
    std::vector<DuplicatePair> out;
    for (int band = 0; band < kBands; ++band) {
        QHash<quint64, std::vector<int>> buckets;
        for (int i = 0; i < static_cast<int>(sigs.size()); ++i) {
            std::uint64_t key = static_cast<std::uint64_t>(band);
            for (int r = 0; r < kRows; ++r)
                key = splitmix(key ^
                               sigs[static_cast<std::size_t>(i)]
                                   [static_cast<std::size_t>(band) * kRows + static_cast<std::size_t>(r)]);
            std::vector<int>& b = buckets[key];
            if (b.size() < kMaxBucket) b.push_back(i);
        }
        for (auto it = buckets.cbegin(); it != buckets.cend(); ++it) {
            const std::vector<int>& b = it.value();
            for (std::size_t x = 0; x < b.size(); ++x) {
                for (std::size_t y = x + 1; y < b.size(); ++y) {
                    const int p = std::min(b[x], b[y]);
                    const int q = std::max(b[x], b[y]);
                    const quint64 pairKey = (static_cast<quint64>(p) << 32U) | static_cast<quint32>(q);
                    if (seen.contains(pairKey)) continue;
                    seen.insert(pairKey);
                    const DocShingles& da = docs[static_cast<std::size_t>(ids[static_cast<std::size_t>(p)])];
                    const DocShingles& db = docs[static_cast<std::size_t>(ids[static_cast<std::size_t>(q)])];
                    const double j = jaccard(da.shingles, db.shingles);
                    if (j >= threshold) out.push_back({da.path, db.path, j});
                }
            }
        }
    }
    std::sort(out.begin(), out.end(), [](const DuplicatePair& a, const DuplicatePair& b) {
        if (a.similarity != b.similarity) return a.similarity > b.similarity;
        return a.a < b.a;
    });
    return out;
}

} // namespace mn::sim
