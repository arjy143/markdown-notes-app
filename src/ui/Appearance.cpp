#include "Appearance.h"

#include "Theme.h"

#include <QApplication>
#include <QFontDatabase>
#include <QPalette>
#include <QSettings>
#include <QStyle>
#include <QStyleFactory>

using namespace Qt::StringLiterals;

namespace mn::ui {

namespace {

struct Original {
    bool saved = false;
    QString style;
    QPalette palette;
    QFont font;
};

Original& original()
{
    static Original o;
    return o;
}

QPalette darkPalette(const QColor& accent)
{
    QPalette p;
    const QColor window(0x25, 0x25, 0x2a), base(0x1c, 0x1c, 0x20), text(0xdd, 0xdd, 0xe1),
        mid(0x3a, 0x3a, 0x40);
    p.setColor(QPalette::Window, window);
    p.setColor(QPalette::WindowText, text);
    p.setColor(QPalette::Base, base);
    p.setColor(QPalette::AlternateBase, window);
    p.setColor(QPalette::ToolTipBase, window);
    p.setColor(QPalette::ToolTipText, text);
    p.setColor(QPalette::PlaceholderText, QColor(0x80, 0x80, 0x88));
    p.setColor(QPalette::Text, text);
    p.setColor(QPalette::Button, QColor(0x30, 0x30, 0x36));
    p.setColor(QPalette::ButtonText, text);
    p.setColor(QPalette::BrightText, Qt::white);
    p.setColor(QPalette::Light, QColor(0x45, 0x45, 0x4c));
    p.setColor(QPalette::Midlight, mid);
    p.setColor(QPalette::Mid, mid);
    p.setColor(QPalette::Dark, QColor(0x18, 0x18, 0x1b));
    p.setColor(QPalette::Shadow, Qt::black);
    p.setColor(QPalette::Highlight, accent.isValid() ? accent : QColor(0x6d, 0x5a, 0xd0));
    p.setColor(QPalette::HighlightedText, Qt::white);
    p.setColor(QPalette::Link, accent.isValid() ? accent.lighter(130) : QColor(0x8a, 0xb4, 0xf8));
    p.setColor(QPalette::Disabled, QPalette::Text, QColor(0x70, 0x70, 0x78));
    p.setColor(QPalette::Disabled, QPalette::WindowText, QColor(0x70, 0x70, 0x78));
    p.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(0x70, 0x70, 0x78));
    return p;
}

QPalette lightPalette(const QColor& accent)
{
    QPalette p;
    const QColor window(0xf3, 0xf3, 0xf5), base(0xff, 0xff, 0xff), text(0x1e, 0x1e, 0x22),
        mid(0xc8, 0xc8, 0xd0);
    p.setColor(QPalette::Window, window);
    p.setColor(QPalette::WindowText, text);
    p.setColor(QPalette::Base, base);
    p.setColor(QPalette::AlternateBase, window);
    p.setColor(QPalette::ToolTipBase, base);
    p.setColor(QPalette::ToolTipText, text);
    p.setColor(QPalette::PlaceholderText, QColor(0x88, 0x88, 0x90));
    p.setColor(QPalette::Text, text);
    p.setColor(QPalette::Button, QColor(0xe9, 0xe9, 0xee));
    p.setColor(QPalette::ButtonText, text);
    p.setColor(QPalette::BrightText, Qt::red);
    p.setColor(QPalette::Light, Qt::white);
    p.setColor(QPalette::Midlight, QColor(0xe0, 0xe0, 0xe6));
    p.setColor(QPalette::Mid, mid);
    p.setColor(QPalette::Dark, QColor(0xa0, 0xa0, 0xa8));
    p.setColor(QPalette::Shadow, QColor(0x60, 0x60, 0x68));
    p.setColor(QPalette::Highlight, accent.isValid() ? accent : QColor(0x6d, 0x28, 0xd9));
    p.setColor(QPalette::HighlightedText, Qt::white);
    p.setColor(QPalette::Link, accent.isValid() ? accent : QColor(0x1a, 0x5f, 0xb4));
    p.setColor(QPalette::Disabled, QPalette::Text, QColor(0xa0, 0xa0, 0xa8));
    p.setColor(QPalette::Disabled, QPalette::WindowText, QColor(0xa0, 0xa0, 0xa8));
    p.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(0xa0, 0xa0, 0xa8));
    return p;
}

