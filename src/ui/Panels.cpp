#include "Panels.h"

#include "HtmlRenderer.h"
#include "core/index/TagTree.h"
#include "core/index/VaultService.h"
#include "core/vault/Paths.h"

#include <QAbstractTextDocumentLayout>
#include <QApplication>
#include <QComboBox>
#include <QDateTime>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QPainter>
#include <QSettings>
#include <QSplitter>
#include <QTextDocument>
#include <QTimer>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <limits>

using namespace Qt::StringLiterals;

namespace mn::ui {

namespace {

constexpr int kPathRole = Qt::UserRole + 1;
constexpr int kLineRole = Qt::UserRole + 2;
constexpr int kHtmlRole = Qt::UserRole + 3;

QString snippetToHtml(const QString& s)
{
    QString out = htmlEscape(s);
    out.replace(QChar(1), u"<b>"_s);
    out.replace(QChar(2), u"</b>"_s);
    out.replace(u'\n', u' ');
    return out;
}

QString passageDate(const Passage& p)
{
    return QDateTime::fromMSecsSinceEpoch(p.dateMs).date().toString(Qt::ISODate);
}

// "Note name  date · heading" on the first line, then the passage.
QString passageHtml(const Passage& p, const QString& extra)
{
    QString meta = passageDate(p);
    if (!p.context.isEmpty()) meta += u" · "_s + p.context;
    if (!extra.isEmpty()) meta += u" · "_s + extra;
    return u"<b>"_s + htmlEscape(paths::linkName(p.path)) + u"</b> <span style=\"color:gray\">"_s +
           htmlEscape(meta) + u"</span><br/>"_s + snippetToHtml(p.snippet);
}

QTreeWidget* makeTree(QWidget* parent)
{
    auto* t = new QTreeWidget(parent);
    t->setHeaderHidden(true);
    t->setRootIsDecorated(true);
    t->setUniformRowHeights(false);
    t->setWordWrap(true);
    t->setItemDelegate(new HtmlDelegate(t));
    t->setTextElideMode(Qt::ElideRight);
    t->header()->setStretchLastSection(true);
    return t;
}

} // namespace

void HtmlDelegate::paint(QPainter* p, const QStyleOptionViewItem& optIn, const QModelIndex& idx) const
{
    const QString html = idx.data(kHtmlRole).toString();
    if (html.isEmpty()) {
        QStyledItemDelegate::paint(p, optIn, idx);
        return;
    }
    QStyleOptionViewItem opt = optIn;
    initStyleOption(&opt, idx);
    opt.text.clear();
    QStyle* style = opt.widget ? opt.widget->style() : QApplication::style();
    style->drawControl(QStyle::CE_ItemViewItem, &opt, p, opt.widget);
    QTextDocument doc;
    doc.setDefaultFont(opt.font);
    doc.setHtml(html);
    doc.setTextWidth(opt.rect.width());
    p->save();
    p->translate(opt.rect.topLeft());
    QAbstractTextDocumentLayout::PaintContext ctx;
    ctx.palette = opt.palette;
    if (opt.state & QStyle::State_Selected)
        ctx.palette.setColor(QPalette::Text, opt.palette.color(QPalette::HighlightedText));
    p->setClipRect(QRect(0, 0, opt.rect.width(), opt.rect.height()));
    doc.documentLayout()->draw(p, ctx);
    p->restore();
}

QSize HtmlDelegate::sizeHint(const QStyleOptionViewItem& opt, const QModelIndex& idx) const
{
    const QString html = idx.data(kHtmlRole).toString();
    if (html.isEmpty()) return QStyledItemDelegate::sizeHint(opt, idx);
    QTextDocument doc;
    doc.setDefaultFont(opt.font);
    doc.setHtml(html);
    const int w = opt.widget ? std::max(120, opt.widget->width() - 40) : 300;
    doc.setTextWidth(w);
    return {w, static_cast<int>(doc.size().height())};
}

// --- search -------------------------------------------------------------------

SearchPanel::SearchPanel(VaultService* vault, QWidget* parent) : QWidget(parent), vault_(vault)
{
    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(4, 4, 4, 4);
    edit_ = new QLineEdit(this);
    edit_->setPlaceholderText(tr("Search notes…"));
    edit_->setClearButtonEnabled(true);
    auto* options = new QHBoxLayout;
    mode_ = new QComboBox(this);
    mode_->addItems({tr("Notes"), tr("Passages")});
    mode_->setToolTip(tr("Notes: matching notes\nPassages: matching sections of notes"));
    mode_->setCurrentIndex(std::clamp(QSettings().value(u"search/mode"_s, 0).toInt(), 0, 1));
    sort_ = new QComboBox(this);
    sort_->addItems({tr("Best match"), tr("Newest first"), tr("Oldest first")});
    sort_->setCurrentIndex(std::clamp(QSettings().value(u"search/passageSort"_s, 0).toInt(), 0, 2));
    sort_->setEnabled(mode_->currentIndex() == 1);
    options->addWidget(mode_, 1);
    options->addWidget(sort_, 1);
    status_ = new QLabel(this);
    status_->setEnabled(false);
    results_ = makeTree(this);
    results_->setRootIsDecorated(false);
    lay->addWidget(edit_);
    lay->addLayout(options);
    lay->addWidget(status_);
    lay->addWidget(results_, 1);
    debounce_ = new QTimer(this);
    debounce_->setSingleShot(true);
    debounce_->setInterval(120);
    connect(debounce_, &QTimer::timeout, this, &SearchPanel::run);
    connect(edit_, &QLineEdit::textChanged, debounce_, qOverload<>(&QTimer::start));
    connect(edit_, &QLineEdit::returnPressed, this, &SearchPanel::run);
    connect(mode_, &QComboBox::currentIndexChanged, this, [this](int i) {
        QSettings().setValue(u"search/mode"_s, i);
        sort_->setEnabled(i >= 1);
        debounce_->setInterval(i == 2 ? 400 : 120); // meaning: one embedding request per pause
        run();
    });
    connect(sort_, &QComboBox::currentIndexChanged, this, [this](int i) {
        QSettings().setValue(u"search/passageSort"_s, i);
        showPassages();
    });
    // Notes open at the first match; passages at their first line.
    auto open = [this](QTreeWidgetItem* it) {
        Q_EMIT openRequested(it->data(0, kPathRole).toString(), it->data(0, kLineRole).toInt(),
                             edit_->text().section(u' ', 0, 0).remove(u'"'));
    };
    connect(results_, &QTreeWidget::itemActivated, this, open);
    connect(results_, &QTreeWidget::itemClicked, this, open);
}

void SearchPanel::setSemanticSearch(PassageSearch search)
{
    semantic_ = std::move(search);
    if (mode_->count() == 2) {
        mode_->addItem(tr("Meaning"));
        mode_->setItemData(2, tr("Semantic search (requires an embedding model)"), Qt::ToolTipRole);
        if (QSettings().value(u"search/mode"_s, 0).toInt() == 2) mode_->setCurrentIndex(2);
    }
}

void SearchPanel::focusSearch(const QString& text)
{
    if (!text.isEmpty()) edit_->setText(text);
    edit_->setFocus();
    edit_->selectAll();
}

void SearchPanel::run()
{
    const QString q = edit_->text().trimmed();
    const int req = ++request_;
    if (q.isEmpty()) {
        results_->clear();
        passages_.clear();
        status_->clear();
        return;
    }
    if (mode_->currentIndex() == 2 && semantic_) {
        status_->setText(tr("Searching…"));
        semantic_(q, [this, req](std::vector<Passage> hits, const QString& error) {
            if (req != request_) return;
            passages_ = std::move(hits);
            showPassages();
            if (!error.isEmpty())
                status_->setText(status_->text() + u" · "_s + tr("keyword matches only (%1)").arg(error));
        });
        return;
    }
    if (mode_->currentIndex() >= 1) {
        vault_->searchPassages(q, [this, req](std::vector<Passage> hits) {
            if (req != request_) return;
            passages_ = std::move(hits);
            showPassages();
        });
        return;
    }
    vault_->search(q, [this, req](std::vector<SearchHit> hits) {
        if (req != request_) return;
        results_->clear();
        for (const SearchHit& h : hits) {
            auto* top = new QTreeWidgetItem(results_);
            top->setText(0, paths::linkName(h.path));
            top->setToolTip(0, h.path);
            top->setData(0, kPathRole, h.path);
            top->setData(0, kLineRole, -1);
            top->setData(0, kHtmlRole,
                         QString(u"<b>"_s + htmlEscape(paths::linkName(h.path)) +
                                 u"</b> <span style=\"color:gray\">"_s + htmlEscape(paths::folderOf(h.path)) +
                                 u"</span><br/>"_s + snippetToHtml(h.snippet)));
        }
        status_->setText(hits.size() >= 200 ? tr("200+ notes")
                                            : tr("%n note(s)", nullptr, static_cast<int>(hits.size())));
    });
}

void SearchPanel::showPassages()
{
    if (mode_->currentIndex() < 1) return;
    results_->clear();
    std::vector<const Passage*> order;
    order.reserve(passages_.size());
    for (const Passage& p : passages_)
        order.push_back(&p);
    // Best match is the index's order; by date reads a topic as a timeline.
    if (sort_->currentIndex() == 1)
        std::stable_sort(order.begin(), order.end(), [](auto a, auto b) { return a->dateMs > b->dateMs; });
    else if (sort_->currentIndex() == 2)
        std::stable_sort(order.begin(), order.end(), [](auto a, auto b) { return a->dateMs < b->dateMs; });
    QSet<QString> notes;
    for (const Passage* p : order) {
        auto* it = new QTreeWidgetItem(results_);
        it->setText(0, paths::linkName(p->path));
        it->setToolTip(0, p->path);
        it->setData(0, kPathRole, p->path);
        it->setData(0, kLineRole, p->startLine);
        it->setData(0, kHtmlRole, passageHtml(*p, QString()));
        notes.insert(p->path);
    }
    const int n = static_cast<int>(passages_.size());
    status_->setText(n >= 200 ? tr("200+ passages")
                              : tr("%n passage(s)", nullptr, n) + u' ' +
                                    tr("in %n note(s)", nullptr, static_cast<int>(notes.size())));
}

// --- backlinks ------------------------------------------------------------------

BacklinksPanel::BacklinksPanel(VaultService* vault, QWidget* parent) : QWidget(parent), vault_(vault)
{
    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(4, 4, 4, 4);
    tree_ = makeTree(this);
    lay->addWidget(tree_);
    auto open = [this](QTreeWidgetItem* it) {
        const QString p = it->data(0, kPathRole).toString();
        if (!p.isEmpty()) Q_EMIT openRequested(p, it->data(0, kLineRole).toInt(), QString());
    };
    connect(tree_, &QTreeWidget::itemActivated, this, open);
    connect(tree_, &QTreeWidget::itemClicked, this, open);
    connect(tree_, &QTreeWidget::itemExpanded, this, [this](QTreeWidgetItem* it) {
        if (it == unlinked_ && !mentionsLoaded_) loadMentions();
    });
}

void BacklinksPanel::setNote(const QString& path)
{
    if (path != path_) relatedPassages_.clear();
    path_ = path;
    refresh();
}

void BacklinksPanel::setCursorPassage(const QString& noteText, int line)
{
    if (path_.isEmpty() || !vault_->isOpen()) return;
    const int req = ++passageRequest_;
    const QString path = path_;
    vault_->relatedPassages(noteText, line, path, [this, req, path](std::vector<Passage> found) {
        if (req != passageRequest_ || path != path_) return;
        auto key = [](const std::vector<Passage>& v) {
            QStringList k;
            for (const Passage& p : v)
                k.append(p.path + u':' + QString::number(p.startLine));
            return k;
        };
        if (key(found) == key(relatedPassages_) && passages_ != nullptr &&
            passages_->childCount() == int(found.size()))
            return; // same list: don't rebuild (and flicker) as the cursor moves within a passage
        relatedPassages_ = std::move(found);
        showRelatedPassages();
    });
}

void BacklinksPanel::showRelatedPassages()
{
    if (passages_ == nullptr) return;
    qDeleteAll(passages_->takeChildren());
    for (const Passage& p : relatedPassages_) {
        auto* item = new QTreeWidgetItem(passages_);
        item->setData(0, kHtmlRole, passageHtml(p, p.reason));
        item->setData(0, kPathRole, p.path);
        item->setData(0, kLineRole, p.startLine);
        item->setToolTip(0, p.text.left(1500));
    }
    passages_->setText(0, tr("Related to this paragraph (%1)").arg(relatedPassages_.size()));
}

void BacklinksPanel::refresh()
{
    tree_->clear();
    linked_ = related_ = passages_ = unlinked_ = nullptr;
    mentionsLoaded_ = false;
    if (path_.isEmpty() || !vault_->isOpen()) return;
    linked_ = new QTreeWidgetItem(tree_, {tr("Linked mentions")});
    related_ = new QTreeWidgetItem(tree_, {tr("Related notes")});
    passages_ = new QTreeWidgetItem(tree_, {tr("Related to this paragraph")});
    passages_->setToolTip(0, tr("Similar passages in other notes"));
    unlinked_ = new QTreeWidgetItem(tree_, {tr("Unlinked mentions")});
    unlinked_->setChildIndicatorPolicy(QTreeWidgetItem::ShowIndicator);
    QFont bold = linked_->font(0);
    bold.setBold(true);
    linked_->setFont(0, bold);
    related_->setFont(0, bold);
    passages_->setFont(0, bold);
    unlinked_->setFont(0, bold);
    linked_->setExpanded(true);
    related_->setExpanded(true);
    passages_->setExpanded(true);
    showRelatedPassages();
    const int req = ++request_;
    const QString path = path_;
    vault_->backlinks(path, [this, req](std::vector<Backlink> links) {
        if (req != request_ || linked_ == nullptr) return;
        QHash<QString, QTreeWidgetItem*> bySource;
        for (const Backlink& b : links) {
            QTreeWidgetItem* src = bySource.value(b.sourcePath);
            if (src == nullptr) {
                src = new QTreeWidgetItem(linked_, {paths::linkName(b.sourcePath)});
                src->setData(0, kPathRole, b.sourcePath);
                src->setData(0, kLineRole, b.line);
                src->setToolTip(0, b.sourcePath);
                src->setExpanded(true);
                bySource.insert(b.sourcePath, src);
            }
            auto* line = new QTreeWidgetItem(src);
            line->setData(0, kHtmlRole, htmlEscape(b.context));
            line->setData(0, kPathRole, b.sourcePath);
            line->setData(0, kLineRole, b.line);
        }
        linked_->setText(0, tr("Linked mentions (%1)").arg(links.size()));
    });
    // Related notes: shared distinctive words (TF-IDF), links and tags.
    vault_->relatedNotes(path, [this, req](std::vector<RelatedNote> related) {
        if (req != request_ || related_ == nullptr) return;
        for (const RelatedNote& r : related) {
            auto* item = new QTreeWidgetItem(related_);
            item->setData(0, kHtmlRole,
                          QString(htmlEscape(paths::linkName(r.path)) +
                                  u"<br/><span style=\"color:gray\">"_s + htmlEscape(r.reason) +
                                  u"</span>"_s));
            item->setData(0, kPathRole, r.path);
            item->setData(0, kLineRole, -1);
            item->setToolTip(0, r.path);
        }
        related_->setText(0, tr("Related notes (%1)").arg(related.size()));
    });
}

void BacklinksPanel::loadMentions()
{
    mentionsLoaded_ = true;
    const int req = request_;
    unlinked_->setText(0, tr("Unlinked mentions (searching…)"));
    vault_->unlinkedMentions(path_, [this, req](std::vector<Mention> ms) {
        if (req != request_ || unlinked_ == nullptr) return;
        QHash<QString, QTreeWidgetItem*> bySource;
        for (const Mention& m : ms) {
            QTreeWidgetItem* src = bySource.value(m.sourcePath);
            if (src == nullptr) {
                src = new QTreeWidgetItem(unlinked_, {paths::linkName(m.sourcePath)});
                src->setData(0, kPathRole, m.sourcePath);
                src->setData(0, kLineRole, m.line);
                src->setExpanded(true);
                bySource.insert(m.sourcePath, src);
            }
            auto* line = new QTreeWidgetItem(src);
            line->setData(0, kHtmlRole, htmlEscape(m.context));
            line->setData(0, kPathRole, m.sourcePath);
            line->setData(0, kLineRole, m.line);
        }
        unlinked_->setText(0, tr("Unlinked mentions (%1)").arg(ms.size()));
    });
}

// --- tags -------------------------------------------------------------------------

namespace {
constexpr int kCountRole = Qt::UserRole + 4;
constexpr int kCategoryRole = Qt::UserRole + 5;
constexpr int kPinRole = Qt::UserRole + 6; // position in the pinned list (top-level items only)

int pinRank(const QTreeWidgetItem& it)
{
    const QVariant v = it.data(0, kPinRole);
    return v.isValid() ? v.toInt() : std::numeric_limits<int>::max();
}

// A small push-pin in the palette's accent colour.
QIcon pinIcon(const QPalette& pal)
{
    QPixmap pm(32, 32);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    const QColor c = pal.color(QPalette::Active, QPalette::Highlight);
    p.setPen(QPen(c, 3, Qt::SolidLine, Qt::RoundCap));
    p.drawLine(QPointF(16, 17), QPointF(16, 29));
    p.setPen(Qt::NoPen);
    p.setBrush(c);
    p.drawRoundedRect(QRectF(10, 3, 12, 9), 2, 2);
    p.drawRoundedRect(QRectF(6, 12, 20, 6), 2, 2);
    return QIcon(pm);
}

// Sorts by name, or numerically by note count (ties stay alphabetical).
class TagItem : public QTreeWidgetItem {
public:
    using QTreeWidgetItem::QTreeWidgetItem;
    bool operator<(const QTreeWidgetItem& other) const override
    {
        const int byName = text(0).compare(other.text(0), Qt::CaseInsensitive);
        const bool descending =
            treeWidget() != nullptr && treeWidget()->header()->sortIndicatorOrder() == Qt::DescendingOrder;
        // Pinned tags come first, in pin order, whichever way the list is sorted.
        const int pa = pinRank(*this);
        const int pb = pinRank(other);
        if (pa != pb) return descending ? pa > pb : pa < pb;
        // Category tags stay at the top whichever way the list is sorted.
        const bool cat = data(0, kCategoryRole).toBool();
        if (cat != other.data(0, kCategoryRole).toBool()) return descending ? !cat : cat;
        if (treeWidget() == nullptr || treeWidget()->sortColumn() != 1) return byName < 0;
        const int a = data(1, kCountRole).toInt();
        const int b = other.data(1, kCountRole).toInt();
        if (a != b) return a < b;
        // Keep names A→Z whichever way the counts are sorted.
        return descending ? byName > 0 : byName < 0;
    }
};
} // namespace

TagPanel::TagPanel(VaultService* vault, QWidget* parent) : QWidget(parent), vault_(vault)
{
    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(4, 4, 4, 4);
    filter_ = new QLineEdit(this);
    filter_->setPlaceholderText(tr("Find a tag…"));
    filter_->setClearButtonEnabled(true);
    lay->addWidget(filter_);
    sort_ = new QComboBox(this);
    sort_->addItems({tr("Sort by name"), tr("Most used first"), tr("Least used first")});
    sort_->setCurrentIndex(std::clamp(QSettings().value(u"tags/sort"_s, 0).toInt(), 0, 2));
    lay->addWidget(sort_);
    auto* split = new QSplitter(Qt::Vertical, this);
    tree_ = new QTreeWidget(split);
    tree_->setHeaderHidden(true);
    tree_->setColumnCount(2);
    tree_->header()->setStretchLastSection(false);
    tree_->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    tree_->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    auto* bottom = new QWidget(split);
    auto* bl = new QVBoxLayout(bottom);
    bl->setContentsMargins(0, 0, 0, 0);
    auto* head = new QHBoxLayout;
    label_ = new QLabel(bottom);
    grouping_ = new QComboBox(bottom);
    grouping_->addItems({tr("Group by other tags"), tr("Plain list")});
    grouping_->setCurrentIndex(std::clamp(QSettings().value(u"tags/grouping"_s, 0).toInt(), 0, 1));
    head->addWidget(label_, 1);
    head->addWidget(grouping_);
    notes_ = new QTreeWidget(bottom);
    notes_->setHeaderHidden(true);
    notes_->setColumnCount(2);
    notes_->header()->setStretchLastSection(false);
    notes_->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    notes_->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    bl->addLayout(head);
    bl->addWidget(notes_);
    lay->addWidget(split);
    connect(tree_, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem* it) {
        if (it) showFiles(it->data(0, kPathRole).toString());
    });
    tree_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(tree_, &QTreeWidget::customContextMenuRequested, this, &TagPanel::showMenu);
    connect(filter_, &QLineEdit::textChanged, this, &TagPanel::applyFilter);
    // Enter opens the first match's notes; Down moves into the list.
    connect(filter_, &QLineEdit::returnPressed, this, [this] {
        for (QTreeWidgetItemIterator it(tree_, QTreeWidgetItemIterator::NotHidden); *it; ++it) {
            if ((*it)
                    ->data(0, kPathRole)
                    .toString()
                    .contains(filter_->text().trimmed().remove(u'#'), Qt::CaseInsensitive)) {
                tree_->setCurrentItem(*it);
                tree_->setFocus();
                return;
            }
        }
    });
    filter_->installEventFilter(this);
    connect(sort_, &QComboBox::currentIndexChanged, this, [this](int i) {
        QSettings().setValue(u"tags/sort"_s, i);
        applySort();
    });
    connect(grouping_, &QComboBox::currentIndexChanged, this, [this](int i) {
        QSettings().setValue(u"tags/grouping"_s, i);
        fillNotes();
    });
    // Clicking a note opens it; clicking a sub-tag folder expands it.
    connect(notes_, &QTreeWidget::itemClicked, this, [this](QTreeWidgetItem* it) {
        const QString path = it->data(0, kPathRole).toString();
        if (path.isEmpty())
            it->setExpanded(!it->isExpanded());
        else
            Q_EMIT openRequested(path, -1, QString(u'#' + current_));
    });
}

