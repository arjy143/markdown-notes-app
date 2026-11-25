#include "IndexStore.h"

#include "Similarity.h"
#include "core/links/RenamePlanner.h"
#include "core/vault/Paths.h"

#include <QCryptographicHash>
#include <QDate>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QStandardPaths>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <type_traits>

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
-- Single-value frontmatter properties (status, summary, categories...), for the home page.
CREATE TABLE props(file_id INTEGER NOT NULL, key TEXT NOT NULL, value TEXT NOT NULL);
CREATE INDEX props_file ON props(file_id);
CREATE INDEX props_key ON props(key);
CREATE TABLE tasks(file_id INTEGER NOT NULL, line INTEGER NOT NULL, done INTEGER NOT NULL, text TEXT NOT NULL,
                   due TEXT NOT NULL);
CREATE INDEX tasks_file ON tasks(file_id);
CREATE VIRTUAL TABLE fts USING fts5(title, body, tokenize = 'unicode61 remove_diacritics 2');
CREATE VIRTUAL TABLE fts_vocab USING fts5vocab(fts, 'row');
CREATE TABLE chunks(id INTEGER PRIMARY KEY, file_id INTEGER NOT NULL, start_line INTEGER NOT NULL,
                    end_line INTEGER NOT NULL, context TEXT NOT NULL, hash BLOB NOT NULL);
CREATE INDEX chunks_file ON chunks(file_id);
CREATE INDEX chunks_hash ON chunks(hash);
-- Passages. Contentless: the text is already in fts.body, sliced by line range when shown.
CREATE VIRTUAL TABLE chunk_fts USING fts5(context, body, content = '', contentless_delete = 1,
                                          tokenize = 'unicode61 remove_diacritics 2');
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

// Builds a snippet around the first match, marking matches with \x01 ... \x02:
// up to `before` characters before it, `length` in all.
QString makeSnippet(const QString& body, const QStringList& terms, qsizetype before = 60,
                    qsizetype length = 200)
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
    qsizetype start = std::max<qsizetype>(0, first - before);
    while (start > 0 && start < body.size() && !body[start - 1].isSpace())
        --start;
    const qsizetype end = std::min<qsizetype>(body.size(), start + length);
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

// Splits "#tag" words (case-folded, without '#') from the full-text words.
QString splitTags(const QString& userQuery, QStringList* tags)
{
    QString rest;
    for (const QString& word : userQuery.split(u' ', Qt::SkipEmptyParts)) {
        if (word.startsWith(u'#') && word.size() > 1 && !word.contains(u'"'))
            tags->append(word.mid(1).toCaseFolded());
        else
            rest += word + u' ';
    }
    return rest.trimmed();
}

// Lines a..b (0-based, inclusive) of `body`, without '\r'.
QString sliceLines(const QString& body, int a, int b)
{
    qsizetype pos = 0;
    for (int i = 0; i < a && pos >= 0; ++i) {
        pos = body.indexOf(u'\n', pos);
        if (pos >= 0) ++pos;
    }
    if (pos < 0) return {};
    qsizetype end = pos;
    for (int i = a; i <= b && end >= 0; ++i) {
        end = body.indexOf(u'\n', end);
        if (end >= 0 && i < b) ++end;
    }
    QString s = body.mid(pos, end < 0 ? -1 : end - pos);
    s.remove(u'\r');
    return s;
}

// The date in a daily note's name (YYYY-MM-DD anywhere in it), else the modification time.
qint64 passageDate(const QString& path, qint64 mtimeMs)
{
    static const QRegularExpression re(uR"((\d{4})-(\d{2})-(\d{2}))"_s);
    const QRegularExpressionMatch m = re.match(paths::linkName(path));
    if (m.hasMatch()) {
        const QDate d(m.captured(1).toInt(), m.captured(2).toInt(), m.captured(3).toInt());
        if (d.isValid()) return QDateTime(d, QTime(12, 0)).toMSecsSinceEpoch();
    }
    return mtimeMs;
}

constexpr qsizetype kMaxPassageText = 4000;

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
        attachEmbeddings(dbPath);
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
    attachEmbeddings(dbPath);
    prepareStatements();
    return true;
}

