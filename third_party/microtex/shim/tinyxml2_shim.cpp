// See tinyxml2.h: a read-only subset of the tinyxml2 API over QXmlStreamReader.
#include "tinyxml2.h"

#include <QFile>
#include <QString>
#include <QXmlStreamReader>

#include <cstring>

namespace tinyxml2 {

namespace {
bool nameMatches(const XMLElement* e, const char* name)
{
    return name == nullptr || std::strcmp(e->Name(), name) == 0;
}
}  // namespace

const XMLElement* XMLElement::FirstChildElement(const char* name) const
{
    for (const XMLElement* c : children_) {
        if (nameMatches(c, name)) return c;
    }
    return nullptr;
}

XMLElement* XMLElement::FirstChildElement(const char* name)
{
    for (XMLElement* c : children_) {
        if (nameMatches(c, name)) return c;
    }
    return nullptr;
}

const XMLElement* XMLElement::NextSiblingElement(const char* name) const
{
    if (parent_ == nullptr) return nullptr;
    const auto& sib = parent_->children_;
    for (std::size_t i = indexInParent_ + 1; i < sib.size(); ++i) {
        if (nameMatches(sib[i], name)) return sib[i];
    }
    return nullptr;
}

const XMLAttribute* XMLElement::FindAttribute(const char* name) const
{
    for (const XMLAttribute& a : attrs_) {
        if (std::strcmp(a.Name(), name) == 0) return &a;
    }
    return nullptr;
}

const char* XMLElement::Attribute(const char* name, const char* value) const
{
    const XMLAttribute* a = FindAttribute(name);
    if (a == nullptr) return nullptr;
    if (value != nullptr && std::strcmp(a->Value(), value) != 0) return nullptr;
    return a->Value();
}

XMLError XMLElement::QueryIntAttribute(const char* name, int* value) const
{
    const XMLAttribute* a = FindAttribute(name);
    if (a == nullptr) return XML_NO_ATTRIBUTE;
    bool ok = false;
    const QString s = QString::fromUtf8(a->Value()).trimmed();
    int v = s.toInt(&ok, 10);
    if (!ok) v = s.toInt(&ok, 0);  // tinyxml2 also accepts 0x-prefixed hex
    if (!ok) return XML_WRONG_ATTRIBUTE_TYPE;
    *value = v;
    return XML_SUCCESS;
}

XMLError XMLElement::QueryFloatAttribute(const char* name, float* value) const
{
    const XMLAttribute* a = FindAttribute(name);
    if (a == nullptr) return XML_NO_ATTRIBUTE;
    bool ok = false;
    const float v = QString::fromUtf8(a->Value()).trimmed().toFloat(&ok);
    if (!ok) return XML_WRONG_ATTRIBUTE_TYPE;
    *value = v;
    return XML_SUCCESS;
}

XMLError XMLElement::QueryBoolAttribute(const char* name, bool* value) const
{
    const XMLAttribute* a = FindAttribute(name);
    if (a == nullptr) return XML_NO_ATTRIBUTE;
    const QString s = QString::fromUtf8(a->Value()).trimmed().toLower();
    if (s == u"true" || s == u"1") {
        *value = true;
    } else if (s == u"false" || s == u"0") {
        *value = false;
    } else {
        return XML_WRONG_ATTRIBUTE_TYPE;
    }
    return XML_SUCCESS;
}

XMLDocument::XMLDocument(bool /*processEntities*/, Whitespace /*ws*/) {}
XMLDocument::~XMLDocument() = default;

XMLError XMLDocument::LoadFile(const char* path)
{
    storage_.clear();
    root_ = nullptr;
    QFile f(QString::fromUtf8(path));
    if (!f.open(QIODevice::ReadOnly)) return XML_ERROR_FILE_NOT_FOUND;
    QXmlStreamReader xml(&f);
    XMLElement* current = nullptr;
    while (!xml.atEnd()) {
        const auto tok = xml.readNext();
        if (tok == QXmlStreamReader::StartElement) {
            XMLElement& e = storage_.emplace_back();
            e.name_ = xml.name().toString().toStdString();
            XMLAttribute* prev = nullptr;
            for (const QXmlStreamAttribute& qa : xml.attributes()) {
                XMLAttribute& a = e.attrs_.emplace_back();
                a.name_ = qa.name().toString().toStdString();
                a.value_ = qa.value().toString().toStdString();
                if (prev != nullptr) prev->next_ = &a;
                prev = &a;
            }
            e.parent_ = current;
            if (current != nullptr) {
                e.indexInParent_ = current->children_.size();
                current->children_.push_back(&e);
            } else if (root_ == nullptr) {
                root_ = &e;
            }
            current = &e;
        } else if (tok == QXmlStreamReader::EndElement) {
            if (current != nullptr) current = current->parent_;
        }
    }
    if (xml.hasError() || root_ == nullptr) {
        storage_.clear();
        root_ = nullptr;
        return XML_ERROR_PARSING;
    }
    return XML_SUCCESS;
}

}  // namespace tinyxml2
