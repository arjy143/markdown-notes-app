#include "MainWindow.h"

#include "AppearanceDialog.h"
#include "CaptureWindow.h"
#include "FileTreeModel.h"
#include "FindBar.h"
#include "HealthDialog.h"
#include "MarkdownHighlighter.h"
#include "NoteEditor.h"
#include "Panels.h"
#include "QuickSwitcher.h"
#include "ReadingView.h"
#include "SettingsDialog.h"
#include "TasksPanel.h"
#include "core/ai/FrontmatterMerge.h"
#ifdef MN_HAVE_AI
#include "AiAskPanel.h"
#include "AiDialogs.h"
#include "core/ai/Assistant.h"
#endif
#include "core/capture/Capture.h"
#include "core/index/IndexStore.h"
#include "core/index/VaultService.h"
#include "core/links/RenamePlanner.h"
#include "core/parse/NoteMetadata.h"
#include "core/templates/Templates.h"
#include "core/vault/Paths.h"

#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QDateTime>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QImage>
#include <QImageWriter>
#include <QInputDialog>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QMouseEvent>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSaveFile>
#include <QScrollBar>
#include <QSettings>
#include <QSplitter>
#include <QStackedWidget>
#include <QStatusBar>
#include <QTabWidget>
#include <QTextBlock>
#include <QTimer>
#include <QTreeView>
#include <QTreeWidget>
#include <QUrl>
#include <QVBoxLayout>

using namespace Qt::StringLiterals;

namespace mn::ui {

namespace {

constexpr int kAutosaveMs = 1000;

QString uniqueName(const QString& root, const QString& folder, const QString& base, const QString& ext)
{
    for (int i = 0; i < 10000; ++i) {
        const QString name = i == 0 ? QString(base + ext) : QString(base + u' ' + QString::number(i) + ext);
        if (!QFileInfo::exists(paths::toAbsolute(root, paths::join(folder, name))))
            return paths::join(folder, name);
    }
    return {};
}

} // namespace

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent)
{
    vault_ = new VaultService(this);
    buildUi();
    buildMenus();
    appearance_ = Appearance::load();
    applyLook(appearance_);
    connect(vault_, &VaultService::snapshotChanged, this, &MainWindow::onSnapshot);
    connect(vault_, &VaultService::filesChangedOnDisk, this, &MainWindow::onFilesChangedOnDisk);
    connect(vault_, &VaultService::errorOccurred, this,
            [this](const QString& msg) { statusBar()->showMessage(msg, 10000); });
    connect(qApp, &QGuiApplication::applicationStateChanged, this, [this](Qt::ApplicationState s) {
        if (s == Qt::ApplicationInactive) {
            saveNow();
        } else if (s == Qt::ApplicationActive && vault_->isOpen()) {
            checkOpenNoteOnDisk();
            vault_->rescan(); // picks up files other apps modified in place
        }
    });
    QSettings settings;
    restoreGeometry(settings.value(u"window/geometry"_s).toByteArray());
    splitter_->restoreState(settings.value(u"window/splitter"_s).toByteArray());
    leftTabs_->setCurrentIndex(settings.value(u"window/leftTab"_s, 0).toInt());
    for (int i : {0, 2}) {
        // A pane hidden by an older version (or collapsed) can always be brought back.
        splitter_->widget(i)->setVisible(true);
        if (splitter_->sizes().value(i) > 0) paneWidth_[i] = splitter_->sizes().value(i);
    }
    connect(splitter_, &QSplitter::splitterMoved, this, &MainWindow::syncPaneActions);
    syncPaneActions();
    QTimer::singleShot(0, this, &MainWindow::syncPaneActions); // sizes are final once laid out
    updateTitle();
}

MainWindow::~MainWindow()
{
    // Children (the editor) are destroyed after this destructor and may emit
    // focus/text signals while going away; nothing may reach a half-destroyed window.
    qApp->removeEventFilter(this);
    autosave_->stop();
    if (!note_.path.isEmpty() && !note_.readOnly && isDirty()) saveNow();
    disconnect(editor_, nullptr, this, nullptr);
    disconnect(editor_->document(), nullptr, this, nullptr);
    disconnect(reading_, nullptr, this, nullptr);
    disconnect(vault_, nullptr, this, nullptr);
    disconnect(noteWatcher_, nullptr, this, nullptr);
    disconnect(qApp, nullptr, this, nullptr);
#ifdef MN_HAVE_AI
    disconnect(assistant_, nullptr, this, nullptr);
    assistant_->client().cancelAll();
    assistant_->stopIndexing();
#endif
}

