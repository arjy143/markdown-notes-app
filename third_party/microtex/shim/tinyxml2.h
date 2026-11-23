// Minimal tinyxml2-compatible API used by MicroTeX's resource parsers.
//
// Markdown Notes does not vendor tinyxml2. MicroTeX only needs a small, read-only
// subset of its API to load the Cyrillic/Greek alphabet descriptions, so this
// shim implements that subset on top of QXmlStreamReader (part of QtCore).
// Only the members MicroTeX calls are provided.
#pragma once

#include <cstdio>
#include <cstring>
#include <deque>
#include <memory>
#include <string>
#include <vector>

namespace tinyxml2 {

enum XMLError { XML_SUCCESS = 0, XML_ERROR_FILE_NOT_FOUND, XML_ERROR_PARSING, XML_NO_ATTRIBUTE, XML_WRONG_ATTRIBUTE_TYPE };
enum Whitespace { PRESERVE_WHITESPACE, COLLAPSE_WHITESPACE };

class XMLAttribute {
public:
    const char* Name() const { return name_.c_str(); }
    const char* Value() const { return value_.c_str(); }
    const XMLAttribute* Next() const { return next_; }

private:
    friend class XMLDocument;
    std::string name_;
    std::string value_;
    const XMLAttribute* next_ = nullptr;
};

class XMLElement {
public:
    const char* Name() const { return name_.c_str(); }

    const XMLElement* FirstChildElement(const char* name = nullptr) const;
    XMLElement* FirstChildElement(const char* name = nullptr);
    const XMLElement* NextSiblingElement(const char* name = nullptr) const;

    const XMLAttribute* FirstAttribute() const { return attrs_.empty() ? nullptr : &attrs_.front(); }
    const XMLAttribute* FindAttribute(const char* name) const;
    const char* Attribute(const char* name, const char* value = nullptr) const;

    XMLError QueryIntAttribute(const char* name, int* value) const;
    XMLError QueryFloatAttribute(const char* name, float* value) const;
    XMLError QueryBoolAttribute(const char* name, bool* value) const;
    XMLError QueryAttribute(const char* name, int* value) const { return QueryIntAttribute(name, value); }
    XMLError QueryAttribute(const char* name, float* value) const { return QueryFloatAttribute(name, value); }
    XMLError QueryAttribute(const char* name, bool* value) const { return QueryBoolAttribute(name, value); }

private:
    friend class XMLDocument;
    std::string name_;
    std::deque<XMLAttribute> attrs_;
    std::vector<XMLElement*> children_;
    XMLElement* parent_ = nullptr;
    std::size_t indexInParent_ = 0;
};

class XMLDocument {
public:
    explicit XMLDocument(bool processEntities = true, Whitespace ws = PRESERVE_WHITESPACE);
    ~XMLDocument();
    XMLDocument(const XMLDocument&) = delete;
    XMLDocument& operator=(const XMLDocument&) = delete;

    XMLError LoadFile(const char* path);
    XMLElement* RootElement() { return root_; }
    const XMLElement* RootElement() const { return root_; }

private:
    std::deque<XMLElement> storage_;
    XMLElement* root_ = nullptr;
};

}  // namespace tinyxml2
