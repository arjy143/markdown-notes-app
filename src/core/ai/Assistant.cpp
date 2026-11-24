#include "Assistant.h"

#include "core/index/VaultService.h"
#include "core/vault/Paths.h"

#include <QPointer>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace mn::ai {

namespace {
constexpr int kBatch = 16;          // passages per embeddings request
constexpr int kAskSources = 8;      // passages sent with a question
constexpr int kSearchResults = 200; // like keyword passage search
} // namespace

Assistant::Assistant(VaultService* vault, QObject* parent) : QObject(parent), vault_(vault)
{
    connect(vault_, &VaultService::indexingFinished, this, [this] {
        if (config().semanticIndex && config().embedReady()) updateIndex();
    });
}

void Assistant::setConfig(const AiConfig& c)
{
    const bool embeddingChanged = c.embedServer() != config().embedServer() ||
                                  c.embedModel != config().embedModel ||
                                  c.documentPrefix != config().documentPrefix || c.apiKey != config().apiKey;
    client_.setConfig(c);
    vault_->setEmbeddingModel(c.embedReady() ? c.embedModel.trimmed() : QString());
    if (embeddingChanged || !c.semanticIndex) stopIndexing();
    if (c.semanticIndex && c.embedReady() && vault_->isOpen()) updateIndex();
}

void Assistant::updateIndex()
{
    if (indexing_ || !config().embedReady() || !vault_->isOpen()) return;
    indexing_ = true;
    Q_EMIT indexingChanged(true);
    nextBatch(generation_);
}

void Assistant::stopIndexing()
{
    ++generation_;
    if (!indexing_) return;
    indexing_ = false;
    Q_EMIT indexingChanged(false);
}

void Assistant::finishIndexing()
{
    indexing_ = false;
    Q_EMIT indexingChanged(false);
    vault_->pruneEmbeddings([](int) {});
}

void Assistant::nextBatch(int generation)
{
    const QString model = config().embedModel.trimmed();
    QPointer<Assistant> self(this);
    vault_->pendingEmbeddings(model, kBatch, [self, generation, model](EmbeddingBatch batch) {
        if (self.isNull() || generation != self->generation_) return;
        self->done_ = batch.done;
        self->total_ = batch.total;
        Q_EMIT self->indexProgress(batch.done, batch.total);
        if (batch.jobs.empty()) {
            self->finishIndexing();
            return;
        }
        QStringList texts;
        std::vector<QByteArray> hashes;
        for (const EmbeddingJob& j : batch.jobs) {
            texts.append(self->config().documentPrefix + j.text);
            hashes.push_back(j.hash);
        }
        self->client_.embed(texts, [self, generation, model,
                                    hashes = std::move(hashes)](std::vector<std::vector<float>> vectors,
                                                                const QString& error) mutable {
            if (self.isNull() || generation != self->generation_) return;
            if (!error.isEmpty()) {
                self->indexing_ = false;
                Q_EMIT self->indexingChanged(false);
                Q_EMIT self->indexError(error);
                return;
            }
            self->vault_->storeEmbeddings(model, std::move(hashes), std::move(vectors),
                                          [self, generation](int) {
                                              if (!self.isNull() && generation == self->generation_)
                                                  self->nextBatch(generation);
                                          });
        });
    });
}

void Assistant::retrieve(const QString& query, bool question, int limit,
                         std::function<void(std::vector<Passage>, QString)> cb)
{
    const QString model = config().embedModel.trimmed();
    QPointer<Assistant> self(this);
    auto run = [self, query, question, limit, model, cb](std::vector<float> vector,
                                                         const QString& embedError) {
        if (self.isNull()) return;
        self->vault_->hybridPassages(
            query, std::move(vector), model, question, limit,
            [cb, embedError](std::vector<Passage> found) { cb(std::move(found), embedError); });
    };
    if (!config().embedReady() || done_ == 0) {
        run({}, {});
        return;
    }
    client_.embed({config().queryPrefix + query},
                  [run](std::vector<std::vector<float>> v, const QString& error) {
                      // If the server is unreachable, fall back to keywords (and say why).
                      run(v.empty() ? std::vector<float>() : std::move(v.front()), error);
                  });
}

void Assistant::search(const QString& query, std::function<void(std::vector<Passage>, QString)> cb)
{
    retrieve(query, false, kSearchResults, std::move(cb));
}

void Assistant::ask(const QString& question, std::function<void(Answer)> cb)
{
    if (!config().chatReady()) {
        cb({{}, {}, tr("Set the server and a chat model in Tools › AI assistant settings first.")});
        return;
    }
    QPointer<Assistant> self(this);
    retrieve(question, true, kAskSources, [self, question, cb](std::vector<Passage> sources, const QString&) {
        if (self.isNull()) return;
        auto shared = std::make_shared<std::vector<Passage>>(std::move(sources));
        self->client_.chat(askPrompt(question, *shared), [shared, cb](QString reply, const QString& error) {
            cb({std::move(reply), std::move(*shared), error});
        });
    });
}

void Assistant::suggestFrontmatter(const QString& path, const QString& noteText,
                                   std::function<void(FrontmatterSuggestion)> cb)
{
    if (!config().chatReady()) {
        FrontmatterSuggestion s;
        s.error = tr("Set the server and a chat model in Tools › AI assistant settings first.");
        cb(s);
        return;
    }
    FrontmatterRequest req;
    req.title = paths::linkName(path);
    req.noteText = noteText;
    req.categoryProperties = vault_->settings().tagProperties;
    req.categories = vault_->settings().categoryTags;
    std::vector<TagCount> tags = vault_->snapshot().tags;
    std::stable_sort(tags.begin(), tags.end(),
                     [](const TagCount& a, const TagCount& b) { return a.notes > b.notes; });
    for (const TagCount& t : tags)
        req.vaultTags.append(t.tag);
    QPointer<Assistant> self(this);
    vault_->exampleFrontmatter(path, [self, req, cb](QStringList examples) mutable {
        if (self.isNull()) return;
        req.examples = std::move(examples);
        self->client_.chat(frontmatterPrompt(req), [req, cb](const QString& reply, const QString& error) {
            if (!error.isEmpty()) {
                FrontmatterSuggestion s;
                s.error = error;
                cb(s);
                return;
            }
            cb(parseFrontmatterSuggestion(reply, req));
        });
    });
}

void Assistant::cancel()
{
    // Cancelling drops the indexer's request too: restart it from where it was.
    const bool wasIndexing = indexing_;
    client_.cancelAll();
    stopIndexing();
    if (wasIndexing) updateIndex();
}

} // namespace mn::ai
