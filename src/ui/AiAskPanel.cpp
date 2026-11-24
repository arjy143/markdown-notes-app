#include "AiAskPanel.h"

#include "core/ai/Assistant.h"
#include "core/vault/Paths.h"

#include <QDateTime>
#include <QElapsedTimer>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QSplitter>
#include <QTextBrowser>
#include <QTreeWidget>
#include <QVBoxLayout>

using namespace Qt::StringLiterals;

namespace mn::ui {

namespace {
constexpr int kPathRole = Qt::UserRole + 1;
constexpr int kLineRole = Qt::UserRole + 2;
} // namespace

AiAskPanel::AiAskPanel(ai::Assistant* assistant, QWidget* parent) : QWidget(parent), assistant_(assistant)
{
    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(4, 4, 4, 4);
    auto* row = new QHBoxLayout;
    question_ = new QLineEdit(this);
    question_->setPlaceholderText(tr("Ask about your notes…"));
    question_->setClearButtonEnabled(true);
    askButton_ = new QPushButton(tr("Ask"), this);
    row->addWidget(question_, 1);
    row->addWidget(askButton_);
    lay->addLayout(row);
    status_ = new QLabel(this);
    status_->setEnabled(false);
    status_->setWordWrap(true);
    lay->addWidget(status_);
    auto* split = new QSplitter(Qt::Vertical, this);
    answer_ = new QTextBrowser(split);
    answer_->setOpenLinks(false);
    sources_ = new QTreeWidget(split);
    sources_->setHeaderHidden(true);
    sources_->setRootIsDecorated(false);
    sources_->setWordWrap(true);
    split->setStretchFactor(0, 3);
    split->setStretchFactor(1, 2);
    lay->addWidget(split, 1);

    connect(question_, &QLineEdit::returnPressed, this, &AiAskPanel::ask);
    connect(askButton_, &QPushButton::clicked, this, [this] {
        if (askButton_->text() == tr("Stop"))
            stop();
        else
            ask();
    });
    auto open = [this](QTreeWidgetItem* it) {
        Q_EMIT openRequested(it->data(0, kPathRole).toString(), it->data(0, kLineRole).toInt(), QString());
    };
    connect(sources_, &QTreeWidget::itemClicked, this, open);
    connect(sources_, &QTreeWidget::itemActivated, this, open);
    // [n] in the answer opens source n.
    connect(answer_, &QTextBrowser::anchorClicked, this, [this](const QUrl& url) {
        const int n = url.toString().section(u':', 1).toInt();
        if (n >= 1 && n <= static_cast<int>(shown_.size())) {
            const Passage& p = shown_[static_cast<std::size_t>(n - 1)];
            Q_EMIT openRequested(p.path, p.startLine, QString());
        }
    });
}

void AiAskPanel::focusQuestion()
{
    question_->setFocus();
    question_->selectAll();
}

void AiAskPanel::ask()
{
    const QString q = question_->text().trimmed();
    if (q.isEmpty()) return;
    const int req = ++request_;
    answer_->clear();
    sources_->clear();
    askButton_->setText(tr("Stop"));
    status_->setText(tr("Generating answer…"));
    auto timer = std::make_shared<QElapsedTimer>();
    timer->start();
    assistant_->ask(q, [this, req, timer](ai::Assistant::Answer a) {
        if (req != request_) return;
        askButton_->setText(tr("Ask"));
        if (!a.error.isEmpty()) {
            status_->setText(a.error);
            return;
        }
        status_->setText(tr("%n source(s) · %1 s", nullptr, static_cast<int>(a.sources.size()))
                             .arg(static_cast<double>(timer->elapsed()) / 1000.0, 0, 'f', 1));
        shown_ = std::move(a.sources);
        // Citations [n] become links to their passage.
        QString md = a.text;
        static const QRegularExpression cite(uR"(\[(\d{1,2})\](?!\())"_s);
        md.replace(cite, uR"([[\1]](source:\1))"_s);
        answer_->setMarkdown(md);
        for (std::size_t i = 0; i < shown_.size(); ++i) {
            const Passage& p = shown_[i];
            auto* it = new QTreeWidgetItem(sources_);
            QString label = u"[%1] %2 · %3"_s.arg(i + 1).arg(
                paths::linkName(p.path),
                QDateTime::fromMSecsSinceEpoch(p.dateMs).date().toString(Qt::ISODate));
            if (!p.context.isEmpty()) label += u" · "_s + p.context;
            it->setText(0, label);
            it->setToolTip(0, p.text.left(1500));
            it->setData(0, kPathRole, p.path);
            it->setData(0, kLineRole, p.startLine);
        }
    });
}

void AiAskPanel::stop()
{
    ++request_;
    assistant_->cancel();
    askButton_->setText(tr("Ask"));
    status_->setText(tr("Cancelled."));
}

} // namespace mn::ui