QFont withFamilyAndSize(QFont f, const QString& family, int size)
{
    if (!family.isEmpty()) f.setFamilies({family});
    if (size > 0) f.setPointSize(size);
    return f;
}

} // namespace

Appearance Appearance::load()
{
    QSettings s;
    s.beginGroup(u"appearance"_s);
    Appearance a;
    const QString theme = s.value(u"theme"_s).toString();
    a.theme = theme == u"light" ? ThemeMode::Light : theme == u"dark" ? ThemeMode::Dark : ThemeMode::System;
    a.accent = QColor::fromString(s.value(u"accent"_s).toString());
    a.uiFamily = s.value(u"uiFamily"_s).toString();
    a.uiSize = s.value(u"uiSize"_s, 0).toInt();
    a.editorFamily = s.value(u"editorFamily"_s).toString();
    a.editorSize = s.value(u"editorSize"_s, 0).toInt();
    a.monoFamily = s.value(u"monoFamily"_s).toString();
    a.readingFamily = s.value(u"readingFamily"_s).toString();
    a.readingSize = s.value(u"readingSize"_s, 0).toInt();
    a.readableLineLength = s.value(u"readableLineLength"_s, false).toBool();
    a.lineLengthChars = std::clamp(s.value(u"lineLengthChars"_s, 90).toInt(), 40, 200);
    return a;
}

void Appearance::save() const
{
    QSettings s;
    s.beginGroup(u"appearance"_s);
    s.setValue(u"theme"_s, theme == ThemeMode::Light  ? u"light"_s
                           : theme == ThemeMode::Dark ? u"dark"_s
                                                      : u"system"_s);
    s.setValue(u"accent"_s, accent.isValid() ? accent.name() : QString());
    s.setValue(u"uiFamily"_s, uiFamily);
    s.setValue(u"uiSize"_s, uiSize);
    s.setValue(u"editorFamily"_s, editorFamily);
    s.setValue(u"editorSize"_s, editorSize);
    s.setValue(u"monoFamily"_s, monoFamily);
    s.setValue(u"readingFamily"_s, readingFamily);
    s.setValue(u"readingSize"_s, readingSize);
    s.setValue(u"readableLineLength"_s, readableLineLength);
    s.setValue(u"lineLengthChars"_s, lineLengthChars);
}

QFont Appearance::uiFont() const
{
    const QFont base = original().saved ? original().font : QApplication::font();
    return withFamilyAndSize(base, uiFamily, uiSize);
}

QFont Appearance::editorFont() const
{
    return withFamilyAndSize(uiFont(), editorFamily, editorSize);
}

QFont Appearance::readingFont() const
{
    return withFamilyAndSize(uiFont(), readingFamily, readingSize);
}

QString Appearance::monoFamilyOrDefault() const
{
    if (!monoFamily.isEmpty()) return monoFamily;
    return QFontDatabase::systemFont(QFontDatabase::FixedFont).family();
}

void applyAppearance(const Appearance& a)
{
    Original& o = original();
    if (!o.saved) {
        o.saved = true;
        o.style = QApplication::style()->name();
        o.palette = QApplication::palette();
        o.font = QApplication::font();
    }
    Theme::setAccentOverride(a.accent);
    if (a.theme == Appearance::ThemeMode::System) {
        if (QApplication::style()->name() != o.style) QApplication::setStyle(QStyleFactory::create(o.style));
        QPalette p = o.palette;
        if (a.accent.isValid()) {
            p.setColor(QPalette::Highlight, a.accent);
            p.setColor(QPalette::Link, a.accent);
        }
        QApplication::setPalette(p);
    } else {
        if (QApplication::style()->name().compare(u"fusion"_s, Qt::CaseInsensitive) != 0)
            QApplication::setStyle(QStyleFactory::create(u"Fusion"_s));
        QApplication::setPalette(a.theme == Appearance::ThemeMode::Dark ? darkPalette(a.accent)
                                                                        : lightPalette(a.accent));
    }
    QApplication::setFont(a.uiFont());
}

} // namespace mn::ui
