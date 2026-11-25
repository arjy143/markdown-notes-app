#pragma once

#include "core/ai/AiClient.h"

#include <QDialog>

class QCheckBox;
class QLabel;
class QLineEdit;

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

} // namespace mn::ui
