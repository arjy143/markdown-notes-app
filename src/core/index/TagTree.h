#pragma once

#include "IndexTypes.h"

#include <QHash>
#include <QString>
#include <QStringList>

#include <vector>

// The tag drill-down: the notes under one tag, split into a folder-like tree by
// their other tags. Each note appears exactly once, at the path formed by its
// other tags in a fixed order (category tags first, then the most used tags), so
// broad topics form the top levels and specific ones the leaves.
namespace mn {

struct TagTreeNode {
    QString tag;                       // tag key ("" for the root)
    int noteCount = 0;                 // notes in this subtree
    std::vector<TagTreeNode> children; // sub-tags, most notes first
    QStringList notes;                 // notes filed directly here, alphabetical
};

struct TagTreeOptions {
    QStringList categories; // category tag keys, in their order of importance
    int maxDepth = 4;       // levels of sub-tags below the root
    int minFolderNotes = 2; // smaller branches are flattened into their parent
};

// `rootTag` matches the tag itself and its nested tags ("a" covers "a/b").
// `tagNotes` = notes per tag key (for ordering); missing tags count as 0.
TagTreeNode buildTagTree(const QString& rootTag, const std::vector<NoteTags>& notes,
                         const QHash<QString, int>& tagNotes, const TagTreeOptions& options = {});

} // namespace mn
