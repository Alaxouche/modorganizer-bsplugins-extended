#ifndef BSPLUGINLIST_PLUGINLISTVIEW_H
#define BSPLUGINLIST_PLUGINLISTVIEW_H

#include <QColor>
#include <QHash>
#include <QList>
#include <QSet>
#include <QMetaObject>
#include <QTreeView>

namespace BSPluginList
{

struct MarkerInfos
{
  QSet<uint> overriding;
  QSet<uint> overridden;
  QSet<uint> overwritingAux;
  QSet<uint> overwrittenAux;
  QSet<uint> highlight;
};

class PluginListModel;
class PluginSortFilterProxyModel;

class PluginListView final : public QTreeView
{
  Q_OBJECT

public:
  explicit PluginListView(QWidget* parent = nullptr);

  void setup();

  void setModel(QAbstractItemModel* model) override;
  QRect visualRect(const QModelIndex& index) const override;

  [[nodiscard]] QColor markerColor(const QModelIndex& index) const;
  [[nodiscard]] uint fileFlags(const QModelIndex& index) const;
  [[nodiscard]] uint conflictFlags(const QModelIndex& index) const;

public slots:
  void setHighlightedOrigins(const QStringList& origins);
  void clearOverwriteMarkers();
  void updateOverwriteMarkers();

signals:
  void openOriginExplorer(const QModelIndex& index);

protected:
  bool event(QEvent* event) override;
  void dragMoveEvent(QDragMoveEvent* event) override;
  void paintEvent(QPaintEvent* event) override;

  bool moveSelection(int key);
  bool toggleSelectionState();

private:
  friend class PluginListContextMenu;

  QModelIndex indexViewToModel(const QModelIndex& index,
                               const QAbstractItemModel* model) const;

  QModelIndexList indexViewToModel(const QModelIndexList& indices,
                                   const QAbstractItemModel* model,
                                   bool includeChildren = true) const;

private slots:
  void onGroupRenameRequested(const QModelIndex& index, const QString& name);

private:
  // markerColor() averages the colours of every child of a collapsed group.
  // The delegate asks once per cell and the marking scroll bar once per row,
  // so the same average is recomputed many times over between two changes.
  // Keyed by proxy item id, dropped whenever the markers or the rows change.
  void invalidateGroupColorCache() const { m_GroupColorCache.clear(); }

  bool m_FirstPaint = true;
  MarkerInfos m_Markers;
  mutable QHash<quintptr, QColor> m_GroupColorCache;
  QList<QMetaObject::Connection> m_ModelConnections;
  PluginListModel* m_PluginModel          = nullptr;
  PluginSortFilterProxyModel* m_SortProxy = nullptr;
  QMetaObject::Connection m_SelectionChangedConnection;
};

}  // namespace BSPluginList

#endif  // BSPLUGINLIST_PLUGINLISTVIEW_H
