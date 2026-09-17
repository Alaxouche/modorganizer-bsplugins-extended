#include "PluginListModel.h"
#include "MOPlugin/Settings.h"
#include "PluginListDropInfo.h"

#include <QGuiApplication>
#include <QMimeData>

#include <algorithm>
#include <atomic>
#include <future>
#include <iterator>
#include <semaphore>
#include <thread>
#include <utility>
#include <vector>

namespace BSPluginList
{

PluginListModel::PluginListModel(TESData::PluginList* plugins) : m_Plugins{plugins} {}

void PluginListModel::clearRoleCaches() const
{
  m_ConflictCache.clear();
  m_FlagsCache.clear();
  m_TooltipCache.clear();
}

QModelIndex PluginListModel::index(int row, int column,
                                   [[maybe_unused]] const QModelIndex& parent) const
{
  if ((row < 0) || (row >= rowCount()) || (column < 0) || (column >= columnCount())) {
    return QModelIndex();
  }
  return createIndex(row, column, row);
}

QModelIndex PluginListModel::parent([[maybe_unused]] const QModelIndex& index) const
{
  return QModelIndex();
}

Qt::ItemFlags PluginListModel::flags(const QModelIndex& index) const
{
  const int id = index.row();

  Qt::ItemFlags result = QAbstractItemModel::flags(index);

  if (index.isValid()) {
    const auto plugin = m_Plugins->getPlugin(id);
    if (plugin && (!plugin->forceLoaded() && !plugin->forceDisabled())) {
      if (index.column() == COL_PRIORITY || index.column() == COL_NOTES)
        result |= Qt::ItemIsEditable;
      result |= Qt::ItemIsUserCheckable;
    }
    result |= Qt::ItemIsDragEnabled;
    result &= ~Qt::ItemIsDropEnabled;
  } else {
    result |= Qt::ItemIsDropEnabled;
  }

  return result;
}

static QVariantList
conflictListData(const TESData::PluginList* pluginList, const TESData::FileInfo* plugin,
                 const QSet<int>& (TESData::FileInfo::*getConflicts)() const)
{
  if (!plugin || !plugin->enabled()) {
    return QVariantList();
  }

  QVariantList list;
  for (const int otherId : (plugin->*getConflicts)()) {
    const auto other = pluginList->getPlugin(otherId);
    if (other && other->enabled()) {
      list.append(otherId);
    }
  }
  return list;
}

QVariant PluginListModel::data(const QModelIndex& index, int role) const
{
  switch (role) {
  case Qt::DisplayRole:
  case Qt::EditRole:
    return displayData(index);
  case Qt::CheckStateRole:
    if (index.column() == 0) {
      return checkstateData(index);
    }
    break;
  case Qt::ForegroundRole:
    return foregroundData(index);
  case Qt::BackgroundRole:
    return backgroundData(index);
  case Qt::FontRole:
    return fontData(index);
  case Qt::TextAlignmentRole:
    return alignmentData(index);
  case Qt::ToolTipRole:
    return tooltipData(index);
  case GroupingRole: {
    const auto id     = index.row();
    const auto plugin = m_Plugins->getPlugin(id);
    return plugin ? plugin->group() : QVariant();
  }
  case IndexRole:
    return index.row();
  case InfoRole: {
    const auto id     = index.row();
    const auto plugin = m_Plugins->getPlugin(id);
    return QVariant::fromValue(plugin);
  }
  case ConflictsIconRole:
    if (!Settings::instance()->enablePluginConflictManagement()) {
      return QVariant::fromValue(0u);
    }
    return conflictData(index);
  case FlagsIconRole:
    return iconData(index);
  case OriginRole: {
    const int id = index.row();
    return m_Plugins->getOriginName(id);
  }
  case OverridingRole: {
    if (!Settings::instance()->enablePluginConflictManagement()) {
      return QVariantList();
    }
    const int id      = index.row();
    const auto plugin = m_Plugins->getPlugin(id);
    return conflictListData(m_Plugins, plugin, &TESData::FileInfo::getPluginOverriding);
  }
  case OverriddenRole: {
    if (!Settings::instance()->enablePluginConflictManagement()) {
      return QVariantList();
    }
    const int id      = index.row();
    const auto plugin = m_Plugins->getPlugin(id);
    return conflictListData(m_Plugins, plugin, &TESData::FileInfo::getPluginOverridden);
  }
  case OverwritingAuxRole: {
    if (!Settings::instance()->enablePluginConflictManagement()) {
      return QVariantList();
    }
    const int id      = index.row();
    const auto plugin = m_Plugins->getPlugin(id);
    return conflictListData(m_Plugins, plugin,
                            &TESData::FileInfo::getPluginOverwritingArchive);
  }
  case OverwrittenAuxRole: {
    if (!Settings::instance()->enablePluginConflictManagement()) {
      return QVariantList();
    }
    const int id      = index.row();
    const auto plugin = m_Plugins->getPlugin(id);
    return conflictListData(m_Plugins, plugin,
                            &TESData::FileInfo::getPluginOverwrittenArchive);
  }
  }
  return QVariant();
}

QVariant PluginListModel::displayData(const QModelIndex& index) const
{
  const int id      = index.row();
  const auto plugin = m_Plugins->getPlugin(id);

  if (!plugin) {
    return QVariant();
  }

  switch (index.column()) {
  case COL_NAME:
    return plugin->name();
  case COL_PRIORITY:
    return plugin->priority();
  case COL_MODINDEX:
    return plugin->index();
  case COL_NOTES:
    return plugin->notes();
  case COL_RECORDS: {
    const auto entry = m_Plugins->findEntryByName(plugin->name().toStdString());
    return entry ? entry->recordCount() : QVariant();
  }
  default:
    return QVariant();
  }
}

QVariant PluginListModel::checkstateData(const QModelIndex& index) const
{
  const int id      = index.row();
  const auto plugin = m_Plugins->getPlugin(id);

  if (!plugin) {
    return QVariant();
  }

  if (plugin->isAlwaysEnabled()) {

    return QVariant();
  } else if (plugin->forceDisabled()) {
    return QVariant();
  } else {
    return plugin->enabled() ? Qt::Checked : Qt::Unchecked;
  }
}

QVariant PluginListModel::foregroundData(const QModelIndex& index) const
{
  const int id      = index.row();
  const auto plugin = m_Plugins->getPlugin(id);

  if (!plugin) {
    return QVariant();
  }

  if (plugin->hasNoRecords()) {
    if (index.column() == COL_NAME) {
      return QBrush(Qt::gray);
    }
  }

  if (plugin->forceDisabled()) {
    if (index.column() == COL_NAME) {
      return QBrush(Qt::darkRed);
    }
  }

  return QVariant();
}

QVariant
PluginListModel::backgroundData([[maybe_unused]] const QModelIndex& index) const
{
  return QVariant();
}

QVariant PluginListModel::fontData(const QModelIndex& index) const
{
  const int id      = index.row();
  const auto plugin = m_Plugins->getPlugin(id);

  QFont result;

  if (index.column() == COL_NAME) {
    if (plugin && plugin->hasNoRecords()) {
      result.setItalic(true);
    }
    if (plugin && Settings::instance()->enableTypefaceIndicators()) {
      const QString& pluginName = plugin->name();



      const bool isEsl = pluginName.endsWith(QLatin1String(".esl"), Qt::CaseInsensitive);
      const bool isLight = isEsl || plugin->isLightFlagged();
      const bool isMaster = isEsl ||
                            pluginName.endsWith(QLatin1String(".esm"), Qt::CaseInsensitive) ||
                            plugin->isMasterFlagged();
      if (isLight) {
        result.setItalic(true);
      } else if (plugin->isMediumFile()) {
        // Same convention as MO2's own plugin list: medium is underlined,
        // blueprint gets extra letter spacing.
        result.setUnderline(true);
      }
      if (isMaster) {
        result.setBold(true);
      }
      if (plugin->isBlueprintFlagged()) {
        result.setLetterSpacing(QFont::SpacingType::AbsoluteSpacing, 2);
      }
    }
  }

  return result;
}

QVariant PluginListModel::alignmentData(const QModelIndex& index) const
{
  if (index.column() == 0) {
    return QVariant(Qt::AlignLeft | Qt::AlignVCenter);
  } else {
    return QVariant(Qt::AlignHCenter | Qt::AlignVCenter);
  }
}

static QString truncateString(const QString& text, int length = 1024)
{
  QString new_text = text;

  if (new_text.length() > length) {
    new_text.truncate(length);
    new_text += "...";
  }

  return new_text;
}

static QString makeLootTooltip(const MOTools::Loot::Plugin& lootInfo)
{
  QString s;
  if (Settings::instance()->lootShowProblems()) {
    for (const auto& f : lootInfo.incompatibilities) {
      s += "<li>" +
           QObject::tr("Incompatible with %1")
               .arg(!f.displayName.isEmpty() ? f.displayName : f.name) +
           "</li>";
    }
  }

  if (Settings::instance()->lootShowMessages()) {
    for (const auto& m : lootInfo.messages) {
      s += "<li>";

      switch (m.type) {
      case MOBase::log::Warning:
        s += QObject::tr("Warning") + ": ";
        break;

      case MOBase::log::Error:
        s += QObject::tr("Error") + ": ";
        break;

      case MOBase::log::Info:
      case MOBase::log::Debug:
        break;
      }

      s += m.text + "</li>";
    }
  }

  if (Settings::instance()->lootShowDirty()) {
    for (const auto& d : lootInfo.dirty) {
      s += "<li>" + d.toString(false) + "</li>";
    }

    for (const auto& c : lootInfo.clean) {
      s += "<li>" + c.toString(true) + "</li>";
    }
  }

  if (s.isEmpty()) {
    return s;
  }

  return "<ul style=\"margin-top:0px; padding-top:0px; margin-left:15px; "
         "-qt-list-indent: 0;\">" +
         s + "</ul>";
}

QVariant PluginListModel::tooltipData(const QModelIndex& index) const
{
  const int id      = index.row();
  const int cacheId = id * 16 + index.column();
  if (m_TooltipCache.contains(cacheId)) {
    return m_TooltipCache.value(cacheId);
  }

  const auto plugin = m_Plugins->getPlugin(id);

  if (!plugin) {
    return QVariant();
  }

  const auto lootInfo = m_Plugins->getLootReport(plugin->name());

  switch (index.column()) {
  case COL_NAME: {
    QString toolTip;

    toolTip += "<b>" + tr("Origin") + "</b>: " + m_Plugins->getOriginName(id);

    if (plugin->forceLoaded()) {
      toolTip += "<br><b><i>" +
                 tr("This plugin can't be disabled or moved (enforced by the game).") +
                 "</i></b>";
    } else if (plugin->forceEnabled()) {
      toolTip += "<br><b><i>" +
                 tr("This plugin can't be disabled (enforced by the game).") +
                 "</i></b>";
    }

    if (!plugin->author().isEmpty()) {
      toolTip += "<br><b>" + tr("Author") + "</b>: " + truncateString(plugin->author());
    }

    if (plugin->description().size() > 0) {
      toolTip += "<br><b>" + tr("Description") +
                 "</b>: " + truncateString(plugin->description());
    }

    if (plugin->enabled() && plugin->missingMasters().size() > 0) {
      toolTip += "<br><b>" + tr("Missing Masters") + "</b>: " + "<b>" +
                 truncateString(QStringList(plugin->missingMasters().begin(),
                                            plugin->missingMasters().end())
                                    .join(", ")) +
                 "</b>";
    }

    QStringList enabledMasters;
    std::ranges::remove_copy_if(plugin->masters(), std::back_inserter(enabledMasters),
                                [&](auto&& master) {
                                  return plugin->missingMasters().contains(master);
                                });

    if (!enabledMasters.empty()) {
      toolTip += "<br><b>" + tr("Enabled Masters") +
                 "</b>: " + truncateString(enabledMasters.join(", "));
    }

    if (!plugin->archives().empty()) {
      QString archiveString =
          plugin->archives().size() < 6
              ? truncateString(
                    QStringList(plugin->archives().begin(), plugin->archives().end())
                        .join(", "))
              : "";
      toolTip += "<br><b>" + tr("Loads Archives") + "</b>: " + archiveString;
    }

    if (plugin->hasIni()) {
      toolTip += "<br><b>" + tr("Loads INI settings") +
                 "</b>: " + QFileInfo(plugin->name()).baseName() + ".ini";
    }

    if (!plugin->isSmallFile() && !plugin->forceLoaded()) {
      switch (plugin->lightCapability()) {
      case TESData::FileInfo::LightCapability::Capable:
        toolTip += "<br><b>" + tr("ESL capable") + "</b>: " +
                   tr("the ESL flag can be added without compacting (%1 new records).")
                       .arg(plugin->newRecordCount());
        break;
      case TESData::FileInfo::LightCapability::CapableWithCompacting:
        toolTip += "<br><b>" + tr("ESL capable") + "</b>: " +
                   tr("requires compacting %1 new records in xEdit first.")
                       .arg(plugin->newRecordCount());
        break;
      default:
        break;
      }
    }

    if (plugin->hasNoRecords()) {
      toolTip +=
          "<br><br>" + tr("This is a dummy plugin. It contains no records and is "
                          "typically used to load a paired archive file.");
    }

    m_TooltipCache.insert(cacheId, toolTip);
    return toolTip;
  }
  case COL_CONFLICTS: {
    const uint conflictFlags = data(index, ConflictsIconRole).toUInt();
    using enum TESData::FileInfo::EConflictFlag;

    QString toolTip;
    if (conflictFlags & CONFLICT_REDUNDANT) {
      toolTip += tr("Totally overwritten records (redundant)");
    } else if ((conflictFlags & CONFLICT_MIXED) == CONFLICT_MIXED) {
      toolTip += tr("Overrides & has overridden records");
    } else if (conflictFlags & CONFLICT_OVERRIDE) {
      toolTip += tr("Overrides records");
    } else if (conflictFlags & CONFLICT_OVERRIDDEN) {
      toolTip += tr("Has overridden records");
    }

    if ((conflictFlags & CONFLICT_MIXED) && (conflictFlags & CONFLICT_ARCHIVE_MIXED)) {
      toolTip += "<br>";
    }

    if ((conflictFlags & CONFLICT_ARCHIVE_MIXED) == CONFLICT_ARCHIVE_MIXED) {
      toolTip += tr("Overwrites & has overwritten archive files");
    } else if (conflictFlags & CONFLICT_ARCHIVE_OVERWRITE) {
      toolTip += tr("Overwrites another archive file");
    } else if (conflictFlags & CONFLICT_ARCHIVE_OVERWRITTEN) {
      toolTip += tr("Overwritten by another archive file");
    }
    m_TooltipCache.insert(cacheId, toolTip);
    return toolTip;
  }
  case COL_FLAGS: {

    QString toolTip       = "<nobr/>";
    const QString spacing = "<br><br>";

    if (plugin->enabled() && plugin->missingMasters().size() > 0) {
      toolTip += "<b>" + tr("Missing Masters") + "</b>: " + "<b>" +
                 truncateString(QStringList(plugin->missingMasters().begin(),
                                            plugin->missingMasters().end())
                                    .join(", ")) +
                 "</b>" + spacing;
    }

    if (plugin->hasIni()) {
      toolTip +=
          tr("There is an ini file connected to this plugin. Its settings will "
             "be added to your game settings, overwriting in case of conflicts.") +
          "<br><br>";
    }

    if (!plugin->archives().empty()) {
      toolTip +=
          tr("There are Archives connected to this plugin. Their assets will be "
             "added to your game, overwriting in case of conflicts following the "
             "plugin order. Loose files will always overwrite assets from "
             "Archives.") +
          spacing;
    }

    if (plugin->isMasterFile()) {
      toolTip += tr("This file is flagged as an ESM. It will load before any non-ESM "
                    "files in the load order.") +
                 spacing;
    }

    if (plugin->isSmallFile()) {
      toolTip += tr("This file is flagged as an ESL. It will adhere to its position in "
                    "the load order but the records will be loaded in ESL space.") +
                 spacing;
    }

    if (plugin->isMediumFile()) {
      toolTip += tr("This file is flagged as a medium plugin (ESH). It keeps its "
                    "position in the load order but its records are loaded in the "
                    "FD space, which holds 256 plugins of 65535 records each.") +
                 spacing;
    }

    if (plugin->isLightFlagged() && plugin->isMediumFlagged()) {
      toolTip += tr("WARNING: this plugin carries both the light and the medium "
                    "flag. The game honours the light flag and ignores the medium "
                    "one; clear one of the two in xEdit.") +
                 spacing;
    }

    if (plugin->isBlueprintFlagged()) {
      toolTip += tr("This file is flagged as a blueprint plugin. It is forced to "
                    "load after every non-blueprint plugin regardless of its "
                    "position in the list.") +
                 spacing;
    }

    if (!plugin->isSmallFile() && !plugin->forceLoaded()) {
      switch (plugin->lightCapability()) {
      case TESData::FileInfo::LightCapability::Capable:
        toolTip += tr("This plugin could be flagged as ESL without compacting "
                      "(%1 new records). Flagging it in xEdit frees a load "
                      "order slot.")
                       .arg(plugin->newRecordCount()) +
                   spacing;
        break;
      case TESData::FileInfo::LightCapability::CapableWithCompacting:
        toolTip += tr("This plugin could be flagged as ESL after compacting its "
                      "FormIDs in xEdit (%1 new records). Compacting invalidates "
                      "existing save games referencing this plugin.")
                       .arg(plugin->newRecordCount()) +
                   spacing;
        break;
      default:
        break;
      }
    }

    if (plugin->isOverlayFlagged()) {
      toolTip += tr("This plugin is flagged as an overlay plugin. It contains only "
                    "modified records and will overlay those changes onto the "
                    "existing records in memory. It takes no memory space.") +
                 spacing;
    }

    if (plugin->forceDisabled()) {
      toolTip += tr("This game does not currently permit custom plugin "
                    "loading. There may be manual workarounds.");
    }

    if (toolTip.endsWith(spacing)) {
      toolTip.chop(spacing.length());
    }

    if (lootInfo) {
      const auto lootToolTip = makeLootTooltip(*lootInfo);
      if (toolTip.length() > 7 && !lootToolTip.isEmpty()) {
        toolTip += "<hr>";
      }
      toolTip += lootToolTip;
    }

    m_TooltipCache.insert(cacheId, toolTip);
    return toolTip;
  }
  default:
    return QVariant();
  }
}

QVariant PluginListModel::conflictData(const QModelIndex& index) const
{
  if (!Settings::instance()->enablePluginConflictManagement()) {
    return QVariant::fromValue(0u);
  }

  const int id      = index.row();
  if (m_ConflictCache.contains(id)) {
    return m_ConflictCache.value(id);
  }

  const auto plugin = m_Plugins->getPlugin(id);
  if (!plugin) {
    return QVariant();
  }
  const auto result = QVariant::fromValue(plugin->conflictState());
  m_ConflictCache.insert(id, result);
  return result;
}

static bool isProblematic(const TESData::FileInfo* plugin,
                          const MOTools::Loot::Plugin* lootInfo)
{
  if (plugin && plugin->enabled() && plugin->hasMissingMasters()) {
    return true;
  }

  if (lootInfo && Settings::instance()->lootShowProblems()) {
    if (!lootInfo->incompatibilities.empty()) {
      return true;
    }

    if (!lootInfo->missingMasters.empty()) {
      return true;
    }
  }

  return false;
}

QVariant PluginListModel::iconData(const QModelIndex& index) const
{
  const int id      = index.row();
  if (m_FlagsCache.contains(id)) {
    return m_FlagsCache.value(id);
  }

  const auto plugin = m_Plugins->getPlugin(id);

  if (!plugin) {
    return QVariant();
  }

  const auto lootInfo = m_Plugins->getLootReport(plugin->name());

  using enum TESData::FileInfo::EFlag;
  uint flag = 0;

  if (isProblematic(plugin, lootInfo)) {
    flag |= FLAG_PROBLEMATIC;
  }

  if (lootInfo && !lootInfo->messages.empty() &&
      Settings::instance()->lootShowMessages()) {
    flag |= FLAG_INFORMATION;
  }

  if (plugin->hasIni()) {
    flag |= FLAG_INI;
  }

  if (!plugin->archives().empty()) {
    flag |= FLAG_BSA;
  }

  if (plugin->isMasterFile()) {
    flag |= FLAG_MASTER;
  }

  if (plugin->isSmallFile()) {
    flag |= FLAG_LIGHT;
  }

  if (!plugin->isSmallFile() && !plugin->forceLoaded()) {
    using enum TESData::FileInfo::LightCapability;
    const auto capability = plugin->lightCapability();
    if (capability == Capable || capability == CapableWithCompacting) {
      flag |= FLAG_LIGHT_CAPABLE;
    }
  }

  if (plugin->isMediumFile()) {
    flag |= FLAG_MEDIUM;
  }

  if (plugin->isBlueprintFlagged()) {
    flag |= FLAG_BLUEPRINT;
  }

  if (plugin->isOverlayFlagged()) {
    flag |= FLAG_OVERLAY;
  }

  if (lootInfo && !lootInfo->dirty.empty() && Settings::instance()->lootShowDirty()) {
    flag |= FLAG_CLEAN;
  }

  if (plugin->lockedOrder()) {
    flag |= FLAG_LOCKED;
  }

  const auto result = QVariant::fromValue(flag);
  m_FlagsCache.insert(id, result);
  return result;
}

QVariant PluginListModel::headerData(int section, Qt::Orientation orientation,
                                     int role) const
{
  if (orientation == Qt::Horizontal) {
    if (role == Qt::DisplayRole) {
      switch (section) {
      case COL_NAME:
        return tr("Name");
      case COL_CONFLICTS:
        return tr("Conflicts");
      case COL_FLAGS:
        return tr("Flags");
      case COL_PRIORITY:
        return tr("Priority");
      case COL_MODINDEX:
        return tr("Mod Index");
      case COL_NOTES:
        return tr("Notes");
      case COL_RECORDS:
        return tr("Records");
      default:
        return tr("unknown");
      }
    }
  }
  return QAbstractItemModel::headerData(section, orientation, role);
}

int PluginListModel::rowCount([[maybe_unused]] const QModelIndex& parent) const
{
  return m_Plugins->pluginCount();
}

int PluginListModel::columnCount([[maybe_unused]] const QModelIndex& parent) const
{
  return COL_COUNT;
}

bool PluginListModel::setData(const QModelIndex& index, const QVariant& value, int role)
{
  if (role == Qt::CheckStateRole) {
    clearRoleCaches();
    const int id = index.row();
    m_Plugins->setEnabled(id, value.toInt() == Qt::Checked);


    emit dataChanged(this->index(0, 0), this->index(rowCount() - 1, COL_MODINDEX),
                     {Qt::EditRole, Qt::CheckStateRole,
                      ConflictsIconRole, FlagsIconRole});
    emit pluginStatesChanged({index});
    return true;
  } else if (role == Qt::EditRole) {
    if (index.column() == COL_PRIORITY) {
      bool ok;
      const int newPriority = value.toInt(&ok);
      if (ok) {
        clearRoleCaches();
        int destination = newPriority;
        if (newPriority > index.data(Qt::EditRole).toInt()) {
          ++destination;
        }
        m_Plugins->moveToPriority({index.row()}, destination);

        emit dataChanged(this->index(0, 0),
                         this->index(rowCount() - 1, columnCount() - 1),
                         {Qt::EditRole});
        emit pluginOrderChanged();
        return true;
      }
    } else if (index.column() == COL_NOTES) {
      const int id = index.row();
      const auto plugin = m_Plugins->getPlugin(id);
      if (plugin) {

        plugin->setNotes(value.toString());
        emit dataChanged(index, index, {Qt::EditRole, Qt::DisplayRole});
        return true;
      }
    }
  }

  return false;
}

Qt::DropActions PluginListModel::supportedDropActions() const
{
  return Qt::MoveAction;
}

bool PluginListModel::canDropMimeData(const QMimeData* data, Qt::DropAction action,
                                      int row, [[maybe_unused]] int column,
                                      const QModelIndex& parent) const
{
  if (action == Qt::IgnoreAction) {
    return true;
  }

  if (action != Qt::MoveAction) {
    return false;
  }

  PluginListDropInfo dropInfo{data, row, parent, m_Plugins};
  return m_Plugins->canMoveToPriority(dropInfo.sourceRows(), dropInfo.destination());
}

bool PluginListModel::dropMimeData(const QMimeData* data, Qt::DropAction action,
                                   int row, [[maybe_unused]] int column,
                                   const QModelIndex& parent)
{
  if (action == Qt::IgnoreAction) {
    return true;
  }

  if (action != Qt::MoveAction) {
    return false;
  }

  PluginListDropInfo dropInfo{data, row, parent, m_Plugins};
  m_Plugins->moveToPriority(dropInfo.sourceRows(), dropInfo.destination());
  emit dataChanged(index(0, 0), index(rowCount() - 1, columnCount() - 1),
                   {Qt::DisplayRole, GroupingRole});
  emit pluginOrderChanged();

  return true;
}

QStringList
PluginListModel::groups(std::function<bool(const TESData::FileInfo*)> pred) const
{
  boost::container::flat_set<QString> groupSet;
  QStringList groups;
  QString lastGroup;

  for (int priority = 0, count = m_Plugins->pluginCount(); priority < count;
       ++priority) {
    const auto plugin = m_Plugins->getPluginByPriority(priority);
    if (!plugin) {
      continue;
    }

    if (pred && !pred(plugin)) {
      continue;
    }

    const auto& group = plugin->group();
    if (group.isEmpty() || group == lastGroup) {
      continue;
    }

    auto [it, inserted] = groupSet.insert(group);
    if (inserted) {
      groups.append(group);
    }
    lastGroup = group;
  }

  for (const auto& group : m_Plugins->knownGroups()) {
    if (group.isEmpty()) {
      continue;
    }

    if (groupSet.insert(group).second) {
      groups.append(group);
    }
  }

  return groups;
}

QString PluginListModel::uniqueGroupName(const QString& desired) const
{
  if (desired.isEmpty()) {
    return desired;
  }

  const QStringList taken = groups();
  const auto isTaken      = [&taken](const QString& name) {
    return std::ranges::any_of(taken, [&name](const QString& other) {
      return other.compare(name, Qt::CaseInsensitive) == 0;
    });
  };

  if (!isTaken(desired)) {
    return desired;
  }

  for (int suffix = 2; suffix < 1000; ++suffix) {
    const QString candidate = QStringLiteral("%1 (%2)").arg(desired).arg(suffix);
    if (!isTaken(candidate)) {
      return candidate;
    }
  }

  return desired;
}

QStringList PluginListModel::masterGroups() const
{
  return groups([](auto&& plugin) {
    return !plugin->forceLoaded() && plugin->isMasterFile();
  });
}

QStringList PluginListModel::regularGroups() const
{
  return groups([](auto&& plugin) {
    return !plugin->forceLoaded() && !plugin->isMasterFile();
  });
}













static void prewarmConflicts(TESData::PluginList* plugins)
{
  if (!Settings::instance()->enablePluginConflictManagement()) {
    return;
  }

  const int count = plugins->pluginCount();


  std::vector<const TESData::FileInfo*> toWarm;
  toWarm.reserve(count);
  for (int i = 0; i < count; ++i) {
    if (const auto* plugin = plugins->getPlugin(i)) {
      if (plugin->enabled()) {
        toWarm.push_back(plugin);
      }
    }
  }


  if (toWarm.empty()) {
    return;
  }

  // Fixed worker pool over an atomic cursor; spawning one thread per plugin
  // costs more than the conflict checks themselves on big load orders.
  const std::size_t workerCount = std::min<std::size_t>(
      std::max(2U, std::thread::hardware_concurrency()), toWarm.size());
  std::atomic<std::size_t> next{0};

  std::vector<std::future<void>> workers;
  workers.reserve(workerCount);
  for (std::size_t w = 0; w < workerCount; ++w) {
    workers.push_back(std::async(std::launch::async, [&]() {
      for (;;) {
        const std::size_t i = next.fetch_add(1, std::memory_order_relaxed);
        if (i >= toWarm.size()) {
          break;
        }
        (void)toWarm[i]->conflictState();
      }
    }));
  }
  for (auto& worker : workers) {
    worker.wait();
  }
}

void PluginListModel::refresh()
{
  clearRoleCaches();
  emit beginResetModel();
  m_Plugins->refresh();
  prewarmConflicts(m_Plugins);
  m_ConflictCache.reserve(m_Plugins->pluginCount());
  m_FlagsCache.reserve(m_Plugins->pluginCount());
  emit endResetModel();

  // After the views have been reset: pin locked plugins back where they
  // belong (LOOT or an external tool may have moved them on disk).
  m_Plugins->enforceLockedOrder();
}

void PluginListModel::invalidate()
{
  clearRoleCaches();
  emit beginResetModel();
  m_Plugins->refresh(true);
  prewarmConflicts(m_Plugins);
  m_ConflictCache.reserve(m_Plugins->pluginCount());
  m_FlagsCache.reserve(m_Plugins->pluginCount());
  emit endResetModel();

  // After the views have been reset: pin locked plugins back where they
  // belong (LOOT or an external tool may have moved them on disk).
  m_Plugins->enforceLockedOrder();
}

void PluginListModel::invalidateConflicts()
{
  if (!Settings::instance()->enablePluginConflictManagement()) {
    m_ConflictCache.clear();
    m_TooltipCache.clear();
    emit dataChanged(index(0, COL_CONFLICTS), index(rowCount() - 1, COL_CONFLICTS),
                     {PluginListModel::ConflictsIconRole});
    return;
  }

  m_ConflictCache.clear();
  m_TooltipCache.clear();

  for (int i = 0, count = m_Plugins->pluginCount(); i < count; ++i) {
    const auto plugin = m_Plugins->getPlugin(i);
    plugin->invalidateConflicts();
  }

  emit dataChanged(index(0, COL_CONFLICTS), index(rowCount() - 1, COL_CONFLICTS),
                   {PluginListModel::ConflictsIconRole});
}

void PluginListModel::movePlugin(const QString& name, [[maybe_unused]] int oldPriority,
                                 int newPriority)
{
  clearRoleCaches();
  m_Plugins->setPriority(name, newPriority);

  emit dataChanged(index(0, 0), index(rowCount() - 1, columnCount() - 1),
                   {Qt::DisplayRole});
  emit pluginOrderChanged();
}

void PluginListModel::changePluginStates(
    const std::map<QString, MOBase::IPluginList::PluginStates>& infos)
{
  clearRoleCaches();

  // A STATE_MISSING change erases a row from the plugin list. Removing rows
  // without begin/endRemoveRows would desync the view and proxies; fall back
  // to a full model reset whenever a removal is about to happen.
  bool willRemoveRows = false;
  for (const auto& [name, state] : infos) {
    if (state == MOBase::IPluginList::STATE_MISSING &&
        m_Plugins->getIndex(name) != -1) {
      willRemoveRows = true;
      break;
    }
  }

  if (willRemoveRows) {
    emit beginResetModel();
    for (const auto& [name, state] : infos) {
      m_Plugins->setState(name, state);
    }
    emit endResetModel();
    emit pluginStatesChanged({});
    return;
  }

  QModelIndexList indices;
  for (auto& [name, state] : infos) {
    m_Plugins->setState(name, state);

    const auto idx = m_Plugins->getIndex(name);
    if (idx != -1) {
      indices.append(index(idx, 0));
    }
  }

  if (!indices.empty()) {
    int minRow = indices.front().row();
    int maxRow = minRow;
    for (const auto& idx : indices) {
      minRow = std::min(minRow, idx.row());
      maxRow = std::max(maxRow, idx.row());
    }


    emit dataChanged(index(minRow, 0), index(maxRow, COL_MODINDEX),
                     {Qt::DisplayRole, Qt::CheckStateRole,
                      ConflictsIconRole, FlagsIconRole});
  }
  emit pluginStatesChanged(indices);
}

void PluginListModel::setEnabledAll(bool enabled)
{
  QModelIndexList indices;
  indices.reserve(rowCount());
  std::generate_n(std::back_inserter(indices), rowCount(), [this, i = 0]() mutable {
    return index(i++, 0);
  });
  setEnabled(indices, enabled);
}

void PluginListModel::setEnabled(const QModelIndexList& indices, bool enabled)
{
  if (indices.empty()) {
    return;
  }

  clearRoleCaches();

  std::vector<int> ids;
  ids.reserve(indices.size());
  std::ranges::transform(indices, std::back_inserter(ids), [](auto&& idx) {
    return idx.row();
  });
  m_Plugins->setEnabled(std::move(ids), enabled);

  int minRow = indices.front().row();
  int maxRow = minRow;
  for (const auto& idx : indices) {
    minRow = std::min(minRow, idx.row());
    maxRow = std::max(maxRow, idx.row());
  }


  emit dataChanged(index(minRow, 0), index(maxRow, COL_MODINDEX),
                   {Qt::DisplayRole, Qt::CheckStateRole,
                    ConflictsIconRole, FlagsIconRole});
  emit pluginStatesChanged(indices);
}

void PluginListModel::sendToPriority(const QModelIndexList& indices, int priority,
                                     bool disjoint)
{
  if (indices.empty()) {
    return;
  }

  clearRoleCaches();

  std::vector<int> ids;
  ids.reserve(indices.size());
  std::ranges::transform(indices, std::back_inserter(ids), [](auto&& idx) {
    return idx.row();
  });
  m_Plugins->moveToPriority(std::move(ids), priority, disjoint);

  emit dataChanged(index(0, COL_PRIORITY), index(rowCount() - 1, COL_MODINDEX),
                   {Qt::DisplayRole});
  emit pluginOrderChanged();
}

void PluginListModel::shiftPluginsPriority(const QModelIndexList& indices, int offset)
{
  if (indices.empty()) {
    return;
  }

  clearRoleCaches();

  std::vector<int> ids;
  ids.reserve(indices.size());
  std::ranges::transform(indices, std::back_inserter(ids), [](auto&& idx) {
    return idx.row();
  });
  m_Plugins->shiftPriority(std::move(ids), offset);

  emit dataChanged(index(0, COL_PRIORITY), index(rowCount() - 1, COL_MODINDEX),
                   {Qt::DisplayRole});
  emit pluginOrderChanged();
}

void PluginListModel::toggleState(const QModelIndexList& indices)
{
  if (indices.empty()) {
    return;
  }

  clearRoleCaches();

  std::vector<int> ids;
  ids.reserve(indices.size());
  std::ranges::transform(indices, std::back_inserter(ids), [](auto&& idx) {
    return idx.row();
  });
  m_Plugins->toggleState(std::move(ids));

  int minRow = indices.front().row();
  int maxRow = minRow;
  for (const auto& idx : indices) {
    minRow = std::min(minRow, idx.row());
    maxRow = std::max(maxRow, idx.row());
  }


  emit dataChanged(index(minRow, 0), index(maxRow, COL_MODINDEX),
                   {Qt::DisplayRole, Qt::CheckStateRole,
                    ConflictsIconRole, FlagsIconRole});
  emit pluginStatesChanged(indices);
}

void PluginListModel::setGroup(const QModelIndexList& indices, const QString& group)
{
  if (indices.empty()) {
    return;
  }

  clearRoleCaches();

  std::vector<int> ids;
  ids.reserve(indices.size());
  std::ranges::transform(indices, std::back_inserter(ids), [](auto&& idx) {
    return idx.row();
  });
  m_Plugins->setGroup(std::move(ids), group);

  int minRow = indices.front().row();
  int maxRow = minRow;
  for (const auto& idx : indices) {
    minRow = std::min(minRow, idx.row());
    maxRow = std::max(maxRow, idx.row());
  }

  emit dataChanged(this->index(minRow, 0), this->index(maxRow, COL_MODINDEX),
                   {GroupingRole, Qt::DisplayRole});
}

void PluginListModel::renameGroup(const QString& oldGroup, const QString& newGroup)
{
  clearRoleCaches();
  m_Plugins->renameGroup(oldGroup, newGroup);
  emit dataChanged(index(0, 0), index(rowCount() - 1, columnCount() - 1),
                   {Qt::DisplayRole, GroupingRole});
  emit pluginOrderChanged();
}

void PluginListModel::removeGroup(const QString& group)
{
  clearRoleCaches();
  m_Plugins->removeGroup(group);
  emit dataChanged(index(0, 0), index(rowCount() - 1, columnCount() - 1),
                   {Qt::DisplayRole, GroupingRole});
  emit pluginOrderChanged();
}

void PluginListModel::mergeGroup(const QString& fromGroup, const QString& toGroup)
{
  clearRoleCaches();
  m_Plugins->mergeGroup(fromGroup, toGroup);
  emit dataChanged(index(0, 0), index(rowCount() - 1, columnCount() - 1),
                   {Qt::DisplayRole, GroupingRole});
  emit pluginOrderChanged();
}

void PluginListModel::resetGroupsStructure()
{
  clearRoleCaches();
  m_Plugins->resetGroupsStructure();
  emit dataChanged(index(0, 0), index(rowCount() - 1, columnCount() - 1),
                   {Qt::DisplayRole, GroupingRole});
  emit pluginOrderChanged();
}

void PluginListModel::cleanEmptyGroups()
{
  clearRoleCaches();
  m_Plugins->cleanEmptyGroups();
  emit dataChanged(index(0, 0), index(rowCount() - 1, columnCount() - 1),
                   {Qt::DisplayRole, GroupingRole});
  emit pluginOrderChanged();
}

void PluginListModel::lockPlugins(const QModelIndexList& indices, bool locked)
{
  if (indices.empty()) {
    return;
  }

  clearRoleCaches();

  for (const auto& idx : indices) {
    m_Plugins->lockPlugin(idx.row(), locked);
  }

  int minRow = indices.front().row();
  int maxRow = minRow;
  for (const auto& idx : indices) {
    minRow = std::min(minRow, idx.row());
    maxRow = std::max(maxRow, idx.row());
  }

  emit dataChanged(index(minRow, COL_FLAGS), index(maxRow, COL_FLAGS),
                   {FlagsIconRole});
}

void PluginListModel::sendToGroup(const QModelIndexList& indices, const QString& group,
                                  bool isESM)
{
  int destination = -1;
  for (int priority = 0, count = m_Plugins->pluginCount(); priority < count;
       ++priority) {
    const auto plugin = m_Plugins->getPluginByPriority(priority);
    if (plugin && plugin->isMasterFile() == isESM && plugin->group() == group) {
      destination = priority + 1;
    }
  }

  if (destination == -1)
    return;

  clearRoleCaches();

  std::vector<int> ids;
  ids.reserve(indices.size());
  std::ranges::transform(indices, std::back_inserter(ids), [](auto&& idx) {
    return idx.row();
  });
  m_Plugins->setGroup(ids, group);
  m_Plugins->moveToPriority(std::move(ids), destination);
  emit dataChanged(index(0, 0), index(rowCount() - 1, columnCount() - 1),
                   {Qt::DisplayRole, GroupingRole});
  emit pluginOrderChanged();
}

}  // namespace BSPluginList
