#include "SettingsDialog.h"

#include "core/index/VaultService.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QTabWidget>
#include <QTableWidget>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <iterator>

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
    alwaysUpdate_ = new QCheckBox(tr("Update links on rename without confirmation"), this);
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
    strictBreaks_ = new QCheckBox(tr("Strict line breaks"), this);
    strictBreaks_->setChecked(s.strictLineBreaks);
    form->addRow(QString(), strictBreaks_);
    captureTo_ = new QComboBox(this);
    captureTo_->addItems({tr("The inbox note"), tr("Today's daily note")});
    captureTo_->setCurrentIndex(s.captureToDailyNote ? 1 : 0);
    captureNote_ = new QLineEdit(s.captureNote, this);
    captureFormat_ = new QLineEdit(s.captureFormat, this);
    captureFormat_->setToolTip(tr("Placeholders: {{text}}, {{date}}, {{time}}, {{date:FORMAT}}"));
    dailyFolder_ = new QLineEdit(s.dailyNotesFolder, this);
    dailyFormat_ = new QLineEdit(s.dailyNoteFormat, this);
    dailyTemplate_ = new QLineEdit(s.dailyNoteTemplate, this);
    dailyTemplate_->setPlaceholderText(tr("e.g. templates/Daily.md"));
    dailyTags_ = new QLineEdit(s.dailyNoteTags.join(u", "_s), this);
    dailyTags_->setPlaceholderText(tr("e.g. journal"));
    dailyTags_->setToolTip(tr("Added to new daily notes"));
    form->addRow(tr("Quick capture goes to:"), captureTo_);
    form->addRow(tr("Inbox note:"), captureNote_);
    form->addRow(tr("Capture format:"), captureFormat_);
    form->addRow(tr("Daily notes folder:"), dailyFolder_);
    form->addRow(tr("Daily note name format:"), dailyFormat_);
    form->addRow(tr("Daily note template:"), dailyTemplate_);
    form->addRow(tr("Daily note tags:"), dailyTags_);
    tagProperties_ = new QLineEdit(s.tagProperties.join(u", "_s), this);
    tagProperties_->setPlaceholderText(tr("e.g. primary, secondary"));
    tagProperties_->setToolTip(tr("Properties whose values are treated as tags"));
    categories_ = new QLineEdit(s.categoryTags.join(u", "_s), this);
    categories_->setPlaceholderText(tr("e.g. tech, projects, research"));
    categories_->setToolTip(tr("Top-level tags, listed first in the tag pane"));
    minCategories_ = new QSpinBox(this);
    minCategories_->setRange(0, 10);
    minCategories_->setValue(s.minCategoryTags);
    minCategories_->setSpecialValueText(tr("Don't check"));
    auto* detect = new QPushButton(tr("Detect from vault"), this);
    detect->setEnabled(vault != nullptr);
    detect->setToolTip(tr("Detect properties and categories from existing frontmatter"));
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
            if (!propertiesEdited_) { // the default list starts with the tag properties
                VaultSettings d;
                d.tagProperties = scheme.properties;
                fillProperties(d.formProperties());
                propertiesReset_ = true;
            }
        });
    });
    auto* tabs = new QTabWidget(this);
    auto* general = new QWidget(tabs);
    general->setLayout(form);
    tabs->addTab(general, tr("General"));

    // Properties and tags: the note form (Ctrl+N, Ctrl+T) and the category scheme.
    auto* propsPage = new QWidget(tabs);
    auto* pl = new QVBoxLayout(propsPage);
    auto* intro =
        new QLabel(tr("The note form (Ctrl+N, Ctrl+T) offers these properties, in this order."), propsPage);
    intro->setWordWrap(true);
    pl->addWidget(intro);
    properties_ = new QTableWidget(0, 3, propsPage);
    properties_->setHorizontalHeaderLabels({tr("Property"), tr("Kind"), tr("Choices (comma-separated)")});
    properties_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    properties_->verticalHeader()->hide();
    properties_->setSelectionBehavior(QAbstractItemView::SelectRows);
    properties_->setSelectionMode(QAbstractItemView::SingleSelection);
    properties_->setMinimumHeight(properties_->verticalHeader()->defaultSectionSize() * 9);
    pl->addWidget(properties_, 1);
    auto* rowButtons = new QHBoxLayout;
    auto button = [&](const QString& text, auto fn) {
        auto* b = new QToolButton(propsPage);
        b->setText(text);
        connect(b, &QToolButton::clicked, this, fn);
        rowButtons->addWidget(b);
    };
    button(tr("Add"), [this] {
        addPropertyRow({QString(), PropertyKind::Text, {}});
        properties_->setCurrentCell(properties_->rowCount() - 1, 0);
        properties_->editItem(properties_->item(properties_->rowCount() - 1, 0));
        propertiesEdited_ = true;
    });
    button(tr("Remove"), [this] {
        if (properties_->currentRow() < 0) return;
        properties_->removeRow(properties_->currentRow());
        propertiesEdited_ = true;
    });
    auto move = [this](int delta) {
        const int row = properties_->currentRow();
        const int to = row + delta;
        if (row < 0 || to < 0 || to >= properties_->rowCount()) return;
        QList<PropertySpec> specs = tableProperties();
        specs.swapItemsAt(row, to);
        fillProperties(specs);
        properties_->setCurrentCell(to, 0);
        propertiesEdited_ = true;
    };
    button(tr("Up"), [move] { move(-1); });
    button(tr("Down"), [move] { move(1); });
    rowButtons->addStretch(1);
    button(tr("Reset to default"), [this] {
        VaultSettings d;
        d.tagProperties = settings().tagProperties;
        fillProperties(d.formProperties());
        propertiesEdited_ = false;
        propertiesReset_ = true;
    });
    pl->addLayout(rowButtons);
    connect(properties_, &QTableWidget::itemChanged, this, [this] { propertiesEdited_ = true; });

    auto* tagForm = new QFormLayout;
    categoriesInTags_ = new QCheckBox(tr("Add chosen categories to the tags too"), propsPage);
    categoriesInTags_->setChecked(s.categoriesInTags);
    writeEmpty_ = new QCheckBox(tr("Write properties left blank as empty"), propsPage);
    writeEmpty_->setChecked(s.writeEmptyProperties);
    writeEmpty_->setToolTip(tr("Every note then has the same properties, e.g. \"status:\" with no value"));
    tagForm->addRow(QString(), categoriesInTags_);
    tagForm->addRow(QString(), writeEmpty_);
    tagForm->addRow(tr("Properties that count as tags:"), tagProperties_);
    tagForm->addRow(tr("Category tags:"), categories_);
    tagForm->addRow(tr("Minimum categories per note:"), minCategories_);
    tagForm->addRow(QString(), detect);
    pl->addLayout(tagForm);
    tabs->addTab(propsPage, tr("Properties and tags"));
    lay->addWidget(tabs);
    fillProperties(s.formProperties());
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
    s.dailyNoteTags = list(dailyTags_->text());
    s.minCategoryTags = minCategories_->value();
    s.categoriesInTags = categoriesInTags_->isChecked();
    s.writeEmptyProperties = writeEmpty_->isChecked();
    if (propertiesEdited_)
        s.noteProperties = tableProperties();
    else if (propertiesReset_)
        s.noteProperties.clear();
    return s;
}

