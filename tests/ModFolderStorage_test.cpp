// SPDX-License-Identifier: GPL-3.0-only

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTest>

#include <minecraft/mod/ModFolderStorage.h>

class ModFolderStorageTest : public QObject {
    Q_OBJECT

   private slots:
    void roundTripMinimalFormat()
    {
        QTemporaryDir temporaryDir;
        QVERIFY(temporaryDir.isValid());
        const auto filePath = temporaryDir.filePath("modfolders.json");

        ModFolderStorage storage(filePath);
        QVERIFY(storage.createFolder("Оптимизация"));
        QVERIFY(storage.createFolder("Графика"));
        QVERIFY(storage.assignMods("Оптимизация", { "sodium.jar", "lithium.jar" }));
        QVERIFY(storage.assignMods("Графика", { "iris.jar" }));
        QVERIFY(storage.save());

        QFile file(filePath);
        QVERIFY(file.open(QIODevice::ReadOnly));
        const auto fileData = file.readAll();
        QVERIFY(fileData.startsWith("\xEF\xBB\xBF"));
        QVERIFY(fileData.indexOf("\"formatVersion\"") < fileData.indexOf("\"folders\""));
        QVERIFY(fileData.indexOf("\"name\"") < fileData.indexOf("\"mods\""));
        QVERIFY(fileData.contains(QStringLiteral("Оптимизация").toUtf8()));

        const auto root = QJsonDocument::fromJson(fileData).object();
        QCOMPARE(root.value("formatVersion").toInt(), 1);
        QVERIFY(root.contains("folders"));
        QVERIFY(!root.contains("collapsed"));

        ModFolderStorage loaded(filePath);
        QVERIFY(loaded.load());
        QCOMPARE(loaded.folders(), storage.folders());
    }

    void oneModCanOnlyBelongToOneFolder()
    {
        QTemporaryDir temporaryDir;
        ModFolderStorage storage(temporaryDir.filePath("modfolders.json"));

        QVERIFY(storage.createFolder("Первая"));
        QVERIFY(storage.createFolder("Вторая"));
        QVERIFY(storage.assignMods("Первая", { "example.jar" }));
        QCOMPARE(storage.folderForMod("example.jar"), QString("Первая"));

        QVERIFY(storage.assignMods("Вторая", { "example.jar" }));
        QCOMPARE(storage.folderForMod("example.jar"), QString("Вторая"));
        QVERIFY(storage.folders().at(0).mods.isEmpty());
    }

    void deletingFolderOnlyRemovesAssignments()
    {
        QTemporaryDir temporaryDir;
        ModFolderStorage storage(temporaryDir.filePath("modfolders.json"));

        QVERIFY(storage.createFolder("Удаляемая"));
        QVERIFY(storage.assignMods("Удаляемая", { "still-installed.jar" }));
        QVERIFY(storage.removeFolder("Удаляемая"));

        QCOMPARE(storage.folderForMod("still-installed.jar"), QString());
        QVERIFY(storage.folders().isEmpty());
    }

    void disabledSuffixDoesNotChangeAssignment()
    {
        QTemporaryDir temporaryDir;
        ModFolderStorage storage(temporaryDir.filePath("modfolders.json"));

        QVERIFY(storage.createFolder("Оптимизация"));
        QVERIFY(storage.assignMods("Оптимизация", { "sodium.jar.disabled" }));

        QCOMPARE(storage.folders().at(0).mods, QStringList({ "sodium.jar" }));
        QCOMPARE(storage.folderForMod("sodium.jar.disabled"), QString("Оптимизация"));
    }

    void renamingUpdatedModKeepsFolder()
    {
        QTemporaryDir temporaryDir;
        ModFolderStorage storage(temporaryDir.filePath("modfolders.json"));

        QVERIFY(storage.createFolder("Оптимизация"));
        QVERIFY(storage.assignMods("Оптимизация", { "sodium-old.jar" }));
        QVERIFY(storage.replaceModFileName("sodium-old.jar", "sodium-new.jar"));

        QCOMPARE(storage.folderForMod("sodium-old.jar"), QString());
        QCOMPARE(storage.folderForMod("sodium-new.jar"), QString("Оптимизация"));
    }

};

QTEST_GUILESS_MAIN(ModFolderStorageTest)

#include "ModFolderStorage_test.moc"
