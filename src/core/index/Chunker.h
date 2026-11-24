#pragma once

#include <QString>
#include <QStringView>

#include <vector>

// Splits a note into passages ("chunks") of roughly one topic each, so search
// and related passages can point at the part of a long or mixed note (a daily
// note with several topics, say) that is actually about something.
//
//  1. Units: paragraphs (separated by blank lines), top-level list items, and
//     code / math / comment blocks (never split). A heading starts a new
//     section and joins the unit after it. A paragraph longer than maxWords
//     that is written as separate lines is split into its lines.
//  2. Within a section, neighbouring units are merged while the chunk is still
//     very short (under minWords), or while they share vocabulary (cosine of their
//     word counts >= mergeSimilarity) and the result stays under maxWords.
//     A block joins the text before it if it fits, and a very short last chunk
//     joins the one before it. A short section under a heading (keepSectionWords)
//     stays whole; text before the first heading, like a daily note's, is split.
//
// Heuristic only (no models): it uses the same word statistics as related notes.
namespace mn {

struct Chunk {
    int startLine = 0; // 0-based, first line
    int endLine = 0;   // 0-based, last line (inclusive)
    QString context;   // heading path, e.g. "Work › Meeting notes" (empty before the first heading)
    QString text;      // the chunk's lines, joined with '\n' (no '\r')
};

struct ChunkOptions {
    int minWords = 15; // shorter chunks (a heading, a one-line intro) join the next unit
    int maxWords = 250;
    int keepSectionWords = 120; // a section under a heading this short stays one chunk
    double mergeSimilarity = 0.1;
};

// `firstLine`: the first line after the frontmatter (0 if there is none).
std::vector<Chunk> chunkNote(QStringView text, int firstLine = 0, const ChunkOptions& options = {});

// The chunk containing `line`, or the nearest one before it; nullptr if there is none.
const Chunk* chunkAtLine(const std::vector<Chunk>& chunks, int line);

} // namespace mn
