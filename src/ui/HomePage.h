#pragma once

#include "core/index/IndexTypes.h"

#include <QHash>
#include <QPushButton>
#include <QWidget>

#include <functional>
#include <vector>

class QGridLayout;
class QLabel;
class QLineEdit;
class QScrollArea;
class QTimer;

namespace mn {
class VaultService;
struct VaultSettings;
} // namespace mn

namespace mn::ui {

// An active note on the home page: name, status, summary, and across the note and
// the notes linked with it, how many there are, open tasks and the last change.
class NoteTile : public QPushButton {
    Q_OBJECT
public:
    NoteTile(const NoteCard& card, int width, QWidget* parent = nullptr);
    const QString& path() const { return path_; }

private:
    QString path_;
};

// The home page (View › Home, and whenever no note is open). Its sections, each
// turned on in Vault settings › Home:
//   today       open or start today's note; a capture box
//   active      notes with a status that isn't finished; a hub note counts the notes
//               it links to and that link to it, so a multi-note project is one tile
//   tasks       open tasks with a due date, overdue first
//   recent      recently edited notes
//   categories  each category tag with its note count and main topics
//   attention   notes without their categories or summary (journal notes excluded)
//   onthisday   journal notes from this date in earlier months and years
class HomePage : public QWidget {
    Q_OBJECT
public:
    explicit HomePage(VaultService* vault, QWidget* parent = nullptr);
    void refresh();
    const HomeData& data() const { return data_; }
    QWidget* section(const QString& id) const { return sections_.value(id); }
    QLineEdit* captureBox() const { return capture_; }
    QList<NoteTile*> tiles() const;
    // Follows a link in a section's list, as a click would.
    void activate(const QString& href);

    // Appends text to the capture note; returns false with a reason on failure.
    using CaptureHandler = std::function<bool(const QString& text, QString* error)>;
    void setCaptureHandler(CaptureHandler h) { captureHandler_ = std::move(h); }

    static HomeRequest requestFor(const VaultSettings& s, const QDate& today);
    // "edited today", "edited 3 days ago", "edited 12 Nov 2026"
    static QString editedText(const QDateTime& modified, const QDateTime& now);

Q_SIGNALS:
    void openRequested(const QString& path, int line);
    void tagRequested(const QString& tag);
    void todayRequested();
    void inboxRequested();
    void newNoteRequested();
    void refreshed();

protected:
    void showEvent(QShowEvent* e) override;
    void resizeEvent(QResizeEvent* e) override;

private:
    QWidget* addSection(const QString& id, const QString& title, QWidget* content);
    QLabel* listLabel();
    QString openLink(const QString& path, int line, const QString& text);
    void fill();
    void layoutTiles();
    void submitCapture();

    VaultService* vault_;
    QScrollArea* scroll_;
    QWidget* page_;
    int contentWidth() const;
    // Columns that fit, and the width each tile gets.
    std::pair<int, int> grid() const;
    QHash<QString, QWidget*> sections_;
    QPushButton* todayButton_;
    QLineEdit* capture_;
    QLabel* captureStatus_;
    QWidget* tileGrid_;
    QGridLayout* tileLayout_;
    QLabel* activeEmpty_;
    QLabel* tasks_;
    QLabel* recent_;
    QWidget* categoryGrid_;
    QGridLayout* categoryLayout_;
    QLabel* categoriesEmpty_;
    QLabel* attention_;
    QLabel* onThisDay_;
    QTimer* refreshTimer_;
    HomeData data_;
    std::vector<std::pair<QString, int>> links_; // "open:N" targets
    CaptureHandler captureHandler_;
    int columns_ = 0; // columns * 10000 + tile width, of the tiles laid out
    int request_ = 0;
};

} // namespace mn::ui
