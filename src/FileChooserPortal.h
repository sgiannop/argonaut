#pragma once

#include "FileChooserRequest.h"

#include <QDBusMessage>
#include <QHash>
#include <QObject>

class QDBusConnection;

// Argonaut's backend for xdg-desktop-portal's FileChooser
// (org.freedesktop.impl.portal.FileChooser), so apps' Open and Save dialogs
// are Argonaut windows. Owned by `argonaut --portal`, which D-Bus starts
// through org.freedesktop.impl.portal.desktop.argonaut.service; the portal
// only uses it when the user picked it (PortalConfig, Preferences). Linux.
//
// Each call arrives as `requested(id, request)`; the reply is held until
// finish(id, …). The portal's Request.Close (the app gave up) becomes
// `closeRequested(id)`.
class FileChooserPortal : public QObject
{
    Q_OBJECT

public:
    static constexpr const char *ServiceName = "org.freedesktop.impl.portal.desktop.argonaut";
    static constexpr const char *ObjectPath = "/org/freedesktop/portal/desktop";

    enum Response : uint { Success = 0, Cancelled = 1, Other = 2 };

    explicit FileChooserPortal(QObject *parent = nullptr);
    ~FileChooserPortal() override;

    // Exports the object and takes the name; false if it's taken.
    bool registerOn(const QDBusConnection &bus);
    int pendingCount() const { return int(m_pending.size()); }

    // Sends the held reply. Unknown or finished ids are ignored.
    void finish(int id, uint response, const QVariantMap &results = {});

    // Called by the adaptor (public for it only).
    void receive(ChooserRequest::Mode mode, const QString &handle, const QString &appId,
                 const QString &parentWindow, const QString &title, const QVariantMap &options,
                 const QDBusMessage &message);
    void close(const QString &handle);

signals:
    void requested(int id, const ChooserRequest &request);
    void closeRequested(int id);

private:
    struct Pending
    {
        QDBusMessage message;
        QString handle;
        QObject *requestObject = nullptr;
    };
    QString m_bus;
    QHash<int, Pending> m_pending;
    int m_nextId = 1;
};