void TagPanel::refresh()
{
    // Nested tags: "a/b/c" counts towards "a" and "a/b" (counts are notes per exact tag,
    // and parents show the sum of their own and their children's counts).
    QSet<QString> expanded;
    for (QTreeWidgetItemIterator it(tree_); *it; ++it) {
        if ((*it)->isExpanded()) expanded.insert((*it)->data(0, kPathRole).toString().toCaseFolded());
    }
    tree_->clear();
    noteTagsLoaded_ = false; // the vault changed: re-read the notes' tags when needed
    QSet<QString> categories;
    for (const QString& c : vault_->settings().categoryTags)
        categories.insert(c.toCaseFolded());
    QHash<QString, QTreeWidgetItem*> nodes;
    QHash<QTreeWidgetItem*, int> counts;
    for (const TagCount& t : vault_->snapshot().tags) {
        const QStringList parts = t.tag.split(u'/', Qt::SkipEmptyParts);
        QString prefix;
        QTreeWidgetItem* parent = nullptr;
        for (const QString& part : parts) {
            prefix = prefix.isEmpty() ? part : prefix + u'/' + part;
            const QString key = prefix.toCaseFolded();
            QTreeWidgetItem* node = nodes.value(key);
            if (node == nullptr) {
                node = parent ? new TagItem(parent) : new TagItem(tree_);
                node->setText(0, u'#' + part);
                node->setData(0, kPathRole, prefix);
                if (parent == nullptr && categories.contains(key)) {
                    QFont f = node->font(0);
                    f.setBold(true);
                    node->setFont(0, f);
                    node->setData(0, kCategoryRole, true);
                    node->setToolTip(0, tr("Category tag"));
                }
                nodes.insert(key, node);
            }
            counts[node] += t.notes;
            parent = node;
        }
    }
    for (auto it = counts.cbegin(); it != counts.cend(); ++it) {
        it.key()->setText(1, QString::number(it.value()));
        it.key()->setData(1, kCountRole, it.value());
    }
    for (auto it = nodes.cbegin(); it != nodes.cend(); ++it) {
        if (expanded.contains(it.key())) it.value()->setExpanded(true);
    }
    // Pinned tags: a top-level tag moves to the top; a nested one ("tech/ai") gets its
    // own top-level entry under its full name. Pins for tags no note has are skipped.
    const QStringList& pinned = vault_->settings().pinnedTags;
    if (!pinned.isEmpty()) {
        const QIcon icon = pinIcon(palette());
        int rank = 0;
        QSet<QString> seen;
        for (const QString& tag : pinned) {
            const QString key = tag.toCaseFolded();
            QTreeWidgetItem* node = nodes.value(key);
            if (node == nullptr || seen.contains(key)) continue;
            seen.insert(key);
            if (node->parent() != nullptr) {
                auto* top = new TagItem(tree_);
                top->setText(0, u'#' + node->data(0, kPathRole).toString());
                top->setData(0, kPathRole, node->data(0, kPathRole));
                top->setText(1, node->text(1));
                top->setData(1, kCountRole, node->data(1, kCountRole));
                node = top;
            }
            node->setData(0, kPinRole, rank++);
            node->setIcon(0, icon);
            node->setToolTip(0, tr("Pinned tag"));
        }
    }
    applySort();
    applyFilter();
    if (!current_.isEmpty()) {
        // Keep the selected tag highlighted across refreshes.
        for (QTreeWidgetItemIterator it(tree_); *it; ++it) {
            if ((*it)->data(0, kPathRole).toString().compare(current_, Qt::CaseInsensitive) == 0) {
                const QSignalBlocker block(tree_);
                tree_->setCurrentItem(*it);
                break;
            }
        }
        showFiles(current_);
    }
}

