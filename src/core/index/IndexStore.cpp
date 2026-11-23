#include "IndexStore.h"

#include "Similarity.h"
#include "core/links/RenamePlanner.h"
#include "core/vault/Paths.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace mn {

namespace {

const char* const kSchema = R"SQL(
CREATE TABLE meta(key TEXT PRIMARY KEY, value TEXT NOT NULL);
CREATE TABLE files(
    id INTEGER PRIMARY KEY,
    path TEXT NOT NULL UNIQUE,
    kind INTEGER NOT NULL,
    size INTEGER NOT NULL,
    mtime INTEGER NOT NULL,
    hash BLOB
);
CREATE TABLE links(file_id INTEGER NOT NULL, embed INTEGER NOT NULL, linkpath TEXT NOT NULL,
                   subpath TEXT NOT NULL, line INTEGER NOT NULL, name_key TEXT NOT NULL);
CREATE INDEX links_file ON links(file_id);
CREATE INDEX links_name ON links(name_key);
CREATE TABLE tags(file_id INTEGER NOT NULL, tag TEXT NOT NULL, tag_key TEXT NOT NULL, line INTEGER NOT NULL);
CREATE INDEX tags_file ON tags(file_id);
CREATE INDEX tags_key ON tags(tag_key);
CREATE TABLE headings(file_id INTEGER NOT NULL, level INTEGER NOT NULL, text TEXT NOT NULL, line INTEGER NOT NULL);
CREATE INDEX headings_file ON headings(file_id);
CREATE TABLE blocks(file_id INTEGER NOT NULL, block_id TEXT NOT NULL, start_line INTEGER NOT NULL,
                    end_line INTEGER NOT NULL);
CREATE INDEX blocks_file ON blocks(file_id);
CREATE TABLE aliases(file_id INTEGER NOT NULL, alias TEXT NOT NULL);
CREATE INDEX aliases_file ON aliases(file_id);
CREATE TABLE tasks(file_id INTEGER NOT NULL, line INTEGER NOT NULL, done INTEGER NOT NULL, text TEXT NOT NULL,
                   due TEXT NOT NULL);
CREATE INDEX tasks_file ON tasks(file_id);
CREATE VIRTUAL TABLE fts USING fts5(title, body, tokenize = 'unicode61 remove_diacritics 2');
CREATE VIRTUAL TABLE fts_vocab USING fts5vocab(fts, 'row');
)SQL";

QString quoteFtsTerm(const QString& t)
{
    QString q = t;
    q.replace(u'"', u"\"\""_s);
    return u'"' + q + u'"';
}

// Folds for matching: lower case, accents removed. One output char per input
// UTF-16 unit, so positions map back 1:1.
QString foldForMatch(QStringView s)
{
    QString out(s.size(), Qt::Uninitialized);
    for (qsizetype i = 0; i < s.size(); ++i) {
        QChar c = s[i];
        if (c.decompositionTag() == QChar::Canonical) {
            const QString d = c.decomposition();
            if (!d.isEmpty()) c = d[0];
        }
        out[i] = c.toLower();
    }
    return out;
}

QStringList snippetTerms(const QString& userQuery)
{
    QStringList terms;
    QString cur;
    bool inQuote = false;
    for (QChar c : userQuery) {
        if (c == u'"') {
            if (!cur.trimmed().isEmpty()) terms.append(cur.trimmed());
            cur.clear();
            inQuote = !inQuote;
        } else if (!inQuote && c.isSpace()) {
            if (!cur.isEmpty()) terms.append(cur);
            cur.clear();
        } else {
            cur += c;
        }
    }
    if (!cur.trimmed().isEmpty()) terms.append(cur.trimmed());
    for (QString& t : terms)
        t = foldForMatch(t);
    return terms;
}

