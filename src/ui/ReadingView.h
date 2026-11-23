#pragma once

#include <QSet>
#include <QTextBrowser>

namespace mn {
class VaultService;
}

namespace mn::ui {

// Rendered view of a note (Ctrl+E): md4c -> Qt rich-text HTML -> QTextBrowser.
class ReadingView : public QTextBrowser {
    Q_OBJECT
public:
    explicit ReadingView(QWidget* parent = nullptr);

    void setVault(VaultService* vault) { vault_ = vault; }
    // Renders `raw` (the note's UTF-8 bytes) as the note at `path`.
    void showNote(const QString& path, const QByteArray& raw);
    void rerender();
    void scrollToHeading(const QString& subpath);
    void setMonoFamily(const QString& family);
    void setMaxLineChars(int chars);
    qint64 lastRenderMs() const { return lastRenderMs_; }
    const QString& notePath() const { return path_; }

Q_SIGNALS:
    void linkActivated(const QString& target, const QString& sourcePath);
    void tagActivated(const QString& tag);
    void openFileRequested(const QString& vaultPath);

protected:
    void changeEvent(QEvent* e) override;
    void resizeEvent(QResizeEvent* e) override;

private:
    void onAnchor(const QUrl& url);
    QString imageHtml(QTextDocument* doc, const QString& path, int w, int h);
    QString mathHtml(QTextDocument* doc, const QString& latex, bool display);

    VaultService* vault_ = nullptr;
    QTextDocument* ownDoc_ = nullptr;
    QString path_;
    QByteArray raw_;
    QSet<int> toggled_;
    int resourceCounter_ = 0;
    QString monoFamily_;
    int maxLineChars_ = 0;
    void updateLineMargins();
    int renderedWidth_ = 0;
    qint64 lastRenderMs_ = 0;
};

} // namespace mn::ui
