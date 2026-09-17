#ifndef TESDATA_FILEINFO_H
#define TESDATA_FILEINFO_H

#include <ifiletree.h>
#include <memoizedlock.h>

#include <boost/container/flat_set.hpp>

#include <QDateTime>
#include <QSet>
#include <QString>

namespace TESData
{

class PluginList;

class FileInfo
{
public:
  enum EConflictFlag : uint
  {
    CONFLICT_NONE                = 0x0,
    CONFLICT_OVERRIDE            = 0x1,
    CONFLICT_OVERRIDDEN          = 0x2,
    CONFLICT_REDUNDANT           = 0x4,
    CONFLICT_ARCHIVE_OVERWRITE   = 0x8,
    CONFLICT_ARCHIVE_OVERWRITTEN = 0x10,

    CONFLICT_MIXED         = CONFLICT_OVERRIDE | CONFLICT_OVERRIDDEN,
    CONFLICT_ARCHIVE_MIXED = CONFLICT_ARCHIVE_OVERWRITE | CONFLICT_ARCHIVE_OVERWRITTEN,
  };

  enum EFlag : uint
  {
    FLAG_NONE          = 0x000,
    FLAG_PROBLEMATIC   = 0x001,
    FLAG_INFORMATION   = 0x002,
    FLAG_INI           = 0x004,
    FLAG_BSA           = 0x008,
    FLAG_MASTER        = 0x010,
    FLAG_LIGHT         = 0x020,
    FLAG_OVERLAY       = 0x040,
    FLAG_CLEAN         = 0x080,
    FLAG_LOCKED        = 0x100,
    FLAG_LIGHT_CAPABLE = 0x200,
    FLAG_MEDIUM        = 0x400,
    FLAG_BLUEPRINT     = 0x800,
  };

  struct FileSystemData
  {
    QString name;

    bool hasMasterExtension;
    bool hasLightExtension;

    bool forceLoaded;
    bool forceEnabled;
    bool forceDisabled;

    bool hasIni;
    boost::container::flat_set<QString, MOBase::FileNameComparator> archives;

    // Where the file was read from and what it looked like then. An
    // incremental refresh compares this against the file system so a plugin
    // edited outside MO2 (xEdit removing a master, say) is re-parsed instead
    // of keeping the metadata it had at startup.
    QString sourcePath;
    qint64 fileSize = -1;
    qint64 fileTime = -1;
  };

  struct Metadata
  {
    QString author;
    QString description;

    bool isMasterFlagged;
    bool isLightFlagged;
    bool isOverlayFlagged;
    // Starfield plugin types; false on every other game.
    bool isMediumFlagged;
    bool isBlueprintFlagged;
    bool hasNoRecords;

    QStringList masters;
    mutable boost::container::flat_set<QString, MOBase::FileNameComparator> masterUnset;
  };

  struct State
  {
    bool enabled     = false;
    int priority     = -1;
    QString index;
    int loadOrder    = -1;
    QString group;
    QString notes;
    bool lockedOrder = false;
    // Priority the plugin is pinned to while lockedOrder is set; external
    // changes (LOOT, xEdit) are snapped back to it on refresh.
    int lockedPriority = -1;

    bool operator<(const State& other) const { return (loadOrder < other.loadOrder); }
  };

  struct Conflicts
  {
    EConflictFlag m_CurrentConflictState = CONFLICT_NONE;
    QSet<int> m_OverridingList;
    QSet<int> m_OverriddenList;
    QSet<int> m_OverwritingArchiveList;
    QSet<int> m_OverwrittenArchiveList;
  };

  FileInfo(PluginList* pluginList, const QString& name, bool forceLoaded,
           bool forceEnabled, bool forceDisabled, bool lightSupported);

  [[nodiscard]] const QString& name() const { return m_FileSystemData.name; }

  [[nodiscard]] bool hasMasterExtension() const
  {
    return m_FileSystemData.hasMasterExtension;
  }

  [[nodiscard]] bool hasLightExtension() const
  {
    return m_FileSystemData.hasLightExtension;
  }

