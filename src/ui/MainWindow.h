#pragma once

#include "Appearance.h"
#include "core/capture/Capture.h"
#include "core/index/IndexTypes.h"
#include "core/tasks/TaskEdit.h"
#include "core/vault/TextFile.h"

#include <QMainWindow>
#include <QPointer>

#include <vector>

class QFileSystemWatcher;
class QLabel;
class QSplitter;
class QStackedWidget;
class QTabBar;
class QTextDocument;
class QTabWidget;
class QTimer;
class QTreeView;
class QPushButton;
class QFrame;
class QMimeData;
class QAction;

namespace mn {
class VaultService;
struct RenamePlan;
struct Move;
struct FrontmatterPatch;
} // namespace mn

namespace mn::ai {
class Assistant;
}

namespace mn::ui {

class NoteEditor;
class ReadingView;
class FindBar;
class FileTreeModel;
class QuickSwitcher;
class SearchPanel;
class BacklinksPanel;
class TagPanel;
class TasksPanel;
class AiAskPanel;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

    bool openVault(const QString& root, QString* error = nullptr);
    // Asks the user for a vault folder (first launch).
    void promptForVault() { chooseVault(); }
    // Opens a note; returns false if the current note couldn't be saved first.
    bool openNote(const QString& path, int line = -1, const QString& subpath = {},
                  const QString& findText = {}, bool pushHistory = true);
    // Saves the open note if its bytes differ from the file. Returns false on failure.
    bool saveNow();

    VaultService* vault() const { return vault_; }
    NoteEditor* editor() const { return editor_; }
    ReadingView* readingView() const { return reading_; }
    QuickSwitcher* switcher() const { return switcher_; }
    const QString& currentPath() const { return note_.path; }
    bool isReadOnly() const { return note_.readOnly; }
    bool isDirty() const;
    void setReadingMode(bool on);
    // Applies (previews) a look without saving it.
    void applyLook(const Appearance& a);
    const Appearance& appearance() const { return appearance_; }
    bool readingMode() const;
    // Applies the rename after confirmation (exposed for tests).
    void renamePath(const QString& from, const QString& to, bool confirm = true);

protected:
    void closeEvent(QCloseEvent* e) override;
    bool eventFilter(QObject* o, QEvent* e) override;

private:
    struct OpenNote {
        QString path;
        TextFormat format;
        QByteArray loadedRaw; // bytes at load time (for mixed line endings)
        QByteArray diskRaw;   // bytes we believe are on disk
        FileStamp diskStamp;
        bool readOnly = false;
        bool orphaned = false;
        bool saveErrorShown = false;
        QString banner; // shown above the note (read-only, invalid frontmatter...)
    };
    struct HistoryEntry {
        QString path;
        int position = 0;
    };
    // Where a tab's note was left: restored when the tab is shown again.
    struct ViewState {
        int cursor = 0;
        int scroll = 0;
        bool reading = false;
    };

    void buildUi();
    void buildMenus();
    void onSnapshot();
    void onFilesChangedOnDisk(const QStringList& paths);
    void checkOpenNoteOnDisk();
    void reloadFromDisk(const QByteArray& raw);
    void scheduleAutosave();
    void showBanner(const QString& text, const QString& actionText = {}, std::function<void()> action = {});
    void hideBanner();
    void updateTitle();
    void updateStatus();
    void navigateWithin(int line, const QString& subpath, const QString& findText);
    void followLink(const QString& target, const QString& sourcePath);
    QString createNote(const QString& folder, const QString& baseName, const QByteArray& content = {});
    void newNote();
    // A new note in `folder`: asks for the name and tags first (and the categories,
    // when the vault settings have category properties); they become its frontmatter.
    void newNoteIn(const QString& folder, const QStringList& tags = {});
    void newFolder(const QString& parentFolder);
    void deletePath(const QString& path, bool folder);
    void promptRename(const QString& path, bool folder);
    void runRename(const std::vector<Move>& moves, const std::optional<Move>& folderMove, bool confirm);
    bool confirmPlan(const RenamePlan& plan);
    void insertTemplate();
    QString handleAttachment(const QMimeData* mime);
    void goBack();
    void goForward();
    void toggleReading();
    void showSettings();
    void showAppearance();
    void showUnresolved();
    void showHealth();

public:
    // Quick capture inside the app (the target note may be the open one).
    void quickCapture();
    // Opens `path` once the vault's index has loaded (startup: links are then styled
    // right the first time, with no re-highlight).
    void openWhenReady(const QString& path);
    // Opens today's daily note, creating it (with the daily-note tags) first.
    void openToday();
    // Tabs: the home page first, then one per open note. Opening a note from a panel
    // or the quick switcher gives it a tab (or switches to its tab); following a link
    // or going back/forward stays in the current tab.
    QTabBar* tabBar() const { return tabBar_; }
    QStringList openTabs() const; // note paths, in tab order
    void closeTab(int index);
    // The home page (View › Home); also shown when no note is open.
    void showHome();
    bool homeShown() const;
    // Appends `text` (and `files`) to the capture note: in the editor when that note
    // is open, else on disk. Returns false with a reason on failure.
    bool captureText(const QString& text, const QList<CaptureFile>& files, QString* error);
    // Opens the daily note for `date`, creating it (tagged) first.
    void openDailyNote(QDate date);
    // File › Export: the open note as rendered in the reading view, light theme.
    bool exportNote(const QString& file, bool pdf, QString* error = nullptr);
    // Ctrl+T: the open note's properties as a form; applied to its frontmatter as one
    // undoable edit. With `suggest`, the assistant fills the empty fields first.
    void editProperties(bool suggest = false);
    // Ticks off or deletes a task in its note (from the Tasks view). `confirmDelete`
    // asks first; tests pass false.
    bool editTask(const mn::TaskInfo& task, mn::TaskAction action, bool confirmDelete = true);
    // Shows or hides a side pane (0 = left sidebar, 2 = right panel); also restores
    // a pane that was collapsed by dragging the splitter.
    void setPaneVisible(int index, bool visible);
    bool paneVisible(int index) const;
    void resetLayout();

