#include "FileTreeModel.h"

#include "core/vault/Paths.h"

#include <QApplication>
#include <QPalette>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace mn::ui {

FileTreeModel::FileTreeModel(QObject* parent) : QAbstractItemModel(parent), root_(std::make_unique<Node>())
{
    root_->folder = true;
}

FileTreeModel::~FileTreeModel() = default;

FileTreeModel::Node* FileTreeModel::ensureFolder(const QString& path, QHash<QString, Node*>& folders)
{
    if (path.isEmpty()) return root_.get();
    if (Node* n = folders.value(path)) return n;
    Node* parent = ensureFolder(paths::folderOf(path), folders);
    auto node = std::make_unique<Node>();
    node->name = paths::fileName(path);
    node->path = path;
    node->folder = true;
    node->parent = parent;
    Node* raw = node.get();
    parent->children.push_back(std::move(node));
    folders.insert(path, raw);
    return raw;
}

void FileTreeModel::sortNode(Node* n)
{
    std::sort(n->children.begin(), n->children.end(), [](const auto& a, const auto& b) {
        if (a->folder != b->folder) return a->folder;
        const int c = a->name.compare(b->name, Qt::CaseInsensitive);
        return c != 0 ? c < 0 : a->name < b->name;
    });
    for (int i = 0; i < static_cast<int>(n->children.size()); ++i) {
        n->children[static_cast<std::size_t>(i)]->row = i;
        if (n->children[static_cast<std::size_t>(i)]->folder)
            sortNode(n->children[static_cast<std::size_t>(i)].get());
    }
}

void FileTreeModel::setSnapshot(const VaultSnapshot& snap, bool showAllFiles)
{
    beginResetModel();
    root_ = std::make_unique<Node>();
    root_->folder = true;
    byPath_.clear();
    QHash<QString, Node*> folders;
    for (const QString& f : snap.folders)
        ensureFolder(f, folders);
    for (const VaultFile& f : snap.files()) {
        if (f.kind == FileKind::Other && !showAllFiles) continue;
        Node* parent = ensureFolder(paths::folderOf(f.path), folders);
        auto node = std::make_unique<Node>();
        node->path = f.path;
        node->kind = f.kind;
        node->name = f.kind == FileKind::Note ? paths::linkName(f.path) : paths::fileName(f.path);
        node->parent = parent;
        byPath_.insert(f.path, node.get());
        parent->children.push_back(std::move(node));
    }
    for (auto it = folders.cbegin(); it != folders.cend(); ++it)
        byPath_.insert(it.key(), it.value());
    sortNode(root_.get());
    endResetModel();
}

FileTreeModel::Node* FileTreeModel::nodeFor(const QModelIndex& idx) const
{
    return idx.isValid() ? static_cast<Node*>(idx.internalPointer()) : root_.get();
}

QModelIndex FileTreeModel::indexForPath(const QString& path) const
{
    Node* n = byPath_.value(path);
    if (n == nullptr) return {};
    return createIndex(n->row, 0, n);
}

QModelIndex FileTreeModel::index(int row, int column, const QModelIndex& parent) const
{
    Node* p = nodeFor(parent);
    if (row < 0 || column != 0 || row >= static_cast<int>(p->children.size())) return {};
    return createIndex(row, 0, p->children[static_cast<std::size_t>(row)].get());
}

QModelIndex FileTreeModel::parent(const QModelIndex& child) const
{
    if (!child.isValid()) return {};
    Node* n = nodeFor(child);
    Node* p = n->parent;
    if (p == nullptr || p == root_.get()) return {};
    return createIndex(p->row, 0, p);
}

int FileTreeModel::rowCount(const QModelIndex& parent) const
{
    if (parent.column() > 0) return 0;
    return static_cast<int>(nodeFor(parent)->children.size());
}

int FileTreeModel::columnCount(const QModelIndex&) const
{
    return 1;
}

QVariant FileTreeModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid()) return {};
    const Node* n = nodeFor(index);
    switch (role) {
    case Qt::DisplayRole:
        return n->name;
    case Qt::ToolTipRole:
        return n->path;
    case Qt::ForegroundRole:
        // No icons (they pull in the icon theme and image plugins); attachments are dimmed instead.
        if (!n->folder && n->kind != FileKind::Note)
            return QApplication::palette().color(QPalette::PlaceholderText);
        return {};
    case PathRole:
        return n->path;
    case IsFolderRole:
        return n->folder;
    case KindRole:
        return static_cast<int>(n->kind);
    default:
        return {};
    }
}

Qt::ItemFlags FileTreeModel::flags(const QModelIndex& index) const
{
    if (!index.isValid()) return Qt::NoItemFlags;
    return Qt::ItemIsEnabled | Qt::ItemIsSelectable;
}

} // namespace mn::ui