void MainWindow::buildUi()
{
    splitter_ = new QSplitter(Qt::Horizontal, this);
    splitter_->setChildrenCollapsible(true);

    leftTabs_ = new QTabWidget(splitter_);
    leftTabs_->setDocumentMode(true);
    leftTabs_->setUsesScrollButtons(false); // every tab stays visible; the labels are short
    tree_ = new QTreeView(leftTabs_);
    treeModel_ = new FileTreeModel(tree_);
    tree_->setModel(treeModel_);
    tree_->setHeaderHidden(true);
    tree_->setUniformRowHeights(true);
    tree_->setContextMenuPolicy(Qt::CustomContextMenu);
    tree_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    tree_->setExpandsOnDoubleClick(true);
    connect(tree_, &QTreeView::activated, this, [this](const QModelIndex& idx) {
        if (idx.data(FileTreeModel::IsFolderRole).toBool()) return;
        const QString path = idx.data(FileTreeModel::PathRole).toString();
        if (classifyPath(path) == FileKind::Note)
            openNote(path);
        else
            QDesktopServices::openUrl(QUrl::fromLocalFile(vault_->absolutePath(path)));
    });
    connect(tree_, &QTreeView::clicked, this, [this](const QModelIndex& idx) {
        if (!idx.data(FileTreeModel::IsFolderRole).toBool() &&
            classifyPath(idx.data(FileTreeModel::PathRole).toString()) == FileKind::Note)
            openNote(idx.data(FileTreeModel::PathRole).toString());
    });
    connect(tree_, &QTreeView::customContextMenuRequested, this, &MainWindow::treeContextMenu);
    leftTabs_->addTab(tree_, tr("Files"));
    search_ = new SearchPanel(vault_, leftTabs_);
    leftTabs_->addTab(search_, tr("Search"));
    tags_ = new TagPanel(vault_, leftTabs_);
    leftTabs_->addTab(tags_, tr("Tags"));
    tasks_ = new TasksPanel(vault_, leftTabs_);
    leftTabs_->addTab(tasks_, tr("Tasks"));
    auto openFromPanel = [this](const QString& path, int line, const QString& find) {
        openNote(path, line, {}, find);
    };
    connect(search_, &SearchPanel::openRequested, this, openFromPanel);
    connect(tags_, &TagPanel::openRequested, this, openFromPanel);
    connect(tasks_, &TasksPanel::openRequested, this, openFromPanel);
    connect(tasks_, &TasksPanel::taskEditRequested, this,
            [this](const TaskInfo& t, TaskAction a) { editTask(t, a); });

    // Centre: banner + editor/reading view + find bar.
    auto* centerBox = new QWidget(splitter_);
    auto* cl = new QVBoxLayout(centerBox);
    cl->setContentsMargins(0, 0, 0, 0);
    cl->setSpacing(0);
    banner_ = new QFrame(centerBox);
    banner_->setFrameShape(QFrame::StyledPanel);
    banner_->setAutoFillBackground(true);
    {
        QPalette p = banner_->palette();
        p.setColor(QPalette::Window, QColor(0xff, 0xe8, 0xa0));
        p.setColor(QPalette::WindowText, QColor(0x40, 0x30, 0x00));
        banner_->setPalette(p);
    }
    auto* bl = new QHBoxLayout(banner_);
    bl->setContentsMargins(10, 4, 10, 4);
    bannerText_ = new QLabel(banner_);
    bannerText_->setWordWrap(true);
    bannerButton_ = new QPushButton(banner_);
    bl->addWidget(bannerText_, 1);
    bl->addWidget(bannerButton_);
    connect(bannerButton_, &QPushButton::clicked, this, [this] {
        if (bannerAction_) bannerAction_();
    });
    banner_->hide();
    center_ = new QStackedWidget(centerBox);
    editor_ = new NoteEditor(center_);
    editor_->setVault(vault_);
    editor_->setEnabled(false);
    editor_->setAttachmentHandler([this](const QMimeData* m) { return handleAttachment(m); });
    center_->addWidget(editor_);
    reading_ = new ReadingView(center_);
    reading_->setVault(vault_);
    center_->addWidget(reading_);
    findBar_ = new FindBar(editor_, centerBox);
    cl->addWidget(banner_);
    cl->addWidget(center_, 1);
    cl->addWidget(findBar_);

    backlinks_ = new BacklinksPanel(vault_, splitter_);
    connect(backlinks_, &BacklinksPanel::openRequested, this, openFromPanel);

    splitter_->addWidget(leftTabs_);
    splitter_->addWidget(centerBox);
    splitter_->addWidget(backlinks_);
    splitter_->setStretchFactor(1, 1);
    splitter_->setSizes({260, 760, 280});
    setCentralWidget(splitter_);

    statusPath_ = new QLabel(this);
    statusInfo_ = new QLabel(this);
    statusBar()->addWidget(statusPath_, 1);
    statusBar()->addPermanentWidget(statusInfo_);

    passageTimer_ = new QTimer(this);
    passageTimer_->setSingleShot(true);
    passageTimer_->setInterval(800);
    connect(passageTimer_, &QTimer::timeout, this, [this] {
        if (note_.path.isEmpty() || !backlinks_->isVisible() || backlinks_->width() == 0) return;
        backlinks_->setCursorPassage(editor_->bufferText(), editor_->textCursor().blockNumber());
    });
    connect(editor_, &QPlainTextEdit::cursorPositionChanged, passageTimer_, qOverload<>(&QTimer::start));

    autosave_ = new QTimer(this);
    autosave_->setSingleShot(true);
    autosave_->setInterval(kAutosaveMs);
    connect(autosave_, &QTimer::timeout, this, [this] { saveNow(); });
    connect(editor_->document(), &QTextDocument::contentsChanged, this, [this] {
        if (loading_) return;
        scheduleAutosave();
        updateStatus();
    });
    connect(editor_, &NoteEditor::focusLost, this, [this] { saveNow(); });
    connect(editor_, &NoteEditor::linkSuggestionHint, this, [this](const QString& hint) {
        if (hint.isEmpty())
            statusBar()->clearMessage();
        else
            statusBar()->showMessage(hint, 8000);
    });
    editor_->setLinkSuggestions(QSettings().value(u"editor/linkSuggestions"_s, true).toBool());
    connect(editor_, &NoteEditor::linkActivated, this,
            [this](const QString& target, bool) { followLink(target, note_.path); });
    connect(editor_, &NoteEditor::urlActivated, this,
            [](const QString& url) { QDesktopServices::openUrl(QUrl(url)); });
    connect(reading_, &ReadingView::linkActivated, this, &MainWindow::followLink);
    connect(reading_, &ReadingView::tagActivated, this, [this](const QString& tag) {
        leftTabs_->setCurrentWidget(tags_);
        tags_->selectTag(tag);
    });
    connect(reading_, &ReadingView::openFileRequested, this, [this](const QString& p) {
        QDesktopServices::openUrl(QUrl::fromLocalFile(vault_->absolutePath(p)));
    });

    switcher_ = new QuickSwitcher(this);
    connect(switcher_, &QuickSwitcher::openRequested, this, [this](const QString& p) {
        if (classifyPath(p) == FileKind::Note)
            openNote(p);
        else
            QDesktopServices::openUrl(QUrl::fromLocalFile(vault_->absolutePath(p)));
    });
    connect(switcher_, &QuickSwitcher::createRequested, this, [this](const QString& name) {
        QString base = name;
        if (base.endsWith(u".md"_s, Qt::CaseInsensitive)) base.chop(3);
        const QString folder =
            base.contains(u'/') ? paths::folderOf(base) : vault_->settings().newNoteFolder(note_.path);
        const QString created = createNote(folder, paths::fileName(base));
        if (!created.isEmpty()) openNote(created);
    });

    noteWatcher_ = new QFileSystemWatcher(this);
    connect(noteWatcher_, &QFileSystemWatcher::fileChanged, this, [this] {
        // Debounce: editors write in several steps.
        QTimer::singleShot(150, this, &MainWindow::checkOpenNoteOnDisk);
    });
    qApp->installEventFilter(this);
    buildAssistant();
}

void MainWindow::buildMenus()
{
    QMenu* file = menuBar()->addMenu(tr("&File"));
    file->addAction(tr("&Open vault…"), QKeySequence(u"Ctrl+Shift+O"_s), this, &MainWindow::chooseVault);
    QMenu* recent = file->addMenu(tr("Open &recent"));
    connect(recent, &QMenu::aboutToShow, this, [this, recent] {
        recent->clear();
        const QStringList list = QSettings().value(u"recentVaults"_s).toStringList();
        for (const QString& v : list) {
            recent->addAction(v, this, [this, v] {
                QString err;
                if (!openVault(v, &err)) QMessageBox::warning(this, tr("Open vault"), err);
            });
        }
        if (list.isEmpty()) recent->addAction(tr("(none)"))->setEnabled(false);
    });
    file->addSeparator();
    file->addAction(tr("&New note"), QKeySequence::New, this, &MainWindow::newNote);
    file->addAction(tr("&Quick capture…"), QKeySequence(u"Ctrl+Shift+C"_s), this, &MainWindow::quickCapture);
    file->addAction(tr("&Save"), QKeySequence::Save, this, [this] { saveNow(); });
    file->addAction(tr("&Rename / move…"), QKeySequence(Qt::Key_F2), this, [this] {
        if (!note_.path.isEmpty()) promptRename(note_.path, false);
    });
    file->addAction(tr("&Close note"), QKeySequence(u"Ctrl+W"_s), this, &MainWindow::closeNote);
    file->addSeparator();
    file->addAction(tr("Vault &settings…"), QKeySequence(u"Ctrl+,"_s), this, &MainWindow::showSettings);
    file->addSeparator();
    file->addAction(tr("&Quit"), QKeySequence::Quit, this, &QWidget::close);

    QMenu* edit = menuBar()->addMenu(tr("&Edit"));
    edit->addAction(tr("&Find…"), QKeySequence::Find, this, [this] {
        if (readingMode()) setReadingMode(false);
        findBar_->showFind(false);
    });
    edit->addAction(tr("&Replace…"), QKeySequence(u"Ctrl+H"_s), this, [this] {
        if (readingMode()) setReadingMode(false);
        findBar_->showFind(true);
    });
    edit->addAction(tr("Insert &template…"), QKeySequence(u"Alt+T"_s), this, &MainWindow::insertTemplate);
    QAction* suggest = edit->addAction(tr("Suggest &links while typing"));
    suggest->setCheckable(true);
    suggest->setChecked(editor_->linkSuggestions());
    connect(suggest, &QAction::toggled, this, [this](bool on) {
        editor_->setLinkSuggestions(on);
        QSettings().setValue(u"editor/linkSuggestions"_s, on);
    });
    edit->addAction(tr("Link the mention at the cursor"), QKeySequence(u"Alt+Return"_s), this,
                    [this] { editor_->acceptLinkSuggestion(); })
        ->setShortcutContext(Qt::WidgetShortcut);

    QMenu* view = menuBar()->addMenu(tr("&View"));
    view->addAction(tr("Toggle &reading view"), QKeySequence(u"Ctrl+E"_s), this, &MainWindow::toggleReading);
    view->addAction(tr("&Appearance…"), this, &MainWindow::showAppearance);
    view->addAction(tr("&Quick switcher…"), QKeySequence(u"Ctrl+O"_s), this, [this] { switcher_->popup(); });
    view->addAction(tr("&Search in vault…"), QKeySequence(u"Ctrl+Shift+F"_s), this, [this] {
        leftTabs_->setCurrentWidget(search_);
        search_->focusSearch(editor_->textCursor().selectedText());
    });
    view->addAction(tr("&Tags"), QKeySequence(u"Ctrl+Shift+T"_s), this,
                    [this] { leftTabs_->setCurrentWidget(tags_); });
    view->addAction(tr("Tas&ks"), QKeySequence(u"Ctrl+Shift+K"_s), this,
                    [this] { leftTabs_->setCurrentWidget(tasks_); });
    view->addAction(tr("&Files"), QKeySequence(u"Ctrl+Shift+E"_s), this,
                    [this] { leftTabs_->setCurrentWidget(tree_); });
    view->addSeparator();
    leftPaneAction_ = view->addAction(tr("&Left sidebar"));
    leftPaneAction_->setCheckable(true);
    leftPaneAction_->setShortcut(QKeySequence(u"Ctrl+Shift+B"_s));
    connect(leftPaneAction_, &QAction::triggered, this, [this](bool on) { setPaneVisible(0, on); });
    rightPaneAction_ = view->addAction(tr("&Right panel (links and related notes)"));
    rightPaneAction_->setCheckable(true);
    rightPaneAction_->setShortcut(QKeySequence(u"Ctrl+Shift+R"_s));
    connect(rightPaneAction_, &QAction::triggered, this, [this](bool on) { setPaneVisible(2, on); });
    view->addAction(tr("Reset &layout"), this, &MainWindow::resetLayout);
    view->addSeparator();
    view->addAction(tr("&Back"), QKeySequence(u"Alt+Left"_s), this, &MainWindow::goBack);
    view->addAction(tr("&Forward"), QKeySequence(u"Alt+Right"_s), this, &MainWindow::goForward);

    QMenu* tools = menuBar()->addMenu(tr("&Tools"));
    tools->addAction(tr("Vault &health…"), this, &MainWindow::showHealth);
    tools->addAction(tr("&Unresolved links…"), this, &MainWindow::showUnresolved);
    tools->addAction(tr("&Rebuild index"), this, [this] { vault_->rescan(); });
#ifdef MN_HAVE_AI
    tools->addSeparator();
    tools->addAction(tr("Suggest &frontmatter and tags…"), QKeySequence(u"Ctrl+Shift+M"_s), this,
                     &MainWindow::suggestFrontmatter);
    tools->addAction(tr("As&k your notes"), QKeySequence(u"Ctrl+Shift+A"_s), this, [this] {
        setPaneVisible(0, true);
        leftTabs_->setCurrentWidget(ask_);
        ask_->focusQuestion();
    });
    tools->addAction(tr("Update &semantic index"), this, [this] { assistant_->updateIndex(); });
    tools->addAction(tr("AI &assistant settings…"), this, &MainWindow::showAiSettings);
#endif

    QMenu* help = menuBar()->addMenu(tr("&Help"));
    help->addAction(tr("&About"), this, [this] {
        QMessageBox::about(this, tr("About Markdown Notes"),
                           tr("<b>Markdown Notes</b> %1<br>A lightweight editor for Obsidian vaults.<br>"
                              "MIT licence. Uses Qt (LGPL), md4c, SQLite and MicroTeX.")
                               .arg(QApplication::applicationVersion()));
    });
}

