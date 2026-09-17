#include "FileInfo.h"
#include "FileEntry.h"
#include "MOPlugin/Settings.h"
#include "PluginList.h"

#include <boost/container/flat_map.hpp>

#include <algorithm>

using namespace Qt::Literals::StringLiterals;

namespace TESData
{

FileInfo::FileInfo(PluginList* pluginList, const QString& name, bool forceLoaded,
                   bool forceEnabled, bool forceDisabled, bool lightSupported)
    : m_PluginList{pluginList},
      m_FileSystemData{
          .name               = name,
          .hasMasterExtension = name.endsWith(u".esm"_s, Qt::CaseInsensitive),
          .hasLightExtension =
              lightSupported && name.endsWith(u".esl"_s, Qt::CaseInsensitive),
          .forceLoaded   = forceLoaded,
          .forceEnabled  = forceEnabled,
          .forceDisabled = forceDisabled,
      },
      m_Conflicts{[this]() {
        return doConflictCheck();
      }}
{}

bool FileInfo::isMasterFile() const
{
  return m_Metadata.isMasterFlagged || m_FileSystemData.hasMasterExtension ||
         m_FileSystemData.hasLightExtension;
}

bool FileInfo::isSmallFile() const
{
  return m_Metadata.isLightFlagged || m_FileSystemData.hasLightExtension;
}

bool FileInfo::isMediumFile() const
{
  return m_Metadata.isMediumFlagged && !isSmallFile();
}

bool FileInfo::isAlwaysEnabled() const
{
  return m_FileSystemData.forceLoaded || m_FileSystemData.forceEnabled;
}

bool FileInfo::canBeToggled() const
{
  return !m_FileSystemData.forceLoaded && !m_FileSystemData.forceEnabled &&
         !m_FileSystemData.forceDisabled;
}

FileInfo::LightCapability FileInfo::lightCapability() const
{
  if (!m_RecordsParsed) {
    return LightCapability::Unknown;
  }

  if (m_LightCapability != LightCapability::Unknown) {
    return m_LightCapability;
  }

  const auto entry = m_PluginList->findEntryByName(name().toStdString());
  if (entry == nullptr) {
    return LightCapability::Unknown;
  }

  // New records are the ones this plugin owns (overrides belong to a master).
  constexpr std::uint32_t eslRangeFirst = 0x800;
  constexpr std::uint32_t eslRangeLast  = 0xFFF;
  constexpr int eslRangeSize            = eslRangeLast - eslRangeFirst + 1;

  const std::string& ownName = entry->name();
  int newCount               = 0;
  bool allInRange            = true;

  entry->forEachRecord([&](auto&& record) {
    if (!record->hasFormId() || record->file() != ownName) {
      return;
    }

    ++newCount;
    const std::uint32_t id = record->formId() & 0xFFFFFFU;
    if (id < eslRangeFirst || id > eslRangeLast) {
      allInRange = false;
    }
  });

  m_NewRecordCount = newCount;
  if (newCount == 0 || allInRange) {
    m_LightCapability = LightCapability::Capable;
  } else if (newCount <= eslRangeSize) {
    m_LightCapability = LightCapability::CapableWithCompacting;
  } else {
    m_LightCapability = LightCapability::NotCapable;
  }

  return m_LightCapability;
}

bool FileInfo::mustLoadAfter(const FileInfo& other) const
{
  const bool hasMaster = this->masters().contains(other.name(), Qt::CaseInsensitive);
  const bool isMaster  = other.masters().contains(this->name(), Qt::CaseInsensitive);

  if (hasMaster && !isMaster) {
    return true;
  } else if (isMaster) {
    return false;
  }

  if (other.forceLoaded() && !this->forceLoaded()) {
    return true;
  }

  if (other.isMasterFile() && !this->isMasterFile()) {
    return true;
  }

  return false;
}

// Everything needed to classify a conflict against one other plugin,
// resolved once per handle instead of once per (record, alternative): the
// entry/name/index lookups take shared locks and allocate strings, and the
// same handful of handles repeats across thousands of records.
namespace
{
  struct AlternativeInfo
  {
    const FileInfo* file = nullptr;
    int index            = -1;
    bool skipAsWinner    = false;  // masked by ignore_master_conflicts
    bool skipAsLoser     = false;
  };
}  // namespace

FileInfo::Conflicts FileInfo::doConflictCheck() const
{
  Conflicts conflicts;

  const auto entry = m_PluginList->findEntryByName(name().toStdString());
  if (entry == nullptr) {
    return conflicts;
  }

  const bool ignoreMasters =
      Settings::instance()->get<bool>("ignore_master_conflicts", false);

  boost::container::flat_map<TESFileHandle, AlternativeInfo> altCache;
  const auto resolveAlternative =
      [&](TESFileHandle alternative) -> const AlternativeInfo& {
    const auto it = altCache.find(alternative);
    if (it != altCache.end()) {
      return it->second;
    }

    AlternativeInfo info;
    const auto otherEntry = m_PluginList->findEntryByHandle(alternative);
    if (otherEntry != nullptr && otherEntry != entry) {
      const QString otherName = QString::fromStdString(otherEntry->name());
      info.file               = m_PluginList->getPluginByName(otherName);
      if (info.file) {
        info.index = m_PluginList->getIndex(otherName);
        if (ignoreMasters) {
          info.skipAsWinner =
              this->masters().contains(info.file->name(), Qt::CaseInsensitive);
          info.skipAsLoser =
              info.file->masters().contains(this->name(), Qt::CaseInsensitive);
        }
      }
    }
    return altCache.emplace(alternative, info).first->second;
  };

  const auto checkConflict = [&](QSet<int>& winning, QSet<int>& losing,
                                 TESFileHandle alternative,
                                 bool* hasWinningConflict = nullptr,
                                 bool* hasLosingConflict  = nullptr) {
    const auto& other = resolveAlternative(alternative);
    if (!other.file) {
      return;
    }

    if (this->priority() > other.file->priority()) {
      if (!other.skipAsWinner) {
        winning.insert(other.index);
        if (hasWinningConflict) {
          *hasWinningConflict = true;
        }
      }
    } else {
      if (!other.skipAsLoser) {
        losing.insert(other.index);
        if (hasLosingConflict) {
          *hasLosingConflict = true;
        }
      }
    }
  };

  int checkedRecords      = 0;
  bool allRecordsLosing   = true;

  entry->forEachRecord([&](auto&& record) {
    if (record->ignored())
      return;

    ++checkedRecords;
    bool recordHasWinningConflict = false;
    bool recordHasLosingConflict  = false;

    for (const auto alternative : record->alternatives()) {
      checkConflict(conflicts.m_OverridingList, conflicts.m_OverriddenList,
                    alternative, &recordHasWinningConflict, &recordHasLosingConflict);
    }



    if (!recordHasLosingConflict) {
      allRecordsLosing = false;
    }
  });

  for (const auto& archive : m_FileSystemData.archives) {
    const auto archiveEntry = m_PluginList->findArchive(archive);
    if (!archiveEntry) {
      continue;
    }

    archiveEntry->forEachMember([&](auto&& item) {
      for (const auto alternative : item->alternatives) {
        checkConflict(conflicts.m_OverwritingArchiveList,
                      conflicts.m_OverwrittenArchiveList, alternative);
      }
    });
  }

  uint conflictState = CONFLICT_NONE;
  if (!conflicts.m_OverridingList.empty()) {
    conflictState |= CONFLICT_OVERRIDE;
  }
  if (!conflicts.m_OverriddenList.empty()) {
    conflictState |= CONFLICT_OVERRIDDEN;
  }
    const bool hasAnyLosingConflict =
      !conflicts.m_OverriddenList.empty() ||
      !conflicts.m_OverwrittenArchiveList.empty();
    const bool hasAnyWinningConflict =
      !conflicts.m_OverridingList.empty() ||
      !conflicts.m_OverwritingArchiveList.empty();




    if (Settings::instance()->enablePluginRedundantConflicts() &&
      checkedRecords > 0 && allRecordsLosing && hasAnyLosingConflict &&
      !hasAnyWinningConflict) {
    conflictState |= CONFLICT_REDUNDANT;
  }
  if (!conflicts.m_OverwritingArchiveList.empty()) {
    conflictState |= CONFLICT_ARCHIVE_OVERWRITE;
  }
  if (!conflicts.m_OverwrittenArchiveList.empty()) {
    conflictState |= CONFLICT_ARCHIVE_OVERWRITTEN;
  }
  conflicts.m_CurrentConflictState = static_cast<EConflictFlag>(conflictState);

  return conflicts;
}

}  // namespace TESData
