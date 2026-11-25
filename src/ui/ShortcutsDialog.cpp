#include "ShortcutsDialog.h"

#include <QAction>
#include <QDialogButtonBox>
#include <QHeaderView>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QTreeWidget>
#include <QVBoxLayout>

using namespace Qt::StringLiterals;

namespace mn::ui {

namespace {

QString clean(QString text)
{
    text.remove(u'&');
    if (text.endsWith(u"…"_s)) text.chop(1);
    return text;
}

void addMenu(QTreeWidgetItem* group, const QMenu* menu)
{
    for (const QAction* a : menu->actions()) {
        if (a->menu() != nullptr) {
            addMenu(group, a->menu());
            continue;
        }
        if (a->isSeparator() || a->shortcut().isEmpty()) continue;
        new QTreeWidgetItem(group, {clean(a->text()), a->shortcut().toString(QKeySequence::NativeText)});
    }
}

} // namespace

ShortcutsDialog::ShortcutsDialog(const QMenuBar* menus, QWidget* parent) : QDialog(parent)
{
    setWindowTitle(tr("Keyboard shortcuts"));
    auto* lay = new QVBoxLayout(this);
    filter_ = new QLineEdit(this);
    filter_->setPlaceholderText(tr("Filter"));
    filter_->setClearButtonEnabled(true);
    lay->addWidget(filter_);
    list_ = new QTreeWidget(this);
    list_->setHeaderLabels({tr("Action"), tr("Keys")});
    list_->setRootIsDecorated(false);
    list_->setSelectionMode(QAbstractItemView::NoSelection);
    list_->setFocusPolicy(Qt::NoFocus);
    list_->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    list_->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    list_->header()->setStretchLastSection(false);
    lay->addWidget(list_, 1);

    auto group = [this](const QString& title) {
        auto* g = new QTreeWidgetItem(list_, {title});
        QFont f = g->font(0);
        f.setBold(true);
        g->setFont(0, f);
        g->setFirstColumnSpanned(true);
        return g;
    };
    if (menus != nullptr) {
        for (const QAction* a : menus->actions()) {
            if (a->menu() == nullptr) continue;
            QTreeWidgetItem* g = group(clean(a->text()));
            addMenu(g, a->menu());
            if (g->childCount() == 0) delete g;
        }
    }
    const std::pair<QString, QList<std::pair<QString, QString>>> fixed[] = {
        {tr("Editor"),
         {{tr("Continue list"), u"Enter"_s},
          {tr("Indent / outdent list item"), u"Tab / Shift+Tab"_s},
          {tr("Follow link"), u"Ctrl+Click"_s},
          {tr("Link autocomplete"), u"[["_s},
          {tr("Tag autocomplete"), u"#"_s},
          {tr("Dismiss link suggestions"), u"Esc"_s}}},
        {tr("Quick switcher"), {{tr("Open"), u"Enter"_s}, {tr("Create note"), u"Shift+Enter"_s}}},
        {tr("Quick capture"),
         {{tr("Save"), u"Enter"_s},
          {tr("New line"), u"Shift+Enter"_s},
          {tr("Draw"), u"Ctrl+D"_s},
          {tr("Cancel"), u"Esc"_s}}},
        {tr("Drawing"),
         {{tr("Pen / highlighter / rectangle"), u"P / H / R"_s},
          {tr("Line width"), u"1 / 2 / 3"_s},
          {tr("Undo / redo"), u"Ctrl+Z / Ctrl+Shift+Z"_s},
          {tr("Done"), u"Ctrl+Enter"_s}}},
    };
    for (const auto& [title, keys] : fixed) {
        QTreeWidgetItem* g = group(title);
        for (const auto& [action, key] : keys)
            new QTreeWidgetItem(g, {action, key});
    }
    list_->expandAll();
    connect(filter_, &QLineEdit::textChanged, this, &ShortcutsDialog::applyFilter);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    lay->addWidget(buttons);
    resize(460, 620);
}

void ShortcutsDialog::applyFilter()
{
    const QString f = filter_->text().trimmed();
    for (int i = 0; i < list_->topLevelItemCount(); ++i) {
        QTreeWidgetItem* g = list_->topLevelItem(i);
        int shown = 0;
        for (int j = 0; j < g->childCount(); ++j) {
            QTreeWidgetItem* it = g->child(j);
            const bool match = f.isEmpty() || it->text(0).contains(f, Qt::CaseInsensitive) ||
                               it->text(1).contains(f, Qt::CaseInsensitive) ||
                               g->text(0).contains(f, Qt::CaseInsensitive);
            it->setHidden(!match);
            shown += match ? 1 : 0;
        }
        g->setHidden(shown == 0);
    }
}

} // namespace mn::ui
