#include "PropertiesForm.h"

#include "TagPicker.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QShortcut>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace mn::ui {

namespace {

constexpr const char* kValueProperty = "mnValue"; // a combo's value before the current change

QPlainTextEdit* textBox(QWidget* parent, int lines, const QString& placeholder)
{
    auto* e = new QPlainTextEdit(parent);
    e->setTabChangesFocus(true);
    e->setPlaceholderText(placeholder);
    e->setFixedHeight(e->fontMetrics().lineSpacing() * lines + 12);
    return e;
}

QString comboValue(const QComboBox* box)
{
    return box->isEditable() ? box->currentText().trimmed() : box->currentData().toString();
}

void selectValue(QComboBox* box, const QString& value)
{
    if (box->isEditable()) {
        box->setEditText(value);
        return;
    }
    int at = box->findData(value);
    for (int i = 0; at < 0 && i < box->count(); ++i) {
        if (box->itemData(i).toString().compare(value, Qt::CaseInsensitive) == 0) at = i;
    }
    if (at < 0 && !value.isEmpty()) {
        box->addItem(value, value); // a value that isn't one of the choices is kept
        at = box->count() - 1;
    }
    box->setCurrentIndex(std::max(at, 0));
}

QStringList currentValues(const FrontmatterField* f)
{
    if (f == nullptr) return {};
    if (f->isList) return f->list;
    return f->value.simplified().isEmpty() ? QStringList() : QStringList{f->value.simplified()};
}

const FrontmatterField* tagField(const Frontmatter& fm)
{
    for (const char16_t* k : {u"tags", u"tag", u"Tags"}) {
        if (const FrontmatterField* f = fm.field(QStringView(k))) return f;
    }
    return nullptr;
}

bool containsTag(const QStringList& list, const QString& tag)
{
    return std::any_of(list.begin(), list.end(),
                       [&](const QString& t) { return t.compare(tag, Qt::CaseInsensitive) == 0; });
}

} // namespace

PropertiesForm::PropertiesForm(const VaultSettings& settings, const std::vector<TagCount>& vaultTags,
                               QWidget* parent)
    : QWidget(parent), categoriesInTags_(settings.categoriesInTags)
{
    auto* form = new QFormLayout(this);
    form->setContentsMargins(0, 0, 0, 0);
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    const QStringList choices = settings.categoryChoices();
    for (const PropertySpec& spec : settings.formProperties()) {
        if (fields_.contains(spec.key)) continue;
        Field f{spec, nullptr};
        if (spec.kind == PropertyKind::Tags && tags_ != nullptr) f.spec.kind = PropertyKind::List;
        switch (f.spec.kind) {
        case PropertyKind::Category:
        case PropertyKind::Choice: {
            auto* box = new QComboBox(this);
            const QStringList& options = f.spec.kind == PropertyKind::Category ? choices : f.spec.options;
            if (options.isEmpty()) {
                box->setEditable(true); // nothing to choose from: type it
            } else {
                box->addItem(tr("(none)"), QString());
                for (const QString& o : options)
                    box->addItem(o, o);
            }
            box->setProperty(kValueProperty, QString());
            if (f.spec.kind == PropertyKind::Category) {
                const qsizetype index = categories_.size();
                categories_.append(box);
                connect(box, &QComboBox::currentIndexChanged, this, [this, box, index] {
                    const QString before = box->property(kValueProperty).toString();
                    const QString now = comboValue(box);
                    box->setProperty(kValueProperty, now);
                    // A later category starts from a different one than those already picked.
                    for (qsizetype j = index + 1; !now.isEmpty() && j < categories_.size(); ++j) {
                        if (comboValue(categories_[j]) == now) categories_[j]->setCurrentIndex(0);
                    }
                    syncCategoryTag(box, before);
                });
            }
            f.widget = box;
            break;
        }
        case PropertyKind::Tags:
            tags_ = new TagPicker(this);
            tags_->setVaultTags(vaultTags);
            tags_->results()->setFixedHeight(tags_->fontMetrics().lineSpacing() * 6 + 8);
            tagsKey_ = spec.key;
            f.widget = tags_;
            break;
        case PropertyKind::Text:
            f.widget = textBox(this, 2, QString());
            break;
        case PropertyKind::List:
            f.widget = textBox(this, 2, tr("One per line"));
            break;
        case PropertyKind::Date: {
            auto* row = new QWidget(this);
            auto* h = new QHBoxLayout(row);
            h->setContentsMargins(0, 0, 0, 0);
            auto* edit = new QLineEdit(row);
            edit->setPlaceholderText(u"YYYY-MM-DD"_s);
            auto* today = new QToolButton(row);
            today->setText(tr("Today"));
            connect(today, &QToolButton::clicked, edit,
                    [edit] { edit->setText(QDate::currentDate().toString(Qt::ISODate)); });
            h->addWidget(edit, 1);
            h->addWidget(today);
            form->addRow(spec.key + u':', row);
            fields_.insert(spec.key, edit);
            f.widget = edit;
            specs_.append(f);
            continue;
        }
        }
        form->addRow(spec.key + u':', f.widget);
        fields_.insert(spec.key, f.widget);
        specs_.append(f);
    }
}