// --- assistant -------------------------------------------------------------------

bool MainWindow::applyFrontmatter(const FrontmatterPatch& patch, QString* error)
{
    auto fail = [error](const QString& why) {
        if (error) *error = why;
        return false;
    };
    if (note_.path.isEmpty()) return fail(tr("No note is open."));
    if (note_.readOnly || editor_->isReadOnly()) return fail(tr("This note is open read-only."));
    const FrontmatterEdit e = planFrontmatterEdit(editor_->bufferText(), patch);
    if (!e.ok) return fail(e.error);
    if (!e.changed) return true;
    QTextDocument* doc = editor_->document();
    QTextCursor c(doc);
    c.beginEditBlock();
    QString text = e.text;
    if (e.replaceLines > 0) {
        const QTextBlock last = doc->findBlockByNumber(e.replaceLines - 1);
        const QTextBlock next = last.next();
        c.setPosition(0);
        if (next.isValid()) {
            c.setPosition(next.position(), QTextCursor::KeepAnchor);
        } else { // the frontmatter is the whole note
            c.setPosition(last.position() + last.length() - 1, QTextCursor::KeepAnchor);
            if (text.endsWith(u'\n')) text.chop(1);
        }
    }
    c.insertText(text);
    c.endEditBlock();
    return true;
}

#ifdef MN_HAVE_AI
void MainWindow::buildAssistant()
{
    assistant_ = new ai::Assistant(vault_, this);
    ask_ = new AiAskPanel(assistant_, leftTabs_);
    leftTabs_->addTab(ask_, tr("Ask"));
    connect(ask_, &AiAskPanel::openRequested, this,
            [this](const QString& path, int line, const QString& find) { openNote(path, line, {}, find); });
    search_->setSemanticSearch([this](const QString& q, auto cb) { assistant_->search(q, std::move(cb)); });
    aiStatus_ = new QLabel(this);
    statusBar()->addPermanentWidget(aiStatus_);
    aiStatus_->hide();
    connect(assistant_, &ai::Assistant::indexProgress, this, [this](int done, int total) {
        aiStatus_->setVisible(done < total);
        aiStatus_->setText(tr("Semantic index %1/%2").arg(done).arg(total));
        aiStatus_->setToolTip(tr("Embedding passages for semantic search"));
    });
    connect(assistant_, &ai::Assistant::indexError, this, [this](const QString& message) {
        aiStatus_->setText(tr("Semantic index paused"));
        aiStatus_->setToolTip(message + u"\n\n"_s + tr("Tools › Update semantic index tries again."));
        aiStatus_->show();
    });
    assistant_->setConfig(ai::AiConfig::load());
}

void MainWindow::showAiSettings()
{
    AiSettingsDialog dlg(assistant_->config(), this);
    if (dlg.exec() != QDialog::Accepted) return;
    const ai::AiConfig c = dlg.config();
    c.save();
    assistant_->setConfig(c);
}

void MainWindow::suggestFrontmatter()
{
    if (note_.path.isEmpty()) return;
    if (note_.readOnly) {
        QMessageBox::information(this, tr("Suggest frontmatter"), tr("This note is open read-only."));
        return;
    }
    if (!assistant_->config().chatReady()) {
        showAiSettings();
        if (!assistant_->config().chatReady()) return;
    }
    const QString path = note_.path;
    statusBar()->showMessage(tr("Asking the model about %1…").arg(paths::linkName(path)));
    assistant_->suggestFrontmatter(path, editor_->bufferText(),
                                   [this, path](const ai::FrontmatterSuggestion& s) {
                                       statusBar()->clearMessage();
                                       if (path != note_.path) return; // another note was opened meanwhile
                                       if (!s.error.isEmpty()) {
                                           QMessageBox::warning(this, tr("Suggest frontmatter"), s.error);
                                           return;
                                       }
                                       // Reviewed against the note as it is now (it may have been edited
                                       // while waiting).
                                       AiFrontmatterDialog dlg(editor_->bufferText(), s, this);
                                       if (dlg.exec() != QDialog::Accepted || path != note_.path) return;
                                       QString err;
                                       if (!applyFrontmatter(dlg.patch(), &err))
                                           QMessageBox::warning(this, tr("Suggest frontmatter"), err);
                                   });
}
#else
void MainWindow::buildAssistant() {}
void MainWindow::suggestFrontmatter() {}
void MainWindow::showAiSettings() {}
#endif

// --- vault ---------------------------------------------------------------------

bool MainWindow::openVault(const QString& root, QString* error)
{
    if (!saveNow()) {
        if (error) *error = tr("The open note couldn't be saved.");
        return false;
    }
    closeNote();
#ifdef MN_HAVE_AI
    if (assistant_ != nullptr) assistant_->stopIndexing(); // late replies belong to the old vault
#endif
    if (!vault_->open(root, error)) return false;
    history_.clear();
    historyIndex_ = -1;
    restoredLastNote_ = false;
    QSettings s;
    QStringList recent = s.value(u"recentVaults"_s).toStringList();
    recent.removeAll(vault_->root());
    recent.prepend(vault_->root());
    while (recent.size() > 10)
        recent.removeLast();
    s.setValue(u"recentVaults"_s, recent);
    s.setValue(u"lastVault"_s, vault_->root());
    if (!vault_->settingsWarning().isEmpty()) statusBar()->showMessage(vault_->settingsWarning(), 10000);
    updateTitle();
    return true;
}

void MainWindow::chooseVault()
{
    const QString dir = QFileDialog::getExistingDirectory(this, tr("Open vault folder"), vault_->root());
    if (dir.isEmpty()) return;
    QString err;
    if (!openVault(dir, &err)) QMessageBox::warning(this, tr("Open vault"), err);
}

