#include "TasksPanel.h"

#include "Theme.h"
#include "core/index/VaultService.h"
#include "core/vault/Paths.h"

#include <QComboBox>
#include <QDate>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QTimer>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace mn::ui {

namespace {
constexpr int kPathRole = Qt::UserRole + 1;
constexpr int kLineRole = Qt::UserRole + 2;
constexpr int kTextRole = Qt::UserRole + 3;
constexpr int kDoneRole = Qt::UserRole + 4;
} // namespace

TasksPanel::TasksPanel(VaultService* vault, QWidget* parent) : QWidget(parent), vault_(vault)
{
    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(4, 4, 4, 4);
    filter_ = new QLineEdit(this);
    filter_->setPlaceholderText(tr("Filter: words, #tag, folder/"));
    filter_->setClearButtonEnabled(true);
    auto* row = new QHBoxLayout;
    show_ = new QComboBox(this);
    show_->addItems({tr("Open"), tr("Done"), tr("All")});
    sort_ = new QComboBox(this);
    sort_->addItems({tr("By due date"), tr("By note")});
    row->addWidget(show_);
    row->addWidget(sort_);
    list_ = new QTreeWidget(this);
    list_->setHeaderLabels({tr("Task"), tr("Due"), tr("Note")});
    list_->setRootIsDecorated(false);
    list_->setUniformRowHeights(true);
    list_->header()->setStretchLastSection(false);
    list_->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    list_->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    list_->header()->setSectionResizeMode(2, QHeaderView::Interactive);
    status_ = new QLabel(this);
    status_->setEnabled(false);
    lay->addWidget(filter_);
    lay->addLayout(row);
    lay->addWidget(list_, 1);
    lay->addWidget(status_);

    debounce_ = new QTimer(this);
    debounce_->setSingleShot(true);
    debounce_->setInterval(150);
    connect(debounce_, &QTimer::timeout, this, &TasksPanel::applyFilter);
    connect(filter_, &QLineEdit::textChanged, debounce_, qOverload<>(&QTimer::start));
    connect(show_, &QComboBox::currentIndexChanged, this, &TasksPanel::applyFilter);
    connect(sort_, &QComboBox::currentIndexChanged, this, &TasksPanel::applyFilter);
    auto open = [this](QTreeWidgetItem* it) {
        Q_EMIT openRequested(it->data(0, kPathRole).toString(), it->data(0, kLineRole).toInt(), QString());
    };
    connect(list_, &QTreeWidget::itemActivated, this, open);
    connect(list_, &QTreeWidget::itemClicked, this, [this, open](QTreeWidgetItem* it) {
        // A click on the checkbox ticks the task; a click elsewhere opens it.
        if (it == justToggled_) {
            justToggled_ = nullptr;
            return;
        }
        open(it);
    });
    connect(list_, &QTreeWidget::itemChanged, this, [this](QTreeWidgetItem* it, int column) {
        if (filling_ || column != 0) return;
        const bool done = it->checkState(0) == Qt::Checked;
        if (done == it->data(0, kDoneRole).toBool()) return;
        justToggled_ = it;
        const TaskInfo task = taskOf(it);
        it->setData(0, kDoneRole, done);
        Q_EMIT taskEditRequested(task, done ? TaskAction::MarkDone : TaskAction::MarkOpen);
    });
    list_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(list_, &QTreeWidget::customContextMenuRequested, this, &TasksPanel::contextMenu);
    list_->installEventFilter(this);
}

TaskInfo TasksPanel::taskOf(const QTreeWidgetItem* item) const
{
    TaskInfo t;
    t.path = item->data(0, kPathRole).toString();
    t.line = item->data(0, kLineRole).toInt();
    t.text = item->data(0, kTextRole).toString();
    t.done = item->data(0, kDoneRole).toBool();
    t.due = item->text(1);
    return t;
}

void TasksPanel::requestDelete(QTreeWidgetItem* item)
{
    if (item != nullptr) Q_EMIT taskEditRequested(taskOf(item), TaskAction::Delete);
}

void TasksPanel::contextMenu(const QPoint& pos)
{
    QTreeWidgetItem* it = list_->itemAt(pos);
    if (it == nullptr) return;
    const TaskInfo t = taskOf(it);
    QMenu menu(this);
    menu.addAction(tr("Open in note"), this, [this, t] { Q_EMIT openRequested(t.path, t.line, QString()); });
    menu.addAction(t.done ? tr("Mark as not done") : tr("Mark as done"), this, [this, t] {
        Q_EMIT taskEditRequested(t, t.done ? TaskAction::MarkOpen : TaskAction::MarkDone);
    });
    menu.addSeparator();
    menu.addAction(tr("Delete task…"), this, [this, it] { requestDelete(it); });
    menu.exec(list_->viewport()->mapToGlobal(pos));
}

