#pragma once

#include "AiClient.h"
#include "Prompts.h"
#include "core/index/IndexTypes.h"

#include <QObject>

#include <functional>
#include <vector>

namespace mn {
class VaultService;
}

// The vault assistant: semantic index, search, questions and frontmatter
// suggestions, on top of the vault index and a local language model. It only
// ever reads notes; suggestions are applied (or not) by the user in the editor.
namespace mn::ai {

class Assistant : public QObject {
    Q_OBJECT
public:
    explicit Assistant(VaultService* vault, QObject* parent = nullptr);

    void setConfig(const AiConfig& config);
    const AiConfig& config() const { return client_.config(); }
    AiClient& client() { return client_; }

    // Embeds passages that have no vector yet, in batches, in the background. Runs
    // after every index update while the config asks for it.
    void updateIndex();
    void stopIndexing();
    bool indexing() const { return indexing_; }
    int indexedPassages() const { return done_; }

    // Keyword and semantic search over passages (keyword only without an embedding model).
    void search(const QString& query, std::function<void(std::vector<Passage>, QString error)> cb);

    struct Answer {
        QString text; // Markdown, citing sources as [1], [2] …
        std::vector<Passage> sources;
        QString error;
    };
    void ask(const QString& question, std::function<void(Answer)> cb);

    void suggestFrontmatter(const QString& path, const QString& noteText,
                            std::function<void(FrontmatterSuggestion)> cb);

    // Drops pending searches, questions and suggestions (indexing carries on).
    void cancel();

Q_SIGNALS:
    void indexProgress(int done, int total);
    void indexingChanged(bool running);
    void indexError(const QString& message);

private:
    void nextBatch(int generation);
    void finishIndexing();
    void retrieve(const QString& query, bool question, int limit,
                  std::function<void(std::vector<Passage>, QString error)> cb);

    VaultService* vault_;
    AiClient client_;
    bool indexing_ = false;
    int generation_ = 0; // bumped by config changes and stops: late replies are ignored
    int done_ = 0;
    int total_ = 0;
};

} // namespace mn::ai
