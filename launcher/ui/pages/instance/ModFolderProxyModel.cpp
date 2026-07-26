// SPDX-License-Identifier: GPL-3.0-only

#include "ModFolderProxyModel.h"

#include <QAbstractProxyModel>
#include <QFont>
#include <QIcon>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMimeData>
#include <QPainter>
#include <QPixmap>
#include <QSet>
#include <QSize>

#include <algorithm>
#include <utility>

#include "minecraft/mod/ModFolderModel.h"
#include "minecraft/mod/ModFolderStorage.h"
#include "minecraft/mod/Mod.h"
#include "minecraft/mod/Resource.h"

namespace {
constexpr quintptr ITEM_TYPE_MASK = 3;
constexpr quintptr FOLDER_MARKER = 1;
constexpr quintptr SEPARATOR_MARKER = 2;

quintptr folderId(int folderIndex)
{
    return (static_cast<quintptr>(folderIndex) << 2) | FOLDER_MARKER;
}

quintptr separatorId(int folderIndex)
{
    return (static_cast<quintptr>(folderIndex) << 2) | SEPARATOR_MARKER;
}

quintptr modId(int sourceRow)
{
    return static_cast<quintptr>(sourceRow) << 2;
}
}  // namespace

ModFolderProxyModel::ModFolderProxyModel(QAbstractItemModel* sortedModModel,
                                         ModFolderModel* modModel,
                                         ModFolderStorage* storage,
                                         QObject* parent)
    : QAbstractProxyModel(parent), m_modModel(modModel), m_storage(storage)
{
    setSourceModel(sortedModModel);

    connect(sortedModModel, &QAbstractItemModel::modelReset, this, &ModFolderProxyModel::rebuild);
    connect(sortedModModel, &QAbstractItemModel::layoutChanged, this, &ModFolderProxyModel::rebuild);
    connect(sortedModModel, &QAbstractItemModel::rowsInserted, this, &ModFolderProxyModel::rebuild);
    connect(sortedModModel, &QAbstractItemModel::rowsRemoved, this, &ModFolderProxyModel::rebuild);
    connect(sortedModModel, &QAbstractItemModel::dataChanged, this,
            [this](const QModelIndex& topLeft, const QModelIndex& bottomRight, const auto& roles) {
                for (int sourceRow = topLeft.row(); sourceRow <= bottomRight.row(); ++sourceRow) {
                    const auto proxyLeft = mapFromSource(sourceModel()->index(sourceRow, topLeft.column()));
                    const auto proxyRight = mapFromSource(sourceModel()->index(sourceRow, bottomRight.column()));
                    if (proxyLeft.isValid() && proxyRight.isValid()) {
                        emit dataChanged(proxyLeft, proxyRight, roles);
                    }
                }
                if (roles.isEmpty() || roles.contains(Qt::DisplayRole) || roles.contains(Qt::DecorationRole)) {
                    for (int folder = 0; folder < m_storage->folders().size(); ++folder) {
                        const auto image = index(folderRootRow(folder), ModFolderModel::ImageColumn);
                        emit dataChanged(image, image, { Qt::DecorationRole });
                    }
                }
            });

    rebuild();
}

QModelIndex ModFolderProxyModel::mapFromSource(const QModelIndex& sourceIndex) const
{
    if (!sourceIndex.isValid() || sourceIndex.model() != sourceModel()) {
        return {};
    }

    const auto assignedFolder = assignedFolderForSourceRow(sourceIndex.row());
    if (assignedFolder >= 0) {
        const auto childRow = m_folderRows.at(assignedFolder).indexOf(sourceIndex.row());
        if (childRow >= 0) {
            return createIndex(childRow, sourceIndex.column(), modId(sourceIndex.row()));
        }
    }

    const auto ungroupedRow = m_ungroupedRows.indexOf(sourceIndex.row());
    if (ungroupedRow >= 0) {
        return createIndex(
            m_storage->folders().size() * 2 + ungroupedRow, sourceIndex.column(), modId(sourceIndex.row()));
    }
    return {};
}

