#include "HomePage.h"

#include "core/capture/Capture.h"
#include "core/index/VaultService.h"
#include "core/settings/VaultSettings.h"
#include "core/vault/Paths.h"

#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QRegularExpression>
#include <QScrollArea>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace mn::ui {

namespace {

constexpr int kMinTileWidth = 220;
constexpr int kMaxTileWidth = 320;
constexpr int kTileHeight = 132;
constexpr int kSpacing = 12;

QLabel* plainLabel(const QString& text, QWidget* parent)
{
    auto* l = new QLabel(text, parent);
    l->setAttribute(Qt::WA_TransparentForMouseEvents);
    l->setTextFormat(Qt::PlainText);
    return l;
}

QString ago(const QDate& d, const QDate& today)
{
    const int years = today.year() - d.year() - (today.month() < d.month() ? 1 : 0);
    if (d.month() == today.month() && years >= 1)
        return years == 1 ? HomePage::tr("1 year ago") : HomePage::tr("%1 years ago").arg(years);
    const int months = (today.year() - d.year()) * 12 + today.month() - d.month();
    return months == 1 ? HomePage::tr("1 month ago") : HomePage::tr("%1 months ago").arg(months);
}

} // namespace

// --- NoteTile ----------------------------------------------------------------------

NoteTile::NoteTile(const NoteCard& card, int width, QWidget* parent) : QPushButton(parent), path_(card.path)
{
    setCursor(Qt::PointingHandCursor);
    setToolTip(card.path);
    setFixedSize(width, kTileHeight);
    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(12, 10, 12, 10);
    lay->setSpacing(4);
    auto* top = new QHBoxLayout;
    auto* title = plainLabel(card.title, this);
    QFont tf = title->font();
    tf.setBold(true);
    tf.setPointSizeF(tf.pointSizeF() * 1.1);
    title->setFont(tf);
    title->setText(title->fontMetrics().elidedText(card.title, Qt::ElideRight, width - 100));
    top->addWidget(title, 1);
    auto* status = plainLabel(card.status, this);
    QFont sf = status->font();
    sf.setPointSizeF(sf.pointSizeF() * 0.85);
    status->setFont(sf);
    status->setStyleSheet(u"color: palette(highlighted-text); background: palette(highlight);"
                          " border-radius: 6px; padding: 1px 6px;"_s);
    top->addWidget(status);
    lay->addLayout(top);
    auto* summary = plainLabel(card.summary.isEmpty() ? tr("No summary") : card.summary, this);
    summary->setWordWrap(true);
    summary->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    summary->setMaximumHeight(summary->fontMetrics().lineSpacing() * 3);
    summary->setEnabled(!card.summary.isEmpty());
    lay->addWidget(summary, 1);
    QStringList footer;
    if (card.linkedNotes > 0)
        footer.append(card.linkedNotes == 1 ? tr("1 linked note")
                                            : tr("%1 linked notes").arg(card.linkedNotes));
    if (card.openTasks > 0)
        footer.append(card.openTasks == 1 ? tr("1 open task") : tr("%1 open tasks").arg(card.openTasks));
    footer.append(HomePage::editedText(card.modified, QDateTime::currentDateTime()));
    auto* foot = plainLabel(QString(), this);
    foot->setText(foot->fontMetrics().elidedText(footer.join(u"  ·  "_s), Qt::ElideRight, width - 24));
    foot->setEnabled(false);
    lay->addWidget(foot);
}

// --- HomePage ----------------------------------------------------------------------

