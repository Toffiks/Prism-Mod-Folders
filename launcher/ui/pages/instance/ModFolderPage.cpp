// SPDX-License-Identifier: GPL-3.0-only
/*
 *  Prism Launcher - Minecraft Launcher
 *  Copyright (c) 2022 Jamie Mansfield <jmansfield@cadixdev.org>
 *  Copyright (C) 2022 Sefa Eyeoglu <contact@scrumplex.net>
 *  Copyright (C) 2022 TheKodeToad <TheKodeToad@proton.me>
 *  Copyright (c) 2023 Trial97 <alexandru.tripon97@gmail.com>
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 3.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <https://www.gnu.org/licenses/>.
 *
 * This file incorporates work covered by the following copyright and
 * permission notice:
 *
 *      Copyright 2013-2021 MultiMC Contributors
 *
 *      Licensed under the Apache License, Version 2.0 (the "License");
 *      you may not use this file except in compliance with the License.
 *      You may obtain a copy of the License at
 *
 *          http://www.apache.org/licenses/LICENSE-2.0
 *
 *      Unless required by applicable law or agreed to in writing, software
 *      distributed under the License is distributed on an "AS IS" BASIS,
 *      WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *      See the License for the specific language governing permissions and
 *      limitations under the License.
 */

#include "ModFolderPage.h"
#include "ModFolderProxyModel.h"

#include "FileSystem.h"
#include "minecraft/mod/Resource.h"
#include "ui/dialogs/ExportToModListDialog.h"
#include "ui/dialogs/InstallLoaderDialog.h"
#include "ui_ExternalResourcesPage.h"

#include <QAbstractItemModel>
#include <QAction>
#include <QHash>
#include <QInputDialog>
#include <QLineEdit>
#include <QLocale>
#include <QMenu>
#include <QMessageBox>
#include <QScrollBar>
#include <QSortFilterProxyModel>
#include <QTimer>
#include <QTreeView>
#include <algorithm>
#include <memory>
#include <utility>

#include "Application.h"
#include "ResourceDownloadTask.h"

#include "ui/dialogs/CustomMessageBox.h"
#include "ui/dialogs/ResourceDownloadDialog.h"
#include "ui/dialogs/ResourceUpdateDialog.h"

#include "minecraft/PackProfile.h"
#include "minecraft/VersionFilterData.h"
#include "minecraft/mod/Mod.h"
#include "minecraft/mod/ModFolderModel.h"
#include "minecraft/mod/ModFolderStorage.h"

#include "tasks/ConcurrentTask.h"
#include "tasks/Task.h"
#include "ui/dialogs/ProgressDialog.h"

