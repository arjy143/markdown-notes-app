#include "AiClient.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSettings>

using namespace Qt::StringLiterals;

namespace mn::ai {

namespace {

constexpr int kChatTimeoutMs = 300000; // local models can be slow
constexpr int kEmbedTimeoutMs = 120000;

// The server's own error message when it sent one, else Qt's.
QString errorText(QNetworkReply* r, const QByteArray& body)
{
    const QJsonObject o = QJsonDocument::fromJson(body).object();
    QString msg;
    const QJsonValue e = o.value(u"error"_s);
    if (e.isObject())
        msg = e.toObject().value(u"message"_s).toString();
    else if (e.isString())
        msg = e.toString();
    const int status = r->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (msg.isEmpty()) msg = r->errorString();
    return status > 0 ? u"HTTP %1: %2"_s.arg(status).arg(msg) : msg;
}

} // namespace

AiConfig AiConfig::load()
{
    QSettings s;
    AiConfig c;
    c.baseUrl = s.value(u"ai/baseUrl"_s).toString();
    c.apiKey = s.value(u"ai/apiKey"_s).toString();
    c.chatModel = s.value(u"ai/chatModel"_s).toString();
    c.embedModel = s.value(u"ai/embedModel"_s).toString();
    c.embedUrl = s.value(u"ai/embedUrl"_s).toString();
    c.queryPrefix = s.value(u"ai/queryPrefix"_s).toString();
    c.documentPrefix = s.value(u"ai/documentPrefix"_s).toString();
    c.semanticIndex = s.value(u"ai/semanticIndex"_s, true).toBool();
    return c;
}

void AiConfig::save() const
{
    QSettings s;
    s.setValue(u"ai/baseUrl"_s, baseUrl.trimmed());
    s.setValue(u"ai/apiKey"_s, apiKey);
    s.setValue(u"ai/chatModel"_s, chatModel.trimmed());
    s.setValue(u"ai/embedModel"_s, embedModel.trimmed());
    s.setValue(u"ai/embedUrl"_s, embedUrl.trimmed());
    s.setValue(u"ai/queryPrefix"_s, queryPrefix);
    s.setValue(u"ai/documentPrefix"_s, documentPrefix);
    s.setValue(u"ai/semanticIndex"_s, semanticIndex);
}

AiClient::AiClient(QObject* parent) : QObject(parent) {}

AiClient::~AiClient()
{
    cancelAll();
}

QNetworkAccessManager* AiClient::nam()
{
    if (nam_ == nullptr) nam_ = new QNetworkAccessManager(this);
    return nam_;
}

QUrl AiClient::endpoint(const QString& server, const QString& path)
{
    QString base = server.trimmed();
    if (!base.contains(u"://"_s)) base.prepend(u"http://"_s);
    while (base.endsWith(u'/'))
        base.chop(1);
    QUrl url(base);
    if (url.path().isEmpty() || url.path() == u"/") url.setPath(u"/v1"_s);
    url.setPath(url.path() + u'/' + path);
    return url;
}

QNetworkReply* AiClient::post(const QUrl& url, const QJsonObject& body, int timeoutMs)
{
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::ContentTypeHeader, u"application/json"_s);
    if (!config_.apiKey.isEmpty()) req.setRawHeader("Authorization", "Bearer " + config_.apiKey.toUtf8());
    req.setTransferTimeout(timeoutMs);
    return nam()->post(req, QJsonDocument(body).toJson(QJsonDocument::Compact));
}

void AiClient::chat(const std::vector<ChatMessage>& messages, ChatCallback cb, double temperature)
{
    QJsonArray msgs;
    for (const ChatMessage& m : messages)
        msgs.append(QJsonObject{{u"role"_s, m.role}, {u"content"_s, m.content}});
    const QJsonObject body{{u"model"_s, config_.chatModel.trimmed()},
                           {u"messages"_s, msgs},
                           {u"temperature"_s, temperature},
                           {u"stream"_s, false}};
    QNetworkReply* r = post(endpoint(u"chat/completions"_s), body, kChatTimeoutMs);
    connect(r, &QNetworkReply::finished, this, [r, cb = std::move(cb)] {
        r->deleteLater();
        const QByteArray data = r->readAll();
        if (r->error() != QNetworkReply::NoError) {
            cb({}, errorText(r, data));
            return;
        }
        const QJsonArray choices = QJsonDocument::fromJson(data).object().value(u"choices"_s).toArray();
        const QString content =
            choices.isEmpty()
                ? QString()
                : choices.first().toObject().value(u"message"_s).toObject().value(u"content"_s).toString();
        if (content.isEmpty()) {
            cb({}, u"The server's reply had no message in it."_s);
            return;
        }
        cb(stripThinking(content), {});
    });
}

void AiClient::embed(const QStringList& inputs, EmbedCallback cb)
{
    const QJsonObject body{{u"model"_s, config_.embedModel.trimmed()},
                           {u"input"_s, QJsonArray::fromStringList(inputs)}};
    QNetworkReply* r = post(endpoint(config_.embedServer(), u"embeddings"_s), body, kEmbedTimeoutMs);
    const qsizetype expected = inputs.size();
    connect(r, &QNetworkReply::finished, this, [r, expected, cb = std::move(cb)] {
        r->deleteLater();
        const QByteArray data = r->readAll();
        if (r->error() != QNetworkReply::NoError) {
            cb({}, errorText(r, data));
            return;
        }
        const QJsonArray items = QJsonDocument::fromJson(data).object().value(u"data"_s).toArray();
        std::vector<std::vector<float>> out(static_cast<std::size_t>(expected));
        int filled = 0;
        for (qsizetype i = 0; i < items.size(); ++i) {
            const QJsonObject o = items[i].toObject();
            const qsizetype at = o.contains(u"index"_s) ? o.value(u"index"_s).toInteger() : i;
            if (at < 0 || at >= expected) continue;
            std::vector<float>& v = out[static_cast<std::size_t>(at)];
            const QJsonArray embedding = o.value(u"embedding"_s).toArray();
            v.reserve(static_cast<std::size_t>(embedding.size()));
            for (const auto& x : embedding)
                v.push_back(static_cast<float>(x.toDouble()));
            if (!v.empty()) ++filled;
        }
        if (filled != expected) {
            cb({}, u"The server returned %1 embeddings for %2 texts."_s.arg(filled).arg(expected));
            return;
        }
        cb(std::move(out), {});
    });
}

void AiClient::cancelAll()
{
    if (nam_ == nullptr) return;
    for (QNetworkReply* r : nam_->findChildren<QNetworkReply*>()) {
        r->disconnect(this); // no callback for a cancelled request
        r->abort();
        r->deleteLater();
    }
}

} // namespace mn::ai