void TagPanel::showMenu(const QPoint& pos)
{
    QTreeWidgetItem* it = tree_->itemAt(pos);
    if (it == nullptr) return;
    const QString tag = it->data(0, kPathRole).toString();
    const QStringList& pinned = vault_->settings().pinnedTags;
    qsizetype index = -1;
    for (qsizetype i = 0; i < pinned.size(); ++i) {
        if (pinned[i].compare(tag, Qt::CaseInsensitive) == 0) index = i;
    }
    QMenu menu(this);
    if (index < 0) {
        menu.addAction(tr("Pin to top"), this, [this, tag] { setPinned(tag, true); });
    } else {
        menu.addAction(tr("Unpin"), this, [this, tag] { setPinned(tag, false); });
        if (index > 0) menu.addAction(tr("Move up"), this, [this, tag] { movePinned(tag, -1); });
        if (index + 1 < pinned.size())
            menu.addAction(tr("Move down"), this, [this, tag] { movePinned(tag, 1); });
    }
    menu.exec(tree_->viewport()->mapToGlobal(pos));
}

void TagPanel::setPinned(const QString& tag, bool pinned)
{
    QStringList list = vault_->settings().pinnedTags;
    list.removeIf([&](const QString& t) { return t.compare(tag, Qt::CaseInsensitive) == 0; });
    if (pinned) list.append(tag);
    savePinned(list);
}

