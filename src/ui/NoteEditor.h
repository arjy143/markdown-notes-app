#pragma once

#include "core/links/LinkSuggest.h"

#include <QPlainTextEdit>
#include <QSet>

#include <functional>

class QCompleter;
class QStringListModel;
class QMimeData;
class QTimer;

namespace mn {
class VaultService;
}

namespace mn::ui {

class MarkdownHighlighter;

// The source editor: QPlainTextEdit with incremental highlighting, link
// following (Ctrl+click), and [[ / # autocomplete.
class NoteEditor : public QPlainTextEdit {
    Q_OBJECT
public:
    explicit NoteEditor(QWidget* parent = nullptr);

    void setVault(VaultService* vault);
    void setNotePath(const QString& vaultPath) { notePath_ = vaultPath; }
    const QString& notePath() const { return notePath_; }
    MarkdownHighlighter* highlighter() const { return highlighter_; }

    // The buffer exactly as it will be saved: block texts joined by '\n'.
    // (QPlainTextEdit::toPlainText() would turn non-breaking spaces into spaces.)
    QString bufferText() const;
    // Replaces the buffer. Returns false if the editor can't represent the text
    // exactly (the caller then opens the note read-only).
    bool setBuffer(const QString& text);

    // Handles pasted/dropped images or files: returns the Markdown to insert, or
    // an empty string to fall back to the default behaviour.
    using AttachmentHandler = std::function<QString(const QMimeData*)>;
    void setAttachmentHandler(AttachmentHandler h) { attachmentHandler_ = std::move(h); }

    void refreshCompletionSources();
    // Underlines mentions of note names in the current paragraph; Alt+Enter links one.
    void setLinkSuggestions(bool on);
    bool linkSuggestions() const { return suggestLinks_; }
    // Turns the suggestion nearest the cursor into a link. Returns false if none.
    bool acceptLinkSuggestion();
    int suggestionCount() const { return static_cast<int>(suggestions_.size()); }
    void scanLinkSuggestions();
    void applyTheme();
    // Font family for code (empty = system fixed-width font).
    void setMonoFamily(const QString& family);
    // Centre the text in a column of about `chars` characters (0 = full width).
    void setMaxLineChars(int chars);

Q_SIGNALS:
    void linkActivated(const QString& target, bool markdown);
    void urlActivated(const QString& url);
    void focusLost();
    void linkSuggestionHint(const QString& hint);

protected:
    void keyPressEvent(QKeyEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void focusOutEvent(QFocusEvent* e) override;
    void changeEvent(QEvent* e) override;
    void resizeEvent(QResizeEvent* e) override;
    void contextMenuEvent(QContextMenuEvent* e) override;
    bool canInsertFromMimeData(const QMimeData* source) const override;
    void insertFromMimeData(const QMimeData* source) override;

private:
    enum class Mode { None, Link, Heading, Block, Tag };
    struct LinkHit {
        QString target;
        bool markdown = false;
        bool url = false;
    };
    void updateCompletion();
    void showCompletion(const QStringList& items, const QString& prefix);
    void insertCompletion(const QString& item);
    bool continueList();
    std::optional<LinkHit> linkAt(const QTextCursor& c) const;

    VaultService* vault_ = nullptr;
    MarkdownHighlighter* highlighter_ = nullptr;
    QString notePath_;
    QCompleter* completer_ = nullptr;
    QStringListModel* model_ = nullptr;
    Mode mode_ = Mode::None;
    int anchor_ = -1;                   // document position where the completed text starts
    QString target_;                    // link target for heading/block completion
    QHash<QString, QString> itemPaths_; // completion item -> vault path (links)
    QStringList fileItems_;
    bool fileItemsStale_ = true;
    int requestId_ = 0;
    AttachmentHandler attachmentHandler_;
    struct Suggestion {
        int position; // document position
        int length;
        QString path;
        QString key;
        QString text;
    };
    void clearSuggestions();
    QString linkTextFor(const Suggestion& s) const;
    void applySuggestion(const Suggestion& s);
    QTimer* suggestTimer_ = nullptr;
    NameTable names_;
    bool namesStale_ = true;
    bool suggestLinks_ = true;
    QSet<QString> dismissed_;
    std::vector<Suggestion> suggestions_;
    QString monoFamily_;
    int maxLineChars_ = 0;
    void updateLineMargins();
};

} // namespace mn::ui