ModFolderPage::ModFolderPage(BaseInstance* inst, ModFolderModel* model, QWidget* parent)
    : ExternalResourcesPage(inst, model, parent), m_model(model)
{
    ui->actionDownloadItem->setText(tr("Download Mods"));
    ui->actionDownloadItem->setToolTip(tr("Download mods from online mod platforms"));
    ui->actionDownloadItem->setEnabled(true);
    ui->actionsToolbar->insertActionBefore(ui->actionAddItem, ui->actionDownloadItem);

    connect(ui->actionDownloadItem, &QAction::triggered, this, &ModFolderPage::downloadMods);

    ui->actionUpdateItem->setToolTip(tr("Try to check or update all selected mods (all mods if none are selected)"));
    connect(ui->actionUpdateItem, &QAction::triggered, this, &ModFolderPage::updateMods);
    ui->actionsToolbar->insertActionBefore(ui->actionAddItem, ui->actionUpdateItem);

    auto* updateMenu = new QMenu(this);

    auto* update = updateMenu->addAction(tr("Check for Updates"));
    connect(update, &QAction::triggered, this, &ModFolderPage::updateMods);

    updateMenu->addAction(ui->actionVerifyItemDependencies);
    connect(ui->actionVerifyItemDependencies, &QAction::triggered, this, [this] { updateMods(true); });

    auto depsDisabled = APPLICATION->settings()->getSetting("ModDependenciesDisabled");
    ui->actionVerifyItemDependencies->setVisible(!depsDisabled->get().toBool());
    connect(depsDisabled.get(), &Setting::SettingChanged, this,
            [this](const Setting&, const QVariant& value) { ui->actionVerifyItemDependencies->setVisible(!value.toBool()); });

    updateMenu->addAction(ui->actionResetItemMetadata);
    connect(ui->actionResetItemMetadata, &QAction::triggered, this, &ModFolderPage::deleteModMetadata);

    ui->actionUpdateItem->setMenu(updateMenu);

    ui->actionChangeVersion->setToolTip(tr("Change a mod's version."));
    connect(ui->actionChangeVersion, &QAction::triggered, this, &ModFolderPage::changeModVersion);
    ui->actionsToolbar->insertActionAfter(ui->actionUpdateItem, ui->actionChangeVersion);

    ui->actionViewHomepage->setToolTip(tr("View the homepages of all selected mods."));

    ui->actionExportMetadata->setToolTip(tr("Export mod's metadata to text."));
    connect(ui->actionExportMetadata, &QAction::triggered, this, &ModFolderPage::exportModMetadata);
    ui->actionsToolbar->insertActionAfter(ui->actionViewHomepage, ui->actionExportMetadata);

    ui->actionsToolbar->insertActionAfter(ui->actionViewFolder, ui->actionViewConfigs);

    auto* minecraftInstance = dynamic_cast<MinecraftInstance*>(m_instance);
    const bool isLoaderModPage = minecraftInstance && minecraftInstance->loaderModList() == model;
    if (!isLoaderModPage) {
        return;
    }

    const auto storagePath = FS::PathCombine(minecraftInstance->gameRoot(), "modfolders.json");

    m_folderStorage = std::make_unique<ModFolderStorage>(storagePath);

    QString loadError;
    if (!m_folderStorage->load(&loadError)) {
        qWarning() << "Could not load mod folders:" << loadError;
        QMessageBox::warning(this, tr("Mod Folders"), tr("Could not load mod folders:\n%1").arg(loadError));
    }

    m_folderProxy = new ModFolderProxyModel(m_filterModel, m_model, m_folderStorage.get(), this);
    connect(m_folderProxy, &ModFolderProxyModel::storageError, this,
            [this](const QString& error) { QMessageBox::warning(this, tr("Mod Folders"), error); });
    connect(ui->filterEdit, &QLineEdit::textChanged, m_folderProxy, &ModFolderProxyModel::rebuild);

    setViewModel(m_folderProxy);
    m_model->loadColumns(ui->treeView);

    ui->treeView->setRootIsDecorated(true);
    ui->treeView->setItemsExpandable(true);
    ui->treeView->setExpandsOnDoubleClick(true);
    ui->treeView->setTreePosition(ModFolderModel::NameColumn);
    ui->treeView->setUniformRowHeights(false);
    ui->treeView->setDragDropMode(QAbstractItemView::DragDrop);
    ui->treeView->setDefaultDropAction(Qt::MoveAction);
    ui->treeView->setAutoScroll(true);

    for (const auto& folder : m_folderStorage->folders()) {
        m_expandedFolders.insert(folder.name);
    }

    connect(ui->treeView, &QTreeView::expanded, this, [this](const QModelIndex& index) {
        if (!m_folderModelResetting && m_folderProxy && m_folderProxy->isFolder(index)) {
            m_expandedFolders.insert(m_folderProxy->folderName(index));
        }
    });
    connect(ui->treeView, &QTreeView::collapsed, this, [this](const QModelIndex& index) {
        if (!m_folderModelResetting && m_folderProxy && m_folderProxy->isFolder(index)) {
            m_expandedFolders.remove(m_folderProxy->folderName(index));
        }
    });
    connect(m_folderProxy, &QAbstractItemModel::modelAboutToBeReset, this, [this] {
        if (!m_folderModelResetting) {
            m_folderScrollPosition = ui->treeView->verticalScrollBar()->value();
        }
        m_folderModelResetting = true;
    });
    connect(m_folderProxy, &QAbstractItemModel::modelReset, this,
            [this] {
                if (m_folderRestorePending) {
                    return;
                }
                m_folderRestorePending = true;
                QTimer::singleShot(0, this, [this] {
                    restoreExpandedFolders();
                    auto* scrollBar = ui->treeView->verticalScrollBar();
                    scrollBar->setValue(qMin(m_folderScrollPosition, scrollBar->maximum()));
                    m_folderModelResetting = false;
                    m_folderRestorePending = false;
                });
            });

    const bool russian = QLocale().language() == QLocale::Russian;
    m_createFolderAction = new QAction(russian ? QStringLiteral("Создать папку") : tr("Create Folder"), this);
    m_createFolderAction->setToolTip(tr("Create a virtual folder for organizing mods."));
    connect(m_createFolderAction, &QAction::triggered, this, &ModFolderPage::createModFolder);
    ui->actionsToolbar->insertActionAfter(ui->actionAddItem, m_createFolderAction);

    m_renameFolderAction = new QAction(russian ? QStringLiteral("Переименовать папку") : tr("Rename Folder"), this);
    m_renameFolderAction->setToolTip(tr("Rename the selected virtual folder."));
    connect(m_renameFolderAction, &QAction::triggered, this, &ModFolderPage::renameModFolder);
    ui->actionsToolbar->insertActionAfter(m_createFolderAction, m_renameFolderAction);

    restoreExpandedFolders();
    updateActions();
}

