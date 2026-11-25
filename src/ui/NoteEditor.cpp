#include "NoteEditor.h"

#include "MarkdownHighlighter.h"
#include "core/index/VaultService.h"
#include "core/links/RenamePlanner.h"
#include "core/parse/MarkdownDoc.h"
#include "core/parse/NoteMetadata.h"
#include "core/vault/Paths.h"

#include <QAbstractItemView>
#include <QCompleter>
#include <QContextMenuEvent>
#include <QKeyEvent>
#include <QMenu>
#include <QMimeData>
#include <QPlainTextDocumentLayout>
#include <QRegularExpression>
#include <QScrollBar>
#include <QStringListModel>
#include <QTextBlock>
#include <QTimer>

using namespace Qt::StringLiterals;

namespace mn::ui {

namespace {

// QSyntaxHighlighter reports every re-highlighted block as a change of
// (start, length, length). QPlainTextDocumentLayout adds those lengths, decides
// the change spills into the next block, and repaints the whole viewport on
// every keystroke (~10 ms with a full screen of formatted text). A change that
// really stays within one block is passed on narrowed to that block, so the
// base class takes its single-block path (relayout + repaint of that block only,
// or a full update if the block's height changed). Structural changes (blocks
// added or removed) still take the full path, because the base class also
// compares block counts.
class BlockLocalLayout : public QPlainTextDocumentLayout {
public:
    using QPlainTextDocumentLayout::QPlainTextDocumentLayout;

protected:
    void documentChanged(int from, int charsRemoved, int charsAdded) override
    {
        QTextDocument* doc = document();
        const int span = std::max(charsRemoved, charsAdded);
        const QTextBlock first = doc->findBlock(from);
        const QTextBlock last = doc->findBlock(std::max(0, from + span - 1));
        if (span > 0 && first.isValid() && first == last) {
            const int inBlock = std::min(span, first.position() + first.length() - from);
            QPlainTextDocumentLayout::documentChanged(from, 0, std::max(1, inBlock));
            return;
        }
        QPlainTextDocumentLayout::documentChanged(from, charsRemoved, charsAdded);
    }
};

} // namespace

NoteEditor::NoteEditor(QWidget* parent) : QPlainTextEdit(parent)
{
    auto* doc = new QTextDocument(this);
    doc->setDocumentLayout(new BlockLocalLayout(doc));
    setDocument(doc);
    setLineWrapMode(QPlainTextEdit::WidgetWidth);
    setTabChangesFocus(false);
    setMouseTracking(true);
    setCenterOnScroll(false);
    document()->setDocumentMargin(16);
    // The side margins of a narrowed text column are painted like the page.
    setBackgroundRole(QPalette::Base);
    setAutoFillBackground(true);
    highlighter_ = new MarkdownHighlighter(document());
    applyTheme();

    model_ = new QStringListModel(this);
    completer_ = new QCompleter(model_, this);
    completer_->setWidget(this);
    completer_->setCompletionMode(QCompleter::PopupCompletion);
    completer_->setCaseSensitivity(Qt::CaseInsensitive);
    completer_->setFilterMode(Qt::MatchContains);
    completer_->setMaxVisibleItems(12);
    connect(completer_, qOverload<const QString&>(&QCompleter::activated), this,
            &NoteEditor::insertCompletion);

    suggestTimer_ = new QTimer(this);
    suggestTimer_->setSingleShot(true);
    suggestTimer_->setInterval(350);
    connect(suggestTimer_, &QTimer::timeout, this, &NoteEditor::scanLinkSuggestions);
    auto later = [this] {
        if (suggestLinks_ && !isReadOnly()) suggestTimer_->start();
    };
    connect(document(), &QTextDocument::contentsChanged, this, later);
    connect(this, &QPlainTextEdit::cursorPositionChanged, this, [this, later] {
        // Suggestions only cover the cursor's paragraph.
        if (!suggestions_.empty()) {
            const QTextBlock b = textCursor().block();
            const int p = suggestions_.front().position;
            if (p < b.position() || p >= b.position() + b.length()) clearSuggestions();
        }
        later();
    });
}

void NoteEditor::applyTheme()
{
    if (highlighter_ == nullptr) return; // palette events arrive during construction
    highlighter_->setTheme(Theme::fromPalette(palette()), font(), monoFamily_);
}

void NoteEditor::setMonoFamily(const QString& family)
{
    if (family == monoFamily_) return;
    monoFamily_ = family;
    applyTheme();
}

void NoteEditor::setMaxLineChars(int chars)
{
    maxLineChars_ = chars;
    updateLineMargins();
}

void NoteEditor::updateLineMargins()
{
    int side = 0;
    if (maxLineChars_ > 0) {
        const int column = fontMetrics().horizontalAdvance(u'n') * maxLineChars_ +
                           2 * static_cast<int>(document()->documentMargin());
        const int available = width() - 2 * frameWidth() - verticalScrollBar()->sizeHint().width();
        side = std::max(0, (available - column) / 2);
    }
    setViewportMargins(side, 0, side, 0);
}

void NoteEditor::resizeEvent(QResizeEvent* e)
{
    QPlainTextEdit::resizeEvent(e);
    updateLineMargins();
}

void NoteEditor::changeEvent(QEvent* e)
{
    QPlainTextEdit::changeEvent(e);
    if (e->type() == QEvent::PaletteChange || e->type() == QEvent::FontChange) applyTheme();
    if (e->type() == QEvent::FontChange) updateLineMargins();
}

void NoteEditor::setVault(VaultService* vault)
{
    vault_ = vault;
    highlighter_->setLinkChecker([this](QStringView lp) {
        if (vault_ == nullptr) return true;
        return vault_->files().resolve(lp, notePath_).resolved();
    });
    refreshCompletionSources();
}

void NoteEditor::refreshCompletionSources()
{
    // Built lazily, on the first [[ after the file set changed.
    fileItems_.clear();
    itemPaths_.clear();
    fileItemsStale_ = true;
    namesStale_ = true;
}

void NoteEditor::setLinkSuggestions(bool on)
{
    suggestLinks_ = on;
    if (!on) clearSuggestions();
}

void NoteEditor::clearSuggestions()
{
    if (suggestions_.empty()) return;
    suggestions_.clear();
    setExtraSelections({});
    Q_EMIT linkSuggestionHint(QString());
}

void NoteEditor::scanLinkSuggestions()
{
    if (!suggestLinks_ || vault_ == nullptr || isReadOnly() || notePath_.isEmpty()) {
        clearSuggestions();
        return;
    }
    const QTextBlock block = textCursor().block();
    // Not inside frontmatter, code fences, $$ math or %% comments.
    if (block.userState() > 0 && (static_cast<unsigned>(block.userState()) & 0xFU) != 0) {
        clearSuggestions();
        return;
    }
    if (namesStale_) {
        names_ = buildNameTable(vault_->snapshot().files(), vault_->snapshot().aliases);
        namesStale_ = false;
    }
    const QString text = block.text();
    std::vector<Suggestion> found;
    for (const LinkSuggestion& s : findLinkSuggestions(text, names_, notePath_, dismissed_))
        found.push_back({block.position() + s.start, s.length, s.path, s.key, text.mid(s.start, s.length)});
    if (found.empty()) {
        clearSuggestions();
        return;
    }
    suggestions_ = std::move(found);
    QList<QTextEdit::ExtraSelection> sels;
    const Theme theme = Theme::fromPalette(palette());
    for (const Suggestion& s : suggestions_) {
        QTextEdit::ExtraSelection sel;
        sel.cursor = QTextCursor(document());
        sel.cursor.setPosition(s.position);
        sel.cursor.setPosition(s.position + s.length, QTextCursor::KeepAnchor);
        sel.format.setUnderlineStyle(QTextCharFormat::DotLine);
        sel.format.setUnderlineColor(theme.accent);
        sels.append(sel);
    }
    setExtraSelections(sels);
    Q_EMIT linkSuggestionHint(
        tr("Alt+Enter: link “%1” to %2 · Esc: dismiss")
            .arg(suggestions_.front().text, paths::linkName(suggestions_.front().path)));
}

QString NoteEditor::linkTextFor(const Suggestion& s) const
{
    const VaultSettings& st = vault_->settings();
    if (st.useMarkdownLinks) {
        const QString lp = vault_->files().linkpathFor(s.path, notePath_, st.newLinkFormat, true);
        return u'[' + s.text + u"]("_s + encodeMarkdownPath(lp) + u')';
    }
    const QString lp = vault_->files().linkpathFor(s.path, notePath_, st.newLinkFormat, false);
    if (lp == s.text) return u"[["_s + lp + u"]]"_s;
    return u"[["_s + lp + u'|' + s.text + u"]]"_s;
}

void NoteEditor::applySuggestion(const Suggestion& s)
{
    QTextCursor c(document());
    c.setPosition(s.position);
    c.setPosition(s.position + s.length, QTextCursor::KeepAnchor);
    if (c.selectedText() != s.text) return; // text changed since the scan
    const int cursorPos = textCursor().position();
    const QString link = linkTextFor(s);
    c.insertText(link);
    // Keep the cursor where it was relative to the text.
    if (cursorPos >= s.position + s.length) {
        QTextCursor keep = textCursor();
        keep.setPosition(cursorPos + static_cast<int>(link.size()) - s.length);
        setTextCursor(keep);
    }
    clearSuggestions();
}

bool NoteEditor::acceptLinkSuggestion()
{
    scanLinkSuggestions();
    if (suggestions_.empty()) return false;
    // Prefer the last suggestion that starts before the cursor, else the first.
    const int pos = textCursor().position();
    const Suggestion* best = &suggestions_.front();
    for (const Suggestion& s : suggestions_) {
        if (s.position <= pos) best = &s;
    }
    const Suggestion chosen = *best;
    applySuggestion(chosen);
    return true;
}

void NoteEditor::contextMenuEvent(QContextMenuEvent* e)
{
    QMenu* menu = createStandardContextMenu(e->pos());
    if (!suggestions_.empty()) {
        QAction* first = menu->actions().isEmpty() ? nullptr : menu->actions().first();
        const std::vector<Suggestion> current = suggestions_;
        for (const Suggestion& s : current) {
            auto* a = new QAction(tr("Link “%1” to %2").arg(s.text, paths::linkName(s.path)), menu);
            connect(a, &QAction::triggered, this, [this, s] { applySuggestion(s); });
            menu->insertAction(first, a);
        }
        if (first != nullptr) menu->insertSeparator(first);
    }
    menu->exec(e->globalPos());
    delete menu;
}

static void buildFileItems(VaultService* vault, QStringList& fileItems, QHash<QString, QString>& itemPaths)
{
    if (vault == nullptr) return;
    const VaultSnapshot& snap = vault->snapshot();
    for (const VaultFile& f : snap.files()) {
        if (f.kind == FileKind::Other) continue;
        QString item = f.path;
        if (f.kind == FileKind::Note) item.chop(3);
        fileItems.append(item);
        itemPaths.insert(item, f.path);
        for (const QString& a : snap.aliases.value(f.path)) {
            const QString ai = a + u" → "_s + item;
            fileItems.append(ai);
            itemPaths.insert(ai, f.path);
        }
    }
}

QString NoteEditor::bufferText() const
{
    QString out;
    out.reserve(document()->characterCount());
    bool first = true;
    for (QTextBlock b = document()->begin(); b.isValid(); b = b.next()) {
        if (!first) out += u'\n';
        out += b.text();
        first = false;
    }
    return out;
}

bool NoteEditor::setBuffer(const QString& text)
{
    suggestions_.clear();
    setExtraSelections({});
    dismissed_.clear();
    setPlainText(text);
    return bufferText() == text;
}

// --- links ------------------------------------------------------------------

std::optional<NoteEditor::LinkHit> NoteEditor::linkAt(const QTextCursor& c) const
{
    const QString text = c.block().text();
    const int pos = c.positionInBlock();
    static const QRegularExpression wiki(uR"(!?\[\[([^\]\n]+?)\]\])"_s);
    for (auto it = wiki.globalMatch(text); it.hasNext();) {
        const auto m = it.next();
        if (pos < m.capturedStart() || pos > m.capturedEnd()) continue;
        QString inner = m.captured(1);
        const qsizetype bar = inner.indexOf(u'|');
        if (bar >= 0) inner.truncate(bar);
        if (inner.endsWith(u'\\')) inner.chop(1);
        return LinkHit{inner, false, false};
    }
    static const QRegularExpression md(uR"(!?\[[^\]\n]*\]\((<[^>\n]*>|[^)\s]*)[^)]*\))"_s);
    for (auto it = md.globalMatch(text); it.hasNext();) {
        const auto m = it.next();
        if (pos < m.capturedStart() || pos > m.capturedEnd()) continue;
        QString dest = m.captured(1);
        if (dest.startsWith(u'<')) dest = dest.mid(1, dest.size() - 2);
        if (hasUrlScheme(dest)) return LinkHit{dest, true, true};
        return LinkHit{percentDecode(dest), true, false};
    }
    static const QRegularExpression url(uR"((https?://|www\.)[^\s<>()\[\]]+[^\s<>()\[\].,;:!?'"])"_s);
    for (auto it = url.globalMatch(text); it.hasNext();) {
        const auto m = it.next();
        if (pos < m.capturedStart() || pos > m.capturedEnd()) continue;
        QString u = m.captured(0);
        if (u.startsWith(u"www."_s)) u.prepend(u"https://"_s);
        return LinkHit{u, false, true};
    }
    return std::nullopt;
}