void IndexStore::attachEmbeddings(const QString& dbPath)
{
    // A separate file: rebuilding the index keeps the vectors (they are keyed by content),
    // and deleting it only costs re-embedding.
    const QString path = QFileInfo(dbPath).absolutePath() + u"/embeddings.sqlite"_s;
    sql::Statement attach = db_.prepare("ATTACH DATABASE ? AS emb");
    attach.bind(1, path);
    if (!attach.exec()) return;
    int version = -1;
    {
        sql::Statement s = db_.prepare("PRAGMA emb.user_version");
        if (s.step()) version = s.int32(0);
    }
    if (version != kEmbeddingsVersion) {
        db_.exec("DROP TABLE IF EXISTS emb.vectors");
        // A rowid table: rows of under 1 KB sit inline in the pages, so a search scans sequentially.
        db_.exec("CREATE TABLE emb.vectors(id INTEGER PRIMARY KEY, hash BLOB NOT NULL, model TEXT NOT NULL, "
                 "vec BLOB NOT NULL)");
        db_.exec("CREATE UNIQUE INDEX emb.vectors_key ON vectors(hash, model)");
        const QByteArray pragma = "PRAGMA emb.user_version=" + QByteArray::number(kEmbeddingsVersion);
        db_.exec(pragma.constData());
    }
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
    st_->insertProp = db_.prepare("INSERT INTO props(file_id, key, value) VALUES (?, ?, ?)");
    st_->insertFts = db_.prepare("INSERT INTO fts(rowid, title, body) VALUES (?, ?, ?)");
    st_->updateStamp = db_.prepare("UPDATE files SET size = ?, mtime = ? WHERE path = ?");
    st_->insertTask = db_.prepare("INSERT INTO tasks(file_id, line, done, text, due) VALUES (?, ?, ?, ?, ?)");
    st_->docFreq = db_.prepare("SELECT doc FROM fts_vocab WHERE term = ?");
    st_->insertChunk = db_.prepare(
        "INSERT INTO chunks(file_id, start_line, end_line, context, hash) VALUES (?, ?, ?, ?, ?)");
    st_->insertChunkFts = db_.prepare("INSERT INTO chunk_fts(rowid, context, body) VALUES (?, ?, ?)");
    static const char* const deletes[] = {
        "DELETE FROM links WHERE file_id = ?",
        "DELETE FROM tags WHERE file_id = ?",
        "DELETE FROM headings WHERE file_id = ?",
        "DELETE FROM blocks WHERE file_id = ?",
        "DELETE FROM aliases WHERE file_id = ?",
        "DELETE FROM props WHERE file_id = ?",
        "DELETE FROM fts WHERE rowid = ?",
        "DELETE FROM tasks WHERE file_id = ?",
        "DELETE FROM chunk_fts WHERE rowid IN (SELECT id FROM chunks WHERE file_id = ?)",
        "DELETE FROM chunks WHERE file_id = ?",
        "DELETE FROM files WHERE id = ?"};
    static_assert(std::size(deletes) == std::extent_v<decltype(Statements::deletes)>);
    for (std::size_t i = 0; i < std::size(deletes); ++i)
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
                         const NoteMetadata& meta, const QString& body, const std::vector<Chunk>& chunks,
                         bool known)
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
    constexpr qsizetype kMaxPropValue = 500;
    sql::Statement& prop = st_->insertProp;
    for (const FrontmatterField& f : meta.frontmatter.fields) {
        if (f.isList || f.complex || f.key == u"tags" || f.key == u"tag" || f.key == u"aliases" ||
            f.key == u"alias")
            continue;
        prop.bind(1, id).bind(2, f.key).bind(3, f.value.left(kMaxPropValue));
        prop.exec();
    }
    sql::Statement& task = st_->insertTask;
    for (const TaskItem& t : meta.tasks) {
        task.bind(1, id).bind(2, t.line).bind(3, t.done ? 1 : 0).bind(4, t.text).bind(5, t.due);
        task.exec();
    }
    const QString title = paths::linkName(path);
    sql::Statement& chunk = st_->insertChunk;
    sql::Statement& chunkFts = st_->insertChunkFts;
    for (const Chunk& c : chunks) {
        chunk.bind(1, id).bind(2, c.startLine).bind(3, c.endLine).bind(4, c.context);
        chunk.bindBlob(5, passageHash(title, c.context, c.text));
        if (!chunk.exec()) continue;
        // The note's name counts as context, so a passage in "Sleep.md" matches "sleep".
        chunkFts.bind(1, db_.lastInsertId()).bind(2, title + u' ' + c.context).bind(3, c.text);
        chunkFts.exec();
    }
    sql::Statement& fts = st_->insertFts;
    fts.bind(1, id).bind(2, title).bind(3, body);
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
    const QString userQuery = splitTags(userQueryIn, &tags);
    const QString q = ftsQuery(userQuery);
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

