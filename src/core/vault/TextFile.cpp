#include "TextFile.h"

#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QStringDecoder>

#include <vector>

using namespace Qt::StringLiterals;

namespace mn {

namespace {

constexpr char kBom[] = "\xEF\xBB\xBF";

struct RawLine {
    QByteArrayView content;
    QByteArrayView ending; // "", "\n" or "\r\n"
};

std::vector<RawLine> splitRawLines(QByteArrayView data)
{
    std::vector<RawLine> lines;
    qsizetype start = 0;
    for (qsizetype i = 0; i < data.size(); ++i) {
        if (data[i] != '\n') continue;
        const bool crlf = i > start && data[i - 1] == '\r';
        const qsizetype contentEnd = crlf ? i - 1 : i;
        lines.push_back(
            {data.sliced(start, contentEnd - start), data.sliced(contentEnd, i + 1 - contentEnd)});
        start = i + 1;
    }
    lines.push_back({data.sliced(start), QByteArrayView()});
    return lines;
}

} // namespace

FileStamp statFile(const QString& absPath)
{
    const QFileInfo fi(absPath);
    if (!fi.exists() || !fi.isFile()) return {};
    return {fi.size(), fi.lastModified().toMSecsSinceEpoch()};
}

LoadedText decodeText(const QByteArray& raw)
{
    LoadedText out;
    out.raw = raw;
    QByteArrayView body(raw);
    if (body.startsWith(QByteArrayView(kBom, 3))) {
        out.format.bom = true;
        body = body.sliced(3);
    }
    if (body.contains('\0')) {
        out.status = LoadStatus::Binary;
        out.error = u"This file contains binary data and can't be edited."_s;
        return out;
    }

    // Keep a second BOM (if any) as U+FEFF so it round-trips.
    QStringDecoder decoder(QStringDecoder::Utf8, QStringDecoder::Flag::ConvertInitialBom);
    QString text = decoder.decode(body);
    if (decoder.hasError()) {
        out.status = LoadStatus::InvalidUtf8;
        out.error = u"Opened read-only: the file is not valid UTF-8."_s;
        out.text = std::move(text);
        return out;
    }

    qsizetype lf = 0;
    qsizetype crlf = 0;
    for (qsizetype i = 0; i < body.size(); ++i) {
        if (body[i] == '\n') {
            if (i > 0 && body[i - 1] == '\r')
                ++crlf;
            else
                ++lf;
        } else if (body[i] == '\r' && (i + 1 >= body.size() || body[i + 1] != '\n')) {
            out.format.loneCr = true;
        }
    }
    out.format.eol = crlf > lf ? LineEnding::CRLF : LineEnding::LF;
    out.format.mixedEol = crlf > 0 && lf > 0;
    if (crlf > 0) text.replace(u"\r\n"_s, u"\n"_s);

    out.text = std::move(text);
    out.status = LoadStatus::Ok;
    out.roundTrips = encodeText(out.text, out.format, raw) == raw;
    return out;
}

LoadedText loadTextFile(const QString& absPath)
{
    QFile f(absPath);
    if (!f.exists()) {
        LoadedText out;
        out.status = LoadStatus::NotFound;
        out.error = u"The file no longer exists."_s;
        return out;
    }
    const FileStamp stamp = statFile(absPath);
    if (!f.open(QIODevice::ReadOnly)) {
        LoadedText out;
        out.status = LoadStatus::ReadError;
        out.error = f.errorString();
        return out;
    }
    const QByteArray raw = f.readAll();
    if (f.error() != QFileDevice::NoError) {
        LoadedText out;
        out.status = LoadStatus::ReadError;
        out.error = f.errorString();
        return out;
    }
    LoadedText out = decodeText(raw);
    out.stamp = stamp;
    return out;
}

QByteArray encodeText(QStringView text, const TextFormat& format, const QByteArray& originalRaw)
{
    const QByteArray utf8 = text.toUtf8();
    QByteArray out;
    out.reserve(utf8.size() + utf8.size() / 16 + 3);
    if (format.bom) out.append(kBom, 3);

    if (!format.mixedEol) {
        if (format.eol == LineEnding::LF) {
            out.append(utf8);
        } else {
            for (char c : utf8) {
                if (c == '\n') out.append('\r');
                out.append(c);
            }
        }
        return out;
    }

    // Mixed endings: keep original endings for the unchanged prefix and suffix.
    QByteArrayView orig(originalRaw);
    if (orig.startsWith(QByteArrayView(kBom, 3))) orig = orig.sliced(3);
    const std::vector<RawLine> oldLines = splitRawLines(orig);
    std::vector<QByteArrayView> newLines;
    {
        qsizetype start = 0;
        for (qsizetype i = 0; i < utf8.size(); ++i) {
            if (utf8[i] == '\n') {
                newLines.emplace_back(QByteArrayView(utf8).sliced(start, i - start));
                start = i + 1;
            }
        }
        newLines.emplace_back(QByteArrayView(utf8).sliced(start));
    }
    const std::size_t n = newLines.size();
    const std::size_t m = oldLines.size();
    std::size_t prefix = 0;
    while (prefix < n - 1 && prefix < m - 1 && newLines[prefix] == oldLines[prefix].content)
        ++prefix;
    std::size_t suffix = 0;
    while (suffix < n - prefix && suffix < m - prefix &&
           newLines[n - 1 - suffix] == oldLines[m - 1 - suffix].content) {
        ++suffix;
    }
    const QByteArrayView dominant =
        format.eol == LineEnding::CRLF ? QByteArrayView("\r\n") : QByteArrayView("\n");
    for (std::size_t i = 0; i < n; ++i) {
        out.append(newLines[i]);
        if (i == n - 1) break;
        if (i < prefix || n == m) {
            // Unchanged prefix, or the same number of lines: keep each line's own ending.
            out.append(oldLines[i].ending.isEmpty() ? dominant : oldLines[i].ending);
        } else if (i >= n - suffix) {
            out.append(oldLines[m - n + i].ending);
        } else {
            out.append(dominant);
        }
    }
    return out;
}

WriteResult writeFileAtomic(const QString& absPath, const QByteArray& data)
{
    QSaveFile f(absPath);
    f.setDirectWriteFallback(false);
    if (!f.open(QIODevice::WriteOnly)) return {false, f.errorString()};
    if (f.write(data) != data.size()) {
        const QString err = f.errorString();
        f.cancelWriting();
        return {false, err};
    }
    if (!f.commit()) return {false, f.errorString()};
    return {true, {}};
}

} // namespace mn