ModFolderPage::~ModFolderPage() = default;

bool ModFolderPage::shouldDisplay() const
{
    return true;
}

void ModFolderPage::updateFrame(const QModelIndex& current, [[maybe_unused]] const QModelIndex& previous)
{
    auto sourceCurrent = mapViewToSource(current);
    if (!sourceCurrent.isValid()) {
        ui->frame->clear();
        return;
    }
    int row = sourceCurrent.row();
    const Mod& mod = m_model->at(row);
    ui->frame->updateWithMod(mod);
}

void ModFolderPage::updateActions()
{
    ExternalResourcesPage::updateActions();

    if (!m_folderProxy) {
        return;
    }

    const auto folders = selectedFolderNames();
    const bool hasFolderSelection = !folders.isEmpty();
    if (m_renameFolderAction) {
        m_renameFolderAction->setEnabled(!selectedFolderNameForRename().isEmpty());
    }

    if (hasFolderSelection) {
        ui->actionRemoveItem->setEnabled(true);
        ui->actionEnableItem->setEnabled(false);
        ui->actionDisableItem->setEnabled(false);
        ui->actionResetItemMetadata->setEnabled(false);
        ui->actionChangeVersion->setEnabled(false);
        ui->actionViewHomepage->setEnabled(false);
        ui->actionExportMetadata->setEnabled(false);
    }
}

void ModFolderPage::createModFolder()
{
    bool accepted = false;
    const auto name =
        QInputDialog::getText(this, tr("Create Folder"), tr("Folder name:"), QLineEdit::Normal, QString(), &accepted).trimmed();
    if (!accepted || name.isEmpty()) {
        return;
    }

    QString error;
    auto candidate = *m_folderStorage;
    if (!candidate.createFolder(name, &error)) {
        QMessageBox::warning(this, tr("Create Folder"), error);
        return;
    }
    if (commitModFolders(std::move(candidate))) {
        m_expandedFolders.insert(name);
        m_folderProxy->rebuild();
    }
}

void ModFolderPage::renameModFolder()
{
    const auto oldName = selectedFolderNameForRename();
    if (oldName.isEmpty()) {
        return;
    }

    bool accepted = false;
    const auto newName =
        QInputDialog::getText(this, tr("Rename Folder"), tr("Folder name:"), QLineEdit::Normal, oldName, &accepted).trimmed();
    if (!accepted || newName.isEmpty() || newName == oldName) {
        return;
    }

    QString error;
    auto candidate = *m_folderStorage;
    if (!candidate.renameFolder(oldName, newName, &error)) {
        QMessageBox::warning(this, tr("Rename Folder"), error);
        return;
    }

    if (commitModFolders(std::move(candidate))) {
        if (m_expandedFolders.remove(oldName)) {
            m_expandedFolders.insert(newName);
        }
        m_folderProxy->rebuild();
    }
}

void ModFolderPage::restoreExpandedFolders()
{
    if (!m_folderProxy) {
        return;
    }
    for (int row = 0; row < m_folderProxy->rowCount(); ++row) {
        const auto index = m_folderProxy->index(row, 0);
        if (m_folderProxy->isFolder(index)) {
            ui->treeView->setExpanded(index, m_expandedFolders.contains(m_folderProxy->folderName(index)));
        }
    }
}

