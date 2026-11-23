#include "MathRenderer.h"

#include "core/formula.h"
#include "latex.h"
#include "platform/qt/graphic_qt.h"
#include "render.h"

#include <QPainter>
#include <QtMath>

#include <memory>

using namespace Qt::StringLiterals;

namespace mn::ui {

namespace {
constexpr int kMaxCache = 512;
}

MathRenderer& MathRenderer::instance()
{
    static MathRenderer r;
    return r;
}

bool MathRenderer::ensureInit()
{
    if (initDone_) return initOk_;
    initDone_ = true;
    try {
        // Fonts and alphabet descriptions are compiled in as Qt resources.
        tex::RES_BASE = ":/microtex/res";
        tex::LaTeX::init(":/microtex/res-not-on-disk");
        tex::RES_BASE = ":/microtex/res";
        initOk_ = true;
    } catch (...) {
        initOk_ = false;
    }
    return initOk_;
}

QImage MathRenderer::render(const QString& latex, bool display, qreal pointSize, qreal dpr,
                            const QColor& color, QString* error)
{
    const QString key = latex + u'\x1f' + (display ? u'D' : u'I') + QString::number(pointSize) + u'@' +
                        QString::number(dpr) + color.name(QColor::HexArgb);
    if (const auto it = cache_.constFind(key); it != cache_.constEnd()) return *it;
    if (!ensureInit()) {
        if (error) *error = u"The math renderer couldn't start."_s;
        return {};
    }
    QImage img;
    try {
        const float px = static_cast<float>(pointSize * 96.0 / 72.0);
        tex::Formula formula(latex.toStdWString());
        tex::TeXRenderBuilder builder;
        std::unique_ptr<tex::TeXRender> r(
            builder.setStyle(display ? tex::TexStyle::display : tex::TexStyle::text)
                .setTextSize(px)
                .setWidth(tex::UnitType::pixel, 4000, tex::Alignment::left)
                .setIsMaxWidth(true)
                .setLineSpace(tex::UnitType::pixel, px / 3.0f)
                .setForeground(static_cast<tex::color>(color.rgba()))
                .build(formula));
        const int pad = 2;
        const int w = r->getWidth() + 2 * pad;
        const int h = r->getHeight() + 2 * pad;
        if (w <= 0 || h <= 0 || w > 20000 || h > 20000) throw std::runtime_error("bad size");
        img = QImage(qCeil(w * dpr), qCeil(h * dpr), QImage::Format_ARGB32_Premultiplied);
        img.fill(Qt::transparent);
        img.setDevicePixelRatio(dpr);
        QPainter p(&img);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setRenderHint(QPainter::TextAntialiasing, true);
        tex::Graphics2D_qt g2(&p);
        r->draw(g2, pad, pad);
    } catch (const std::exception& e) {
        if (error) *error = QString::fromUtf8(e.what());
        return {};
    } catch (...) {
        if (error) *error = u"Invalid formula."_s;
        return {};
    }
    if (cache_.size() >= kMaxCache) cache_.clear();
    cache_.insert(key, img);
    return img;
}

} // namespace mn::ui
