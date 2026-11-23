#include "Sqlite.h"

#include <QFile>

#include <sqlite3.h>

namespace mn::sql {

Statement::Statement(sqlite3* db, const char* sql)
{
    if (db == nullptr) return;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt_, nullptr) != SQLITE_OK) {
        sqlite3_finalize(stmt_);
        stmt_ = nullptr;
    }
}

Statement::~Statement()
{
    sqlite3_finalize(stmt_);
}

Statement::Statement(Statement&& o) noexcept : stmt_(o.stmt_)
{
    o.stmt_ = nullptr;
}

Statement& Statement::operator=(Statement&& o) noexcept
{
    if (this != &o) {
        sqlite3_finalize(stmt_);
        stmt_ = o.stmt_;
        o.stmt_ = nullptr;
    }
    return *this;
}

Statement& Statement::bind(int index, const QString& v)
{
    if (stmt_ != nullptr) {
        const QByteArray utf8 = v.toUtf8();
        sqlite3_bind_text(stmt_, index, utf8.constData(), static_cast<int>(utf8.size()), SQLITE_TRANSIENT);
    }
    return *this;
}

Statement& Statement::bind(int index, qint64 v)
{
    if (stmt_ != nullptr) sqlite3_bind_int64(stmt_, index, v);
    return *this;
}

Statement& Statement::bindBlob(int index, const QByteArray& v)
{
    if (stmt_ != nullptr)
        sqlite3_bind_blob(stmt_, index, v.constData(), static_cast<int>(v.size()), SQLITE_TRANSIENT);
    return *this;
}

Statement& Statement::bindNull(int index)
{
    if (stmt_ != nullptr) sqlite3_bind_null(stmt_, index);
    return *this;
}

bool Statement::step()
{
    return stmt_ != nullptr && sqlite3_step(stmt_) == SQLITE_ROW;
}

bool Statement::exec()
{
    if (stmt_ == nullptr) return false;
    int rc = SQLITE_ROW;
    while (rc == SQLITE_ROW)
        rc = sqlite3_step(stmt_);
    const bool ok = rc == SQLITE_DONE;
    sqlite3_reset(stmt_);
    sqlite3_clear_bindings(stmt_);
    return ok;
}

void Statement::reset()
{
    if (stmt_ == nullptr) return;
    sqlite3_reset(stmt_);
    sqlite3_clear_bindings(stmt_);
}

QString Statement::text(int col) const
{
    const auto* p = reinterpret_cast<const char*>(sqlite3_column_text(stmt_, col));
    const int n = sqlite3_column_bytes(stmt_, col);
    return p == nullptr ? QString() : QString::fromUtf8(p, n);
}

qint64 Statement::int64(int col) const
{
    return sqlite3_column_int64(stmt_, col);
}

double Statement::real(int col) const
{
    return sqlite3_column_double(stmt_, col);
}

QByteArray Statement::blob(int col) const
{
    const void* p = sqlite3_column_blob(stmt_, col);
    const int n = sqlite3_column_bytes(stmt_, col);
    return p == nullptr ? QByteArray() : QByteArray(static_cast<const char*>(p), n);
}

bool Statement::isNull(int col) const
{
    return sqlite3_column_type(stmt_, col) == SQLITE_NULL;
}

Database::~Database()
{
    close();
}

bool Database::open(const QString& path, QString* error)
{
    close();
    const QByteArray p = QFile::encodeName(path);
    const int rc = sqlite3_open_v2(p.constData(), &db_,
                                   SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_NOMUTEX, nullptr);
    if (rc != SQLITE_OK) {
        if (error) *error = QString::fromUtf8(sqlite3_errmsg(db_));
        sqlite3_close(db_);
        db_ = nullptr;
        return false;
    }
    sqlite3_busy_timeout(db_, 2000);
    return true;
}

void Database::close()
{
    if (db_ != nullptr) {
        sqlite3_close_v2(db_);
        db_ = nullptr;
    }
}

bool Database::exec(const char* sql, QString* error)
{
    char* msg = nullptr;
    const int rc = sqlite3_exec(db_, sql, nullptr, nullptr, &msg);
    if (rc != SQLITE_OK && error) *error = QString::fromUtf8(msg ? msg : "unknown error");
    sqlite3_free(msg);
    return rc == SQLITE_OK;
}

qint64 Database::lastInsertId() const
{
    return sqlite3_last_insert_rowid(db_);
}

QString Database::lastError() const
{
    return db_ ? QString::fromUtf8(sqlite3_errmsg(db_)) : QString();
}

} // namespace mn::sql
