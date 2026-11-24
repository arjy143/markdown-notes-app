#include "AiDialogs.h"

#include "core/parse/Frontmatter.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFontDatabase>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QVBoxLayout>

using namespace Qt::StringLiterals;

namespace mn::ui {

AiSettingsDialog::AiSettingsDialog(const ai::AiConfig& c, QWidget* parent) : QDialog(parent)
{
    setWindowTitle(tr("AI assistant"));
    auto* lay = new QVBoxLayout(this);
    auto* intro =
        new QLabel(tr("Requires an OpenAI-compatible server, such as Ollama, llama.cpp or LM Studio. "
                      "Note content is sent to this server."),
                   this);
    intro->setWordWrap(true);
    lay->addWidget(intro);
    auto* form = new QFormLayout;
    url_ = new QLineEdit(c.baseUrl, this);
    url_->setPlaceholderText(u"http://192.168.1.20:8080/v1"_s);
    key_ = new QLineEdit(c.apiKey, this);
    key_->setEchoMode(QLineEdit::Password);
    key_->setPlaceholderText(tr("Optional"));
    chat_ = new QLineEdit(c.chatModel, this);
    chat_->setPlaceholderText(tr("e.g. qwen2.5:14b"));
    embed_ = new QLineEdit(c.embedModel, this);
    embed_->setPlaceholderText(tr("e.g. nomic-embed-text"));
    form->addRow(tr("Server URL:"), url_);
    form->addRow(tr("API key:"), key_);
    form->addRow(tr("Chat model:"), chat_);
    form->addRow(tr("Embedding model:"), embed_);
    embedUrl_ = new QLineEdit(c.embedUrl, this);
    embedUrl_->setPlaceholderText(tr("Optional; defaults to the server URL"));
    form->addRow(tr("Embedding server:"), embedUrl_);
    lay->addLayout(form);

    auto* adv = new QGroupBox(tr("Embedding prefixes"), this);
    auto* af = new QFormLayout(adv);
    queryPrefix_ = new QLineEdit(c.queryPrefix, adv);
    queryPrefix_->setPlaceholderText(u"search_query: "_s);
    docPrefix_ = new QLineEdit(c.documentPrefix, adv);
    docPrefix_->setPlaceholderText(u"search_document: "_s);
    af->addRow(tr("Query prefix:"), queryPrefix_);
    af->addRow(tr("Document prefix:"), docPrefix_);
    lay->addWidget(adv);

    index_ = new QCheckBox(tr("Build semantic index in the background"), this);
    index_->setChecked(c.semanticIndex);
    lay->addWidget(index_);

    auto* testRow = new QHBoxLayout;
    auto* testButton = new QPushButton(tr("Test connection"), this);
    result_ = new QLabel(this);
    result_->setWordWrap(true);
    result_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    testRow->addWidget(testButton);
    testRow->addWidget(result_, 1);
    lay->addLayout(testRow);
    connect(testButton, &QPushButton::clicked, this, &AiSettingsDialog::test);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    lay->addWidget(buttons);
    resize(560, sizeHint().height());
}

ai::AiConfig AiSettingsDialog::config() const
{
    ai::AiConfig c;
    c.baseUrl = url_->text().trimmed();
    c.apiKey = key_->text();
    c.chatModel = chat_->text().trimmed();
    c.embedModel = embed_->text().trimmed();
    c.embedUrl = embedUrl_->text().trimmed();
    c.queryPrefix = queryPrefix_->text();
    c.documentPrefix = docPrefix_->text();
    c.semanticIndex = index_->isChecked();
    return c;
}

void AiSettingsDialog::test()
{
    const ai::AiConfig c = config();
    client_.cancelAll();
    client_.setConfig(c);
    if (c.baseUrl.isEmpty() && c.embedServer().isEmpty()) {
        result_->setText(tr("Enter a server URL."));
        return;
    }
    result_->setText(tr("Testing…"));
    auto lines = std::make_shared<QStringList>();
    auto pending = std::make_shared<int>((c.chatModel.isEmpty() ? 0 : 1) + (c.embedModel.isEmpty() ? 0 : 1));
    if (*pending == 0) {
        result_->setText(tr("Enter a chat or embedding model."));
        return;
    }
    auto report = [this, lines, pending](const QString& line) {
        lines->append(line);
        if (--*pending == 0) result_->setText(lines->join(u'\n'));
    };
    if (!c.chatModel.isEmpty()) {
        client_.chat({{u"user"_s, u"Reply with the single word OK."_s}},
                     [report](const QString&, const QString& error) {
                         report(error.isEmpty() ? tr("Chat: OK") : tr("Chat: %1").arg(error));
                     });
    }
    if (!c.embedModel.isEmpty()) {
        client_.embed({u"test"_s}, [report](std::vector<std::vector<float>> v, const QString& error) {
            report(error.isEmpty()
                       ? tr("Embeddings: OK (%1 dimensions)").arg(v.empty() ? 0 : v.front().size())
                       : tr("Embeddings: %1").arg(error));
        });
    }
}

AiFrontmatterDialog::AiFrontmatterDialog(const QString& noteText, const ai::FrontmatterSuggestion& s,
                                         QWidget* parent)
    : QDialog(parent), noteText_(noteText)
{
    setWindowTitle(tr("Suggested frontmatter"));
    auto* lay = new QVBoxLayout(this);
    auto* intro = new QLabel(tr("Review before applying. Existing values are kept."), this);
    intro->setWordWrap(true);
    lay->addWidget(intro);
    const Frontmatter fm = parseFrontmatter(noteText.toUtf8());
    auto* form = new QFormLayout;
    for (const auto& [key, value] : s.properties) {
        auto* edit = new QLineEdit(value, this);
        if (const FrontmatterField* have = fm.field(key)) {
            edit->setText(have->value);
            edit->setEnabled(false);
            edit->setToolTip(tr("Already set (suggested: %1)").arg(value));
        }
        form->addRow(key + u':', edit);
        fields_.append({key, edit});
        connect(edit, &QLineEdit::textChanged, this, &AiFrontmatterDialog::updatePreview);
    }
    tags_ = new QLineEdit(s.tags.join(u", "_s), this);
    tags_->setToolTip(tr("Comma-separated"));
    form->addRow(tr("Add tags:"), tags_);
    connect(tags_, &QLineEdit::textChanged, this, &AiFrontmatterDialog::updatePreview);
    lay->addLayout(form);
    lay->addWidget(new QLabel(tr("Preview:"), this));
    preview_ = new QPlainTextEdit(this);
    preview_->setReadOnly(true);
    preview_->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    lay->addWidget(preview_, 1);
    note_ = new QLabel(this);
    note_->setWordWrap(true);
    lay->addWidget(note_);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("Apply"));
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    lay->addWidget(buttons);
    updatePreview();
    resize(560, 520);
}

FrontmatterPatch AiFrontmatterDialog::patch() const
{
    FrontmatterPatch p;
    for (const auto& [key, edit] : fields_) {
        if (edit->isEnabled() && !edit->text().trimmed().isEmpty())
            p.addIfMissing.append({key, edit->text().trimmed()});
    }
    for (const QString& t : tags_->text().split(u',', Qt::SkipEmptyParts))
        p.addTags.append(t.trimmed());
    return p;
}

void AiFrontmatterDialog::updatePreview()
{
    const FrontmatterEdit e = planFrontmatterEdit(noteText_, patch());
    if (!e.ok) {
        preview_->setPlainText(QString());
        note_->setText(e.error);
        return;
    }
    QString block = e.text;
    if (!e.changed && e.replaceLines == 0) block = tr("No changes");
    preview_->setPlainText(block);
    note_->setText(e.skipped.isEmpty() ? QString() : tr("Not added: %1").arg(e.skipped.join(u"; "_s)));
}

} // namespace mn::ui
