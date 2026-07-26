// SPDX-License-Identifier: GPL-3.0-only

#include "ModFolderStorage.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QSaveFile>
#include <QSet>

#include <utility>

namespace {
constexpr int FORMAT_VERSION = 1;

bool fail(QString* error, const QString& message)
{
    if (error) {
        *error = message;
    }
    return false;
}

bool isBareFileName(const QString& fileName)
{
    return !fileName.isEmpty() && !fileName.contains('/') && !fileName.contains('\\');
}

QByteArray quotedJsonString(const QString& value)
{
    QJsonArray wrapper;
    wrapper.append(value);
    const auto compact = QJsonDocument(wrapper).toJson(QJsonDocument::Compact);
    return compact.mid(1, compact.size() - 2);
}
}  // namespace

ModFolderStorage::ModFolderStorage(QString filePath) : m_filePath(std::move(filePath)) {}

bool ModFolderStorage::load(QString* error)
{
    if (error) {
        error->clear();
    }

    QFile file(m_filePath);
    if (!file.exists()) {
        m_folders.clear();
        return true;
    }
    if (!file.open(QIODevice::ReadOnly)) {
        return fail(error, QStringLiteral("Could not open %1: %2").arg(m_filePath, file.errorString()));
    }

    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        return fail(error, QStringLiteral("Could not parse %1 at offset %2: %3")
                               .arg(m_filePath)
                               .arg(parseError.offset)
                               .arg(parseError.errorString()));
    }
    if (!document.isObject()) {
        return fail(error, QStringLiteral("The root of %1 must be a JSON object.").arg(m_filePath));
    }

    const auto root = document.object();
    if (root.value(QStringLiteral("formatVersion")).toInt(-1) != FORMAT_VERSION) {
        return fail(error, QStringLiteral("Unsupported mod folder format version in %1.").arg(m_filePath));
    }
    if (!root.value(QStringLiteral("folders")).isArray()) {
        return fail(error, QStringLiteral("The folders field in %1 must be an array.").arg(m_filePath));
    }

    QList<Folder> loadedFolders;
    QSet<QString> folderNames;
    QSet<QString> assignedMods;

    const auto folders = root.value(QStringLiteral("folders")).toArray();
    for (const auto& folderValue : folders) {
        if (!folderValue.isObject()) {
            return fail(error, QStringLiteral("Every folder in %1 must be an object.").arg(m_filePath));
        }

        const auto folderObject = folderValue.toObject();
        const auto name = folderObject.value(QStringLiteral("name")).toString().trimmed();
        if (name.isEmpty()) {
            return fail(error, QStringLiteral("Folder names in %1 cannot be empty.").arg(m_filePath));
        }

        const auto foldedName = name.toCaseFolded();
        if (folderNames.contains(foldedName)) {
            return fail(error, QStringLiteral("Folder names in %1 must be unique.").arg(m_filePath));
        }
        folderNames.insert(foldedName);

        if (!folderObject.value(QStringLiteral("mods")).isArray()) {
            return fail(error, QStringLiteral("The mods field of folder \"%1\" must be an array.").arg(name));
        }

        Folder folder;
        folder.name = name;
        for (const auto& modValue : folderObject.value(QStringLiteral("mods")).toArray()) {
            if (!modValue.isString()) {
                return fail(error, QStringLiteral("Every mod in folder \"%1\" must be a file name string.").arg(name));
            }

            const auto fileName = canonicalFileName(modValue.toString());
            if (!isBareFileName(fileName)) {
                return fail(error, QStringLiteral("\"%1\" is not a valid mod file name.").arg(modValue.toString()));
            }

            const auto foldedFileName = fileName.toCaseFolded();
            if (assignedMods.contains(foldedFileName)) {
                continue;
            }
            assignedMods.insert(foldedFileName);
            folder.mods.append(fileName);
        }
        loadedFolders.append(folder);
    }

    m_folders = loadedFolders;
    return true;
}

bool ModFolderStorage::save(QString* error) const
{
    if (error) {
        error->clear();
    }

    const QFileInfo fileInfo(m_filePath);
    if (!QDir().mkpath(fileInfo.absolutePath())) {
        return fail(error, QStringLiteral("Could not create %1.").arg(fileInfo.absolutePath()));
    }

    // The UTF-8 BOM keeps folder names readable in Windows editors that would
    // otherwise guess the system ANSI code page for a plain JSON file.
    QByteArray json("\xEF\xBB\xBF");
    json += "{\n";
    json += "    \"formatVersion\": " + QByteArray::number(FORMAT_VERSION) + ",\n";
    json += "    \"folders\": [";
    if (!m_folders.isEmpty()) {
        json += "\n";
    }
    for (int folderIndex = 0; folderIndex < m_folders.size(); ++folderIndex) {
        const auto& folder = m_folders.at(folderIndex);
        json += "        {\n";
        json += "            \"name\": " + quotedJsonString(folder.name) + ",\n";
        json += "            \"mods\": [";
        if (!folder.mods.isEmpty()) {
            json += "\n";
        }
        for (int modIndex = 0; modIndex < folder.mods.size(); ++modIndex) {
            json += "                " + quotedJsonString(folder.mods.at(modIndex));
            json += modIndex + 1 < folder.mods.size() ? ",\n" : "\n";
        }
        json += folder.mods.isEmpty() ? "]\n" : "            ]\n";
        json += folderIndex + 1 < m_folders.size() ? "        },\n" : "        }\n";
    }
    json += m_folders.isEmpty() ? "]\n" : "    ]\n";
    json += "}\n";

    QSaveFile file(m_filePath);
    if (!file.open(QIODevice::WriteOnly)) {
        return fail(error, QStringLiteral("Could not open %1 for writing: %2").arg(m_filePath, file.errorString()));
    }
    if (file.write(json) < 0) {
        file.cancelWriting();
        return fail(error, QStringLiteral("Could not write %1: %2").arg(m_filePath, file.errorString()));
    }
    if (!file.commit()) {
        return fail(error, QStringLiteral("Could not save %1: %2").arg(m_filePath, file.errorString()));
    }
    return true;
}