QModelIndex ModFolderProxyModel::mapToSource(const QModelIndex& proxyIndex) const
{
    if (!proxyIndex.isValid() || isFolder(proxyIndex) || isSeparator(proxyIndex)) {
        return {};
    }
    return sourceModel()->index(sourceRow(proxyIndex), proxyIndex.column());
}

QModelIndex ModFolderProxyModel::index(int row, int column, const QModelIndex& parent) const
{
    if (row < 0 || column < 0 || column >= columnCount()) {
        return {};
    }

    if (!parent.isValid()) {
        const auto folderCount = m_storage->folders().size();
        const auto groupedRootRows = folderCount * 2;
        if (row < groupedRootRows) {
            const auto folder = row / 2;
            return row % 2 == 0 ? createIndex(row, column, folderId(folder))
                                : createIndex(row, column, separatorId(folder));
        }

        const auto ungroupedRow = row - groupedRootRows;
        if (ungroupedRow >= 0 && ungroupedRow < m_ungroupedRows.size()) {
            return createIndex(row, column, modId(m_ungroupedRows.at(ungroupedRow)));
        }
        return {};
    }

    if (!isFolder(parent) || parent.column() != 0) {
        return {};
    }

    const auto folder = folderIndex(parent);
    if (folder < 0 || folder >= m_folderRows.size() || row >= m_folderRows.at(folder).size()) {
        return {};
    }
    return createIndex(row, column, modId(m_folderRows.at(folder).at(row)));
}

QModelIndex ModFolderProxyModel::parent(const QModelIndex& child) const
{
    if (!child.isValid() || isFolder(child) || isSeparator(child)) {
        return {};
    }

    const auto folder = assignedFolderForSourceRow(sourceRow(child));
    if (folder < 0) {
        return {};
    }
    return createIndex(folderRootRow(folder), 0, folderId(folder));
}

int ModFolderProxyModel::rowCount(const QModelIndex& parent) const
{
    if (!parent.isValid()) {
        return m_storage->folders().size() * 2 + m_ungroupedRows.size();
    }
    if (isFolder(parent) && parent.column() == 0) {
        const auto folder = folderIndex(parent);
        return folder >= 0 && folder < m_folderRows.size() ? m_folderRows.at(folder).size() : 0;
    }
    return 0;
}

int ModFolderProxyModel::columnCount(const QModelIndex&) const
{
    return sourceModel() ? sourceModel()->columnCount() : 0;
}

QVariant ModFolderProxyModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid()) {
        return {};
    }
    if (isSeparator(index)) {
        if (role == SeparatorRole) {
            return true;
        }
        if (role == Qt::SizeHintRole) {
            return QSize(-1, 10);
        }
        return {};
    }
    if (!isFolder(index)) {
        return sourceModel()->data(mapToSource(index), role);
    }

    const auto folder = folderIndex(index);
    if (folder < 0 || folder >= m_storage->folders().size()) {
        return {};
    }

    const auto& folderData = m_storage->folders().at(folder);
    if (role == FolderRole) {
        return true;
    }
    if (role == FolderNameRole) {
        return folderData.name;
    }
    if (role == Qt::DisplayRole && index.column() == ModFolderModel::NameColumn) {
        return tr("%1 (%2)").arg(folderData.name).arg(m_folderRows.at(folder).size());
    }
    if (role == Qt::DecorationRole && index.column() == ModFolderModel::ImageColumn) {
        return folderPreviewPixmap(folder);
    }
    if (role == Qt::ToolTipRole) {
        return tr("%1 mods in this folder").arg(m_folderRows.at(folder).size());
    }
    if (role == Qt::FontRole) {
        QFont font;
        font.setBold(true);
        return font;
    }
    if (role == Qt::SizeHintRole) {
        return index.column() == ModFolderModel::ImageColumn ? QSize(32, 32) : QSize(-1, 32);
    }
    return {};
}

