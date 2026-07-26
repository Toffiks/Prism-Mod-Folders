// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QList>
#include <QString>
#include <QStringList>

class ModFolderStorage {
   public:
    struct Folder {
        QString name;
        QStringList mods;

        bool operator==(const Folder& other) const { return name == other.name && mods == other.mods; }
    };

    explicit ModFolderStorage(QString filePath);

    bool load(QString* error = nullptr);
    bool save(QString* error = nullptr) const;

    const QList<Folder>& folders() const { return m_folders; }

    bool createFolder(const QString& name, QString* error = nullptr);
    bool renameFolder(const QString& oldName, const QString& newName, QString* error = nullptr);
    bool removeFolder(const QString& name);

    bool assignMods(const QString& folderName, const QStringList& fileNames, QString* error = nullptr);
    void unassignMods(const QStringList& fileNames);
    bool replaceModFileName(const QString& oldFileName, const QString& newFileName);

    QString folderForMod(const QString& fileName) const;

    static QString canonicalFileName(QString fileName);

   private:
    int findFolder(const QString& name) const;
    bool validateFolderName(const QString& name, QString* error) const;

   private:
    QString m_filePath;
    QList<Folder> m_folders;
};
