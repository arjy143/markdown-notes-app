#pragma once

#include <QColor>
#include <QFont>
#include <QString>

namespace mn::ui {

// App-wide look and fonts (stored with QSettings, not per vault). Empty
// families and zero sizes mean "use the default", so the defaults leave the
// desktop's own theme and fonts untouched.
struct Appearance {
    enum class ThemeMode : unsigned char { System, Light, Dark };

    ThemeMode theme = ThemeMode::System;
    QColor accent; // links, highlights, selection; invalid = theme default
    QString uiFamily;
    int uiSize = 0;
    QString editorFamily;
    int editorSize = 0;
    QString monoFamily; // code in the editor and reading view
    QString readingFamily;
    int readingSize = 0;
    bool readableLineLength = false; // centre text in a column of `lineLengthChars`
    int lineLengthChars = 90;

    static Appearance load();
    void save() const;

    QFont uiFont() const;
    QFont editorFont() const;
    QFont readingFont() const;
    QString monoFamilyOrDefault() const;

    friend bool operator==(const Appearance&, const Appearance&) = default;
};

// Applies theme, accent and interface font to the whole application. The first
// call remembers the desktop's style and palette so "System" can restore them.
void applyAppearance(const Appearance& a);

} // namespace mn::ui