void ModFolderPage::removeItem()
{
    const auto folders = selectedFolderNames();
    if (folders.isEmpty()) {
        ExternalResourcesPage::removeItem();
        return;
    }

    const auto message = folders.size() == 1
                             ? tr("Delete the folder \"%1\"?\n\nThe mods will not be deleted and will become ungrouped.").arg(folders.front())
                             : tr("Delete %1 folders?\n\nThe mods will not be deleted and will become ungrouped.").arg(folders.size());
    if (QMessageBox::question(this, tr("Delete Folder"), message, QMessageBox::Yes | QMessageBox::No, QMessageBox::No) !=
        QMessageBox::Yes) {
        return;
    }

    auto candidate = *m_folderStorage;
    for (const auto& folder : folders) {
        candidate.removeFolder(folder);
    }
    if (commitModFolders(std::move(candidate))) {
        for (const auto& folder : folders) {
            m_expandedFolders.remove(folder);
        }
        m_folderProxy->rebuild();
    }
}

void ModFolderPage::removeItems(const QItemSelection& selection)
{
    if (m_instance != nullptr && m_instance->isRunning()) {
        auto response = CustomMessageBox::selectable(this, tr("Confirm Delete"),
                                                     tr("If you remove mods while the game is running it may crash your game.\n"
                                                        "Are you sure you want to do this?"),
                                                     QMessageBox::Warning, QMessageBox::Yes | QMessageBox::No, QMessageBox::No)
                            ->exec();

        if (response != QMessageBox::Yes) {
            return;
        }
    }

    auto indexes = selection.indexes();
    auto affected = m_model->getAffectedMods(indexes, EnableAction::DISABLE);
    if (!affected.isEmpty()) {
        auto response = CustomMessageBox::selectable(this, tr("Confirm Disable"),
                                                     tr("The mods you are trying to delete are required by %1 mods.\n"
                                                        "Do you want to disable them?")
                                                         .arg(affected.length()),
                                                     QMessageBox::Warning, QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel,
                                                     QMessageBox::Cancel)
                            ->exec();

        if (response == QMessageBox::Cancel) {
            return;
        }
        if (response == QMessageBox::Yes) {
            m_model->setResourceEnabled(affected, EnableAction::DISABLE);
        }
    }
    QStringList removedFileNames;
    for (const auto& index : indexes) {
        if (index.column() == 0) {
            removedFileNames.append(m_model->at(index.row()).getOriginalFileName());
        }
    }

    if (m_model->deleteResources(indexes) && m_folderStorage) {
        auto candidate = *m_folderStorage;
        candidate.unassignMods(removedFileNames);
        if (commitModFolders(std::move(candidate))) {
            m_folderProxy->rebuild();
        }
    }
}

void ModFolderPage::downloadMods()
{
    if (m_instance->typeName() != "Minecraft") {
        return;  // this is a null instance or a legacy instance
    }

    auto* profile = static_cast<MinecraftInstance*>(m_instance)->getPackProfile();
    if (!profile->getModLoaders().has_value()) {
        if (handleNoModLoader()) {
            return;
        }
    }

    m_downloadDialog = new ResourceDownload::ModDownloadDialog(this, m_model, m_instance);
    connect(this, &QObject::destroyed, m_downloadDialog, &QDialog::close);
    connect(m_downloadDialog, &QDialog::finished, this, &ModFolderPage::downloadDialogFinished);

    m_downloadDialog->open();
}

void ModFolderPage::downloadDialogFinished(int result)
{
    if (result != 0) {
        auto* tasks = new ConcurrentTask(tr("Download Mods"), APPLICATION->settings()->get("NumberOfConcurrentDownloads").toInt());
        connect(tasks, &Task::failed, [this, tasks](const QString& reason) {
            CustomMessageBox::selectable(this, tr("Error"), reason, QMessageBox::Critical)->show();
            tasks->deleteLater();
        });
        connect(tasks, &Task::aborted, [this, tasks]() {
            CustomMessageBox::selectable(this, tr("Aborted"), tr("Download stopped by user."), QMessageBox::Information)->show();
            tasks->deleteLater();
        });
        connect(tasks, &Task::succeeded, [this, tasks]() {
            QStringList warnings = tasks->warnings();
            if (warnings.count()) {
                CustomMessageBox::selectable(this, tr("Warnings"), warnings.join('\n'), QMessageBox::Warning)->show();
            }

            tasks->deleteLater();
        });

        if (m_downloadDialog) {
            for (auto& task : m_downloadDialog->getTasks()) {
                tasks->addTask(task);
            }
        } else {
            qWarning() << "ResourceDownloadDialog vanished before we could collect tasks!";
        }

        ProgressDialog loadDialog(this);
        loadDialog.setSkipButton(true, tr("Abort"));
        loadDialog.execWithTask(tasks);

        m_model->update();
    }
    if (m_downloadDialog) {
        m_downloadDialog->deleteLater();
    }
}

