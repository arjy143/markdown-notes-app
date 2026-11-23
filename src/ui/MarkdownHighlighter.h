#pragma once

#include "Theme.h"

#include <QSyntaxHighlighter>
#include <QTextCharFormat>

#include <functional>

namespace mn::ui {

// Incremental per-block highlighter for the source editor. Multi-line
// constructs (frontmatter, code fences, $$ math, %% comments) are carried in
// the block state, so a keystroke only re-highlights the edited block (and the
// following ones only while their state changes).
class MarkdownHighlighter : public QSyntaxHighlighter {
    Q_OBJECT
public:
    // Returns true if a link path resolves (for styling unresolved links).
    using LinkChecker = std::function<bool(QStringView linkpath)>;

    explicit MarkdownHighlighter(QTextDocument* doc);

    void setLinkChecker(LinkChecker checker) { checker_ = std::move(checker); }
    // `monoFamily` empty = the system's fixed-width font.
    void setTheme(const Theme& theme, const QFont& baseFont, const QString& monoFamily = {});
    static double headingScale(int level);

protected:
    void highlightBlock(const QString& text) override;

private:
    enum Kind : int { Normal = 0, Frontmatter = 1, Fence = 2, MathBlock = 3, Comment = 4 };
    static int encode(Kind k, int fenceLen = 0, bool tilde = false)
    {
        return static_cast<int>(static_cast<unsigned>(k) | (static_cast<unsigned>(fenceLen) << 4U) |
                                (tilde ? 1U << 12U : 0U));
    }
    static Kind kindOf(int state)
    {
        return state < 0 ? Normal : static_cast<Kind>(static_cast<unsigned>(state) & 0xFU);
    }

    void inlineSpans(const QString& text, int from, int to, const QTextCharFormat& base);
    void apply(int start, int len, const QTextCharFormat& f);

    LinkChecker checker_;
    Theme theme_;
    QFont baseFont_;
    qreal monoPointSize_ = 10;
    QTextCharFormat headingFmt_[6], codeFmt_, fenceFmt_, linkFmt_, unresolvedFmt_, tagFmt_, markupFmt_,
        commentFmt_, mathFmt_, boldFmt_, italicFmt_, strikeFmt_, highlightFmt_, quoteFmt_, fmKeyFmt_, fmFmt_,
        blockIdFmt_, urlFmt_;
};

} // namespace mn::ui
