#include "CalendarPanel.h"

#include "core/index/VaultService.h"
#include "core/vault/Paths.h"

#include <QLabel>
#include <QListWidget>
#include <QLocale>
#include <QPainter>
#include <QPushButton>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

using namespace Qt::StringLiterals;

namespace mn::ui {

void DotCalendar::setDates(QSet<QDate> dates)
{
    dates_ = std::move(dates);
    updateCells();
}

void DotCalendar::paintCell(QPainter* painter, const QRect& rect, QDate date) const
{
    QCalendarWidget::paintCell(painter, rect, date);
    if (!dates_.contains(date)) return;
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);
    painter->setPen(Qt::NoPen);
    painter->setBrush(
        palette().color(date == selectedDate() ? QPalette::HighlightedText : QPalette::Highlight));
    // Under the day number, clear of it.
    const qreal r = std::clamp<qreal>(rect.height() / 16.0, 1.5, 3.0);
    const qreal y =
        std::min<qreal>(rect.bottom() - r - 1, rect.center().y() + painter->fontMetrics().height() / 2.0 + r);
    painter->drawEllipse(QPointF(rect.center().x() + 0.5, y), r, r);
    painter->restore();
}

CalendarPanel::CalendarPanel(VaultService* vault, QWidget* parent) : QWidget(parent), vault_(vault)
{
    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(4, 4, 4, 4);
    calendar_ = new DotCalendar(this);
    calendar_->setVerticalHeaderFormat(QCalendarWidget::NoVerticalHeader);
    calendar_->setGridVisible(false);
    calendar_->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    calendar_->setFirstDayOfWeek(QLocale().firstDayOfWeek());
    lay->addWidget(calendar_);

    auto* row = new QHBoxLayout;
    day_ = new QLabel(this);
    QFont f = day_->font();
    f.setBold(true);
    day_->setFont(f);
    auto* today = new QToolButton(this);
    today->setText(tr("Today"));
    connect(today, &QToolButton::clicked, this, [this] {
        calendar_->setSelectedDate(QDate::currentDate());
        showDay(QDate::currentDate());
    });
    row->addWidget(day_, 1);
    row->addWidget(today);
    lay->addLayout(row);

    notes_ = new QListWidget(this);
    lay->addWidget(notes_, 1);
    connect(notes_, &QListWidget::itemActivated, this,
            [this](QListWidgetItem* it) { Q_EMIT openRequested(it->data(Qt::UserRole).toString()); });
    connect(notes_, &QListWidget::itemClicked, this,
            [this](QListWidgetItem* it) { Q_EMIT openRequested(it->data(Qt::UserRole).toString()); });
    create_ = new QPushButton(this);
    lay->addWidget(create_);
    lay->addStretch(0);
    connect(create_, &QPushButton::clicked, this,
            [this] { Q_EMIT createRequested(calendar_->selectedDate()); });

    connect(calendar_, &QCalendarWidget::clicked, this, [this](QDate d) {
        showDay(d);
        const QStringList notes = byDate_.value(d);
        if (notes.size() == 1) Q_EMIT openRequested(notes.front());
    });
    connect(calendar_, &QCalendarWidget::activated, this, [this](QDate d) {
        if (byDate_.value(d).isEmpty()) Q_EMIT createRequested(d);
    });
    connect(calendar_, &QCalendarWidget::selectionChanged, this,
            [this] { showDay(calendar_->selectedDate()); });

    refreshTimer_ = new QTimer(this);
    refreshTimer_->setSingleShot(true);
    refreshTimer_->setInterval(300);
    connect(refreshTimer_, &QTimer::timeout, this, &CalendarPanel::refresh);
    connect(vault_, &VaultService::snapshotChanged, this, [this] {
        if (isVisible()) refreshTimer_->start();
    });
    showDay(calendar_->selectedDate());
}

void CalendarPanel::showEvent(QShowEvent* e)
{
    QWidget::showEvent(e);
    refresh();
}

void CalendarPanel::refresh()
{
    if (!vault_->isOpen()) return;
    const int req = ++request_;
    vault_->datedNotes(vault_->settings().journalTag(), [this, req](std::vector<DatedNote> notes) {
        if (req != request_) return;
        byDate_.clear();
        QSet<QDate> dates;
        for (const DatedNote& n : notes) {
            byDate_[n.date].append(n.path);
            dates.insert(n.date);
        }
        calendar_->setDates(std::move(dates));
        showDay(calendar_->selectedDate());
        Q_EMIT refreshed();
    });
}

void CalendarPanel::showDay(QDate d)
{
    day_->setText(QLocale().toString(d, u"dddd d MMMM"_s));
    notes_->clear();
    for (const QString& p : byDate_.value(d)) {
        auto* it = new QListWidgetItem(paths::linkName(p), notes_);
        it->setData(Qt::UserRole, p);
        it->setToolTip(p);
    }
    notes_->setVisible(notes_->count() > 0);
    create_->setVisible(notes_->count() == 0);
    create_->setText(tr("New note for %1").arg(QLocale().toString(d, u"d MMM"_s)));
}

} // namespace mn::ui