HomePage::HomePage(VaultService* vault, QWidget* parent) : QWidget(parent), vault_(vault)
{
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    scroll_ = new QScrollArea(this);
    scroll_->setWidgetResizable(true);
    scroll_->setFrameShape(QFrame::NoFrame);
    scroll_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    outer->addWidget(scroll_);
    page_ = new QWidget(scroll_);
    scroll_->setWidget(page_);
    auto* lay = new QVBoxLayout(page_);
    lay->setContentsMargins(28, 22, 28, 22);
    lay->setSpacing(18);

    auto* header = new QHBoxLayout;
    auto* title = new QLabel(tr("Home"), page_);
    QFont hf = title->font();
    hf.setPointSizeF(hf.pointSizeF() * 1.6);
    hf.setBold(true);
    title->setFont(hf);
    auto* date = new QLabel(QLocale().toString(QDate::currentDate(), u"dddd d MMMM"_s), page_);
    date->setEnabled(false);
    auto* newNote = new QPushButton(tr("New note"), page_);
    connect(newNote, &QPushButton::clicked, this, &HomePage::newNoteRequested);
    header->addWidget(title);
    header->addSpacing(10);
    header->addWidget(date, 0, Qt::AlignBottom);
    header->addStretch(1);
    header->addWidget(newNote);
    lay->addLayout(header);

    // Today
    auto* today = new QWidget(page_);
    auto* tl = new QVBoxLayout(today);
    tl->setContentsMargins(0, 0, 0, 0);
    auto* row = new QHBoxLayout;
    todayButton_ = new QPushButton(today);
    connect(todayButton_, &QPushButton::clicked, this, &HomePage::todayRequested);
    capture_ = new QLineEdit(today);
    capture_->setClearButtonEnabled(true);
    connect(capture_, &QLineEdit::returnPressed, this, &HomePage::submitCapture);
    auto* inbox = new QPushButton(tr("Open inbox"), today);
    connect(inbox, &QPushButton::clicked, this, &HomePage::inboxRequested);
    row->addWidget(todayButton_);
    row->addWidget(capture_, 1);
    row->addWidget(inbox);
    tl->addLayout(row);
    captureStatus_ = new QLabel(today);
    captureStatus_->setEnabled(false);
    captureStatus_->hide();
    tl->addWidget(captureStatus_);
    lay->addWidget(addSection(u"today"_s, tr("Today"), today));

    // Active
    auto* active = new QWidget(page_);
    auto* al = new QVBoxLayout(active);
    al->setContentsMargins(0, 0, 0, 0);
    activeEmpty_ = new QLabel(tr("No active notes. Set a note's status with Ctrl+T."), active);
    activeEmpty_->setWordWrap(true);
    activeEmpty_->setEnabled(false);
    al->addWidget(activeEmpty_);
    tileGrid_ = new QWidget(active);
    tileLayout_ = new QGridLayout(tileGrid_);
    tileLayout_->setContentsMargins(0, 0, 0, 0);
    tileLayout_->setSpacing(kSpacing);
    tileLayout_->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    al->addWidget(tileGrid_);
    lay->addWidget(addSection(u"active"_s, tr("Active"), active));

    tasks_ = listLabel();
    lay->addWidget(addSection(u"tasks"_s, tr("Tasks due"), tasks_));
    recent_ = listLabel();
    lay->addWidget(addSection(u"recent"_s, tr("Recently edited"), recent_));

    auto* cats = new QWidget(page_);
    auto* cl = new QVBoxLayout(cats);
    cl->setContentsMargins(0, 0, 0, 0);
    categoriesEmpty_ = new QLabel(tr("No categories. Set category tags in Vault settings."), cats);
    categoriesEmpty_->setEnabled(false);
    cl->addWidget(categoriesEmpty_);
    categoryGrid_ = new QWidget(cats);
    categoryLayout_ = new QGridLayout(categoryGrid_);
    categoryLayout_->setContentsMargins(0, 0, 0, 0);
    categoryLayout_->setSpacing(kSpacing);
    categoryLayout_->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    cl->addWidget(categoryGrid_);
    lay->addWidget(addSection(u"categories"_s, tr("Categories"), cats));

    attention_ = listLabel();
    lay->addWidget(addSection(u"attention"_s, tr("Missing properties"), attention_));
    onThisDay_ = listLabel();
    lay->addWidget(addSection(u"onthisday"_s, tr("On this day"), onThisDay_));
    lay->addStretch(1);

    refreshTimer_ = new QTimer(this);
    refreshTimer_->setSingleShot(true);
    refreshTimer_->setInterval(400);
    connect(refreshTimer_, &QTimer::timeout, this, &HomePage::refresh);
    connect(vault_, &VaultService::snapshotChanged, this, [this] {
        if (isVisible()) refreshTimer_->start();
    });
}

int HomePage::contentWidth() const
{
    return scroll_->viewport()->width() - 56; // minus the page margins
}

std::pair<int, int> HomePage::grid() const
{
    const int w = std::max(kMinTileWidth, contentWidth());
    const int columns = std::max(1, (w + kSpacing) / (kMinTileWidth + kSpacing));
    const int width = std::min(kMaxTileWidth, (w - kSpacing * (columns - 1)) / columns);
    return {columns, width};
}

QWidget* HomePage::addSection(const QString& id, const QString& title, QWidget* content)
{
    auto* box = new QWidget(page_);
    auto* l = new QVBoxLayout(box);
    l->setContentsMargins(0, 0, 0, 0);
    l->setSpacing(6);
    auto* heading = new QLabel(title, box);
    QFont f = heading->font();
    f.setBold(true);
    f.setPointSizeF(f.pointSizeF() * 1.15);
    heading->setFont(f);
    l->addWidget(heading);
    content->setParent(box);
    l->addWidget(content);
    sections_.insert(id, box);
    return box;
}