void MainWindow::onSnapshot()
{
    // Keep expanded folders across rebuilds.
    QStringList expanded;
    for (const QString& f : vault_->snapshot().folders) {
        const QModelIndex idx = treeModel_->indexForPath(f);
        if (idx.isValid() && tree_->isExpanded(idx)) expanded.append(f);
    }
    const int scroll = tree_->verticalScrollBar()->value();
    treeModel_->setSnapshot(vault_->snapshot(), vault_->settings().showAllFiles);
    for (const QString& f : expanded)
        tree_->setExpanded(treeModel_->indexForPath(f), true);
    tree_->verticalScrollBar()->setValue(scroll);
    if (!note_.path.isEmpty()) tree_->setCurrentIndex(treeModel_->indexForPath(note_.path));
    switcher_->setFiles(vault_->snapshot());
    editor_->refreshCompletionSources();
    tags_->refresh();
    tasks_->refresh();
    // Link styling depends on the file set.
    if (static_cast<int>(vault_->snapshot().files().size()) != lastFileCount_) {
        lastFileCount_ = static_cast<int>(vault_->snapshot().files().size());
        if (!note_.path.isEmpty()) editor_->highlighter()->rehighlight();
    }
    if (!restoredLastNote_) restoreLastNote();
}

void MainWindow::restoreLastNote()
{
    restoredLastNote_ = true;
    if (!note_.path.isEmpty()) return;
    const QString last = QSettings().value(u"lastNote/"_s + IndexStore::vaultId(vault_->root())).toString();
    if (!last.isEmpty() && QFileInfo::exists(vault_->absolutePath(last))) openNote(last);
}

// --- open / save -----------------------------------------------------------------

bool MainWindow::isDirty() const
{
    if (note_.path.isEmpty() || note_.readOnly) return false;
    return encodeText(editor_->bufferText(), note_.format, note_.loadedRaw) != note_.diskRaw;
}

void MainWindow::scheduleAutosave()
{
    if (note_.path.isEmpty() || note_.readOnly || note_.orphaned) return;
    autosave_->start();
}

bool MainWindow::saveNow()
{
    autosave_->stop();
    if (note_.path.isEmpty() || note_.readOnly) return true;
    const QByteArray bytes = encodeText(editor_->bufferText(), note_.format, note_.loadedRaw);
    if (bytes == note_.diskRaw && !note_.orphaned) {
        editor_->document()->setModified(false);
        updateStatus();
        return true;
    }
    if (note_.orphaned) return false; // the user decides via the banner
    const QString abs = vault_->absolutePath(note_.path);

    // Never silently overwrite changes made by another program.
    const FileStamp now = statFile(abs);
    if (!now.exists()) {
        note_.orphaned = true;
        showBanner(tr("This note was deleted or moved outside the app. Your text is kept here."),
                   tr("Recreate file"), [this] {
                       note_.orphaned = false;
                       note_.diskRaw.clear();
                       hideBanner();
                       saveNow();
                   });
        return false;
    }
    if (now != note_.diskStamp) {
        const LoadedText disk = loadTextFile(abs);
        if (disk.raw != note_.diskRaw) {
            const auto choice = QMessageBox::warning(
                this, tr("Note changed on disk"),
                tr("“%1” was changed by another program while you were editing it.\n\n"
                   "Overwrite it with your version, or reload it and discard your edits?")
                    .arg(note_.path),
                QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Cancel);
            if (choice == QMessageBox::Discard) {
                reloadFromDisk(disk.raw);
                return true;
            }
            if (choice != QMessageBox::Save) return false;
        }
    }

    const WriteResult w = writeFileAtomic(abs, bytes);
    if (!w.ok) {
        // Keep the buffer dirty; autosave will retry on the next edit.
        statusBar()->showMessage(tr("Couldn't save “%1”: %2").arg(note_.path, w.error));
        if (!note_.saveErrorShown) {
            note_.saveErrorShown = true;
            QMessageBox::critical(
                this, tr("Save failed"),
                tr("Couldn't save “%1”:\n%2\n\nYour text is still here and will be saved when "
                   "possible.")
                    .arg(note_.path, w.error));
        }
        updateStatus();
        return false;
    }
    note_.saveErrorShown = false;
    note_.diskRaw = bytes;
    note_.diskStamp = statFile(abs);
    editor_->document()->setModified(false);
    if (!noteWatcher_->files().contains(abs)) noteWatcher_->addPath(abs);
    vault_->refresh({note_.path});
    updateStatus();
    return true;
}

bool MainWindow::openNote(const QString& path, int line, const QString& subpath, const QString& findText,
                          bool pushHistory)
{
    if (path == note_.path && !path.isEmpty()) {
        navigateWithin(line, subpath, findText);
        return true;
    }
    if (!saveNow()) {
        const auto r = QMessageBox::question(
            this, tr("Unsaved changes"),
            tr("“%1” couldn't be saved. Stay on it so you can copy your text?").arg(note_.path),
            QMessageBox::Yes | QMessageBox::Discard, QMessageBox::Yes);
        if (r != QMessageBox::Discard) return false;
    }
    const QString abs = vault_->absolutePath(path);
    const LoadedText t = loadTextFile(abs);
    if (t.status == LoadStatus::NotFound || t.status == LoadStatus::ReadError) {
        QMessageBox::warning(this, tr("Open note"), tr("Couldn't open “%1”: %2").arg(path, t.error));
        return false;
    }
    // Remember where we were.
    if (pushHistory && !note_.path.isEmpty()) {
        if (historyIndex_ >= 0 && historyIndex_ < static_cast<int>(history_.size()))
            history_[static_cast<std::size_t>(historyIndex_)].position = editor_->textCursor().position();
    }
    if (!note_.path.isEmpty()) noteWatcher_->removePath(vault_->absolutePath(note_.path));

    hideBanner();
    note_ = OpenNote{};
    note_.path = path;
    note_.format = t.format;
    note_.loadedRaw = t.raw;
    note_.diskRaw = t.raw;
    note_.diskStamp = t.stamp;
    loading_ = true;
    editor_->setNotePath(path);
    const bool exact = editor_->setBuffer(t.text);
    loading_ = false;
    editor_->setEnabled(true);
    note_.readOnly = !t.editable() || !exact || !t.roundTrips;
    editor_->setReadOnly(note_.readOnly);
    if (!t.editable()) {
        showBanner(t.error);
    } else if (!exact || !t.roundTrips) {
        showBanner(tr("This note contains characters the editor can't keep exactly (for example old Mac line "
                      "endings), so it's opened read-only to avoid changing them."));
    } else {
        const Frontmatter fm = parseFrontmatter(
            t.raw.startsWith("\xEF\xBB\xBF") ? QByteArrayView(t.raw).sliced(3) : QByteArrayView(t.raw));
        if (fm.present && !fm.valid)
            showBanner(
                tr("The properties (frontmatter) aren't valid YAML: %1 They're left exactly as they are.")
                    .arg(fm.error));
        else if (t.format.mixedEol)
            showBanner(
                tr("This note mixes Windows and Unix line endings. Lines you don't edit keep theirs."));
    }
    editor_->document()->setModified(false);
    noteWatcher_->addPath(abs);

    if (pushHistory) {
        if (historyIndex_ + 1 < static_cast<int>(history_.size()))
            history_.erase(history_.begin() + historyIndex_ + 1, history_.end());
        history_.push_back({path, 0});
        historyIndex_ = static_cast<int>(history_.size()) - 1;
    }
    tree_->setCurrentIndex(treeModel_->indexForPath(path));
    backlinks_->setNote(path);
    if (readingMode()) reading_->showNote(path, t.raw);
    QSettings().setValue(u"lastNote/"_s + IndexStore::vaultId(vault_->root()), path);
    updateTitle();
    updateStatus();
    navigateWithin(line, subpath, findText);
    if (!readingMode()) editor_->setFocus();
    return true;
}

void MainWindow::navigateWithin(int line, const QString& subpath, const QString& findText)
{
    int targetLine = line;
    if (!subpath.isEmpty()) {
        const QByteArray utf8 = editor_->bufferText().toUtf8();
        const NoteMetadata meta = parseNote(utf8);
        if (subpath.startsWith(u'^')) {
            for (const BlockRef& b : meta.blocks) {
                if (b.id.compare(subpath.mid(1), Qt::CaseInsensitive) == 0) targetLine = b.startLine;
            }
        } else {
            QStringList texts;
            for (const Heading& h : meta.headings)
                texts.append(h.text);
            const int idx = findHeading(texts, subpath);
            if (idx >= 0) targetLine = meta.headings[static_cast<std::size_t>(idx)].line;
        }
        if (readingMode()) reading_->scrollToHeading(subpath);
    }
    if (targetLine >= 0) {
        const QTextBlock b = editor_->document()->findBlockByNumber(targetLine);
        if (b.isValid()) {
            QTextCursor c(b);
            editor_->setTextCursor(c);
            editor_->centerCursor();
        }
    } else if (!findText.isEmpty()) {
        QTextCursor c(editor_->document());
        c = editor_->document()->find(findText, c);
        if (!c.isNull()) {
            editor_->setTextCursor(c);
            editor_->centerCursor();
        }
    } else if (subpath.isEmpty()) {
        editor_->moveCursor(QTextCursor::Start);
    }
}