QPixmap ModFolderProxyModel::folderPreviewPixmap(int folder) const
{
    if (folder < 0 || folder >= m_folderRows.size() || m_folderRows.at(folder).isEmpty()) {
        return QIcon::fromTheme(QStringLiteral("viewfolder")).pixmap(32, 32);
    }

    auto rows = m_folderRows.at(folder);
    std::sort(rows.begin(), rows.end(), [this](int left, int right) {
        const auto leftName = sourceModel()->data(sourceModel()->index(left, ModFolderModel::NameColumn), Qt::DisplayRole).toString();
        const auto rightName = sourceModel()->data(sourceModel()->index(right, ModFolderModel::NameColumn), Qt::DisplayRole).toString();
        const int nameOrder = QString::localeAwareCompare(leftName.toCaseFolded(), rightName.toCaseFolded());
        if (nameOrder != 0) {
            return nameOrder < 0;
        }
        return fileNameForSourceRow(left).compare(fileNameForSourceRow(right), Qt::CaseInsensitive) < 0;
    });

    QPixmap preview(32, 32);
    preview.fill(Qt::transparent);
    QPainter painter(&preview);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);

    const int count = qMin(4, rows.size());
    for (int i = 0; i < count; ++i) {
        const auto decoration =
            sourceModel()->data(sourceModel()->index(rows.at(i), ModFolderModel::ImageColumn), Qt::DecorationRole);
        auto pixmap = qvariant_cast<QPixmap>(decoration);
        if (pixmap.isNull()) {
            const auto icon = qvariant_cast<QIcon>(decoration);
            pixmap = icon.pixmap(16, 16);
        }
        if (pixmap.isNull()) {
            continue;
        }
        const QRect cell((i % 2) * 16, (i / 2) * 16, 16, 16);
        painter.drawPixmap(cell, pixmap.scaled(cell.size(), Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation));
    }
    painter.end();
    return preview;
}

QVariant ModFolderProxyModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    return sourceModel() ? sourceModel()->headerData(section, orientation, role) : QVariant();
}

Qt::ItemFlags ModFolderProxyModel::flags(const QModelIndex& index) const
{
    if (!index.isValid()) {
        return Qt::ItemIsDropEnabled;
    }
    if (isSeparator(index)) {
        return Qt::NoItemFlags;
    }
    if (isFolder(index)) {
        return Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsDropEnabled;
    }
    return sourceModel()->flags(mapToSource(index)) | Qt::ItemIsDragEnabled;
}

QStringList ModFolderProxyModel::mimeTypes() const
{
    auto types = sourceModel()->mimeTypes();
    if (!types.contains(QString::fromLatin1(MimeType))) {
        types.append(QString::fromLatin1(MimeType));
    }
    return types;
}

QMimeData* ModFolderProxyModel::mimeData(const QModelIndexList& indexes) const
{
    auto* data = new QMimeData;
    QJsonArray fileNames;
    QSet<QString> addedFileNames;

    for (const auto& index : indexes) {
        if (index.column() != 0 || isFolder(index) || isSeparator(index)) {
            continue;
        }

        const auto fileName = modFileName(index);
        const auto key = fileName.toCaseFolded();
        if (!fileName.isEmpty() && !addedFileNames.contains(key)) {
            addedFileNames.insert(key);
            fileNames.append(fileName);
        }
    }

    if (!fileNames.isEmpty()) {
        data->setData(MimeType, QJsonDocument(fileNames).toJson(QJsonDocument::Compact));
    }
    return data;
}

bool ModFolderProxyModel::canDropMimeData(const QMimeData* data,
                                          Qt::DropAction action,
                                          int row,
                                          int column,
                                          const QModelIndex& parent) const
{
    if (action == Qt::IgnoreAction) {
        return true;
    }
    if (!data) {
        return false;
    }
    if (data->hasFormat(MimeType)) {
        return action == Qt::MoveAction && (!parent.isValid() || isFolder(parent));
    }
    return sourceModel()->canDropMimeData(data, action, row, column, mapToSource(parent));
}

