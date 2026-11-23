#include "SettingsDialog.h"

#include "core/index/VaultService.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

using namespace Qt::StringLiterals;

namespace mn::ui {

SettingsDialog::SettingsDialog(const VaultSettings& s, VaultService* vault, QWidget* parent)
    : QDialog(parent), base_(s)
{
    setWindowTitle(tr("Vault settings"));
    auto* lay = new QVBoxLayout(this);
    auto* form = new QFormLayout;
    attachments_ = new QLineEdit(s.attachmentFolderPath, this);
    attachments_->setToolTip(
        tr("“/” = vault root, “./” = same folder as the note, “./sub” = subfolder of the "
           "note's folder, or a folder path"));
    linkFormat_ = new QComboBox(this);
    linkFormat_->addItems(
        {tr("Shortest path when possible"), tr("Relative path to file"), tr("Absolute path in vault")});
    linkFormat_->setCurrentIndex(static_cast<int>(s.newLinkFormat));
    markdownLinks_ = new QCheckBox(tr("Use [text](path) instead of [[wikilinks]]"), this);
    markdownLinks_->setChecked(s.useMarkdownLinks);
    newLocation_ = new QComboBox(this);
    newLocation_->addItems({tr("Vault root"), tr("Same folder as current note"), tr("In the folder below")});
    newLocation_->setCurrentIndex(static_cast<int>(s.newFileLocation));
    newFolder_ = new QLineEdit(s.newFileFolderPath, this);
    alwaysUpdate_ = new QCheckBox(tr("Don't ask before updating links (the list is still shown)"), this);
    alwaysUpdate_->setChecked(s.alwaysUpdateLinks);
    templates_ = new QLineEdit(s.templatesFolder, this);
    dateFormat_ = new QLineEdit(s.dateFormat, this);
    timeFormat_ = new QLineEdit(s.timeFormat, this);
    showAll_ = new QCheckBox(tr("Show all file types in the file tree"), this);
    showAll_->setChecked(s.showAllFiles);
    form->addRow(tr("Attachment folder:"), attachments_);
    form->addRow(tr("New link format:"), linkFormat_);
    form->addRow(QString(), markdownLinks_);
    form->addRow(tr("New notes go in:"), newLocation_);
    form->addRow(tr("New note folder:"), newFolder_);
    form->addRow(QString(), alwaysUpdate_);
    form->addRow(tr("Template folder:"), templates_);
    form->addRow(tr("Date format:"), dateFormat_);
    form->addRow(tr("Time format:"), timeFormat_);
    form->addRow(QString(), showAll_);
    strictBreaks_ = new QCheckBox(tr("Strict line breaks (reading view joins single line breaks)"), this);
    strictBreaks_->setChecked(s.strictLineBreaks);
    form->addRow(QString(), strictBreaks_);
    captureTo_ = new QComboBox(this);
    captureTo_->addItems({tr("The inbox note"), tr("Today's daily note")});
    captureTo_->setCurrentIndex(s.captureToDailyNote ? 1 : 0);
    captureNote_ = new QLineEdit(s.captureNote, this);
    captureFormat_ = new QLineEdit(s.captureFormat, this);
    captureFormat_->setToolTip(
        tr("{{text}} is the captured text; {{date}}, {{time}}, {{date:FORMAT}} work too"));
    dailyFolder_ = new QLineEdit(s.dailyNotesFolder, this);
    dailyFormat_ = new QLineEdit(s.dailyNoteFormat, this);
    dailyTemplate_ = new QLineEdit(s.dailyNoteTemplate, this);
    dailyTemplate_->setPlaceholderText(tr("e.g. templates/Daily.md"));
    form->addRow(tr("Quick capture goes to:"), captureTo_);
    form->addRow(tr("Inbox note:"), captureNote_);
    form->addRow(tr("Capture format:"), captureFormat_);
    form->addRow(tr("Daily notes folder:"), dailyFolder_);
    form->addRow(tr("Daily note name format:"), dailyFormat_);
    form->addRow(tr("Daily note template:"), dailyTemplate_);
    tagProperties_ = new QLineEdit(s.tagProperties.join(u", "_s), this);
    tagProperties_->setPlaceholderText(tr("e.g. primary, secondary"));
    tagProperties_->setToolTip(tr("Frontmatter properties whose values also count as tags"));
    categories_ = new QLineEdit(s.categoryTags.join(u", "_s), this);
    categories_->setPlaceholderText(tr("e.g. tech, projects, research"));
    categories_->setToolTip(
        tr("Your top-level tags: listed first in the tag pane and checked by Vault health"));
    minCategories_ = new QSpinBox(this);
    minCategories_->setRange(0, 10);
    minCategories_->setValue(s.minCategoryTags);
    minCategories_->setSpecialValueText(tr("Don't check"));
    auto* detect = new QPushButton(tr("Detect from vault"), this);
    detect->setEnabled(vault != nullptr);
    detect->setToolTip(tr("Suggest properties and categories from how your notes' frontmatter is used"));
    connect(detect, &QPushButton::clicked, this, [this, vault, detect] {
        detect->setEnabled(false);
        detect->setText(tr("Detecting…"));
        vault->detectTagScheme([this, detect](TagScheme scheme) {
            detect->setEnabled(true);
            detect->setText(tr("Detect from vault"));
            if (scheme.properties.isEmpty()) {
                detect->setText(tr("Nothing found"));
                return;
            }
            tagProperties_->setText(scheme.properties.join(u", "_s));
            categories_->setText(scheme.categories.join(u", "_s));
        });
    });
    form->addRow(tr("Properties that count as tags:"), tagProperties_);
    form->addRow(tr("Category tags:"), categories_);
    form->addRow(tr("Categories each note should have:"), minCategories_);
    form->addRow(QString(), detect);
    lay->addLayout(form);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    lay->addWidget(buttons);
}

VaultSettings SettingsDialog::settings() const
{
    VaultSettings s = base_;
    s.attachmentFolderPath =
        attachments_->text().trimmed().isEmpty() ? QStringLiteral("/") : attachments_->text().trimmed();
    s.newLinkFormat = static_cast<LinkStyle>(linkFormat_->currentIndex());
    s.useMarkdownLinks = markdownLinks_->isChecked();
    s.newFileLocation = static_cast<NewFileLocation>(newLocation_->currentIndex());
    s.newFileFolderPath = newFolder_->text().trimmed();
    s.alwaysUpdateLinks = alwaysUpdate_->isChecked();
    s.templatesFolder = templates_->text().trimmed();
    s.dateFormat = dateFormat_->text().trimmed().isEmpty() ? QStringLiteral("YYYY-MM-DD")
                                                           : dateFormat_->text().trimmed();
    s.timeFormat =
        timeFormat_->text().trimmed().isEmpty() ? QStringLiteral("HH:mm") : timeFormat_->text().trimmed();
    s.showAllFiles = showAll_->isChecked();
    s.strictLineBreaks = strictBreaks_->isChecked();
    s.captureToDailyNote = captureTo_->currentIndex() == 1;
    s.captureNote = captureNote_->text().trimmed().isEmpty() ? QStringLiteral("Inbox.md")
                                                             : captureNote_->text().trimmed();
    s.captureFormat =
        captureFormat_->text().isEmpty() ? QStringLiteral("- {{text}}") : captureFormat_->text();
    s.dailyNotesFolder = dailyFolder_->text().trimmed();
    s.dailyNoteFormat = dailyFormat_->text().trimmed().isEmpty() ? QStringLiteral("YYYY-MM-DD")
                                                                 : dailyFormat_->text().trimmed();
    s.dailyNoteTemplate = dailyTemplate_->text().trimmed();
    auto list = [](const QString& text) {
        QStringList out;
        for (const QString& part : text.split(u',', Qt::SkipEmptyParts)) {
            QString t = part.trimmed();
            while (t.startsWith(u'#'))
                t.remove(0, 1);
            if (!t.isEmpty() && !out.contains(t)) out.append(t);
        }
        return out;
    };
    s.tagProperties = list(tagProperties_->text());
    s.categoryTags = list(categories_->text());
    s.minCategoryTags = minCategories_->value();
    return s;
}

} // namespace mn::ui