void MainWindow::closeNote()
{
    if (!saveNow()) return;
    if (!note_.path.isEmpty()) noteWatcher_->removePath(vault_->absolutePath(note_.path));
    note_ = OpenNote{};
    loading_ = true;
    editor_->setBuffer(QString());
    loading_ = false;
    editor_->setNotePath({});
    editor_->setEnabled(false);
    backlinks_->setNote({});
    hideBanner();
    updateTitle();
    updateStatus();
}

// --- external changes ------------------------------------------------------------

void MainWindow::onFilesChangedOnDisk(const QStringList& paths)
{
    if (!note_.path.isEmpty() && paths.contains(note_.path)) checkOpenNoteOnDisk();
    if (!note_.path.isEmpty()) backlinks_->refresh();
}

void MainWindow::checkOpenNoteOnDisk()
{
    if (note_.path.isEmpty()) return;
    const QString abs = vault_->absolutePath(note_.path);
    if (!noteWatcher_->files().contains(abs) && QFileInfo::exists(abs)) noteWatcher_->addPath(abs);
    const FileStamp now = statFile(abs);
    if (!now.exists()) {
        if (!note_.orphaned) {
            note_.orphaned = true;
            showBanner(tr("This note was deleted or moved outside the app. Your text is kept here."),
                       tr("Recreate file"), [this] {
                           note_.orphaned = false;
                           note_.diskRaw.clear();
                           hideBanner();
                           saveNow();
                       });
        }
        return;
    }
    if (now == note_.diskStamp && !note_.orphaned) return;
    const LoadedText disk = loadTextFile(abs);
    if (disk.raw == note_.diskRaw) {
        note_.diskStamp = now;
        if (note_.orphaned) {
            note_.orphaned = false;
            hideBanner();
        }
        return;
    }
    if (!isDirty() || note_.readOnly) {
        reloadFromDisk(disk.raw);
        return;
    }
    const auto choice =
        QMessageBox::question(this, tr("Note changed on disk"),
                              tr("“%1” was changed by another program, and you have unsaved edits.\n\nReload "
                                 "it from disk (your edits are "
                                 "discarded) or keep your version (it overwrites the file on the next save)?")
                                  .arg(note_.path),
                              QMessageBox::Discard | QMessageBox::Save, QMessageBox::Save);
    if (choice == QMessageBox::Discard) {
        reloadFromDisk(disk.raw);
    } else {
        note_.diskRaw = disk.raw;
        note_.diskStamp = now;
        note_.orphaned = false;
        scheduleAutosave();
    }
}

void MainWindow::reloadFromDisk(const QByteArray& raw)
{
    const LoadedText t = decodeText(raw);
    const int pos = editor_->textCursor().position();
    const int scroll = editor_->verticalScrollBar()->value();
    loading_ = true;
    const bool exact = editor_->setBuffer(t.text);
    loading_ = false;
    note_.format = t.format;
    note_.loadedRaw = raw;
    note_.diskRaw = raw;
    note_.diskStamp = statFile(vault_->absolutePath(note_.path));
    note_.orphaned = false;
    note_.readOnly = !t.editable() || !exact || !t.roundTrips;
    editor_->setReadOnly(note_.readOnly);
    editor_->document()->setModified(false);
    QTextCursor c = editor_->textCursor();
    c.setPosition(std::min(pos, editor_->document()->characterCount() - 1));
    editor_->setTextCursor(c);
    editor_->verticalScrollBar()->setValue(scroll);
    if (readingMode()) reading_->showNote(note_.path, raw);
    hideBanner();
    statusBar()->showMessage(tr("Reloaded “%1” from disk.").arg(note_.path), 4000);
    updateStatus();
}

// --- links / creation ---------------------------------------------------------------

void MainWindow::followLink(const QString& target, const QString& sourcePath)
{
    QString lp;
    QString sub;
    splitLinkTarget(target, lp, sub);
    if (lp.isEmpty()) {
        if (sourcePath != note_.path) openNote(sourcePath);
        navigateWithin(-1, sub, {});
        return;
    }
    const Resolution r = vault_->files().resolve(lp, sourcePath);
    if (r.resolved()) {
        const QString path = vault_->files().file(r.file).path;
        if (classifyPath(path) == FileKind::Note)
            openNote(path, -1, sub);
        else
            QDesktopServices::openUrl(QUrl::fromLocalFile(vault_->absolutePath(path)));
        return;
    }
    // Unresolved: create the note (compat spec §3 step 6).
    QString base = lp;
    if (base.endsWith(u".md"_s, Qt::CaseInsensitive)) base.chop(3);
    if (hasKnownExtension(lp) && classifyPath(lp) != FileKind::Note) {
        QMessageBox::information(this, tr("Missing file"), tr("“%1” doesn't exist in this vault.").arg(lp));
        return;
    }
    QString folder;
    if (base.contains(u'/')) {
        const auto norm =
            paths::normalize(base.startsWith(u'.') ? paths::join(paths::folderOf(sourcePath), base) : base);
        if (!norm) return;
        folder = paths::folderOf(*norm);
        base = paths::fileName(*norm);
    } else {
        folder = vault_->settings().newNoteFolder(sourcePath);
    }
    const QString created = createNote(folder, base);
    if (!created.isEmpty()) openNote(created);
}

QString MainWindow::createNote(const QString& folder, const QString& baseName, const QByteArray& content)
{
    const QString why = checkNewVaultPath(paths::join(folder, baseName));
    if (!why.isEmpty()) {
        QMessageBox::warning(this, tr("Can't create note"), tr("“%1”: %2").arg(baseName, why));
        return {};
    }
    QString path = paths::join(folder, QString(baseName + u".md"_s));
    const QString abs = vault_->absolutePath(path);
    if (QFileInfo::exists(abs)) return path;
    if (!QDir().mkpath(QFileInfo(abs).absolutePath())) {
        QMessageBox::warning(this, tr("Can't create note"),
                             tr("Couldn't create the folder “%1”.").arg(folder));
        return {};
    }
    const WriteResult w = writeFileAtomic(abs, content);
    if (!w.ok) {
        QMessageBox::warning(this, tr("Can't create note"), w.error);
        return {};
    }
    vault_->refresh({path});
    return path;
}

void MainWindow::newNote()
{
    const QString folder = vault_->settings().newNoteFolder(note_.path);
    const QString path = uniqueName(vault_->root(), folder, u"Untitled"_s, u".md"_s);
    if (path.isEmpty()) return;
    const QString created = createNote(folder, paths::linkName(path));
    if (!created.isEmpty()) openNote(created);
}

void MainWindow::newFolder(const QString& parentFolder)
{
    bool ok = false;
    const QString name =
        QInputDialog::getText(this, tr("New folder"), tr("Folder name:"), QLineEdit::Normal, {}, &ok);
    if (!ok || name.trimmed().isEmpty()) return;
    const QString why = checkNewFileName(name);
    if (!why.isEmpty()) {
        QMessageBox::warning(this, tr("New folder"), why);
        return;
    }
    const QString path = paths::join(parentFolder, name.trimmed());
    if (!QDir().mkpath(vault_->absolutePath(path)))
        QMessageBox::warning(this, tr("New folder"), tr("Couldn't create it."));
    vault_->refresh({paths::join(path, u"x"_s)});
}

void MainWindow::deletePath(const QString& path, bool folder)
{
    const auto r = QMessageBox::question(
        this, tr("Move to trash"),
        folder ? tr("Move the folder “%1” and everything in it to the system trash?").arg(path)
               : tr("Move “%1” to the system trash?").arg(path));
    if (r != QMessageBox::Yes) return;
    if (!folder && path == note_.path) {
        saveNow();
        closeNote();
    } else if (folder && paths::isInside(note_.path, path)) {
        saveNow();
        closeNote();
    }
    if (!QFile::moveToTrash(vault_->absolutePath(path)))
        QMessageBox::warning(this, tr("Move to trash"), tr("Couldn't move “%1” to the trash.").arg(path));
    vault_->rescan();
}

