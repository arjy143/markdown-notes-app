#include "QuickSwitcher.h"

#include "core/vault/Paths.h"

#include <QAbstractListModel>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QVBoxLayout>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace mn::ui {

int fuzzyScore(QStringView q, QStringView c, int nameStart)
{
    if (q.isEmpty()) return 0;
    int score = 0;
    qsizetype ci = 0;
    qsizetype prev = -2;
    for (qsizetype qi = 0; qi < q.size(); ++qi) {
        const QChar qc = q[qi];
        if (qc == u' ') continue;
        while (ci < c.size() && c[ci] != qc)
            ++ci;
        if (ci >= c.size()) return -1;
        int s = 1;
        if (ci == prev + 1) s += 5; // contiguous
        if (ci == 0 || ci == nameStart || c[ci - 1] == u' ' || c[ci - 1] == u'/' || c[ci - 1] == u'-' ||
            c[ci - 1] == u'_')
            s += 8;                  // word start
        if (ci >= nameStart) s += 2; // in the file name rather than the folder
        score += s;
        prev = ci;
        ++ci;
    }
    // Prefer shorter candidates, and exact prefixes of the name.
    score -= static_cast<int>(c.size() / 8);
    if (c.mid(nameStart).startsWith(q)) score += 20;
    return score;
}

struct SwitcherItem {
    QString display; // shown text
    QString detail;  // folder / alias target
    QString path;
    QString key; // folded search key
    int nameStart = 0;
};

class SwitcherModel : public QAbstractListModel {
public:
    using QAbstractListModel::QAbstractListModel;

    std::vector<SwitcherItem> items;
    std::vector<int> shown;

    int rowCount(const QModelIndex& parent = {}) const override
    {
        return parent.isValid() ? 0 : static_cast<int>(shown.size());
    }
    QVariant data(const QModelIndex& idx, int role) const override
    {
        if (!idx.isValid() || idx.row() >= static_cast<int>(shown.size())) return {};
        const SwitcherItem& it = items[static_cast<std::size_t>(shown[static_cast<std::size_t>(idx.row())])];
        if (role == Qt::DisplayRole)
            return it.detail.isEmpty() ? it.display : it.display + u"    "_s + it.detail;
        if (role == Qt::ToolTipRole) return it.path;
        if (role == Qt::UserRole) return it.path;
        return {};
    }

    void filter(const QString& query)
    {
        beginResetModel();
        shown.clear();
        const QString q = query.toCaseFolded();
        if (q.trimmed().isEmpty()) {
            for (int i = 0; i < static_cast<int>(items.size()) && i < 200; ++i)
                shown.push_back(i);
            endResetModel();
            return;
        }
        std::vector<std::pair<int, int>> scored;
        scored.reserve(256);
        for (int i = 0; i < static_cast<int>(items.size()); ++i) {
            const SwitcherItem& it = items[static_cast<std::size_t>(i)];
            const int s = fuzzyScore(q, it.key, it.nameStart);
            if (s >= 0) scored.emplace_back(-s, i);
        }
        const std::size_t keep = std::min<std::size_t>(scored.size(), 200);
        std::partial_sort(scored.begin(), scored.begin() + static_cast<std::ptrdiff_t>(keep), scored.end());
        for (std::size_t k = 0; k < keep; ++k)
            shown.push_back(scored[k].second);
        endResetModel();
    }
};

QuickSwitcher::QuickSwitcher(QWidget* parent) : QDialog(parent, Qt::Popup | Qt::FramelessWindowHint)
{
    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(8, 8, 8, 8);
    edit_ = new QLineEdit(this);
    edit_->setPlaceholderText(tr("Find or create a note…"));
    list_ = new QListView(this);
    list_->setUniformItemSizes(true);
    list_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    hint_ = new QLabel(tr("↵ open   ⇧↵ create   esc close"), this);
    hint_->setEnabled(false);
    model_ = new SwitcherModel(this);
    list_->setModel(model_);
    lay->addWidget(edit_);
    lay->addWidget(list_);
    lay->addWidget(hint_);
    resize(640, 420);
    edit_->installEventFilter(this);
    connect(edit_, &QLineEdit::textChanged, this, [this](const QString& t) {
        model_->filter(t);
        if (model_->rowCount() > 0) list_->setCurrentIndex(model_->index(0));
    });
    connect(list_, &QListView::activated, this, [this] { accept(false); });
}