// Builds a snippet around the first match, marking matches with \x01 ... \x02.
QString makeSnippet(const QString& body, const QStringList& terms)
{
    const QString folded = foldForMatch(body);
    qsizetype first = -1;
    for (const QString& t : terms) {
        qsizetype from = 0;
        while (true) {
            const qsizetype p = folded.indexOf(t, from);
            if (p < 0) break;
            if (p == 0 || !folded[p - 1].isLetterOrNumber()) {
                if (first < 0 || p < first) first = p;
                break;
            }
            from = p + 1;
        }
    }
    if (first < 0) first = 0;
    qsizetype start = std::max<qsizetype>(0, first - 60);
    while (start > 0 && start < body.size() && !body[start - 1].isSpace())
        --start;
    const qsizetype end = std::min<qsizetype>(body.size(), start + 200);
    QString out;
    if (start > 0) out += u'…';
    qsizetype i = start;
    while (i < end) {
        qsizetype matchLen = 0;
        if (i == 0 || !folded[i - 1].isLetterOrNumber()) {
            for (const QString& t : terms) {
                if (!t.isEmpty() && QStringView(folded).mid(i).startsWith(t)) {
                    qsizetype l = t.size();
                    while (i + l < body.size() && folded[i + l].isLetterOrNumber())
                        ++l; // whole word (prefix match)
                    matchLen = std::max(matchLen, l);
                }
            }
        }
        if (matchLen > 0) {
            out += QChar(1) + body.mid(i, matchLen) + QChar(2);
            i += matchLen;
        } else {
            const QChar c = body[i];
            out += (c == u'\n' || c == u'\r' || c == u'\t') ? QChar(u' ') : c;
            ++i;
        }
    }
    if (end < body.size()) out += u'…';
    return out.simplified();
}

} // namespace

QString IndexStore::vaultId(const QString& vaultRoot)
{
    QString canonical = QFileInfo(vaultRoot).canonicalFilePath();
    if (canonical.isEmpty()) canonical = QFileInfo(vaultRoot).absoluteFilePath();
    return QString::fromLatin1(
        QCryptographicHash::hash(canonical.toUtf8(), QCryptographicHash::Sha1).toHex().left(16));
}

QString IndexStore::defaultPath(const QString& vaultRoot)
{
    const QString base = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    return base + u'/' + vaultId(vaultRoot) + u"/index.sqlite"_s;
}

bool IndexStore::open(const QString& dbPath, QString* error)
{
    QDir().mkpath(QFileInfo(dbPath).absolutePath());
    if (!db_.open(dbPath, error)) return false;
    db_.exec("PRAGMA journal_mode=WAL");
    db_.exec("PRAGMA synchronous=NORMAL");
    db_.exec("PRAGMA temp_store=MEMORY");
    db_.exec("PRAGMA cache_size=-1024"); // 1 MB page cache

    int version = -1;
    {
        sql::Statement s = db_.prepare("PRAGMA user_version");
        if (s.step()) version = s.int32(0);
    }
    if (version == kSchemaVersion) {
        prepareStatements();
        return true;
    }

    // Missing or outdated: the index is a disposable cache, so rebuild it.
    db_.close();
    QFile::remove(dbPath);
    QFile::remove(dbPath + u"-wal"_s);
    QFile::remove(dbPath + u"-shm"_s);
    if (!db_.open(dbPath, error)) return false;
    db_.exec("PRAGMA journal_mode=WAL");
    db_.exec("PRAGMA synchronous=NORMAL");
    db_.exec("PRAGMA temp_store=MEMORY");
    db_.exec("PRAGMA cache_size=-1024");
    if (!createSchema(error)) return false;
    prepareStatements();
    return true;
}