// --- rename -----------------------------------------------------------------------

void MainWindow::promptRename(const QString& path, bool folder)
{
    const bool note = !folder && classifyPath(path) == FileKind::Note;
    QString current = note ? path.chopped(3) : path;
    bool ok = false;
    const QString entered =
        QInputDialog::getText(this, folder ? tr("Rename folder") : tr("Rename / move"),
                              tr("New path (folders separated by “/”):"), QLineEdit::Normal, current, &ok);
    if (!ok) return;
    QString target = entered.trimmed();
    while (target.startsWith(u'/'))
        target.remove(0, 1);
    if (target.isEmpty() || target == current) return;
    const auto norm = paths::normalize(target);
    if (!norm || norm->isEmpty()) {
        QMessageBox::warning(this, tr("Rename"), tr("That path is outside the vault."));
        return;
    }
    target = *norm;
    for (const QString& seg : target.split(u'/')) {
        const QString why = checkNewFileName(seg);
        if (!why.isEmpty()) {
            QMessageBox::warning(this, tr("Rename"), tr("“%1”: %2").arg(seg, why));
            return;
        }
    }
    if (note)
        target += u".md"_s;
    else if (!folder && extensionOf(target) != extensionOf(path))
        target += u'.' + extensionOf(path);
    renamePath(path, target, true);
}

void MainWindow::renamePath(const QString& from, const QString& to, bool confirm)
{
    const bool folder = QFileInfo(vault_->absolutePath(from)).isDir();
    if (folder) {
        std::vector<Move> moves;
        for (const VaultFile& f : vault_->snapshot().files()) {
            if (paths::isInside(f.path, from)) moves.push_back({f.path, to + f.path.mid(from.size())});
        }
        runRename(moves, Move{from, to}, confirm);
    } else {
        runRename({{from, to}}, std::nullopt, confirm);
    }
}

void MainWindow::runRename(const std::vector<Move>& moves, const std::optional<Move>& folderMove,
                           bool confirm)
{
    if (!saveNow()) {
        QMessageBox::warning(this, tr("Rename"), tr("Save the open note first."));
        return;
    }
    statusBar()->showMessage(tr("Finding links to update…"));
    vault_->planRename(moves, [this, moves, folderMove, confirm](RenamePlan plan) {
        statusBar()->clearMessage();
        if (!saveNow()) return;
        if ((confirm || !plan.warnings.isEmpty()) && (!plan.files.empty() || !plan.warnings.isEmpty())) {
            if (!confirmPlan(plan)) return;
        }
        // The plan may be stale if the open note changed meanwhile; executeRenamePlan
        // skips any file whose bytes differ from the plan.
        const RenameResult r = executeRenamePlan(vault_->root(), plan, folderMove);
        if (!r.moved) {
            QMessageBox::warning(this, tr("Rename"), r.moveError);
            return;
        }
        QStringList touched;
        for (const Move& m : moves) {
            touched << m.from << m.to;
        }
        touched << r.updated;
        // Follow the open note to its new path, and reload it if its links changed.
        QString openPath = note_.path;
        for (const Move& m : moves) {
            if (m.from == openPath) openPath = m.to;
        }
        if (!note_.path.isEmpty()) {
            noteWatcher_->removePath(vault_->absolutePath(note_.path));
            const QString newAbs = vault_->absolutePath(openPath);
            note_.path = openPath;
            editor_->setNotePath(openPath);
            const LoadedText disk = loadTextFile(newAbs);
            if (disk.raw != note_.diskRaw) reloadFromDisk(disk.raw);
            note_.diskStamp = statFile(newAbs);
            noteWatcher_->addPath(newAbs);
            backlinks_->setNote(openPath);
            updateTitle();
        }
        vault_->refresh(touched);
        if (folderMove) vault_->rescan();
        if (!r.failed.isEmpty()) {
            QMessageBox::warning(
                this, tr("Rename"),
                tr("Renamed. These files were updated:\n%1\n\nThese files were NOT updated:\n%2")
                    .arg(r.updated.isEmpty() ? tr("(none)") : r.updated.join(u'\n'), r.failed.join(u'\n')));
        } else {
            statusBar()->showMessage(
                tr("Renamed; updated links in %n file(s).", nullptr, static_cast<int>(r.updated.size())),
                6000);
        }
    });
}

bool MainWindow::confirmPlan(const RenamePlan& plan)
{
    QDialog dlg(this);
    dlg.setWindowTitle(tr("Update links?"));
    auto* lay = new QVBoxLayout(&dlg);
    lay->addWidget(new QLabel(
        tr("Renaming will update links in these %n file(s):", nullptr, static_cast<int>(plan.files.size())),
        &dlg));
    auto* tree = new QTreeWidget(&dlg);
    tree->setHeaderLabels({tr("File / line"), tr("Change")});
    for (const FileEditPlan& f : plan.files) {
        auto* top =
            new QTreeWidgetItem(tree, {f.path, tr("%n link(s)", nullptr, static_cast<int>(f.edits.size()))});
        for (const LinkEdit& e : f.edits)
            new QTreeWidgetItem(top, {tr("line %1").arg(e.line + 1), e.before + u"  →  "_s + e.after});
    }
    tree->expandAll();
    tree->resizeColumnToContents(0);
    lay->addWidget(tree, 1);
    if (!plan.warnings.isEmpty()) {
        auto* w =
            new QLabel(tr("These links can't be updated automatically:\n") + plan.warnings.join(u'\n'), &dlg);
        w->setWordWrap(true);
        lay->addWidget(w);
    }
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("Rename and update links"));
    connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    lay->addWidget(buttons);
    dlg.resize(720, 460);
    return dlg.exec() == QDialog::Accepted;
}

// --- templates / attachments ---------------------------------------------------------

void MainWindow::insertTemplate()
{
    if (note_.path.isEmpty() || note_.readOnly) return;
    const QString folder = vault_->settings().templatesFolder;
    if (folder.isEmpty()) {
        QMessageBox::information(this, tr("Insert template"),
                                 tr("No template folder is set in the vault settings."));
        return;
    }
    QStringList names;
    QStringList pathsList;
    for (const VaultFile& f : vault_->snapshot().files()) {
        if (f.kind == FileKind::Note && paths::isInside(f.path, folder)) {
            names.append(f.path.mid(folder.size() + 1).chopped(3));
            pathsList.append(f.path);
        }
    }
    if (names.isEmpty()) {
        QMessageBox::information(this, tr("Insert template"),
                                 tr("The template folder “%1” has no notes.").arg(folder));
        return;
    }
    bool ok = false;
    const QString choice =
        QInputDialog::getItem(this, tr("Insert template"), tr("Template:"), names, 0, false, &ok);
    if (!ok) return;
    const LoadedText tpl = loadTextFile(vault_->absolutePath(pathsList[names.indexOf(choice)]));
    if (!tpl.editable()) return;
    TemplateContext ctx;
    ctx.title = paths::linkName(note_.path);
    ctx.now = QDateTime::currentDateTime();
    ctx.dateFormat = vault_->settings().dateFormat;
    ctx.timeFormat = vault_->settings().timeFormat;
    const QString text = substituteTemplate(tpl.text, ctx);
    const TemplateInsertion ins = planTemplateInsertion(editor_->bufferText(), text);
    QTextCursor c = editor_->textCursor();
    c.beginEditBlock();
    int pos = c.position();
    if (ins.frontmatterChanged) {
        QTextCursor fm(editor_->document());
        fm.setPosition(0);
        fm.setPosition(ins.replaceLength, QTextCursor::KeepAnchor);
        fm.insertText(ins.frontmatter);
        const int delta = static_cast<int>(ins.frontmatter.size()) - ins.replaceLength;
        pos = pos < ins.replaceLength ? static_cast<int>(ins.frontmatter.size()) : pos + delta;
    }
    c.setPosition(pos);
    c.insertText(ins.body);
    c.endEditBlock();
    editor_->setTextCursor(c);
    if (!ins.warning.isEmpty()) statusBar()->showMessage(ins.warning, 8000);
}