bool ModFolderStorage::createFolder(const QString& name, QString* error)
{
    const auto cleanName = name.trimmed();
    if (!validateFolderName(cleanName, error)) {
        return false;
    }
    m_folders.append({ cleanName, {} });
    return true;
}

bool ModFolderStorage::renameFolder(const QString& oldName, const QString& newName, QString* error)
{
    const auto folderIndex = findFolder(oldName);
    if (folderIndex < 0) {
        return fail(error, QStringLiteral("Folder \"%1\" does not exist.").arg(oldName));
    }

    const auto cleanName = newName.trimmed();
    if (m_folders.at(folderIndex).name.compare(cleanName, Qt::CaseInsensitive) != 0 && !validateFolderName(cleanName, error)) {
        return false;
    }
    if (cleanName.isEmpty()) {
        return fail(error, QStringLiteral("Folder names cannot be empty."));
    }

    m_folders[folderIndex].name = cleanName;
    return true;
}

bool ModFolderStorage::removeFolder(const QString& name)
{
    const auto folderIndex = findFolder(name);
    if (folderIndex < 0) {
        return false;
    }
    m_folders.removeAt(folderIndex);
    return true;
}

bool ModFolderStorage::assignMods(const QString& folderName, const QStringList& fileNames, QString* error)
{
    const auto folderIndex = findFolder(folderName);
    if (folderIndex < 0) {
        return fail(error, QStringLiteral("Folder \"%1\" does not exist.").arg(folderName));
    }

    QStringList canonicalNames;
    for (auto fileName : fileNames) {
        fileName = canonicalFileName(fileName);
        if (!isBareFileName(fileName)) {
            return fail(error, QStringLiteral("\"%1\" is not a valid mod file name.").arg(fileName));
        }
        if (!canonicalNames.contains(fileName, Qt::CaseInsensitive)) {
            canonicalNames.append(fileName);
        }
    }

    unassignMods(canonicalNames);
    for (const auto& fileName : canonicalNames) {
        m_folders[folderIndex].mods.append(fileName);
    }
    return true;
}

void ModFolderStorage::unassignMods(const QStringList& fileNames)
{
    QSet<QString> canonicalNames;
    for (const auto& fileName : fileNames) {
        canonicalNames.insert(canonicalFileName(fileName).toCaseFolded());
    }

    for (auto& folder : m_folders) {
        for (auto it = folder.mods.end(); it != folder.mods.begin();) {
            --it;
            if (canonicalNames.contains(it->toCaseFolded())) {
                it = folder.mods.erase(it);
            }
        }
    }
}

bool ModFolderStorage::replaceModFileName(const QString& oldFileName, const QString& newFileName)
{
    const auto oldCanonicalName = canonicalFileName(oldFileName);
    const auto newCanonicalName = canonicalFileName(newFileName);
    if (!isBareFileName(oldCanonicalName) || !isBareFileName(newCanonicalName)) {
        return false;
    }

    const auto folderName = folderForMod(oldCanonicalName);
    if (folderName.isEmpty()) {
        return false;
    }

    unassignMods({ oldCanonicalName, newCanonicalName });
    return assignMods(folderName, { newCanonicalName });
}

QString ModFolderStorage::folderForMod(const QString& fileName) const
{
    const auto canonicalName = canonicalFileName(fileName);
    for (const auto& folder : m_folders) {
        if (folder.mods.contains(canonicalName, Qt::CaseInsensitive)) {
            return folder.name;
        }
    }
    return {};
}

QString ModFolderStorage::canonicalFileName(QString fileName)
{
    fileName = fileName.trimmed();
    if (fileName.endsWith(QStringLiteral(".disabled"), Qt::CaseInsensitive)) {
        fileName.chop(9);
    }
    return fileName;
}

int ModFolderStorage::findFolder(const QString& name) const
{
    for (int i = 0; i < m_folders.size(); ++i) {
        if (m_folders.at(i).name.compare(name, Qt::CaseInsensitive) == 0) {
            return i;
        }
    }
    return -1;
}

bool ModFolderStorage::validateFolderName(const QString& name, QString* error) const
{
    if (name.isEmpty()) {
        return fail(error, QStringLiteral("Folder names cannot be empty."));
    }
    if (findFolder(name) >= 0) {
        return fail(error, QStringLiteral("A folder named \"%1\" already exists.").arg(name));
    }
    return true;
}