void IndexStore::prepareStatements()
{
    st_ = std::make_unique<Statements>();
    st_->selectId = db_.prepare("SELECT id FROM files WHERE path = ?");
    st_->insertFile = db_.prepare("INSERT INTO files(path, kind, size, mtime, hash) VALUES (?, ?, ?, ?, ?)");
    st_->insertLink = db_.prepare(
        "INSERT INTO links(file_id, embed, linkpath, subpath, line, name_key) VALUES (?, ?, ?, ?, ?, ?)");
    st_->insertTag = db_.prepare("INSERT INTO tags(file_id, tag, tag_key, line) VALUES (?, ?, ?, ?)");
    st_->insertHeading = db_.prepare("INSERT INTO headings(file_id, level, text, line) VALUES (?, ?, ?, ?)");
    st_->insertBlock =
        db_.prepare("INSERT INTO blocks(file_id, block_id, start_line, end_line) VALUES (?, ?, ?, ?)");
    st_->insertAlias = db_.prepare("INSERT INTO aliases(file_id, alias) VALUES (?, ?)");
    st_->insertFts = db_.prepare("INSERT INTO fts(rowid, title, body) VALUES (?, ?, ?)");
    st_->updateStamp = db_.prepare("UPDATE files SET size = ?, mtime = ? WHERE path = ?");
    st_->insertTask = db_.prepare("INSERT INTO tasks(file_id, line, done, text, due) VALUES (?, ?, ?, ?, ?)");
    st_->docFreq = db_.prepare("SELECT doc FROM fts_vocab WHERE term = ?");
    static const char* const deletes[] = {
        "DELETE FROM links WHERE file_id = ?",    "DELETE FROM tags WHERE file_id = ?",
        "DELETE FROM headings WHERE file_id = ?", "DELETE FROM blocks WHERE file_id = ?",
        "DELETE FROM aliases WHERE file_id = ?",  "DELETE FROM fts WHERE rowid = ?",
        "DELETE FROM tasks WHERE file_id = ?",    "DELETE FROM files WHERE id = ?"};
    for (int i = 0; i < 8; ++i)
        st_->deletes[i] = db_.prepare(deletes[i]);
}

bool IndexStore::createSchema(QString* error)
{
    if (!db_.exec(kSchema, error)) return false;
    const QByteArray pragma = "PRAGMA user_version=" + QByteArray::number(kSchemaVersion);
    return db_.exec(pragma.constData(), error);
}

void IndexStore::beginBulk()
{
    db_.exec("PRAGMA cache_size=-65536");
    db_.exec("PRAGMA synchronous=OFF");
}

void IndexStore::endBulk()
{
    db_.exec("PRAGMA synchronous=NORMAL");
    db_.exec("PRAGMA cache_size=-1024");
    db_.exec("PRAGMA shrink_memory");
}

QString IndexStore::meta(const QString& key)
{
    sql::Statement s = db_.prepare("SELECT value FROM meta WHERE key = ?");
    s.bind(1, key);
    return s.step() ? s.text(0) : QString();
}

void IndexStore::setMeta(const QString& key, const QString& value)
{
    sql::Statement s = db_.prepare("INSERT OR REPLACE INTO meta(key, value) VALUES (?, ?)");
    s.bind(1, key).bind(2, value);
    s.exec();
}

std::vector<StoredFile> IndexStore::loadFiles()
{
    std::vector<StoredFile> out;
    sql::Statement s = db_.prepare("SELECT id, path, kind, size, mtime, hash FROM files ORDER BY path");
    while (s.step()) {
        out.push_back(
            {s.int64(0), s.text(1), static_cast<FileKind>(s.int32(2)), s.int64(3), s.int64(4), s.blob(5)});
    }
    return out;
}

qint64 IndexStore::idFor(const QString& path)
{
    sql::Statement& s = st_->selectId;
    s.bind(1, path);
    const qint64 id = s.step() ? s.int64(0) : 0;
    s.reset();
    return id;
}

bool IndexStore::removeFile(const QString& path)
{
    const qint64 id = idFor(path);
    if (id == 0) return true;
    bool ok = true;
    for (sql::Statement& d : st_->deletes) {
        d.bind(1, id);
        ok = d.exec() && ok;
    }
    return ok;
}

