#pragma once

#include <QByteArrayView>
#include <QList>
#include <QString>
#include <QStringList>

// Frontmatter detection and a small hand-written YAML subset (compat spec §6):
// scalars, quoted strings, block lists and flow lists. Anything more complex
// (nested maps, block scalars) is tolerated and skipped. Invalid YAML is
// reported, never "fixed".
namespace mn {

struct FrontmatterField {
    QString key;
    QString value;    // scalar value, or the list joined with ", "
    QStringList list; // list items when isList
    bool isList = false;
    bool complex = false; // nested map / block scalar, not interpreted
};

struct Frontmatter {
    bool present = false;     // first line is "---" and a closing "---" line exists
    bool valid = true;        // YAML parsed with the supported subset
    QString error;            // reason when !valid
    int errorLine = -1;       // 0-based line in the file
    int closingLine = -1;     // 0-based line of the closing "---"
    qsizetype bodyOffset = 0; // byte offset of the first body byte
    QList<FrontmatterField> fields;
    QStringList tags;    // from tags / tag / Tags, without '#'
    QStringList aliases; // from aliases / alias
    QStringList cssclasses;

    const FrontmatterField* field(QStringView key) const;
};

// `data` is the file content without a BOM, in any line-ending style.
Frontmatter parseFrontmatter(QByteArrayView data);

// Splits a frontmatter tag string ("a, b c") into tags without '#'.
QStringList splitTagString(QStringView s);

} // namespace mn
