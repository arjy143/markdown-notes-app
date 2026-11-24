#pragma once

#include "Prompts.h"

#include <QObject>
#include <QString>
#include <QStringList>
#include <QUrl>

#include <functional>
#include <vector>

class QNetworkAccessManager;
class QNetworkReply;
class QJsonObject;

// A minimal client for an OpenAI-compatible server (llama.cpp, Ollama, LM Studio,
// vLLM, …): /v1/chat/completions and /v1/embeddings. Asynchronous; lives on the
// UI thread. Nothing is sent until a server is configured.
namespace mn::ai {

struct AiConfig {
    QString baseUrl; // e.g. http://192.168.1.20:8080/v1 ("/v1" is added when the URL has no path)
    QString apiKey;  // optional
    QString chatModel;
    QString embedModel;
    QString embedUrl;    // optional: a separate server for embeddings (llama.cpp serves one model per server)
    QString queryPrefix; // some embedding models want e.g. "search_query: "
    QString documentPrefix;    // … and "search_document: "
    bool semanticIndex = true; // embed passages in the background

    bool chatReady() const { return !baseUrl.trimmed().isEmpty() && !chatModel.trimmed().isEmpty(); }
    bool embedReady() const { return !embedServer().isEmpty() && !embedModel.trimmed().isEmpty(); }
    QString embedServer() const
    {
        return embedUrl.trimmed().isEmpty() ? baseUrl.trimmed() : embedUrl.trimmed();
    }
    // App-wide (QSettings): the server belongs to the machine, not the vault.
    static AiConfig load();
    void save() const;
    friend bool operator==(const AiConfig&, const AiConfig&) = default;
};

class AiClient : public QObject {
    Q_OBJECT
public:
    using ChatCallback = std::function<void(QString reply, QString error)>;
    using EmbedCallback = std::function<void(std::vector<std::vector<float>> vectors, QString error)>;

    explicit AiClient(QObject* parent = nullptr);
    ~AiClient() override;

    void setConfig(const AiConfig& config) { config_ = config; }
    const AiConfig& config() const { return config_; }

    // The reply's text, without a reasoning model's <think> part.
    void chat(const std::vector<ChatMessage>& messages, ChatCallback cb, double temperature = 0.2);
    // One vector per input, in order.
    void embed(const QStringList& inputs, EmbedCallback cb);
    // Drops pending requests (their callbacks are not called).
    void cancelAll();

    // `server`'s URL for an API path ("chat/completions"); "/v1" is added when the URL has no path.
    static QUrl endpoint(const QString& server, const QString& path);
    QUrl endpoint(const QString& path) const { return endpoint(config_.baseUrl, path); }

private:
    QNetworkReply* post(const QUrl& url, const QJsonObject& body, int timeoutMs);
    QNetworkAccessManager* nam();

    AiConfig config_;
    QNetworkAccessManager* nam_ = nullptr; // created on first use
};

} // namespace mn::ai
