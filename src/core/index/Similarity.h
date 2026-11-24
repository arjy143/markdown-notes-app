#pragma once

#include <QHash>
#include <QString>
#include <QStringList>

#include <cstdint>
#include <vector>

// Heuristic text similarity (no models, no vector database):
//  - related notes: TF-IDF cosine similarity over words, with document
//    frequencies from the FTS5 vocabulary;
//  - near-duplicate notes: MinHash over 3-word shingles with LSH banding,
//    confirmed by exact Jaccard similarity.
namespace mn::sim {

// Lower-case, accents removed, runs of letters/digits (like FTS5 unicode61
// with remove_diacritics), so terms match the FTS vocabulary.
std::vector<QString> tokenize(QStringView text);

// The note's prose for comparison: frontmatter, URLs and link targets removed.
QString contentText(QStringView noteText);

bool isStopWord(QStringView term);
const QStringList& stopWords();

// Term frequencies of useful terms (no stop words, numbers or 1–2 letter words).
QHash<QString, int> termFrequencies(QStringView content);

// Sorted, unique 64-bit hashes of every 3-word shingle.
std::vector<std::uint64_t> shingles(QStringView content);

double jaccard(const std::vector<std::uint64_t>& a, const std::vector<std::uint64_t>& b);

struct DocShingles {
    QString path;
    std::vector<std::uint64_t> shingles;
};

struct DuplicatePair {
    QString a;
    QString b;
    double similarity = 0; // Jaccard of shingle sets, 0..1
};

// Near-duplicate pairs among `docs` with Jaccard >= threshold, most similar first.
// Documents with fewer than `minShingles` shingles are ignored.
std::vector<DuplicatePair> nearDuplicates(const std::vector<DocShingles>& docs, double threshold,
                                          std::size_t minShingles = 20);

} // namespace mn::sim
