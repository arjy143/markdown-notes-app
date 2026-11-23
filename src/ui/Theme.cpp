#include "Theme.h"

namespace mn::ui {

namespace {
QColor& accentOverride()
{
    static QColor c;
    return c;
}
} // namespace

void Theme::setAccentOverride(const QColor& c)
{
    accentOverride() = c;
}

Theme Theme::fromPalette(const QPalette& p)
{
    Theme t;
    // Always the Active group: a widget that is disabled (e.g. the editor before a note
    // is open) would otherwise bake its greyed-out colours into the theme.
    const QColor base = p.color(QPalette::Active, QPalette::Base);
    t.dark = base.lightness() < 128;
    t.text = p.color(QPalette::Active, QPalette::Text);
    t.muted = t.dark ? QColor(0x8a, 0x8f, 0x98) : QColor(0x80, 0x80, 0x88);
    t.accent = t.dark ? QColor(0xa8, 0x8b, 0xfa) : QColor(0x6d, 0x28, 0xd9);
    t.link = t.dark ? QColor(0x8a, 0xb4, 0xf8) : QColor(0x1a, 0x5f, 0xb4);
    t.unresolved = t.dark ? QColor(0xc0, 0x90, 0x60) : QColor(0xb0, 0x60, 0x20);
    t.tag = t.dark ? QColor(0x7d, 0xd3, 0xc0) : QColor(0x0f, 0x76, 0x6e);
    t.code = t.dark ? QColor(0xe6, 0xc0, 0x7b) : QColor(0x9a, 0x34, 0x12);
    t.codeBg = t.dark ? QColor(255, 255, 255, 18) : QColor(0, 0, 0, 14);
    t.highlightBg = t.dark ? QColor(0x80, 0x70, 0x20, 140) : QColor(0xff, 0xf0, 0x80);
    t.comment = t.muted;
    t.math = t.dark ? QColor(0xc4, 0xb5, 0xfd) : QColor(0x5b, 0x21, 0xb6);
    t.heading = t.text;
    t.quote = t.muted;
    if (const QColor& a = accentOverride(); a.isValid()) {
        t.accent = a;
        t.link = t.dark ? a.lighter(135) : a;
    }
    return t;
}

} // namespace mn::ui