void NoteEditor::mouseReleaseEvent(QMouseEvent* e)
{
    if (e->button() == Qt::LeftButton && (e->modifiers() & Qt::ControlModifier)) {
        if (const auto hit = linkAt(cursorForPosition(e->position().toPoint()))) {
            if (hit->url)
                Q_EMIT urlActivated(hit->target);
            else
                Q_EMIT linkActivated(hit->target, hit->markdown);
            return;
        }
    }
    QPlainTextEdit::mouseReleaseEvent(e);
}

void NoteEditor::mouseMoveEvent(QMouseEvent* e)
{
    QPlainTextEdit::mouseMoveEvent(e);
    const bool overLink =
        (e->modifiers() & Qt::ControlModifier) && linkAt(cursorForPosition(e->position().toPoint()));
    viewport()->setCursor(overLink ? Qt::PointingHandCursor : Qt::IBeamCursor);
}

void NoteEditor::focusOutEvent(QFocusEvent* e)
{
    QPlainTextEdit::focusOutEvent(e);
    if (completer_->popup()->isVisible()) return;
    Q_EMIT focusLost();
}

// --- typing helpers -----------------------------------------------------------

bool NoteEditor::continueList()
{
    QTextCursor c = textCursor();
    if (c.hasSelection() || !c.atBlockEnd()) return false;
    const QString line = c.block().text();
    static const QRegularExpression item(uR"(^(\s*)([-*+]|(\d{1,9})([.)]))(\s+)(\[[ xX]\]\s+)?)"_s);
    const auto m = item.match(line);
    if (!m.hasMatch()) return false;
    if (m.capturedEnd(0) == line.size()) {
        // Empty item: end the list.
        c.movePosition(QTextCursor::StartOfBlock, QTextCursor::KeepAnchor);
        c.removeSelectedText();
        setTextCursor(c);
        return true;
    }
    QString marker = m.captured(1);
    if (!m.captured(3).isEmpty())
        marker += QString::number(m.captured(3).toLongLong() + 1) + m.captured(4);
    else
        marker += m.captured(2);
    marker += m.captured(5);
    if (!m.captured(6).isEmpty()) marker += u"[ ] "_s;
    c.insertText(u"\n"_s + marker);
    setTextCursor(c);
    return true;
}

