#ifndef BSPLUGINLIST_PLUGINSORTFILTERPROXYMODEL_H
#define BSPLUGINLIST_PLUGINSORTFILTERPROXYMODEL_H

#include <QList>
#include <QSortFilterProxyModel>
#include <QStringList>

namespace BSPluginList
{

class PluginSortFilterProxyModel : public QSortFilterProxyModel
{
  Q_OBJECT

public:
  void hideForceEnabledFiles(bool doHide);

  [[nodiscard]] bool filterMatchesPlugin(const QString& plugin) const;
  [[nodiscard]] bool hasActiveFilter() const { return !m_CurrentFilter.isEmpty(); }

  bool canDropMimeData(const QMimeData* data, Qt::DropAction action, int row,
                       int column, const QModelIndex& parent) const override;
  bool dropMimeData(const QMimeData* data, Qt::DropAction action, int row, int column,
                    const QModelIndex& parent) override;

  bool filterAcceptsRow(int source_row,
                        const QModelIndex& source_parent) const override;

protected:
  bool lessThan(const QModelIndex& source_left,
                const QModelIndex& source_right) const override;

public slots:
  void updateFilter(const QString& filter);

private:
  QString m_CurrentFilter;
  // The filter text tokenized once, as a list of OR alternatives each holding
  // the AND keywords it requires. filterAcceptsRow() runs per row on every
  // change, so the splitting must not happen there.
  QList<QStringList> m_FilterTerms;
  bool m_HideForceEnabledFiles = false;
};

}  // namespace BSPluginList

#endif  // BSPLUGINLIST_PLUGINSORTFILTERPROXYMODEL_H