void ModFolderPage::updateMods(bool includeDeps)
{
    if (m_instance->typeName() != "Minecraft") {
        return;  // this is a null instance or a legacy instance
    }

    auto* profile = static_cast<MinecraftInstance*>(m_instance)->getPackProfile();
    if (!profile->getModLoaders().has_value()) {
        if (handleNoModLoader()) {
            return;
        }
    }
    if (APPLICATION->settings()->get("ModMetadataDisabled").toBool()) {
        QMessageBox::critical(this, tr("Error"), tr("Mod updates are unavailable when metadata is disabled!"));
        return;
    }
    if (m_instance != nullptr && m_instance->isRunning()) {
        auto response =
            CustomMessageBox::selectable(this, tr("Confirm Update"),
                                         tr("Updating mods while the game is running may cause mod duplication and game crashes.\n"
                                            "The old files may not be deleted as they are in use.\n"
                                            "Are you sure you want to do this?"),
                                         QMessageBox::Warning, QMessageBox::Yes | QMessageBox::No, QMessageBox::No)
                ->exec();

        if (response != QMessageBox::Yes) {
            return;
        }
    }
    auto selection = mapViewSelectionToSource(ui->treeView->selectionModel()->selection()).indexes();

    auto modsList = m_model->selectedResources(selection);
    bool useAll = modsList.empty();
    if (useAll) {
        modsList = m_model->allResources();
    }

    ResourceUpdateDialog updateDialog(this, m_instance, m_model, modsList, includeDeps, profile->getModLoadersList());
    updateDialog.checkCandidates();

    if (updateDialog.aborted()) {
        CustomMessageBox::selectable(this, tr("Aborted"), tr("The mod updater was aborted!"), QMessageBox::Warning)->show();
        return;
    }
    if (updateDialog.noUpdates()) {
        QString message{ tr("'%1' is up-to-date! :)").arg(modsList.front()->name()) };
        if (modsList.size() > 1) {
            if (useAll) {
                message = tr("All mods are up-to-date! :)");
            } else {
                message = tr("All selected mods are up-to-date! :)");
            }
        }
        CustomMessageBox::selectable(this, tr("Update checker"), message)->exec();
        return;
    }

    if (updateDialog.exec() != 0) {
        auto* tasks = new ConcurrentTask("Download Mods", APPLICATION->settings()->get("NumberOfConcurrentDownloads").toInt());
        connect(tasks, &Task::failed, [this, tasks](const QString& reason) {
            CustomMessageBox::selectable(this, tr("Error"), reason, QMessageBox::Critical)->show();
            tasks->deleteLater();
        });
        connect(tasks, &Task::aborted, [this, tasks]() {
            CustomMessageBox::selectable(this, tr("Aborted"), tr("Download stopped by user."), QMessageBox::Information)->show();
            tasks->deleteLater();
        });
        connect(tasks, &Task::succeeded, [this, tasks]() {
            QStringList warnings = tasks->warnings();
            if (warnings.count()) {
                CustomMessageBox::selectable(this, tr("Warnings"), warnings.join('\n'), QMessageBox::Warning)->show();
            }
            tasks->deleteLater();
        });

        QHash<QString, QString> oldFileNamesByProject;
        for (const auto* resource : modsList) {
            if (!resource->metadata() || !resource->metadata()->project_id.isValid() ||
                resource->metadata()->project_id.isNull()) {
                continue;
            }
            const auto key = QStringLiteral("%1:%2")
                                 .arg(static_cast<int>(resource->metadata()->provider))
                                 .arg(resource->metadata()->project_id.toString());
            oldFileNamesByProject.insert(key, resource->getOriginalFileName());
        }

        for (const auto& task : updateDialog.getTasks()) {
            auto* downloadTask = qobject_cast<ResourceDownloadTask*>(task.get());
            if (m_folderStorage && downloadTask && downloadTask->getPack() && downloadTask->getPack()->addonId.isValid() &&
                !downloadTask->getPack()->addonId.isNull()) {
                const auto key = QStringLiteral("%1:%2")
                                     .arg(static_cast<int>(downloadTask->getProvider()))
                                     .arg(downloadTask->getPack()->addonId.toString());
                const auto oldFileName = oldFileNamesByProject.value(key);
                const auto newFileName = downloadTask->getFilename();
                if (!oldFileName.isEmpty() && !m_folderStorage->folderForMod(oldFileName).isEmpty()) {
                    connect(downloadTask, &Task::succeeded, this, [this, oldFileName, newFileName] {
                        auto candidate = *m_folderStorage;
                        if (candidate.replaceModFileName(oldFileName, newFileName) &&
                            commitModFolders(std::move(candidate))) {
                            m_folderProxy->rebuild();
                        }
                    });
                }
            }
            tasks->addTask(task);
        }

        ProgressDialog loadDialog(this);
        loadDialog.setSkipButton(true, tr("Abort"));
        loadDialog.execWithTask(tasks);

        m_model->update();
    }
}

