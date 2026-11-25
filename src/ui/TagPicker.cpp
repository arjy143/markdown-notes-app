#include "TagPicker.h"

#include "QuickSwitcher.h"
#include "core/ai/FrontmatterMerge.h"

#include <QHash>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <tuple>

using namespace Qt::StringLiterals;

namespace mn::ui {

namespace {

constexpr int kMaxResults = 50;
constexpr int kTagRole = Qt::UserRole;

bool containsTag(const QStringList& list, const QString& tag)
{
    return std::any_of(list.begin(), list.end(),
                       [&](const QString& t) { return t.compare(tag, Qt::CaseInsensitive) == 0; });
}

} // namespace

TagPicker::TagPicker(QWidget* parent) : QWidget(parent)
{
    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);

    chosen_ = new QListWidget(this);
    chosen_->setFlow(QListView::LeftToRight);
    chosen_->setWrapping(true);
    chosen_->setResizeMode(QListView::Adjust);
    chosen_->setSpacing(3);
    chosen_->setSelectionMode(QAbstractItemView::NoSelection);
    chosen_->setFocusPolicy(Qt::NoFocus);
    chosen_->setFixedHeight(chosen_->fontMetrics().height() * 3 + 16);
    chosen_->setToolTip(tr("Click a tag to remove it"));
    connect(chosen_, &QListWidget::itemClicked, this,
            [this](QListWidgetItem* it) { removeTag(it->data(kTagRole).toString()); });
    none_ = new QLabel(tr("No tags"), chosen_);
    none_->setEnabled(false);
    none_->move(8, 6);
    lay->addWidget(chosen_);

    search_ = new QLineEdit(this);
    search_->setPlaceholderText(tr("Add a tag…"));
    search_->setClearButtonEnabled(true);
    search_->installEventFilter(this);
    lay->addWidget(search_);
    connect(search_, &QLineEdit::textChanged, this, &TagPicker::refreshResults);

    results_ = new QListWidget(this);
    results_->setUniformItemSizes(true);
    results_->setFocusPolicy(Qt::NoFocus);
    lay->addWidget(results_, 1);
    connect(results_, &QListWidget::itemClicked, this, [this](QListWidgetItem* it) {
        results_->setCurrentItem(it);
        addCurrent();
    });
    refreshChosen();
}

void TagPicker::setVaultTags(const std::vector<TagCount>& tags)
{
    vault_ = tags;
    refreshResults();
}

void TagPicker::setTags(const QStringList& tags)
{
    tags_.clear();
    for (const QString& t : tags) {
        if (!t.isEmpty() && !containsTag(tags_, t)) tags_.append(t);
    }
    refreshChosen();
    refreshResults();
}

void TagPicker::addTag(const QString& tagIn)
{
    QString tag = cleanTag(tagIn);
    if (tag.isEmpty() || containsTag(tags_, tag)) return;
    for (const TagCount& t : vault_) {
        if (t.tag.compare(tag, Qt::CaseInsensitive) == 0) tag = t.tag;
    }
    tags_.append(tag);
    refreshChosen();
    search_->clear();
    refreshResults();
    Q_EMIT changed();
}

void TagPicker::removeTag(const QString& tag)
{
    const qsizetype before = tags_.size();
    tags_.removeIf([&](const QString& t) { return t.compare(tag, Qt::CaseInsensitive) == 0; });
    if (tags_.size() == before) return;
    refreshChosen();
    refreshResults();
    Q_EMIT changed();
}

void TagPicker::refreshChosen()
{
    chosen_->clear();
    for (const QString& t : std::as_const(tags_)) {
        auto* it = new QListWidgetItem(u'#' + t + u"  ✕"_s, chosen_);
        it->setData(kTagRole, t);
        it->setToolTip(tr("Remove #%1").arg(t));
    }
    none_->setVisible(tags_.isEmpty());
}

