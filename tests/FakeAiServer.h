#pragma once

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTest>

#include <functional>
#include <memory>
#include <vector>

namespace mn::test {

using namespace Qt::StringLiterals;

// A stand-in for a local OpenAI-compatible server. Its "embeddings" put words with the
// same meaning on the same dimension, so semantic search can be tested without a model.
class FakeServer : public QObject {
public:
    int embedCalls = 0;
    int chatCalls = 0;
    int failStatus = 0; // answer every request with this HTTP status
    QJsonObject lastChat;
    std::function<QString(const QJsonObject&)> chatReply = [](const QJsonObject&) { return u"ok"_s; };

    FakeServer()
    {
        connect(&server_, &QTcpServer::newConnection, this, [this] {
            while (QTcpSocket* s = server_.nextPendingConnection()) {
                auto buf = std::make_shared<QByteArray>();
                connect(s, &QTcpSocket::readyRead, s, [this, s, buf] {
                    *buf += s->readAll();
                    const qsizetype end = buf->indexOf("\r\n\r\n");
                    if (end < 0) return;
                    const QByteArray head = buf->left(end);
                    qsizetype length = 0;
                    for (const QByteArray& l : head.split('\n')) {
                        if (l.toLower().startsWith("content-length:"))
                            length = l.mid(15).trimmed().toLongLong();
                    }
                    if (buf->size() < end + 4 + length) return;
                    const QByteArray path = head.split(' ').value(1);
                    respond(s, path, QJsonDocument::fromJson(buf->mid(end + 4, length)).object());
                    buf->clear();
                });
                connect(s, &QTcpSocket::disconnected, s, &QObject::deleteLater);
            }
        });
        QVERIFY(server_.listen(QHostAddress::LocalHost));
    }
    QString url() const { return u"http://127.0.0.1:%1"_s.arg(server_.serverPort()); }

    static std::vector<double> vectorFor(const QString& text)
    {
        static const QList<QStringList> groups{
            {u"coffee"_s, u"caffeine"_s, u"espresso"_s},
            {u"sleep"_s, u"slept"_s, u"asleep"_s, u"awake"_s, u"insomnia"_s, u"melatonin"_s},
            {u"tomato"_s, u"compost"_s, u"garden"_s, u"seedlings"_s, u"plants"_s},
            {u"rust"_s, u"borrow"_s, u"compiler"_s, u"lifetimes"_s, u"lexer"_s},
            {u"budget"_s, u"meeting"_s, u"planning"_s, u"quarterly"_s}};
        std::vector<double> v(groups.size() + 1, 0.0);
        v.back() = 0.05;
        for (const QString& w : text.toLower().split(QRegularExpression(u"[^a-z]+"_s), Qt::SkipEmptyParts)) {
            for (qsizetype g = 0; g < groups.size(); ++g)
                if (groups[g].contains(w)) v[static_cast<std::size_t>(g)] += 1;
        }
        return v;
    }

private:
    void respond(QTcpSocket* s, const QByteArray& path, const QJsonObject& body)
    {
        int status = 200;
        QJsonObject reply;
        if (failStatus != 0) {
            status = failStatus;
            reply = QJsonObject{{u"error"_s, QJsonObject{{u"message"_s, u"model not loaded"_s}}}};
        } else if (path.endsWith("/embeddings")) {
            ++embedCalls;
            QJsonArray data;
            const QJsonArray input = body.value(u"input"_s).toArray();
            for (qsizetype i = 0; i < input.size(); ++i) {
                QJsonArray vec;
                for (double x : vectorFor(input[i].toString()))
                    vec.append(x);
                data.append(QJsonObject{{u"index"_s, i}, {u"embedding"_s, vec}});
            }
            reply = QJsonObject{{u"data"_s, data}};
        } else if (path.endsWith("/chat/completions")) {
            ++chatCalls;
            lastChat = body;
            reply = QJsonObject{
                {u"choices"_s,
                 QJsonArray{QJsonObject{{u"message"_s, QJsonObject{{u"role"_s, u"assistant"_s},
                                                                   {u"content"_s, chatReply(body)}}}}}}};
        } else {
            status = 404;
        }
        const QByteArray json = QJsonDocument(reply).toJson(QJsonDocument::Compact);
        s->write("HTTP/1.1 " + QByteArray::number(status) +
                 " X\r\nContent-Type: application/json\r\nContent-Length: " +
                 QByteArray::number(json.size()) + "\r\nConnection: close\r\n\r\n" + json);
        s->disconnectFromHost();
    }

    QTcpServer server_;
};

} // namespace mn::test
