#pragma once

#include "core/settings/VaultSettings.h"

#include <QByteArray>
#include <QDateTime>
#include <QList>
#include <QString>
#include <QStringList>

#include <functional>
#include <optional>

// Quick capture: append a thought to the inbox note or today's daily note.
//
// This is the one feature (besides rename) that writes to a note the user
// doesn't have open. It only ever appends: the existing bytes are kept exactly,
// the new text uses the file's own line-ending style, and the write is atomic.
// Files that aren't valid UTF-8 text are refused.
//
// A capture can carry files (pasted images, drawings). They are saved as new
// files in the attachment folder for the target note, under names that aren't
// taken, and embedded at the end of the captured text. No existing file is
// changed by this, and the files are removed again if the note can't be written.
namespace mn {

// A file attached to a capture, already encoded.
struct CaptureFile {
    QString baseName; // e.g. "Pasted image 20261124153012"
    QString suffix;   // without the dot, e.g. "png"
    QByteArray bytes;
};

// Whether another file in the vault already has this file name (for shortest links).
using NameInUse = std::function<bool(const QString& fileName)>;

// Vault path of the daily note for `now`: the daily-notes folder plus the date
// in the daily-note format.
QString dailyNotePath(const VaultSettings& s, const QDateTime& now);

// The text a new daily note starts with: the daily-note template (if set and
// readable) filled in, with the daily-note tags added to its frontmatter. A
// template whose frontmatter doesn't parse is used as it is.
QString newDailyNoteText(const QString& vaultRoot, const VaultSettings& s, const QString& path,
                         const QDateTime& now);

struct DailyNoteResult {
    bool ok = false;
    QString path;  // vault path of the daily note
    QString error; // when !ok
    bool created = false;
};

// Today's daily note, created first (atomically, from newDailyNoteText) when it
// doesn't exist. An existing note is never changed.
DailyNoteResult ensureDailyNote(const QString& vaultRoot, const VaultSettings& s,
                                const QDateTime& now = QDateTime::currentDateTime());

// Vault path of the note a capture goes to at `now`.
QString captureTargetPath(const VaultSettings& s, const QDateTime& now);

// The text to append: the capture format with {{text}}, {{date}}, {{time}} filled
// in. Extra lines of a multi-line capture are indented to stay in the list item.
QString formatCapture(const VaultSettings& s, const QString& text, const QDateTime& now);

// Appends `line` (which may contain '\n') to `raw` as new line(s) at the end, in
// the file's line-ending style. Returns nullopt if `raw` isn't editable text.
std::optional<QByteArray> appendToNote(const QByteArray& raw, const QString& line);

struct SavedFiles {
    bool ok = false;
    QString error;     // when !ok
    QStringList paths; // vault paths written
    QStringList links; // an embed for each, as seen from the target note
};

// Saves `files` as new files in the attachment folder for note `target`. On a
// failure, the files already saved are removed again.
SavedFiles saveCaptureFiles(const QString& vaultRoot, const VaultSettings& s, const QString& target,
                            const QList<CaptureFile>& files, const NameInUse& nameInUse = {});
// Removes files saveCaptureFiles wrote (when the note they belong to couldn't be saved).
void removeCaptureFiles(const QString& vaultRoot, const QStringList& written);

// The capture text with one embed per line after it.
QString captureTextWithEmbeds(const QString& text, const QStringList& links);

struct CaptureResult {
    bool ok = false;
    QString path;  // vault path written
    QString error; // when !ok
    bool created = false;
    QStringList attachments; // vault paths of the files saved with it
};

// Appends a capture to the target note (creating it when it doesn't exist yet; a
// daily note starts as newDailyNoteText), with `files` saved and embedded.
CaptureResult appendCapture(const QString& vaultRoot, const VaultSettings& s, const QString& text,
                            const QDateTime& now = QDateTime::currentDateTime(),
                            const QList<CaptureFile>& files = {}, const NameInUse& nameInUse = {});

} // namespace mn
