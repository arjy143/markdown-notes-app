#include "FileIndex.h"

#include "core/vault/Paths.h"

#include <algorithm>

namespace mn {

namespace {

QString lastSegment(QStringView path)
{
    const qsizetype slash = path.lastIndexOf(u'/');
    return path.mid(slash + 1).toString();
}

} // namespace

void FileIndex::clear()
{
    files_.clear();
    byPath_.clear();
    byName_.clear();
    dirty_ = false;
}

void FileIndex::add(const QString& path, FileKind kind)
{
    files_.push_back({path, paths::foldKey(path), kind});
    dirty_ = true;
}

void FileIndex::remove(QStringView path)
{
    const QString key = paths::foldKey(path);
    const auto it =
        std::find_if(files_.begin(), files_.end(), [&](const VaultFile& f) { return f.folded == key; });
    if (it == files_.end()) return;
    files_.erase(it);
    rebuild();
}

void FileIndex::rename(QStringView from, const QString& to)
{
    const QString key = paths::foldKey(from);
    for (VaultFile& f : files_) {
        if (f.folded == key) {
            f.path = to;
            f.folded = paths::foldKey(to);
            f.kind = classifyPath(to);
            break;
        }
    }
    rebuild();
}

void FileIndex::rebuild()
{
    std::sort(files_.begin(), files_.end(),
              [](const VaultFile& a, const VaultFile& b) { return a.path < b.path; });
    byPath_.clear();
    byName_.clear();
    byPath_.reserve(static_cast<qsizetype>(files_.size()));
    for (int i = 0; i < size(); ++i) {
        const VaultFile& f = files_[static_cast<std::size_t>(i)];
        byPath_.insert(f.folded, i);
        byName_[lastSegment(f.folded)].push_back(i);
    }
    dirty_ = false;
}

int FileIndex::find(QStringView path) const
{
    return byPath_.value(paths::foldKey(path), -1);
}

const std::vector<int>* FileIndex::filesNamed(QStringView fileName) const
{
    const auto it = byName_.constFind(paths::foldKey(fileName));
    return it == byName_.constEnd() ? nullptr : &it.value();
}

bool FileIndex::isNameAmbiguous(QStringView fileName) const
{
    const std::vector<int>* ids = filesNamed(fileName);
    return ids != nullptr && ids->size() > 1;
}

std::vector<int> FileIndex::nameCandidates(QStringView candidateFolded) const
{
    std::vector<int> out;
    const auto it = byName_.constFind(lastSegment(candidateFolded));
    if (it == byName_.constEnd()) return out;
    for (int id : it.value()) {
        const QString& p = file(id).folded;
        if (p == candidateFolded || (p.endsWith(candidateFolded) && p.size() > candidateFolded.size() &&
                                     p[p.size() - candidateFolded.size() - 1] == u'/')) {
            out.push_back(id);
        }
    }
    return out;
}

int FileIndex::pickBest(const std::vector<int>& ids, QStringView sourcePath) const
{
    if (ids.empty()) return -1;
    if (ids.size() == 1) return ids.front();
    // Step 5 (working assumption, compat spec open question 1):
    // (a) same folder as the source, (b) shortest path, (c) alphabetical.
    const QString sourceFolder = paths::foldKey(paths::folderOf(sourcePath));
    std::vector<int> sameFolder;
    for (int id : ids) {
        if (paths::foldKey(paths::folderOf(file(id).path)) == sourceFolder) sameFolder.push_back(id);
    }
    const std::vector<int>& pool = sameFolder.empty() ? ids : sameFolder;
    return *std::min_element(pool.begin(), pool.end(), [&](int a, int b) {
        const VaultFile& fa = file(a);
        const VaultFile& fb = file(b);
        if (fa.path.size() != fb.path.size()) return fa.path.size() < fb.path.size();
        if (fa.folded != fb.folded) return fa.folded < fb.folded;
        return fa.path < fb.path;
    });
}

Resolution FileIndex::resolve(QStringView linkpath, QStringView sourcePath) const
{
    if (linkpath.isEmpty()) {
        const int self = find(sourcePath);
        return {self, self >= 0 ? ResolveStep::Self : ResolveStep::None};
    }
    // Step 1: without a known extension, try "<linkpath>.md" first, then as written.
    QString candidates[2];
    int count = 0;
    if (!hasKnownExtension(linkpath)) candidates[count++] = linkpath.toString() + QStringLiteral(".md");
    candidates[count++] = linkpath.toString();

    const QString sourceFolder = paths::folderOf(sourcePath);
    for (int c = 0; c < count; ++c) {
        const QString& cand = candidates[c];
        const bool explicitRelative =
            cand.startsWith(QStringLiteral("./")) || cand.startsWith(QStringLiteral("../"));
        // Step 2: exact path from the vault root (not for explicit ./ or ../ paths).
        if (const auto norm = explicitRelative ? std::nullopt : paths::normalize(cand)) {
            const int id = find(*norm);
            if (id >= 0) return {id, ResolveStep::Exact};
        }
        // Step 3: relative to the source note's folder.
        if (const auto rel = paths::normalize(paths::join(sourceFolder, cand))) {
            const int id = find(*rel);
            if (id >= 0) return {id, ResolveStep::Relative};
        }
        // Steps 4–5: name lookup on the path suffix.
        if (cand.startsWith(u'.') || cand.contains(QStringLiteral("/."))) continue;
        QString folded = paths::foldKey(cand);
        while (folded.startsWith(u'/'))
            folded.remove(0, 1);
        const int id = pickBest(nameCandidates(folded), sourcePath);
        if (id >= 0) return {id, ResolveStep::Name};
    }
    return {};
}

QString FileIndex::linkpathFor(QStringView targetPath, QStringView sourcePath, LinkStyle style,
                               bool keepExtension) const
{
    QString target = targetPath.toString();
    const bool isNote = classifyPath(target) == FileKind::Note;
    auto strip = [&](QString p) {
        if (isNote && !keepExtension && p.endsWith(QStringLiteral(".md"), Qt::CaseInsensitive)) p.chop(3);
        return p;
    };
    switch (style) {
    case LinkStyle::Absolute:
        return strip(target);
    case LinkStyle::Relative:
        return strip(paths::relativePath(paths::folderOf(sourcePath), target));
    case LinkStyle::Shortest:
        break;
    }
    const QString name = paths::fileName(target);
    if (!isNameAmbiguous(name)) return strip(name);
    return strip(target);
}

} // namespace mn