void NoteEditor::keyPressEvent(QKeyEvent* e)
{
    QAbstractItemView* popup = completer_->popup();
    if (popup->isVisible()) {
        switch (e->key()) {
        case Qt::Key_Enter:
        case Qt::Key_Return:
        case Qt::Key_Tab: {
            const QModelIndex idx = popup->currentIndex().isValid()
                                        ? popup->currentIndex()
                                        : completer_->completionModel()->index(0, 0);
            if (idx.isValid()) {
                popup->hide();
                insertCompletion(idx.data().toString());
                return;
            }
            break;
        }
        case Qt::Key_Escape:
            popup->hide();
            mode_ = Mode::None;
            return;
        case Qt::Key_Up:
        case Qt::Key_Down:
        case Qt::Key_PageUp:
        case Qt::Key_PageDown:
            e->ignore();
            return;
        default:
            break;
        }
    }
    if ((e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) && (e->modifiers() & Qt::AltModifier)) {
        acceptLinkSuggestion();
        return;
    }
    if (e->key() == Qt::Key_Escape && !suggestions_.empty()) {
        for (const Suggestion& s : suggestions_)
            dismissed_.insert(s.key);
        clearSuggestions();
        return;
    }
    if ((e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter)) {
        if (e->modifiers() & Qt::ShiftModifier) {
            // QPlainTextEdit would insert U+2028; a note only has real line breaks.
            textCursor().insertText(u"\n"_s);
            return;
        }
        if (e->modifiers() == Qt::NoModifier && continueList()) return;
    }
    QPlainTextEdit::keyPressEvent(e);
    if (!e->text().isEmpty() || e->key() == Qt::Key_Backspace) {
        updateCompletion();
    } else if (e->key() == Qt::Key_Left || e->key() == Qt::Key_Right || e->key() == Qt::Key_Home ||
               e->key() == Qt::Key_End) {
        popup->hide();
    }
}

