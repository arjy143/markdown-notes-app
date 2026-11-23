#pragma once

#include <QColor>
#include <QHash>
#include <QImage>
#include <QString>

namespace mn::ui {

// LaTeX math to images via MicroTeX. Images are cached by
// (formula, display, font size, device pixel ratio, colour). UI thread only.
class MathRenderer {
public:
    static MathRenderer& instance();

    // Returns a null image (and sets `error`) if the formula can't be parsed.
    QImage render(const QString& latex, bool display, qreal pointSize, qreal dpr, const QColor& color,
                  QString* error = nullptr);
    int cacheSize() const { return static_cast<int>(cache_.size()); }
    void clearCache() { cache_.clear(); }

private:
    MathRenderer() = default;
    bool ensureInit();

    bool initDone_ = false;
    bool initOk_ = false;
    QHash<QString, QImage> cache_;
};

} // namespace mn::ui
