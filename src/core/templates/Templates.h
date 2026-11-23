#pragma once

#include <QDateTime>
#include <QString>

// Core Templates plugin behaviour (compat spec §9).
namespace mn {

// Formats with the Moment.js subset: YYYY YY MMMM MMM MM M DD D dddd ddd HH H
// hh h mm m ss s A a Do, plus [literal text]. Unknown tokens are copied unchanged.
QString formatMoment(const QDateTime& dt, QStringView format);

struct TemplateContext {
    QString title; // current note's file name without extension
    QDateTime now;
    QString dateFormat = QStringLiteral("YYYY-MM-DD");
    QString timeFormat = QStringLiteral("HH:mm");
};

// Substitutes {{title}}, {{date}}, {{time}}, {{date:FORMAT}} and {{time:FORMAT}}.
QString substituteTemplate(QStringView text, const TemplateContext& ctx);

// How to insert a (substituted) template into a note.
struct TemplateInsertion {
    QString body; // inserted at the cursor
    // When frontmatterChanged, replace the first `replaceLength` characters of
    // the note (its current frontmatter block, possibly 0) with `frontmatter`.
    bool frontmatterChanged = false;
    int replaceLength = 0;
    QString frontmatter;
    QString warning;
};

// Merges template frontmatter into the note's (the note's values win; template
// keys the note lacks are appended). Working assumption: compat spec open question 7.
TemplateInsertion planTemplateInsertion(QStringView noteText, QStringView templateText);

} // namespace mn