// --- completion ---------------------------------------------------------------

namespace {

// Where the tag being typed starts, when the cursor (column `col` of `block`) is
// in the value of the frontmatter's tags property: "tags: [a, b|", "tags: a b|"
// or a "  - b|" item under "tags:". -1 elsewhere.
int frontmatterTagStart(const QTextBlock& block, int col)
{
    constexpr int kMaxFrontmatterLines = 200;
    const int number = block.blockNumber();
    if (number < 1 || number > kMaxFrontmatterLines) return -1;
    const QTextDocument* doc = block.document();
    if (doc->firstBlock().text().trimmed() != u"---"_s) return -1;
    for (QTextBlock b = doc->firstBlock().next(); b.isValid() && b != block; b = b.next()) {
        const QString t = b.text().trimmed();
        if (t == u"---"_s || t == u"..."_s) return -1; // the frontmatter ended above
    }
    // The property this line belongs to: the nearest unindented "key:" at or above it.
    QString key;
    int keyEnd = -1; // column after the colon, when the key is on this line
    for (QTextBlock b = block; b.isValid() && b.blockNumber() >= 1; b = b.previous()) {
        const QString t = b.text();
        if (t.isEmpty() || t[0].isSpace() || t.startsWith(u'-') || t.startsWith(u'#')) continue;
        const qsizetype colon = t.indexOf(u':');
        if (colon <= 0) return -1;
        key = t.left(colon).trimmed();
        if (b == block) keyEnd = static_cast<int>(colon) + 1;
        break;
    }
    if (key.compare(u"tags"_s, Qt::CaseInsensitive) != 0 && key.compare(u"tag"_s, Qt::CaseInsensitive) != 0)
        return -1;
    const QString line = block.text();
    if (col < keyEnd) return -1;
    int s = col;
    while (s > 0 && line[s - 1] != u'#' && md::isTagCodePoint(line[s - 1].unicode()))
        --s;
    const QChar before = s > 0 ? line[s - 1] : QChar(u' ');
    if (!(before.isSpace() || before == u'[' || before == u',' || before == u':' || before == u'"' ||
          before == u'\''))
        return -1;
    if (keyEnd < 0 && !line.trimmed().startsWith(u'-')) return -1; // under the key, only list items
    return s;
}

} // namespace

