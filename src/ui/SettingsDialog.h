#pragma once

#include "core/settings/VaultSettings.h"

#include <QDialog>

class QLineEdit;
class QComboBox;
class QCheckBox;
class QSpinBox;

namespace mn {
class VaultService;
}

namespace mn::ui {

// Per-vault settings (stored in <vault>/.markdown-notes/settings.json).
class SettingsDialog : public QDialog {
    Q_OBJECT
public:
    // `vault` (optional) enables "Detect from vault" for the tag settings.
    SettingsDialog(const VaultSettings& s, VaultService* vault = nullptr, QWidget* parent = nullptr);
    VaultSettings settings() const;

private:
    VaultSettings base_;
    QLineEdit* attachments_;
    QComboBox* linkFormat_;
    QCheckBox* markdownLinks_;
    QComboBox* newLocation_;
    QLineEdit* newFolder_;
    QCheckBox* alwaysUpdate_;
    QLineEdit* templates_;
    QLineEdit* dateFormat_;
    QLineEdit* timeFormat_;
    QCheckBox* showAll_;
    QCheckBox* strictBreaks_;
    QComboBox* captureTo_;
    QLineEdit* captureNote_;
    QLineEdit* captureFormat_;
    QLineEdit* dailyFolder_;
    QLineEdit* dailyFormat_;
    QLineEdit* dailyTemplate_;
    QLineEdit* dailyTags_;
    QLineEdit* tagProperties_;
    QLineEdit* categories_;
    QSpinBox* minCategories_;
};

} // namespace mn::ui
