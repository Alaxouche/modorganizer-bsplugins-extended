#include "PluginListContextMenu.h"
#include "GUI/ListDialog.h"
#include "MOPlugin/Settings.h"
#include "PluginGroupProxyModel.h"
#include "PluginListModel.h"
#include "PluginListView.h"
#include "TESData/FileInfo.h"
#include "TESData/PluginList.h"

#include <utility.h>

#include <QApplication>
#include <QColorDialog>
#include <QInputDialog>
#include <QMessageBox>

#include <algorithm>
#include <limits>
#include <utility>

namespace BSPluginList
{

static bool confirmIfEnabled(QWidget* parent, const QString& title,
                             const QString& text)
{
  if (!Settings::instance()->confirmMassOperations()) {
    return true;
  }

  return QMessageBox::question(parent, title, text,
                               QMessageBox::Yes | QMessageBox::No) ==
         QMessageBox::Yes;
}

PluginListContextMenu::PluginListContextMenu(const QModelIndex& index,
                                             PluginListModel* model,
                                             PluginListView* view,
                                             MOBase::IModList* modList,
                                             MOBase::IPluginList* pluginList)
    : QMenu(view), m_Index{index}, m_Model{model}, m_View{view},
      m_TESPluginList{dynamic_cast<TESData::PluginList*>(pluginList)}
{
  m_ViewSelected = view->selectionModel()->selectedRows();
  if (!m_ViewSelected.isEmpty()) {
    m_ModelSelected = view->indexViewToModel(m_ViewSelected, model);
  } else if (index.isValid()) {
    m_ModelSelected = {index};
  }

  m_FilesSelected =
      !m_ViewSelected.isEmpty() && std::ranges::all_of(m_ViewSelected, [](auto&& idx) {
        return !idx.model()->hasChildren(idx);
      });

  m_GroupsSelected =
      !m_ViewSelected.isEmpty() && std::ranges::all_of(m_ViewSelected, [](auto&& idx) {
        return idx.model()->hasChildren(idx);
      });

  m_FilesTogglable =
      m_FilesSelected && std::ranges::all_of(m_ViewSelected, [](auto&& idx) {
        const auto plugin =
            idx.data(PluginListModel::InfoRole).value<const TESData::FileInfo*>();
        return plugin && plugin->canBeToggled();
      });

  m_FilesESM =
      m_FilesSelected && std::ranges::all_of(m_ViewSelected, [](auto&& idx) {
        const auto plugin =
            idx.data(PluginListModel::InfoRole).value<const TESData::FileInfo*>();
        return plugin && !plugin->forceLoaded() && plugin->isMasterFile();
      });

  m_FilesESP =
      m_FilesSelected && std::ranges::all_of(m_ViewSelected, [](auto&& idx) {
        const auto plugin =
            idx.data(PluginListModel::InfoRole).value<const TESData::FileInfo*>();
        return plugin && !plugin->forceLoaded() && !plugin->isMasterFile();
      });

  m_FilesMovable = m_FilesESM | m_FilesESP;

  addAllItemsMenu();
  addSelectedFilesActions();
  addSelectedGroupActions();
  addSelectionActions();
  addOriginActions(modList, pluginList);
}

void PluginListContextMenu::addAllItemsMenu()
{
  QMenu* const allItemsMenu = addMenu(tr("All Items"));

  allItemsMenu->addAction(tr("Collapse all"), [this]() {
    m_View->collapseAll();
    m_View->scrollToTop();
  });
  allItemsMenu->addAction(tr("Expand all"), [this]() {
    m_View->expandAll();
    m_View->scrollToTop();
  });

  allItemsMenu->addSeparator();

  allItemsMenu->addAction(tr("Enable all"), [this]() {
    if (confirmIfEnabled(m_View->topLevelWidget(), tr("Confirm"),
                         tr("Really enable all plugins?"))) {
      m_Model->setEnabledAll(true);
    }
  });
  allItemsMenu->addAction(tr("Disable all"), [this]() {
    if (confirmIfEnabled(m_View->topLevelWidget(), tr("Confirm"),
                         tr("Really disable all plugins?"))) {
      m_Model->setEnabledAll(false);
    }
  });
}

void PluginListContextMenu::addSelectedFilesActions()
{
  if (!m_FilesSelected)
    return;

  addSeparator();

  if (m_FilesTogglable) {
    addAction(tr("Enable selected"), [this]() {
      m_Model->setEnabled(m_ModelSelected, true);
    });
    addAction(tr("Disable selected"), [this]() {
      m_Model->setEnabled(m_ModelSelected, false);
    });
  }

  const bool allLocked = std::ranges::all_of(m_ModelSelected, [](auto&& idx) {
    const auto plugin =
        idx.data(PluginListModel::InfoRole).value<const TESData::FileInfo*>();
    return plugin && plugin->lockedOrder();
  });
  const bool anyLocked = std::ranges::any_of(m_ModelSelected, [](auto&& idx) {
    const auto plugin =
        idx.data(PluginListModel::InfoRole).value<const TESData::FileInfo*>();
    return plugin && plugin->lockedOrder();
  });

  if (anyLocked) {
    addAction(tr("Unlock load order position"), [this]() {
      m_Model->lockPlugins(m_ModelSelected, false);
    });
  }
  if (!allLocked) {
    addAction(tr("Lock load order position"), [this]() {
      m_Model->lockPlugins(m_ModelSelected, true);
    });
  }

  if (m_TESPluginList) {
    addAction(tr("ESL Capability Report..."), [this]() {
      showEslCapabilityReport();
    });
  }

  addSeparator();

  if (m_FilesSelected && m_ModelSelected.length() == 1) {
    addAction(tr("Add/Edit Note..."), [this]() {
      const auto plugin =
          m_ModelSelected.first().data(PluginListModel::InfoRole)
              .value<const TESData::FileInfo*>();
      if (!plugin)
        return;

      bool ok;
      const QString note = QInputDialog::getMultiLineText(
          m_View->topLevelWidget(), tr("Edit Plugin Note: ") + plugin->name(),
          tr("Note:"), plugin->notes(), &ok);

      if (ok) {
        m_Model->setData(
            m_ModelSelected.first().siblingAtColumn(PluginListModel::COL_NOTES), note,
            Qt::EditRole);
      }
    });
  } else if (m_FilesSelected && m_ModelSelected.length() > 1) {
    addAction(tr("Add/Edit Note (all)..."), [this]() {
      bool ok;
      const QString note = QInputDialog::getMultiLineText(
          m_View->topLevelWidget(), tr("Edit Plugin Notes"),
          tr("Note to add to all selected:"), "", &ok);

      if (ok && !note.isEmpty()) {
        for (const auto& idx : m_ModelSelected) {
          m_Model->setData(idx.siblingAtColumn(PluginListModel::COL_NOTES), note,
                           Qt::EditRole);
        }
      }
    });
  }
}

void PluginListContextMenu::addSelectedGroupActions()
{
  if (!m_GroupsSelected)
    return;

  addSeparator();

  addAction(tr("Enable Group"), [this]() {
    const auto children = m_View->indexViewToModel(m_ViewSelected, m_Model, true);
    m_Model->setEnabled(children, true);
  });
  addAction(tr("Disable Group"), [this]() {
    const auto children = m_View->indexViewToModel(m_ViewSelected, m_Model, true);
    m_Model->setEnabled(children, false);
  });

  if (m_ViewSelected.length() == 1) {
    const auto selectedIndex = m_ViewSelected.first();
    const bool expanded      = m_View->isExpanded(selectedIndex);

    addAction(tr("Collapse others"), [=, this]() {
      m_View->collapseAll();
      m_View->setExpanded(selectedIndex, expanded);
      m_View->scrollTo(selectedIndex);
    });
  }

  addSeparator();

  QStringList groupNames;
  groupNames.reserve(m_ViewSelected.length());
  for (const auto& idx : m_ViewSelected) {
    groupNames.append(idx.data(Qt::DisplayRole).toString());
  }

  addAction(groupNames.length() == 1 ? tr("Set Group Color...")
                                     : tr("Set Color of %1 Groups...")
                                           .arg(groupNames.length()),
            [groupNames, this]() {
              auto* const groupProxy =
                  qobject_cast<PluginGroupProxyModel*>(m_View->model());
              if (!groupProxy)
                return;

              const QColor current = groupProxy->groupColor(groupNames.first());
              QColorDialog dlg(current.isValid() ? current : Qt::white,
                               m_View->topLevelWidget());
              dlg.setWindowTitle(
                  groupNames.length() == 1
                      ? tr("Set Group Color: ") + groupNames.first()
                      : tr("Set Color of %1 Groups").arg(groupNames.length()));
              dlg.setOption(QColorDialog::ShowAlphaChannel);
              if (dlg.exec() == QDialog::Accepted) {
                for (const auto& groupName : groupNames) {
                  groupProxy->setGroupColor(groupName, dlg.selectedColor());
                }
              }
            });

  addAction(groupNames.length() == 1 ? tr("Clear Group Color")
                                     : tr("Clear Color of %1 Groups")
                                           .arg(groupNames.length()),
            [groupNames, this]() {
              auto* const groupProxy =
                  qobject_cast<PluginGroupProxyModel*>(m_View->model());
              if (groupProxy) {
                for (const auto& groupName : groupNames) {
                  groupProxy->setGroupColor(groupName, QColor());
                }
              }
            });
}

void PluginListContextMenu::addSelectionActions()
{
  if (m_ModelSelected.isEmpty() && !m_GroupsSelected)
    return;

  addSeparator();

  if (Settings::instance()->enablePluginGrouping()) {
    addSendToMenu();
  }

  if (m_FilesSelected && Settings::instance()->enablePluginGrouping()) {
    addAction(tr("Create Group..."), [this]() {
      bool ok;
      const QString requested =
          QInputDialog::getText(m_View->topLevelWidget(), tr("Create Group..."),
                                tr("Please enter a name:"), QLineEdit::Normal, "", &ok);

      if (!ok || requested.isEmpty())
        return;

      // Reusing an existing name would produce a second separator bound to the
      // same group, so renaming one would rename both.
      const QString group = m_Model->uniqueGroupName(requested);

      QList<QPersistentModelIndex> persistent;
      persistent.reserve(m_ViewSelected.length());
      std::ranges::transform(m_ViewSelected, std::back_inserter(persistent),
                             [](auto&& idx) {
                               return QPersistentModelIndex(idx);
                             });

      const int priority = m_ModelSelected.first()
                               .siblingAtColumn(PluginListModel::COL_PRIORITY)
                               .data()
                               .toInt();
      m_Model->sendToPriority(m_ModelSelected, priority);
      m_Model->setGroup(m_ModelSelected, group);
      for (const auto& index : persistent) {
        m_View->setExpanded(index.parent(), true);
      }
    });
  } else if (m_GroupsSelected) {
    const auto selectedGroup = m_ViewSelected.first().data().toString();

    if (m_ViewSelected.length() == 1) {
      addAction(tr("Rename Group..."), [this]() {
        const auto& selected = m_ViewSelected.first();
        const auto oldGroup  = selected.data().toString();

        bool ok;
        const QString requested = QInputDialog::getText(
            m_View->topLevelWidget(), tr("Rename Group..."), tr("Please enter a name:"),
            QLineEdit::Normal, oldGroup, &ok);

        if (!ok || requested.isEmpty() || requested == oldGroup)
          return;

        m_Model->renameGroup(oldGroup, m_Model->uniqueGroupName(requested));
      });

      addAction(tr("Merge Group Into..."), [this, selectedGroup]() {
        GUI::ListDialog dialog{*Settings::instance(), m_View->topLevelWidget()};
        dialog.setWindowTitle(tr("Merge Group Into..."));

        QStringList choices = m_Model->groups();
        choices.removeAll(selectedGroup);
        dialog.setChoices(choices);

        if (dialog.exec() != QDialog::Accepted) {
          return;
        }

        const QString targetGroup = dialog.getChoice();
        if (targetGroup.isEmpty()) {
          return;
        }

        m_Model->mergeGroup(selectedGroup, targetGroup);
      });
    }

    addAction(tr("Remove Group..."), [this]() {
      if (confirmIfEnabled(
              m_View->topLevelWidget(), tr("Confirm"),
              tr("Are you sure you want to remove \"%1\"?")
                  .arg(m_ViewSelected.first().data().toString()))) {
        m_Model->removeGroup(m_ViewSelected.first().data().toString());
      }
    });
  }
}

void PluginListContextMenu::addSendToMenu()
{
  if (!m_FilesMovable) {
    return;
  }

  QMenu* const sendToMenu = addMenu(tr("Send to... "));
  sendToMenu->addAction(tr("Top"), [this]() {
    const auto selectedIndex   = m_ViewSelected.first();
    const auto persistentIndex = QPersistentModelIndex(selectedIndex);
    m_Model->sendToPriority(m_ModelSelected, 0, true);
    m_View->scrollTo(persistentIndex);
  });
  sendToMenu->addAction(tr("Bottom"), [this]() {
    const auto selectedIndex   = m_ViewSelected.first();
    const auto persistentIndex = QPersistentModelIndex(selectedIndex);
    m_Model->sendToPriority(m_ModelSelected, std::numeric_limits<int>::max(), true);
    m_View->scrollTo(persistentIndex);
  });
  sendToMenu->addAction(tr("Priority..."), [this]() {
    const auto selectedIndex = m_ViewSelected.first();

    bool ok;
    const int newPriority =
        QInputDialog::getInt(m_View->topLevelWidget(), tr("Set Priority"),
                             tr("Set the priority of the selected plugins"), 0, 0,
                             std::numeric_limits<int>::max(), 1, &ok);
    if (!ok)
      return;

    const auto persistentIndex = QPersistentModelIndex(selectedIndex);
    m_Model->sendToPriority(m_ModelSelected, newPriority);
    m_View->scrollTo(persistentIndex);
  });
  sendToMenu->addAction(tr("Group..."), [this]() {
    sendSelectedToGroup();
  });
}

static void openOriginExplorer(const QModelIndexList& indices,
                               MOBase::IModList* modList,
                               MOBase::IPluginList* pluginList)
{
  for (const auto& idx : indices) {
    QString fileName   = idx.data().toString();
    const auto modInfo = modList->getMod(pluginList->origin(fileName));
    if (modInfo == nullptr) {
      continue;
    }
    MOBase::shell::Explore(modInfo->absolutePath());
  }
}

void PluginListContextMenu::addOriginActions(MOBase::IModList* modList,
                                             MOBase::IPluginList* pluginList)
{
  if (!m_Index.isValid())
    return;

  addSeparator();

  const auto selectedIdx =
      m_ModelSelected.length() == 1 ? m_ModelSelected.first() : m_Index;
  const auto nameIdx = selectedIdx.siblingAtColumn(PluginListModel::COL_NAME);

  if (std::ranges::any_of(m_ModelSelected, [=](auto&& idx) {
        QString fileName   = idx.data().toString();
        const auto modInfo = modList->getMod(pluginList->origin(fileName));
        return modInfo != nullptr;
      })) {
    addAction(tr("Open Origin in Explorer"), [=, this]() {
      openOriginExplorer(m_ModelSelected, modList, pluginList);
    });

    const auto fileName = nameIdx.data().toString();
    const auto modInfo  = modList->getMod(pluginList->origin(fileName));
    if (modInfo && !modInfo->isForeign()) {
      addAction(tr("Open Origin Info..."), [this, nameIdx] {
        emit openModInformation(nameIdx);
      });
    }
  }

  if (m_FilesSelected) {
    const auto pluginInfoAction =
        addAction(tr("Open Plugin Info..."), [this, nameIdx] {
          emit openPluginInformation(nameIdx);
        });
    setDefaultAction(pluginInfoAction);
  }
}

void PluginListContextMenu::showEslCapabilityReport()
{
  if (!m_TESPluginList) {
    return;
  }

  QStringList lines;

  QApplication::setOverrideCursor(Qt::WaitCursor);
  for (const auto& idx : m_ModelSelected) {
    const int id = idx.data(PluginListModel::IndexRole).toInt();
    const auto* plugin = m_TESPluginList->getPlugin(id);
    if (!plugin) {
      continue;
    }

    if (plugin->isSmallFile()) {
      lines.append(tr("%1 — already a light plugin.").arg(plugin->name()));
      continue;
    }

    // Header-only scans have no record data; parse this plugin on demand.
    if (!plugin->recordsParsed()) {
      m_TESPluginList->parsePluginRecords(id);
    }

    switch (plugin->lightCapability()) {
    case TESData::FileInfo::LightCapability::Capable:
      lines.append(tr("%1 — ESL capable, no compacting needed (%2 new records).")
                       .arg(plugin->name())
                       .arg(plugin->newRecordCount()));
      break;
    case TESData::FileInfo::LightCapability::CapableWithCompacting:
      lines.append(tr("%1 — ESL capable after compacting FormIDs in xEdit "
                      "(%2 new records).")
                       .arg(plugin->name())
                       .arg(plugin->newRecordCount()));
      break;
    case TESData::FileInfo::LightCapability::NotCapable:
      lines.append(tr("%1 — not ESL capable: %2 new records exceed the 2048 "
                      "slots of the ESL range.")
                       .arg(plugin->name())
                       .arg(plugin->newRecordCount()));
      break;
    case TESData::FileInfo::LightCapability::Unknown:
      lines.append(tr("%1 — no record data available.").arg(plugin->name()));
      break;
    }
  }
  QApplication::restoreOverrideCursor();

  if (lines.isEmpty()) {
    return;
  }

  QMessageBox box{m_View->topLevelWidget()};
  box.setWindowTitle(tr("ESL Capability Report"));
  box.setIcon(QMessageBox::Information);
  box.setText(tr("Flagging a plugin as ESL frees a load order slot. Plugins "
                 "listed as \"capable\" can take the flag in xEdit; compacting "
                 "FormIDs first is required where noted (and invalidates "
                 "existing save games referencing those FormIDs)."));
  box.setDetailedText(lines.join(QStringLiteral("\n\n")));
  box.setStandardButtons(QMessageBox::Ok);
  box.exec();
}

void PluginListContextMenu::sendSelectedToGroup()
{
  GUI::ListDialog dialog{*Settings::instance(), m_View->topLevelWidget()};
  dialog.setWindowTitle(tr("Select a group..."));
  if (m_FilesESM) {
    dialog.setChoices(m_Model->masterGroups());
  } else if (m_FilesESP) {
    dialog.setChoices(m_Model->regularGroups());
  }

  if (dialog.exec() != QDialog::Accepted) {
    return;
  }

  const QString result = dialog.getChoice();
  if (result.isEmpty()) {
    return;
  }

  m_Model->sendToGroup(m_ModelSelected, result, m_FilesESM);
}

}  // namespace BSPluginList