void PropertiesForm::load(const Frontmatter& fm)
{
    loading_ = true;
    for (const Field& f : std::as_const(specs_)) {
        if (f.spec.kind == PropertyKind::Tags) {
            tags_->setTags(fm.tags);
            const FrontmatterField* tf = tagField(fm);
            if (tf != nullptr && tf->complex) tags_->setEnabled(false);
            continue;
        }
        const FrontmatterField* cur = fm.field(f.spec.key);
        const bool single = f.spec.kind != PropertyKind::List;
        if (cur != nullptr && (cur->complex || (single && cur->isList))) {
            f.widget->setEnabled(false);
            f.widget->setToolTip(tr("This value can't be edited here; change it in the note."));
        }
        const QStringList values = currentValues(cur);
        if (auto* box = qobject_cast<QComboBox*>(f.widget)) {
            selectValue(box, values.value(0));
            box->setProperty(kValueProperty, comboValue(box));
        } else if (auto* text = qobject_cast<QPlainTextEdit*>(f.widget)) {
            text->setPlainText(values.join(u'\n'));
        } else if (auto* line = qobject_cast<QLineEdit*>(f.widget)) {
            line->setText(values.value(0));
        }
    }
    loading_ = false;
}

void PropertiesForm::prefillNew(const QDate& today, const QString& clipboardText)
{
    const QString clip = clipboardText.trimmed();
    const bool link = (clip.startsWith(u"http://"_s) || clip.startsWith(u"https://"_s)) &&
                      !clip.contains(u' ') && !clip.contains(u'\n');
    bool sourced = false;
    for (const Field& f : std::as_const(specs_)) {
        if (f.spec.kind == PropertyKind::Date) {
            if (auto* line = qobject_cast<QLineEdit*>(f.widget); line != nullptr && line->text().isEmpty())
                line->setText(today.toString(Qt::ISODate));
        } else if (link && !sourced && f.spec.kind == PropertyKind::List &&
                   f.spec.key.contains(u"source"_s, Qt::CaseInsensitive)) {
            if (auto* text = qobject_cast<QPlainTextEdit*>(f.widget);
                text != nullptr && text->toPlainText().isEmpty())
                text->setPlainText(clip);
            sourced = true;
        }
    }
}

void PropertiesForm::applySuggestion(const QList<QPair<QString, QString>>& properties,
                                     const QStringList& tags)
{
    for (const auto& [key, value] : properties) {
        QWidget* w = fields_.value(key);
        if (w == nullptr || !w->isEnabled() || value.trimmed().isEmpty()) continue;
        if (auto* box = qobject_cast<QComboBox*>(w)) {
            if (!comboValue(box).isEmpty()) continue;
            for (int i = 0; i < box->count(); ++i) {
                if (!box->itemData(i).toString().isEmpty() &&
                    box->itemData(i).toString().compare(value.trimmed(), Qt::CaseInsensitive) == 0)
                    box->setCurrentIndex(i);
            }
            if (box->isEditable()) box->setEditText(value.trimmed());
        } else if (auto* text = qobject_cast<QPlainTextEdit*>(w)) {
            if (text->toPlainText().trimmed().isEmpty()) text->setPlainText(value.trimmed());
        } else if (auto* line = qobject_cast<QLineEdit*>(w)) {
            if (line->text().trimmed().isEmpty()) line->setText(value.trimmed());
        }
    }
    if (tags_ != nullptr && tags_->isEnabled()) {
        for (const QString& t : tags)
            tags_->addTag(t);
    }
}

QStringList PropertiesForm::values(const Field& f) const
{
    if (f.spec.kind == PropertyKind::Tags) return tags_->tags();
    QStringList out;
    if (auto* box = qobject_cast<QComboBox*>(f.widget)) {
        out.append(comboValue(box));
    } else if (auto* text = qobject_cast<QPlainTextEdit*>(f.widget)) {
        if (f.spec.kind == PropertyKind::List)
            out = text->toPlainText().split(u'\n');
        else
            out.append(text->toPlainText());
    } else if (auto* line = qobject_cast<QLineEdit*>(f.widget)) {
        out.append(line->text());
    }
    QStringList clean;
    for (const QString& v : std::as_const(out)) {
        const QString t = v.simplified();
        if (!t.isEmpty() && !clean.contains(t)) clean.append(t);
    }
    return clean;
}

QString PropertiesForm::text(const QString& key) const
{
    for (const Field& f : specs_) {
        if (f.spec.key == key) return values(f).join(u'\n');
    }
    return {};
}

