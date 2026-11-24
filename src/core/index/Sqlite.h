#pragma once

#include <QByteArray>
#include <QString>

struct sqlite3;
struct sqlite3_stmt;

// Thin RAII wrapper over the SQLite C API (the QtSql module isn't used).
namespace mn::sql {

class Statement {
public:
    Statement() = default;
    Statement(sqlite3* db, const char* sql);
    ~Statement();
    Statement(Statement&& o) noexcept;
    Statement& operator=(Statement&& o) noexcept;
    Statement(const Statement&) = delete;
    Statement& operator=(const Statement&) = delete;

    bool valid() const { return stmt_ != nullptr; }
    Statement& bind(int index, const QString& v);
    Statement& bind(int index, qint64 v);
    Statement& bind(int index, int v) { return bind(index, static_cast<qint64>(v)); }
    Statement& bindBlob(int index, const QByteArray& v);
    Statement& bindNull(int index);

    bool step(); // true while a row is available
    bool exec(); // runs to completion; false on error
    void reset();

    QString text(int col) const;
    qint64 int64(int col) const;
    int int32(int col) const { return static_cast<int>(int64(col)); }
    double real(int col) const;
    QByteArray blob(int col) const;
    // The column's bytes without a copy (valid until the next step or reset); returns the size.
    qsizetype blobView(int col, const void** data) const;
    bool isNull(int col) const;

private:
    sqlite3_stmt* stmt_ = nullptr;
};

class Database {
public:
    Database() = default;
    ~Database();
    Database(const Database&) = delete;
    Database& operator=(const Database&) = delete;

    bool open(const QString& path, QString* error);
    void close();
    bool isOpen() const { return db_ != nullptr; }
    bool exec(const char* sql, QString* error = nullptr);
    Statement prepare(const char* sql) const { return Statement(db_, sql); }
    qint64 lastInsertId() const;
    QString lastError() const;
    sqlite3* handle() const { return db_; }

private:
    sqlite3* db_ = nullptr;
};

class Transaction {
public:
    explicit Transaction(Database& db) : db_(db) { active_ = db_.exec("BEGIN IMMEDIATE"); }
    ~Transaction()
    {
        if (active_) db_.exec("ROLLBACK");
    }
    Transaction(const Transaction&) = delete;
    Transaction& operator=(const Transaction&) = delete;
    bool commit()
    {
        if (!active_) return false;
        active_ = false;
        return db_.exec("COMMIT");
    }

private:
    Database& db_;
    bool active_ = false;
};

} // namespace mn::sql
