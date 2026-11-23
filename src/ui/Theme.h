#pragma once

#include <QColor>
#include <QPalette>

namespace mn::ui {

// Colours derived from the current palette, so light and dark themes both work.
struct Theme {
    bool dark = false;
    QColor text, muted, accent, link, unresolved, tag, code, codeBg, highlightBg, comment, math, heading,
        quote;

    static Theme fromPalette(const QPalette& p);
    // User-chosen accent (links and accents); an invalid colour restores the defaults.
    static void setAccentOverride(const QColor& c);
};

} // namespace mn::ui
