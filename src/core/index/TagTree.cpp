#include "TagTree.h"

#include <algorithm>
#include <map>

namespace mn {

namespace {

struct Trie {
    std::map<QString, Trie> children;
    QStringList notes;
    int count = 0;
};

void insert(Trie& t, const QStringList& path, int depth, const QString& note)
{
    ++t.count;
    if (depth >= path.size()) {
        t.notes.append(note);
        return;
    }
    insert(t.children[path[depth]], path, depth + 1, note);
}

// Moves every note of a subtree into `into` (used to flatten tiny branches).
void collect(const Trie& t, QStringList& into)
{
    into += t.notes;
    for (const auto& [key, child] : t.children)
        collect(child, into);
}

TagTreeNode convert(const QString& tag, const Trie& t, int minFolderNotes)
{
    TagTreeNode n;
    n.tag = tag;
    n.noteCount = t.count;
    n.notes = t.notes;
    for (const auto& [key, child] : t.children) {
        if (child.count < minFolderNotes)
            collect(child, n.notes);
        else
            n.children.push_back(convert(key, child, minFolderNotes));
    }
    std::sort(n.children.begin(), n.children.end(), [](const TagTreeNode& a, const TagTreeNode& b) {
        return a.noteCount != b.noteCount ? a.noteCount > b.noteCount : a.tag < b.tag;
    });
    n.notes.sort(Qt::CaseInsensitive);
    return n;
}

} // namespace

TagTreeNode buildTagTree(const QString& rootTagIn, const std::vector<NoteTags>& notes,
                         const QHash<QString, int>& tagNotes, const TagTreeOptions& options)
{
    const QString root = rootTagIn.toCaseFolded();
    const QString nestedPrefix = root + u'/';
    QHash<QString, int> categoryRank;
    for (qsizetype i = 0; i < options.categories.size(); ++i)
        categoryRank.insert(options.categories[i].toCaseFolded(), static_cast<int>(i));
    auto before = [&](const QString& a, const QString& b) {
        const auto ca = categoryRank.constFind(a);
        const auto cb = categoryRank.constFind(b);
        const bool isA = ca != categoryRank.constEnd();
        const bool isB = cb != categoryRank.constEnd();
        if (isA != isB) return isA;
        if (isA) return *ca < *cb;
        const int na = tagNotes.value(a);
        const int nb = tagNotes.value(b);
        return na != nb ? na > nb : a < b;
    };
    Trie trie;
    for (const NoteTags& n : notes) {
        bool has = false;
        QStringList others;
        for (const QString& k : n.keys) {
            if (k == root || k.startsWith(nestedPrefix))
                has = true;
            else
                others.append(k);
        }
        if (!has) continue;
        others.removeDuplicates();
        std::sort(others.begin(), others.end(), before);
        if (others.size() > options.maxDepth) others.resize(options.maxDepth);
        insert(trie, others, 0, n.path);
    }
    return convert(root, trie, std::max(1, options.minFolderNotes));
}

} // namespace mn
