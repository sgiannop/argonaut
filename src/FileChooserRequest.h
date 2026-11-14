#pragma once

#include <QList>
#include <QPair>
#include <QString>
#include <QStringList>
#include <QMetaType>
#include <QVariantMap>

// One Open/Save request from xdg-desktop-portal's FileChooser backend
// interface (org.freedesktop.impl.portal.FileChooser), decoded from its
// D-Bus options, plus the answer we send back. No widgets, so it's testable.
// Spec: https://flatpak.github.io/xdg-desktop-portal/docs/doc-org.freedesktop.impl.portal.FileChooser.html
struct ChooserRequest
{
    enum class Mode { Open, Save, SaveFiles };

    // One rule of a filter: a glob ("*.jpg", case-insensitive) or a mime type
    // ("image/png"; types that inherit it match too).
    struct Rule
    {
        bool mime = false;
        QString value;
    };
    struct Filter
    {
        QString name;
        QList<Rule> rules;
        // True for a file with this name and (name-based) mime type.
        bool matches(const QString &fileName, const QString &mimeType) const;
        bool matchesAll() const; // no rules, or a "*" glob
    };
    // A checkbox (no options; values "true"/"false") or a list of options.
    struct Choice
    {
        QString id;
        QString label;
        QList<QPair<QString, QString>> options; // id, label
        QString initial;
    };

    Mode mode = Mode::Open;
    QString appId;        // "org.mozilla.firefox", or empty for host apps
    QString parentWindow; // "x11:1a00004", "wayland:…" or empty
    QString title;
    QString acceptLabel;  // mnemonic underscores removed
    bool multiple = false;
    bool directory = false;
    bool modal = true;
    QList<Filter> filters;
    int currentFilter = -1; // index into filters, or -1
    QList<Choice> choices;
    QString currentName;   // Save: the suggested name
    QString currentFolder; // start here if set
    QString currentFile;   // Save: an existing file being saved again
    QStringList files;     // SaveFiles: the names to save

    // Decodes the a{sv} options as xdg-desktop-portal sends them.
    static ChooserRequest fromOptions(Mode mode, const QString &appId, const QString &parentWindow,
                                      const QString &title, const QVariantMap &options);

    // The a{sv} results: uris (as), choices (a(ss)), current_filter
    // ((sa(us)), when filters were offered) and writable (b).
    QVariantMap results(const QStringList &paths, const QList<QPair<QString, QString>> &choiceValues,
                        int filterIndex) const;

    // SaveFiles answers one path per requested name, inside `folder`.
    QStringList saveFilesPaths(const QString &folder) const;

    // "_Save" -> "Save" (GTK mnemonics), "__" -> "_".
    static QString withoutMnemonic(const QString &label);
};
Q_DECLARE_METATYPE(ChooserRequest)