void TagPicker::refreshResults()
{
    QString query = search_->text().trimmed();
    while (query.startsWith(u'#'))
        query.remove(0, 1);
    const QString folded = query.toCaseFolded();
    const QString look = tagLookAlikeKey(query);

    struct Row {
        const TagCount* tag;
        QString group; // look-alike key
        int fuzzy;     // -1: no fuzzy match
        bool alike;    // looks like the search
    };
    std::vector<Row> rows;
    for (const TagCount& t : vault_) {
        if (containsTag(tags_, t.tag)) continue;
        Row r{&t, tagLookAlikeKey(t.tag), 0, false};
        if (!folded.isEmpty()) {
            const QString cand = t.tag.toCaseFolded();
            r.fuzzy = fuzzyScore(folded, cand, static_cast<int>(cand.lastIndexOf(u'/') + 1));
            r.alike = !look.isEmpty() && r.group == look;
            if (r.fuzzy < 0 && !r.alike) continue;
        }
        rows.push_back(r);
    }
    // Spellings of the same tag rank together, at the best score among them, and
    // the most used spelling first: that's the one to reuse.
    QHash<QString, int> best;
    QHash<QString, int> most;
    for (const Row& r : rows) {
        best[r.group] = std::max(best.value(r.group, -1), r.fuzzy);
        most[r.group] = std::max(most.value(r.group, 0), r.tag->notes);
    }
    auto key = [&](const Row& r) {
        return std::tuple(r.alike, best.value(r.group), most.value(r.group), r.group, r.tag->notes);
    };
    std::stable_sort(rows.begin(), rows.end(), [&](const Row& a, const Row& b) { return key(a) > key(b); });
    if (rows.size() > kMaxResults) rows.resize(kMaxResults);

    results_->clear();
    for (const Row& r : rows) {
        const QString count = r.tag->notes == 1 ? tr("1 note") : tr("%1 notes").arg(r.tag->notes);
        auto* it = new QListWidgetItem(u'#' + r.tag->tag + u"   "_s + count, results_);
        it->setData(kTagRole, r.tag->tag);
    }
    const QString clean = cleanTag(query);
    bool known = containsTag(tags_, clean);
    for (const TagCount& t : vault_)
        known = known || t.tag.compare(clean, Qt::CaseInsensitive) == 0;
    if (!clean.isEmpty() && !known) {
        auto* it = new QListWidgetItem(tr("New tag: #%1").arg(clean), results_);
        it->setData(kTagRole, clean);
        QFont f = it->font();
        f.setItalic(true);
        it->setFont(f);
    }
    if (results_->count() > 0) results_->setCurrentRow(0);
}

void TagPicker::addCurrent()
{
    if (QListWidgetItem* it = results_->currentItem()) addTag(it->data(kTagRole).toString());
}

bool TagPicker::eventFilter(QObject* o, QEvent* e)
{
    if (o == search_ && e->type() == QEvent::KeyPress) {
        auto* k = static_cast<QKeyEvent*>(e);
        switch (k->key()) {
        case Qt::Key_Return:
        case Qt::Key_Enter:
            if (search_->text().trimmed().isEmpty())
                Q_EMIT submitted();
            else
                addCurrent();
            return true;
        case Qt::Key_Down:
        case Qt::Key_Up:
        case Qt::Key_PageDown:
        case Qt::Key_PageUp: {
            const int step = (k->key() == Qt::Key_Down       ? 1
                              : k->key() == Qt::Key_Up       ? -1
                              : k->key() == Qt::Key_PageDown ? 8
                                                             : -8);
            const int row = std::clamp(results_->currentRow() + step, 0, std::max(0, results_->count() - 1));
            results_->setCurrentRow(row);
            return true;
        }
        case Qt::Key_Backspace:
            if (search_->text().isEmpty() && !tags_.isEmpty()) {
                removeTag(tags_.last());
                return true;
            }
            break;
        default:
            break;
        }
    }
    return QWidget::eventFilter(o, e);
}

} // namespace mn::ui
