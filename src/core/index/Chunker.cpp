#include "Chunker.h"

#include "Similarity.h"

#include <QStringList>

#include <algorithm>
#include <cmath>
#include <cstdint>

using namespace Qt::StringLiterals;

namespace mn {

namespace {

// Word counts as (word hash, count), sorted by hash: no allocation per word, and
// merging or comparing two units is a linear walk.
using Terms = std::vector<std::pair<std::uint64_t, int>>;

struct Unit {
    int start = 0;
    int end = 0;
    int section = 0;
    int words = 0;
    Terms tf;
    bool heading = false;
    bool content = false; // has more than headings
    bool block = false;   // contains a code, math or comment block: never split
};

int leadingSpaces(QStringView l)
{
    int n = 0;
    for (QChar c : l) {
        if (c == u' ')
            ++n;
        else if (c == u'\t')
            n += 4;
        else
            break;
    }
    return n;
}

// ATX heading ("## Text"): up to 3 spaces, 1–6 '#', then a space or the end of the line.
bool headingText(QStringView l, int* level, QString* text)
{
    if (leadingSpaces(l) > 3) return false;
    QStringView s = l.trimmed();
    int n = 0;
    while (n < s.size() && s[n] == u'#')
        ++n;
    if (n == 0 || n > 6 || (n < s.size() && s[n] != u' ' && s[n] != u'\t')) return false;
    s = s.mid(n).trimmed();
    // An optional closing run of '#' (preceded by a space).
    qsizetype e = s.size();
    while (e > 0 && s[e - 1] == u'#')
        --e;
    if (e < s.size() && (e == 0 || s[e - 1] == u' ')) s = s.left(e).trimmed();
    *level = n;
    *text = s.toString();
    return true;
}

bool isTopListItem(QStringView l)
{
    if (l.isEmpty() || l[0].isSpace()) return false;
    if ((l[0] == u'-' || l[0] == u'*' || l[0] == u'+') && l.size() > 1 && (l[1] == u' ' || l[1] == u'\t'))
        return true;
    qsizetype i = 0;
    while (i < l.size() && i < 9 && l[i].isDigit())
        ++i;
    return i > 0 && i + 1 < l.size() && (l[i] == u'.' || l[i] == u')') &&
           (l[i + 1] == u' ' || l[i + 1] == u'\t');
}

// Opening code fence: up to 3 spaces, then 3+ backticks or tildes.
bool fenceOpen(QStringView l, QChar* ch, int* len)
{
    if (leadingSpaces(l) > 3) return false;
    const QStringView s = l.trimmed();
    if (s.size() < 3 || (s[0] != u'`' && s[0] != u'~')) return false;
    int n = 0;
    while (n < s.size() && s[n] == s[0])
        ++n;
    if (n < 3) return false;
    *ch = s[0];
    *len = n;
    return true;
}

bool fenceCloses(QStringView l, QChar ch, int len)
{
    if (leadingSpaces(l) > 3) return false;
    const QStringView s = l.trimmed();
    int n = 0;
    while (n < s.size() && s[n] == ch)
        ++n;
    return n >= len && n == s.size();
}

bool oddCount(QStringView l, QStringView token)
{
    int n = 0;
    for (qsizetype p = l.indexOf(token); p >= 0; p = l.indexOf(token, p + token.size()))
        ++n;
    return (n % 2) == 1;
}

std::uint64_t mix(std::uint64_t h, char16_t c)
{
    return (h ^ c) * 1099511628211ULL;
}

constexpr std::uint64_t kSeed = 1469598103934665603ULL;

// Hashes of the stop words, folded the same way as text (sorted, for binary search).
const std::vector<std::uint64_t>& stopHashes()
{
    static const std::vector<std::uint64_t> hashes = [] {
        std::vector<std::uint64_t> h;
        for (const QString& w : sim::stopWords()) {
            std::uint64_t x = kSeed;
            for (QChar c : w)
                x = mix(x, c.unicode());
            h.push_back(x);
        }
        std::sort(h.begin(), h.end());
        return h;
    }();
    return hashes;
}

void mergeTerms(Terms& into, const Terms& add)
{
    if (into.empty()) {
        into = add;
        return;
    }
    Terms merged;
    merged.reserve(into.size() + add.size());
    std::size_t a = 0;
    std::size_t b = 0;
    while (a < into.size() || b < add.size()) {
        if (b == add.size() || (a < into.size() && into[a].first < add[b].first)) {
            merged.push_back(into[a++]);
        } else if (a == into.size() || add[b].first < into[a].first) {
            merged.push_back(add[b++]);
        } else {
            merged.emplace_back(into[a].first, into[a].second + add[b].second);
            ++a;
            ++b;
        }
    }
    into = std::move(merged);
}

// Counts words like sim::tokenize (letters and digits, lower case, accents removed) and
// collects the useful ones like sim::termFrequencies (3+ letters, not a stop word or a number).
void addWords(Unit& u, QStringView text, std::vector<std::uint64_t>& scratch)
{
    const std::vector<std::uint64_t>& stop = stopHashes();
    scratch.clear();
    std::uint64_t h = kSeed;
    int len = 0;
    bool digits = true;
    auto finish = [&] {
        if (len == 0) return;
        ++u.words;
        if (len >= 3 && !digits && !std::binary_search(stop.begin(), stop.end(), h)) scratch.push_back(h);
        h = kSeed;
        len = 0;
        digits = true;
    };
    for (QChar c : text) {
        const char16_t ch = c.unicode();
        if (ch < 128) {
            if ((ch >= u'a' && ch <= u'z') || (ch >= u'0' && ch <= u'9')) {
                h = mix(h, ch);
            } else if (ch >= u'A' && ch <= u'Z') {
                h = mix(h, static_cast<char16_t>(ch + 32));
            } else {
                finish();
                continue;
            }
            digits = digits && ch >= u'0' && ch <= u'9';
            ++len;
        } else if (c.isMark()) {
            continue; // combining accents are dropped, as in sim::tokenize
        } else if (c.isLetterOrNumber()) {
            QChar f = c;
            if (f.decompositionTag() == QChar::Canonical) {
                const QString d = f.decomposition();
                if (!d.isEmpty()) f = d[0];
            }
            h = mix(h, f.toLower().unicode());
            digits = digits && c.isDigit();
            ++len;
        } else {
            finish();
        }
    }
    finish();
    if (scratch.empty()) return;
    std::sort(scratch.begin(), scratch.end());
    Terms add;
    for (std::size_t i = 0; i < scratch.size();) {
        std::size_t j = i;
        while (j < scratch.size() && scratch[j] == scratch[i])
            ++j;
        add.emplace_back(scratch[i], static_cast<int>(j - i));
        i = j;
    }
    mergeTerms(u.tf, add);
}

double norm(const Terms& t)
{
    double s = 0;
    for (const auto& [h, n] : t)
        s += static_cast<double>(n) * n;
    return std::sqrt(s);
}

double cosine(const Terms& a, const Terms& b)
{
    if (a.empty() || b.empty()) return 0;
    double dot = 0;
    std::size_t i = 0;
    std::size_t j = 0;
    while (i < a.size() && j < b.size()) {
        if (a[i].first < b[j].first) {
            ++i;
        } else if (b[j].first < a[i].first) {
            ++j;
        } else {
            dot += static_cast<double>(a[i].second) * b[j].second;
            ++i;
            ++j;
        }
    }
    return dot <= 0 ? 0 : dot / (norm(a) * norm(b));
}

void absorb(Unit& into, const Unit& u)
{
    into.end = u.end;
    into.heading = into.heading || u.heading;
    into.content = into.content || u.content;
    into.words += u.words;
    mergeTerms(into.tf, u.tf);
}

} // namespace

std::vector<Chunk> chunkNote(QStringView text, int firstLine, const ChunkOptions& options)
{
    std::vector<QStringView> lines;
    for (qsizetype pos = 0;;) {
        const qsizetype nl = text.indexOf(u'\n', pos);
        QStringView l = text.mid(pos, nl < 0 ? text.size() - pos : nl - pos);
        if (l.endsWith(u'\r')) l.chop(1);
        lines.push_back(l);
        if (nl < 0) break;
        pos = nl + 1;
    }
    const int n = static_cast<int>(lines.size());

    std::vector<std::uint64_t> scratch;

    // 1. Units.
    std::vector<Unit> units;
    QStringList contexts{QString()}; // per section
    std::vector<std::pair<int, QString>> headings;
    Unit cur;
    bool open = false;
    bool curIsList = false;
    QChar fenceChar;
    int fenceLen = 0;
    bool inMath = false;
    bool inComment = false;
    auto close = [&] {
        cur.content = !cur.heading;
        if (open) units.push_back(std::move(cur));
        cur = Unit();
        open = false;
    };
    for (int i = std::max(0, firstLine); i < n; ++i) {
        const QStringView l = lines[static_cast<std::size_t>(i)];
        if (fenceLen > 0 || inMath || inComment) {
            cur.end = i;
            if (fenceLen > 0) {
                if (fenceCloses(l, fenceChar, fenceLen)) fenceLen = 0;
            } else {
                if (inMath && oddCount(l, u"$$")) inMath = false;
                if (inComment && oddCount(l, u"%%")) inComment = false;
            }
            continue;
        }
        if (l.trimmed().isEmpty()) {
            close();
            continue;
        }
        int level = 0;
        QString htext;
        if (headingText(l, &level, &htext)) {
            close();
            while (!headings.empty() && headings.back().first >= level)
                headings.pop_back();
            headings.emplace_back(level, htext);
            QStringList path;
            for (const auto& h : headings)
                path.append(h.second);
            contexts.append(path.join(u" › "_s));
            cur.start = cur.end = i;
            cur.section = static_cast<int>(contexts.size()) - 1;
            cur.heading = true;
            addWords(cur, htext, scratch);
            open = true;
            close();
            continue;
        }
        const bool listItem = isTopListItem(l);
        if (open && listItem && curIsList) close();
        if (!open) {
            cur.start = i;
            cur.section = static_cast<int>(contexts.size()) - 1;
            curIsList = listItem;
            open = true;
        }
        cur.end = i;
        if (fenceOpen(l, &fenceChar, &fenceLen)) {
            cur.block = true;
        } else {
            if (oddCount(l, u"$$")) inMath = cur.block = true;
            if (oddCount(l, u"%%")) inComment = cur.block = true;
        }
    }
    close();

    // Word counts; long paragraphs written as separate lines become one unit per line.
    std::vector<Unit> expanded;
    expanded.reserve(units.size());
    auto joined = [&](int a, int b) {
        QString s;
        for (int i = a; i <= b; ++i) {
            if (i > a) s += u'\n';
            s += lines[static_cast<std::size_t>(i)];
        }
        return s;
    };
    for (Unit& u : units) {
        if (u.heading) {
            expanded.push_back(std::move(u));
            continue;
        }
        for (int i = u.start; i <= u.end; ++i) // words never span lines
            addWords(u, lines[static_cast<std::size_t>(i)], scratch);
        if (u.block || u.words <= options.maxWords || u.start == u.end) {
            expanded.push_back(std::move(u));
            continue;
        }
        for (int i = u.start; i <= u.end; ++i) {
            Unit line;
            line.start = line.end = i;
            line.section = u.section;
            line.content = true;
            addWords(line, lines[static_cast<std::size_t>(i)], scratch);
            if (line.words > 0) expanded.push_back(std::move(line));
        }
    }

    // 2. Merge neighbours within each section. A short section under a heading is
    // one topic already (often a list of short items that share few words): kept whole.
    std::vector<int> sectionWords(static_cast<std::size_t>(contexts.size()), 0);
    for (const Unit& u : expanded)
        sectionWords[static_cast<std::size_t>(u.section)] += u.words;
    std::vector<Unit> merged;
    for (const Unit& u : expanded) {
        if (merged.empty() || merged.back().section != u.section) {
            merged.push_back(u);
            continue;
        }
        Unit& acc = merged.back();
        const bool tiny =
            acc.words < options.minWords ||
            (u.section > 0 && sectionWords[static_cast<std::size_t>(u.section)] <= options.keepSectionWords);
        const bool fits = acc.words + u.words <= options.maxWords;
        // A code, math or comment block belongs with the text before it.
        if (tiny || (fits && (u.block || cosine(acc.tf, u.tf) >= options.mergeSimilarity))) {
            absorb(acc, u);
            continue;
        }
        merged.push_back(u);
    }
    // A short chunk always absorbs the next unit, so only a section's last chunk can
    // still be very short: it joins the chunk before it.
    std::vector<Unit> out;
    out.reserve(merged.size());
    for (Unit& u : merged) {
        if (!out.empty() && out.back().section == u.section && !u.heading && u.words < options.minWords / 2) {
            absorb(out.back(), u);
            continue;
        }
        out.push_back(std::move(u));
    }

    std::vector<Chunk> chunks;
    chunks.reserve(out.size());
    for (const Unit& u : out) {
        // A heading straight above a subheading: the subsections carry it in their context.
        if (u.words == 0 || !u.content) continue;
        chunks.push_back({u.start, u.end, contexts.value(u.section), joined(u.start, u.end)});
    }
    return chunks;
}

const Chunk* chunkAtLine(const std::vector<Chunk>& chunks, int line)
{
    const Chunk* best = nullptr;
    for (const Chunk& c : chunks) {
        if (c.startLine > line) break;
        best = &c;
    }
    return best;
}

} // namespace mn