void QuickSwitcher::setFiles(const VaultSnapshot& snapshot)
{
    snapshot_ = &snapshot;
    stale_ = true;
    model_->items.clear();
    model_->items.shrink_to_fit();
    if (isVisible()) rebuild();
}

void QuickSwitcher::rebuild()
{
    stale_ = false;
    if (snapshot_ == nullptr) return;
    std::vector<SwitcherItem> items;
    items.reserve(snapshot_->files().size());
    for (const VaultFile& f : snapshot_->files()) {
        if (f.kind == FileKind::Other) continue;
        SwitcherItem it;
        it.display = f.kind == FileKind::Note ? paths::linkName(f.path) : paths::fileName(f.path);
        it.detail = paths::folderOf(f.path);
        it.path = f.path;
        QString keyPath = f.path;
        if (f.kind == FileKind::Note) keyPath.chop(3);
        it.key = keyPath.toCaseFolded();
        it.nameStart = static_cast<int>(keyPath.lastIndexOf(u'/') + 1);
        items.push_back(it);
        for (const QString& a : snapshot_->aliases.value(f.path)) {
            SwitcherItem al;
            al.display = a;
            al.detail = u"→ "_s + it.display;
            al.path = f.path;
            al.key = a.toCaseFolded();
            al.nameStart = 0;
            items.push_back(al);
        }
    }
    model_->items = std::move(items);
    model_->filter(edit_->text());
}

int QuickSwitcher::filterFor(const QString& query)
{
    if (stale_) rebuild();
    model_->filter(query);
    return model_->rowCount();
}

void QuickSwitcher::popup()
{
    if (QWidget* p = parentWidget()) {
        const QPoint center = p->mapToGlobal(QPoint(p->width() / 2, 0));
        move(center.x() - width() / 2, center.y() + 60);
    }
    if (stale_) rebuild();
    edit_->clear();
    model_->filter(QString());
    if (model_->rowCount() > 0) list_->setCurrentIndex(model_->index(0));
    show();
    raise();
    activateWindow();
    edit_->setFocus();
}

void QuickSwitcher::accept(bool create)
{
    const QString typed = edit_->text().trimmed();
    if (!create && list_->currentIndex().isValid()) {
        Q_EMIT openRequested(list_->currentIndex().data(Qt::UserRole).toString());
    } else if (!typed.isEmpty()) {
        Q_EMIT createRequested(typed);
    } else {
        return;
    }
    hide();
}

bool QuickSwitcher::eventFilter(QObject* obj, QEvent* e)
{
    if (obj == edit_ && e->type() == QEvent::KeyPress) {
        auto* k = static_cast<QKeyEvent*>(e);
        switch (k->key()) {
        case Qt::Key_Down:
        case Qt::Key_Up:
        case Qt::Key_PageDown:
        case Qt::Key_PageUp: {
            const int rows = model_->rowCount();
            if (rows == 0) return true;
            int r = list_->currentIndex().isValid() ? list_->currentIndex().row() : 0;
            const int step = (k->key() == Qt::Key_PageDown || k->key() == Qt::Key_PageUp) ? 10 : 1;
            r += (k->key() == Qt::Key_Down || k->key() == Qt::Key_PageDown) ? step : -step;
            list_->setCurrentIndex(model_->index(std::clamp(r, 0, rows - 1)));
            return true;
        }
        case Qt::Key_Return:
        case Qt::Key_Enter:
            accept((k->modifiers() & Qt::ShiftModifier) || model_->rowCount() == 0);
            return true;
        case Qt::Key_Escape:
            hide();
            return true;
        default:
            break;
        }
    }
    return QDialog::eventFilter(obj, e);
}

} // namespace mn::ui