    // The vault assistant (null when built without MN_AI).
    ai::Assistant* assistant() const { return assistant_; }
    // Adds reviewed frontmatter to the open note, in the editor as one undoable edit
    // (saved like typing). Returns false, with a reason, if it can't.
    bool applyFrontmatter(const FrontmatterPatch& patch, QString* error = nullptr,
                          QStringList* skipped = nullptr);

private:
    void chooseVault();
    void openRecentMenu();
    void closeNote();
    void treeContextMenu(const QPoint& pos);
    void restoreLastNote();

    VaultService* vault_ = nullptr;
    OpenNote note_;
    NoteEditor* editor_ = nullptr;
    ReadingView* reading_ = nullptr;
    FindBar* findBar_ = nullptr;
    QStackedWidget* center_ = nullptr;
    QSplitter* splitter_ = nullptr;
    QTabWidget* leftTabs_ = nullptr;
    class CalendarPanel* calendar_ = nullptr;
    class HomePage* home_ = nullptr;
    QTabBar* tabBar_ = nullptr;
    bool syncingTabs_ = false;      // the tab bar is being updated to match the open note
    bool openInCurrentTab_ = false; // the next openNote replaces the current tab's note
    QHash<QString, ViewState> viewStates_;
    // Notes open in tabs keep their documents (and undo history); the ones not shown
    // are parked here with their state. Parked notes have been saved.
    QHash<QString, QTextDocument*> docs_;
    QHash<QString, OpenNote> parked_;
    QMetaObject::Connection docConn_;
    void parkCurrent();
    void pruneDocuments();
    bool saveParked(QStringList* failed = nullptr);
    int tabIndexFor(const QString& path) const;
    void syncTabFor(const QString& path);
    void onTabChanged(int index);
    void rememberView();
    void restoreView(const QString& path);
    void saveTabs();
    bool restoreTabs();
    QString startupNote_;
    QTreeView* tree_ = nullptr;
    FileTreeModel* treeModel_ = nullptr;
    SearchPanel* search_ = nullptr;
    TagPanel* tags_ = nullptr;
    TasksPanel* tasks_ = nullptr;
    BacklinksPanel* backlinks_ = nullptr;
    QuickSwitcher* switcher_ = nullptr;
    QFrame* banner_ = nullptr;
    QLabel* bannerText_ = nullptr;
    QPushButton* bannerButton_ = nullptr;
    std::function<void()> bannerAction_;
    QLabel* statusPath_ = nullptr;
    QLabel* statusInfo_ = nullptr;
    // Assistant (MN_AI builds only).
    void buildAssistant();
    void suggestFrontmatter();
    void showAiSettings();
    ai::Assistant* assistant_ = nullptr;
    AiAskPanel* ask_ = nullptr;
    QLabel* aiStatus_ = nullptr;
    QTimer* autosave_ = nullptr;
    QTimer* passageTimer_ = nullptr; // related passages, once the cursor rests in a paragraph
    QFileSystemWatcher* noteWatcher_ = nullptr;
    std::vector<HistoryEntry> history_;
    int historyIndex_ = -1;
    bool loading_ = false;
    bool restoredLastNote_ = false;
    int lastFileCount_ = -1;
    QAction* leftPaneAction_ = nullptr;
    QAction* rightPaneAction_ = nullptr;
    int paneWidth_[3] = {260, 0, 280};
    void syncPaneActions();
    Appearance appearance_;
    bool lookApplied_ = false;
};

} // namespace mn::ui