bool IndexStore::putAttachment(const QString& path, FileKind kind, qint64 size, qint64 mtimeMs, bool known)
{
    if (known) removeFile(path);
    sql::Statement& s = st_->insertFile;
    s.bind(1, path).bind(2, static_cast<int>(kind)).bind(3, size).bind(4, mtimeMs).bindNull(5);
    return s.exec();
}

bool IndexStore::updateStamp(const QString& path, qint64 size, qint64 mtimeMs)
{
    sql::Statement& s = st_->updateStamp;
    s.bind(1, size).bind(2, mtimeMs).bind(3, path);
    return s.exec();
}

bool IndexStore::putNote(const QString& path, qint64 size, qint64 mtimeMs, const QByteArray& hash,
                         const NoteMetadata& meta, const QString& body, bool known)
{
    if (known) removeFile(path);
    sql::Statement& ins = st_->insertFile;
    ins.bind(1, path)
        .bind(2, static_cast<int>(FileKind::Note))
        .bind(3, size)
        .bind(4, mtimeMs)
        .bindBlob(5, hash);
    if (!ins.exec()) return false;
    const qint64 id = db_.lastInsertId();

    sql::Statement& link = st_->insertLink;
    for (const LinkRef& l : meta.links) {
        link.bind(1, id).bind(2, l.embed ? 1 : 0).bind(3, l.linkpath).bind(4, l.subpath).bind(5, l.line);
        link.bind(6, l.linkpath.isEmpty() ? QString() : linkNameKey(l.linkpath));
        link.exec();
    }
    sql::Statement& tag = st_->insertTag;
    for (const TagRef& t : meta.tags) {
        tag.bind(1, id).bind(2, t.tag).bind(3, t.tag.toCaseFolded()).bind(4, t.line);
        tag.exec();
    }
    sql::Statement& head = st_->insertHeading;
    for (const Heading& h : meta.headings) {
        head.bind(1, id).bind(2, h.level).bind(3, h.text).bind(4, h.line);
        head.exec();
    }
    sql::Statement& block = st_->insertBlock;
    for (const BlockRef& b : meta.blocks) {
        block.bind(1, id).bind(2, b.id).bind(3, b.startLine).bind(4, b.endLine);
        block.exec();
    }
    sql::Statement& alias = st_->insertAlias;
    for (const QString& a : meta.frontmatter.aliases) {
        alias.bind(1, id).bind(2, a);
        alias.exec();
    }
    sql::Statement& task = st_->insertTask;
    for (const TaskItem& t : meta.tasks) {
        task.bind(1, id).bind(2, t.line).bind(3, t.done ? 1 : 0).bind(4, t.text).bind(5, t.due);
        task.exec();
    }
    sql::Statement& fts = st_->insertFts;
    fts.bind(1, id).bind(2, paths::linkName(path)).bind(3, body);
    return fts.exec();
}

std::vector<StoredLink> IndexStore::linksWithNameKeys(const QStringList& keys)
{
    std::vector<StoredLink> out;
    sql::Statement s = db_.prepare(
        "SELECT f.path, l.linkpath, l.subpath, l.line, l.embed FROM links l JOIN files f ON f.id = l.file_id "
        "WHERE l.name_key = ?");
    for (const QString& k : keys) {
        s.bind(1, k);
        while (s.step())
            out.push_back({s.text(0), s.text(1), s.text(2), s.int32(3), s.int32(4) != 0});
        s.reset();
    }
    return out;
}

std::vector<StoredLink> IndexStore::allLinks()
{
    std::vector<StoredLink> out;
    sql::Statement s = db_.prepare(
        "SELECT f.path, l.linkpath, l.subpath, l.line, l.embed FROM links l JOIN files f ON f.id = l.file_id "
        "WHERE l.linkpath <> ''");
    while (s.step())
        out.push_back({s.text(0), s.text(1), s.text(2), s.int32(3), s.int32(4) != 0});
    return out;
}

