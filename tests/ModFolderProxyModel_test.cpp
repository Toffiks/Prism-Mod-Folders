// SPDX-License-Identifier: GPL-3.0-only

#include <QFile>
#include <QMimeData>
#include <QSignalSpy>
#include <QSortFilterProxyModel>
#include <QTemporaryDir>
#include <QTest>

#include <memory>

#include <minecraft/mod/ModFolderModel.h>
#include <minecraft/mod/ModFolderStorage.h>
#include <ui/pages/instance/ModFolderProxyModel.h>

class TestModFolderModel : public ModFolderModel {
   public:
    explicit TestModFolderModel(const QDir& directory) : ModFolderModel(directory, nullptr, false, true) {}

    void appendMod(const QString& filePath)
    {
        const auto row = m_resources.size();
        beginInsertRows({}, row, row);
        m_resources.append(Resource::Ptr(new Mod(QFileInfo(filePath))));
        endInsertRows();
    }
};

class ModFolderProxyModelTest : public QObject {
    Q_OBJECT

   private:
    static void createFile(const QString& path)
    {
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write("test"), 4);
    }

    static QStringList expectedFiles(const QSortFilterProxyModel& sorted,
                                     const TestModFolderModel& model,
                                     const ModFolderStorage& storage,
                                     const QString& folder)
    {
        QStringList files;
        for (int row = 0; row < sorted.rowCount(); ++row) {
            const auto source = sorted.mapToSource(sorted.index(row, 0));
            const auto fileName = model.at(source.row()).getOriginalFileName();
            if (storage.folderForMod(fileName).compare(folder, Qt::CaseInsensitive) == 0) {
                files.append(fileName);
            }
        }
        return files;
    }

    static QStringList actualFiles(const ModFolderProxyModel& grouped, const QModelIndex& folder)
    {
        QStringList files;
        for (int row = 0; row < grouped.rowCount(folder); ++row) {
            files.append(grouped.modFileName(grouped.index(row, 0, folder)));
        }
        return files;
    }

    static QModelIndex findMod(const ModFolderProxyModel& grouped, const QModelIndex& parent, const QString& fileName)
    {
        for (int row = 0; row < grouped.rowCount(parent); ++row) {
            const auto index = grouped.index(row, 0, parent);
            if (grouped.modFileName(index).compare(fileName, Qt::CaseInsensitive) == 0) {
                return index;
            }
        }
        return {};
    }

   private slots:
    void folderContentUsesImageAndNameColumns()
    {
        QTemporaryDir temporaryDir;
        QVERIFY(temporaryDir.isValid());

        const auto alpha = temporaryDir.filePath("alpha.jar");
        createFile(alpha);

        TestModFolderModel model(QDir(temporaryDir.path()));
        model.appendMod(alpha);
        std::unique_ptr<QSortFilterProxyModel> sorted(model.createFilterProxyModel());
        sorted->setSourceModel(&model);

        ModFolderStorage storage(temporaryDir.filePath("modfolders.json"));
        QVERIFY(storage.createFolder("Empty"));
        QVERIFY(storage.createFolder("Filled"));
        QVERIFY(storage.assignMods("Filled", { "alpha.jar" }));

        ModFolderProxyModel grouped(sorted.get(), &model, &storage);
        const auto emptyName = grouped.index(0, ModFolderModel::NameColumn);
        const auto emptyImage = grouped.index(0, ModFolderModel::ImageColumn);
        const auto filledName = grouped.index(2, ModFolderModel::NameColumn);
        const auto filledImage = grouped.index(2, ModFolderModel::ImageColumn);

        QCOMPARE(emptyName.data(Qt::DisplayRole).toString(), QString("Empty (0)"));
        QVERIFY(emptyImage.data(Qt::DisplayRole).toString().isEmpty());
        QCOMPARE(emptyImage.data(Qt::SizeHintRole).toSize(), QSize(32, 32));

        QCOMPARE(filledName.data(Qt::DisplayRole).toString(), QString("Filled (1)"));
        QVERIFY(filledImage.data(Qt::DisplayRole).toString().isEmpty());
        QCOMPARE(filledImage.data(Qt::SizeHintRole).toSize(), QSize(32, 32));

        QCOMPARE(grouped.rowCount(), 4);
        for (const int row : { 1, 3 }) {
            const auto separator = grouped.index(row, 0);
            QVERIFY(grouped.isSeparator(separator));
            QCOMPARE(separator.data(Qt::SizeHintRole).toSize(), QSize(-1, 10));
            QVERIFY(separator.data(Qt::DisplayRole).toString().isEmpty());
            QCOMPARE(grouped.flags(separator), Qt::NoItemFlags);
        }
    }

    void sortingStaysInsideEveryFolder()
    {
        QTemporaryDir temporaryDir;
        QVERIFY(temporaryDir.isValid());

        const auto alpha = temporaryDir.filePath("alpha.jar");
        const auto beta = temporaryDir.filePath("beta.jar.disabled");
        const auto gamma = temporaryDir.filePath("gamma.jar");
        const auto delta = temporaryDir.filePath("delta.jar.disabled");
        createFile(gamma);
        createFile(beta);
        createFile(delta);
        createFile(alpha);

        TestModFolderModel model(QDir(temporaryDir.path()));
        model.appendMod(gamma);
        model.appendMod(beta);
        model.appendMod(delta);
        model.appendMod(alpha);

        std::unique_ptr<QSortFilterProxyModel> sorted(model.createFilterProxyModel());
        sorted->setSourceModel(&model);

        ModFolderStorage storage(temporaryDir.filePath("modfolders.json"));
        QVERIFY(storage.createFolder("Оптимизация"));
        QVERIFY(storage.createFolder("Графика"));
        QVERIFY(storage.assignMods("Оптимизация", { "beta.jar.disabled", "alpha.jar" }));
        QVERIFY(storage.assignMods("Графика", { "gamma.jar" }));

        ModFolderProxyModel grouped(sorted.get(), &model, &storage);
        QCOMPARE(grouped.rowCount(), 5);
        QCOMPARE(grouped.folderName(grouped.index(0, 0)), QString("Оптимизация"));
        QCOMPARE(grouped.folderName(grouped.index(2, 0)), QString("Графика"));

        const auto verifyPartition = [&] {
            const auto optimization = grouped.index(0, 0);
            const auto graphics = grouped.index(2, 0);
            QCOMPARE(actualFiles(grouped, optimization), expectedFiles(*sorted, model, storage, "Оптимизация"));
            QCOMPARE(actualFiles(grouped, graphics), expectedFiles(*sorted, model, storage, "Графика"));

            const auto ungrouped = grouped.index(4, 0);
            QCOMPARE(grouped.modFileName(ungrouped), QString("delta.jar"));
        };

        grouped.sort(ModFolderModel::NameColumn, Qt::DescendingOrder);
        verifyPartition();

        grouped.sort(ModFolderModel::ActiveColumn, Qt::AscendingOrder);
        verifyPartition();

        grouped.sort(ModFolderModel::ActiveColumn, Qt::DescendingOrder);
        verifyPartition();
    }

    void dragAndDropMovesOnlyAssignments()
    {
        QTemporaryDir temporaryDir;
        QVERIFY(temporaryDir.isValid());
        const auto alpha = temporaryDir.filePath("alpha.jar");
        createFile(alpha);

        TestModFolderModel model(QDir(temporaryDir.path()));
        model.appendMod(alpha);
        std::unique_ptr<QSortFilterProxyModel> sorted(model.createFilterProxyModel());
        sorted->setSourceModel(&model);

        ModFolderStorage storage(temporaryDir.filePath("modfolders.json"));
        QVERIFY(storage.createFolder("Первая"));
        QVERIFY(storage.createFolder("Вторая"));
        QVERIFY(storage.assignMods("Первая", { "alpha.jar" }));

        ModFolderProxyModel grouped(sorted.get(), &model, &storage);
        auto first = grouped.index(0, 0);
        auto second = grouped.index(2, 0);
        auto alphaIndex = findMod(grouped, first, "alpha.jar");
        QVERIFY(alphaIndex.isValid());

        std::unique_ptr<QMimeData> moveToSecond(grouped.mimeData({ alphaIndex }));
        QVERIFY(grouped.dropMimeData(moveToSecond.get(), Qt::MoveAction, -1, -1, second));
        QCOMPARE(storage.folderForMod("alpha.jar"), QString("Вторая"));
        QCOMPARE(grouped.rowCount(grouped.index(0, 0)), 0);
        QCOMPARE(grouped.rowCount(grouped.index(2, 0)), 1);

        second = grouped.index(2, 0);
        alphaIndex = findMod(grouped, second, "alpha.jar");
        std::unique_ptr<QMimeData> moveToRoot(grouped.mimeData({ alphaIndex }));
        QVERIFY(grouped.dropMimeData(moveToRoot.get(), Qt::MoveAction, -1, -1, {}));
        QCOMPARE(storage.folderForMod("alpha.jar"), QString());
        QCOMPARE(grouped.rowCount(), 5);
        QCOMPARE(grouped.modFileName(grouped.index(4, 0)), QString("alpha.jar"));
        QVERIFY(QFileInfo::exists(alpha));
    }

    void failedSaveDoesNotChangeAssignments()
    {
        QTemporaryDir temporaryDir;
        QVERIFY(temporaryDir.isValid());

        const auto alpha = temporaryDir.filePath("alpha.jar");
        const auto blockedDirectory = temporaryDir.filePath("blocked");
        createFile(alpha);
        createFile(blockedDirectory);

        TestModFolderModel model(QDir(temporaryDir.path()));
        model.appendMod(alpha);
        std::unique_ptr<QSortFilterProxyModel> sorted(model.createFilterProxyModel());
        sorted->setSourceModel(&model);

        ModFolderStorage storage(blockedDirectory + "/modfolders.json");
        QVERIFY(storage.createFolder("First"));
        QVERIFY(storage.createFolder("Second"));
        QVERIFY(storage.assignMods("First", { "alpha.jar" }));

        ModFolderProxyModel grouped(sorted.get(), &model, &storage);
        const auto first = grouped.index(0, 0);
        const auto second = grouped.index(2, 0);
        const auto alphaIndex = findMod(grouped, first, "alpha.jar");
        QVERIFY(alphaIndex.isValid());

        QSignalSpy errors(&grouped, &ModFolderProxyModel::storageError);
        std::unique_ptr<QMimeData> moveToSecond(grouped.mimeData({ alphaIndex }));
        QVERIFY(!grouped.dropMimeData(moveToSecond.get(), Qt::MoveAction, -1, -1, second));

        QCOMPARE(errors.count(), 1);
        QCOMPARE(storage.folderForMod("alpha.jar"), QString("First"));
        QCOMPARE(grouped.rowCount(first), 1);
        QCOMPARE(grouped.rowCount(second), 0);
    }
};

QTEST_GUILESS_MAIN(ModFolderProxyModelTest)

#include "ModFolderProxyModel_test.moc"