  [[nodiscard]] bool forceLoaded() const { return m_FileSystemData.forceLoaded; }
  [[nodiscard]] bool forceEnabled() const { return m_FileSystemData.forceEnabled; }
  [[nodiscard]] bool forceDisabled() const { return m_FileSystemData.forceDisabled; }

  [[nodiscard]] bool hasIni() const { return m_FileSystemData.hasIni; }
  void setHasIni(bool hasIni) { m_FileSystemData.hasIni = hasIni; }
  [[nodiscard]] const auto& archives() const { return m_FileSystemData.archives; }

  void setFileStamp(const QString& path, qint64 size, qint64 time)
  {
    m_FileSystemData.sourcePath = path;
    m_FileSystemData.fileSize   = size;
    m_FileSystemData.fileTime   = time;
  }

  [[nodiscard]] bool fileStampMatches(const QString& path, qint64 size,
                                      qint64 time) const
  {
    return !m_FileSystemData.sourcePath.isEmpty() &&
           m_FileSystemData.sourcePath == path && m_FileSystemData.fileSize == size &&
           m_FileSystemData.fileTime == time;
  }

  // Drops everything that was read out of the file itself, so the plugin can be
  // parsed again in place. The load-order state (enabled, priority, group,
  // notes, lock) lives elsewhere and is deliberately kept.
  void resetForRescan()
  {
    m_FileSystemData.hasIni = false;
    m_FileSystemData.archives.clear();
    m_Metadata = Metadata{};
    m_Conflicts.invalidate();
    m_RecordsParsed   = false;
    m_LightCapability = LightCapability::Unknown;
    m_NewRecordCount  = 0;
  }

  void addArchive(const QString& archive) { m_FileSystemData.archives.insert(archive); }

  [[nodiscard]] const QString& author() const { return m_Metadata.author; }
  void setAuthor(const QString& author) { m_Metadata.author = author; }
  [[nodiscard]] const QString& description() const { return m_Metadata.description; }
  void setDescription(const QString& text) { m_Metadata.description = text; }
  [[nodiscard]] bool isMasterFlagged() const { return m_Metadata.isMasterFlagged; }
  void setMasterFlagged(bool value) { m_Metadata.isMasterFlagged = value; }
  [[nodiscard]] bool isLightFlagged() const { return m_Metadata.isLightFlagged; }
  void setLightFlagged(bool value) { m_Metadata.isLightFlagged = value; }
  [[nodiscard]] bool isOverlayFlagged() const { return m_Metadata.isOverlayFlagged; }
  void setOverlayFlagged(bool value) { m_Metadata.isOverlayFlagged = value; }
  [[nodiscard]] bool isMediumFlagged() const { return m_Metadata.isMediumFlagged; }
  void setMediumFlagged(bool value) { m_Metadata.isMediumFlagged = value; }
  [[nodiscard]] bool isBlueprintFlagged() const
  {
    return m_Metadata.isBlueprintFlagged;
  }
  void setBlueprintFlagged(bool value) { m_Metadata.isBlueprintFlagged = value; }
  [[nodiscard]] bool hasNoRecords() const { return m_Metadata.hasNoRecords; }
  void setHasNoRecords(bool value) { m_Metadata.hasNoRecords = value; }

  [[nodiscard]] const auto& masters() const { return m_Metadata.masters; }
  void addMaster(const QString& master)
  {
    // Idempotent: a re-parse (on-demand record parsing) must not duplicate
    // masters already collected by the initial header-only parse.
    if (!m_Metadata.masters.contains(master, Qt::CaseInsensitive)) {
      m_Metadata.masters.push_back(master);
    }
  }

  [[nodiscard]] bool hasMissingMasters() const
  {
    return !m_Metadata.masterUnset.empty();
  }

  [[nodiscard]] const auto& missingMasters() const { return m_Metadata.masterUnset; }

  template <std::ranges::input_range R>
  void setMissingMasters(R&& range) const
  {
    m_Metadata.masterUnset.clear();
    m_Metadata.masterUnset.insert(std::begin(range), std::end(range));
  }