bool TasksPanel::eventFilter(QObject* o, QEvent* e)
{
    if (o == list_ && e->type() == QEvent::KeyPress) {
        auto* k = static_cast<QKeyEvent*>(e);
        if (k->key() == Qt::Key_Delete || k->key() == Qt::Key_Backspace) {
            requestDelete(list_->currentItem());
            return true;
        }
        if (k->key() == Qt::Key_Space && list_->currentItem() != nullptr) {
            QTreeWidgetItem* it = list_->currentItem();
            it->setCheckState(0, it->checkState(0) == Qt::Checked ? Qt::Unchecked : Qt::Checked);
            justToggled_ = nullptr;
            return true;
        }
    }
    return QWidget::eventFilter(o, e);
}

void TasksPanel::showEvent(QShowEvent* e)
{
    QWidget::showEvent(e);
    if (stale_) refresh();
}

void TasksPanel::refresh()
{
    if (!isVisible()) {
        stale_ = true; // fetched when the tab is shown
        return;
    }
    stale_ = false;
    notesWithTag_.clear();
    const int req = ++request_;
    vault_->tasks([this, req](std::vector<TaskInfo> tasks) {
        if (req != request_) return;
        tasks_ = std::move(tasks);
        applyFilter();
    });
}

void TasksPanel::applyFilter()
{
    QStringList words;
    QStringList tags;
    QStringList folders;
    for (const QString& tok : filter_->text().split(u' ', Qt::SkipEmptyParts)) {
        if (tok.startsWith(u'#') && tok.size() > 1)
            tags.append(tok.mid(1).toCaseFolded());
        else if (tok.contains(u'/'))
            folders.append(tok.endsWith(u'/') ? tok.chopped(1) : tok);
        else
            words.append(tok);
    }
    // Notes carrying a tag (frontmatter or inline) come from the index, once per tag.
    for (const QString& tag : std::as_const(tags)) {
        if (notesWithTag_.contains(tag)) continue;
        notesWithTag_.insert(tag, {});
        const int req = request_;
        vault_->filesWithTag(tag, [this, tag, req](QStringList files) {
            if (req != request_) return;
            notesWithTag_[tag] = QSet<QString>(files.begin(), files.end());
            applyFilter();
        });
    }

    const int show = show_->currentIndex(); // 0 open, 1 done, 2 all
    std::vector<const TaskInfo*> shown;
    for (const TaskInfo& t : tasks_) {
        if ((show == 0 && t.done) || (show == 1 && !t.done)) continue;
        bool ok = true;
        for (const QString& w : std::as_const(words))
            ok = ok && t.text.contains(w, Qt::CaseInsensitive);
        for (const QString& f : std::as_const(folders))
            ok = ok && paths::isInside(t.path, f);
        for (const QString& tag : std::as_const(tags)) {
            const bool inText = t.text.toCaseFolded().contains(u'#' + tag);
            ok = ok && (inText || notesWithTag_.value(tag).contains(t.path));
        }
        if (ok) shown.push_back(&t);
    }
    if (sort_->currentIndex() == 0) {
        std::stable_sort(shown.begin(), shown.end(), [](const TaskInfo* a, const TaskInfo* b) {
            if (a->due.isEmpty() != b->due.isEmpty()) return !a->due.isEmpty(); // dated tasks first
            return a->due < b->due;
        });
    }

    const QString today = QDate::currentDate().toString(Qt::ISODate);
    const Theme theme = Theme::fromPalette(palette());
    list_->setUpdatesEnabled(false);
    filling_ = true;
    justToggled_ = nullptr;
    list_->clear();
    for (const TaskInfo* t : shown) {
        auto* item = new QTreeWidgetItem(list_);
        item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsUserCheckable);
        item->setCheckState(0, t->done ? Qt::Checked : Qt::Unchecked);
        item->setText(0, t->text);
        item->setData(0, kTextRole, t->text);
        item->setData(0, kDoneRole, t->done);
        item->setText(1, t->due);
        item->setText(2, paths::linkName(t->path));
        item->setToolTip(0, t->text);
        item->setToolTip(2, t->path);
        item->setData(0, kPathRole, t->path);
        item->setData(0, kLineRole, t->line);
        if (!t->done && !t->due.isEmpty()) {
            if (t->due < today)
                item->setForeground(1, theme.unresolved);
            else if (t->due == today)
                item->setForeground(1, theme.accent);
        }
        if (t->done) {
            QFont f = item->font(0);
            f.setStrikeOut(true);
            item->setFont(0, f);
            item->setForeground(0, theme.muted);
        }
    }
    filling_ = false;
    list_->setUpdatesEnabled(true);
    status_->setText(
        tr("%1 of %2 tasks · tick to complete · Del deletes").arg(shown.size()).arg(tasks_.size()));
}

} // namespace mn::ui