QString MainWindow::handleAttachment(const QMimeData* mime)
{
    if (note_.path.isEmpty() || note_.readOnly) return {};
    const VaultSettings& st = vault_->settings();
    const QString folder = st.attachmentFolderFor(note_.path);
    if (!folder.isEmpty() && !checkNewVaultPath(folder).isEmpty()) {
        QMessageBox::warning(
            this, tr("Add attachment"),
            tr("The attachment folder “%1” isn't allowed: %2").arg(folder, checkNewVaultPath(folder)));
        return {};
    }
    auto linkFor = [&](const QString& path, bool embed) -> QString {
        if (st.useMarkdownLinks) {
            const QString lp = vault_->files().linkpathFor(path, note_.path, st.newLinkFormat, true);
            return QString(embed ? u"!"_s : QString()) + u'[' + paths::completeBaseName(path) + u"]("_s +
                   encodeMarkdownPath(lp) + u')';
        }
        // The new file isn't in the index yet: use its name unless it would be ambiguous.
        QString lp = st.newLinkFormat == LinkStyle::Absolute ? path
                     : st.newLinkFormat == LinkStyle::Relative
                         ? paths::relativePath(paths::folderOf(note_.path), path)
                         : (vault_->files().filesNamed(paths::fileName(path)) ? path : paths::fileName(path));
        if (classifyPath(path) == FileKind::Note && lp.endsWith(u".md"_s)) lp.chop(3);
        return QString(embed ? u"!"_s : QString()) + u"[["_s + lp + u"]]"_s;
    };
    if (mime->hasImage()) {
        const QImage img = qvariant_cast<QImage>(mime->imageData());
        if (img.isNull()) return {};
        const QString base = u"Pasted image "_s + QDateTime::currentDateTime().toString(u"yyyyMMddHHmmss"_s);
        const QString path = uniqueName(vault_->root(), folder, base, u".png"_s);
        const QString abs = vault_->absolutePath(path);
        QDir().mkpath(QFileInfo(abs).absolutePath());
        QSaveFile f(abs);
        if (!f.open(QIODevice::WriteOnly) || !img.save(&f, "PNG") || !f.commit()) {
            QMessageBox::warning(this, tr("Paste image"), tr("Couldn't save the image to “%1”.").arg(path));
            return {};
        }
        vault_->refresh({path});
        return linkFor(path, true);
    }
    if (mime->hasUrls()) {
        QStringList parts;
        for (const QUrl& url : mime->urls()) {
            if (!url.isLocalFile()) continue;
            const QFileInfo fi(url.toLocalFile());
            if (!fi.isFile()) continue;
            const QString inVault = paths::toVaultPath(vault_->root(), fi.absoluteFilePath());
            const FileKind kind = classifyPath(fi.fileName());
            if (!inVault.startsWith(u"../"_s) && !QDir::isAbsolutePath(inVault)) {
                parts.append(linkFor(inVault, isAttachmentKind(kind)));
                continue;
            }
            if (!isAttachmentKind(kind)) continue;
            // Copy outside files into the attachment folder.
            const QString path =
                uniqueName(vault_->root(), folder, fi.completeBaseName(), u'.' + fi.suffix());
            const QString abs = vault_->absolutePath(path);
            QDir().mkpath(QFileInfo(abs).absolutePath());
            if (!QFile::copy(fi.absoluteFilePath(), abs)) {
                QMessageBox::warning(this, tr("Add attachment"),
                                     tr("Couldn't copy “%1”.").arg(fi.fileName()));
                continue;
            }
            vault_->refresh({path});
            parts.append(linkFor(path, true));
        }
        return parts.join(u'\n');
    }
    return {};
}

// --- navigation / misc -------------------------------------------------------------

void MainWindow::goBack()
{
    if (historyIndex_ <= 0) return;
    history_[static_cast<std::size_t>(historyIndex_)].position = editor_->textCursor().position();
    const HistoryEntry e = history_[static_cast<std::size_t>(--historyIndex_)];
    if (!openNote(e.path, -1, {}, {}, false)) {
        ++historyIndex_;
        return;
    }
    QTextCursor c = editor_->textCursor();
    c.setPosition(std::min(e.position, editor_->document()->characterCount() - 1));
    editor_->setTextCursor(c);
    editor_->centerCursor();
}

void MainWindow::goForward()
{
    if (historyIndex_ + 1 >= static_cast<int>(history_.size())) return;
    if (historyIndex_ >= 0)
        history_[static_cast<std::size_t>(historyIndex_)].position = editor_->textCursor().position();
    const HistoryEntry e = history_[static_cast<std::size_t>(++historyIndex_)];
    if (!openNote(e.path, -1, {}, {}, false)) {
        --historyIndex_;
        return;
    }
    QTextCursor c = editor_->textCursor();
    c.setPosition(std::min(e.position, editor_->document()->characterCount() - 1));
    editor_->setTextCursor(c);
}

bool MainWindow::readingMode() const
{
    return center_->currentWidget() == reading_;
}

void MainWindow::setReadingMode(bool on)
{
    if (on == readingMode()) return;
    if (on) {
        if (note_.path.isEmpty()) return;
        saveNow();
        findBar_->hide();
        const QByteArray raw = encodeText(editor_->bufferText(), note_.format, note_.loadedRaw);
        center_->setCurrentWidget(reading_);
        reading_->showNote(note_.path, raw);
        reading_->setFocus();
    } else {
        center_->setCurrentWidget(editor_);
        editor_->setFocus();
    }
}

void MainWindow::toggleReading()
{
    setReadingMode(!readingMode());
}

void MainWindow::showSettings()
{
    if (!vault_->isOpen()) return;
    SettingsDialog dlg(vault_->settings(), vault_, this);
    if (dlg.exec() != QDialog::Accepted) return;
    QString err;
    if (!vault_->setSettings(dlg.settings(), &err)) {
        QMessageBox::warning(this, tr("Settings"), tr("Couldn't save the settings: %1").arg(err));
        return;
    }
    treeModel_->setSnapshot(vault_->snapshot(), vault_->settings().showAllFiles);
    tags_->refresh();
    if (readingMode()) reading_->rerender();
}

void MainWindow::applyLook(const Appearance& a)
{
    // With default settings the desktop's own theme is left completely alone.
    if (lookApplied_ || !(a == Appearance{})) {
        applyAppearance(a);
        lookApplied_ = true;
    }
    const int chars = a.readableLineLength ? a.lineLengthChars : 0;
    editor_->setFont(a.editorFont());
    editor_->setMonoFamily(a.monoFamily);
    editor_->setMaxLineChars(chars);
    editor_->applyTheme();
    reading_->setFont(a.readingFont());
    reading_->setMonoFamily(a.monoFamily);
    reading_->setMaxLineChars(chars);
    if (!reading_->notePath().isEmpty()) reading_->rerender();
}

void MainWindow::showAppearance()
{
    AppearanceDialog dlg(appearance_, this);
    connect(&dlg, &AppearanceDialog::changed, this, &MainWindow::applyLook);
    if (dlg.exec() == QDialog::Accepted) {
        appearance_ = dlg.appearance();
        appearance_.save();
    }
    applyLook(appearance_);
}

void MainWindow::quickCapture()
{
    if (!vault_->isOpen()) return;
    const VaultSettings& st = vault_->settings();
    const QDateTime now = QDateTime::currentDateTime();
    const QString target = captureTargetPath(st, now);
    CaptureWindow dlg(target, this);
    connect(&dlg, &CaptureWindow::submitted, &dlg, [this, &dlg, &st, target](const QString& text) {
        const QDateTime at = QDateTime::currentDateTime();
        if (target == note_.path && !note_.readOnly) {
            // The target is open: append in the editor (undoable) and save.
            QTextCursor c(editor_->document());
            c.movePosition(QTextCursor::End);
            c.beginEditBlock();
            const QString buffer = editor_->bufferText();
            if (!buffer.isEmpty() && !buffer.endsWith(u'\n')) c.insertText(u"\n"_s);
            c.insertText(formatCapture(st, text, at) + u'\n');
            c.endEditBlock();
            if (!saveNow()) {
                dlg.showError(tr("The note couldn't be saved; your capture is in the editor."));
                return;
            }
        } else {
            const CaptureResult r = appendCapture(vault_->root(), st, text, at);
            if (!r.ok) {
                dlg.showError(r.error);
                return;
            }
            vault_->refresh({r.path});
        }
        statusBar()->showMessage(tr("Captured to %1").arg(target), 4000);
        dlg.accept();
    });
    dlg.exec();
}