QStringList IndexStore::notesLinkingToNames(const QStringList& keys)
{
    QStringList out;
    sql::Statement s = db_.prepare(
        "SELECT DISTINCT f.path FROM links l JOIN files f ON f.id = l.file_id WHERE l.name_key = ?");
    for (const QString& k : keys) {
        s.bind(1, k);
        while (s.step())
            out.append(s.text(0));
        s.reset();
    }
    out.removeDuplicates();
    return out;
}

QHash<QString, QStringList> IndexStore::allAliases()
{
    QHash<QString, QStringList> out;
    sql::Statement s = db_.prepare("SELECT f.path, a.alias FROM aliases a JOIN files f ON f.id = a.file_id");
    while (s.step())
        out[s.text(0)].append(s.text(1));
    return out;
}

std::vector<TagCount> IndexStore::tagCounts()
{
    // Notes per tag key, plus the most common spelling.
    struct Acc {
        QHash<QString, int> spellings;
        int notes = 0;
    };
    QHash<QString, Acc> acc;
    sql::Statement s =
        db_.prepare("SELECT tag_key, tag, COUNT(*), COUNT(DISTINCT file_id) FROM tags GROUP BY tag_key, tag");
    while (s.step()) {
        Acc& a = acc[s.text(0)];
        a.spellings[s.text(1)] += s.int32(2);
    }
    sql::Statement n = db_.prepare("SELECT tag_key, COUNT(DISTINCT file_id) FROM tags GROUP BY tag_key");
    while (n.step())
        acc[n.text(0)].notes = n.int32(1);
    std::vector<TagCount> out;
    out.reserve(static_cast<std::size_t>(acc.size()));
    for (auto it = acc.cbegin(); it != acc.cend(); ++it) {
        QString best;
        int bestCount = -1;
        for (auto sp = it->spellings.cbegin(); sp != it->spellings.cend(); ++sp) {
            if (sp.value() > bestCount || (sp.value() == bestCount && sp.key() < best)) {
                best = sp.key();
                bestCount = sp.value();
            }
        }
        out.push_back({best, it->notes});
    }
    std::sort(out.begin(), out.end(), [](const TagCount& a, const TagCount& b) {
        return a.tag.compare(b.tag, Qt::CaseInsensitive) < 0;
    });
    return out;
}

QStringList IndexStore::filesWithTag(const QString& tagKey)
{
    QStringList out;
    sql::Statement s = db_.prepare(
        "SELECT DISTINCT f.path FROM tags t JOIN files f ON f.id = t.file_id "
        "WHERE t.tag_key = ?1 OR substr(t.tag_key, 1, length(?1) + 1) = ?1 || '/' ORDER BY f.path");
    s.bind(1, tagKey.toCaseFolded());
    while (s.step())
        out.append(s.text(0));
    return out;
}

std::vector<HeadingInfo> IndexStore::headings(const QString& path)
{
    std::vector<HeadingInfo> out;
    sql::Statement s = db_.prepare(
        "SELECT h.level, h.text, h.line FROM headings h JOIN files f ON f.id = h.file_id WHERE f.path = ? "
        "ORDER BY h.line");
    s.bind(1, path);
    while (s.step())
        out.push_back({s.int32(0), s.text(1), s.int32(2)});
    return out;
}

std::vector<BlockInfo> IndexStore::blocks(const QString& path)
{
    std::vector<BlockInfo> out;
    sql::Statement s = db_.prepare(
        "SELECT b.block_id, b.start_line, b.end_line FROM blocks b JOIN files f ON f.id = b.file_id "
        "WHERE f.path = ? ORDER BY b.start_line");
    s.bind(1, path);
    while (s.step())
        out.push_back({s.text(0), s.int32(1), s.int32(2)});
    return out;
}