void ModFolderPage::deleteModMetadata()
{
    auto selection = mapViewSelectionToSource(ui->treeView->selectionModel()->selection()).indexes();
    auto selectionCount = m_model->selectedMods(selection).length();
    if (selectionCount == 0) {
        return;
    }
    if (selectionCount > 1) {
        auto response = CustomMessageBox::selectable(this, tr("Confirm Removal"),
                                                     tr("You are about to remove the metadata for %1 mods.\n"
                                                        "Are you sure?")
                                                         .arg(selectionCount),
                                                     QMessageBox::Warning, QMessageBox::Yes | QMessageBox::No, QMessageBox::No)
                            ->exec();

        if (response != QMessageBox::Yes) {
            return;
        }
    }

    m_model->deleteMetadata(selection);
}

void ModFolderPage::changeModVersion()
{
    if (m_instance->typeName() != "Minecraft") {
        return;  // this is a null instance or a legacy instance
    }

    auto* profile = static_cast<MinecraftInstance*>(m_instance)->getPackProfile();
    if (!profile->getModLoaders().has_value()) {
        if (handleNoModLoader()) {
            return;
        }
    }
    if (APPLICATION->settings()->get("ModMetadataDisabled").toBool()) {
        QMessageBox::critical(this, tr("Error"), tr("Mod updates are unavailable when metadata is disabled!"));
        return;
    }
    auto selection = mapViewSelectionToSource(ui->treeView->selectionModel()->selection()).indexes();
    auto modsList = m_model->selectedMods(selection);
    if (modsList.length() != 1 || modsList[0]->metadata() == nullptr) {
        return;
    }

    m_downloadDialog = new ResourceDownload::ModDownloadDialog(this, m_model, m_instance, true);
    connect(this, &QObject::destroyed, m_downloadDialog, &QDialog::close);
    connect(m_downloadDialog, &QDialog::finished, this, &ModFolderPage::downloadDialogFinished);

    m_downloadDialog->setResourceMetadata((*modsList.begin())->metadata());
    m_downloadDialog->open();
}

void ModFolderPage::exportModMetadata()
{
    auto selection = mapViewSelectionToSource(ui->treeView->selectionModel()->selection()).indexes();
    auto selectedMods = m_model->selectedMods(selection);
    if (selectedMods.length() == 0) {
        selectedMods = m_model->allMods();
    }

    std::ranges::sort(selectedMods, [](const Mod* a, const Mod* b) { return a->name() < b->name(); });
    ExportToModListDialog dlg(m_instance->name(), selectedMods, this);
    dlg.exec();
}

