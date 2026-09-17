#ifndef BSPLUGINLIST_PLUGINSWIDGET_H
#define BSPLUGINLIST_PLUGINSWIDGET_H

#include "MOPlugin/IPanelInterface.h"
#include "PluginGroupProxyModel.h"
#include "PluginListModel.h"
#include "PluginSortFilterProxyModel.h"
#include "TESData/PluginList.h"

#include <QMetaObject>
#include <QSortFilterProxyModel>
#include <QWidget>

class Ui_PluginsWidget;
class QTimer;

namespace BSPluginList
{

class PluginsWidget final : public QWidget
{
  Q_OBJECT

public:
  PluginsWidget(MOBase::IOrganizer* organizer, IPanelInterface* panelInterface,
                QWidget* parent = nullptr);

  PluginsWidget(const PluginsWidget&) = delete;
  PluginsWidget(PluginsWidget&&)      = delete;

  ~PluginsWidget() noexcept;

  PluginsWidget& operator=(const PluginsWidget&) = delete;
  PluginsWidget& operator=(PluginsWidget&&)      = delete;

  [[nodiscard]] TESData::PluginList* getPluginList() { return m_PluginList; }

  bool eventFilter(QObject* watched, QEvent* event) override;

public slots:
  void updatePluginCount();

protected:
  void changeEvent(QEvent* event) override;

private slots:
  void onGroupCollapsed(const QModelIndex& index);
  void onGroupExpanded(const QModelIndex& index);
  void onSelectionChanged();

  void onPanelActivated();

  void onSelectedOriginsChanged(const QList<QString>& origins);

  void toggleHideForceEnabled();
  void toggleIgnoreMasterConflicts();
  void displayPluginInformation(const QModelIndex& index);

  void on_pluginList_customContextMenuRequested(const QPoint& pos);
  void on_pluginList_doubleClicked(const QModelIndex& index);
  void on_pluginList_openOriginExplorer(const QModelIndex& index);
  void on_espFilterEdit_textChanged(const QString& filter);
  void on_sortButton_clicked();
  void on_resetGroupsButton_clicked();
  void on_cleanGroupsButton_clicked();
  void on_restoreButton_clicked();
  void on_saveButton_clicked();

private:
  void updateGroupActionVisibility();
  void applyGroupingSetting();
  void applyConflictManagementSetting();
  void saveScrollPosition() const;
  void restoreScrollPosition();
  void refreshPluginListPreservingScroll();
  [[nodiscard]] bool selectedSingleGroup(QString* groupName = nullptr) const;
  void renameSelectedGroup();
  void removeSelectedGroup();
  void mergeSelectedGroup();
  [[nodiscard]] QMenu* listOptionsMenu();
  void saveState();
  void restoreState();

  void onModStateChanged(const std::map<QString, MOBase::IModList::ModStates>& mods);
  bool onAboutToRun(const QString& binary);
  void onFinishedRun(const QString& binary, unsigned int exitCode);
  void onSettingChanged(const QString& key, const QVariant& oldValue,
                        const QVariant& newValue);
  void checkLoadOrderChanged(const QString& binaryName);
  void importLootGroups();
  [[nodiscard]] bool confirmMassOperation(const QString& text) const;


  void synchronizePluginLists(MOBase::IOrganizer* organizer);

  Ui_PluginsWidget* ui;

  QMenu* optionsMenu           = nullptr;
  QAction* toggleForceEnabled  = nullptr;
  QAction* toggleIgnoreMasters = nullptr;
  QAction* resetGroupsAction   = nullptr;
  QAction* cleanGroupsAction   = nullptr;

  TESData::PluginList* m_PluginList       = nullptr;
  PluginListModel* m_PluginListModel      = nullptr;
  PluginSortFilterProxyModel* m_SortProxy = nullptr;
  PluginGroupProxyModel* m_GroupProxy     = nullptr;
  IPanelInterface* m_PanelInterface;

  MOBase::IOrganizer* m_Organizer;

  bool m_DidUpdateMasterList   = false;
  bool m_OrganizerRefreshing   = false;
  bool m_IsRunningApp          = false;
  bool m_DeferPostLootRefresh  = false;
  bool m_ExternalStatesChanged = false;
  bool m_ModStateRefreshPending = false;
  bool m_RestoringExpandState   = false;
  // The plugin info dialog holds raw pointers into the conflict tree, which a
  // refresh tears down and rebuilds. Refreshes are held back while it is up.
  bool m_PluginInfoOpen         = false;
  bool m_RefreshDeferred        = false;
  int m_PendingScrollPosition  = -1;
  QTimer* m_WriteTimer         = nullptr;
  // "Expand all" emits one signal per group and each save walks the whole
  // model; coalesce them into a single write.
  QTimer* m_ExpandStateTimer   = nullptr;
  QMetaObject::Connection m_ViewSelectionChangedConnection;
};

}  // namespace BSPluginList

#endif  // TESDATA_PLUGINSWIDGET_H
