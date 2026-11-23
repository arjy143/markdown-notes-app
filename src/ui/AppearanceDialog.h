#pragma once

#include "Appearance.h"

#include <QDialog>

class QComboBox;
class QSpinBox;
class QCheckBox;
class QPushButton;

namespace mn::ui {

// View > Appearance: theme, accent colour, fonts and line length. Changes are
// previewed live through `changed`; Cancel restores the original settings.
class AppearanceDialog : public QDialog {
    Q_OBJECT
public:
    explicit AppearanceDialog(const Appearance& current, QWidget* parent = nullptr);
    Appearance appearance() const;

Q_SIGNALS:
    void changed(const mn::ui::Appearance& a);

private:
    void setFrom(const Appearance& a);
    void emitChanged();
    void updateAccentButton();

    QComboBox* theme_;
    QPushButton* accentButton_;
    QPushButton* accentReset_;
    QColor accent_;
    QComboBox* uiFamily_;
    QSpinBox* uiSize_;
    QComboBox* editorFamily_;
    QSpinBox* editorSize_;
    QComboBox* monoFamily_;
    QComboBox* readingFamily_;
    QSpinBox* readingSize_;
    QCheckBox* readable_;
    QSpinBox* lineChars_;
    bool updating_ = false;
};

} // namespace mn::ui
