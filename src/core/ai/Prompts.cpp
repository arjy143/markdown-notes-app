#include "Prompts.h"

#include "FrontmatterMerge.h"
#include "core/index/Similarity.h"
#include "core/vault/Paths.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>

using namespace Qt::StringLiterals;

namespace mn::ai {

namespace {

constexpr qsizetype kMaxNoteChars = 8000;   // of the note sent for frontmatter
constexpr qsizetype kMaxSourceChars = 1500; // per passage sent with a question
constexpr qsizetype kMaxSourcesChars = 14000;
constexpr int kMaxVaultTags = 150;

// The first {...} object in the text (models like to wrap JSON in prose or ``` fences).
QJsonObject firstJsonObject(const QString& text)
{
    for (qsizetype start = text.indexOf(u'{'); start >= 0; start = text.indexOf(u'{', start + 1)) {
        int depth = 0;
        bool inString = false;
        for (qsizetype i = start; i < text.size(); ++i) {
            const QChar c = text[i];
            if (inString) {
                if (c == u'\\')
                    ++i;
                else if (c == u'"')
                    inString = false;
                continue;
            }
            if (c == u'"') {
                inString = true;
            } else if (c == u'{') {
                ++depth;
            } else if (c == u'}' && --depth == 0) {
                const QJsonDocument doc = QJsonDocument::fromJson(text.mid(start, i - start + 1).toUtf8());
                if (doc.isObject()) return doc.object();
                break;
            }
        }
    }
    return {};
}

QStringList stringList(const QJsonValue& v)
{
    QStringList out;
    if (v.isArray()) {
        for (const auto& x : v.toArray())
            if (x.isString()) out.append(x.toString());
    } else if (v.isString()) {
        out = v.toString().split(u',', Qt::SkipEmptyParts);
    }
    return out;
}

} // namespace

QString stripThinking(QString reply)
{
    static const QRegularExpression think(uR"(<think>.*?</think>)"_s,
                                          QRegularExpression::DotMatchesEverythingOption);
    reply.remove(think);
    // An unclosed <think> (reply cut off while reasoning): nothing usable after it.
    const qsizetype open = reply.indexOf(u"<think>"_s);
    if (open >= 0) reply.truncate(open);
    return reply.trimmed();
}

std::vector<ChatMessage> frontmatterPrompt(const FrontmatterRequest& req)
{
    QString system =
        u"You organise a personal Markdown knowledge base (an Obsidian vault). Given one note, you suggest "
        "its frontmatter. Reply with one JSON object and nothing else, with these keys:\n"_s;
    QStringList keys;
    for (const QString& p : req.categoryProperties)
        keys.append(u"  \"%1\": one category%2"_s.arg(
            p, req.categories.isEmpty() ? QString() : u" from the list below"_s));
    keys.append(u"  \"tags\": 2 to 6 lowercase tags, single words or hyphenated, no '#'"_s);
    keys.append(u"  \"summary\": one sentence saying what the note is about"_s);
    system += keys.join(u'\n') + u"\n"_s;
    if (!req.categories.isEmpty())
        system += u"\nCategories: "_s + req.categories.join(u", "_s) +
                  u"\nEach category property must have a different category, the ones that fit best.\n"_s;
    if (!req.vaultTags.isEmpty())
        system += u"\nTags already used in the vault (prefer these; only add a new tag when none fits): "_s +
                  req.vaultTags.mid(0, kMaxVaultTags).join(u", "_s) + u'\n';
    if (!req.examples.isEmpty()) {
        system += u"\nFrontmatter of similar notes in this vault, to match their style:\n"_s;
        for (const QString& ex : req.examples)
            system += u"---\n"_s + ex.trimmed() + u"\n---\n"_s;
    }
    QString note = req.noteText;
    if (note.size() > kMaxNoteChars) note = note.left(kMaxNoteChars) + u"\n[…]"_s;
    return {{u"system"_s, system}, {u"user"_s, u"Note title: "_s + req.title + u"\n\n"_s + note}};
}

FrontmatterSuggestion parseFrontmatterSuggestion(const QString& replyIn, const FrontmatterRequest& req)
{
    FrontmatterSuggestion s;
    const QJsonObject o = firstJsonObject(stripThinking(replyIn));
    if (o.isEmpty()) {
        s.error = u"The model's reply wasn't the JSON that was asked for."_s;
        return s;
    }
    // Category properties: only known categories (in the vault's spelling), each used once.
    QStringList used;
    for (const QString& p : req.categoryProperties) {
        QString v = o.value(p).toString().trimmed();
        while (v.startsWith(u'#'))
            v.remove(0, 1);
        if (!req.categories.isEmpty()) {
            QString match;
            for (const QString& c : req.categories)
                if (c.compare(v, Qt::CaseInsensitive) == 0) match = c;
            v = match;
        }
        if (v.isEmpty() || used.contains(v, Qt::CaseInsensitive)) continue;
        used.append(v);
        s.properties.append({p, v});
    }
    // Tags: cleaned, in the vault's spelling when they exist, without the categories already set.
    for (const QString& raw : stringList(o.value(u"tags"_s))) {
        QString t = cleanTag(raw);
        if (t.isEmpty()) continue;
        for (const QString& known : req.vaultTags)
            if (known.compare(t, Qt::CaseInsensitive) == 0) t = known;
        if (s.tags.contains(t, Qt::CaseInsensitive) || used.contains(t, Qt::CaseInsensitive)) continue;
        s.tags.append(t);
    }
    const QString summary = o.value(u"summary"_s).toString().simplified();
    if (!summary.isEmpty()) s.properties.append({u"summary"_s, summary});
    return s;
}

std::vector<ChatMessage> askPrompt(const QString& question, const std::vector<Passage>& sources)
{
    const QString system =
        u"You are the user's assistant for their personal notes. Answer the question using only the numbered "
        "excerpts from their notes below. Cite the excerpts you use like [1] or [2][3]. If the excerpts "
        "don't "
        "answer the question, say so plainly instead of guessing. Be concise. Use Markdown."_s;
    QString context;
    for (std::size_t i = 0; i < sources.size(); ++i) {
        const Passage& p = sources[i];
        QString text = p.text;
        if (text.size() > kMaxSourceChars) text = text.left(kMaxSourceChars) + u" […]"_s;
        QString head = u"[%1] %2 (%3)"_s.arg(i + 1).arg(
            paths::linkName(p.path), QDateTime::fromMSecsSinceEpoch(p.dateMs).date().toString(Qt::ISODate));
        if (!p.context.isEmpty()) head += u" › "_s + p.context;
        const QString block = head + u'\n' + text + u"\n\n"_s;
        if (context.size() + block.size() > kMaxSourcesChars && i > 0) break;
        context += block;
    }
    if (context.isEmpty()) context = u"(No excerpts matched.)\n\n"_s;
    return {{u"system"_s, system}, {u"user"_s, u"Excerpts:\n\n"_s + context + u"Question: "_s + question}};
}

QStringList questionTerms(const QString& question)
{
    const QHash<QString, int> tf = sim::termFrequencies(question);
    QStringList out = tf.keys();
    out.sort();
    return out;
}

} // namespace mn::ai
