#pragma once

#include <QCalendarWidget>
#include <QDate>
#include <QHash>
#include <QSet>
#include <QStringList>
#include <QWidget>

class QLabel;
class QListWidget;
class QPushButton;
class QTimer;

namespace mn {
class VaultService;
}

namespace mn::ui {

// A month calendar with a dot on each day that has a journal note.
class DotCalendar : public QCalendarWidget {
    Q_OBJECT
public:
    using QCalendarWidget::QCalendarWidget;
    void setDates(QSet<QDate> dates);
    const QSet<QDate>& dates() const { return dates_; }

protected:
    void paintCell(QPainter* painter, const QRect& rect, QDate date) const override;

private:
    QSet<QDate> dates_;
};

// Left-hand Calendar tab. Journal notes (the daily-note tag, "journal" by default)
// are placed on their date: the date in the file name, else their created or date
// property. Clicking a day with one note opens it; a day's notes are listed below,
// and a day without notes offers a new daily note (double-click creates it).
class CalendarPanel : public QWidget {
    Q_OBJECT
public:
    explicit CalendarPanel(VaultService* vault, QWidget* parent = nullptr);
    void refresh();
    DotCalendar* calendar() const { return calendar_; }
    QListWidget* notes() const { return notes_; }
    QPushButton* createButton() const { return create_; }
    QStringList notesOn(QDate d) const { return byDate_.value(d); }

Q_SIGNALS:
    void openRequested(const QString& path);
    void createRequested(QDate date);
    void refreshed();

protected:
    void showEvent(QShowEvent* e) override;

private:
    void showDay(QDate d);

    VaultService* vault_;
    DotCalendar* calendar_;
    QLabel* day_;
    QListWidget* notes_;
    QPushButton* create_;
    QTimer* refreshTimer_;
    QHash<QDate, QStringList> byDate_;
    int request_ = 0;
};

} // namespace mn::ui