QString IndexStore::body(const QString& path)
{
    sql::Statement s =
        db_.prepare("SELECT x.body FROM fts x JOIN files f ON f.id = x.rowid WHERE f.path = ?");
    s.bind(1, path);
    return s.step() ? s.text(0) : QString();
}

std::vector<TaskInfo> IndexStore::tasks()
{
    std::vector<TaskInfo> out;
    sql::Statement s = db_.prepare(
        "SELECT f.path, t.line, t.done, t.text, t.due FROM tasks t JOIN files f ON f.id = t.file_id "
        "ORDER BY f.path, t.line");
    while (s.step())
        out.push_back({s.text(0), s.int32(1), s.int32(2) != 0, s.text(3), s.text(4)});
    return out;
}

QStringList IndexStore::tagKeysOf(const QString& path)
{
    QStringList out;
    sql::Statement s = db_.prepare(
        "SELECT DISTINCT t.tag_key FROM tags t JOIN files f ON f.id = t.file_id WHERE f.path = ?");
    s.bind(1, path);
    while (s.step())
        out.append(s.text(0));
    return out;
}

std::vector<NoteTags> IndexStore::noteTags()
{
    std::vector<NoteTags> out;
    QHash<qint64, std::size_t> byId;
    sql::Statement notes = db_.prepare("SELECT id, path FROM files WHERE kind = 0 ORDER BY path");
    while (notes.step()) {
        byId.insert(notes.int64(0), out.size());
        out.push_back({notes.text(1), {}});
    }
    sql::Statement tags = db_.prepare("SELECT DISTINCT file_id, tag_key FROM tags");
    while (tags.step()) {
        const auto it = byId.constFind(tags.int64(0));
        if (it != byId.constEnd()) out[*it].keys.append(tags.text(1));
    }
    return out;
}

int IndexStore::documentFrequency(const QString& term)
{
    sql::Statement& s = st_->docFreq;
    s.bind(1, term);
    const int n = s.step() ? s.int32(0) : 0;
    s.reset();
    return n;
}

QHash<QString, int> IndexStore::allDocumentFrequencies()
{
    QHash<QString, int> out;
    sql::Statement s = db_.prepare("SELECT term, doc FROM fts_vocab");
    while (s.step())
        out.insert(s.text(0), s.int32(1));
    return out;
}

int IndexStore::noteCount()
{
    sql::Statement s = db_.prepare("SELECT COUNT(*) FROM fts");
    return s.step() ? s.int32(0) : 0;
}

QStringList IndexStore::notesMatchingAny(const QStringList& terms, int limit)
{
    QStringList out;
    if (terms.isEmpty()) return out;
    QStringList quoted;
    for (const QString& t : terms)
        quoted.append(quoteFtsTerm(t));
    sql::Statement s = db_.prepare("SELECT f.path FROM fts JOIN files f ON f.id = fts.rowid WHERE fts MATCH "
                                   "? ORDER BY bm25(fts, 2.0, 1.0) "
                                   "LIMIT ?");
    s.bind(1, u"body:("_s + quoted.join(u" OR "_s) + u')').bind(2, limit);
    while (s.step())
        out.append(s.text(0));
    return out;
}

void IndexStore::forEachBody(const std::function<void(const QString&, const QString&)>& fn)
{
    sql::Statement s = db_.prepare("SELECT f.path, x.body FROM fts x JOIN files f ON f.id = x.rowid");
    while (s.step())
        fn(s.text(0), s.text(1));
}

