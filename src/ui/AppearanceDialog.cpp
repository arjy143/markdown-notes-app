#include "AppearanceDialog.h"

#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QCompleter>
#include <QDialogButtonBox>
#include <QFontDatabase>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

using namespace Qt::StringLiterals;

namespace mn::ui {

namespace {

QComboBox* familyCombo(const QStringList& families, QWidget* parent)
{
    auto* c = new QComboBox(parent);
    c->addItem(QObject::tr("Default"), QString());
    for (const QString& f : families)
        c->addItem(f, f);
    c->setEditable(true);
    c->setInsertPolicy(QComboBox::NoInsert);
    c->completer()->setCompletionMode(QCompleter::PopupCompletion);
    c->completer()->setFilterMode(Qt::MatchContains);
    c->completer()->setCaseSensitivity(Qt::CaseInsensitive);
    return c;
}

QSpinBox* sizeSpin(QWidget* parent)
{
    auto* s = new QSpinBox(parent);
    s->setRange(5, 48);
    s->setSpecialValueText(QObject::tr("Default")); // the minimum (5) means "default"
    s->setSuffix(u" pt"_s);
    return s;
}

QString familyOf(const QComboBox* c)
{
    const int idx = c->findText(c->currentText(), Qt::MatchFixedString);
    if (idx < 0) return {}; // unknown family typed: treat as default
    return c->itemData(idx).toString();
}

void selectFamily(QComboBox* c, const QString& family)
{
    const int idx = family.isEmpty() ? 0 : c->findData(family);
    c->setCurrentIndex(idx < 0 ? 0 : idx);
}

int sizeOf(const QSpinBox* s)
{
    return s->value() == s->minimum() ? 0 : s->value();
}

} // namespace

AppearanceDialog::AppearanceDialog(const Appearance& current, QWidget* parent) : QDialog(parent)
{
    setWindowTitle(tr("Appearance"));
    const QStringList families = QFontDatabase::families();
    QStringList monoFamilies;
    for (const QString& f : families) {
        if (QFontDatabase::isFixedPitch(f)) monoFamilies.append(f);
    }

    auto* lay = new QVBoxLayout(this);
    auto* form = new QFormLayout;
    theme_ = new QComboBox(this);
    theme_->addItems({tr("System"), tr("Light"), tr("Dark")});
    form->addRow(tr("Theme:"), theme_);

    auto* accentRow = new QHBoxLayout;
    accentButton_ = new QPushButton(this);
    accentReset_ = new QPushButton(tr("Default"), this);
    accentRow->addWidget(accentButton_, 1);
    accentRow->addWidget(accentReset_);
    form->addRow(tr("Accent colour:"), accentRow);

    auto addFont = [&](const QString& label, QComboBox*& family, QSpinBox** size, const QStringList& list) {
        auto* row = new QHBoxLayout;
        family = familyCombo(list, this);
        row->addWidget(family, 1);
        if (size != nullptr) {
            *size = sizeSpin(this);
            row->addWidget(*size);
        }
        form->addRow(label, row);
    };
    addFont(tr("Interface font:"), uiFamily_, &uiSize_, families);
    addFont(tr("Editor font:"), editorFamily_, &editorSize_, families);
    addFont(tr("Code font:"), monoFamily_, nullptr, monoFamilies);
    addFont(tr("Reading view font:"), readingFamily_, &readingSize_, families);

    auto* lineRow = new QHBoxLayout;
    readable_ = new QCheckBox(tr("Readable line length"), this);
    lineChars_ = new QSpinBox(this);
    lineChars_->setRange(40, 200);
    lineChars_->setSuffix(tr(" characters"));
    lineRow->addWidget(readable_);
    lineRow->addWidget(lineChars_);
    lineRow->addStretch(1);
    form->addRow(tr("Text width:"), lineRow);
    lay->addLayout(form);

    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel | QDialogButtonBox::RestoreDefaults, this);
    lay->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(buttons->button(QDialogButtonBox::RestoreDefaults), &QPushButton::clicked, this, [this] {
        setFrom(Appearance{});
        emitChanged();
    });

    setFrom(current);

    connect(theme_, &QComboBox::currentIndexChanged, this, &AppearanceDialog::emitChanged);
    connect(accentButton_, &QPushButton::clicked, this, [this] {
        const QColor c = QColorDialog::getColor(
            accent_.isValid() ? accent_ : palette().color(QPalette::Highlight), this, tr("Accent colour"));
        if (!c.isValid()) return;
        accent_ = c;
        updateAccentButton();
        emitChanged();
    });
    connect(accentReset_, &QPushButton::clicked, this, [this] {
        accent_ = QColor();
        updateAccentButton();
        emitChanged();
    });
    for (QComboBox* c : {uiFamily_, editorFamily_, monoFamily_, readingFamily_})
        connect(c, &QComboBox::currentIndexChanged, this, &AppearanceDialog::emitChanged);
    for (QSpinBox* s : {uiSize_, editorSize_, readingSize_, lineChars_})
        connect(s, &QSpinBox::valueChanged, this, &AppearanceDialog::emitChanged);
    connect(readable_, &QCheckBox::toggled, this, [this](bool on) {
        lineChars_->setEnabled(on);
        emitChanged();
    });
}

void AppearanceDialog::setFrom(const Appearance& a)
{
    updating_ = true;
    theme_->setCurrentIndex(static_cast<int>(a.theme));
    accent_ = a.accent;
    updateAccentButton();
    selectFamily(uiFamily_, a.uiFamily);
    uiSize_->setValue(a.uiSize > 0 ? a.uiSize : uiSize_->minimum());
    selectFamily(editorFamily_, a.editorFamily);
    editorSize_->setValue(a.editorSize > 0 ? a.editorSize : editorSize_->minimum());
    selectFamily(monoFamily_, a.monoFamily);
    selectFamily(readingFamily_, a.readingFamily);
    readingSize_->setValue(a.readingSize > 0 ? a.readingSize : readingSize_->minimum());
    readable_->setChecked(a.readableLineLength);
    lineChars_->setValue(a.lineLengthChars);
    lineChars_->setEnabled(a.readableLineLength);
    updating_ = false;
}

void AppearanceDialog::updateAccentButton()
{
    if (accent_.isValid()) {
        accentButton_->setText(accent_.name());
        QPixmap swatch(16, 16);
        swatch.fill(accent_);
        accentButton_->setIcon(QIcon(swatch));
    } else {
        accentButton_->setText(tr("Theme default"));
        accentButton_->setIcon(QIcon());
    }
    accentReset_->setEnabled(accent_.isValid());
}

Appearance AppearanceDialog::appearance() const
{
    Appearance a;
    a.theme = static_cast<Appearance::ThemeMode>(theme_->currentIndex());
    a.accent = accent_;
    a.uiFamily = familyOf(uiFamily_);
    a.uiSize = sizeOf(uiSize_);
    a.editorFamily = familyOf(editorFamily_);
    a.editorSize = sizeOf(editorSize_);
    a.monoFamily = familyOf(monoFamily_);
    a.readingFamily = familyOf(readingFamily_);
    a.readingSize = sizeOf(readingSize_);
    a.readableLineLength = readable_->isChecked();
    a.lineLengthChars = lineChars_->value();
    return a;
}

void AppearanceDialog::emitChanged()
{
    if (updating_) return;
    Q_EMIT changed(appearance());
}

} // namespace mn::ui