QLabel* HomePage::listLabel()
{
    auto* l = new QLabel(page_);
    l->setTextFormat(Qt::RichText);
    l->setWordWrap(true);
    l->setTextInteractionFlags(Qt::LinksAccessibleByMouse | Qt::LinksAccessibleByKeyboard);
    l->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    connect(l, &QLabel::linkActivated, this, &HomePage::activate);
    return l;
}

QString HomePage::openLink(const QString& path, int line, const QString& text)
{
    links_.emplace_back(path, line);
    return u"<a href=\"open:%1\">%2</a>"_s.arg(links_.size() - 1).arg(text.toHtmlEscaped());
}

void HomePage::activate(const QString& href)
{
    if (href.startsWith(u"open:"_s)) {
        const auto i = static_cast<std::size_t>(href.mid(5).toInt());
        if (i < links_.size()) Q_EMIT openRequested(links_[i].first, links_[i].second);
    } else if (href.startsWith(u"tag:"_s)) {
        Q_EMIT tagRequested(QUrl::fromPercentEncoding(href.mid(4).toUtf8()));
    }
}

HomeRequest HomePage::requestFor(const VaultSettings& s, const QDate& today)
{
    HomeRequest r;
    r.today = today;
    r.finishedStatuses = {u"done"_s,      u"archived"_s,  u"finished"_s, u"complete"_s,
                          u"completed"_s, u"cancelled"_s, u"canceled"_s, u"dropped"_s};
    r.categories = s.categoryChoices();
    r.requiredKeys = s.categoryProperties();
    for (const PropertySpec& p : s.formProperties()) {
        if (p.key == u"summary") r.requiredKeys.append(p.key);
    }
    r.journalTag = s.journalTag();
    r.dailyFormat = s.dailyNoteFormat;
    r.templatesFolder = s.templatesFolder;
    return r;
}

QString HomePage::editedText(const QDateTime& modified, const QDateTime& now)
{
    if (!modified.isValid()) return {};
    const qint64 days = modified.date().daysTo(now.date());
    if (days <= 0) return tr("edited today");
    if (days == 1) return tr("edited yesterday");
    if (days < 7) return tr("edited %1 days ago").arg(days);
    return tr("edited %1").arg(QLocale().toString(modified.date(), u"d MMM yyyy"_s));
}

void HomePage::showEvent(QShowEvent* e)
{
    QWidget::showEvent(e);
    refresh();
}

void HomePage::refresh()
{
    if (!vault_->isOpen()) return;
    const VaultSettings& st = vault_->settings();
    for (auto it = sections_.constBegin(); it != sections_.constEnd(); ++it)
        it.value()->setVisible(st.homeSections.contains(it.key()));
    // Today's note and the capture box need no index.
    const QString daily = dailyNotePath(st, QDateTime::currentDateTime());
    todayButton_->setText(tr("Today's note"));
    capture_->setPlaceholderText(
        tr("Capture to %1").arg(paths::linkName(captureTargetPath(st, QDateTime::currentDateTime()))));
    const int req = ++request_;
    vault_->home(requestFor(st, QDate::currentDate()), [this, req](HomeData d) {
        if (req != request_) return;
        data_ = std::move(d);
        fill();
        Q_EMIT refreshed();
    });
}