void TagPanel::movePinned(const QString& tag, int delta)
{
    QStringList list = vault_->settings().pinnedTags;
    for (qsizetype i = 0; i < list.size(); ++i) {
        if (list[i].compare(tag, Qt::CaseInsensitive) != 0) continue;
        const qsizetype j = i + delta;
        if (j < 0 || j >= list.size()) return;
        list.swapItemsAt(i, j);
        savePinned(list);
        return;
    }
}

void TagPanel::savePinned(const QStringList& pinned)
{
    VaultSettings s = vault_->settings();
    if (s.pinnedTags == pinned) return;
    s.pinnedTags = pinned;
    QString err;
    if (!vault_->setSettings(s, &err)) {
        QMessageBox::warning(this, tr("Tags"), tr("Couldn't save the pinned tags: %1").arg(err));
        return;
    }
    refresh();
}

bool TagPanel::eventFilter(QObject* o, QEvent* e)
{
    if (o == filter_ && e->type() == QEvent::KeyPress) {
        const int key = static_cast<QKeyEvent*>(e)->key();
        if (key == Qt::Key_Down) {
            QTreeWidgetItemIterator it(tree_, QTreeWidgetItemIterator::NotHidden);
            if (tree_->currentItem() == nullptr || tree_->currentItem()->isHidden()) {
                if (*it) tree_->setCurrentItem(*it);
            }
            tree_->setFocus();
            return true;
        }
        if (key == Qt::Key_Escape && !filter_->text().isEmpty()) {
            filter_->clear();
            return true;
        }
    }
    return QWidget::eventFilter(o, e);
}