void NoteEditor::updateCompletion()
{
    const QTextCursor c = textCursor();
    const QString line = c.block().text();
    const int col = c.positionInBlock();
    const int blockPos = c.block().position();
    const QString before = line.left(col);

    const qsizetype open = before.lastIndexOf(u"[["_s);
    if (open >= 0 && before.indexOf(u"]]"_s, open) < 0) {
        const QString inside = before.mid(open + 2);
        if (inside.contains(u'|')) {
            completer_->popup()->hide();
            return;
        }
        const qsizetype hash = inside.indexOf(u'#');
        if (hash < 0) {
            if (fileItemsStale_) {
                buildFileItems(vault_, fileItems_, itemPaths_);
                fileItemsStale_ = false;
            }
            mode_ = Mode::Link;
            anchor_ = blockPos + static_cast<int>(open) + 2;
            showCompletion(fileItems_, inside);
            return;
        }
        QString target = inside.left(hash);
        QString rest = inside.mid(hash + 1);
        const bool block = rest.startsWith(u'^');
        if (block) rest = rest.mid(1);
        QString path = notePath_;
        if (!target.isEmpty() && vault_ != nullptr) {
            const Resolution r = vault_->files().resolve(target, notePath_);
            if (!r.resolved()) {
                completer_->popup()->hide();
                return;
            }
            path = vault_->files().file(r.file).path;
        }
        mode_ = block ? Mode::Block : Mode::Heading;
        anchor_ = blockPos + static_cast<int>(open + 2 + hash + 1 + (block ? 1 : 0));
        target_ = path;
        if (path == notePath_) {
            // The current note: parse the buffer, which may have unsaved headings.
            const QByteArray utf8 = bufferText().toUtf8();
            const NoteMetadata meta = parseNote(utf8);
            QStringList items;
            if (block) {
                for (const BlockRef& b : meta.blocks)
                    items.append(b.id);
            } else {
                for (const Heading& h : meta.headings)
                    items.append(h.text);
            }
            showCompletion(items, rest);
            return;
        }
        const int req = ++requestId_;
        auto show = [this, req, rest](const QStringList& items) {
            if (req != requestId_ || mode_ == Mode::None) return;
            showCompletion(items, rest);
        };
        if (block) {
            vault_->blocks(path, [show](std::vector<BlockInfo> bs) {
                QStringList items;
                for (const BlockInfo& b : bs)
                    items.append(b.id);
                show(items);
            });
        } else {
            vault_->headings(path, [show](std::vector<HeadingInfo> hs) {
                QStringList items;
                for (const HeadingInfo& h : hs)
                    items.append(h.text);
                show(items);
            });
        }
        return;
    }

    // Tag in the frontmatter's tags property (no '#' there).
    if (vault_ != nullptr) {
        const int start = frontmatterTagStart(c.block(), col);
        if (start >= 0 && start < col) {
            mode_ = Mode::Tag;
            anchor_ = blockPos + start;
            QStringList tags;
            for (const TagCount& t : vault_->snapshot().tags)
                tags.append(t.tag);
            showCompletion(tags, line.mid(start, col - start));
            return;
        }
    }

    // Tag: '#' at a boundary followed by tag characters up to the cursor.
    int s = col;
    while (s > 0) {
        const QChar ch = line[s - 1];
        if (ch == u'#' || !md::isTagCodePoint(ch.unicode())) break;
        --s;
    }
    if (s > 0 && line[s - 1] == u'#' && (s == 1 || line[s - 2].isSpace() || line[s - 2] == u'(') &&
        vault_ != nullptr && !(s == 1 && col == 1)) {
        const QString prefix = line.mid(s, col - s);
        if (!prefix.isEmpty()) {
            mode_ = Mode::Tag;
            anchor_ = blockPos + s;
            QStringList tags;
            for (const TagCount& t : vault_->snapshot().tags)
                tags.append(t.tag);
            showCompletion(tags, prefix);
            return;
        }
    }
    mode_ = Mode::None;
    completer_->popup()->hide();
}

