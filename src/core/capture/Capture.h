#pragma once

#include "core/settings/VaultSettings.h"

#include <QByteArray>
#include <QDateTime>
#include <QString>

// Quick capture: append a thought to the inbox note or today's daily note.
//
// This is the one feature (besides rename) that writes to a note the user
// doesn't have open. It only ever appends: the existing bytes are kept exactly,
// the new text uses the file's own line-ending style, and the write is atomic.
// Files that aren't valid UTF-8 text are refused.
namespace mn {

// Vault path of the note a capture goes to at `now`.
QString captureTargetPath(const VaultSettings& s, const QDateTime& now);

// The text to append: the capture format with {{text}}, {{date}}, {{time}} filled
// in. Extra lines of a multi-line capture are indented to stay in the list item.
QString formatCapture(const VaultSettings& s, const QString& text, const QDateTime& now);

// Appends `line` (which may contain '\n') to `raw` as new line(s) at the end, in
// the file's line-ending style. Returns nullopt if `raw` isn't editable text.
std::optional<QByteArray> appendToNote(const QByteArray& raw, const QString& line);

struct CaptureResult {
    bool ok = false;
    QString path;  // vault path written
    QString error; // when !ok
    bool created = false;
};

// Appends a capture to the target note (creating it, from the daily template if
// one is set, when it doesn't exist yet).
CaptureResult appendCapture(const QString& vaultRoot, const VaultSettings& s, const QString& text,
                            const QDateTime& now = QDateTime::currentDateTime());

} // namespace mn