bool MainWindow::editTask(const TaskInfo& task, TaskAction action, bool confirmDelete)
{
    if (!vault_->isOpen()) return false;
    if (action == TaskAction::Delete && confirmDelete) {
        const auto r = QMessageBox::question(
            this, tr("Delete task"),
            tr("Delete this task from “%1”? Nested items under it are deleted too.\n\n%2")
                .arg(paths::linkName(task.path), task.text),
            QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel);
        if (r != QMessageBox::Yes) return false;
    }
    const TaskLocator where{task.line, task.text};
    bool ok = false;
    QString error;
    if (task.path == note_.path && !note_.readOnly) {
        // The note is open: edit the buffer (undoable), then save.
        const QByteArray body = editor_->bufferText().toUtf8();
        if (const auto e = planTaskEdit(body, where, action, &error)) {
            const int start = static_cast<int>(QString::fromUtf8(body.left(e->start)).size());
            const int end = static_cast<int>(QString::fromUtf8(body.left(e->end)).size());
            QTextCursor c(editor_->document());
            c.setPosition(start);
            c.setPosition(end, QTextCursor::KeepAnchor);
            c.insertText(QString::fromUtf8(e->replacement));
            ok = saveNow();
            if (!ok) error = tr("The note couldn't be saved; the change is in the editor.");
        }
    } else {
        const TaskEditResult r = editTaskInFile(vault_->absolutePath(task.path), where, action);
        ok = r.ok;
        error = r.error;
        if (ok) vault_->refresh({task.path});
    }
    if (!ok) QMessageBox::warning(this, tr("Tasks"), error);
    tasks_->refresh();
    return ok;
}

bool MainWindow::paneVisible(int index) const
{
    return splitter_->sizes().value(index) > 0 && splitter_->widget(index)->isVisible();
}

void MainWindow::setPaneVisible(int index, bool visible)
{
    QList<int> sizes = splitter_->sizes();
    if (index < 0 || index >= sizes.size() || index == 1) return;
    splitter_->widget(index)->setVisible(true);
    if (visible == (sizes[index] > 0)) {
        syncPaneActions();
        return;
    }
    if (visible) {
        const int want = std::max(160, paneWidth_[index]);
        sizes[index] = want;
        sizes[1] = std::max(200, sizes[1] - want);
    } else {
        paneWidth_[index] = sizes[index];
        sizes[1] += sizes[index];
        sizes[index] = 0;
    }
    splitter_->setSizes(sizes);
    syncPaneActions();
}

void MainWindow::resetLayout()
{
    const int total = std::max(900, splitter_->width());
    for (int i = 0; i < 3; ++i)
        splitter_->widget(i)->setVisible(true);
    paneWidth_[0] = 260;
    paneWidth_[2] = 280;
    splitter_->setSizes({260, total - 260 - 280, 280});
    syncPaneActions();
}

void MainWindow::syncPaneActions()
{
    const QList<int> sizes = splitter_->sizes();
    if (leftPaneAction_ != nullptr) leftPaneAction_->setChecked(sizes.value(0) > 0);
    if (rightPaneAction_ != nullptr) rightPaneAction_->setChecked(sizes.value(2) > 0);
    for (int i : {0, 2}) {
        if (sizes.value(i) > 0) paneWidth_[i] = sizes.value(i);
    }
}

void MainWindow::showHealth()
{
    if (!vault_->isOpen()) return;
    auto* dlg = new HealthDialog(vault_, this);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    connect(dlg, &HealthDialog::openRequested, this,
            [this](const QString& path, int line, const QString& find) { openNote(path, line, {}, find); });
    connect(dlg, &HealthDialog::tagRequested, this, [this](const QString& tag) {
        leftTabs_->setCurrentWidget(tags_);
        tags_->selectTag(tag);
    });
    dlg->show();
    dlg->run();
}

void MainWindow::showUnresolved()
{
    vault_->unresolvedLinks([this](std::vector<UnresolvedLink> links) {
        QDialog dlg(this);
        dlg.setWindowTitle(tr("Unresolved links"));
        auto* lay = new QVBoxLayout(&dlg);
        lay->addWidget(new QLabel(
            tr("%n unresolved link(s) in the vault.", nullptr, static_cast<int>(links.size())), &dlg));
        auto* tree = new QTreeWidget(&dlg);
        tree->setHeaderLabels({tr("Link"), tr("In note"), tr("Line")});
        for (const UnresolvedLink& l : links)
            new QTreeWidgetItem(tree, {l.linkpath, l.sourcePath, QString::number(l.line + 1)});
        tree->resizeColumnToContents(0);
        connect(tree, &QTreeWidget::itemActivated, &dlg, [this, &dlg](QTreeWidgetItem* it) {
            dlg.accept();
            openNote(it->text(1), it->text(2).toInt() - 1);
        });
        lay->addWidget(tree, 1);
        dlg.resize(700, 500);
        dlg.exec();
    });
}

void MainWindow::treeContextMenu(const QPoint& pos)
{
    if (!vault_->isOpen()) return;
    const QModelIndex idx = tree_->indexAt(pos);
    const QString path = idx.data(FileTreeModel::PathRole).toString();
    const bool folder = !idx.isValid() || idx.data(FileTreeModel::IsFolderRole).toBool();
    const QString folderPath = folder ? path : paths::folderOf(path);
    QMenu menu(this);
    menu.addAction(tr("New note"), this, [this, folderPath] {
        const QString p = uniqueName(vault_->root(), folderPath, u"Untitled"_s, u".md"_s);
        const QString created = createNote(folderPath, paths::linkName(p));
        if (!created.isEmpty()) openNote(created);
    });
    menu.addAction(tr("New folder"), this, [this, folderPath] { newFolder(folderPath); });
    if (idx.isValid()) {
        menu.addSeparator();
        menu.addAction(tr("Rename / move…"), this, [this, path, folder] { promptRename(path, folder); });
        menu.addAction(tr("Move to trash"), this, [this, path, folder] { deletePath(path, folder); });
        menu.addSeparator();
        menu.addAction(tr("Show in file manager"), this, [this, path, folder] {
            QDesktopServices::openUrl(
                QUrl::fromLocalFile(vault_->absolutePath(folder ? path : paths::folderOf(path))));
        });
    }
    menu.exec(tree_->viewport()->mapToGlobal(pos));
}

void MainWindow::showBanner(const QString& text, const QString& actionText, std::function<void()> action)
{
    bannerText_->setText(text);
    bannerButton_->setVisible(!actionText.isEmpty());
    bannerButton_->setText(actionText);
    bannerAction_ = std::move(action);
    banner_->show();
}

void MainWindow::hideBanner()
{
    banner_->hide();
    bannerAction_ = nullptr;
}

void MainWindow::updateTitle()
{
    QString t = u"Markdown Notes"_s;
    if (vault_->isOpen()) t = QFileInfo(vault_->root()).fileName() + u" — "_s + t;
    if (!note_.path.isEmpty()) t = paths::linkName(note_.path) + u" — "_s + t;
    setWindowTitle(t);
}

void MainWindow::updateStatus()
{
    if (note_.path.isEmpty()) {
        statusPath_->clear();
        statusInfo_->clear();
        return;
    }
    statusPath_->setText(note_.path);
    QString info;
    if (note_.readOnly)
        info = tr("Read-only");
    else
        info = editor_->document()->isModified() ? tr("Edited") : tr("Saved");
    const int chars = editor_->document()->characterCount() - 1;
    info += u"   "_s + tr("%n character(s)", nullptr, chars);
    statusInfo_->setText(info);
}

void MainWindow::closeEvent(QCloseEvent* e)
{
    if (!saveNow()) {
        const auto r = QMessageBox::warning(
            this, tr("Unsaved changes"),
            tr("“%1” couldn't be saved. Quit anyway and lose the changes?").arg(note_.path),
            QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Cancel);
        if (r != QMessageBox::Discard) {
            e->ignore();
            return;
        }
    }
    QSettings s;
    s.setValue(u"window/geometry"_s, saveGeometry());
    s.setValue(u"window/splitter"_s, splitter_->saveState());
    s.setValue(u"window/leftTab"_s, leftTabs_->currentIndex());
    vault_->close();
    e->accept();
}

bool MainWindow::eventFilter(QObject* o, QEvent* e)
{
    // Mouse back/forward buttons navigate history.
    if (e->type() == QEvent::MouseButtonRelease && isActiveWindow()) {
        auto* me = static_cast<QMouseEvent*>(e);
        if (me->button() == Qt::BackButton) {
            goBack();
            return true;
        }
        if (me->button() == Qt::ForwardButton) {
            goForward();
            return true;
        }
    }
    return QMainWindow::eventFilter(o, e);
}

} // namespace mn::ui
