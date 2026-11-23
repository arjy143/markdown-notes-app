#pragma once

#include "core/index/IndexTypes.h"

#include <QAbstractItemModel>

#include <memory>
#include <vector>

namespace mn::ui {

// Folder/file tree built from a VaultSnapshot. Notes are shown without ".md";
// other file types only when "show all files" is on.
class FileTreeModel : public QAbstractItemModel {
    Q_OBJECT
public:
    enum Roles { PathRole = Qt::UserRole + 1, IsFolderRole, KindRole };

    explicit FileTreeModel(QObject* parent = nullptr);
    ~FileTreeModel() override;

    void setSnapshot(const VaultSnapshot& snap, bool showAllFiles);
    QModelIndex indexForPath(const QString& path) const;

    QModelIndex index(int row, int column, const QModelIndex& parent = {}) const override;
    QModelIndex parent(const QModelIndex& child) const override;
    int rowCount(const QModelIndex& parent = {}) const override;
    int columnCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    Qt::ItemFlags flags(const QModelIndex& index) const override;

private:
    struct Node {
        QString name;
        QString path;
        bool folder = false;
        FileKind kind = FileKind::Other;
        Node* parent = nullptr;
        int row = 0;
        std::vector<std::unique_ptr<Node>> children;
    };
    Node* nodeFor(const QModelIndex& idx) const;
    Node* ensureFolder(const QString& path, QHash<QString, Node*>& folders);
    static void sortNode(Node* n);

    std::unique_ptr<Node> root_;
    QHash<QString, Node*> byPath_;
};

} // namespace mn::ui