void HomePage::fill()
{
    const VaultSettings& st = vault_->settings();
    const QDate today = QDate::currentDate();
    links_.clear();

    activeEmpty_->setVisible(data_.active.empty());
    columns_ = 0;
    layoutTiles();

    QStringList rows;
    for (const TaskInfo& t : data_.due) {
        const QDate due = QDate::fromString(t.due, Qt::ISODate);
        QString when = due.isValid() ? QLocale().toString(due, u"ddd d MMM"_s) : t.due;
        if (due.isValid() && due < today)
            when =
                u"<span style=\"color:#c0392b\">%1</span>"_s.arg(tr("overdue, %1").arg(when.toHtmlEscaped()));
        else if (due == today)
            when = u"<b>%1</b>"_s.arg(tr("today"));
        else
            when = when.toHtmlEscaped();
        // The due date is shown already: not again in the task's text.
        static const QRegularExpression dueMark(
            uR"(\s*(📅\s*\d{4}-\d{2}-\d{2}|due:\s*\d{4}-\d{2}-\d{2}|\[due::[^\]]*\]))"_s);
        const QString text = QString(t.text).remove(dueMark).trimmed();
        rows.append(when + u" · "_s + openLink(t.path, t.line, text) + u" <span style=\"color:gray\">"_s +
                    paths::linkName(t.path).toHtmlEscaped() + u"</span>"_s);
    }
    tasks_->setText(rows.isEmpty() ? tr("No tasks due.") : rows.join(u"<br>"_s));
    tasks_->setEnabled(!rows.isEmpty());

    rows.clear();
    for (const NoteCard& c : data_.recent)
        rows.append(openLink(c.path, -1, c.title) + u" <span style=\"color:gray\">"_s +
                    editedText(c.modified, QDateTime::currentDateTime()).toHtmlEscaped() + u"</span>"_s);
    recent_->setText(rows.join(u"<br>"_s));

    // Categories, as small panels in a grid.
    while (QLayoutItem* item = categoryLayout_->takeAt(0)) {
        delete item->widget();
        delete item;
    }
    categoriesEmpty_->setVisible(data_.categories.empty());
    const auto [catColumns, catWidth] = grid();
    int i = 0;
    for (const CategoryOverview& c : data_.categories) {
        auto* frame = new QFrame(categoryGrid_);
        frame->setFrameShape(QFrame::StyledPanel);
        frame->setFixedWidth(catWidth);
        auto* fl = new QVBoxLayout(frame);
        fl->setContentsMargins(10, 8, 10, 8);
        auto* label = new QLabel(frame);
        label->setTextFormat(Qt::RichText);
        label->setWordWrap(true);
        connect(label, &QLabel::linkActivated, this, &HomePage::activate);
        QStringList topics;
        for (const QString& t : c.topTags)
            topics.append(u"<a href=\"tag:%1\">%2</a>"_s.arg(QString::fromUtf8(QUrl::toPercentEncoding(t)),
                                                             t.toHtmlEscaped()));
        label->setText(u"<b><a href=\"tag:%1\">#%2</a></b> <span style=\"color:gray\">%3</span><br>%4"_s.arg(
            QString::fromUtf8(QUrl::toPercentEncoding(c.tag)), c.tag.toHtmlEscaped(),
            c.notes == 1 ? tr("1 note") : tr("%1 notes").arg(c.notes), topics.join(u" · "_s)));
        fl->addWidget(label);
        categoryLayout_->addWidget(frame, i / catColumns, i % catColumns);
        ++i;
    }

    rows.clear();
    for (const AttentionNote& a : data_.attention)
        rows.append(openLink(a.path, -1, paths::linkName(a.path)) + u" <span style=\"color:gray\">"_s +
                    tr("missing %1").arg(a.missing.join(u", "_s)).toHtmlEscaped() + u"</span>"_s);
    if (data_.attentionTotal > static_cast<int>(data_.attention.size()))
        rows.append(tr("%1 more").arg(data_.attentionTotal - static_cast<int>(data_.attention.size())));
    attention_->setText(rows.join(u"<br>"_s));
    sections_.value(u"attention"_s)->setVisible(st.homeSections.contains(u"attention") && !rows.isEmpty());

    rows.clear();
    for (const DatedNote& d : data_.onThisDay)
        rows.append(ago(d.date, today).toHtmlEscaped() + u" · "_s +
                    openLink(d.path, -1, paths::linkName(d.path)));
    onThisDay_->setText(rows.join(u"<br>"_s));
    sections_.value(u"onthisday"_s)->setVisible(st.homeSections.contains(u"onthisday") && !rows.isEmpty());
}

QList<NoteTile*> HomePage::tiles() const
{
    return tileGrid_->findChildren<NoteTile*>(Qt::FindDirectChildrenOnly);
}

void HomePage::resizeEvent(QResizeEvent* e)
{
    QWidget::resizeEvent(e);
    layoutTiles();
}

void HomePage::layoutTiles()
{
    const auto [columns, width] = grid();
    if (columns * 10000 + width == columns_ &&
        static_cast<std::size_t>(tiles().size()) == data_.active.size())
        return;
    columns_ = columns * 10000 + width;
    for (NoteTile* t : tiles())
        delete t;
    int i = 0;
    for (const NoteCard& c : data_.active) {
        auto* tile = new NoteTile(c, width, tileGrid_);
        connect(tile, &QPushButton::clicked, this, [this, p = c.path] { Q_EMIT openRequested(p, -1); });
        tileLayout_->addWidget(tile, i / columns, i % columns);
        ++i;
    }
}

void HomePage::submitCapture()
{
    const QString text = capture_->text().trimmed();
    if (text.isEmpty() || !captureHandler_) return;
    QString error;
    captureStatus_->show();
    if (captureHandler_(text, &error)) {
        capture_->clear();
        captureStatus_->setText(tr("Captured."));
    } else {
        captureStatus_->setText(error); // the text stays in the box
    }
}

} // namespace mn::ui