void IndexStore::fillPassages(std::vector<Passage>& passages, const std::vector<qint64>& fileIds,
                              const QStringList& terms, qsizetype snippetLength)
{
    sql::Statement body = db_.prepare("SELECT body FROM fts WHERE rowid = ?");
    QHash<qint64, QString> bodies;
    for (std::size_t i = 0; i < passages.size(); ++i) {
        auto it = bodies.constFind(fileIds[i]);
        if (it == bodies.constEnd()) {
            body.bind(1, fileIds[i]);
            it = bodies.insert(fileIds[i], body.step() ? body.text(0) : QString());
            body.reset();
        }
        Passage& p = passages[i];
        p.text = sliceLines(*it, p.startLine, p.endLine);
        if (p.text.size() > kMaxPassageText) p.text = p.text.left(kMaxPassageText) + u'…';
        p.snippet = makeSnippet(p.text, terms, snippetLength / 4, snippetLength);
    }
}

std::vector<Passage> IndexStore::searchPassages(const QString& userQueryIn, int limit)
{
    std::vector<Passage> out;
    QStringList tags;
    const QString userQuery = splitTags(userQueryIn, &tags);
    const QString q = ftsQuery(userQuery);
    if (q.isEmpty() && tags.isEmpty()) return out;
    // A tag counts for a passage if it's written inside it, or in the note's frontmatter (line -1).
    QString tagFilter;
    for (int i = 0; i < tags.size(); ++i) {
        const QString p = u"?"_s + QString::number(i + 3);
        tagFilter += u" AND EXISTS (SELECT 1 FROM tags t WHERE t.file_id = c.file_id AND (t.tag_key = "_s +
                     p + u" OR substr(t.tag_key, 1, length("_s + p + u") + 1) = "_s + p +
                     u" || '/') AND (t.line < 0 OR t.line BETWEEN c.start_line AND c.end_line))"_s;
    }
    const QString cols = u"SELECT c.file_id, f.path, c.start_line, c.end_line, c.context, f.mtime, "_s;
    const QString sqlText =
        q.isEmpty()
            ? cols + u"0.0 FROM chunks c JOIN files f ON f.id = c.file_id WHERE ?1 = ?1"_s + tagFilter +
                  u" ORDER BY f.path, c.start_line LIMIT ?2"_s
            : cols +
                  u"bm25(chunk_fts, 2.0, 1.0) AS r FROM chunk_fts JOIN chunks c ON c.id = chunk_fts.rowid "
                  "JOIN files f ON f.id = c.file_id WHERE chunk_fts MATCH ?1"_s +
                  tagFilter + u" ORDER BY r LIMIT ?2"_s;
    const QByteArray sqlUtf8 = sqlText.toUtf8();
    sql::Statement s = db_.prepare(sqlUtf8.constData());
    s.bind(1, q.isEmpty() ? u"x"_s : q).bind(2, limit);
    for (int i = 0; i < tags.size(); ++i)
        s.bind(i + 3, tags[i]);
    std::vector<qint64> ids;
    while (s.step()) {
        Passage p;
        p.path = s.text(1);
        p.startLine = s.int32(2);
        p.endLine = s.int32(3);
        p.context = s.text(4);
        p.dateMs = passageDate(p.path, s.int64(5));
        p.score = s.real(6);
        out.push_back(std::move(p));
        ids.push_back(s.int64(0));
    }
    fillPassages(out, ids, snippetTerms(userQuery), 480);
    return out;
}

