#pragma once

#include <QDialog>

class QTreeWidget;
class QLabel;
class QPushButton;

namespace mn {
class VaultService;
struct HealthReport;
} // namespace mn

namespace mn::ui {

// Tools > Vault health: broken links, orphan and empty notes, unused
// attachments, tags that look alike and near-duplicate notes. Read-only;
// activating an entry opens it.
class HealthDialog : public QDialog {
    Q_OBJECT
public:
    explicit HealthDialog(VaultService* vault, QWidget* parent = nullptr);
    void run();
    int sectionCount(int section) const; // for tests: items in a section (0..5)

Q_SIGNALS:
    void openRequested(const QString& path, int line, const QString& findText);
    void tagRequested(const QString& tag);

private:
    void display(const HealthReport& r);
    VaultService* vault_;
    QTreeWidget* tree_;
    QLabel* status_;
    QPushButton* refresh_;
    int request_ = 0;
};

} // namespace mn::ui