namespace {

constexpr PropertyKind kKinds[] = {PropertyKind::Category, PropertyKind::Choice, PropertyKind::Tags,
                                   PropertyKind::Text,     PropertyKind::List,   PropertyKind::Date};

} // namespace

void SettingsDialog::addPropertyRow(const PropertySpec& spec)
{
    const QSignalBlocker block(properties_);
    const int row = properties_->rowCount();
    properties_->insertRow(row);
    properties_->setItem(row, 0, new QTableWidgetItem(spec.key));
    auto* kind = new QComboBox(properties_);
    kind->addItems({tr("Category"), tr("Choice"), tr("Tags"), tr("Text"), tr("List"), tr("Date")});
    kind->setCurrentIndex(
        static_cast<int>(std::find(std::begin(kKinds), std::end(kKinds), spec.kind) - std::begin(kKinds)));
    properties_->setCellWidget(row, 1, kind);
    auto* options = new QTableWidgetItem(spec.options.join(u", "_s));
    options->setToolTip(tr("For Choice: the values to choose from. Categories use the category tags."));
    properties_->setItem(row, 2, options);
    connect(kind, &QComboBox::currentIndexChanged, this, [this] { propertiesEdited_ = true; });
}

void SettingsDialog::fillProperties(const QList<PropertySpec>& specs)
{
    const QSignalBlocker block(properties_);
    properties_->setRowCount(0);
    for (const PropertySpec& p : specs)
        addPropertyRow(p);
}

QList<PropertySpec> SettingsDialog::tableProperties() const
{
    QList<PropertySpec> out;
    for (int row = 0; row < properties_->rowCount(); ++row) {
        const QString key =
            properties_->item(row, 0) ? properties_->item(row, 0)->text().trimmed() : QString();
        if (key.isEmpty() || key.contains(u':') ||
            std::any_of(out.begin(), out.end(), [&](const PropertySpec& p) { return p.key == key; }))
            continue;
        const auto* kind = qobject_cast<const QComboBox*>(properties_->cellWidget(row, 1));
        PropertySpec p{key, kKinds[std::clamp(kind ? kind->currentIndex() : 3, 0, 5)], {}};
        const QString options = properties_->item(row, 2) ? properties_->item(row, 2)->text() : QString();
        for (const QString& o : options.split(u',', Qt::SkipEmptyParts)) {
            if (!o.trimmed().isEmpty() && !p.options.contains(o.trimmed())) p.options.append(o.trimmed());
        }
        out.append(p);
    }
    return out;
}

} // namespace mn::ui
