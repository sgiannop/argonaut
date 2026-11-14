#pragma once

#include "Theme.h"

#include <QFrame>
#include <QList>
#include <QString>

class QHBoxLayout;
class QLineEdit;
class QStackedWidget;
class QToolButton;

// Nautilus-style location pill: clickable breadcrumbs ("Home / dev / x")
// that turn into a text entry when clicked on empty space or with Ctrl+L.
class PathBar : public QFrame
{
    Q_OBJECT

public:
    explicit PathBar(QWidget *parent = nullptr);

    void setPath(const QString &path); // clean path with '/' separators
    void setTheme(const Theme &theme);
    void startEditing();
    QToolButton *menuButton() const { return m_menuButton; }
    // The git branch pill ("main · 3"), hidden when `branch` is empty.
    void setGitInfo(const QString &branch, int changes, const QString &tooltip);

signals:
    void pathActivated(const QString &path);

protected:
    // A click on the free space types a path; a drag moves the window (it's
    // most of the header, which drags everywhere else).
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void rebuild();
    void fitCrumbs();
    void stopEditing();

    QString m_path;
    Theme m_theme;
    QStackedWidget *m_stack;
    QWidget *m_crumbs;
    QHBoxLayout *m_crumbLayout;
    QLineEdit *m_edit;
    QToolButton *m_menuButton;
    QToolButton *m_gitBadge;
    QList<QList<QWidget *>> m_groups; // a crumb button + its trailing "/"
    bool m_pressed = false;
    QPoint m_pressPos;
};
