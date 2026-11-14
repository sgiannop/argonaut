#pragma once

#include <QSortFilterProxyModel>

#if defined(ARGONAUT_DBUS) // Open/Save windows exist only with the portal backend (Linux)
#include "FileChooserRequest.h"
#include <optional>
#endif

// Sits between DirectoryModel and the views. Sorting is forwarded to the
// source model, which keeps its own (folders-first, natural) order; the
// proxy never reorders. (Search is done by DirectoryModel::setSearch, which
// also looks in sub-folders.) It filters only in an Open/Save window: files
// must match the chosen file type, and a folder picker shows only folders.
class FileFilterProxy : public QSortFilterProxyModel
{
    Q_OBJECT

public:
    explicit FileFilterProxy(QObject *parent = nullptr);

    void sort(int column, Qt::SortOrder order = Qt::AscendingOrder) override;

#if defined(ARGONAUT_DBUS)
    // Open/Save windows: no filter (nullopt) shows every file.
    void setChooserFilter(const std::optional<ChooserRequest::Filter> &filter, bool foldersOnly);

protected:
    bool filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const override;

private:
    std::optional<ChooserRequest::Filter> m_filter;
    bool m_foldersOnly = false;
#endif
};
