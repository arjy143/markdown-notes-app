#pragma once

#include "core/index/IndexTypes.h"

#include <QList>
#include <QPair>
#include <QString>
#include <QStringList>

#include <vector>

// What the assistant asks the language model, and how its replies are read.
// Pure functions: no network, so they are tested on their own.
namespace mn::ai {

struct ChatMessage {
    QString role; // "system", "user" or "assistant"
    QString content;
};

// --- frontmatter and tags ---

struct FrontmatterRequest {
    QString title;
    QString noteText;
    QStringList categoryProperties; // e.g. primary, secondary (vault settings: tagProperties)
    QStringList categories;         // allowed values for them (vault settings: categoryTags)
    QStringList vaultTags;          // existing tags, most used first
    QStringList examples;           // frontmatter blocks of similar notes, to copy their style
};

struct FrontmatterSuggestion {
    QList<QPair<QString, QString>> properties; // category properties, then summary
    QStringList tags;
    QString error; // the reply couldn't be read
};

std::vector<ChatMessage> frontmatterPrompt(const FrontmatterRequest& req);
FrontmatterSuggestion parseFrontmatterSuggestion(const QString& reply, const FrontmatterRequest& req);

// --- questions about the vault ---

std::vector<ChatMessage> askPrompt(const QString& question, const std::vector<Passage>& sources);
// Distinctive words of a question, for keyword retrieval ("what did I decide about coffee?" -> coffee,
// decide).
QStringList questionTerms(const QString& question);

// Removes a reasoning model's <think>…</think> part.
QString stripThinking(QString reply);

} // namespace mn::ai
