#pragma once

#include "FileChooserRequest.h"

#include <QWidget>

class QCheckBox;
class QComboBox;
class QLineEdit;
class QPushButton;

// The bottom of an Open/Save window (MainWindow in chooser mode): the file
// name (Save), the app's extra options (choices), the file-type filter, and
// Cancel / Open or Save. Styled by MainWindow's style sheet.
class ChooserBar : public QWidget
{
    Q_OBJECT

public:
    ChooserBar(const ChooserRequest &request, QWidget *parent = nullptr);

    QString name() const;
    void setName(const QString &name);
    void focusName(); // selects the base name, like the inline rename
    QLineEdit *nameEdit() const { return m_name; }

    int currentFilter() const; // index into request.filters, or -1
    QList<QPair<QString, QString>> choiceValues() const;
    void setAcceptEnabled(bool enabled);

signals:
    void accepted();
    void rejected();
    void filterChanged(int index);

private:
    QLineEdit *m_name = nullptr;
    QComboBox *m_filter = nullptr;
    QPushButton *m_accept = nullptr;
    struct ChoiceWidget
    {
        QString id;
        QCheckBox *check = nullptr;
        QComboBox *combo = nullptr;
    };
    QList<ChoiceWidget> m_choices;
};