std::vector<Passage> IndexStore::passagesMatchingAny(const QStringList& terms, const QString& excludePath,
                                                     int limit)
{
    std::vector<Passage> out;
    if (terms.isEmpty()) return out;
    QStringList quoted;
    for (const QString& t : terms)
        quoted.append(quoteFtsTerm(t));
    sql::Statement s =
        db_.prepare("SELECT c.file_id, f.path, c.start_line, c.end_line, c.context, f.mtime, bm25(chunk_fts, "
                    "2.0, 1.0) AS r "
                    "FROM chunk_fts JOIN chunks c ON c.id = chunk_fts.rowid JOIN files f ON f.id = c.file_id "
                    "WHERE chunk_fts MATCH ?1 AND f.path <> ?2 ORDER BY r LIMIT ?3");
    s.bind(1, quoted.join(u" OR "_s)).bind(2, excludePath).bind(3, limit);
    std::vector<qint64> ids;
    while (s.step()) {
        Passage p;
        p.path = s.text(1);
        p.startLine = s.int32(2);
        p.endLine = s.int32(3);
        p.context = s.text(4);
        p.dateMs = passageDate(p.path, s.int64(5));
        p.score = s.real(6);
        out.push_back(std::move(p));
        ids.push_back(s.int64(0));
    }
    fillPassages(out, ids, terms, 240); // shown in the side panel
    return out;
}

namespace {

std::vector<float> normalized(const std::vector<float>& v)
{
    double n = 0;
    for (float x : v)
        n += static_cast<double>(x) * x;
    const double inv = n > 0 ? 1.0 / std::sqrt(n) : 0.0;
    std::vector<float> out(v.size());
    for (std::size_t i = 0; i < v.size(); ++i)
        out[i] = static_cast<float>(v[i] * inv);
    return out;
}

// A unit-length vector as a float scale followed by one int8 per dimension:
// component i = byte[i] * scale / 127.
QByteArray packVector(const std::vector<float>& v)
{
    const std::vector<float> u = normalized(v);
    float scale = 0;
    for (float x : u)
        scale = std::max(scale, std::abs(x));
    if (scale <= 0) scale = 1;
    QByteArray out(static_cast<qsizetype>(sizeof(float) + u.size()), Qt::Uninitialized);
    std::memcpy(out.data(), &scale, sizeof(float));
    auto* q = reinterpret_cast<qint8*>(out.data() + sizeof(float));
    for (std::size_t i = 0; i < u.size(); ++i)
        q[i] = static_cast<qint8>(std::clamp(std::lround(u[i] / scale * 127.0f), -127L, 127L));
    return out;
}

std::vector<float> unpackVector(const void* data, qsizetype bytes)
{
    if (bytes <= static_cast<qsizetype>(sizeof(float))) return {};
    float scale = 0;
    std::memcpy(&scale, data, sizeof(float));
    const auto* q = static_cast<const qint8*>(data) + sizeof(float);
    std::vector<float> v(static_cast<std::size_t>(bytes) - sizeof(float));
    for (std::size_t i = 0; i < v.size(); ++i)
        v[i] = static_cast<float>(q[i]) * scale / 127.0f;
    return v;
}

} // namespace

EmbeddingBatch IndexStore::pendingEmbeddings(const QString& model, int limit)
{
    EmbeddingBatch b;
    {
        sql::Statement s = db_.prepare("SELECT COUNT(DISTINCT hash) FROM chunks");
        if (s.step()) b.total = s.int32(0);
    }
    {
        sql::Statement s = db_.prepare("SELECT COUNT(*) FROM emb.vectors v WHERE v.model = ? AND EXISTS "
                                       "(SELECT 1 FROM chunks c WHERE c.hash = v.hash)");
        s.bind(1, model);
        if (s.step()) b.done = s.int32(0);
    }
    sql::Statement s =
        db_.prepare("SELECT c.hash, f.path, c.start_line, c.end_line, c.context, c.file_id FROM chunks c "
                    "JOIN files f ON f.id = c.file_id "
                    "WHERE NOT EXISTS (SELECT 1 FROM emb.vectors v WHERE v.hash = c.hash AND v.model = ?1) "
                    "GROUP BY c.hash LIMIT ?2");
    s.bind(1, model).bind(2, limit);
    std::vector<Passage> passages;
    std::vector<qint64> ids;
    std::vector<QByteArray> hashes;
    while (s.step()) {
        Passage p;
        p.path = s.text(1);
        p.startLine = s.int32(2);
        p.endLine = s.int32(3);
        p.context = s.text(4);
        passages.push_back(std::move(p));
        ids.push_back(s.int64(5));
        hashes.push_back(s.blob(0));
    }
    fillPassages(passages, ids, {}, 0);
    for (std::size_t i = 0; i < passages.size(); ++i) {
        const Passage& p = passages[i];
        b.jobs.push_back({hashes[i], passageEmbeddingText(paths::linkName(p.path), p.context, p.text)});
    }
    return b;
}

