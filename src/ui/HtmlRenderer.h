#pragma once

#include "Theme.h"

#include <QByteArray>
#include <QSet>
#include <QString>
#include <QStringList>

#include <functional>
#include <optional>

namespace mn {
class FileIndex;
}

namespace mn::ui {

// Renders a note to the HTML subset QTextBrowser understands (reading view).
// Images and math go through callbacks that register document resources.
struct RenderContext {
    const FileIndex* files = nullptr;
    QString notePath; // the note being rendered (links resolve relative to it)
    Theme theme;
    qreal fontPointSize = 11;
    int maxImageWidth = 800;
    bool strictLineBreaks = false; // single line breaks render as spaces

    // Returns <img> HTML for an image file (vault path), or empty if it can't be loaded.
    std::function<QString(const QString& path, int width, int height)> imageHtml;
    // Returns <img> HTML for a formula, or empty on error.
    std::function<QString(const QString& latex, bool display)> mathHtml;
    // Loads a note's bytes (for embeds).
    std::function<std::optional<QByteArray>(const QString& path)> loadNote;

    QSet<int> toggledCallouts; // callouts whose fold state the user flipped
    int depth = 0;             // embed depth
    QStringList embedStack;    // notes being embedded (cycle detection)
    int calloutCounter = 0;    // running callout index (shared with embeds)
};

// Renders a whole note (frontmatter shown as a properties table).
QString renderNote(QByteArrayView raw, RenderContext& ctx);
// Renders Markdown without frontmatter handling (used for embedded fragments).
QString renderMarkdown(QByteArrayView body, RenderContext& ctx);

QString htmlEscape(QStringView s);

// Callout styling: maps a type to its canonical family ("note", "tip", ...).
QString calloutFamily(QStringView type);

} // namespace mn::ui
