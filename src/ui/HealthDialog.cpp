#include "HealthDialog.h"

#include "core/index/VaultService.h"
#include "core/vault/Paths.h"

#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QTreeWidget>
#include <QUrl>
#include <QVBoxLayout>

using namespace Qt::StringLiterals;

namespace mn::ui {

namespace {
constexpr int kPathRole = Qt::UserRole + 1;
constexpr int kLineRole = Qt::UserRole + 2;
constexpr int kKindRole = Qt::UserRole + 3; // 0 note, 1 attachment, 2 tag
} // namespace

HealthDialog::HealthDialog(VaultService* vault, QWidget* parent) : QDialog(parent), vault_(vault)
{
    setWindowTitle(tr("Vault health"));
    auto* lay = new QVBoxLayout(this);
    status_ = new QLabel(this);
    tree_ = new QTreeWidget(this);
    tree_->setHeaderLabels({tr("Item"), tr("Details")});
    tree_->header()->setSectionResizeMode(0, QHeaderView::Interactive);
    tree_->setColumnWidth(0, 360);
    tree_->setUniformRowHeights(true);
    lay->addWidget(status_);
    lay->addWidget(tree_, 1);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    refresh_ = buttons->addButton(tr("Refresh"), QDialogButtonBox::ActionRole);
    lay->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::close);
    connect(refresh_, &QPushButton::clicked, this, &HealthDialog::run);
    connect(tree_, &QTreeWidget::itemActivated, this, [this](QTreeWidgetItem* it) {
        const QString path = it->data(0, kPathRole).toString();
        if (path.isEmpty()) return;
        switch (it->data(0, kKindRole).toInt()) {
        case 1:
            QDesktopServices::openUrl(QUrl::fromLocalFile(vault_->absolutePath(paths::folderOf(path))));
            break;
        case 2:
            Q_EMIT tagRequested(path);
            break;
        default:
            Q_EMIT openRequested(path, it->data(0, kLineRole).toInt(), QString());
            break;
        }
    });
    resize(820, 560);
}

void HealthDialog::run()
{
    tree_->clear();
    status_->setText(tr("Checking the vault…"));
    refresh_->setEnabled(false);
    const int req = ++request_;
    vault_->healthReport([this, req](HealthReport r) {
        if (req != request_) return;
        display(r);
    });
}

int HealthDialog::sectionCount(int section) const
{
    const QTreeWidgetItem* top = tree_->topLevelItem(section);
    return top ? top->childCount() : -1;
}

void HealthDialog::display(const HealthReport& r)
{
    refresh_->setEnabled(true);
    tree_->clear();
    auto section = [&](const QString& title, const QString& help) {
        auto* top = new QTreeWidgetItem(tree_, {title, help});
        QFont f = top->font(0);
        f.setBold(true);
        top->setFont(0, f);
        return top;
    };
    auto item = [](QTreeWidgetItem* parent, const QString& text, const QString& detail, const QString& path,
                   int line, int kind) {
        auto* it = new QTreeWidgetItem(parent, {text, detail});
        it->setData(0, kPathRole, path);
        it->setData(0, kLineRole, line);
        it->setData(0, kKindRole, kind);
        it->setToolTip(0, path);
        return it;
    };
    auto finish = [](QTreeWidgetItem* top, int n) {
        top->setText(0, top->text(0) + u" ("_s + QString::number(n) + u')');
        top->setExpanded(n > 0 && n <= 50);
    };

    QTreeWidgetItem* broken = section(tr("Broken links"), tr("link targets that don't exist"));
    for (const UnresolvedLink& l : r.brokenLinks)
        item(broken, l.linkpath, tr("in %1, line %2").arg(l.sourcePath).arg(l.line + 1), l.sourcePath, l.line,
             0);
    finish(broken, static_cast<int>(r.brokenLinks.size()));

    QTreeWidgetItem* orphans = section(tr("Orphan notes"), tr("not linked from any note"));
    for (const QString& p : r.orphanNotes)
        item(orphans, paths::linkName(p), paths::folderOf(p), p, -1, 0);
    finish(orphans, static_cast<int>(r.orphanNotes.size()));

    QTreeWidgetItem* empty = section(tr("Empty notes"), tr("only frontmatter or whitespace"));
    for (const QString& p : r.emptyNotes)
        item(empty, paths::linkName(p), paths::folderOf(p), p, -1, 0);
    finish(empty, static_cast<int>(r.emptyNotes.size()));

    QTreeWidgetItem* unused = section(tr("Unused attachments"), tr("not linked or embedded anywhere"));
    for (const QString& p : r.unusedAttachments)
        item(unused, paths::fileName(p), paths::folderOf(p), p, -1, 1);
    finish(unused, static_cast<int>(r.unusedAttachments.size()));

    QTreeWidgetItem* tags =
        section(tr("Tags that look alike"), tr("differ only in case, separators or plural form"));
    for (const TagGroup& g : r.similarTags) {
        QStringList parts;
        for (qsizetype i = 0; i < g.tags.size(); ++i)
            parts.append(u'#' + g.tags[i] + u" ("_s + QString::number(g.notes.value(i)) + u')');
        auto* top = item(tags, parts.join(u"  ·  "_s), QString(), g.tags.first(), -1, 2);
        for (const QString& t : g.tags)
            item(top, u'#' + t, tr("show notes"), t, -1, 2);
    }
    finish(tags, static_cast<int>(r.similarTags.size()));

    QTreeWidgetItem* dups = section(tr("Near-duplicate notes"), tr("mostly identical text"));
    for (const DuplicateNotes& d : r.duplicateNotes) {
        auto* top = item(dups, paths::linkName(d.a) + u"  ↔  "_s + paths::linkName(d.b),
                         tr("%1% shared text").arg(qRound(d.similarity * 100)), d.a, -1, 0);
        item(top, paths::linkName(d.a), d.a, d.a, -1, 0);
        item(top, paths::linkName(d.b), d.b, d.b, -1, 0);
    }
    finish(dups, static_cast<int>(r.duplicateNotes.size()));

    if (r.minCategories > 0) {
        QTreeWidgetItem* cats = section(tr("Notes with too few categories"),
                                        tr("fewer than %n category tag(s)", nullptr, r.minCategories));
        for (const CategoryProblem& c : r.fewCategories) {
            const QString has =
                c.categories.isEmpty() ? tr("no categories") : u"#"_s + c.categories.join(u"  #"_s);
            item(cats, paths::linkName(c.path), has, c.path, -1, 0);
        }
        finish(cats, static_cast<int>(r.fewCategories.size()));
    }

    status_->setText(tr("Checked in %1 ms.").arg(r.elapsedMs));
}

} // namespace mn::ui