void TagPanel::setFilter(const QString& text)
{
    filter_->setText(text);
}

void TagPanel::applyFilter()
{
    const QString needle = filter_->text().trimmed().remove(u'#');
    // Bottom-up: an item is shown if it matches or a child is shown.
    std::function<bool(QTreeWidgetItem*)> visit = [&](QTreeWidgetItem* item) {
        bool childShown = false;
        for (int i = 0; i < item->childCount(); ++i)
            childShown = visit(item->child(i)) || childShown;
        const bool match =
            needle.isEmpty() || item->data(0, kPathRole).toString().contains(needle, Qt::CaseInsensitive);
        item->setHidden(!match && !childShown);
        // While filtering, open parents so nested matches are visible.
        if (!needle.isEmpty() && childShown) item->setExpanded(true);
        return match || childShown;
    };
    for (int i = 0; i < tree_->topLevelItemCount(); ++i)
        visit(tree_->topLevelItem(i));
}

void TagPanel::applySort()
{
    switch (sort_->currentIndex()) {
    case 1:
        tree_->sortItems(1, Qt::DescendingOrder);
        break;
    case 2:
        tree_->sortItems(1, Qt::AscendingOrder);
        break;
    default:
        tree_->sortItems(0, Qt::AscendingOrder);
        break;
    }
}