  [[nodiscard]] bool enabled() const { return m_State.enabled; }
  void setEnabled(bool enabled) { m_State.enabled = enabled; }
  [[nodiscard]] int priority() const { return m_State.priority; }
  void setPriority(int priority)
  {
    m_State.priority = priority;
    m_Conflicts.invalidate();
  }
  [[nodiscard]] const QString& index() const { return m_State.index; }
  void setIndex(const QString& index) { m_State.index = index; }
  [[nodiscard]] int loadOrder() const { return m_State.loadOrder; }
  void setLoadOrder(int loadOrder) { m_State.loadOrder = loadOrder; }
  [[nodiscard]] const QString& group() const { return m_State.group; }
  void setGroup(const QString& group) { m_State.group = group; }
  [[nodiscard]] const QString& notes() const { return m_State.notes; }
  void setNotes(const QString& notes) { m_State.notes = notes; }
  [[nodiscard]] bool lockedOrder() const { return m_State.lockedOrder; }
  void setLockedOrder(bool locked) { m_State.lockedOrder = locked; }
  [[nodiscard]] int lockedPriority() const { return m_State.lockedPriority; }
  void setLockedPriority(int priority) { m_State.lockedPriority = priority; }

  [[nodiscard]] EConflictFlag conflictState() const
  {
    return m_Conflicts.value().m_CurrentConflictState;
  }

  [[nodiscard]] const auto& getPluginOverriding() const
  {
    return m_Conflicts.value().m_OverridingList;
  }

  [[nodiscard]] const auto& getPluginOverridden() const
  {
    return m_Conflicts.value().m_OverriddenList;
  }

  [[nodiscard]] const auto& getPluginOverwritingArchive() const
  {
    return m_Conflicts.value().m_OverwritingArchiveList;
  }

  [[nodiscard]] const auto& getPluginOverwrittenArchive() const
  {
    return m_Conflicts.value().m_OverwrittenArchiveList;
  }

  [[nodiscard]] bool isMasterFile() const;
  [[nodiscard]] bool isSmallFile() const;
  // A medium plugin is only medium when it is not also light: the light flag
  // wins in the engine, and a plugin carrying both is a mistake to surface.
  [[nodiscard]] bool isMediumFile() const;
  [[nodiscard]] bool isAlwaysEnabled() const;
  [[nodiscard]] bool canBeToggled() const;
  [[nodiscard]] bool mustLoadAfter(const FileInfo& other) const;

  void invalidateConflicts() const { m_Conflicts.invalidate(); }

  // Whether this plugin's records have been parsed into the conflict tree.
  // False when the up-front scan was header-only (conflict management off).
  [[nodiscard]] bool recordsParsed() const { return m_RecordsParsed; }
  void setRecordsParsed(bool value)
  {
    m_RecordsParsed    = value;
    m_LightCapability  = LightCapability::Unknown;
  }

  // Whether the plugin could take the ESL flag, following the classic xEdit
  // rule: new records must fit in the 0x800-0xFFF FormID range (2048 slots).
  enum class LightCapability
  {
    Unknown,                // records not parsed yet
    NotCapable,             // more new records than the ESL range can hold
    CapableWithCompacting,  // fits after renumbering FormIDs (xEdit compact)
    Capable,                // new FormIDs are already inside the ESL range
  };

  [[nodiscard]] LightCapability lightCapability() const;

  // Number of records the plugin itself introduces (not overrides); only
  // meaningful once lightCapability() has been computed.
  [[nodiscard]] int newRecordCount() const { return m_NewRecordCount; }

private:
  [[nodiscard]] Conflicts doConflictCheck() const;

  PluginList* m_PluginList;
  FileSystemData m_FileSystemData;
  Metadata m_Metadata;
  State m_State;
  mutable MOBase::MemoizedLocked<Conflicts> m_Conflicts;
  bool m_RecordsParsed = false;
  mutable LightCapability m_LightCapability = LightCapability::Unknown;
  mutable int m_NewRecordCount              = 0;
};

}  // namespace TESData

#endif  // TESDATA_FILEINFO_H
