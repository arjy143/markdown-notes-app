#include "VaultSettings.h"

#include "core/vault/Paths.h"
#include "core/vault/TextFile.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

using namespace Qt::StringLiterals;

namespace mn {

namespace {

QJsonObject readJsonObject(const QString& path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return {};
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    return doc.isObject() ? doc.object() : QJsonObject();
}

QString styleName(LinkStyle s)
{
    switch (s) {
    case LinkStyle::Relative:
        return u"relative"_s;
    case LinkStyle::Absolute:
        return u"absolute"_s;
    case LinkStyle::Shortest:
        break;
    }
    return u"shortest"_s;
}

LinkStyle parseStyle(const QString& s)
{
    if (s == u"relative") return LinkStyle::Relative;
    if (s == u"absolute") return LinkStyle::Absolute;
    return LinkStyle::Shortest;
}

QString locationName(NewFileLocation l)
{
    switch (l) {
    case NewFileLocation::Current:
        return u"current"_s;
    case NewFileLocation::Folder:
        return u"folder"_s;
    case NewFileLocation::Root:
        break;
    }
    return u"root"_s;
}

NewFileLocation parseLocation(const QString& s)
{
    if (s == u"current") return NewFileLocation::Current;
    if (s == u"folder") return NewFileLocation::Folder;
    return NewFileLocation::Root;
}

QString cleanFolder(QString f)
{
    f.replace(u'\\', u'/');
    while (f.startsWith(u'/'))
        f.remove(0, 1);
    while (f.endsWith(u'/'))
        f.chop(1);
    return paths::normalize(f).value_or(QString());
}

void applyJson(VaultSettings& s, const QJsonObject& o)
{
    if (o.contains(u"attachmentFolderPath"_s))
        s.attachmentFolderPath = o.value(u"attachmentFolderPath"_s).toString(s.attachmentFolderPath);
    if (o.contains(u"newLinkFormat"_s)) s.newLinkFormat = parseStyle(o.value(u"newLinkFormat"_s).toString());
    if (o.contains(u"useMarkdownLinks"_s)) s.useMarkdownLinks = o.value(u"useMarkdownLinks"_s).toBool();
    if (o.contains(u"newFileLocation"_s))
        s.newFileLocation = parseLocation(o.value(u"newFileLocation"_s).toString());
    if (o.contains(u"newFileFolderPath"_s)) s.newFileFolderPath = o.value(u"newFileFolderPath"_s).toString();
    if (o.contains(u"alwaysUpdateLinks"_s)) s.alwaysUpdateLinks = o.value(u"alwaysUpdateLinks"_s).toBool();
    if (o.contains(u"showAllFiles"_s)) s.showAllFiles = o.value(u"showAllFiles"_s).toBool();
    if (o.contains(u"strictLineBreaks"_s)) s.strictLineBreaks = o.value(u"strictLineBreaks"_s).toBool();
    if (o.contains(u"captureToDailyNote"_s)) s.captureToDailyNote = o.value(u"captureToDailyNote"_s).toBool();
    if (o.contains(u"captureNote"_s)) s.captureNote = o.value(u"captureNote"_s).toString(s.captureNote);
    if (o.contains(u"captureFormat"_s))
        s.captureFormat = o.value(u"captureFormat"_s).toString(s.captureFormat);
    if (o.contains(u"dailyNotesFolder"_s)) s.dailyNotesFolder = o.value(u"dailyNotesFolder"_s).toString();
    if (o.contains(u"dailyNoteFormat"_s))
        s.dailyNoteFormat = o.value(u"dailyNoteFormat"_s).toString(s.dailyNoteFormat);
    if (o.contains(u"dailyNoteTemplate"_s)) s.dailyNoteTemplate = o.value(u"dailyNoteTemplate"_s).toString();
    auto stringList = [&](const QString& key) {
        QStringList out;
        for (const QJsonValue& v : o.value(key).toArray()) {
            const QString t = v.toString().trimmed();
            if (!t.isEmpty()) out.append(t);
        }
        return out;
    };
    if (o.contains(u"tagProperties"_s)) s.tagProperties = stringList(u"tagProperties"_s);
    if (o.contains(u"categoryTags"_s)) s.categoryTags = stringList(u"categoryTags"_s);
    if (o.contains(u"minCategoryTags"_s))
        s.minCategoryTags = std::clamp(o.value(u"minCategoryTags"_s).toInt(2), 0, 20);
    if (o.contains(u"templatesFolder"_s)) s.templatesFolder = o.value(u"templatesFolder"_s).toString();
    if (o.contains(u"dateFormat"_s)) s.dateFormat = o.value(u"dateFormat"_s).toString(s.dateFormat);
    if (o.contains(u"timeFormat"_s)) s.timeFormat = o.value(u"timeFormat"_s).toString(s.timeFormat);
}

} // namespace

QString VaultSettings::attachmentFolderFor(QStringView notePath) const
{
    QString noteFolder = paths::folderOf(notePath);
    const QString a = attachmentFolderPath.trimmed();
    if (a.isEmpty() || a == u"/") return {};
    if (a == u"./" || a == u".") return noteFolder;
    if (a.startsWith(u"./"_s)) return cleanFolder(paths::join(noteFolder, a.mid(2)));
    return cleanFolder(a);
}

QString VaultSettings::newNoteFolder(QStringView currentNotePath) const
{
    switch (newFileLocation) {
    case NewFileLocation::Current:
        return paths::folderOf(currentNotePath);
    case NewFileLocation::Folder:
        return cleanFolder(newFileFolderPath);
    case NewFileLocation::Root:
        break;
    }
    return {};
}

QString settingsFilePath(const QString& vaultRoot)
{
    return vaultRoot + u"/.markdown-notes/settings.json"_s;
}

VaultSettings importObsidianSettings(const QString& vaultRoot)
{
    VaultSettings s;
    const QJsonObject app = readJsonObject(vaultRoot + u"/.obsidian/app.json"_s);
    applyJson(s, app);
    const QJsonObject tpl = readJsonObject(vaultRoot + u"/.obsidian/templates.json"_s);
    if (tpl.contains(u"folder"_s)) s.templatesFolder = cleanFolder(tpl.value(u"folder"_s).toString());
    if (tpl.contains(u"dateFormat"_s) && !tpl.value(u"dateFormat"_s).toString().isEmpty())
        s.dateFormat = tpl.value(u"dateFormat"_s).toString();
    if (tpl.contains(u"timeFormat"_s) && !tpl.value(u"timeFormat"_s).toString().isEmpty())
        s.timeFormat = tpl.value(u"timeFormat"_s).toString();
    // Core "Daily notes" plugin: {"folder": ..., "format": ..., "template": ...}
    const QJsonObject daily = readJsonObject(vaultRoot + u"/.obsidian/daily-notes.json"_s);
    if (!daily.value(u"folder"_s).toString().isEmpty())
        s.dailyNotesFolder = cleanFolder(daily.value(u"folder"_s).toString());
    if (!daily.value(u"format"_s).toString().isEmpty())
        s.dailyNoteFormat = daily.value(u"format"_s).toString();
    if (!daily.value(u"template"_s).toString().isEmpty()) {
        QString t = cleanFolder(daily.value(u"template"_s).toString());
        if (!t.endsWith(u".md"_s, Qt::CaseInsensitive)) t += u".md"_s;
        s.dailyNoteTemplate = std::move(t);
    }
    return s;
}

SettingsLoadResult loadVaultSettings(const QString& vaultRoot)
{
    SettingsLoadResult r;
    const QString path = settingsFilePath(vaultRoot);
    if (QFile::exists(path)) {
        applyJson(r.settings, readJsonObject(path));
        return r;
    }
    r.settings = importObsidianSettings(vaultRoot);
    r.imported = true;
    QString err;
    if (!saveVaultSettings(vaultRoot, r.settings, &err)) r.warning = u"Couldn't save settings: %1"_s.arg(err);
    return r;
}

bool saveVaultSettings(const QString& vaultRoot, const VaultSettings& s, QString* error)
{
    QJsonObject o;
    o.insert(u"version"_s, 1);
    o.insert(u"attachmentFolderPath"_s, s.attachmentFolderPath);
    o.insert(u"newLinkFormat"_s, styleName(s.newLinkFormat));
    o.insert(u"useMarkdownLinks"_s, s.useMarkdownLinks);
    o.insert(u"newFileLocation"_s, locationName(s.newFileLocation));
    o.insert(u"newFileFolderPath"_s, s.newFileFolderPath);
    o.insert(u"alwaysUpdateLinks"_s, s.alwaysUpdateLinks);
    o.insert(u"templatesFolder"_s, s.templatesFolder);
    o.insert(u"dateFormat"_s, s.dateFormat);
    o.insert(u"timeFormat"_s, s.timeFormat);
    o.insert(u"showAllFiles"_s, s.showAllFiles);
    o.insert(u"strictLineBreaks"_s, s.strictLineBreaks);
    o.insert(u"captureToDailyNote"_s, s.captureToDailyNote);
    o.insert(u"captureNote"_s, s.captureNote);
    o.insert(u"captureFormat"_s, s.captureFormat);
    o.insert(u"dailyNotesFolder"_s, s.dailyNotesFolder);
    o.insert(u"dailyNoteFormat"_s, s.dailyNoteFormat);
    o.insert(u"dailyNoteTemplate"_s, s.dailyNoteTemplate);
    o.insert(u"tagProperties"_s, QJsonArray::fromStringList(s.tagProperties));
    o.insert(u"categoryTags"_s, QJsonArray::fromStringList(s.categoryTags));
    o.insert(u"minCategoryTags"_s, s.minCategoryTags);
    if (!QDir().mkpath(vaultRoot + u"/.markdown-notes"_s)) {
        if (error) *error = u"Couldn't create the .markdown-notes folder."_s;
        return false;
    }
    const WriteResult w =
        writeFileAtomic(settingsFilePath(vaultRoot), QJsonDocument(o).toJson(QJsonDocument::Indented));
    if (!w.ok && error) *error = w.error;
    return w.ok;
}

} // namespace mn
