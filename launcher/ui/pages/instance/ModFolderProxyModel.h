// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QAbstractProxyModel>
#include <QList>
#include <QPixmap>

class ModFolderModel;
class ModFolderStorage;

class ModFolderProxyModel : public QAbstractProxyModel {
    Q_OBJECT

   public:
    inline static constexpr char MimeType[] = "application/x-prism-mod-folder-items";

    enum Roles {
        FolderRole = Qt::UserRole + 100,
        FolderNameRole,
        SeparatorRole,
    };

    ModFolderProxyModel(QAbstractItemModel* sortedModModel,
                        ModFolderModel* modModel,
                        ModFolderStorage* storage,
                        QObject* parent = nullptr);

    QModelIndex mapFromSource(const QModelIndex& sourceIndex) const override;
    QModelIndex mapToSource(const QModelIndex& proxyIndex) const override;

    QModelIndex index(int row, int column, const QModelIndex& parent = {}) const override;
    QModelIndex parent(const QModelIndex& child) const override;
    int rowCount(const QModelIndex& parent = {}) const override;
    int columnCount(const QModelIndex& parent = {}) const override;

    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;
    Qt::ItemFlags flags(const QModelIndex& index) const override;

    QStringList mimeTypes() const override;
    QMimeData* mimeData(const QModelIndexList& indexes) const override;
    bool canDropMimeData(const QMimeData* data,
                         Qt::DropAction action,
                         int row,
                         int column,
                         const QModelIndex& parent) const override;
    bool dropMimeData(const QMimeData* data,
                      Qt::DropAction action,
                      int row,
                      int column,
                      const QModelIndex& parent) override;
    Qt::DropActions supportedDropActions() const override;
    Qt::DropActions supportedDragActions() const override;

    void sort(int column, Qt::SortOrder order = Qt::AscendingOrder) override;

    bool isFolder(const QModelIndex& index) const;
    bool isSeparator(const QModelIndex& index) const;
    QString folderName(const QModelIndex& index) const;
    QString modFileName(const QModelIndex& index) const;

   public slots:
    void rebuild();

   signals:
    void storageError(const QString& message);

   private:
    int folderIndex(const QModelIndex& index) const;
    int folderRootRow(int folder) const;
    int sourceRow(const QModelIndex& index) const;
    QString fileNameForSourceRow(int row) const;
    int assignedFolderForSourceRow(int row) const;
    QPixmap folderPreviewPixmap(int folder) const;

   private:
    ModFolderModel* m_modModel;
    ModFolderStorage* m_storage;
    QList<QList<int>> m_folderRows;
    QList<int> m_ungroupedRows;
};
