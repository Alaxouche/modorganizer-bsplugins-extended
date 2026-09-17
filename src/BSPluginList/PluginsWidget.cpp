#include "PluginsWidget.h"

#include "BSPluginInfo/PluginInfoDialog.h"
#include "GUI/ListDialog.h"
#include "GUI/MessageDialog.h"
#include "GUI/SelectionDialog.h"
#include "MOPlugin/Settings.h"
#include "MOTools/Loot.h"
#include "MOTools/LootGroups.h"
#include "PluginListContextMenu.h"
#include "PluginSortFilterProxyModel.h"
#include "ui_pluginswidget.h"

#include <game_features/igamefeatures.h>

#include <boost/range/adaptor/reversed.hpp>

#include <QApplication>
#include <QCryptographicHash>
#include <QInputDialog>
#include <QMenu>
#include <QMessageBox>
#include <QMouseEvent>
#include <QScrollBar>
#include <QShortcut>
#include <QStandardPaths>
#include <QTimer>
#include <QToolTip>

using namespace Qt::Literals::StringLiterals;

namespace BSPluginList
{

PluginsWidget::PluginsWidget(MOBase::IOrganizer* organizer,
                             IPanelInterface* panelInterface, QWidget* parent)
    : QWidget(parent), ui{new Ui_PluginsWidget()}, m_PanelInterface{panelInterface},
      m_Organizer{organizer}
{
  ui->setupUi(this);

  m_PluginList      = new TESData::PluginList(organizer);
  m_PluginListModel = new PluginListModel(m_PluginList);
  m_SortProxy       = new PluginSortFilterProxyModel();
  m_SortProxy->setSourceModel(m_PluginListModel);
  m_GroupProxy = new PluginGroupProxyModel(organizer);
  m_GroupProxy->setSourceModel(m_SortProxy);
  ui->pluginList->setModel(m_SortProxy);
  ui->pluginList->setup();
  ui->pluginList->sortByColumn(PluginListModel::COL_PRIORITY, Qt::AscendingOrder);
  optionsMenu = listOptionsMenu();
  ui->listOptionsBtn->setMenu(optionsMenu);

  ui->sortButton->setVisible(Settings::instance()->enableSortButton());
  updateGroupActionVisibility();

    auto* const sortShortcut = new QShortcut(QKeySequence(tr("Ctrl+Shift+S")), this);
    sortShortcut->setContext(Qt::WidgetWithChildrenShortcut);
    connect(sortShortcut, &QShortcut::activated, this,
      &PluginsWidget::on_sortButton_clicked);

    auto* const cleanShortcut =
        new QShortcut(QKeySequence(tr("Ctrl+Shift+G")), this);
    cleanShortcut->setContext(Qt::WidgetWithChildrenShortcut);
    connect(cleanShortcut, &QShortcut::activated, this,
      &PluginsWidget::on_cleanGroupsButton_clicked);

    auto* const resetShortcut =
        new QShortcut(QKeySequence(tr("Ctrl+Alt+G")), this);
    resetShortcut->setContext(Qt::WidgetWithChildrenShortcut);
    connect(resetShortcut, &QShortcut::activated, this,
      &PluginsWidget::on_resetGroupsButton_clicked);

    auto* const renameGroupShortcut = new QShortcut(QKeySequence(Qt::Key_F2), this);
    renameGroupShortcut->setContext(Qt::WidgetWithChildrenShortcut);
    connect(renameGroupShortcut, &QShortcut::activated, this,
      &PluginsWidget::renameSelectedGroup);

    auto* const removeGroupShortcut =
        new QShortcut(QKeySequence::Delete, ui->pluginList);
    removeGroupShortcut->setContext(Qt::WidgetWithChildrenShortcut);
    connect(removeGroupShortcut, &QShortcut::activated, this,
      &PluginsWidget::removeSelectedGroup);

    auto* const mergeGroupShortcut =
        new QShortcut(QKeySequence(tr("Ctrl+Shift+M")), this);
    mergeGroupShortcut->setContext(Qt::WidgetWithChildrenShortcut);
    connect(mergeGroupShortcut, &QShortcut::activated, this,
      &PluginsWidget::mergeSelectedGroup);

  if (Settings::instance()->autoCleanGroupSeparatorsOnStartup()) {
    m_PluginListModel->cleanEmptyGroups();
  }


  topLevelWidget()->installEventFilter(this);
  ui->activePluginsCounter->installEventFilter(this);
  ui->activePluginsCounter->setCursor(Qt::PointingHandCursor);

  restoreState();

  connect(m_PluginList, &TESData::PluginList::pluginsListChanged, this,
          &PluginsWidget::updatePluginCount);

  connect(m_PluginListModel, &PluginListModel::pluginStatesChanged, ui->pluginList,
          &PluginListView::updateOverwriteMarkers);
  connect(m_PluginListModel, &PluginListModel::pluginOrderChanged, ui->pluginList,
          &PluginListView::updateOverwriteMarkers);
  connect(m_PluginListModel, &QAbstractItemModel::modelReset, ui->pluginList,
          &PluginListView::clearOverwriteMarkers);

  connect(m_PluginListModel, &PluginListModel::pluginStatesChanged, this,
          &PluginsWidget::updatePluginCount);

  m_WriteTimer = new QTimer(this);
  m_WriteTimer->setSingleShot(true);
  m_WriteTimer->setInterval(500);
  connect(m_WriteTimer, &QTimer::timeout, this, [this]() {
    if (m_IsRunningApp || m_PluginList->isRefreshing()) {
      return;
    }
    m_PluginList->writePluginLists();
  });

  connect(m_PluginListModel, &QAbstractItemModel::dataChanged, this,
          [this](const QModelIndex&, const QModelIndex&, const QList<int>&) {
            if (m_IsRunningApp || m_PluginList->isRefreshing()) {
              return;
            }

            // Coalesce bursts of edits and keep the disk write - which rescans
            // every record to rebuild the ignored-records file - off the
            // interactive path. Flushed explicitly on run/save/close.
            m_WriteTimer->start();
          });

  // The context object matters: without it the connection outlives this
  // widget and the lambda dereferences a freed ui during panel teardown.
  //
  // The restore itself MUST be deferred: this reset fires synchronously in
  // the middle of applyGroupingSetting() (before the view has switched
  // models) and in the middle of source-model reset cascades. Walking the
  // proxy chain at that point reads half-updated mappings and crashed MO2
  // when toggling the grouping setting.
  connect(m_GroupProxy, &QAbstractItemModel::modelReset, this, [this]() {
    QTimer::singleShot(0, this, [this]() {
      if (Settings::instance()->enablePluginGrouping() &&
          ui->pluginList->model() == m_GroupProxy) {
        m_RestoringExpandState = true;
        Settings::instance()->restoreTreeExpandState(ui->pluginList);
        m_RestoringExpandState = false;
      }

      restoreScrollPosition();
    });
  });

  m_ExpandStateTimer = new QTimer(this);
  m_ExpandStateTimer->setSingleShot(true);
  m_ExpandStateTimer->setInterval(200);
  connect(m_ExpandStateTimer, &QTimer::timeout, this, [this]() {
    if (!m_RestoringExpandState && Settings::instance()->enablePluginGrouping()) {
      Settings::instance()->saveTreeExpandState(ui->pluginList);
    }
  });

  connect(ui->pluginList, &QTreeView::collapsed, this,
          &PluginsWidget::onGroupCollapsed);
  connect(ui->pluginList, &QTreeView::expanded, this, &PluginsWidget::onGroupExpanded);

  panelInterface->onPanelActivated(
      std::bind_front(&PluginsWidget::onPanelActivated, this));
  panelInterface->onSelectedOriginsChanged(
      std::bind_front(&PluginsWidget::onSelectedOriginsChanged, this));

  organizer->onAboutToRun(std::bind_front(&PluginsWidget::onAboutToRun, this));
  organizer->onFinishedRun(std::bind_front(&PluginsWidget::onFinishedRun, this));

  organizer->modList()->onModStateChanged(
      std::bind_front(&PluginsWidget::onModStateChanged, this));

  Settings::instance()->onSettingChanged(
      std::bind_front(&PluginsWidget::onSettingChanged, this));

  synchronizePluginLists(organizer);
  updatePluginCount();
}

PluginsWidget::~PluginsWidget() noexcept
{
  // Tear the model chain down top-first and before the ui: destroying a source
  // model resets its proxies, and those resets must not reach a deleted view
  // or the ui struct. The view survives (child widget) and handles model
  // destruction on its own.
  disconnect(m_GroupProxy, nullptr, nullptr, nullptr);
  delete m_GroupProxy;
  delete m_SortProxy;
  delete m_PluginListModel;
  delete m_PluginList;

  delete optionsMenu;
  delete ui;
}

void PluginsWidget::updatePluginCount()
{
  int activeMasterCount      = 0;
  int activeLightMasterCount = 0;
  int activeOverlayCount     = 0;
  int activeMediumCount      = 0;
  int mediumCount            = 0;
  int activeRegularCount     = 0;
  int masterCount            = 0;
  int lightMasterCount       = 0;
  int overlayCount           = 0;
  int regularCount           = 0;
  int activeVisibleCount     = 0;
  int conflictCount          = 0;

    const auto gameFeatures = m_Organizer->gameFeatures();
    const auto tesSupport = gameFeatures ? gameFeatures->gameFeature<MOBase::GamePlugins>() : nullptr;

  const bool lightPluginsAreSupported =
      tesSupport && tesSupport->lightPluginsAreSupported();
  const bool mediumPluginsAreSupported =
      tesSupport && tesSupport->mediumPluginsAreSupported();
  const bool conflictManagementEnabled =
      Settings::instance()->enablePluginConflictManagement();

  for (int i = 0, count = m_PluginListModel->rowCount(); i < count; ++i) {
    const auto index = m_PluginListModel->index(i, 0);
    const auto id    = index.data(PluginListModel::IndexRole).toInt();
    const auto info  = m_PluginList->getPlugin(id);

    if (!info)
      continue;

    const bool active  = info->enabled() || info->isAlwaysEnabled();
    const bool visible = m_SortProxy->filterAcceptsRow(index.row(), index.parent());
    if (conflictManagementEnabled &&
        info->conflictState() != TESData::FileInfo::CONFLICT_NONE)
      ++conflictCount;
    if (info->isSmallFile()) {
      ++lightMasterCount;
      activeLightMasterCount += active ? 1 : 0;
      activeVisibleCount += visible && active ? 1 : 0;
    } else if (mediumPluginsAreSupported && info->isMediumFile()) {
      ++mediumCount;
      activeMediumCount += active ? 1 : 0;
      activeVisibleCount += visible && active ? 1 : 0;
    } else if (info->isMasterFile()) {
      ++masterCount;
      activeMasterCount += active ? 1 : 0;
      activeVisibleCount += visible && active ? 1 : 0;
    } else if (info->isOverlayFlagged()) {
      ++overlayCount;
      activeOverlayCount += active ? 1 : 0;
      activeVisibleCount += visible && active ? 1 : 0;
    } else {
      ++regularCount;
      activeRegularCount += active ? 1 : 0;
      activeVisibleCount += visible && active ? 1 : 0;
    }
  }

  const int activeCount = activeMasterCount + activeLightMasterCount +
                          activeMediumCount + activeOverlayCount + activeRegularCount;
  const int totalCount =
      masterCount + lightMasterCount + mediumCount + overlayCount + regularCount;

  ui->activePluginsCounter->display(activeVisibleCount);

  QString toolTip;
  toolTip.reserve(575);
  toolTip += uR"(<table cellspacing="6">)"_s
             uR"(<tr><th>%1</th><th>%2</th><th>%3</th></tr>)"_s.arg(tr("Type"))
                 .arg(tr("Active"), -12)
                 .arg(tr("Total"));

  const QString row = uR"(<tr><td>%1:</td><td align=right>%2    </td>)"_s
                      uR"(<td align=right>%3</td></tr>)"_s;

  toolTip += row.arg(tr("All plugins")).arg(activeCount).arg(totalCount);
  toolTip += row.arg(tr("ESMs")).arg(activeMasterCount).arg(masterCount);
  toolTip += row.arg(tr("ESPs")).arg(activeRegularCount).arg(regularCount);
  toolTip += row.arg(tr("ESMs+ESPs"))
                 .arg(activeMasterCount + activeRegularCount)
                 .arg(masterCount + regularCount);
  if (lightPluginsAreSupported)
    toolTip += row.arg(tr("ESLs")).arg(activeLightMasterCount).arg(lightMasterCount);
  if (mediumPluginsAreSupported)
    toolTip += row.arg(tr("ESHs")).arg(activeMediumCount).arg(mediumCount);
  if (Settings::instance()->enablePluginConflictManagement() && conflictCount > 0)
    toolTip +=
        uR"(<tr><td>%1:</td><td align=right colspan=2>%2</td></tr>)"_s.arg(
            tr("Conflicting plugins"), QString::number(conflictCount));
  toolTip += uR"(</table>)"_s;

  ui->activePluginsCounter->setToolTip(toolTip);
}

void PluginsWidget::on_espFilterEdit_textChanged(const QString& filter)
{
  m_SortProxy->updateFilter(filter);

  if (!filter.isEmpty()) {
    setStyleSheet("QTreeView { border: 2px ridge #f00; }");
    ui->activePluginsCounter->setStyleSheet("QLCDNumber { border: 2px ridge #f00; }");
  } else {
    setStyleSheet("");
    ui->activePluginsCounter->setStyleSheet("");
  }
  updatePluginCount();
}

bool PluginsWidget::eventFilter(QObject* watched, QEvent* event)
{
  if (watched == ui->activePluginsCounter &&
      event->type() == QEvent::MouseButtonRelease) {
    auto* const mouseEvent = static_cast<QMouseEvent*>(event);
    if (mouseEvent->button() == Qt::LeftButton) {
      QToolTip::showText(ui->activePluginsCounter->mapToGlobal(
                             mouseEvent->position().toPoint()),
                         ui->activePluginsCounter->toolTip(),
                         ui->activePluginsCounter);
      return true;
    }
  }

  if (event->type() == QEvent::Close) {
    saveState();
    if (m_WriteTimer) {
      m_WriteTimer->stop();
    }
    m_PluginList->writePluginLists();
  }

  return QWidget::eventFilter(watched, event);
}

void PluginsWidget::changeEvent(QEvent* event)
{
  QWidget::changeEvent(event);
  switch (event->type()) {
  case QEvent::LanguageChange:
    ui->retranslateUi(this);
    break;
  default:
    break;
  }
}

void PluginsWidget::onGroupCollapsed(const QModelIndex& index)
{
  // Saving while restoreTreeExpandState() is expanding rows would overwrite
  // the persisted list with a partially-restored one.
  if (!m_RestoringExpandState && Settings::instance()->enablePluginGrouping()) {
    m_ExpandStateTimer->start();
  }

  if (ui->pluginList->selectionModel()->isSelected(index)) {
    onSelectionChanged();
  }
}

void PluginsWidget::onGroupExpanded(const QModelIndex& index)
{
  if (!m_RestoringExpandState && Settings::instance()->enablePluginGrouping()) {
    m_ExpandStateTimer->start();
  }

  if (ui->pluginList->selectionModel()->isSelected(index)) {
    onSelectionChanged();
  }
}

void PluginsWidget::onSelectionChanged()
{
  QList<QString> selectedFiles;
  std::function<void(const QModelIndex&)> addFiles;
  addFiles = [&](const QModelIndex& index) {
    if (index.model()->hasChildren(index)) {
      if (ui->pluginList->isExpanded(index)) {
        return;
      }

      for (int i = 0, count = index.model()->rowCount(index); i < count; ++i) {
        addFiles(index.model()->index(i, 0, index));
      }
    } else {
      selectedFiles.append(index.data(Qt::DisplayRole).toString());
    }
  };

  for (const auto& index : ui->pluginList->selectionModel()->selectedRows()) {
    addFiles(index);
  }

  m_PanelInterface->setSelectedFiles(selectedFiles);
}

void PluginsWidget::onPanelActivated()
{
  if (m_DeferPostLootRefresh) {
    m_DeferPostLootRefresh = false;
    QTimer::singleShot(1000, this, [this]() {
      refreshPluginListPreservingScroll();
    });
  }
}

void PluginsWidget::onSelectedOriginsChanged(const QList<QString>& origins)
{
  ui->pluginList->setHighlightedOrigins(origins);
}

void PluginsWidget::toggleHideForceEnabled()
{
  const bool doHide = toggleForceEnabled->isChecked();
  m_SortProxy->hideForceEnabledFiles(doHide);
  updatePluginCount();

  Settings::instance()->set("hide_force_enabled", doHide);
}

void PluginsWidget::toggleIgnoreMasterConflicts()
{
  if (!Settings::instance()->enablePluginConflictManagement()) {
    return;
  }

  const bool doIgnore = toggleIgnoreMasters->isChecked();
  Settings::instance()->set("ignore_master_conflicts", doIgnore);

  m_PluginListModel->invalidateConflicts();
}

constexpr auto PATTERN_BACKUP_GLOB  = R"/(.????_??_??_??_??_??)/";
constexpr auto PATTERN_BACKUP_REGEX = R"/(\.(\d\d\d\d_\d\d_\d\d_\d\d_\d\d_\d\d))/";
constexpr auto PATTERN_BACKUP_DATE  = R"/(yyyy_MM_dd_hh_mm_ss)/";

static QString queryRestore(const QString& filePath, QWidget* parent = nullptr)
{
  QFileInfo pluginFileInfo(filePath);
  QString pattern     = pluginFileInfo.fileName() + ".*";
  QFileInfoList files = pluginFileInfo.absoluteDir().entryInfoList(
      QStringList(pattern), QDir::Files, QDir::Name);

  GUI::SelectionDialog dialog(QObject::tr("Choose backup to restore"), parent);
  QRegularExpression exp(QRegularExpression::anchoredPattern(pluginFileInfo.fileName() +
                                                             PATTERN_BACKUP_REGEX));
  QRegularExpression exp2(
      QRegularExpression::anchoredPattern(pluginFileInfo.fileName() + "\\.(.*)"));
  for (const QFileInfo& info : boost::adaptors::reverse(files)) {
    auto match  = exp.match(info.fileName());
    auto match2 = exp2.match(info.fileName());
    if (match.hasMatch()) {
      QDateTime time = QDateTime::fromString(match.captured(1), PATTERN_BACKUP_DATE);
      dialog.addChoice(time.toString(), "", match.captured(1));
    } else if (match2.hasMatch()) {
      dialog.addChoice(match2.captured(1), "", match2.captured(1));
    }
  }

  if (dialog.numChoices() == 0) {
    QMessageBox::information(parent, QObject::tr("No Backups"),
                             QObject::tr("There are no backups to restore"));
    return QString();
  }

  if (dialog.exec() == QDialog::Accepted) {
    return dialog.getChoiceData().toString();
  } else {
    return QString();
  }
}

void PluginsWidget::displayPluginInformation(const QModelIndex& index)
{
  const int id     = index.data(PluginListModel::IndexRole).toInt();
  const auto* info = m_PluginList->getPlugin(id);
  if (!info) {
    return;
  }
  const auto fileName = info->name();

  // Records are scanned up-front only when conflict management is enabled.
  // Otherwise parse this plugin on demand so the dialog can show its records.
  if (!info->recordsParsed()) {
    QApplication::setOverrideCursor(Qt::WaitCursor);
    m_PluginList->parsePluginRecords(id);
    QApplication::restoreOverrideCursor();
  }

  const auto parent = topLevelWidget();
  {
    BSPluginInfo::PluginInfoDialog dialog{m_Organizer, m_PluginList, fileName, parent};
    m_PluginInfoOpen = true;
    dialog.exec();
    m_PluginInfoOpen = false;
  }

  const bool ignoreMasters =
      Settings::instance()->get<bool>("ignore_master_conflicts", false);
  toggleIgnoreMasters->setChecked(ignoreMasters);
  m_PluginListModel->invalidateConflicts();

  if (m_RefreshDeferred) {
    m_RefreshDeferred = false;
    refreshPluginListPreservingScroll();
  }
}

void PluginsWidget::on_pluginList_customContextMenuRequested(const QPoint& pos)
{
  PluginListContextMenu menu{ui->pluginList->indexAt(pos), m_PluginListModel,
                             ui->pluginList, m_Organizer->modList(), m_PluginList};

  connect(&menu, &PluginListContextMenu::openModInformation,
          [this](const QModelIndex& index) {
            const int id        = index.data(PluginListModel::IndexRole).toInt();
            const auto* info    = m_PluginList->getPlugin(id);
            if (!info) {
              return;
            }
            m_PanelInterface->displayOriginInformation(info->name());
          });

  connect(&menu, &PluginListContextMenu::openPluginInformation, this,
          &PluginsWidget::displayPluginInformation);

  const QPoint p = ui->pluginList->viewport()->mapToGlobal(pos);
  menu.exec(p);
}

void PluginsWidget::on_pluginList_doubleClicked(const QModelIndex& index)
{
  const int column = index.column();


  if (column == PluginListModel::COL_NOTES) {
    ui->pluginList->edit(index);
    return;
  }

  bool ok;
  const int id = index.data(PluginListModel::IndexRole).toInt(&ok);
  if (ok) {
    Qt::KeyboardModifiers modifiers = QApplication::queryKeyboardModifiers();
    if (modifiers.testFlag(Qt::ControlModifier)) {

      const auto origin  = m_PluginList->getOriginName(id);
      const auto modInfo = m_Organizer->modList()->getMod(origin);

      if (modInfo) {
        MOBase::shell::Explore(modInfo->absolutePath());
      }
    } else if (Settings::instance()->doubleClickOpensPluginInfo()) {

      displayPluginInformation(index);
    } else {

      const auto plugin = m_PluginList->getPlugin(id);
      if (plugin) {
        m_PanelInterface->displayOriginInformation(plugin->name());
      }
    }
  } else if (ui->pluginList->model()->hasChildren(index)) {

    ui->pluginList->setExpanded(index, !ui->pluginList->isExpanded(index));
  }
}

void PluginsWidget::on_pluginList_openOriginExplorer(const QModelIndex& index)
{
  const int id       = index.data(PluginListModel::IndexRole).toInt();
  const auto origin  = m_PluginList->getOriginName(id);
  const auto modInfo = m_Organizer->modList()->getMod(origin);

  if (modInfo == nullptr) {
    return;
  }

  MOBase::shell::Explore(modInfo->absolutePath());
}

void PluginsWidget::on_sortButton_clicked()
{
  const auto logLevel = Settings::instance()->lootLogLevel();
  const bool offline  = Settings::instance()->offlineMode();

  auto r = QMessageBox::No;

  if (offline) {
    r = QMessageBox::question(topLevelWidget(), tr("Sorting plugins"),
                              tr("Are you sure you want to sort your plugins list?") +
                                  "\r\n\r\n" +
                                  tr("Note: You are currently in offline mode and LOOT "
                                     "will not update the master list."),
                              QMessageBox::Yes | QMessageBox::No);
  } else {
    r = QMessageBox::question(topLevelWidget(), tr("Sorting plugins"),
                              tr("Are you sure you want to sort your plugins list?"),
                              QMessageBox::Yes | QMessageBox::No);
  }

  if (r != QMessageBox::Yes) {
    return;
  }

  // LOOT reads the load order from disk; make sure pending edits are flushed.
  if (m_WriteTimer) {
    m_WriteTimer->stop();
  }
  m_PluginList->writePluginLists();

  const bool didUpdateMasterList = offline ? true : m_DidUpdateMasterList;

  if (MOTools::runLoot(topLevelWidget(), m_Organizer, m_PluginList, logLevel,
                       didUpdateMasterList, *Settings::instance())) {

    if (!offline) {
      m_DidUpdateMasterList = true;
    }

    importLootGroups();
    m_PluginListModel->invalidate();
  }
}

void PluginsWidget::on_resetGroupsButton_clicked()
{
  if (!Settings::instance()->enablePluginGrouping() ||
      !Settings::instance()->enableResetGroupsButton()) {
    return;
  }

  if (!confirmMassOperation(
          tr("Reset all groups and separators? Plugin load order will stay unchanged."))) {
    return;
  }

  m_PluginListModel->resetGroupsStructure();
}

void PluginsWidget::on_cleanGroupsButton_clicked()
{
  if (!Settings::instance()->enablePluginGrouping() ||
      !Settings::instance()->enableCleanGroupsButton()) {
    return;
  }

  m_PluginListModel->cleanEmptyGroups();
}

static bool tryRestore(const QString& filePath, const QString& identifier,
                       bool required, QWidget* parent = nullptr)
{
  const auto backupName = filePath + "." + identifier;
  if (required || QFileInfo::exists(backupName)) {
    return MOBase::shellCopy(backupName, filePath, true, parent);
  } else {
    return !QFileInfo::exists(filePath) || MOBase::shellDeleteQuiet(filePath, parent);
  }
}

void PluginsWidget::on_restoreButton_clicked()
{
  const auto app         = this->topLevelWidget();
  const auto profilePath = QDir(m_Organizer->profilePath());
  const auto pluginsName = QDir::cleanPath(profilePath.absoluteFilePath("plugins.txt"));

  QString choice = queryRestore(pluginsName, app);
  if (!choice.isEmpty()) {
    const auto groupsName =
        QDir::cleanPath(profilePath.absoluteFilePath("plugingroups.txt"));
    const auto loadOrderName =
        QDir::cleanPath(profilePath.absoluteFilePath("loadorder.txt"));

    if (!tryRestore(pluginsName, choice, true, app) ||
        !tryRestore(loadOrderName, choice, true, app) ||
        !tryRestore(groupsName, choice, false, app)) {
      const auto e = ::GetLastError();

      QMessageBox::critical(
          this, tr("Restore failed"),
          tr("Failed to restore the backup. Errorcode: %1")
              .arg(QString::fromStdWString(MOBase::formatSystemMessage(e))));
    }
    m_PluginListModel->invalidate();
  }
}

static bool createBackup(const QString& filePath, const QString& identifier,
                         QWidget* parent = nullptr)
{
  QString outPath = filePath + "." + identifier;
  if (MOBase::shellCopy(QStringList(filePath), QStringList(outPath), parent)) {
    QFileInfo fileInfo(filePath);
    MOBase::removeOldFiles(fileInfo.absolutePath(),
                           fileInfo.fileName() + PATTERN_BACKUP_GLOB, 10, QDir::Name);
    return true;
  } else {
    return false;
  }
}

static bool createBackup(const QString& filePath, const QDateTime& time,
                         QWidget* parent = nullptr)
{
  return createBackup(filePath, time.toString(PATTERN_BACKUP_DATE), parent);
}

void PluginsWidget::on_saveButton_clicked()
{
  if (m_WriteTimer) {
    m_WriteTimer->stop();
  }
  m_PluginList->writePluginLists();

  const auto app         = this->topLevelWidget();
  const auto profilePath = QDir(m_Organizer->profilePath());
  const auto pluginsName = QDir::cleanPath(profilePath.absoluteFilePath("plugins.txt"));
  const auto groupsName =
      QDir::cleanPath(profilePath.absoluteFilePath("plugingroups.txt"));
  const auto loadOrderName =
      QDir::cleanPath(profilePath.absoluteFilePath("loadorder.txt"));
  const auto lockedOrderName =
      QDir::cleanPath(profilePath.absoluteFilePath("lockedorder.txt"));

  const QDateTime now = QDateTime::currentDateTime();

  if (createBackup(pluginsName, now, app) && createBackup(loadOrderName, now, app) &&
      createBackup(groupsName, now, app) && createBackup(lockedOrderName, now, app)) {
    GUI::MessageDialog::showMessage(tr("Backup of load order created"), app);
  }
}

QMenu* PluginsWidget::listOptionsMenu()
{
  QMenu* const menu  = new QMenu(this);
  toggleForceEnabled = menu->addAction(tr("Hide force-enabled files"), this,
                                       &PluginsWidget::toggleHideForceEnabled);
  toggleForceEnabled->setCheckable(true);

  toggleIgnoreMasters = menu->addAction(tr("Ignore conflicts with masters"), this,
                                        &PluginsWidget::toggleIgnoreMasterConflicts);
  toggleIgnoreMasters->setCheckable(true);

  menu->addSeparator();

  menu->addAction(tr("Collapse all"), [this]() {
    ui->pluginList->collapseAll();
    ui->pluginList->scrollToTop();
  });
  menu->addAction(tr("Expand all"), [this]() {
    ui->pluginList->expandAll();
    ui->pluginList->scrollToTop();
  });

  menu->addSeparator();

  menu->addAction(tr("Enable all"), [this]() {
    if (confirmMassOperation(tr("Really enable all plugins?"))) {
      m_PluginListModel->setEnabledAll(true);
    }
  });
  menu->addAction(tr("Disable all"), [this]() {
    if (confirmMassOperation(tr("Really disable all plugins?"))) {
      m_PluginListModel->setEnabledAll(false);
    }
  });

  menu->addSeparator();
  resetGroupsAction = menu->addAction(tr("Reset Group Structure"), [this]() {
    if (confirmMassOperation(tr(
            "Reset all groups and separators? Plugin load order will stay unchanged."))) {
      m_PluginListModel->resetGroupsStructure();
    }
  });
  cleanGroupsAction = menu->addAction(tr("Clean Groups"), [this]() {
    m_PluginListModel->cleanEmptyGroups();
  });

  updateGroupActionVisibility();

  return menu;
}

void PluginsWidget::saveState()
{
  auto* const settings = Settings::instance();
  saveScrollPosition();
  settings->saveState(ui->pluginList->header());
  if (settings->enablePluginGrouping()) {
    // Flush whatever the coalescing timer still owes before writing.
    if (m_ExpandStateTimer) {
      m_ExpandStateTimer->stop();
    }
    settings->saveTreeExpandState(ui->pluginList);
  }
}

void PluginsWidget::restoreState()
{
  const auto* const settings = Settings::instance();
  settings->restoreState(ui->pluginList->header());
  applyGroupingSetting();

  if (settings->enablePluginGrouping()) {
    m_RestoringExpandState = true;
    settings->restoreTreeExpandState(ui->pluginList);
    m_RestoringExpandState = false;
  }

  const bool doHide = settings->get<bool>("hide_force_enabled", false);
  toggleForceEnabled->setChecked(doHide);
  toggleHideForceEnabled();

  const bool doIgnore = settings->get<bool>("ignore_master_conflicts", false);
  toggleIgnoreMasters->setChecked(doIgnore);
  toggleIgnoreMasterConflicts();

  applyConflictManagementSetting();

  QTimer::singleShot(0, this, [this]() { restoreScrollPosition(); });
}

void PluginsWidget::saveScrollPosition() const
{
  if (const auto* const scrollBar = ui->pluginList->verticalScrollBar()) {
    Settings::instance()->set("plugin_list_scroll", scrollBar->value());
  }
}

void PluginsWidget::restoreScrollPosition()
{
  auto* const scrollBar = ui->pluginList->verticalScrollBar();
  if (!scrollBar) {
    m_PendingScrollPosition = -1;
    return;
  }

  const int persisted =
      Settings::instance()->get<int>("plugin_list_scroll", scrollBar->value());
  const int target = m_PendingScrollPosition >= 0 ? m_PendingScrollPosition : persisted;
  scrollBar->setValue(target);
  m_PendingScrollPosition = -1;
}

void PluginsWidget::refreshPluginListPreservingScroll()
{
  // Rebuilding the list would free the file entries and records the plugin
  // info dialog is still pointing at; replay the refresh once it closes.
  if (m_PluginInfoOpen) {
    m_RefreshDeferred = true;
    return;
  }

  if (const auto* const scrollBar = ui->pluginList->verticalScrollBar()) {
    m_PendingScrollPosition = scrollBar->value();
  } else {
    m_PendingScrollPosition = -1;
  }

  saveScrollPosition();
  m_PluginListModel->refresh();
}

static bool containsPlugin(const MOBase::IModInterface* mod)
{
  const auto fileTree = mod ? mod->fileTree() : nullptr;
  if (!fileTree)
    return false;

  return std::ranges::any_of(*fileTree, [&](auto&& entry) {
    if (!entry)
      return false;
    const QString filename = entry->name();
    return filename.endsWith(u".esp"_s, Qt::CaseInsensitive) ||
           filename.endsWith(u".esm"_s, Qt::CaseInsensitive) ||
           filename.endsWith(u".esl"_s, Qt::CaseInsensitive);
  });
}

void PluginsWidget::onModStateChanged(
    const std::map<QString, MOBase::IModList::ModStates>& mods)
{
  const auto modList = m_Organizer->modList();
  if (!modList)
    return;

  bool affectsPlugins = false;
  for (const auto& [modName, modState] : mods) {
    const auto mod = modList->getMod(modName);
    if (containsPlugin(mod)) {
      m_PluginList->notifyPendingState(modName, modState);
      affectsPlugins = true;
    }
  }

  if (!affectsPlugins) {
    return;
  }

  // Toggling a mod also makes MO2 refresh its virtual file system, which fires
  // startRefresh() (see synchronizePluginLists). Refreshing here as well would
  // rebuild the whole list twice. Defer to the end of the event loop and skip
  // the refresh entirely if startRefresh already handled it in the meantime.
  if (m_ModStateRefreshPending) {
    return;
  }
  m_ModStateRefreshPending = true;
  QTimer::singleShot(0, this, [this]() {
    if (!m_ModStateRefreshPending) {
      return;
    }
    m_ModStateRefreshPending = false;
    refreshPluginListPreservingScroll();
  });
}

bool PluginsWidget::onAboutToRun([[maybe_unused]] const QString& binary)
{
  if (m_WriteTimer) {
    m_WriteTimer->stop();
  }
  m_PluginList->writePluginLists();

  const auto profilePath = QDir(m_Organizer->profilePath());
  const auto pluginsName = QDir::cleanPath(profilePath.absoluteFilePath("plugins.txt"));
  const auto loadOrderName =
      QDir::cleanPath(profilePath.absoluteFilePath("loadorder.txt"));
    const auto lockedOrderName =
      QDir::cleanPath(profilePath.absoluteFilePath("lockedorder.txt"));
  const auto parent = this->topLevelWidget();

  if (QFileInfo::exists(pluginsName + ".snapshot")) {
    MOBase::shellDeleteQuiet(pluginsName + ".snapshot", parent);
  }

  if (QFileInfo::exists(loadOrderName + ".snapshot")) {
    MOBase::shellDeleteQuiet(loadOrderName + ".snapshot", parent);
  }

  if (QFileInfo::exists(lockedOrderName + ".snapshot")) {
    MOBase::shellDeleteQuiet(lockedOrderName + ".snapshot", parent);
  }

  if (QFileInfo(binary).fileName().compare("lootcli.exe", Qt::CaseInsensitive) != 0) {
    createBackup(pluginsName, "snapshot", parent);
    createBackup(loadOrderName, "snapshot", parent);
    createBackup(lockedOrderName, "snapshot", parent);
    m_IsRunningApp = true;
  }

  return true;
}

void PluginsWidget::onFinishedRun(const QString& binary,
                                  [[maybe_unused]] unsigned int exitCode)
{
  const auto binaryName = QFileInfo(binary).fileName();
  if (binaryName.compare("lootcli.exe", Qt::CaseInsensitive) == 0) {
    return;
  }

  const bool isLootGui =
      binaryName.compare("Loot.exe", Qt::CaseInsensitive) == 0;

  if (isLootGui) {
    const auto profilePath = QDir(m_Organizer->profilePath());
    const auto pluginsName =
        QDir::cleanPath(profilePath.absoluteFilePath("plugins.txt"));
    const auto loadOrderName =
        QDir::cleanPath(profilePath.absoluteFilePath("loadorder.txt"));
    const auto lockedOrderName =
        QDir::cleanPath(profilePath.absoluteFilePath("lockedorder.txt"));
    const auto parent = this->topLevelWidget();

    MOBase::shellDeleteQuiet(pluginsName + ".snapshot", parent);
    MOBase::shellDeleteQuiet(loadOrderName + ".snapshot", parent);
    MOBase::shellDeleteQuiet(lockedOrderName + ".snapshot", parent);

    m_DeferPostLootRefresh = true;
    m_IsRunningApp         = false;
    m_ExternalStatesChanged = false;
    return;
  }



  m_Organizer->onNextRefresh([=, this]() {
    m_PluginListModel->refresh();
    checkLoadOrderChanged(binaryName);

    m_IsRunningApp          = false;
    m_ExternalStatesChanged = false;
  });
}

void PluginsWidget::onSettingChanged(const QString& key,
                                     [[maybe_unused]] const QVariant& oldValue,
                                     const QVariant& newValue)
{
  if (key == u"enable_sort_button"_s) {
    ui->sortButton->setVisible(newValue.value<bool>());
  } else if (key == u"enable_plugin_grouping"_s) {
    applyGroupingSetting();
    updateGroupActionVisibility();
  } else if (key == u"enable_reset_groups_button"_s ||
             key == u"enable_clean_groups_button"_s) {
    updateGroupActionVisibility();
  } else if (key == u"enable_plugin_conflict_management"_s) {
    applyConflictManagementSetting();
    // Whether records are scanned up-front depends on this setting; re-parse
    // so the list reflects the new mode (full scan on, header-only off).
    m_PluginListModel->invalidate();
  } else if (key == u"enable_cell_conflict_detection"_s) {
    // The conflict data was built with the previous value; re-parse so
    // CELL/WRLD child conflicts appear or disappear without a manual refresh.
    if (Settings::instance()->enablePluginConflictManagement()) {
      m_PluginListModel->invalidate();
    }
  }
}

void PluginsWidget::updateGroupActionVisibility()
{
  const auto* const settings = Settings::instance();
  const bool groupingEnabled = settings->enablePluginGrouping();
  const bool showReset = groupingEnabled && settings->enableResetGroupsButton();
  const bool showClean = groupingEnabled && settings->enableCleanGroupsButton();

  ui->resetGroupsButton->setVisible(showReset);
  ui->cleanGroupsButton->setVisible(showClean);

  if (resetGroupsAction) {
    resetGroupsAction->setVisible(showReset);
  }

  if (cleanGroupsAction) {
    cleanGroupsAction->setVisible(showClean);
  }
}

void PluginsWidget::applyConflictManagementSetting()
{
  const bool enabled = Settings::instance()->enablePluginConflictManagement();

  ui->pluginList->setColumnHidden(PluginListModel::COL_CONFLICTS, !enabled);
  // The records count is a by-product of the conflict scan; hide it too when
  // conflict management is off (records are then parsed only on demand).
  ui->pluginList->setColumnHidden(PluginListModel::COL_RECORDS, !enabled);

  if (toggleIgnoreMasters) {
    toggleIgnoreMasters->setEnabled(enabled);
    if (!enabled) {
      toggleIgnoreMasters->setChecked(false);
    }
  }

  m_PluginListModel->invalidateConflicts();
  ui->pluginList->updateOverwriteMarkers();
}

bool PluginsWidget::confirmMassOperation(const QString& text) const
{
  if (!Settings::instance()->confirmMassOperations()) {
    return true;
  }

  return QMessageBox::question(topLevelWidget(), tr("Confirm"), text,
                               QMessageBox::Yes | QMessageBox::No) ==
         QMessageBox::Yes;
}

bool PluginsWidget::selectedSingleGroup(QString* groupName) const
{
  if (!Settings::instance()->enablePluginGrouping()) {
    return false;
  }

  const auto selected = ui->pluginList->selectionModel()->selectedRows();
  if (selected.size() != 1) {
    return false;
  }

  const auto idx = selected.first();
  if (!idx.isValid() || !idx.model()->hasChildren(idx)) {
    return false;
  }

  if (groupName) {
    *groupName = idx.data().toString();
  }

  return true;
}

void PluginsWidget::renameSelectedGroup()
{
  QString oldGroup;
  if (!selectedSingleGroup(&oldGroup)) {
    return;
  }

  bool ok = false;
  const QString group = QInputDialog::getText(topLevelWidget(), tr("Rename Group..."),
                                              tr("Please enter a name:"),
                                              QLineEdit::Normal, oldGroup, &ok);

  if (!ok || group.isEmpty() || group == oldGroup) {
    return;
  }

  m_PluginListModel->renameGroup(oldGroup,
                                 m_PluginListModel->uniqueGroupName(group));
}

void PluginsWidget::removeSelectedGroup()
{
  QString group;
  if (!selectedSingleGroup(&group)) {
    return;
  }

  if (!confirmMassOperation(
          tr("Are you sure you want to remove \"%1\"?").arg(group))) {
    return;
  }

  m_PluginListModel->removeGroup(group);
}

void PluginsWidget::mergeSelectedGroup()
{
  QString fromGroup;
  if (!selectedSingleGroup(&fromGroup)) {
    return;
  }

  GUI::ListDialog dialog{*Settings::instance(), topLevelWidget()};
  dialog.setWindowTitle(tr("Merge Group Into..."));

  QStringList choices = m_PluginListModel->groups();
  choices.removeAll(fromGroup);
  dialog.setChoices(choices);

  if (dialog.exec() != QDialog::Accepted) {
    return;
  }

  const QString toGroup = dialog.getChoice();
  if (toGroup.isEmpty()) {
    return;
  }

  m_PluginListModel->mergeGroup(fromGroup, toGroup);
}

void PluginsWidget::applyGroupingSetting()
{
  const bool groupingEnabled = Settings::instance()->enablePluginGrouping();

  // The group proxy is attached to the sort proxy only while grouping is on.
  // Left attached, it would rebuild its whole group tree on every source
  // change and shuffle persistent indexes nobody looks at - and any stale
  // mapping it produced could crash the view later.
  if (groupingEnabled && m_GroupProxy->sourceModel() != m_SortProxy) {
    m_GroupProxy->setSourceModel(m_SortProxy);
  }

  auto* const model = groupingEnabled
                          ? static_cast<QAbstractItemModel*>(m_GroupProxy)
                          : static_cast<QAbstractItemModel*>(m_SortProxy);

  if (ui->pluginList->model() != model) {
    ui->pluginList->setModel(model);
    ui->pluginList->sortByColumn(PluginListModel::COL_PRIORITY, Qt::AscendingOrder);
  }

  // Detach only after the view stopped using the proxy.
  if (!groupingEnabled && m_GroupProxy->sourceModel()) {
    m_GroupProxy->setSourceModel(nullptr);
  }

  if (m_ViewSelectionChangedConnection) {
    disconnect(m_ViewSelectionChangedConnection);
  }
  m_ViewSelectionChangedConnection =
      connect(ui->pluginList->selectionModel(), &QItemSelectionModel::selectionChanged,
              this, &PluginsWidget::onSelectionChanged);

  if (!groupingEnabled) {
    ui->pluginList->collapseAll();
  }
}

static QByteArray hashFile(const QString& filePath)
{
  QCryptographicHash hash{QCryptographicHash::Sha1};
  QFile file{filePath};
  if (file.open(QIODevice::ReadOnly)) {
    hash.addData(file.readAll());
  } else {
    return ""_ba;
  }

  return hash.result();
}

void PluginsWidget::checkLoadOrderChanged(const QString& binaryName)
{
  const auto profilePath = QDir(m_Organizer->profilePath());
  const auto pluginsName = QDir::cleanPath(profilePath.absoluteFilePath("plugins.txt"));
  const auto loadOrderName =
      QDir::cleanPath(profilePath.absoluteFilePath("loadorder.txt"));
    const auto lockedOrderName =
      QDir::cleanPath(profilePath.absoluteFilePath("lockedorder.txt"));
  const auto parent = this->topLevelWidget();

  const auto pluginsSnapshot   = pluginsName + ".snapshot";
  const auto loadOrderSnapshot = loadOrderName + ".snapshot";
    const auto lockedOrderSnapshot = lockedOrderName + ".snapshot";

  const auto pluginsFile   = QFileInfo(pluginsName);
  const auto loadOrderFile = QFileInfo(loadOrderName);

  if (!QFileInfo(loadOrderSnapshot).exists())
    return;


  const bool enableWarning    = Settings::instance()->externalChangeWarning();
  const bool loadOrderChanged = m_ExternalStatesChanged ||
                                hashFile(loadOrderName) != hashFile(loadOrderSnapshot);

  if (loadOrderChanged) {
    if (binaryName.compare("Loot.exe", Qt::CaseInsensitive) != 0) {


      bool shouldRestore = true;
      if (enableWarning) {
        const auto answer = QMessageBox::question(
            this, tr("Load Order Changed"),
            tr("%1 has modified the load order. Do you want to apply these changes?")
                .arg(binaryName),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        shouldRestore = (answer == QMessageBox::No);
      }

      if (shouldRestore) {
        if (!tryRestore(pluginsName, "snapshot", true, parent) ||
            !tryRestore(loadOrderName, "snapshot", true, parent) ||
            !tryRestore(lockedOrderName, "snapshot", false, parent)) {
          const auto e = ::GetLastError();

          QMessageBox::critical(
              this, tr("Restore failed"),
              tr("Failed to restore the backup. Errorcode: %1")
                  .arg(QString::fromStdWString(MOBase::formatSystemMessage(e))));

          return;
        }
      }
    }
  }

  MOBase::shellDeleteQuiet(pluginsSnapshot, parent);
  MOBase::shellDeleteQuiet(loadOrderSnapshot, parent);
  MOBase::shellDeleteQuiet(lockedOrderSnapshot, parent);
  m_PluginListModel->invalidate();
}

void PluginsWidget::importLootGroups()
{
  const auto profilePath = QDir(m_Organizer->profilePath());
  const auto plugingroups =
      QDir::cleanPath(profilePath.absoluteFilePath(u"plugingroups.txt"_s));

  const auto* const managedGame = m_Organizer->managedGame();
  const auto gameName           = managedGame->gameName();
  const auto localAppData =
      QDir(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation));
  const auto masterlist =
      localAppData.filePath(u"LOOT/games/%1/masterlist.yaml"_s.arg(gameName));
  const auto userlist =
      localAppData.filePath(u"LOOT/games/%1/userlist.yaml"_s.arg(gameName));

  MOTools::importLootGroups(m_PluginList, plugingroups, masterlist, userlist);
}

void PluginsWidget::synchronizePluginLists(MOBase::IOrganizer* organizer)
{
  MOBase::IPluginList* const ipluginlist = organizer->pluginList();
  if (ipluginlist == nullptr || ipluginlist == m_PluginList) {
    return;
  }

  std::function<void()> startRefresh = [this] {
    m_OrganizerRefreshing = true;
    m_PluginList->flushPendingStates();

    // See refreshPluginListPreservingScroll(): the info dialog runs its own
    // event loop, so an organizer refresh can land while it is still open.
    if (m_PluginInfoOpen) {
      m_RefreshDeferred        = true;
      m_ModStateRefreshPending = false;
      return;
    }

    if (const auto* const sb = ui->pluginList->verticalScrollBar())
      m_PendingScrollPosition = sb->value();

    // A full invalidate() clears the plugin list and re-parses every plugin
    // file from disk, which freezes the UI on large load orders. An incremental
    // refresh() only parses plugins that newly appeared and drops the ones that
    // disappeared - exactly what a VFS refresh after a mod toggle requires.
    m_PluginListModel->refresh();

    // This refresh supersedes any pending onModStateChanged refresh.
    m_ModStateRefreshPending = false;
  };

  organizer->onNextRefresh(startRefresh, false);

  ipluginlist->onRefreshed([this, organizer, startRefresh]() {
    if (m_OrganizerRefreshing) {
      m_OrganizerRefreshing = false;
      organizer->onNextRefresh(startRefresh, false);
    }
  });

  ipluginlist->onPluginMoved(
      [this](const QString& name, int oldPriority, int newPriority) {
        if (m_OrganizerRefreshing)
          return;
        m_PluginListModel->movePlugin(name, oldPriority, newPriority);
      });

  ipluginlist->onPluginStateChanged(
      [this](const std::map<QString, MOBase::IPluginList::PluginStates>& infos) {
        if (m_OrganizerRefreshing)
          return;
        m_PluginListModel->changePluginStates(infos);
      });

  m_PluginList->onPluginMoved([=, this](const QString& name,
                                        [[maybe_unused]] int oldPriority,
                                        int newPriority) {
    if (m_PluginList->isRefreshing())
      return;

    ipluginlist->setPriority(name, newPriority);
  });

  m_PluginList->onPluginStateChanged(
      [=, this](const std::map<QString, MOBase::IPluginList::PluginStates>& infos) {
        if (m_IsRunningApp && !m_PluginList->isRefreshing() && !infos.empty()) {
          m_ExternalStatesChanged = true;
        }

        if (m_PluginList->isRefreshing() || infos.empty())
          return;

        for (const auto& [name, state] : infos) {
          m_PanelInterface->setPluginState(name,
                                           state == MOBase::IPluginList::STATE_ACTIVE);
        }
      });
}

}  // namespace BSPluginList