QStringList ModFolderPage::selectedFolderNames() const
{
    QStringList folders;
    if (!m_folderProxy || !ui->treeView->selectionModel()) {
        return folders;
    }

    for (const auto& index : ui->treeView->selectionModel()->selectedRows(0)) {
        if (m_folderProxy->isFolder(index)) {
            const auto name = m_folderProxy->folderName(index);
            if (!folders.contains(name, Qt::CaseInsensitive)) {
                folders.append(name);
            }
        }
    }
    return folders;
}

QString ModFolderPage::selectedFolderNameForRename() const
{
    if (!m_folderProxy || !ui->treeView->selectionModel()) {
        return {};
    }

    QString selectedFolder;
    for (const auto& index : ui->treeView->selectionModel()->selectedRows(0)) {
        QModelIndex folderIndex;
        if (m_folderProxy->isFolder(index)) {
            folderIndex = index;
        } else if (m_folderProxy->isFolder(index.parent())) {
            folderIndex = index.parent();
        } else {
            return {};
        }

        const auto folderName = m_folderProxy->folderName(folderIndex);
        if (folderName.isEmpty()) {
            return {};
        }
        if (selectedFolder.isEmpty()) {
            selectedFolder = folderName;
        } else if (selectedFolder.compare(folderName, Qt::CaseInsensitive) != 0) {
            return {};
        }
    }
    return selectedFolder;
}

bool ModFolderPage::commitModFolders(ModFolderStorage&& candidate)
{
    if (!m_folderStorage) {
        return false;
    }

    QString error;
    if (!candidate.save(&error)) {
        QMessageBox::warning(this, tr("Mod Folders"), error);
        return false;
    }

    *m_folderStorage = std::move(candidate);
    return true;
}

CoreModFolderPage::CoreModFolderPage(BaseInstance* inst, ModFolderModel* mods, QWidget* parent) : ModFolderPage(inst, mods, parent)
{
    auto* mcInst = dynamic_cast<MinecraftInstance*>(m_instance);
    if (mcInst) {
        auto* version = mcInst->getPackProfile();
        if ((version != nullptr) && version->getComponent("net.minecraftforge") && version->getComponent("net.minecraft")) {
            auto minecraftCmp = version->getComponent("net.minecraft");
            if (!minecraftCmp->m_loaded) {
                version->reload(Net::Mode::Offline);
                auto update = version->getCurrentTask();
                if (update) {
                    connect(update.get(), &Task::finished, this, [this] {
                        if (m_container) {
                            m_container->refreshContainer();
                        }
                    });
                    if (!update->isRunning()) {
                        update->start();
                    }
                }
            }
        }
    }
}

bool CoreModFolderPage::shouldDisplay() const
{
    if (ModFolderPage::shouldDisplay()) {
        auto* inst = dynamic_cast<MinecraftInstance*>(m_instance);
        if (!inst) {
            return true;
        }

        auto* version = inst->getPackProfile();
        if ((version == nullptr) || !version->getComponent("net.minecraftforge") || !version->getComponent("net.minecraft")) {
            return false;
        }
        auto minecraftCmp = version->getComponent("net.minecraft");
        return minecraftCmp->m_loaded && minecraftCmp->getReleaseDateTime() < g_VersionFilterData.legacyCutoffDate;
    }
    return false;
}

NilModFolderPage::NilModFolderPage(BaseInstance* inst, ModFolderModel* mods, QWidget* parent) : ModFolderPage(inst, mods, parent) {}

bool NilModFolderPage::shouldDisplay() const
{
    return m_model->dir().exists();
}

// Helper function so this doesn't need to be duplicated 3 times
inline bool ModFolderPage::handleNoModLoader()
{
    int resp = QMessageBox::question(
        this, ModFolderPage::tr("Missing Mod Loader"),
        ModFolderPage::tr("You need to install a compatible mod loader before installing mods. Would you like to do so?"),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
    if (resp == QMessageBox::Yes) {
        // Should be safe
        auto* profile = static_cast<MinecraftInstance*>(this->m_instance)->getPackProfile();
        InstallLoaderDialog dialog(profile, QString(), this);
        bool ret = dialog.exec() != 0;
        this->m_container->refreshContainer();

        // returning negation of dialog.exec which'll be true if the install loader dialog got canceled/closed
        // and false if the user went through and installed a loader
        return !ret;
    }
    // Nothing happens the dialog is already closing
    // returning true so the caller doesn't go and continue with opening it's dialog without a mod loader
    return true;
}