QString IndexStore::ftsQuery(const QString& userQuery)
{
    QStringList terms;
    QString cur;
    bool inQuote = false;
    QList<bool> quoted;
    for (QChar c : userQuery) {
        if (c == u'"') {
            if (inQuote) {
                if (!cur.trimmed().isEmpty()) {
                    terms.append(cur.trimmed());
                    quoted.append(true);
                }
                cur.clear();
            }
            inQuote = !inQuote;
            continue;
        }
        if (!inQuote && c.isSpace()) {
            if (!cur.isEmpty()) {
                terms.append(cur);
                quoted.append(false);
            }
            cur.clear();
            continue;
        }
        cur += c;
    }
    if (!cur.trimmed().isEmpty()) {
        terms.append(cur.trimmed());
        quoted.append(inQuote);
    }
    QStringList parts;
    for (qsizetype i = 0; i < terms.size(); ++i) {
        QString p = quoteFtsTerm(terms[i]);
        // Prefix-match the last word as you type (not single letters: they expand to most of the index).
        if (i == terms.size() - 1 && !quoted[i] && terms[i].size() >= 2) p += u'*';
        parts.append(p);
    }
    return parts.join(u' ');
}

std::vector<SearchHit> IndexStore::search(const QString& userQueryIn, int limit)
{
    std::vector<SearchHit> out;
    // "#tag" words filter by tag (nested tags included; all must match); the rest is full text.
    QStringList tags;
    QString userQuery;
    for (const QString& word : userQueryIn.split(u' ', Qt::SkipEmptyParts)) {
        if (word.startsWith(u'#') && word.size() > 1 && !word.contains(u'"'))
            tags.append(word.mid(1).toCaseFolded());
        else
            userQuery += word + u' ';
    }
    const QString q = ftsQuery(userQuery.trimmed());
    if (q.isEmpty() && tags.isEmpty()) return out;

    QString tagFilter;
    for (int i = 0; i < tags.size(); ++i) {
        const QString p = u"?"_s + QString::number(i + 3);
        tagFilter += u" AND f.id IN (SELECT file_id FROM tags WHERE tag_key = "_s + p +
                     u" OR substr(tag_key, 1, length("_s + p + u") + 1) = "_s + p + u" || '/')"_s;
    }
    // Rank first (cheap), then build snippets only for the top results: snippet()
    // re-tokenises each document and dominated the cost when run on every row.
    constexpr int kSnippets = 30;
    const QString sqlText =
        q.isEmpty()
            ? u"SELECT f.path, f.id, 0.0 FROM files f WHERE f.kind = 0 AND ?1 = ?1"_s + tagFilter +
                  u" ORDER BY f.path LIMIT ?2"_s
            : u"SELECT f.path, fts.rowid, bm25(fts, 5.0, 1.0) AS r FROM fts JOIN files f ON f.id = fts.rowid "
              "WHERE fts MATCH ?1"_s +
                  tagFilter + u" ORDER BY r LIMIT ?2"_s;
    const QByteArray sqlUtf8 = sqlText.toUtf8();
    sql::Statement rank = db_.prepare(sqlUtf8.constData());
    rank.bind(1, q.isEmpty() ? u"x"_s : q).bind(2, limit);
    for (int i = 0; i < tags.size(); ++i)
        rank.bind(i + 3, tags[i]);
    std::vector<qint64> ids;
    while (rank.step()) {
        out.push_back({rank.text(0), QString(), rank.real(2)});
        ids.push_back(rank.int64(1));
    }
    sql::Statement body = db_.prepare("SELECT body FROM fts WHERE rowid = ?");
    const QStringList terms = snippetTerms(userQuery);
    for (std::size_t i = 0; i < out.size() && i < static_cast<std::size_t>(kSnippets); ++i) {
        body.bind(1, ids[i]);
        if (body.step())
            out[i].snippet = makeSnippet(q.isEmpty() ? sim::contentText(body.text(0)) : body.text(0), terms);
        body.reset();
    }
    return out;
}

QStringList IndexStore::ftsCandidates(const QString& phrase, int limit)
{
    QStringList out;
    sql::Statement s =
        db_.prepare("SELECT f.path FROM fts JOIN files f ON f.id = fts.rowid WHERE fts MATCH ? LIMIT ?");
    s.bind(1, u"body:"_s + quoteFtsTerm(phrase)).bind(2, limit);
    while (s.step())
        out.append(s.text(0));
    return out;
}

} // namespace mn