void TagPanel::selectTag(const QString& tag)
{
    for (QTreeWidgetItemIterator it(tree_); *it; ++it) {
        if ((*it)->data(0, kPathRole).toString().compare(tag, Qt::CaseInsensitive) == 0) {
            tree_->setCurrentItem(*it);
            return;
        }
    }
    showFiles(tag);
}

void TagPanel::showFiles(const QString& tag)
{
    current_ = tag;
    label_->setText(tr("Notes tagged #%1").arg(tag));
    if (noteTagsLoaded_) {
        fillNotes();
        return;
    }
    const int req = ++request_;
    vault_->noteTags([this, req](std::vector<NoteTags> tags) {
        if (req != request_) return;
        noteTags_ = std::move(tags);
        noteTagsLoaded_ = true;
        fillNotes();
    });
}

void TagPanel::fillNotes()
{
    notes_->clear();
    if (current_.isEmpty() || !noteTagsLoaded_) return;
    // Display spellings and note counts per tag key.
    QHash<QString, QString> spelling;
    QHash<QString, int> counts;
    for (const TagCount& t : vault_->snapshot().tags) {
        spelling.insert(t.tag.toCaseFolded(), t.tag);
        counts.insert(t.tag.toCaseFolded(), t.notes);
    }
    auto noteItem = [](QTreeWidgetItem* parent, QTreeWidget* tree, const QString& path) {
        auto* it = parent ? new QTreeWidgetItem(parent) : new QTreeWidgetItem(tree);
        it->setText(0, paths::linkName(path));
        it->setData(0, kPathRole, path);
        it->setToolTip(0, path);
        return it;
    };
    TagTreeOptions options;
    for (const QString& c : vault_->settings().categoryTags)
        options.categories.append(c.toCaseFolded());
    const TagTreeNode root = buildTagTree(current_, noteTags_, counts, options);
    label_->setText(tr("#%1 · %n note(s)", nullptr, root.noteCount).arg(current_));
    if (grouping_->currentIndex() == 1) {
        // Plain list of every note under the tag.
        QStringList all;
        std::function<void(const TagTreeNode&)> collect = [&](const TagTreeNode& n) {
            all += n.notes;
            for (const TagTreeNode& c : n.children)
                collect(c);
        };
        collect(root);
        all.sort(Qt::CaseInsensitive);
        for (const QString& p : std::as_const(all))
            noteItem(nullptr, notes_, p);
        return;
    }
    QFont folderFont = notes_->font();
    folderFont.setBold(true);
    std::function<void(const TagTreeNode&, QTreeWidgetItem*)> add = [&](const TagTreeNode& n,
                                                                        QTreeWidgetItem* parent) {
        for (const TagTreeNode& c : n.children) {
            auto* folder = parent ? new QTreeWidgetItem(parent) : new QTreeWidgetItem(notes_);
            folder->setText(0, u'#' + spelling.value(c.tag, c.tag));
            folder->setText(1, QString::number(c.noteCount));
            folder->setFont(0, folderFont);
            folder->setToolTip(0, tr("Notes tagged #%1 and #%2").arg(current_, spelling.value(c.tag, c.tag)));
            add(c, folder);
        }
        for (const QString& p : n.notes)
            noteItem(parent, notes_, p);
    };
    add(root, nullptr);
    for (int i = 0; i < notes_->topLevelItemCount(); ++i) {
        if (notes_->topLevelItem(i)->childCount() > 0 && notes_->topLevelItemCount() <= 12)
            notes_->topLevelItem(i)->setExpanded(true);
    }
}

} // namespace mn::ui
