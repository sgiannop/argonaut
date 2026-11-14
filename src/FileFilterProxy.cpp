#include "FileFilterProxy.h"

#include "DirectoryModel.h"

FileFilterProxy::FileFilterProxy(QObject *parent)
    : QSortFilterProxyModel(parent)
{
    setDynamicSortFilter(true);
}

void FileFilterProxy::sort(int column, Qt::SortOrder order)
{
    if (sourceModel())
        sourceModel()->sort(column, order);
}

#if defined(ARGONAUT_DBUS)
void FileFilterProxy::setChooserFilter(const std::optional<ChooserRequest::Filter> &filter, bool foldersOnly)
{
    m_filter = (filter && filter->matchesAll()) ? std::nullopt : filter;
    m_foldersOnly = foldersOnly;
    invalidateFilter();
}

bool FileFilterProxy::filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const
{
    if (!m_filter && !m_foldersOnly)
        return true;
    const auto *model = qobject_cast<const DirectoryModel *>(sourceModel());
    if (!model)
        return true;
    const QModelIndex index = model->index(sourceRow, 0, sourceParent);
    if (model->isDir(index))
        return true; // folders are always there to open
    if (m_foldersOnly)
        return false;
    return m_filter->matches(model->fileName(index), index.data(DirectoryModel::NameMimeRole).toString());
}
#endif