void PropertiesForm::setText(const QString& key, const QString& value)
{
    QWidget* w = fields_.value(key);
    if (auto* box = qobject_cast<QComboBox*>(w))
        selectValue(box, value);
    else if (auto* text = qobject_cast<QPlainTextEdit*>(w))
        text->setPlainText(value);
    else if (auto* line = qobject_cast<QLineEdit*>(w))
        line->setText(value);
}

FrontmatterPatch PropertiesForm::patch(const Frontmatter& current, bool writeEmpty) const
{
    FrontmatterPatch p;
    for (const Field& f : specs_) {
        if (!f.widget->isEnabled()) continue;
        const QStringList now = values(f);
        if (f.spec.kind == PropertyKind::Tags) {
            if (tagField(current) != nullptr) {
                // Existing tags are edited in place, in their own style.
                for (const QString& t : now) {
                    if (!containsTag(current.tags, t)) p.addTags.append(t);
                }
                for (const QString& t : current.tags) {
                    if (!containsTag(now, t)) p.removeTags.append(t);
                }
            } else if (!now.isEmpty() || writeEmpty) {
                p.set.append({f.spec.key, now, true, writeEmpty});
            }
            continue;
        }
        const FrontmatterField* cur = current.field(f.spec.key);
        if (now == currentValues(cur) && (cur != nullptr || !writeEmpty)) continue;
        p.set.append({f.spec.key, now, f.spec.kind == PropertyKind::List, writeEmpty});
    }
    return p;
}

bool PropertiesForm::isEmpty(const FrontmatterPatch& p)
{
    return p.set.isEmpty() && p.addTags.isEmpty() && p.removeTags.isEmpty() && p.addIfMissing.isEmpty();
}

void PropertiesForm::syncCategoryTag(QComboBox* box, const QString& before)
{
    if (!categoriesInTags_ || loading_ || tags_ == nullptr || !tags_->isEnabled()) return;
    const QString now = comboValue(box);
    if (before == now) return;
    const bool stillUsed = std::any_of(categories_.begin(), categories_.end(), [&](const QComboBox* c) {
        return c != box && comboValue(c).compare(before, Qt::CaseInsensitive) == 0;
    });
    if (!before.isEmpty() && !stillUsed) tags_->removeTag(before);
    if (!now.isEmpty()) tags_->addTag(now);
}

// --- PropertiesDialog --------------------------------------------------------------

PropertiesDialog::PropertiesDialog(const QString& noteName, const Frontmatter& fm,
                                   const VaultSettings& settings, const std::vector<TagCount>& vaultTags,
                                   Suggest suggest, QWidget* parent)
    : QDialog(parent), suggest_(std::move(suggest))
{
    setWindowTitle(tr("Properties: %1").arg(noteName));
    auto* lay = new QVBoxLayout(this);
    form_ = new PropertiesForm(settings, vaultTags, this);
    form_->load(fm);
    lay->addWidget(form_, 1);
    status_ = new QLabel(this);
    status_->setWordWrap(true);
    status_->setEnabled(false);
    lay->addWidget(status_);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, this);
    buttons->addButton(tr("Done"), QDialogButtonBox::AcceptRole)->setAutoDefault(false);
    if (suggest_) {
        suggestButton_ = buttons->addButton(tr("Suggest"), QDialogButtonBox::ActionRole);
        suggestButton_->setAutoDefault(false);
        suggestButton_->setToolTip(tr("Fill the empty fields with the assistant's suggestions"));
        connect(suggestButton_, &QPushButton::clicked, this, &PropertiesDialog::suggest);
    }
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    lay->addWidget(buttons);
    for (const char* key : {"Ctrl+Return", "Ctrl+Enter"}) {
        auto* sc = new QShortcut(QKeySequence(QString::fromLatin1(key)), this);
        connect(sc, &QShortcut::activated, this, &QDialog::accept);
    }
    if (TagPicker* tags = form_->tagPicker()) {
        connect(tags, &TagPicker::submitted, this, &QDialog::accept);
        tags->searchBox()->setFocus();
    }
    resize(520, sizeHint().height());
}

void PropertiesDialog::suggest()
{
    if (!suggest_) return;
    if (suggestButton_ != nullptr) suggestButton_->setEnabled(false);
    status_->setText(tr("Suggesting…"));
    QPointer<PropertiesDialog> self(this);
    suggest_([self](const QList<QPair<QString, QString>>& properties, const QStringList& tags,
                    const QString& error) {
        if (self.isNull()) return;
        if (self->suggestButton_ != nullptr) self->suggestButton_->setEnabled(true);
        if (!error.isEmpty()) {
            self->status_->setText(error);
            return;
        }
        self->form_->applySuggestion(properties, tags);
        self->status_->setText(tr("Empty fields filled in. Check them before saving."));
    });
}

} // namespace mn::ui
