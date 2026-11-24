#pragma once

#include "core/ai/AiClient.h"
#include "core/ai/FrontmatterMerge.h"
#include "core/ai/Prompts.h"

#include <QDialog>

class QCheckBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;

namespace mn::ui {

// Tools › AI assistant settings: the local server and models.
class AiSettingsDialog : public QDialog {
    Q_OBJECT
public:
    explicit AiSettingsDialog(const ai::AiConfig& config, QWidget* parent = nullptr);
    ai::AiConfig config() const;

private:
    void test();
    QLineEdit* url_;
    QLineEdit* key_;
    QLineEdit* chat_;
    QLineEdit* embed_;
    QLineEdit* embedUrl_;
    QLineEdit* queryPrefix_;
    QLineEdit* docPrefix_;
    QCheckBox* index_;
    QLabel* result_;
    ai::AiClient client_;
};

// Shows the model's suggested frontmatter for review: every value can be edited
// or cleared, and the preview shows exactly what the note's frontmatter becomes.
class AiFrontmatterDialog : public QDialog {
    Q_OBJECT
public:
    AiFrontmatterDialog(const QString& noteText, const ai::FrontmatterSuggestion& suggestion,
                        QWidget* parent = nullptr);
    FrontmatterPatch patch() const;

private:
    void updatePreview();
    QString noteText_;
    QList<QPair<QString, QLineEdit*>> fields_;
    QLineEdit* tags_;
    QPlainTextEdit* preview_;
    QLabel* note_;
};

} // namespace mn::ui
