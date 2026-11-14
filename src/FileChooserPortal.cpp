#include "FileChooserPortal.h"

#include <QDBusAbstractAdaptor>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusObjectPath>

namespace {

// org.freedesktop.impl.portal.FileChooser. The replies are sent later by
// FileChooserPortal::finish, so the return values here are never used.
class ChooserAdaptor : public QDBusAbstractAdaptor
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.freedesktop.impl.portal.FileChooser")

public:
    explicit ChooserAdaptor(FileChooserPortal *portal)
        : QDBusAbstractAdaptor(portal), m_portal(portal)
    {
    }

public slots:
    uint OpenFile(const QDBusObjectPath &handle, const QString &app_id, const QString &parent_window,
                  const QString &title, const QVariantMap &options, const QDBusMessage &message,
                  QVariantMap &results)
    {
        return hold(ChooserRequest::Mode::Open, handle, app_id, parent_window, title, options, message, results);
    }
    uint SaveFile(const QDBusObjectPath &handle, const QString &app_id, const QString &parent_window,
                  const QString &title, const QVariantMap &options, const QDBusMessage &message,
                  QVariantMap &results)
    {
        return hold(ChooserRequest::Mode::Save, handle, app_id, parent_window, title, options, message, results);
    }
    uint SaveFiles(const QDBusObjectPath &handle, const QString &app_id, const QString &parent_window,
                   const QString &title, const QVariantMap &options, const QDBusMessage &message,
                   QVariantMap &results)
    {
        return hold(ChooserRequest::Mode::SaveFiles, handle, app_id, parent_window, title, options, message,
                    results);
    }

private:
    uint hold(ChooserRequest::Mode mode, const QDBusObjectPath &handle, const QString &appId,
              const QString &parentWindow, const QString &title, const QVariantMap &options,
              const QDBusMessage &message, QVariantMap &results)
    {
        Q_UNUSED(results)
        message.setDelayedReply(true);
        m_portal->receive(mode, handle.path(), appId, parentWindow, title, options, message);
        return FileChooserPortal::Other;
    }

    FileChooserPortal *m_portal;
};

// org.freedesktop.impl.portal.Request at the request's handle path.
class RequestObject : public QObject
{
    Q_OBJECT
public:
    RequestObject(FileChooserPortal *portal, const QString &handle)
        : QObject(portal), m_portal(portal), m_handle(handle)
    {
    }
    void close() { m_portal->close(m_handle); }

private:
    FileChooserPortal *m_portal;
    QString m_handle;
};

class RequestAdaptor : public QDBusAbstractAdaptor
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.freedesktop.impl.portal.Request")

public:
    explicit RequestAdaptor(RequestObject *request)
        : QDBusAbstractAdaptor(request), m_request(request)
    {
    }

public slots:
    void Close() { m_request->close(); }

private:
    RequestObject *m_request;
};

} // namespace

FileChooserPortal::FileChooserPortal(QObject *parent)
    : QObject(parent)
{
    new ChooserAdaptor(this);
}

FileChooserPortal::~FileChooserPortal()
{
    const QList<int> ids = m_pending.keys();
    for (int id : ids)
        finish(id, Other);
    if (!m_bus.isEmpty()) {
        QDBusConnection connection(m_bus);
        connection.unregisterService(QString::fromLatin1(ServiceName));
        connection.unregisterObject(QString::fromLatin1(ObjectPath));
    }
}

bool FileChooserPortal::registerOn(const QDBusConnection &bus)
{
    QDBusConnection connection = bus;
    if (!connection.isConnected())
        return false;
    if (!connection.registerObject(QString::fromLatin1(ObjectPath), this, QDBusConnection::ExportAdaptors))
        return false;
    const auto reply = connection.interface()->registerService(
        QString::fromLatin1(ServiceName), QDBusConnectionInterface::DontQueueService,
        QDBusConnectionInterface::DontAllowReplacement);
    if (!reply.isValid() || reply.value() != QDBusConnectionInterface::ServiceRegistered) {
        connection.unregisterObject(QString::fromLatin1(ObjectPath));
        return false;
    }
    m_bus = connection.name();
    return true;
}

void FileChooserPortal::receive(ChooserRequest::Mode mode, const QString &handle, const QString &appId,
                                const QString &parentWindow, const QString &title,
                                const QVariantMap &options, const QDBusMessage &message)
{
    const int id = m_nextId++;
    Pending pending;
    pending.message = message;
    pending.handle = handle;
    if (!m_bus.isEmpty() && !handle.isEmpty()) {
        auto *object = new RequestObject(this, handle);
        new RequestAdaptor(object);
        if (QDBusConnection(m_bus).registerObject(handle, object, QDBusConnection::ExportAdaptors))
            pending.requestObject = object;
        else
            delete object;
    }
    m_pending.insert(id, pending);
    emit requested(id, ChooserRequest::fromOptions(mode, appId, parentWindow, title, options));
}

void FileChooserPortal::close(const QString &handle)
{
    for (auto it = m_pending.cbegin(); it != m_pending.cend(); ++it) {
        if (it->handle == handle) {
            const int id = it.key();
            emit closeRequested(id);
            finish(id, Cancelled); // in case nobody answered the signal
            return;
        }
    }
}

void FileChooserPortal::finish(int id, uint response, const QVariantMap &results)
{
    const auto it = m_pending.find(id);
    if (it == m_pending.end())
        return;
    const Pending pending = it.value();
    m_pending.erase(it);
    if (pending.requestObject) {
        QDBusConnection(m_bus).unregisterObject(pending.handle);
        pending.requestObject->deleteLater();
    }
    QDBusMessage reply = pending.message.createReply();
    reply << response << results;
    QDBusConnection(m_bus.isEmpty() ? QStringLiteral("qt_default_session_bus") : m_bus).send(reply);
}

#include "FileChooserPortal.moc"