bool IndexStore::storeEmbeddings(const QString& model, const std::vector<QByteArray>& hashes,
                                 const std::vector<std::vector<float>>& vectors)
{
    if (hashes.size() != vectors.size()) return false;
    db_.exec("BEGIN");
    sql::Statement s = db_.prepare("INSERT OR REPLACE INTO emb.vectors(hash, model, vec) VALUES (?, ?, ?)");
    bool ok = true;
    for (std::size_t i = 0; i < hashes.size(); ++i) {
        if (vectors[i].empty()) continue;
        s.bindBlob(1, hashes[i]).bind(2, model).bindBlob(3, packVector(vectors[i]));
        ok = s.exec() && ok;
    }
    db_.exec("COMMIT");
    return ok;
}

std::vector<float> IndexStore::embeddingFor(const QString& model, const QByteArray& hash)
{
    sql::Statement s = db_.prepare("SELECT vec FROM emb.vectors WHERE hash = ? AND model = ?");
    s.bindBlob(1, hash).bind(2, model);
    if (!s.step()) return {};
    const void* data = nullptr;
    const qsizetype bytes = s.blobView(0, &data);
    return unpackVector(data, bytes);
}

int IndexStore::embeddingCount(const QString& model)
{
    sql::Statement s = db_.prepare("SELECT COUNT(*) FROM emb.vectors WHERE model = ?");
    s.bind(1, model);
    return s.step() ? s.int32(0) : 0;
}

std::vector<Passage> IndexStore::semanticPassages(const QString& model, const std::vector<float>& queryIn,
                                                  const QString& excludePath, int limit)
{
    std::vector<Passage> out;
    if (queryIn.empty() || limit <= 0) return out;
    // The query is quantised like the stored vectors: the scan is then integer dot
    // products (four independent sums, so the adds don't wait on each other).
    const QByteArray packed = packVector(queryIn);
    float qScale = 0;
    std::memcpy(&qScale, packed.constData(), sizeof(float));
    const auto* q = reinterpret_cast<const qint8*>(packed.constData() + sizeof(float));
    const std::size_t dim = queryIn.size();
    // Brute force over every vector, read from disk: nothing stays in memory between searches.
    using Hit = std::pair<float, QByteArray>;
    std::vector<Hit> top;                                             // min-heap of the best
    const std::size_t keep = static_cast<std::size_t>(limit) * 2 + 8; // room for excluded or stale ones
    sql::Statement s = db_.prepare("SELECT hash, vec FROM emb.vectors WHERE model = ?");
    s.bind(1, model);
    auto worse = [](const Hit& a, const Hit& b) { return a.first > b.first; };
    while (s.step()) {
        const void* data = nullptr;
        const qsizetype bytes = s.blobView(1, &data);
        if (bytes != static_cast<qsizetype>(sizeof(float)) + static_cast<qsizetype>(dim))
            continue; // another model's size
        float scale = 0;
        std::memcpy(&scale, data, sizeof(float));
        const auto* v = static_cast<const qint8*>(data) + sizeof(float);
        std::int32_t s0 = 0;
        std::int32_t s1 = 0;
        std::int32_t s2 = 0;
        std::int32_t s3 = 0;
        std::size_t i = 0;
        for (; i + 4 <= dim; i += 4) {
            s0 += q[i] * v[i];
            s1 += q[i + 1] * v[i + 1];
            s2 += q[i + 2] * v[i + 2];
            s3 += q[i + 3] * v[i + 3];
        }
        for (; i < dim; ++i)
            s0 += q[i] * v[i];
        const float dot = static_cast<float>(s0 + s1 + s2 + s3) * (qScale / 127.0f) * (scale / 127.0f);
        if (top.size() < keep) {
            top.emplace_back(dot, s.blob(0));
            std::push_heap(top.begin(), top.end(), worse);
        } else if (dot > top.front().first) {
            std::pop_heap(top.begin(), top.end(), worse);
            top.back() = {dot, s.blob(0)};
            std::push_heap(top.begin(), top.end(), worse);
        }
    }
    std::sort(top.begin(), top.end(), [](const Hit& a, const Hit& b) { return a.first > b.first; });
    sql::Statement c =
        db_.prepare("SELECT c.file_id, f.path, c.start_line, c.end_line, c.context, f.mtime "
                    "FROM chunks c JOIN files f ON f.id = c.file_id WHERE c.hash = ? AND f.path <> ?");
    std::vector<qint64> ids;
    for (const Hit& h : top) {
        c.bindBlob(1, h.second).bind(2, excludePath);
        while (c.step() && static_cast<int>(out.size()) < limit) {
            Passage p;
            p.path = c.text(1);
            p.startLine = c.int32(2);
            p.endLine = c.int32(3);
            p.context = c.text(4);
            p.dateMs = passageDate(p.path, c.int64(5));
            p.score = h.first;
            out.push_back(std::move(p));
            ids.push_back(c.int64(0));
        }
        c.reset();
        if (static_cast<int>(out.size()) >= limit) break;
    }
    fillPassages(out, ids, {}, 480);
    return out;
}