bool ModFolderProxyModel::dropMimeData(const QMimeData* data,
                                       Qt::DropAction action,
                                       int row,
                                       int column,
                                       const QModelIndex& parent)
{
    if (action == Qt::IgnoreAction) {
        return true;
    }
    if (!data) {
        return false;
    }

    if (data->hasFormat(MimeType)) {
        const auto document = QJsonDocument::fromJson(data->data(MimeType));
        if (!document.isArray()) {
            return false;
        }

        QStringList fileNames;
        for (const auto& value : document.array()) {
            if (value.isString()) {
                fileNames.append(value.toString());
            }
        }

        QModelIndex targetFolder = parent;
        if (targetFolder.isValid() && !isFolder(targetFolder)) {
            targetFolder = targetFolder.parent();
        }

        auto candidate = *m_storage;
        QString error;
        if (targetFolder.isValid() && isFolder(targetFolder)) {
            if (!candidate.assignMods(folderName(targetFolder), fileNames, &error)) {
                emit storageError(error);
                return false;
            }
        } else {
            candidate.unassignMods(fileNames);
        }

        if (candidate.folders() == m_storage->folders()) {
            return false;
        }
        if (!candidate.save(&error)) {
            emit storageError(error);
            return false;
        }

        *m_storage = std::move(candidate);
        rebuild();
        return true;
    }

    return sourceModel()->dropMimeData(data, action, row, column, mapToSource(parent));
}

Qt::DropActions ModFolderProxyModel::supportedDropActions() const
{
    return Qt::CopyAction | Qt::MoveAction;
}

Qt::DropActions ModFolderProxyModel::supportedDragActions() const
{
    return Qt::MoveAction;
}

void ModFolderProxyModel::sort(int column, Qt::SortOrder order)
{
    if (sourceModel()) {
        sourceModel()->sort(column, order);
    }
}

bool ModFolderProxyModel::isFolder(const QModelIndex& index) const
{
    return index.isValid() && (index.internalId() & ITEM_TYPE_MASK) == FOLDER_MARKER;
}

bool ModFolderProxyModel::isSeparator(const QModelIndex& index) const
{
    return index.isValid() && (index.internalId() & ITEM_TYPE_MASK) == SEPARATOR_MARKER;
}

QString ModFolderProxyModel::folderName(const QModelIndex& index) const
{
    if (!isFolder(index)) {
        return {};
    }
    const auto folder = folderIndex(index);
    return folder >= 0 && folder < m_storage->folders().size() ? m_storage->folders().at(folder).name : QString();
}

QString ModFolderProxyModel::modFileName(const QModelIndex& index) const
{
    return isFolder(index) || isSeparator(index) ? QString() : fileNameForSourceRow(sourceRow(index));
}

void ModFolderProxyModel::rebuild()
{
    beginResetModel();

    m_folderRows.clear();
    m_folderRows.resize(m_storage->folders().size());
    m_ungroupedRows.clear();

    if (sourceModel()) {
        for (int row = 0; row < sourceModel()->rowCount(); ++row) {
            const auto folder = assignedFolderForSourceRow(row);
            if (folder >= 0) {
                m_folderRows[folder].append(row);
            } else {
                m_ungroupedRows.append(row);
            }
        }
    }

    endResetModel();
}

int ModFolderProxyModel::folderIndex(const QModelIndex& index) const
{
    return isFolder(index) ? static_cast<int>(index.internalId() >> 2) : -1;
}

int ModFolderProxyModel::folderRootRow(int folder) const
{
    return folder * 2;
}

int ModFolderProxyModel::sourceRow(const QModelIndex& index) const
{
    return !isFolder(index) && !isSeparator(index) ? static_cast<int>(index.internalId() >> 2) : -1;
}

QString ModFolderProxyModel::fileNameForSourceRow(int row) const
{
    if (!sourceModel() || row < 0 || row >= sourceModel()->rowCount()) {
        return {};
    }

    const auto* proxy = qobject_cast<const QAbstractProxyModel*>(sourceModel());
    if (!proxy) {
        return {};
    }
    const auto modIndex = proxy->mapToSource(sourceModel()->index(row, 0));
    if (!modIndex.isValid() || modIndex.row() < 0 || modIndex.row() >= m_modModel->rowCount()) {
        return {};
    }
    return m_modModel->at(modIndex.row()).getOriginalFileName();
}

int ModFolderProxyModel::assignedFolderForSourceRow(int row) const
{
    const auto assignedFolder = m_storage->folderForMod(fileNameForSourceRow(row));
    if (assignedFolder.isEmpty()) {
        return -1;
    }

    for (int i = 0; i < m_storage->folders().size(); ++i) {
        if (m_storage->folders().at(i).name.compare(assignedFolder, Qt::CaseInsensitive) == 0) {
            return i;
        }
    }
    return -1;
}
