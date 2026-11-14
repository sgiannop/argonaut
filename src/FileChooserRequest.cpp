#include "FileChooserRequest.h"

#include <QDBusArgument>
#include <QDBusMetaType>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMimeDatabase>
#include <QRegularExpression>
#include <QUrl>

// The D-Bus shapes of filters and choices, registered so qdbus_cast can read
// them and QDBusMessage can send them back.
struct PortalRule
{
    uint type = 0; // 0 glob, 1 mime type
    QString value;
};
struct PortalFilter
{
    QString name;
    QList<PortalRule> rules;
};
struct PortalChoice
{
    QString id;
    QString label;
    QList<QPair<QString, QString>> options;
    QString initial;
};
struct PortalChoiceValue
{
    QString id;
    QString value;
};
Q_DECLARE_METATYPE(PortalRule)
Q_DECLARE_METATYPE(PortalFilter)
Q_DECLARE_METATYPE(PortalChoice)
Q_DECLARE_METATYPE(PortalChoiceValue)

QDBusArgument &operator<<(QDBusArgument &arg, const PortalRule &rule)
{
    arg.beginStructure();
    arg << rule.type << rule.value;
    arg.endStructure();
    return arg;
}
const QDBusArgument &operator>>(const QDBusArgument &arg, PortalRule &rule)
{
    arg.beginStructure();
    arg >> rule.type >> rule.value;
    arg.endStructure();
    return arg;
}
QDBusArgument &operator<<(QDBusArgument &arg, const PortalFilter &filter)
{
    arg.beginStructure();
    arg << filter.name << filter.rules;
    arg.endStructure();
    return arg;
}
const QDBusArgument &operator>>(const QDBusArgument &arg, PortalFilter &filter)
{
    arg.beginStructure();
    arg >> filter.name >> filter.rules;
    arg.endStructure();
    return arg;
}
QDBusArgument &operator<<(QDBusArgument &arg, const PortalChoiceValue &choice)
{
    arg.beginStructure();
    arg << choice.id << choice.value;
    arg.endStructure();
    return arg;
}
const QDBusArgument &operator>>(const QDBusArgument &arg, PortalChoiceValue &choice)
{
    arg.beginStructure();
    arg >> choice.id >> choice.value;
    arg.endStructure();
    return arg;
}
QDBusArgument &operator<<(QDBusArgument &arg, const PortalChoice &choice)
{
    arg.beginStructure();
    arg << choice.id << choice.label;
    arg.beginArray(qMetaTypeId<PortalChoiceValue>());
    for (const auto &option : choice.options)
        arg << PortalChoiceValue{option.first, option.second};
    arg.endArray();
    arg << choice.initial;
    arg.endStructure();
    return arg;
}
const QDBusArgument &operator>>(const QDBusArgument &arg, PortalChoice &choice)
{
    arg.beginStructure();
    arg >> choice.id >> choice.label;
    choice.options.clear();
    arg.beginArray();
    while (!arg.atEnd()) {
        PortalChoiceValue option;
        arg >> option;
        choice.options.append({option.id, option.value});
    }
    arg.endArray();
    arg >> choice.initial;
    arg.endStructure();
    return arg;
}

namespace {

void registerTypes()
{
    static const bool done = [] {
        qDBusRegisterMetaType<PortalRule>();
        qDBusRegisterMetaType<QList<PortalRule>>();
        qDBusRegisterMetaType<PortalFilter>();
        qDBusRegisterMetaType<QList<PortalFilter>>();
        qDBusRegisterMetaType<PortalChoiceValue>();
        qDBusRegisterMetaType<QList<PortalChoiceValue>>();
        qDBusRegisterMetaType<PortalChoice>();
        qDBusRegisterMetaType<QList<PortalChoice>>();
        return true;
    }();
    Q_UNUSED(done)
}

// A value from the options map: already the type (sent from Qt in-process)
// or still a QDBusArgument (from the bus).
template <typename T>
bool read(const QVariant &value, T *out)
{
    if (!value.isValid())
        return false;
    if (value.canConvert<QDBusArgument>() && value.userType() == qMetaTypeId<QDBusArgument>()) {
        *out = qdbus_cast<T>(value.value<QDBusArgument>());
        return true;
    }
    if (value.canConvert<T>()) {
        *out = value.value<T>();
        return true;
    }
    return false;
}

// A NUL-terminated byte-string path (ay) -> QString.
QString bytesPath(const QVariant &value)
{
    QByteArray bytes;
    if (value.userType() == QMetaType::QByteArray)
        bytes = value.toByteArray();
    else if (!read(value, &bytes))
        return {};
    while (bytes.endsWith('\0'))
        bytes.chop(1);
    return bytes.isEmpty() ? QString() : QDir::cleanPath(QFile::decodeName(bytes));
}

ChooserRequest::Filter toFilter(const PortalFilter &wire)
{
    ChooserRequest::Filter filter;
    filter.name = wire.name;
    for (const PortalRule &rule : wire.rules)
        filter.rules.append({rule.type == 1, rule.value});
    return filter;
}

PortalFilter toWire(const ChooserRequest::Filter &filter)
{
    PortalFilter wire;
    wire.name = filter.name;
    for (const auto &rule : filter.rules)
        wire.rules.append({rule.mime ? 1u : 0u, rule.value});
    return wire;
}

bool sameFilter(const ChooserRequest::Filter &a, const ChooserRequest::Filter &b)
{
    if (a.name != b.name || a.rules.size() != b.rules.size())
        return false;
    for (int i = 0; i < a.rules.size(); ++i)
        if (a.rules[i].mime != b.rules[i].mime || a.rules[i].value != b.rules[i].value)
            return false;
    return true;
}

const QMimeDatabase &mimeDatabase()
{
    static const QMimeDatabase db;
    return db;
}

} // namespace