int IndexStore::pruneEmbeddings()
{
    db_.exec("DELETE FROM emb.vectors WHERE hash NOT IN (SELECT hash FROM chunks)");
    sql::Statement s = db_.prepare("SELECT changes()");
    return s.step() ? s.int32(0) : 0;
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

std::vector<std::pair<QString, QString>> IndexStore::filesWithProperty(const QString& key)
{
    std::vector<std::pair<QString, QString>> out;
    sql::Statement s = db_.prepare("SELECT f.path, p.value FROM props p JOIN files f ON f.id = p.file_id "
                                   "WHERE p.key = ? AND p.value != '' "
                                   "ORDER BY f.path");
    s.bind(1, key);
    while (s.step())
        out.emplace_back(s.text(0), s.text(1));
    return out;
}

QHash<QString, QHash<QString, QString>> IndexStore::properties(const QStringList& keys)
{
    QHash<QString, QHash<QString, QString>> out;
    sql::Statement s = db_.prepare(
        "SELECT f.path, p.key, p.value FROM props p JOIN files f ON f.id = p.file_id WHERE p.value != ''");
    while (s.step()) {
        const QString key = s.text(1);
        if (keys.contains(key)) out[s.text(0)].insert(key, s.text(2));
    }
    return out;
}

QHash<QString, qint64> IndexStore::noteTimes()
{
    QHash<QString, qint64> out;
    sql::Statement s = db_.prepare("SELECT path, mtime FROM files WHERE kind = ?");
    s.bind(1, static_cast<int>(FileKind::Note));
    while (s.step())
        out.insert(s.text(0), s.int64(1));
    return out;
}

std::vector<StoredLink> IndexStore::linksFrom(const QString& path)
{
    std::vector<StoredLink> out;
    sql::Statement s = db_.prepare(
        "SELECT l.linkpath, l.subpath, l.line, l.embed FROM links l JOIN files f ON f.id = l.file_id "
        "WHERE f.path = ? AND l.linkpath != ''");
    s.bind(1, path);
    while (s.step())
        out.push_back({path, s.text(0), s.text(1), s.int32(2), s.int32(3) != 0});
    return out;
}

QStringList IndexStore::coTags(const QString& tag, const QStringList& exclude, int limit)
{
    QStringList excluded;
    for (const QString& e : exclude)
        excluded.append(e.toCaseFolded());
    excluded.append(tag.toCaseFolded());
    sql::Statement s = db_.prepare(
        "SELECT t2.tag_key, max(t2.tag), count(DISTINCT t2.file_id) AS n FROM tags t1 "
        "JOIN tags t2 ON t2.file_id = t1.file_id WHERE t1.tag_key = ? GROUP BY t2.tag_key ORDER BY n DESC, "
        "t2.tag_key LIMIT ?");
    s.bind(1, tag.toCaseFolded()).bind(2, limit + static_cast<int>(excluded.size()));
    QStringList out;
    while (s.step() && out.size() < limit) {
        if (!excluded.contains(s.text(0))) out.append(s.text(1));
    }
    return out;
}

} // namespace mn
