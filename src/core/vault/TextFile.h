#pragma once

#include <QByteArray>
#include <QString>
#include <QStringView>

// Safe reading and writing of note files (compat spec §7).
//
// A note is loaded into a "buffer form": '\n' line breaks, no BOM. The original
// bytes and the detected format are kept so the buffer can be written back
// byte-for-byte when nothing changed, and with the original BOM, line-ending
// style and trailing newline when it did.
namespace mn {

enum class LineEnding : unsigned char { LF, CRLF };

struct TextFormat {
    bool bom = false;
    LineEnding eol = LineEnding::LF; // dominant style
    bool mixedEol = false;           // file mixes LF and CRLF
    bool loneCr = false;             // file contains a '\r' not followed by '\n'
};

struct FileStamp {
    qint64 size = -1;
    qint64 mtimeMs = 0;
    bool exists() const { return size >= 0; }
    friend bool operator==(const FileStamp&, const FileStamp&) = default;
};

enum class LoadStatus : unsigned char { Ok, NotFound, ReadError, InvalidUtf8, Binary };

struct LoadedText {
    LoadStatus status = LoadStatus::NotFound;
    QString error; // user-facing reason when status != Ok
    QString text;  // buffer form
    TextFormat format;
    QByteArray raw; // exact bytes on disk
    FileStamp stamp;
    // True when encodeText(text, format, raw) reproduces `raw` exactly.
    bool roundTrips = false;
    // True if the buffer may be edited and saved. False for binary or invalid UTF-8.
    bool editable() const { return status == LoadStatus::Ok; }
};

struct WriteResult {
    bool ok = false;
    QString error;
};

FileStamp statFile(const QString& absPath);

// Pure: decodes bytes into buffer form and detects the format.
LoadedText decodeText(const QByteArray& raw);

// Reads and decodes a file. Never modifies it.
LoadedText loadTextFile(const QString& absPath);

// Encodes a buffer back to bytes using `format`. For files with mixed line
// endings, `originalRaw` is used so unchanged leading and trailing lines keep
// their original endings; changed lines use the dominant style.
QByteArray encodeText(QStringView text, const TextFormat& format, const QByteArray& originalRaw = {});

// Atomic write through QSaveFile. Never leaves a partially written file.
WriteResult writeFileAtomic(const QString& absPath, const QByteArray& data);

// Format for brand-new files: UTF-8, LF, no BOM.
inline TextFormat newFileFormat()
{
    return {};
}

} // namespace mn