bool ChooserRequest::Filter::matchesAll() const
{
    if (rules.isEmpty())
        return true;
    for (const Rule &rule : rules)
        if (!rule.mime && rule.value == QLatin1String("*"))
            return true;
    return false;
}

bool ChooserRequest::Filter::matches(const QString &fileName, const QString &mimeType) const
{
    if (matchesAll())
        return true;
    QMimeType type; // looked up only when a mime rule needs it
    for (const Rule &rule : rules) {
        if (!rule.mime) {
            const QRegularExpression re(QRegularExpression::wildcardToRegularExpression(rule.value),
                                        QRegularExpression::CaseInsensitiveOption);
            if (re.match(fileName).hasMatch())
                return true;
            continue;
        }
        if (mimeType.isEmpty())
            continue;
        if (!type.isValid())
            type = mimeDatabase().mimeTypeForName(mimeType);
        if (rule.value.endsWith(QLatin1String("/*"))) { // "image/*"
            const QString group = rule.value.chopped(1);
            if (mimeType.startsWith(group))
                return true;
            for (const QString &parent : type.allAncestors())
                if (parent.startsWith(group))
                    return true;
            continue;
        }
        if (mimeType == rule.value || (type.isValid() && type.inherits(rule.value)))
            return true;
    }
    return false;
}

QString ChooserRequest::withoutMnemonic(const QString &label)
{
    QString out;
    for (int i = 0; i < label.size(); ++i) {
        if (label[i] == QLatin1Char('_')) {
            if (i + 1 < label.size() && label[i + 1] == QLatin1Char('_'))
                out += label[++i];
            continue;
        }
        out += label[i];
    }
    return out;
}

ChooserRequest ChooserRequest::fromOptions(Mode mode, const QString &appId, const QString &parentWindow,
                                           const QString &title, const QVariantMap &options)
{
    registerTypes();
    ChooserRequest request;
    request.mode = mode;
    request.appId = appId;
    request.parentWindow = parentWindow;
    request.title = title;
    request.acceptLabel = withoutMnemonic(options.value(QStringLiteral("accept_label")).toString());
    request.multiple = mode == Mode::Open && options.value(QStringLiteral("multiple")).toBool();
    request.directory = mode == Mode::Open && options.value(QStringLiteral("directory")).toBool();
    if (options.contains(QStringLiteral("modal")))
        request.modal = options.value(QStringLiteral("modal")).toBool();

    QList<PortalFilter> filters;
    if (read(options.value(QStringLiteral("filters")), &filters))
        for (const PortalFilter &filter : filters)
            request.filters.append(toFilter(filter));
    PortalFilter current;
    if (read(options.value(QStringLiteral("current_filter")), &current)) {
        const Filter wanted = toFilter(current);
        for (int i = 0; i < request.filters.size(); ++i)
            if (sameFilter(request.filters[i], wanted))
                request.currentFilter = i;
        if (request.currentFilter < 0) { // not in the list: offer it too
            request.filters.append(wanted);
            request.currentFilter = int(request.filters.size()) - 1;
        }
    }
    if (request.currentFilter < 0 && !request.filters.isEmpty())
        request.currentFilter = 0;

    QList<PortalChoice> choices;
    if (read(options.value(QStringLiteral("choices")), &choices))
        for (const PortalChoice &choice : choices)
            request.choices.append({choice.id, choice.label, choice.options, choice.initial});

    request.currentName = options.value(QStringLiteral("current_name")).toString();
    request.currentFolder = bytesPath(options.value(QStringLiteral("current_folder")));
    request.currentFile = bytesPath(options.value(QStringLiteral("current_file")));
    if (request.currentName.isEmpty() && !request.currentFile.isEmpty())
        request.currentName = QFileInfo(request.currentFile).fileName();
    if (request.currentFolder.isEmpty() && !request.currentFile.isEmpty())
        request.currentFolder = QFileInfo(request.currentFile).absolutePath();

    QList<QByteArray> files;
    if (read(options.value(QStringLiteral("files")), &files)) {
        for (QByteArray name : files) {
            while (name.endsWith('\0'))
                name.chop(1);
            const QString decoded = QFileInfo(QFile::decodeName(name)).fileName(); // names only
            if (!decoded.isEmpty())
                request.files.append(decoded);
        }
    }
    return request;
}

QVariantMap ChooserRequest::results(const QStringList &paths,
                                    const QList<QPair<QString, QString>> &choiceValues,
                                    int filterIndex) const
{
    registerTypes();
    QVariantMap out;
    QStringList uris;
    for (const QString &path : paths)
        uris.append(QUrl::fromLocalFile(path).toString(QUrl::FullyEncoded));
    out.insert(QStringLiteral("uris"), uris);
    if (!choiceValues.isEmpty()) {
        QList<PortalChoiceValue> values;
        for (const auto &value : choiceValues)
            values.append({value.first, value.second});
        out.insert(QStringLiteral("choices"), QVariant::fromValue(values));
    }
    if (filterIndex >= 0 && filterIndex < filters.size())
        out.insert(QStringLiteral("current_filter"), QVariant::fromValue(toWire(filters[filterIndex])));
    out.insert(QStringLiteral("writable"), mode != Mode::Open);
    return out;
}

QStringList ChooserRequest::saveFilesPaths(const QString &folder) const
{
    QStringList paths;
    for (const QString &name : files)
        paths.append(QDir(folder).filePath(name));
    return paths;
}