void NoteEditor::showCompletion(const QStringList& items, const QString& prefix)
{
    if (items.isEmpty()) {
        completer_->popup()->hide();
        return;
    }
    if (model_->stringList() != items) model_->setStringList(items);
    completer_->setCompletionPrefix(prefix);
    if (completer_->completionCount() == 0) {
        completer_->popup()->hide();
        return;
    }
    completer_->popup()->setCurrentIndex(completer_->completionModel()->index(0, 0));
    QRect r = cursorRect();
    r.setWidth(std::max(320, completer_->popup()->sizeHintForColumn(0) +
                                 completer_->popup()->verticalScrollBar()->sizeHint().width() + 16));
    completer_->complete(r);
}

void NoteEditor::insertCompletion(const QString& item)
{
    if (mode_ == Mode::None || anchor_ < 0) return;
    QTextCursor c = textCursor();
    const int end = c.position();
    if (end < anchor_) return;
    c.beginEditBlock();
    c.setPosition(anchor_);
    c.setPosition(end, QTextCursor::KeepAnchor);
    const QString after = c.block().text().mid(c.positionInBlock());
    const bool closed = after.startsWith(u"]]"_s);

    switch (mode_) {
    case Mode::Link: {
        const QString path = itemPaths_.value(item);
        if (path.isEmpty() || vault_ == nullptr) break;
        QString alias;
        const qsizetype arrow = item.indexOf(u" → "_s);
        if (arrow > 0) alias = item.left(arrow);
        const VaultSettings& st = vault_->settings();
        if (st.useMarkdownLinks) {
            // Replace "[[prefix" with a Markdown link.
            c.setPosition(anchor_ - 2);
            c.setPosition(end + (closed ? 2 : 0), QTextCursor::KeepAnchor);
            const QString lp = vault_->files().linkpathFor(path, notePath_, st.newLinkFormat, true);
            const QString text = alias.isEmpty() ? paths::linkName(path) : alias;
            c.insertText(u'[' + text + u"]("_s + encodeMarkdownPath(lp) + u')');
        } else {
            QString lp = vault_->files().linkpathFor(path, notePath_, st.newLinkFormat, false);
            if (!alias.isEmpty()) lp += u'|' + alias;
            c.insertText(lp + (closed ? QString() : u"]]"_s));
        }
        break;
    }
    case Mode::Heading:
    case Mode::Block:
        c.insertText(item + (closed ? QString() : u"]]"_s));
        break;
    case Mode::Tag:
        c.insertText(item);
        break;
    case Mode::None:
        break;
    }
    c.endEditBlock();
    setTextCursor(c);
    mode_ = Mode::None;
}

// --- paste / drop ---------------------------------------------------------------

bool NoteEditor::canInsertFromMimeData(const QMimeData* source) const
{
    return source->hasImage() || source->hasUrls() || QPlainTextEdit::canInsertFromMimeData(source);
}

void NoteEditor::insertFromMimeData(const QMimeData* source)
{
    if (attachmentHandler_ && (source->hasImage() || source->hasUrls())) {
        const QString md = attachmentHandler_(source);
        if (!md.isEmpty()) {
            textCursor().insertText(md);
            return;
        }
    }
    if (source->hasText()) {
        // Plain text only; normalise line breaks so the buffer never gets U+2029/CR.
        QString t = source->text();
        t.replace(u"\r\n"_s, u"\n"_s);
        t.replace(u'\r', u'\n');
        t.replace(QChar(QChar::ParagraphSeparator), u'\n');
        t.replace(QChar(QChar::LineSeparator), u'\n');
        textCursor().insertText(t);
        return;
    }
    QPlainTextEdit::insertFromMimeData(source);
}

} // namespace mn::ui
