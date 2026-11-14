#include "MainWindow.h"
#if defined(ARGONAUT_DBUS)
#include "ChooserBar.h"
#include "FileChooserPortal.h"
#endif

#include "AboutDialog.h"
#include "Access.h"
#include "BatchRenameDialog.h"
#include "CommandPalette.h"
#include "ConnectDialog.h"
#include "OrganizerDialog.h"
#include "Remote.h"
#include "HelpDialog.h"
#include "Naming.h"

#include "Format.h"

#include "Bookmarks.h"
#include "ConflictDialog.h"
#include "CompressDialog.h"
#include "ContentType.h"
#include "DirectoryModel.h"
#include "DiskUsageModel.h"
#include "FileFilterProxy.h"
#include "FileItemDelegate.h"
#include "FileOperations.h"
#include "HeaderBar.h"
#include "OpenWith.h"
#include "OpenWithDialog.h"
#include "OperationsPanel.h"
#include "PathBar.h"
#include "PreferencesDialog.h"
#include "RecentFiles.h"
#include "Starred.h"
#include "PropertiesDialog.h"
#include "QuickLook.h"
#include "Thumbnailer.h"
#if defined(ARGONAUT_TERMINAL)
#include "TerminalWidget.h"
#endif
#include "Toast.h"
#include "UsageDelegate.h"
#include "Trash.h"

#include <QAction>
#include <functional>
#include <QApplication>
#include <QClipboard>
#include <QCloseEvent>
#include <QDesktopServices>
#include <QDir>
#include <QEvent>
#include <QFileDialog>
#include <QFileIconProvider>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QKeyEvent>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QListWidget>
#include <QLocale>
#include <QActionGroup>
#include <QMenu>
#include <QMenuBar>
#include <QMimeData>
#include <QMimeDatabase>
#include <QMessageBox>
#include <QMouseEvent>
#include <QFileSystemWatcher>
#include <QInputDialog>
#include <QProcess>
#include <QPushButton>
#include <QSettings>
#include <QSocketNotifier>
#include <QRegularExpression>
#include <QScrollBar>
#include <QPainter>
#include <QCursor>
#include <QGuiApplication>
#include <QPointer>
#include <QScreen>
#include <QSplitter>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QStorageInfo>
#include <QStyledItemDelegate>
#include <QTabBar>
#include <QSignalBlocker>
#include <QTimer>
#include <QWheelEvent>
#include <QTreeView>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>
#include <QWindow>

#if defined(Q_OS_UNIX)
#include <fcntl.h>
#include <unistd.h>
#endif

namespace {

constexpr int PathRole = Qt::UserRole;
constexpr int SeparatorRole = Qt::UserRole + 1;
constexpr int KindRole = Qt::UserRole + 2;  // SidebarKind
constexpr int EjectRole = Qt::UserRole + 3; // bool: show an eject button
enum SidebarKind { PlaceItem, BookmarkItem, DriveItem, NetworkItem };
const QString kConnectPlace = QStringLiteral("connect:///"); // the sidebar's "Connect to Server…" row

QStringList localPaths(const QMimeData *mime);

// The eject button's area inside a sidebar row.
QRect ejectRect(const QRect &row)
{
    return QRect(row.right() - 30, row.center().y() - 12, 24, 24);
}
constexpr int ResizeMargin = 5;

// Separator rows are drawn as a thin line; other rows are drawn normally so
// the style sheet's rounded hover/selection applies.
class SidebarDelegate : public QStyledItemDelegate
{
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    QColor lineColor;

    QIcon ejectIcon;

    void paint(QPainter *painter, const QStyleOptionViewItem &option,
               const QModelIndex &index) const override
    {
        if (!index.data(SeparatorRole).toBool()) {
            QStyleOptionViewItem opt = option;
            if (index.data(EjectRole).toBool())
                opt.rect.setRight(opt.rect.right() - 30); // room for the button
            QStyledItemDelegate::paint(painter, opt, index);
            if (index.data(EjectRole).toBool())
                ejectIcon.paint(painter, ejectRect(option.rect).adjusted(4, 4, -4, -4));
            return;
        }
        const int y = option.rect.center().y();
        painter->save();
        painter->setPen(QPen(lineColor, 1));
        painter->drawLine(option.rect.left() + 4, y, option.rect.right() - 4, y);
        painter->restore();
    }

    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        if (index.data(SeparatorRole).toBool())
            return QSize(0, 13);
        return QStyledItemDelegate::sizeHint(option, index);
    }
};

QString styleSheetFor(const Theme &t)
{
    QString css = QStringLiteral(R"(
QWidget#root { background: @outline; }
QWidget#sidebarPane { background: @sidebar; }
QWidget#contentPane { background: @window; }
QLabel#title { color: @fg; font-weight: bold; }

QToolButton#flat { background: transparent; border: none; border-radius: 6px;
    padding: 5px; color: @fg; }
QToolButton#flat:hover { background: @hover; }
QToolButton#flat:pressed, QToolButton#flat:checked { background: @pressed; }
QToolButton#flat::menu-indicator { image: none; width: 0px; }

QToolButton#win { background: @button; border: none; border-radius: 12px; }
QToolButton#win:hover { background: @buttonHover; }
QToolButton#win:pressed { background: @pressed; }

QFrame#pathBar { background: @pathBg; border-radius: 8px; }
QToolButton#crumb, QToolButton#crumbCurrent { background: transparent; border: none;
    border-radius: 6px; padding: 3px 7px; color: @dim; }
QToolButton#crumbCurrent { color: @fg; font-weight: bold; }
QToolButton#crumb:hover, QToolButton#crumbCurrent:hover { background: @hover; color: @fg; }
QLabel#crumbSep { color: @dim; }
QLineEdit#pathEdit { background: transparent; border: none; color: @fg; padding: 0px 8px;
    selection-background-color: @accent; }
QLineEdit#searchEdit { background: @pathBg; border: none; border-radius: 8px; color: @fg;
    padding: 0px 10px; selection-background-color: @accent; }

QListWidget#sidebar { background: @sidebar; color: @fg; border: none; padding: 0px 6px 6px 6px;
    outline: 0; }
QListWidget#sidebar::item { height: 36px; padding-left: 8px; border: none; border-radius: 6px;
    margin: 1px 0px; color: @fg; }
QListWidget#sidebar::item:hover { background: @hover; }
QListWidget#sidebar::item:selected { background: @selected; color: @fg; }

QListView#files { background: @window; border: none; outline: 0; padding: 6px 6px; }
QTreeView#detailView { background: @window; border: none; outline: 0; padding: 0px 6px 6px 6px; }
QTreeView#detailView::item, QTreeView#detailView::item:selected,
QTreeView#detailView::branch { background: transparent; }
QTreeView#detailView QHeaderView { background: @window; border: none; }
QTreeView#detailView QHeaderView::section { background: @window; color: @dim; border: none;
    padding: 8px 10px 6px 16px; font-weight: bold; }
QTreeView#detailView QHeaderView::section:hover { color: @fg; }
QTreeView#analyzeView { background: @window; border: none; outline: 0; padding: 0px 6px 6px 6px; }
QTreeView#analyzeView::item { border: none; padding-left: 4px; }
QTreeView#analyzeView::item:hover { background: @hover; }
QTreeView#analyzeView::item:selected { background: @itemSel; color: @fg; }
QTreeView#analyzeView QHeaderView { background: @window; border: none; }
QTreeView#analyzeView QHeaderView::section { background: @window; color: @dim; border: none;
    padding: 8px 10px 6px 16px; font-weight: bold; }
QTreeView#analyzeView QHeaderView::section:hover { color: @fg; }
QLabel#floatingBar { background: @popup; color: @fg; border: 1px solid @border;
    border-radius: 8px; padding: 5px 12px; }

QSplitter#mainSplitter::handle { background: @splitter; }

QScrollBar:vertical { background: transparent; width: 10px; margin: 2px 0px; }
QScrollBar::handle:vertical { background: @scroll; border-radius: 3px; min-height: 32px;
    margin: 0px 2px; }
QScrollBar::handle:vertical:hover { background: @scrollHover; }
QScrollBar:horizontal { background: transparent; height: 10px; margin: 0px 2px; }
QScrollBar::handle:horizontal { background: @scroll; border-radius: 3px; min-width: 32px;
    margin: 2px 0px; }
QScrollBar::handle:horizontal:hover { background: @scrollHover; }
QScrollBar::add-line, QScrollBar::sub-line { width: 0px; height: 0px; }
QScrollBar::add-page, QScrollBar::sub-page { background: none; }

QMenu { background: @popup; color: @fg; border: 1px solid @border; border-radius: 10px;
    padding: 6px; }
QMenu::item { padding: 7px 32px 7px 28px; border-radius: 6px; }
QMenu::item:selected { background: @hover; }
QMenu::item:disabled { color: @dim; }
QMenu::separator { height: 1px; background: @border; margin: 5px 6px; }

QFrame#opsPanel { background: @popup; border: 1px solid @border; border-radius: 12px; }
QLabel#opsTitle { color: @fg; }
QLabel#opsDetail { color: @dim; }
QProgressBar#opsProgress { background: @pathBg; border: none; border-radius: 3px; }
QProgressBar#opsProgress::chunk { background: @accent; border-radius: 3px; }
QFrame#toast { background: @popup; border: 1px solid @border; border-radius: 20px; }
QLabel#toastText { color: @fg; }
QToolButton#toastAction { background: @button; color: @fg; border: none; border-radius: 14px;
    padding: 5px 14px; font-weight: bold; }
QToolButton#toastAction:hover { background: @buttonHover; }

QWidget#menuRow { background: @sidebar; border: none; border-bottom: 1px solid @border; }
QMenuBar#menuBar { background: @sidebar; color: @fg; border: none; padding: 2px 4px; }
QMenuBar#menuBar::item { background: transparent; padding: 4px 10px; border-radius: 6px; }
QMenuBar#menuBar::item:selected { background: @hover; }
QMenuBar#menuBar::item:pressed { background: @pressed; }
QLineEdit#renameEdit { background: @window; color: @fg; border: 2px solid @accent;
    border-radius: 6px; padding: 1px 4px; selection-background-color: @accent;
    selection-color: #ffffff; }

QTabBar#tabs { background: @window; border: none; }
QTabBar#tabs::tab { background: transparent; color: @dim; border: none; border-radius: 6px;
    padding: 5px 8px 5px 14px; margin: 3px 2px; min-width: 120px; }
QTabBar#tabs::tab:selected { background: @pathBg; color: @fg; }
QTabBar#tabs::tab:hover:!selected { background: @hover; }
QWidget#trashBar { background: @pathBg; border-bottom: 1px solid @border; }
QLabel#trashBarText { color: @fg; }
QToolButton#barButton { background: @button; color: @fg; border: none; border-radius: 6px;
    padding: 5px 14px; font-weight: bold; }
QToolButton#barButton:hover { background: @buttonHover; }
QToolButton#barButton:disabled { color: @dim; }
QWidget#terminalPanel { background: @window; }
QWidget#terminalBar { background: @window; border-top: 1px solid @border; }
QLabel#terminalTitle { color: @dim; font-weight: bold; }
QSplitter#contentSplitter::handle { background: @border; }

QDialog#conflictDialog { background: @window; }
QLabel#conflictTitle { color: @fg; font-weight: bold; font-size: 15px; }
QLabel#conflictText { color: @dim; }
QCheckBox#conflictCheck { color: @fg; spacing: 8px; }
QCheckBox#conflictCheck::indicator { width: 14px; height: 14px; border: 1px solid @dim;
    border-radius: 4px; background: @pathBg; }
QCheckBox#conflictCheck::indicator:checked { background: @accent; border-color: @accent; }
QWidget#conflictCard { background: @pathBg; border-radius: 10px; }
QLabel#conflictCardHeading { color: @fg; font-weight: bold; }
QLineEdit#conflictName { background: @pathBg; color: @fg; border: none; border-radius: 6px;
    padding: 6px 8px; selection-background-color: @accent; }
QPushButton#conflictButton { background: @button; color: @fg; border: none; border-radius: 6px;
    padding: 7px 14px; }
QPushButton#conflictButton:hover { background: @buttonHover; }
QPushButton#conflictPrimary { background: @accent; color: #ffffff; border: none;
    border-radius: 6px; padding: 7px 16px; font-weight: bold; }

QDialog#propertiesDialog { background: @window; }
QLabel#propLabel { color: @dim; }
QLabel#propValue { color: @fg; }
QLabel#propHeading { color: @fg; font-weight: bold; padding-top: 10px; }
QLabel#propWarning { color: #c88800; font-weight: bold; }
QLabel#propDanger { color: #e01b24; font-weight: bold; }
QComboBox#propCombo { background: @pathBg; color: @fg; border: none; border-radius: 6px;
    padding: 5px 10px; }
QComboBox#propCombo:disabled { color: @dim; }
QComboBox#propCombo::drop-down { border: none; width: 18px; }
QComboBox#propCombo QAbstractItemView { background: @popup; color: @fg; border: 1px solid @border;
    selection-background-color: @hover; selection-color: @fg; outline: 0; }
QDialog#openWithDialog { background: @window; }
QListWidget#appList { background: @pathBg; color: @fg; border-radius: 8px; padding: 4px; outline: 0; }
QListWidget#appList::item { padding: 6px; border-radius: 6px; }
QListWidget#appList::item:selected { background: @itemSel; color: @fg; }
QListWidget#appList::item:hover:!selected { background: @hover; }

QDialog#preferences { background: @window; }
QScrollArea#prefScroll, QWidget#prefContent { background: @window; }
QDialog#connectDialog { background: @window; }
QLineEdit#connectAddress { background: @pathBg; color: @fg; border: none; border-radius: 6px;
    padding: 7px 9px; selection-background-color: @accent; }
QLineEdit#connectAddress:disabled { color: @dim; }
QLabel#prefHeading { color: @fg; font-weight: bold; font-size: 15px; }
QLabel#prefHint { color: @dim; }
QCheckBox#prefCheck { color: @fg; spacing: 8px; }
QCheckBox#prefCheck::indicator { width: 14px; height: 14px; border: 1px solid @dim;
    border-radius: 4px; background: @pathBg; }
QCheckBox#prefCheck::indicator:checked { background: @accent; border-color: @accent; }
QWidget#quickLook { background: @window; }
QLabel#paneCaption { background: @window; color: @dim; padding: 5px 14px; border-bottom: 1px solid @border; }
QLabel#paneCaption[active="true"] { color: @accent; font-weight: bold; }
QSplitter#paneSplitter::handle { background: @border; }
QLabel#qlTitle { color: @fg; font-weight: bold; font-size: 15px; }
QLabel#qlDetails { color: @dim; }
QPlainTextEdit#qlText { background: @pathBg; color: @fg; border: none; border-radius: 8px; padding: 6px; }
QTreeWidget#qlArchive { background: @window; color: @fg; border: none; }
QTreeWidget#qlArchive QHeaderView::section { background: @window; color: @dim; border: none;
    padding: 4px 8px; font-weight: bold; }
QPushButton#prefButton { background: @pathBg; color: @fg; border: none; border-radius: 6px;
    padding: 7px 16px; }
QPushButton#prefButton:hover { background: @hover; }
QPushButton#prefButton:disabled { color: @dim; }
QToolButton#themeCard { background: transparent; color: @fg; border: 2px solid transparent;
    border-radius: 12px; padding: 8px 8px 6px 8px; }
QToolButton#themeCard:hover { background: @hover; }
QToolButton#themeCard:checked { border-color: @accent; }

QToolButton#searchOption { background: @pathBg; color: @dim; border: none; border-radius: 8px;
    padding: 0 12px; font-weight: bold; }
QToolButton#searchOption:hover { color: @fg; }
QToolButton#searchOption:checked { background: @accent; color: #ffffff; }
QToolButton#gitBadge { background: transparent; color: @dim; border: none; border-radius: 6px;
    padding: 2px 8px; font-weight: bold; }
QToolButton#gitBadge:hover { background: @hover; }
QFrame#commandPalette { background: @popup; border: 1px solid @border; border-radius: 12px; }
QLineEdit#paletteEdit { background: @pathBg; color: @fg; border: none; border-radius: 8px;
    padding: 8px 10px; font-size: 14px; selection-background-color: @accent; }
QListWidget#paletteList { background: transparent; color: @fg; outline: 0; }
QDialog#helpDialog { background: @window; }
QWidget#helpSide { background: @sidebar; border-right: 1px solid @border; }
QLabel#helpTitle { color: @fg; font-weight: bold; font-size: 15px; padding: 2px 4px; }
QLabel#helpNoMatch { color: @dim; padding: 4px; }
QLineEdit#helpSearch { background: @pathBg; color: @fg; border: none; border-radius: 6px;
    padding: 6px 8px; selection-background-color: @accent; }
QListWidget#helpTopics { background: transparent; color: @fg; outline: 0; }
QListWidget#helpTopics::item { padding: 7px 8px; border-radius: 6px; }
QListWidget#helpTopics::item:selected { background: @selected; color: @fg; }
QListWidget#helpTopics::item:hover:!selected { background: @hover; }
QTextBrowser#helpPage { background: @window; color: @fg; border: none; }

QDialog#aboutDialog { background: @window; }
QLabel#aboutName { color: @fg; font-weight: bold; font-size: 26px; }
QLabel#aboutVersion { background: @itemSel; color: @accent; font-weight: bold; border-radius: 10px;
    padding: 3px 12px; }
QLabel#aboutTagline { color: @dim; font-size: 14px; }
QToolButton#aboutTab { background: transparent; color: @dim; border: none; border-radius: 14px;
    padding: 5px 14px; font-weight: bold; }
QToolButton#aboutTab:hover { background: @hover; color: @fg; }
QToolButton#aboutTab:checked { background: @pathBg; color: @fg; }
QLabel#aboutText { color: @fg; }
QLabel#aboutDim { color: @dim; }
QWidget#aboutCard { background: @pathBg; border-radius: 10px; }
QLabel#aboutCardHeading { color: @fg; font-weight: bold; }
QLabel#aboutCardText { color: @dim; }
QLabel#aboutLabel { color: @dim; }
QWidget#chooserBar { background: @sidebar; border-top: 1px solid @border; }
QLabel#chooserLabel { color: @fg; }
QLabel#aboutValue { color: @fg; }
QPushButton#aboutClose { background: @accent; color: #ffffff; border: none; border-radius: 6px;
    padding: 7px 18px; font-weight: bold; }
)");
    const QList<std::pair<const char *, QColor>> tokens = {
        {"@outline", t.outline},   {"@sidebar", t.sidebar},       {"@window", t.window},
        {"@fg", t.fg},             {"@dim", t.dimFg},             {"@hover", t.hover},
        {"@pressed", t.pressed},   {"@selected", t.selected},     {"@buttonHover", t.buttonHover},
        {"@button", t.button},     {"@pathBg", t.pathBg},         {"@accent", t.accent},
        {"@popup", t.popup},       {"@border", t.border},         {"@scrollHover", t.scrollHover},
        {"@scroll", t.scroll},     {"@splitter", t.dark ? t.sidebar : t.border},
        {"@itemSel", t.itemSelected},
    };
    // Longer tokens first where one is a prefix of another (@button/@buttonHover…).
    for (const auto &[token, color] : tokens)
        css.replace(QLatin1String(token), cssColor(color));
    return css;
}

} // namespace

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    m_csd = qEnvironmentVariableIsEmpty("ARGONAUT_NATIVE_DECORATIONS");
#if defined(Q_OS_MACOS)
    m_csd = false; // keep the native traffic-light title bar on macOS
#endif
    if (m_csd)
        setWindowFlag(Qt::FramelessWindowHint);

    resize(1150, 720);
    setWindowTitle("Argonaut");

    m_ops = new FileOperations(this);
    m_ops->setAskOnConflict(true);
    connect(m_ops, &FileOperations::jobFinished, this, &MainWindow::onJobFinished);
    connect(m_ops, &FileOperations::conflictRaised, this, &MainWindow::askConflict);
    connect(m_ops, &FileOperations::undoChanged, this, &MainWindow::updateEditActions);
    connect(QApplication::clipboard(), &QClipboard::dataChanged, this,
            &MainWindow::updateEditActions);

    m_sidebarRefresh = new QTimer(this);
    m_sidebarRefresh->setSingleShot(true);
    m_sidebarRefresh->setInterval(250);
    connect(m_sidebarRefresh, &QTimer::timeout, this, [this] {
        populateSidebar();
        syncSidebarSelection();
    });

    setupActions();
    setupUi();
    applyTheme(true);
    updateWindowState();
    loadSettings();

    // The sidebar follows bookmark edits (also Nautilus's) and drives being
    // plugged in or removed.
    auto *bookmarksWatcher = new QFileSystemWatcher(this);
    const QString bookmarksFile = Bookmarks::filePath();
    QDir().mkpath(QFileInfo(bookmarksFile).absolutePath());
    bookmarksWatcher->addPath(QFileInfo(bookmarksFile).absolutePath()); // atomic saves replace the file
    connect(bookmarksWatcher, &QFileSystemWatcher::directoryChanged, this,
            [this] { scheduleSidebarRefresh(); updateBookmarkAction(); });
    // Servers connect and disconnect inside one FUSE mount, so /proc/mounts
    // doesn't change: watch GVfs's folder of mounts instead.
    if (const QString gvfs = Remote::gvfsRoot(); !gvfs.isEmpty()) {
        auto *gvfsWatcher = new QFileSystemWatcher({gvfs}, this);
        connect(gvfsWatcher, &QFileSystemWatcher::directoryChanged, this, &MainWindow::scheduleSidebarRefresh);
    }
#if defined(Q_OS_LINUX)
    m_mountsFd = ::open("/proc/self/mounts", O_RDONLY | O_CLOEXEC);
    if (m_mountsFd >= 0) {
        auto *mounts = new QSocketNotifier(m_mountsFd, QSocketNotifier::Exception, this);
        connect(mounts, &QSocketNotifier::activated, this, &MainWindow::scheduleSidebarRefresh);
    }
#endif
    if (m_mountsFd < 0) {
        auto *poll = new QTimer(this);
        connect(poll, &QTimer::timeout, this, [this] {
            QStringList now;
            for (const QStorageInfo &vol : QStorageInfo::mountedVolumes())
                now << vol.rootPath();
            if (now != m_lastMounts)
                scheduleSidebarRefresh();
        });
        poll->start(3000);
    }

    // Watch the native window's mouse events: back/forward mouse buttons
    // anywhere in the window, plus edge resizing when frameless.
    winId();
    if (QWindow *handle = windowHandle())
        handle->installEventFilter(this);
}

// ---------------------------------------------------------------- setup ---

void MainWindow::setupActions()
{
    m_backAct = new QAction(tr("Back"), this);
    m_backAct->setShortcuts({QKeySequence::Back, QKeySequence(Qt::ALT | Qt::Key_Left)});
    connect(m_backAct, &QAction::triggered, this, &MainWindow::goBack);

    m_forwardAct = new QAction(tr("Forward"), this);
    m_forwardAct->setShortcuts({QKeySequence::Forward, QKeySequence(Qt::ALT | Qt::Key_Right)});
    connect(m_forwardAct, &QAction::triggered, this, &MainWindow::goForward);

    m_upAct = new QAction(tr("Up"), this);
    m_upAct->setShortcut(QKeySequence(Qt::ALT | Qt::Key_Up));
    connect(m_upAct, &QAction::triggered, this, &MainWindow::goUp);

    m_homeAct = new QAction(tr("Home"), this);
    m_homeAct->setShortcut(QKeySequence(Qt::ALT | Qt::Key_Home));
    connect(m_homeAct, &QAction::triggered, this, [this] { navigateTo(QDir::homePath()); });

    m_locationAct = new QAction(tr("Enter Location…"), this);
    m_locationAct->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_L));
    connect(m_locationAct, &QAction::triggered, this, [this] {
        setSearchActive(false);
        m_pathBar->startEditing();
    });

    m_hiddenAct = new QAction(tr("Show Hidden Files"), this);
    m_hiddenAct->setCheckable(true);
    m_hiddenAct->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_H));
    connect(m_hiddenAct, &QAction::toggled, this, &MainWindow::setShowHidden);

    m_searchAct = new QAction(tr("Search"), this);
    m_searchAct->setCheckable(true);
    m_searchAct->setShortcut(QKeySequence::Find);
    connect(m_searchAct, &QAction::toggled, this, &MainWindow::setSearchActive);

    m_gridAct = new QAction(tr("Grid"), this);
    m_gridAct->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_1));
    m_gridAct->setCheckable(true);
    m_gridAct->setChecked(true);
    connect(m_gridAct, &QAction::triggered, this, [this] { setGridMode(true); });
    m_listAct = new QAction(tr("List"), this);
    m_listAct->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_2));
    m_listAct->setCheckable(true);
    connect(m_listAct, &QAction::triggered, this, [this] { setGridMode(false); });
    auto *viewGroup = new QActionGroup(this);
    viewGroup->addAction(m_gridAct);
    viewGroup->addAction(m_listAct);
    m_analyzeAct = new QAction(tr("Analyze Disk Usage"), this);
    m_analyzeAct->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_3));
    m_analyzeAct->setCheckable(true);
    connect(m_analyzeAct, &QAction::triggered, this, [this] { setAnalyzeMode(true); });
    viewGroup->addAction(m_analyzeAct);
    m_analyzeFilesAct = new QAction(tr("Show Files When Analyzing"), this);
    m_analyzeFilesAct->setCheckable(true);
    connect(m_analyzeFilesAct, &QAction::toggled, this, &MainWindow::setAnalyzeShowFiles);

    m_sidebarAct = new QAction(tr("Sidebar"), this);
    m_sidebarAct->setShortcut(QKeySequence(Qt::Key_F9));
    m_sidebarAct->setCheckable(true);
    m_sidebarAct->setChecked(true);
    connect(m_sidebarAct, &QAction::toggled, this, [this](bool on) { m_sidebarPane->setVisible(on); });

    // Sorting (also changed by clicking the list view's column headers).
    auto *sortGroup = new QActionGroup(this);
    const char *sortNames[] = {QT_TR_NOOP("By Name"), QT_TR_NOOP("By Size"), QT_TR_NOOP("By Type"),
                               QT_TR_NOOP("By Modification Date")};
    for (int column = 0; column < int(std::size(sortNames)); ++column) { // Owner… sort from the header
        auto *act = new QAction(tr(sortNames[column]), this);
        act->setCheckable(true);
        sortGroup->addAction(act);
        connect(act, &QAction::triggered, this,
                [this, column] { sortBy(column, m_model->sortOrder()); });
        m_sortColumnActs.append(act);
    }
    m_sortColumnActs.first()->setChecked(true);
    auto *orderGroup = new QActionGroup(this);
    m_sortAscAct = new QAction(tr("Ascending"), this);
    m_sortDescAct = new QAction(tr("Descending"), this);
    for (QAction *act : {m_sortAscAct, m_sortDescAct}) {
        act->setCheckable(true);
        orderGroup->addAction(act);
    }
    m_sortAscAct->setChecked(true);
    connect(m_sortAscAct, &QAction::triggered, this,
            [this] { sortBy(m_model->sortColumn(), Qt::AscendingOrder); });
    connect(m_sortDescAct, &QAction::triggered, this,
            [this] { sortBy(m_model->sortColumn(), Qt::DescendingOrder); });

#if defined(ARGONAUT_TERMINAL)
    m_terminalAct = new QAction(tr("Terminal"), this);
    m_terminalAct->setCheckable(true);
    m_terminalAct->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_QuoteLeft)); // like VS Code
    connect(m_terminalAct, &QAction::toggled, this, [this](bool on) {
        if (on)
            showTerminal(m_current);
        else
            hideTerminal();
    });
    m_openTerminalHereAct = new QAction(tr("Open in Terminal"), this);
    connect(m_openTerminalHereAct, &QAction::triggered, this, [this] {
        showTerminal(m_contextFolder.isEmpty() ? m_current : m_contextFolder);
    });
#endif

    m_newFolderAct = new QAction(tr("New Folder"), this);
    m_newFolderAct->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_N));
    connect(m_newFolderAct, &QAction::triggered, this, [this] { createNewItem(true); });

    m_newFileAct = new QAction(tr("New File"), this);
    m_newFileAct->setShortcut(QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_N));
    connect(m_newFileAct, &QAction::triggered, this, [this] { createNewItem(false); });

    m_renameAct = new QAction(tr("Rename…"), this);
    m_renameAct->setShortcut(QKeySequence(Qt::Key_F2));
    connect(m_renameAct, &QAction::triggered, this, &MainWindow::renameSelection);

    m_selectAllAct = new QAction(tr("Select All"), this);
    m_selectAllAct->setShortcut(QKeySequence::SelectAll);
    connect(m_selectAllAct, &QAction::triggered, this, [this] {
        if (!m_analyzing)
            m_view->selectAll();
    });

    m_selectMatchingAct = new QAction(tr("Select Items Matching…"), this);
    m_selectMatchingAct->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_S));
    connect(m_selectMatchingAct, &QAction::triggered, this, &MainWindow::selectMatching);

    m_invertSelectionAct = new QAction(tr("Invert Selection"), this);
    m_invertSelectionAct->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_I));
    connect(m_invertSelectionAct, &QAction::triggered, this, &MainWindow::invertSelection);

    m_newDocumentMenu = makeMenu();
    m_newDocumentMenu->setTitle(tr("New Document"));
    connect(m_newDocumentMenu, &QMenu::aboutToShow, this, &MainWindow::fillNewDocumentMenu);

    m_closeAct = new QAction(tr("Close Tab"), this);
    m_closeAct->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_W)); // the last tab closes the window
    connect(m_closeAct, &QAction::triggered, this, [this] { closeTab(m_activeTab); });

    m_closeWindowAct = new QAction(tr("Close Window"), this);
    m_closeWindowAct->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_W));
    connect(m_closeWindowAct, &QAction::triggered, this, &QWidget::close);

    m_newTabAct = new QAction(tr("New Tab"), this);
    m_newTabAct->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_T));
    connect(m_newTabAct, &QAction::triggered, this, [this] { openTab(m_current, true); });

    m_reopenTabAct = new QAction(tr("Reopen Closed Tab"), this);
    m_reopenTabAct->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_T));
    connect(m_reopenTabAct, &QAction::triggered, this, &MainWindow::reopenClosedTab);

    m_openLocationAct = new QAction(tr("Open Item Location"), this);
    connect(m_openLocationAct, &QAction::triggered, this, [this] {
        const QStringList paths = selectedPaths();
        if (paths.isEmpty())
            return;
        const QString item = paths.first();
        if (navigateTo(QFileInfo(item).absolutePath()))
            selectPaths({item});
    });

    m_openInNewTabAct = new QAction(tr("Open in New Tab"), this);
    connect(m_openInNewTabAct, &QAction::triggered, this, [this] {
        for (const QModelIndex &index : selectedViewIndexes()) {
            const QModelIndex source = toSource(index);
            if (m_model->isDir(source))
                openTab(m_model->filePath(source), false);
        }
    });

    auto addTabSwitch = [this](const QList<QKeySequence> &keys, int step) {
        auto *act = new QAction(this);
        act->setShortcuts(keys);
        connect(act, &QAction::triggered, this, [this, step] {
            if (m_tabs.size() > 1)
                m_tabBar->setCurrentIndex((m_activeTab + step + int(m_tabs.size())) % int(m_tabs.size()));
        });
        addAction(act);
    };
    addTabSwitch({QKeySequence(Qt::CTRL | Qt::Key_Tab), QKeySequence(Qt::CTRL | Qt::Key_PageDown)}, 1);
    addTabSwitch({QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Backtab),
                  QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Tab),
                  QKeySequence(Qt::CTRL | Qt::Key_PageUp)},
                 -1);
    for (int n = 1; n <= 9; ++n) {
        auto *act = new QAction(this);
        act->setShortcut(QKeySequence(Qt::ALT | Qt::Key(Qt::Key_0 + n)));
        connect(act, &QAction::triggered, this, [this, n] {
            if (n - 1 < m_tabs.size())
                m_tabBar->setCurrentIndex(n - 1);
        });
        addAction(act);
    }

    m_quitAct = new QAction(tr("Quit"), this);
    m_quitAct->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Q));
    m_quitAct->setMenuRole(QAction::QuitRole);
    connect(m_quitAct, &QAction::triggered, this, &QApplication::closeAllWindows);

    m_copyLocationAct = new QAction(tr("Copy Location"), this);
    connect(m_copyLocationAct, &QAction::triggered, this, [this] {
        QApplication::clipboard()->setText(QDir::toNativeSeparators(m_current));
        showMessage(tr("Location copied"));
    });

    m_prefsAct = new QAction(tr("Preferences"), this);
    m_prefsAct->setMenuRole(QAction::PreferencesRole);
    m_prefsAct->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Comma));
    connect(m_prefsAct, &QAction::triggered, this, [this] {
        static QPointer<PreferencesDialog> dialog;
        if (!dialog) {
            dialog = new PreferencesDialog(this);
            dialog->setAttribute(Qt::WA_DeleteOnClose);
        }
        dialog->show();
        dialog->raise();
        dialog->activateWindow();
    });

    m_openAct = new QAction(tr("Open"), this);
    connect(m_openAct, &QAction::triggered, this, &MainWindow::openSelection);

    m_cutAct = new QAction(tr("Cut"), this);
    m_cutAct->setShortcut(QKeySequence::Cut);
    connect(m_cutAct, &QAction::triggered, this, [this] { copySelection(true); });

    m_copyAct = new QAction(tr("Copy"), this);
    m_copyAct->setShortcut(QKeySequence::Copy);
    connect(m_copyAct, &QAction::triggered, this, [this] { copySelection(false); });

    m_pasteAct = new QAction(tr("Paste"), this);
    m_pasteAct->setShortcut(QKeySequence::Paste);
    connect(m_pasteAct, &QAction::triggered, this, &MainWindow::paste);

    m_trashAct = new QAction(tr("Move to Trash"), this);
    m_trashAct->setShortcut(QKeySequence::Delete);
    connect(m_trashAct, &QAction::triggered, this, &MainWindow::trashSelection);

    m_propertiesAct = new QAction(tr("Properties"), this);
    m_propertiesAct->setShortcuts({QKeySequence(Qt::CTRL | Qt::Key_I),
                                   QKeySequence(Qt::ALT | Qt::Key_Return)});
    connect(m_propertiesAct, &QAction::triggered, this, [this] {
        QStringList paths = selectedPaths();
        if (paths.isEmpty())
            paths << m_current; // nothing selected: the open folder
        (new PropertiesDialog(paths, this))->show();
    });

    m_compressAct = new QAction(tr("Compress…"), this);
    connect(m_compressAct, &QAction::triggered, this, &MainWindow::compressSelection);
    m_extractHereAct = new QAction(tr("Extract Here"), this);
    connect(m_extractHereAct, &QAction::triggered, this, [this] {
        const QStringList archives = selectedArchives();
        if (!archives.isEmpty())
            m_ops->extract(archives, m_current);
    });
    m_extractToAct = new QAction(tr("Extract to…"), this);
    connect(m_extractToAct, &QAction::triggered, this, [this] {
        const QStringList archives = selectedArchives();
        if (archives.isEmpty())
            return;
        const QString dir = QFileDialog::getExistingDirectory(this, tr("Extract to"), m_current);
        if (!dir.isEmpty())
            m_ops->extract(archives, QDir::fromNativeSeparators(dir));
    });

    m_bookmarkAct = new QAction(tr("Bookmark This Folder"), this);
    m_bookmarkAct->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_D));
    connect(m_bookmarkAct, &QAction::triggered, this, &MainWindow::toggleBookmark);

    m_deleteAct = new QAction(tr("Delete Permanently…"), this);
    m_deleteAct->setShortcut(QKeySequence(Qt::SHIFT | Qt::Key_Delete));
    connect(m_deleteAct, &QAction::triggered, this, &MainWindow::deleteSelection);

    m_restoreAct = new QAction(tr("Restore From Trash"), this);
    connect(m_restoreAct, &QAction::triggered, this, &MainWindow::restoreSelection);

    m_emptyTrashAct = new QAction(tr("Empty Trash…"), this);
    connect(m_emptyTrashAct, &QAction::triggered, this, &MainWindow::emptyTrash);

    m_restoreAllAct = new QAction(tr("Restore All"), this);
    connect(m_restoreAllAct, &QAction::triggered, this, [this] {
        if (!m_model->isTrash())
            return;
        QStringList all;
        for (int row = 0; row < m_model->rowCount(); ++row)
            all << m_model->filePath(m_model->index(row, 0));
        if (!all.isEmpty())
            m_ops->restore(all); // back where they came from; never overwrites
    });

    m_undoAct = new QAction(tr("Undo"), this);
    m_undoAct->setShortcut(QKeySequence::Undo);
    connect(m_undoAct, &QAction::triggered, m_ops, &FileOperations::undo);
    m_previewAct = new QAction(tr("Preview"), this);
    // Shown as "Space" in menus; the key itself is handled in eventFilter, so
    // typing a name with spaces still works and line edits keep their spaces.
    m_previewAct->setShortcut(QKeySequence(Qt::Key_Space));
    m_previewAct->setShortcutContext(Qt::WidgetShortcut);
    connect(m_previewAct, &QAction::triggered, this, &MainWindow::toggleQuickLook);

    m_splitAct = new QAction(tr("Split View"), this);
    m_splitAct->setShortcut(QKeySequence(Qt::Key_F3));
    m_splitAct->setCheckable(true);
    connect(m_splitAct, &QAction::toggled, this, &MainWindow::setSplitView);
    m_copyToOtherAct = new QAction(tr("Copy to Other Pane"), this);
    m_copyToOtherAct->setShortcut(QKeySequence(Qt::Key_F5));
    connect(m_copyToOtherAct, &QAction::triggered, this, [this] { transferToOtherPane(false); });
    m_moveToOtherAct = new QAction(tr("Move to Other Pane"), this);
    m_moveToOtherAct->setShortcut(QKeySequence(Qt::Key_F6));
    connect(m_moveToOtherAct, &QAction::triggered, this, [this] { transferToOtherPane(true); });

    m_starAct = new QAction(tr("Star"), this);
    connect(m_starAct, &QAction::triggered, this, [this] {
        const QStringList paths = selectedPaths();
        if (paths.isEmpty())
            return;
        // Unstar only if everything selected is starred already.
        bool all = true;
        for (const QString &p : paths)
            all = all && Starred::instance().isStarred(p);
        Starred::instance().setStarred(paths, !all);
        showMessage(all ? (paths.size() == 1 ? tr("Unstarred") : tr("%1 items unstarred").arg(paths.size()))
                        : (paths.size() == 1 ? tr("Starred") : tr("%1 items starred").arg(paths.size())));
    });
    m_removeRecentAct = new QAction(tr("Remove from Recent"), this);
    connect(m_removeRecentAct, &QAction::triggered, this, [this] {
        const QStringList paths = selectedPaths();
        if (!paths.isEmpty() && RecentFiles::remove(paths))
            m_model->refresh(); // also picked up by the watcher; this is just sooner
    });

    m_redoAct = new QAction(tr("Redo"), this);
    m_redoAct->setShortcuts({QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Z), QKeySequence(Qt::CTRL | Qt::Key_Y)});
    connect(m_redoAct, &QAction::triggered, m_ops, &FileOperations::redo);

    m_zoomInAct = new QAction(tr("Zoom In"), this);
    m_zoomInAct->setShortcuts({QKeySequence::ZoomIn, QKeySequence(Qt::CTRL | Qt::Key_Equal)});
    connect(m_zoomInAct, &QAction::triggered, this, [this] { zoom(+1); });
    m_zoomOutAct = new QAction(tr("Zoom Out"), this);
    m_zoomOutAct->setShortcuts({QKeySequence::ZoomOut, QKeySequence(Qt::CTRL | Qt::Key_Underscore)});
    connect(m_zoomOutAct, &QAction::triggered, this, [this] { zoom(-1); });
    m_zoomResetAct = new QAction(tr("Normal Size"), this);
    m_zoomResetAct->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_0));
    connect(m_zoomResetAct, &QAction::triggered, this, [this] { zoom(0); });

    m_aboutAct = new QAction(tr("About Argonaut"), this);
    m_aboutAct->setMenuRole(QAction::AboutRole);
    connect(m_aboutAct, &QAction::triggered, this, [this] {
        static QPointer<AboutDialog> dialog;
        if (!dialog)
            dialog = new AboutDialog(this);
        dialog->show();
        dialog->raise();
        dialog->activateWindow();
    });

    // Search also inside files (the "Inside files" button while searching).
    m_searchContentsAct = new QAction(tr("Search Inside Files"), this);
    m_searchContentsAct->setCheckable(true);
    m_searchContentsAct->setToolTip(tr("Also find files by the words inside them: text files, and documents "
                                       "in the folders GNOME's search indexes"));
    m_searchContentsAct->setChecked(QSettings().value("search/contents", false).toBool());
    connect(m_searchContentsAct, &QAction::toggled, this, [this](bool on) {
        QSettings().setValue("search/contents", on);
        for (int i = 0; i < m_paneCount; ++i) {
            DirectoryModel *model = i == m_activePane ? m_model : m_panes[i].model;
            if (model)
                model->setSearchContents(on);
        }
        updateFloatingBar();
    });
    m_connectAct = new QAction(tr("Connect to Server…"), this);
    connect(m_connectAct, &QAction::triggered, this, [this] {
        ConnectDialog dialog(this);
        if (dialog.exec() == QDialog::Accepted)
            connectToServer(dialog.address());
    });

    m_organizeAct = new QAction(tr("Organize with Claude…"), this);
    connect(m_organizeAct, &QAction::triggered, this, &MainWindow::organizeWithClaude);

    m_paletteAct = new QAction(tr("Command Palette…"), this);
    m_paletteAct->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_P));
    connect(m_paletteAct, &QAction::triggered, this, &MainWindow::showCommandPalette);

    m_guideAct = new QAction(tr("User Guide"), this);
    m_guideAct->setShortcut(QKeySequence::HelpContents); // F1
    connect(m_guideAct, &QAction::triggered, this, [this] { showGuide(QStringLiteral("start")); });

    m_shortcutsAct = new QAction(tr("Keyboard Shortcuts"), this);
    m_shortcutsAct->setShortcuts({QKeySequence(Qt::CTRL | Qt::Key_Question),
                                  QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Slash)});
    connect(m_shortcutsAct, &QAction::triggered, this, [this] { showGuide(QStringLiteral("shortcuts")); });

    addActions({m_backAct, m_forwardAct, m_upAct, m_homeAct, m_locationAct, m_hiddenAct,
                m_searchAct, m_gridAct, m_listAct, m_analyzeAct, m_analyzeFilesAct, m_sidebarAct, m_prefsAct, m_cutAct, m_copyAct,
                m_pasteAct, m_trashAct, m_undoAct, m_redoAct, m_zoomInAct, m_zoomOutAct,
                m_zoomResetAct, m_newFolderAct, m_newFileAct, m_renameAct,
                m_selectAllAct, m_selectMatchingAct, m_invertSelectionAct, m_closeAct, m_quitAct, m_deleteAct, m_bookmarkAct,
                m_newTabAct, m_closeWindowAct, m_reopenTabAct, m_propertiesAct, m_compressAct,
                m_extractHereAct, m_extractToAct, m_openLocationAct, m_starAct, m_removeRecentAct,
                m_splitAct, m_copyToOtherAct, m_moveToOtherAct, m_guideAct, m_shortcutsAct, m_paletteAct});
    if (m_terminalAct)
        addAction(m_terminalAct);
}

void MainWindow::addWindowButtons(QHBoxLayout *row)
{
    m_minButton = makeButton(tr("Minimize"), "win");
    connect(m_minButton, &QToolButton::clicked, this, &QWidget::showMinimized);
    m_maxButton = makeButton(tr("Maximize"), "win");
    connect(m_maxButton, &QToolButton::clicked, this,
            [this] { isMaximized() ? showNormal() : showMaximized(); });
    m_closeButton = makeButton(tr("Close"), "win");
    connect(m_closeButton, &QToolButton::clicked, this, &QWidget::close);
    row->addSpacing(10);
    for (QToolButton *b : {m_minButton, m_maxButton, m_closeButton}) {
        row->addWidget(b, 0, Qt::AlignVCenter);
        if (b != m_closeButton)
            row->addSpacing(6);
    }
}

QToolButton *MainWindow::makeButton(const QString &toolTip, const char *objectName)
{
    auto *button = new QToolButton(this);
    button->setObjectName(objectName);
    button->setToolTip(toolTip);
    button->setIconSize(QSize(16, 16));
    button->setFocusPolicy(Qt::NoFocus);
    if (qstrcmp(objectName, "win") == 0)
        button->setFixedSize(24, 24);
    else
        button->setFixedSize(32, 32);
    return button;
}

// Rounded popup menu (needs a translucent, frameless popup window).
QMenu *MainWindow::makeMenu()
{
    auto *menu = new QMenu(this);
    menu->setWindowFlags(menu->windowFlags() | Qt::FramelessWindowHint
                         | Qt::NoDropShadowWindowHint);
    menu->setAttribute(Qt::WA_TranslucentBackground);
    return menu;
}

void MainWindow::setupUi()
{

    m_thumbnailer = new Thumbnailer(this);

    m_root = new QWidget(this);
    m_root->setObjectName("root");
    m_root->setAttribute(Qt::WA_StyledBackground);
    auto *rootLayout = new QVBoxLayout(m_root);
    rootLayout->setSpacing(0);

#if defined(Q_OS_MACOS)
    setMenuBar(qobject_cast<QMenuBar *>(buildMenuBar())); // global macOS menu bar
#else
    // The menu bar row also holds the window buttons, at its right end.
    auto *menuRow = new QWidget(m_root);
    menuRow->setObjectName("menuRow");
    menuRow->setAttribute(Qt::WA_StyledBackground);
    auto *menuLayout = new QHBoxLayout(menuRow);
    menuLayout->setContentsMargins(0, 0, 8, 0);
    menuLayout->setSpacing(0);
    menuLayout->addWidget(buildMenuBar(), 1); // its empty space drags the window
    if (m_csd)
        addWindowButtons(menuLayout);
    // The row drags too: the menu bar can be lower than the row (and is only
    // a thin band when its menus are hidden, as in an Open/Save window).
    m_menuRow = menuRow;
    menuRow->installEventFilter(this);
    rootLayout->addWidget(menuRow, 0);
#endif

    m_splitter = new QSplitter(m_root);
    m_splitter->setObjectName("mainSplitter");
    m_splitter->addWidget(buildSidebarPane());
    m_splitter->addWidget(buildContentPane());
    m_splitter->setChildrenCollapsible(false);
    m_splitter->setHandleWidth(1);
    m_splitter->setStretchFactor(1, 1);
    m_splitter->setSizes({230, 920});
    rootLayout->addWidget(m_splitter, 1);

    setCentralWidget(m_root);
}

QWidget *MainWindow::buildSidebarPane()
{
    auto *pane = new QWidget(this);
    m_sidebarPane = pane;
    pane->setObjectName("sidebarPane");
    pane->setAttribute(Qt::WA_StyledBackground);
    pane->setMinimumWidth(170);
    auto *layout = new QVBoxLayout(pane);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto *header = new HeaderBar(pane);
    m_searchButton = makeButton(tr("Search (Ctrl+F)"));
    m_searchButton->setCheckable(true);
    connect(m_searchButton, &QToolButton::toggled, m_searchAct, &QAction::setChecked);
    connect(m_searchAct, &QAction::toggled, m_searchButton, &QToolButton::setChecked);

    m_titleLabel = new QLabel(tr("Argonaut"), header);
    m_titleLabel->setObjectName("title");
    m_titleLabel->setAlignment(Qt::AlignCenter);

    m_menuButton = makeButton(tr("Main menu"));
    m_menuButton->setPopupMode(QToolButton::InstantPopup);
    QMenu *mainMenu = makeMenu();
    mainMenu->addAction(m_undoAct);
    mainMenu->addAction(m_redoAct);
    mainMenu->addSeparator();
    mainMenu->addAction(m_hiddenAct);
    mainMenu->addSeparator();
    mainMenu->addAction(m_prefsAct);
    mainMenu->addAction(m_guideAct);
    mainMenu->addAction(m_aboutAct);
    m_menuButton->setMenu(mainMenu);

    header->row()->addWidget(m_searchButton);
    header->row()->addWidget(m_titleLabel, 1);
    header->row()->addWidget(m_menuButton);
    layout->addWidget(header);

    m_sidebar = new QListWidget(pane);
    m_sidebar->setObjectName("sidebar");
    m_sidebar->setItemDelegate(new SidebarDelegate(m_sidebar));
    m_sidebar->setIconSize(QSize(16, 16));
    m_sidebar->setFrameShape(QFrame::NoFrame);
    m_sidebar->setFocusPolicy(Qt::NoFocus);
    m_sidebar->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_sidebar->setTextElideMode(Qt::ElideRight);
    m_sidebar->viewport()->setAcceptDrops(true); // drop onto places and drives
    m_sidebar->viewport()->installEventFilter(this);
    m_sidebar->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_sidebar, &QWidget::customContextMenuRequested, this, &MainWindow::showSidebarMenu);
    connect(m_sidebar, &QListWidget::itemClicked, this, [this](QListWidgetItem *item) {
        const QString path = item->data(PathRole).toString();
        if (path.isEmpty() || !navigateTo(path))
            syncSidebarSelection();
    });
    layout->addWidget(m_sidebar, 1);
    return pane;
}

QWidget *MainWindow::buildContentPane()
{
    auto *pane = new QWidget(this);
    pane->setObjectName("contentPane");
    pane->setAttribute(Qt::WA_StyledBackground);
    auto *layout = new QVBoxLayout(pane);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    m_contentLayout = layout; // an Open/Save window adds its bar at the bottom

    auto *header = new HeaderBar(pane);
    QHBoxLayout *row = header->row();

    m_backButton = makeButton(tr("Back (Alt+Left)"));
    connect(m_backButton, &QToolButton::clicked, m_backAct, &QAction::trigger);
    m_forwardButton = makeButton(tr("Forward (Alt+Right)"));
    connect(m_forwardButton, &QToolButton::clicked, m_forwardAct, &QAction::trigger);
    row->addWidget(m_backButton);
    row->addWidget(m_forwardButton);
    row->addSpacing(4);

    m_pathBar = new PathBar(header);
    connect(m_pathBar, &PathBar::pathActivated, this, [this](const QString &p) { navigateTo(p); });
    QMenu *folderMenu = makeMenu();
    folderMenu->addAction(m_pasteAct);
    folderMenu->addSeparator();
    folderMenu->addAction(m_bookmarkAct);
    folderMenu->addAction(m_copyLocationAct);
    folderMenu->addSeparator();
    folderMenu->addAction(m_hiddenAct);
    m_pathBar->menuButton()->setMenu(folderMenu);

    m_searchEdit = new QLineEdit(header);
    m_searchEdit->setObjectName("searchEdit");
    m_searchEdit->setFixedHeight(32);
    m_searchEdit->setPlaceholderText(tr("Search this folder and its sub-folders"));
    m_searchEdit->installEventFilter(this);
    connect(m_searchEdit, &QLineEdit::textChanged, this, &MainWindow::updateFilter);
    m_searchTimer = new QTimer(this);
    m_searchTimer->setSingleShot(true);
    m_searchTimer->setInterval(150);
    connect(m_searchTimer, &QTimer::timeout, this, [this] {
        m_model->setSearch(m_searchEdit->text().simplified());
        updateFloatingBar();
    });

    // Search also inside files: next to the field while searching.
    auto *searchBox = new QWidget(header);
    auto *searchRow = new QHBoxLayout(searchBox);
    searchRow->setContentsMargins(0, 0, 0, 0);
    searchRow->setSpacing(4);
    searchRow->addWidget(m_searchEdit, 1);
    auto *contentsButton = new QToolButton(searchBox);
    contentsButton->setObjectName("searchOption");
    m_searchContentsAct->setIconText(tr("Inside files")); // the button's label; menus show the full text
    contentsButton->setDefaultAction(m_searchContentsAct);
    contentsButton->setToolButtonStyle(Qt::ToolButtonTextOnly);
    contentsButton->setFixedHeight(32);
    contentsButton->setCursor(Qt::PointingHandCursor);
    searchRow->addWidget(contentsButton);

    // Filters: by type and by date; they also search on their own.
    m_filterButton = new QToolButton(searchBox);
    m_filterButton->setObjectName("searchOption");
    m_filterButton->setFixedHeight(32);
    m_filterButton->setPopupMode(QToolButton::InstantPopup);
    m_filterButton->setCursor(Qt::PointingHandCursor);
    QMenu *filters = makeMenu();
    filters->addSection(tr("Type"));
    m_typeFilters = new QActionGroup(this);
    const QList<std::pair<DirectoryModel::TypeFilter, QString>> types = {
        {DirectoryModel::TypeFilter::Any, tr("Any type")},       {DirectoryModel::TypeFilter::Folders, tr("Folders")},
        {DirectoryModel::TypeFilter::Documents, tr("Documents")}, {DirectoryModel::TypeFilter::Images, tr("Images")},
        {DirectoryModel::TypeFilter::Audio, tr("Audio")},         {DirectoryModel::TypeFilter::Video, tr("Video")},
        {DirectoryModel::TypeFilter::Archives, tr("Archives")},   {DirectoryModel::TypeFilter::Text, tr("Text and code")}};
    for (const auto &[type, label] : types) {
        QAction *a = filters->addAction(label);
        a->setCheckable(true);
        a->setData(int(type));
        a->setChecked(type == DirectoryModel::TypeFilter::Any);
        m_typeFilters->addAction(a);
    }
    filters->addSection(tr("Changed"));
    m_dateFilters = new QActionGroup(this);
    const QList<std::pair<DirectoryModel::DateFilter, QString>> dates = {
        {DirectoryModel::DateFilter::Any, tr("Any time")},        {DirectoryModel::DateFilter::Today, tr("Today")},
        {DirectoryModel::DateFilter::Week, tr("In the last 7 days")}, {DirectoryModel::DateFilter::Month, tr("In the last 30 days")},
        {DirectoryModel::DateFilter::Year, tr("This year")},      {DirectoryModel::DateFilter::OlderThanYear, tr("Over a year ago")}};
    for (const auto &[date, label] : dates) {
        QAction *a = filters->addAction(label);
        a->setCheckable(true);
        a->setData(int(date));
        a->setChecked(date == DirectoryModel::DateFilter::Any);
        m_dateFilters->addAction(a);
    }
    connect(m_typeFilters, &QActionGroup::triggered, this, &MainWindow::applySearchFilters);
    connect(m_dateFilters, &QActionGroup::triggered, this, &MainWindow::applySearchFilters);
    m_filterButton->setMenu(filters);
    m_filterButton->setText(tr("Filters"));
    searchRow->addWidget(m_filterButton);

    m_locationStack = new QStackedWidget(header);
    m_locationStack->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    m_locationStack->setFixedHeight(32);
    m_locationStack->addWidget(m_pathBar);
    m_locationStack->addWidget(searchBox);
    row->addWidget(m_locationStack, 1);
    row->addSpacing(4);

    if (m_terminalAct) {
        m_terminalButton = makeButton(tr("Terminal (Ctrl+`)"));
        m_terminalButton->setCheckable(true);
        connect(m_terminalButton, &QToolButton::toggled, m_terminalAct, &QAction::setChecked);
        connect(m_terminalAct, &QAction::toggled, m_terminalButton, &QToolButton::setChecked);
        row->addWidget(m_terminalButton);
    }
    m_analyzeButton = makeButton(tr("Analyze disk usage (Ctrl+3)"));
    m_analyzeButton->setCheckable(true);
    connect(m_analyzeButton, &QToolButton::clicked, this, [this](bool on) {
        on ? setAnalyzeMode(true) : setGridMode(m_grid);
    });
    row->addWidget(m_analyzeButton);
    m_splitButton = makeButton(tr("Split view (F3)"));
    m_splitButton->setCheckable(true);
    connect(m_splitButton, &QToolButton::clicked, m_splitAct, &QAction::setChecked);
    row->addWidget(m_splitButton);
    m_viewButton = makeButton(QString());
    connect(m_viewButton, &QToolButton::clicked, this, [this] {
        setGridMode(m_analyzing ? m_grid : !m_grid); // from Analyze: back to the file view
    });
    row->addWidget(m_viewButton);

    layout->addWidget(header);

    // The first file pane (a second one comes with the split view).
    m_paneSplitter = new QSplitter(Qt::Horizontal, pane);
    m_paneSplitter->setObjectName("paneSplitter");
    m_paneSplitter->setChildrenCollapsible(false);
    m_paneSplitter->setHandleWidth(1);
    m_paneSplitter->addWidget(buildPane(m_paneSplitter, 0));
    m_springTimer = new QTimer(this);
    m_springTimer->setSingleShot(true);
    m_springTimer->setInterval(800);
    connect(m_springTimer, &QTimer::timeout, this, [this] {
        const QString folder = m_springPath;
        m_springPath.clear();
        if (folder.isEmpty())
            return;
        springOver(folder, m_springPane);
    });
    m_dragScrollTimer = new QTimer(this);
    m_dragScrollTimer->setInterval(40);
    connect(m_dragScrollTimer, &QTimer::timeout, this, [this] {
        if (m_dragScrollStep == 0) {
            m_dragScrollTimer->stop();
            return;
        }
        if (m_dragScrollView) { // the pane being dragged over (maybe not the active one)
            QScrollBar *bar = m_dragScrollView->verticalScrollBar();
            bar->setValue(bar->value() + m_dragScrollStep);
        }
    });


    // Analyze: its own model and selection; never m_view.
    m_usageModel = new DiskUsageModel(this);
    m_analyzeView = new QTreeView(m_viewStack);
    m_analyzeView->setObjectName("analyzeView");
    m_analyzeView->setModel(m_usageModel);
    m_analyzeView->setItemDelegate(new UsageDelegate(m_analyzeView));
    m_analyzeView->setUniformRowHeights(true);
    m_analyzeView->setAllColumnsShowFocus(true);
    m_analyzeView->setSelectionMode(QAbstractItemView::SingleSelection);
    m_analyzeView->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_analyzeView->setContextMenuPolicy(Qt::CustomContextMenu);
    m_analyzeView->setFrameShape(QFrame::NoFrame);
    m_analyzeView->setSortingEnabled(true);
    m_analyzeView->sortByColumn(DiskUsageModel::SizeColumn, Qt::DescendingOrder);
    QHeaderView *usageColumns = m_analyzeView->header();
    usageColumns->setStretchLastSection(false);
    usageColumns->setHighlightSections(false);
    usageColumns->setSectionsMovable(false);
    usageColumns->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    usageColumns->setSectionResizeMode(DiskUsageModel::NameColumn, QHeaderView::Stretch);
    usageColumns->resizeSection(DiskUsageModel::UsageColumn, 200);
    usageColumns->resizeSection(DiskUsageModel::SizeColumn, 110);
    usageColumns->resizeSection(DiskUsageModel::ItemsColumn, 130);
    connect(m_analyzeView, &QTreeView::customContextMenuRequested, this, &MainWindow::showAnalyzeMenu);
    connect(m_analyzeView->selectionModel(), &QItemSelectionModel::currentChanged, this,
            &MainWindow::updateFloatingBar);
    connect(m_usageModel, &DiskUsageModel::progress, this, &MainWindow::updateFloatingBar);
    connect(m_usageModel, &DiskUsageModel::finished, this, &MainWindow::updateFloatingBar);
    // The analysed folder is the top row: keep it open.
    connect(m_usageModel, &QAbstractItemModel::modelReset, this, [this] {
        m_analyzeView->expand(m_usageModel->index(0, 0));
    });
    connect(m_usageModel, &QAbstractItemModel::rowsInserted, this, [this](const QModelIndex &parent) {
        if (!parent.parent().isValid())
            m_analyzeView->expand(m_usageModel->index(0, 0));
    });
    // Enter / double-click: a folder opens in the Analyze view, a file opens
    // in its app (with the usual real-type checks).
    connect(m_analyzeView, &QTreeView::activated, this, [this](const QModelIndex &index) {
        const QString path = m_usageModel->filePath(index);
        const int kind = index.data(DiskUsageModel::KindRole).toInt();
        if (path.isEmpty())
            return;
        if (kind == DiskUsageModel::Node::File)
            openFile(path);
        else if (path != m_current)
            navigateTo(path);
    });
    auto *analyzeTrash = new QAction(tr("Move to Trash"), m_analyzeView);
    analyzeTrash->setShortcut(QKeySequence::Delete);
    analyzeTrash->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    connect(analyzeTrash, &QAction::triggered, this, [this] {
        const QString path = selectedAnalyzePath();
        if (!path.isEmpty() && path != m_current)
            m_ops->trash({path});
    });
    m_analyzeView->addAction(analyzeTrash);

    m_viewStack->addWidget(m_analyzeView); // in the first pane only

    // Shown only in the Trash: what it is, plus Restore / Empty Trash.
    m_trashBar = new QWidget(pane);
    m_trashBar->setObjectName("trashBar");
    m_trashBar->setAttribute(Qt::WA_StyledBackground);
    auto *trashRow = new QHBoxLayout(m_trashBar);
    trashRow->setContentsMargins(16, 6, 10, 6);
    auto *trashText = new QLabel(tr("Items in the Trash are deleted forever when you empty it."),
                                 m_trashBar);
    trashText->setObjectName("trashBarText");
    m_trashRestoreButton = new QToolButton(m_trashBar);
    m_trashRestoreButton->setObjectName("barButton");
    m_trashRestoreButton->setDefaultAction(m_restoreAct);
    m_trashRestoreButton->setText(tr("Restore"));
    m_trashEmptyButton = new QToolButton(m_trashBar);
    m_trashEmptyButton->setObjectName("barButton");
    m_trashEmptyButton->setDefaultAction(m_emptyTrashAct);
    m_trashRestoreAllButton = new QToolButton(m_trashBar);
    m_trashRestoreAllButton->setObjectName("barButton");
    m_trashRestoreAllButton->setDefaultAction(m_restoreAllAct);
    for (QToolButton *b : {m_trashRestoreButton, m_trashRestoreAllButton, m_trashEmptyButton})
        b->setFocusPolicy(Qt::NoFocus);
    trashRow->addWidget(trashText, 1);
    trashRow->addWidget(m_trashRestoreButton);
    trashRow->addWidget(m_trashRestoreAllButton);
    trashRow->addWidget(m_trashEmptyButton);
    m_trashBar->hide();
    m_tabBar = new QTabBar(pane);
    m_tabBar->setObjectName("tabs");
    m_tabBar->setExpanding(true);
    m_tabBar->setMovable(true);
    m_tabBar->setDocumentMode(true);
    m_tabBar->setElideMode(Qt::ElideMiddle);
    m_tabBar->setFocusPolicy(Qt::NoFocus);
    m_tabBar->setAcceptDrops(true); // drop files onto a tab's folder
    m_tabBar->installEventFilter(this);
    m_tabs.append(TabState());
    m_tabBar->addTab(QString());
    addTabCloseButton(0);
    m_tabBar->hide(); // shown from two tabs on
    connect(m_tabBar, &QTabBar::currentChanged, this, &MainWindow::switchToTab);
    connect(m_tabBar, &QTabBar::tabMoved, this, [this](int from, int to) {
        m_tabs.move(from, to);
        if (m_activeTab == from)
            m_activeTab = to;
        else if (from < m_activeTab && to >= m_activeTab)
            --m_activeTab;
        else if (from > m_activeTab && to <= m_activeTab)
            ++m_activeTab;
    });
    layout->addWidget(m_tabBar);
    layout->addWidget(m_trashBar);

    m_contentSplitter = new QSplitter(Qt::Vertical, pane);
    m_contentSplitter->setObjectName("contentSplitter");
    m_contentSplitter->setChildrenCollapsible(false);
    m_contentSplitter->setHandleWidth(1);
    m_contentSplitter->addWidget(m_paneSplitter);
#if defined(ARGONAUT_TERMINAL)
    m_contentSplitter->addWidget(buildTerminalPanel());
#endif
    m_contentSplitter->setStretchFactor(0, 1);
    layout->addWidget(m_contentSplitter, 1);


    m_opsPanel = new OperationsPanel(m_ops, m_viewStack);
    m_toast = new Toast(m_viewStack);
    connect(m_opsPanel, &OperationsPanel::layoutChanged, this, [this] {
        m_toast->setAvoidRect(m_opsPanel->isVisible() ? m_opsPanel->geometry() : QRect());
    });
    updateFilter();
    updateEditActions();

    setGridMode(true);
    savePane(0); // m_panes[0] knows its widgets from now on
    return pane;
}


// ---------------------------------------------------------------- panes ---

// While alive, the window's pane members belong to pane `index` (if it isn't
// the active one): for handling what happened in the other pane (its folder
// loaded, a drop into it…) with the same code. Header, title and actions are
// left alone meanwhile (m_background).
class MainWindow::PaneScope
{
public:
    PaneScope(MainWindow *window, int index)
        : m_window(window), m_previous(window->m_activePane),
          m_switched(index != window->m_activePane && index >= 0 && index < window->m_paneCount)
    {
        if (!m_switched)
            return;
        m_window->savePane(m_previous);
        m_window->loadPane(index);
        m_window->m_activePane = index;
        ++m_window->m_background;
    }
    ~PaneScope()
    {
        if (!m_switched)
            return;
        m_window->savePane(m_window->m_activePane);
        m_window->loadPane(m_previous);
        m_window->m_activePane = m_previous;
        --m_window->m_background;
    }
    PaneScope(const PaneScope &) = delete;
    PaneScope &operator=(const PaneScope &) = delete;

private:
    MainWindow *m_window;
    int m_previous;
    bool m_switched;
};

void MainWindow::savePane(int index)
{
    Pane &p = m_panes[index];
    p.model = m_model;
    p.proxy = m_proxy;
    p.gridDelegate = m_gridDelegate;
    p.listDelegate = m_listDelegate;
    p.iconView = m_iconView;
    p.detailView = m_detailView;
    p.viewStack = m_viewStack;
    p.floatingBar = m_floatingBar;
    p.messageTimer = m_messageTimer;
    p.view = m_view;
    p.message = m_message;
    p.current = m_current;
    p.back = m_backHistory;
    p.forward = m_forwardHistory;
    p.grid = m_grid;
    p.gridZoom = m_gridZoom;
    p.listZoom = m_listZoom;
    p.pendingSelection = m_pendingSelection;
    p.pendingSince = m_pendingSince;
    p.pendingScroll = m_pendingScroll;
    p.pendingRename = m_pendingRename;
    p.recentSort = m_recentSort;
    p.sortColumnBefore = m_sortColumnBefore;
    p.sortOrderBefore = m_sortOrderBefore;
}

void MainWindow::loadPane(int index)
{
    const Pane &p = m_panes[index];
    m_model = p.model;
    m_proxy = p.proxy;
    m_gridDelegate = p.gridDelegate;
    m_listDelegate = p.listDelegate;
    m_iconView = p.iconView;
    m_detailView = p.detailView;
    m_viewStack = p.viewStack;
    m_floatingBar = p.floatingBar;
    m_messageTimer = p.messageTimer;
    m_view = p.view;
    m_message = p.message;
    m_current = p.current;
    m_backHistory = p.back;
    m_forwardHistory = p.forward;
    m_grid = p.grid;
    m_gridZoom = p.gridZoom;
    m_listZoom = p.listZoom;
    m_pendingSelection = p.pendingSelection;
    m_pendingSince = p.pendingSince;
    m_pendingScroll = p.pendingScroll;
    m_pendingRename = p.pendingRename;
    m_recentSort = p.recentSort;
    m_sortColumnBefore = p.sortColumnBefore;
    m_sortOrderBefore = p.sortOrderBefore;
}

int MainWindow::paneOf(const QObject *object) const
{
    for (int i = 0; i < m_paneCount; ++i) {
        const Pane &p = m_panes[i];
        if (object == p.iconView || object == p.detailView || object == p.viewStack || object == p.caption
            || (p.iconView && object == p.iconView->viewport())
            || (p.detailView && object == p.detailView->viewport()))
            return i;
    }
    return -1;
}

void MainWindow::activatePane(int index)
{
    if (index == m_activePane || index < 0 || index >= m_paneCount || m_background)
        return;
    cancelRename();
    if (m_quickLook)
        m_quickLook->hide();
    setSearchActive(false); // the search field belongs to the active pane
    if (m_analyzing)
        setGridMode(m_grid); // Analyze is the first pane's
    savePane(m_activePane);
    loadPane(index);
    m_activePane = index;
    refreshChrome();
    if (!m_view->hasFocus())
        m_view->setFocus();
}

QString MainWindow::titleFor(const QString &path) const
{
    switch (DirectoryModel::placeOf(path)) {
    case DirectoryModel::Place::Recent:
        return tr("Recent");
    case DirectoryModel::Place::Starred:
        return tr("Starred");
    case DirectoryModel::Place::Folder:
        break;
    }
    if (Trash::isFilesDir(path))
        return tr("Trash");
    if (const Remote::Mount server = Remote::mountOf(path); server.localPath == path)
        return server.name; // the top of a server, not "sftp:host=…"
    const QString name = QFileInfo(path).fileName();
    return name.isEmpty() ? QDir::toNativeSeparators(path) : name;
}

// The header, title, sidebar and actions show the active pane.
void MainWindow::refreshChrome()
{
    if (m_background)
        return;
    m_trashBar->setVisible(m_model->isTrash());
    m_pathBar->setPath(m_current);
    updateGitBadge();
#if defined(ARGONAUT_DBUS)
    if (m_chooser) {
        const bool folder = m_chooser->directory;
        const QString fallback = m_chooser->mode == ChooserRequest::Mode::Open
                                     ? (folder ? tr("Select Folder") : tr("Open"))
                                     : tr("Save");
        setWindowTitle(m_chooser->title.isEmpty() ? fallback : m_chooser->title);
        m_titleLabel->setText(fallback); // short: apps' titles can be long
    } else
#endif
        setWindowTitle(tr("%1 — Argonaut").arg(titleFor(m_current)));
    syncSidebarSelection();
    updateActions();
    updateEditActions();
    updateBookmarkAction();
    updateSortActions();
    updateViewChrome();
    updatePaneCaptions();
    updateFloatingBar();
}

// Ctrl+Shift+P: every enabled command of the menu bar, labelled with its
// menu, plus the ones that only live in right-click menus.
void MainWindow::showCommandPalette()
{
    if (!m_palette)
        m_palette = new CommandPalette(this);
    updateEditActions();
    bool allStarred = !selectedPaths().isEmpty();
    for (const QString &p : selectedPaths())
        allStarred = allStarred && Starred::instance().isStarred(p);
    m_starAct->setText(allStarred ? tr("Unstar") : tr("Star"));

    QList<CommandPalette::Command> commands;
    QSet<QAction *> seen{m_paletteAct};
    auto clean = [](QString text) {
        text.remove(QLatin1Char('&'));
        text.remove(QChar(0x2026));
        return text.trimmed();
    };
    std::function<void(QMenu *, const QString &)> walk = [&](QMenu *menu, const QString &where) {
        for (QAction *act : menu->actions()) {
            if (act->isSeparator() || !act->isVisible())
                continue;
            if (QMenu *sub = act->menu()) {
                if (sub == m_newDocumentMenu) {
                    fillNewDocumentMenu(); // its items are made when it opens
                    if (!act->isEnabled())
                        continue;
                }
                walk(sub, where + QStringLiteral(" › ") + clean(sub->title()));
                continue;
            }
            if (!act->isEnabled() || act->text().isEmpty() || seen.contains(act))
                continue;
            seen.insert(act);
            commands.append({clean(act->text()), where, act});
        }
    };
    for (QAction *top : m_menuBar->actions()) {
        if (QMenu *menu = top->menu())
            walk(menu, clean(menu->title()));
    }
    const QString selection = tr("Selection");
    for (QAction *act : {m_starAct, m_removeRecentAct, m_openInNewTabAct, m_openLocationAct, m_openTerminalHereAct}) {
        if (!act || seen.contains(act) || !act->isEnabled() || !act->isVisible())
            continue;
        if (act == m_removeRecentAct && m_model->place() != DirectoryModel::Place::Recent)
            continue;
        if (act == m_openLocationAct && !(m_model->isSearch() || m_model->isVirtual()))
            continue;
        if ((act == m_starAct || act == m_openInNewTabAct || act == m_openLocationAct) && selectedPaths().isEmpty())
            continue;
        seen.insert(act);
        commands.append({clean(act->text()), selection, act});
    }
    m_contextFolder = m_current; // "Open in Terminal" acts on the open folder
    m_palette->open(commands);
}

// Help → User Guide / Keyboard Shortcuts. One window; the shortcuts page is
// made from these actions, so it always matches the menus.
void MainWindow::showGuide(const QString &topic)
{
    static QPointer<HelpDialog> dialog;
    if (!dialog) {
        using Group = HelpDialog::ShortcutGroup;
        auto row = [](QAction *act, const QString &name = QString()) {
            QString text = name.isEmpty() ? act->text() : name;
            text.remove(QLatin1Char('&')).remove(QChar(0x2026)); // "Rename…" -> "Rename"
            return HelpDialog::Shortcut{text, act->shortcuts(), QString()};
        };
        auto note = [](const QString &name, const QString &keys) {
            return HelpDialog::Shortcut{name, {}, keys};
        };
        QList<Group> groups;
        groups.append({tr("Moving Around"),
                       {row(m_backAct, tr("Back")), row(m_forwardAct, tr("Forward")), row(m_upAct, tr("Up one folder")),
                        row(m_homeAct, tr("Home folder")), row(m_locationAct, tr("Type a path")),
                        row(m_searchAct, tr("Search this folder and sub-folders")),
                        note(tr("Open the selected item"), QStringLiteral("[[Enter]]")),
                        note(tr("Jump to a name"), tr("type its first letters"))}});
        groups.append({tr("Views"),
                       {row(m_gridAct, tr("Grid")), row(m_listAct, tr("List")), row(m_analyzeAct, tr("Disk usage")),
                        row(m_hiddenAct, tr("Show hidden files")), row(m_sidebarAct, tr("Show sidebar")),
                        row(m_zoomInAct, tr("Zoom in")), row(m_zoomOutAct, tr("Zoom out")),
                        row(m_zoomResetAct, tr("Normal size")),
                        note(tr("Zoom with the mouse"), tr("[[Ctrl]] + mouse wheel")),
                        note(tr("Quick Look (preview)"), QStringLiteral("[[Space]]")),
                        row(m_splitAct, tr("Split view")), row(m_copyToOtherAct, tr("Copy to the other side")),
                        row(m_moveToOtherAct, tr("Move to the other side"))}});
        QList<HelpDialog::Shortcut> files = {
            row(m_selectAllAct), row(m_selectMatchingAct), row(m_invertSelectionAct),
            row(m_copyAct), row(m_cutAct), row(m_pasteAct), row(m_renameAct, tr("Rename (several items: batch rename)")),
            row(m_newFolderAct), row(m_newFileAct), row(m_trashAct), row(m_deleteAct, tr("Delete permanently")),
            row(m_undoAct, tr("Undo")), row(m_redoAct, tr("Redo")), row(m_propertiesAct),
            row(m_bookmarkAct, tr("Bookmark this folder"))};
        groups.append({tr("Files"), files});
        QList<HelpDialog::Shortcut> windows = {
            row(m_newTabAct), row(m_closeAct), row(m_reopenTabAct),
            note(tr("Next / previous tab"), QStringLiteral("[[Ctrl+Tab]] / [[Ctrl+Shift+Tab]]")),
            note(tr("Go to tab 1…9"), QStringLiteral("[[Alt+1]] … [[Alt+9]]")),
            row(m_closeWindowAct), row(m_quitAct)};
        if (m_terminalAct)
            windows.prepend(row(m_terminalAct, tr("Terminal")));
        groups.append({tr("Tabs and Windows"), windows});
        groups.append({tr("Help"), {row(m_guideAct), row(m_shortcutsAct), row(m_prefsAct)}});

        dialog = new HelpDialog(groups, this);
    }
    dialog->showTopic(topic);
    dialog->show();
    dialog->raise();
    dialog->activateWindow();
}

void MainWindow::updateGitBadge()
{
    if (m_background || !m_pathBar)
        return;
    const GitStatus *git = m_model ? m_model->git() : nullptr;
    if (!git || !git->inRepository()) {
        m_pathBar->setGitInfo(QString(), 0, QString());
        return;
    }
    QString tip = tr("Git repository: %1").arg(QDir::toNativeSeparators(git->root()));
    QString branch = git->branch();
    if (git->blocked()) {
        branch = tr("git: off");
        tip += QLatin1Char('\n') + tr("Not checked: this repository's settings would run programs.");
    } else if (git->tooSlow()) {
        branch = tr("git: too big");
        tip += QLatin1Char('\n') + tr("Not checked: git took too long for this repository.");
    } else if (!git->ready()) {
        branch = QStringLiteral("…");
    } else {
        tip += QLatin1Char('\n') + (git->changeCount() == 0 ? tr("No changes")
                                     : git->changeCount() == 1 ? tr("1 changed item")
                                                                : tr("%1 changed items").arg(git->changeCount()));
    }
    m_pathBar->setGitInfo(branch, git->blocked() || git->tooSlow() ? 0 : git->changeCount(), tip);
}

void MainWindow::refreshGitAll()
{
    for (int i = 0; i < m_paneCount; ++i) {
        DirectoryModel *model = i == m_activePane ? m_model : m_panes[i].model;
        if (model)
            model->refreshGit();
    }
}

void MainWindow::resetGitEverywhere()
{
    for (QWidget *widget : QApplication::topLevelWidgets()) {
        if (auto *window = qobject_cast<MainWindow *>(widget)) {
            for (int i = 0; i < window->m_paneCount; ++i) {
                DirectoryModel *model = i == window->m_activePane ? window->m_model : window->m_panes[i].model;
                if (model)
                    model->resetGit();
            }
        }
    }
}

void MainWindow::applyExpandFolders()
{
    for (int i = 0; i < m_paneCount; ++i) {
        const bool active = i == m_activePane;
        DirectoryModel *model = active ? m_model : m_panes[i].model;
        const bool grid = active ? m_grid : m_panes[i].grid;
        if (model)
            model->setTreeEnabled(m_expandFolders && !grid);
    }
    if (m_detailView)
        m_detailView->viewport()->update();
}

void MainWindow::setExpandFoldersEverywhere(bool on)
{
    QSettings().setValue("view/expandFolders", on);
    for (QWidget *widget : QApplication::topLevelWidgets()) {
        if (auto *window = qobject_cast<MainWindow *>(widget)) {
            window->m_expandFolders = on;
            window->applyExpandFolders();
        }
    }
}

// Expandable folders: → opens the current folder, ← closes it, or goes to
// the folder an item is in (like Nautilus).
bool MainWindow::handleTreeKey(QKeyEvent *key)
{
    if (!m_model->isTreeActive() || key->modifiers() != Qt::NoModifier
        || (key->key() != Qt::Key_Right && key->key() != Qt::Key_Left))
        return false;
    const QModelIndex current = m_detailView->currentIndex().siblingAtColumn(0);
    if (!current.isValid())
        return false;
    const QString path = m_model->filePath(toSource(current));
    if (key->key() == Qt::Key_Right) {
        if (current.data(DirectoryModel::ExpandableRole).toBool() && !m_model->isExpanded(path))
            m_model->expand(path);
        return true; // never scroll sideways
    }
    if (m_model->isExpanded(path)) {
        m_model->collapse(path);
        return true;
    }
    if (current.data(DirectoryModel::TreeDepthRole).toInt() > 0) {
        const QModelIndex parent = toView(m_model->indexForPath(QFileInfo(path).absolutePath()));
        if (parent.isValid()) {
            m_detailView->selectionModel()->setCurrentIndex(
                parent, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
            m_detailView->scrollTo(parent);
        }
    }
    return true;
}

void MainWindow::applyListColumns()
{
    for (int i = 0; i < m_paneCount; ++i) {
        QTreeView *view = i == m_activePane ? m_detailView : m_panes[i].detailView;
        if (!view)
            continue;
        QHeaderView *header = view->header();
        for (int column : {DirectoryModel::OwnerColumn, DirectoryModel::GroupColumn,
                           DirectoryModel::PermissionsColumn}) {
            const bool shown = m_listColumns.contains(QString::number(column));
            header->setSectionHidden(column, !shown);
            if (shown && header->sectionSize(column) < 40)
                header->resizeSection(column, 110); // a header saved before these columns existed
        }
    }
}

void MainWindow::showListHeaderMenu(QHeaderView *header, const QPoint &pos)
{
    if (!Access::hasOwners())
        return; // no Unix owners or permission bits to show
    QMenu *menu = makeMenu();
    menu->setAttribute(Qt::WA_DeleteOnClose);
    menu->addSection(tr("Show Columns"));
    for (int column : {DirectoryModel::OwnerColumn, DirectoryModel::GroupColumn,
                       DirectoryModel::PermissionsColumn}) {
        QAction *act = menu->addAction(m_model->headerData(column, Qt::Horizontal).toString());
        act->setCheckable(true);
        const QString key = QString::number(column);
        act->setChecked(m_listColumns.contains(key));
        connect(act, &QAction::toggled, this, [this, key](bool on) {
            m_listColumns.removeAll(key);
            if (on)
                m_listColumns.append(key);
            applyListColumns();
        });
    }
    menu->popup(header->viewport()->mapToGlobal(pos));
}

bool MainWindow::canChangeSelection() const
{
    if (m_model->isTrash())
        return true; // the Trash's own folders
    QHash<QString, Access::FolderAccess> folders;
    for (const QModelIndex &viewIndex : selectedViewIndexes()) {
        const QModelIndex source = toSource(viewIndex);
        const QString path = m_model->filePath(source);
        const int slash = int(path.lastIndexOf(QLatin1Char('/')));
        const QString dir = slash <= 0 ? QStringLiteral("/") : path.left(slash);
        if (!Access::canChangeItemsIn(dir, source.data(DirectoryModel::OwnerIdRole).toUInt(), &folders))
            return false;
    }
    return true;
}

void MainWindow::fitListColumns()
{
    if (!m_detailView)
        return;
    const int width = m_viewStack->width();
    QHeaderView *header = m_detailView->header();
    header->setSectionHidden(DirectoryModel::TypeColumn, width < 640);
    header->setSectionHidden(DirectoryModel::ModifiedColumn, width < 460);
}

// Split view: a slim caption over each pane with its folder; the active
// pane's is highlighted.
void MainWindow::updatePaneCaptions()
{
    for (int i = 0; i < m_paneCount; ++i) {
        QLabel *caption = m_panes[i].caption;
        if (!caption)
            continue;
        caption->setVisible(m_split);
        const QString path = i == m_activePane ? m_current : m_panes[i].current;
        caption->setText(titleFor(path));
        caption->setToolTip(QDir::toNativeSeparators(path));
        if (m_background)
            continue; // only texts change; which pane is active didn't
        const bool active = i == m_activePane;
        if (caption->property("active").toBool() != active) {
            caption->setProperty("active", active); // styled by QLabel#paneCaption[active="true"]
            caption->style()->unpolish(caption);
            caption->style()->polish(caption);
        }
    }
}

void MainWindow::ensureSecondPane()
{
    if (m_paneCount > 1)
        return;
    // Build pane 1 into the members, then put pane 0 back.
    const QString start = m_current;
    const bool grid = m_grid;
    const int gridZoom = m_gridZoom;
    const int listZoom = m_listZoom;
    savePane(0);
    m_activePane = 1;
    m_grid = grid;
    m_gridZoom = gridZoom;
    m_listZoom = listZoom;
    m_current.clear();
    m_backHistory.clear();
    m_forwardHistory.clear();
    m_pendingSelection.clear();
    m_pendingScroll = -1;
    m_pendingRename.clear();
    m_recentSort = false;
    m_message.clear();
    QWidget *container = buildPane(m_paneSplitter, 1);
    m_paneSplitter->addWidget(container);
    m_iconView->setGridSize(m_gridDelegate->gridCellSize(m_iconView->fontMetrics()));
    sortBy(m_panes[0].detailView->header()->sortIndicatorSection(),
           m_panes[0].detailView->header()->sortIndicatorOrder()); // same order as the first pane
    savePane(1);
    loadPane(0);
    m_activePane = 0;
    {
        PaneScope scope(this, 1);
        navigateTo(start, false); // starts where the first pane is
    }
    const int half = qMax(1, m_paneSplitter->width() / 2);
    m_paneSplitter->setSizes({half, half});
}

void MainWindow::setSplitView(bool on)
{
    if (on == m_split)
        return;
    if (on) {
        if (m_analyzing)
            setGridMode(m_grid);
        ensureSecondPane();
        m_split = true;
        m_panes[1].container->show();
        activatePane(1); // the new pane is where you'll go next
    } else {
        if (m_activePane != 0)
            activatePane(0);
        m_split = false;
        m_panes[1].container->hide();
    }
    const QSignalBlocker block(m_splitAct);
    m_splitAct->setChecked(on);
    if (m_splitButton)
        m_splitButton->setChecked(on);
    updatePaneCaptions();
    updateEditActions();
}

QString MainWindow::otherPaneFolder() const
{
    if (!m_split || m_paneCount < 2)
        return {};
    const QString other = m_panes[1 - m_activePane].current;
    if (other.isEmpty() || DirectoryModel::isVirtualPath(other) || Trash::isFilesDir(other)
        || !QFileInfo(other).isDir() || !QFileInfo(other).isWritable())
        return {};
    return other;
}

// F5 / F6: the selection to the other pane's folder, through the normal
// copy/move engine (progress, conflicts, undo, USB safety).
void MainWindow::transferToOtherPane(bool move)
{
    const QString target = otherPaneFolder();
    const QStringList paths = selectedPaths();
    if (target.isEmpty() || paths.isEmpty())
        return;
    if (move && QFileInfo(paths.first()).absolutePath() == target) {
        showMessage(tr("They're already in that folder"));
        return;
    }
    move ? m_ops->move(paths, target) : m_ops->copy(paths, target);
}

// Everything one file pane needs: model, views, delegates, floating bar. It
// builds into the window's members (as the active pane), which the caller
// saves into m_panes[index].
QWidget *MainWindow::buildPane(QWidget *parent, int index)
{
    m_model = new DirectoryModel(this);
    m_model->setTreeEnabled(m_expandFolders && !m_grid);
    m_model->setSearchContents(QSettings().value("search/contents", false).toBool());
    m_proxy = new FileFilterProxy(this);
    m_proxy->setSourceModel(m_model);
    m_model->setShowHidden(m_showHidden);
    connect(m_model, &DirectoryModel::gitChanged, this, [this, index] {
        if (index == m_activePane && !m_background)
            updateGitBadge();
    });
    for (auto signal : {&QAbstractItemModel::rowsInserted, &QAbstractItemModel::rowsRemoved}) {
        connect(m_model, signal, this, [this, index] {
            if (index == m_activePane && !m_background && m_model->isTrash())
                updateEditActions(); // Restore All follows what's left in the Trash
        });
    }
    connect(m_model, &DirectoryModel::loaded, this, [this, index] {
        PaneScope scope(this, index);
        updateFloatingBar();
        updateEditActions();
        applyPendingSelection();
        if (m_pendingScroll >= 0) { // a tab coming back: where it was
            m_view->verticalScrollBar()->setValue(m_pendingScroll);
            m_pendingScroll = -1;
        }
    });
    // The open folder was deleted or moved: go to the nearest folder left.
    connect(m_model, &DirectoryModel::folderGone, this, [this, index](const QString &gone) {
        PaneScope scope(this, index);
        QString path = gone;
        while (!QFileInfo(path).isDir()) {
            const QString parent = QFileInfo(path).absolutePath();
            if (parent == path)
                break;
            path = parent;
        }
        navigateTo(path);
        showMessage(tr("“%1” no longer exists").arg(QFileInfo(gone).fileName()));
    });
    // New files may reach the view a little after a paste finishes.
    connect(m_proxy, &QAbstractItemModel::rowsInserted, this, [this, index] {
        PaneScope scope(this, index);
        applyPendingSelection();
    });
    // "Searching… 12 found" as results arrive.
    auto searchCount = [this, index] {
        PaneScope scope(this, index);
        if (m_model->isSearch())
            updateFloatingBar();
    };
    connect(m_model, &QAbstractItemModel::rowsInserted, this, searchCount);
    connect(m_model, &QAbstractItemModel::rowsRemoved, this, searchCount);
    connect(m_model, &QAbstractItemModel::modelReset, this, searchCount);

    m_gridDelegate = new FileItemDelegate(m_thumbnailer, this);
    m_listDelegate = new FileItemDelegate(m_thumbnailer, this);
    m_listDelegate->setGridMode(false);
    m_gridDelegate->setTheme(m_theme);
    m_listDelegate->setTheme(m_theme);

    auto *container = new QWidget(parent);
    container->setObjectName("paneContainer");
    auto *layout = new QVBoxLayout(container);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    auto *caption = new QLabel(container);
    caption->setObjectName("paneCaption");
    caption->hide(); // split view only
    caption->installEventFilter(this); // a click makes the pane active
    layout->addWidget(caption);

    m_viewStack = new QStackedWidget(container);
    m_viewStack->setObjectName("viewStack");
    m_viewStack->installEventFilter(this); // resize -> reposition overlays
    layout->addWidget(m_viewStack, 1);

    // Grid.
    m_iconView = new QListView(m_viewStack);
    m_iconView->setObjectName("files");
    m_iconView->setViewMode(QListView::IconMode);
    m_iconView->setMovement(QListView::Static);
    m_iconView->setResizeMode(QListView::Adjust);
    m_iconView->setUniformItemSizes(true);
    m_iconView->setWordWrap(true);
    m_iconView->setSpacing(0);
    setupFileView(m_iconView);
    m_iconView->setItemDelegate(m_gridDelegate);
    m_gridDelegate->setIconSize(FileItemDelegate::gridSizes().at(m_gridZoom));
    m_listDelegate->setIconSize(FileItemDelegate::listSizes().at(m_listZoom));
    m_iconView->setGridSize(m_gridDelegate->gridCellSize(m_iconView->fontMetrics()));

    // List with columns. Both views share the model and the selection.
    m_detailView = new QTreeView(m_viewStack);
    m_detailView->setObjectName("detailView");
    m_detailView->setRootIsDecorated(false);
    m_detailView->setItemsExpandable(false);
    m_detailView->setIndentation(0);
    m_detailView->setUniformRowHeights(true);
    m_detailView->setAllColumnsShowFocus(true);
    m_detailView->setSelectionBehavior(QAbstractItemView::SelectRows);
    setupFileView(m_detailView);
    m_detailView->setItemDelegate(m_listDelegate);
    QItemSelectionModel *unused = m_detailView->selectionModel();
    m_detailView->setSelectionModel(m_iconView->selectionModel());
    delete unused;
    QHeaderView *columns = m_detailView->header();
    columns->setStretchLastSection(false);
    columns->setHighlightSections(false);
    columns->setSectionsMovable(false);
    columns->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    columns->setSectionResizeMode(DirectoryModel::NameColumn, QHeaderView::Stretch);
    columns->resizeSection(DirectoryModel::SizeColumn, 110);
    columns->resizeSection(DirectoryModel::TypeColumn, 170);
    columns->resizeSection(DirectoryModel::ModifiedColumn, 130);
    columns->resizeSection(DirectoryModel::OwnerColumn, 110);
    columns->resizeSection(DirectoryModel::GroupColumn, 110);
    columns->resizeSection(DirectoryModel::PermissionsColumn, 110);
    for (int column : {DirectoryModel::OwnerColumn, DirectoryModel::GroupColumn,
                       DirectoryModel::PermissionsColumn})
        columns->setSectionHidden(column, !m_listColumns.contains(QString::number(column)));
    columns->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(columns, &QWidget::customContextMenuRequested, this,
            [this, columns](const QPoint &pos) { showListHeaderMenu(columns, pos); });
    // Header clicks sort the model (FileFilterProxy forwards sort()).
    columns->setSortIndicator(DirectoryModel::NameColumn, Qt::AscendingOrder);
    m_detailView->setSortingEnabled(true);
    connect(columns, &QHeaderView::sortIndicatorChanged, this, &MainWindow::updateSortActions);
    m_viewStack->addWidget(m_iconView);
    m_viewStack->addWidget(m_detailView);
    m_view = m_grid ? static_cast<QAbstractItemView *>(m_iconView) : m_detailView;
    m_viewStack->setCurrentWidget(m_view);

    QItemSelectionModel *selection = m_iconView->selectionModel();
    connect(selection, &QItemSelectionModel::currentChanged, this, [this, index] {
        if (index == m_activePane && !m_background && quickLookVisible())
            updateQuickLook(); // arrows move through the files, the preview follows
    });
    connect(selection, &QItemSelectionModel::selectionChanged, this, [this, index] {
        PaneScope scope(this, index);
        updateFloatingBar();
        updateEditActions();
    });
    connect(m_proxy, &QAbstractItemModel::layoutChanged, this, &MainWindow::updateRenameGeometry);

    m_floatingBar = new QLabel(m_viewStack);
    m_floatingBar->setObjectName("floatingBar");
    m_floatingBar->hide();
    m_messageTimer = new QTimer(this);
    m_messageTimer->setSingleShot(true);
    m_messageTimer->setInterval(3000);
    connect(m_messageTimer, &QTimer::timeout, this, [this, index] {
        PaneScope scope(this, index);
        m_message.clear();
        updateFloatingBar();
    });

    m_panes[index].container = container;
    m_panes[index].caption = caption;
    m_paneCount = qMax(m_paneCount, index + 1);
    return container;
}

// ---------------------------------------------------------------- theme ---

void MainWindow::applyTheme(bool force)
{
    // The applied theme (built from the application palette for "Follow
    // System"; never from widget palettes, which our style sheet changes).
    const Theme theme = currentTheme();
    if (!force && m_themeId == theme.id)
        return;
    m_theme = theme;
    m_themeId = theme.id;

    setStyleSheet(styleSheetFor(m_theme));

    m_backButton->setIcon(glyphIcon(Glyph::ChevronLeft, m_theme.fg));
    m_forwardButton->setIcon(glyphIcon(Glyph::ChevronRight, m_theme.fg));
    m_searchButton->setIcon(glyphIcon(Glyph::Search, m_theme.fg));
    m_menuButton->setIcon(glyphIcon(Glyph::Menu, m_theme.fg));
    m_viewButton->setIcon(glyphIcon(m_grid ? Glyph::List : Glyph::Grid, m_theme.fg));
    m_analyzeButton->setIcon(glyphIcon(Glyph::Usage, m_theme.fg));
    if (m_csd) {
        m_minButton->setIcon(glyphIcon(Glyph::Minimize, m_theme.fg));
        m_closeButton->setIcon(glyphIcon(Glyph::Close, m_theme.fg));
        updateWindowState();
    }

    static_cast<SidebarDelegate *>(m_sidebar->itemDelegate())->lineColor = m_theme.border;
    static_cast<SidebarDelegate *>(m_sidebar->itemDelegate())->ejectIcon =
        glyphIcon(Glyph::Eject, m_theme.fg);
    m_pathBar->setTheme(m_theme);
    for (int i = 0; i < m_paneCount; ++i) {
        m_panes[i].gridDelegate->setTheme(m_theme);
        m_panes[i].listDelegate->setTheme(m_theme);
    }
    if (m_splitButton)
        m_splitButton->setIcon(glyphIcon(Glyph::Split, m_theme.fg));
    m_opsPanel->setTheme(m_theme);
    for (int i = 0; m_tabBar && i < m_tabBar->count(); ++i) {
        if (auto *close = qobject_cast<QToolButton *>(m_tabBar->tabButton(i, QTabBar::RightSide)))
            close->setIcon(glyphIcon(Glyph::Close, m_theme.dimFg));
    }
#if defined(ARGONAUT_TERMINAL)
    m_terminalButton->setIcon(glyphIcon(Glyph::Terminal, m_theme.fg));
    m_terminalCloseButton->setIcon(glyphIcon(Glyph::Close, m_theme.fg));
    m_terminal->setTheme(m_theme);
#endif
    m_toast->setTheme(m_theme);
    m_view->viewport()->update();

    populateSidebar();
    syncSidebarSelection();
}

void MainWindow::changeEvent(QEvent *event)
{
    QMainWindow::changeEvent(event);
    // Widgets with a style sheet get PaletteChange rather than
    // ApplicationPaletteChange. applyTheme() reads the application palette
    // and returns early if light/dark didn't change, so this can't loop.
    if (event->type() == QEvent::ApplicationPaletteChange
        || event->type() == QEvent::PaletteChange)
        applyTheme();
    else if (event->type() == QEvent::WindowStateChange)
        updateWindowState();
    else if (event->type() == QEvent::ActivationChange && isActiveWindow())
        refreshGitAll(); // back from an editor or a terminal: files may have changed
}

// -------------------------------------------------------------- sidebar ---

void MainWindow::populateSidebar()
{
    const int scroll = m_sidebar->verticalScrollBar()->value();
    m_sidebar->clear();
    const QColor iconColor = m_theme.fg;

    auto addItem = [&](const QString &name, const QString &path, Glyph glyph, SidebarKind kind) {
        auto *item = new QListWidgetItem(glyphIcon(glyph, iconColor), name, m_sidebar);
        item->setData(PathRole, path);
        item->setData(KindRole, int(kind));
        item->setToolTip(QDir::toNativeSeparators(path));
        return item;
    };
    auto addSeparator = [&] {
        auto *sep = new QListWidgetItem(m_sidebar);
        sep->setFlags(Qt::NoItemFlags);
        sep->setData(SeparatorRole, true);
    };

    const struct {
        QStandardPaths::StandardLocation loc;
        const char *name;
        Glyph glyph;
    } places[] = {
        {QStandardPaths::HomeLocation, QT_TR_NOOP("Home"), Glyph::Home},
        {QStandardPaths::DesktopLocation, QT_TR_NOOP("Desktop"), Glyph::Desktop},
        {QStandardPaths::DocumentsLocation, QT_TR_NOOP("Documents"), Glyph::Documents},
        {QStandardPaths::DownloadLocation, QT_TR_NOOP("Downloads"), Glyph::Downloads},
        {QStandardPaths::MusicLocation, QT_TR_NOOP("Music"), Glyph::Music},
        {QStandardPaths::PicturesLocation, QT_TR_NOOP("Pictures"), Glyph::Pictures},
        {QStandardPaths::MoviesLocation, QT_TR_NOOP("Videos"), Glyph::Videos},
    };
    addItem(tr("Recent"), DirectoryModel::recentPath(), Glyph::Recent, PlaceItem)->setToolTip(tr("Recently used files"));
    addItem(tr("Starred"), DirectoryModel::starredPath(), Glyph::Star, PlaceItem)->setToolTip(tr("Starred files and folders"));
    QStringList shown;
    for (const auto &p : places) {
        const QString path = QDir::cleanPath(QStandardPaths::writableLocation(p.loc));
        if (path.isEmpty() || shown.contains(path) || !QFileInfo(path).isDir())
            continue;
        shown << path;
        addItem(tr(p.name), path, p.glyph, PlaceItem);
    }

    if (Trash::isSupported()) {
        // Shows every trash folder merged (home and removable drives).
        const QString trash = Trash::homeFilesDir();
        QDir().mkpath(trash);
        QDir().mkpath(QFileInfo(trash).absolutePath() + QStringLiteral("/info"));
        addItem(tr("Trash"), trash, Glyph::Trash, PlaceItem);
    }

    // Bookmarks (shared with Nautilus). Ones that duplicate a place above,
    // or whose folder is gone, aren't shown (but stay in the file).
    bool bookmarkSection = false;
    for (const Bookmarks::Bookmark &b : Bookmarks::load()) {
        if (shown.contains(b.path) || !QFileInfo(b.path).isDir())
            continue;
        if (!bookmarkSection) {
            addSeparator();
            bookmarkSection = true;
        }
        shown << b.path;
        addItem(b.displayName(), b.path, Glyph::Folder, BookmarkItem);
    }

    addSeparator();

    m_lastMounts.clear();
    for (const QStorageInfo &vol : QStorageInfo::mountedVolumes()) {
        if (!vol.isValid() || !vol.isReady())
            continue;
        const QString root = vol.rootPath();
        m_lastMounts << root;
#if defined(Q_OS_LINUX)
        // Skip system/snap mounts; keep "/" and user-visible drives.
        if (root != "/" && !root.startsWith("/media/") && !root.startsWith("/mnt/")
            && !root.startsWith("/run/media/"))
            continue;
#elif defined(Q_OS_MACOS)
        if (root.startsWith("/System/Volumes"))
            continue;
#endif
        const bool system = root == QDir::rootPath();
        QListWidgetItem *item = addItem(system ? tr("Computer") : vol.displayName(), root,
                                        system ? Glyph::Drive : Glyph::Removable, DriveItem);
#if defined(Q_OS_LINUX)
        // Drives mounted by the desktop (udisks) can be ejected.
        if (root.startsWith("/media/") || root.startsWith("/run/media/")) {
            item->setData(EjectRole, true);
            item->setToolTip(QDir::toNativeSeparators(root) + QLatin1Char('\n') + tr("Eject: click ⏏"));
        }
#else
        Q_UNUSED(item)
#endif
    }

#if defined(Q_OS_LINUX)
    // Network: connected servers (and phones), network bookmarks not
    // connected now, and Connect to Server….
    addSeparator();
    QSet<QString> connected;
    for (const Remote::Mount &m : Remote::mounts()) {
        QListWidgetItem *item = addItem(m.name, m.localPath, Glyph::Network, NetworkItem);
        item->setData(EjectRole, true);
        item->setToolTip(m.address + QLatin1Char('\n') + tr("Disconnect: click ⏏"));
        connected.insert(QUrl(m.address).host());
    }
    for (const Bookmarks::Bookmark &b : Bookmarks::loadRemote()) {
        const QUrl url(b.path);
        if (connected.contains(url.host()))
            continue;
        QListWidgetItem *item = addItem(b.label.isEmpty() ? url.host() : b.label, b.path, Glyph::Network, NetworkItem);
        item->setToolTip(b.path);
    }
    addItem(tr("Connect to Server…"), kConnectPlace, Glyph::Network, PlaceItem)->setToolTip(tr("sftp, smb, ftp, WebDAV…"));
#endif
    m_sidebar->verticalScrollBar()->setValue(scroll);
}

void MainWindow::scheduleSidebarRefresh()
{
    m_sidebarRefresh->start();
}

void MainWindow::showSidebarMenu(const QPoint &pos)
{
    QListWidgetItem *item = m_sidebar->itemAt(pos);
    if (!item || item->data(SeparatorRole).toBool())
        return;
    const QString path = item->data(PathRole).toString();
    const int kind = item->data(KindRole).toInt();

    QMenu *menu = makeMenu();
    menu->setAttribute(Qt::WA_DeleteOnClose);
    menu->addAction(tr("Open"), this, [this, path] { navigateTo(path); });
    if (m_openTerminalHereAct) {
        menu->addAction(tr("Open in Terminal"), this, [this, path] {
            m_contextFolder = path;
            m_openTerminalHereAct->trigger();
        });
    }
    if (kind == BookmarkItem) {
        menu->addSeparator();
        menu->addAction(tr("Rename…"), this, [this, path, item] {
            bool ok = false;
            const QString label = QInputDialog::getText(this, tr("Rename Bookmark"), tr("Name"),
                                                        QLineEdit::Normal, item->text(), &ok);
            if (ok)
                Bookmarks::rename(path, label == QFileInfo(path).fileName() ? QString() : label);
            scheduleSidebarRefresh();
        });
        menu->addAction(tr("Remove from Bookmarks"), this, [this, path] {
            Bookmarks::remove(path);
            scheduleSidebarRefresh();
            updateBookmarkAction();
        });
    } else if (kind == NetworkItem && item->data(EjectRole).toBool()) {
        menu->addSeparator();
        const QString name = item->text();
        menu->addAction(glyphIcon(Glyph::Eject, m_theme.fg), tr("Disconnect"), this,
                        [this, path, name] { ejectDrive(path, name); });
    } else if (kind == DriveItem && item->data(EjectRole).toBool()) {
        menu->addSeparator();
        const QString name = item->text();
        menu->addAction(glyphIcon(Glyph::Eject, m_theme.fg), tr("Eject"), this,
                        [this, path, name] { ejectDrive(path, name); });
    }
    menu->popup(m_sidebar->viewport()->mapToGlobal(pos));
}

// Ctrl+D: add the open folder to the bookmarks, or remove it.
void MainWindow::toggleBookmark()
{
    if (m_current.isEmpty() || m_model->isTrash() || m_model->isVirtual())
        return;
    if (Bookmarks::contains(m_current)) {
        Bookmarks::remove(m_current);
        showMessage(tr("Removed from bookmarks"));
    } else if (Bookmarks::add(m_current)) {
        showMessage(tr("“%1” added to bookmarks").arg(QFileInfo(m_current).fileName()));
    }
    scheduleSidebarRefresh();
    updateBookmarkAction();
}

void MainWindow::updateBookmarkAction()
{
    if (m_background)
        return;
    const bool marked = Bookmarks::contains(m_current);
    m_bookmarkAct->setText(marked ? tr("Remove Bookmark") : tr("Bookmark This Folder"));
    m_bookmarkAct->setEnabled(!m_current.isEmpty() && !m_model->isTrash() && !m_model->isVirtual());
}

void MainWindow::applySearchFilters()
{
    const auto type = DirectoryModel::TypeFilter(m_typeFilters->checkedAction()->data().toInt());
    const auto date = DirectoryModel::DateFilter(m_dateFilters->checkedAction()->data().toInt());
    QStringList shown;
    if (type != DirectoryModel::TypeFilter::Any)
        shown << m_typeFilters->checkedAction()->text();
    if (date != DirectoryModel::DateFilter::Any)
        shown << m_dateFilters->checkedAction()->text();
    m_filterButton->setText(shown.isEmpty() ? tr("Filters") : shown.join(QStringLiteral(" · ")));
    m_model->setSearchFilters(type, date);
    updateFloatingBar();
}

// ------------------------------------------------------- drag, extras ---

// A drag held over a folder: open it in place (list with expandable
// folders), or go into it, like Nautilus.
void MainWindow::springOver(const QString &folder, int pane)
{
    if (pane != m_activePane && pane < m_paneCount) {
        PaneScope scope(this, pane); // a drag over the other pane: open it there
        springOver(folder, m_activePane);
        return;
    }
    if (!m_grid && m_model->isTreeActive() && m_model->indexForPath(folder).isValid()) {
        if (!m_model->isExpanded(folder))
            m_model->expand(folder);
        return;
    }
    navigateTo(folder);
}

// Opens the email app with the files attached (xdg-email); it sends nothing.
void MainWindow::sendByEmail(const QStringList &paths)
{
    QStringList args;
    for (const QString &p : paths)
        args << QStringLiteral("--attach") << p;
    const QString program = qEnvironmentVariable("ARGONAUT_EMAIL_PROGRAM", QStringLiteral("xdg-email"));
    if (!QProcess::startDetached(program, args))
        showMessage(tr("Couldn't open the email app (xdg-email is missing)"));
}

// GNOME: copy the image into ~/.local/share/backgrounds (so deleting or
// moving the original later doesn't break it), then point the desktop at it.
void MainWindow::setAsBackground(const QString &image)
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/backgrounds");
    QDir().mkpath(dir);
    QString copy = dir + QLatin1Char('/') + QFileInfo(image).fileName();
    if (QFileInfo(copy).exists() && QFileInfo(copy).size() != QFileInfo(image).size())
        copy = uniqueTargetPath(dir, QFileInfo(image).fileName(), false);
    if (!QFileInfo::exists(copy) && !QFile::copy(image, copy)) {
        showMessage(tr("Couldn't copy the image for the background"));
        return;
    }
    const QString uri = QUrl::fromLocalFile(copy).toString();
    const QString gsettings = qEnvironmentVariable("ARGONAUT_GSETTINGS_PROGRAM", QStringLiteral("gsettings"));
    bool ok = true;
    for (const char *key : {"picture-uri", "picture-uri-dark"}) {
        ok = QProcess::execute(gsettings, {QStringLiteral("set"), QStringLiteral("org.gnome.desktop.background"),
                                           QString::fromLatin1(key), uri})
                 == 0
             && ok;
    }
    showMessage(ok ? tr("Background changed") : tr("Couldn't change the background (GNOME only)"));
}

// Scripts ▸: the user's scripts in ~/.local/share/nautilus/scripts, run
// the way Nautilus runs them (same folder, same variables), so existing
// ones work unchanged. Sub-folders become sub-menus.
QMenu *MainWindow::scriptsMenu(const QStringList &selection)
{
#if !defined(Q_OS_UNIX)
    Q_UNUSED(selection);
    return nullptr;
#else
    const QString root = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/nautilus/scripts");
    if (m_model->isVirtual() || m_model->isTrash() || !QFileInfo(root).isDir())
        return nullptr;
    std::function<bool(QMenu *, const QString &, int)> fill = [&](QMenu *menu, const QString &dir, int depth) {
        bool any = false;
        const QFileInfoList entries = QDir(dir).entryInfoList(QDir::Dirs | QDir::Files | QDir::NoDotAndDotDot, QDir::Name | QDir::DirsFirst);
        for (const QFileInfo &e : entries) {
            if (e.isDir() && depth < 3) {
                QMenu *sub = makeMenu();
                sub->setTitle(e.fileName());
                if (fill(sub, e.filePath(), depth + 1)) {
                    sub->setParent(menu, sub->windowFlags());
                    menu->addMenu(sub);
                    any = true;
                } else {
                    delete sub;
                }
            } else if (e.isFile() && e.isExecutable()) {
                menu->addAction(e.fileName(), this, [this, script = e.filePath(), selection] {
                    const QString folder = m_current;
                    QStringList paths, uris, args;
                    for (const QString &p : selection) {
                        paths << p;
                        uris << QUrl::fromLocalFile(p).toString();
                        args << (QFileInfo(p).absolutePath() == folder ? QFileInfo(p).fileName() : p);
                    }
                    QProcess process;
                    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
                    env.insert(QStringLiteral("NAUTILUS_SCRIPT_SELECTED_FILE_PATHS"), paths.isEmpty() ? QString() : paths.join(QLatin1Char('\n')) + QLatin1Char('\n'));
                    env.insert(QStringLiteral("NAUTILUS_SCRIPT_SELECTED_URIS"), uris.isEmpty() ? QString() : uris.join(QLatin1Char('\n')) + QLatin1Char('\n'));
                    env.insert(QStringLiteral("NAUTILUS_SCRIPT_CURRENT_URI"), QUrl::fromLocalFile(folder).toString());
                    const QRect g = geometry();
                    env.insert(QStringLiteral("NAUTILUS_SCRIPT_WINDOW_GEOMETRY"),
                               QStringLiteral("%1x%2+%3+%4").arg(g.width()).arg(g.height()).arg(g.x()).arg(g.y()));
                    process.setProcessEnvironment(env);
                    process.setProgram(script);
                    process.setArguments(args);
                    process.setWorkingDirectory(folder);
                    if (!process.startDetached())
                        showMessage(tr("Couldn't run “%1”").arg(QFileInfo(script).fileName()));
                });
                any = true;
            }
        }
        return any;
    };
    QMenu *menu = makeMenu();
    menu->setTitle(tr("Scripts"));
    if (!fill(menu, root, 0)) {
        delete menu;
        return nullptr;
    }
    menu->addSeparator();
    menu->addAction(tr("Open Scripts Folder"), this, [this, root] { navigateTo(root); });
    menu->setAttribute(Qt::WA_DeleteOnClose);
    return menu;
#endif
}

// ------------------------------------------------------------ organize ---

void MainWindow::organizeWithClaude()
{
    if (m_current.isEmpty() || !QFileInfo(m_current).isWritable() || m_model->isVirtual())
        return;
    const QString folder = m_current;
    OrganizerDialog dialog(folder, this);
    connect(&dialog, &OrganizerDialog::openPreferences, this, [this] { m_prefsAct->trigger(); });
    if (dialog.exec() != QDialog::Accepted)
        return;
    QList<QPair<QString, QPair<QString, QString>>> moves;
    for (const Organizer::Move &m : dialog.keptMoves())
        moves.append({m.item, {m.folder, m.newName}});
    applyOrganizePlan(folder, moves);
}

// The kept moves: new folders first (undoable on their own), then one move
// job with renames ("Undo Organize" puts everything back).
void MainWindow::applyOrganizePlan(const QString &folder, const QList<QPair<QString, QPair<QString, QString>>> &moves)
{
    QList<TransferResult> created; // the top-most folders made for the plan
    QList<Transfer> transfers;
    for (const auto &m : moves) {
        const QString &item = m.first;
        const QString &sub = m.second.first;
        const QString &newName = m.second.second;
        QString dir = folder;
        for (const QString &part : sub.split(QLatin1Char('/'), Qt::SkipEmptyParts)) {
            dir += QLatin1Char('/') + part;
            if (!QFileInfo::exists(dir)) {
                if (!QDir().mkdir(dir)) {
                    showMessage(tr("Couldn't create the folder “%1”").arg(part));
                    return;
                }
                bool inside = false;
                for (const TransferResult &c : std::as_const(created))
                    inside = inside || dir.startsWith(c.target + QLatin1Char('/'));
                if (!inside)
                    created.append({QString(), dir});
            }
        }
        transfers.append({folder + QLatin1Char('/') + item, dir, newName.isEmpty() ? item : newName});
    }
    if (!created.isEmpty())
        m_ops->recordUndo(FileJob::Kind::Copy, created, tr("New Folders"));
    if (!transfers.isEmpty())
        m_ops->moveItems(transfers, tr("Organize"));
    showMessage(transfers.size() == 1 ? tr("Organizing 1 item…") : tr("Organizing %1 items…").arg(transfers.size()));
}

// ------------------------------------------------------------- servers ---

void MainWindow::connectToServer(const QString &address)
{
#if !defined(Q_OS_LINUX)
    showMessage(tr("Connecting to servers needs GVfs (Linux)"));
    Q_UNUSED(address);
    return;
#else
    QString clean = address.trimmed();
    if (clean.startsWith(QLatin1String("ssh://")))
        clean.replace(0, 3, QStringLiteral("sftp")); // GVfs knows it as sftp
    auto *mounter = new Remote::Mounter(clean, this);
    showMessage(tr("Connecting to %1…").arg(QUrl(clean).host().isEmpty() ? clean : QUrl(clean).host()));
    connect(mounter, &Remote::Mounter::needLogin, this,
            [this, mounter](const QString &message, bool askUser, const QString &defaultUser, bool askDomain,
                            const QString &defaultDomain, bool retry) {
                LoginDialog dialog(mounter->address(), message, askUser, defaultUser, askDomain, defaultDomain, retry, this);
                if (dialog.exec() != QDialog::Accepted) {
                    mounter->cancel();
                    return;
                }
                if (dialog.anonymous()) {
                    mounter->start(true);
                    return;
                }
                mounter->answer({dialog.user(), dialog.domain(), dialog.password()});
            });
    connect(mounter, &Remote::Mounter::question, this,
            [this, mounter](const QString &message, const QStringList &choices, const QList<int> &numbers) {
                // E.g. an unknown SSH host key: "Log In Anyway" / "Cancel Login".
                QMessageBox box(QMessageBox::Question, tr("Connect to Server"), message, QMessageBox::NoButton, this);
                QList<QAbstractButton *> buttons;
                for (const QString &choice : choices)
                    buttons << box.addButton(choice, QMessageBox::AcceptRole);
                box.exec();
                const int at = int(buttons.indexOf(box.clickedButton()));
                if (at < 0) {
                    mounter->cancel();
                    return;
                }
                mounter->choose(numbers.value(at, at));
            });
    connect(mounter, &Remote::Mounter::finished, this, [this, mounter, clean](bool ok, const QString &local, const QString &error) {
        mounter->deleteLater();
        scheduleSidebarRefresh();
        if (!ok) {
            if (!error.isEmpty())
                showMessage(error);
            return;
        }
        ConnectDialog::rememberServer(clean);
        if (local.isEmpty()) {
            chooseShare(clean); // smb://server/: a list of shares, no folder
            return;
        }
        navigateTo(local);
    });
    mounter->start();
#endif
}

// smb://server/ has no folder of its own: pick one of its shares.
void MainWindow::chooseShare(const QString &serverAddress)
{
    auto *list = new QProcess(this);
    connect(list, &QProcess::finished, this, [this, list, serverAddress] {
        QStringList shares;
        for (const QString &line : QString::fromUtf8(list->readAllStandardOutput()).split(QLatin1Char('\n'))) {
            const QString name = line.trimmed();
            if (!name.isEmpty() && !name.endsWith(QLatin1Char('$'))) // hidden admin shares (C$, IPC$)
                shares << name;
        }
        list->deleteLater();
        if (shares.isEmpty()) {
            showMessage(tr("%1 has no shared folders").arg(QUrl(serverAddress).host()));
            return;
        }
        bool ok = false;
        const QString share = QInputDialog::getItem(this, tr("Shared Folders"),
                                                    tr("Shared folders on %1:").arg(QUrl(serverAddress).host()), shares,
                                                    0, false, &ok);
        if (!ok)
            return;
        QString base = serverAddress;
        if (!base.endsWith(QLatin1Char('/')))
            base += QLatin1Char('/');
        connectToServer(base + QString::fromUtf8(QUrl::toPercentEncoding(share)));
    });
    list->start(qEnvironmentVariable("ARGONAUT_GIO_PROGRAM", QStringLiteral("gio")), {QStringLiteral("list"), serverAddress});
}

// Unmount and power off a USB drive (GNOME's own tool does it safely).
void MainWindow::ejectDrive(const QString &root, const QString &name)
{
    for (FileJob *job : m_ops->activeJobs()) {
        for (const Transfer &t : job->transfers()) {
            if (t.source.startsWith(root + QLatin1Char('/')) || t.targetDir == root
                || t.targetDir.startsWith(root + QLatin1Char('/'))) {
                showMessage(tr("Wait until copying to or from “%1” is finished").arg(name));
                return;
            }
        }
    }
    if (m_current == root || m_current.startsWith(root + QLatin1Char('/')))
        navigateTo(QDir::homePath()); // don't keep the drive busy ourselves

    if (const Remote::Mount server = Remote::mountOf(root); !server.localPath.isEmpty()) {
        showMessage(tr("Disconnecting from “%1”…").arg(name));
        Remote::unmount(server.address, this, [this, name](bool ok, const QString &error) {
            showMessage(ok ? tr("Disconnected from “%1”").arg(name)
                           : tr("Couldn't disconnect from “%1”: %2").arg(name, error));
            scheduleSidebarRefresh();
        });
        return;
    }
    const QString program = qEnvironmentVariable("ARGONAUT_EJECT_PROGRAM", QStringLiteral("gio"));
    auto *process = new QProcess(this);
    connect(process, &QProcess::finished, this, [this, process, name](int code, QProcess::ExitStatus status) {
        if (status == QProcess::NormalExit && code == 0) {
            showMessage(tr("“%1” can be safely removed").arg(name));
        } else {
            QString why = QString::fromLocal8Bit(process->readAllStandardError()).trimmed();
            why = why.section(QLatin1Char('\n'), 0, 0);
            if (why.contains(QLatin1String("busy"), Qt::CaseInsensitive))
                why = tr("it's in use by another program or the terminal");
            showMessage(why.isEmpty() ? tr("Couldn't eject “%1”").arg(name)
                                      : tr("Couldn't eject “%1”: %2").arg(name, why));
        }
        scheduleSidebarRefresh();
        process->deleteLater();
    });
    connect(process, &QProcess::errorOccurred, this, [this, process, name](QProcess::ProcessError e) {
        if (e == QProcess::FailedToStart) {
            showMessage(tr("Couldn't eject “%1”: the gio tool isn't installed").arg(name));
            process->deleteLater();
        }
    });
    showMessage(tr("Ejecting “%1”…").arg(name));
    process->start(program, {QStringLiteral("mount"), QStringLiteral("--eject"), root});
}

// ------------------------------------------------------------ settings ---

void MainWindow::loadSettings()
{
    QSettings settings;
    if (settings.contains("window/geometry"))
        restoreGeometry(settings.value("window/geometry").toByteArray());
    if (settings.contains("window/sidebarSplitter"))
        m_splitter->restoreState(settings.value("window/sidebarSplitter").toByteArray());
    m_terminalHeight = settings.value("window/terminalHeight", 0).toInt();
    if (settings.contains("view/listHeader"))
        m_detailView->header()->restoreState(settings.value("view/listHeader").toByteArray());
    m_listColumns = settings.value("view/listColumns").toStringList();
    m_expandFolders = settings.value("view/expandFolders", false).toBool();
    applyExpandFolders();
    applyListColumns(); // after restoreState: an older saved header knows only 4 columns
        fitListColumns(); // the saved layout may hide columns this width has room for
    sortBy(settings.value("view/sortColumn", DirectoryModel::NameColumn).toInt(),
           Qt::SortOrder(settings.value("view/sortOrder", int(Qt::AscendingOrder)).toInt()));
    m_hiddenAct->setChecked(settings.value("view/showHidden", false).toBool());
    m_sidebarAct->setChecked(settings.value("view/sidebar", true).toBool());
    setGridMode(settings.value("view/grid", true).toBool());
    // The split view comes back (once the first pane has its folder).
    if (settings.value("view/split", false).toBool()) {
        const QString otherPath = settings.value("view/splitPath").toString();
        QTimer::singleShot(0, this, [this, otherPath] {
            if (m_current.isEmpty() || m_split)
                return;
            setSplitView(true);
            if (!otherPath.isEmpty() && (QFileInfo(otherPath).isDir() || DirectoryModel::isVirtualPath(otherPath)))
                navigateTo(otherPath, false);
            activatePane(0);
        });
    }
    m_analyzeFilesAct->setChecked(settings.value("view/analyzeFiles", false).toBool());
    // Saved as pixels (not levels), so changing the levels later keeps the choice.
    auto nearest = [](const QList<int> &sizes, int px) {
        int best = 0;
        for (int i = 0; i < sizes.size(); ++i) {
            if (qAbs(sizes.at(i) - px) < qAbs(sizes.at(best) - px))
                best = i;
        }
        return best;
    };
    m_gridZoom = nearest(FileItemDelegate::gridSizes(),
                         settings.value("view/gridIconSize", FileItemDelegate::DefaultGridIconSize).toInt());
    m_listZoom = nearest(FileItemDelegate::listSizes(),
                         settings.value("view/listIconSize", FileItemDelegate::DefaultListIconSize).toInt());
    applyZoom();
}

void MainWindow::saveSettings()
{
    QSettings settings;
    settings.setValue("window/geometry", saveGeometry());
    settings.setValue("window/sidebarSplitter", m_splitter->saveState());
    if (m_terminalPanel && m_terminalPanel->isVisible())
        m_terminalHeight = m_terminalPanel->height();
    settings.setValue("window/terminalHeight", m_terminalHeight);
    settings.setValue("view/listHeader", m_detailView->header()->saveState());
    settings.setValue("view/listColumns", m_listColumns);
    settings.setValue("view/sortColumn", m_recentSort ? m_sortColumnBefore : m_model->sortColumn());
    settings.setValue("view/sortOrder", int(m_recentSort ? m_sortOrderBefore : m_model->sortOrder()));
    settings.setValue("view/showHidden", m_showHidden);
    settings.setValue("view/sidebar", m_sidebarAct->isChecked());
    settings.setValue("view/grid", m_grid);
    settings.setValue("view/split", m_split);
    if (m_split)
        settings.setValue("view/splitPath", m_panes[1].current);
    settings.setValue("view/gridIconSize", FileItemDelegate::gridSizes().at(m_gridZoom));
    settings.setValue("view/listIconSize", FileItemDelegate::listSizes().at(m_listZoom));
    settings.setValue("view/analyzeFiles", m_analyzeFilesAct->isChecked());
}

// Highlight the sidebar entry matching the current folder, like Nautilus.
void MainWindow::syncSidebarSelection()
{
    if (m_background)
        return;
    for (int i = 0; i < m_sidebar->count(); ++i) {
        QListWidgetItem *item = m_sidebar->item(i);
        const QString path = item->data(PathRole).toString();
        if (!path.isEmpty() && (path == m_current || QDir::cleanPath(path) == m_current)) {
            m_sidebar->setCurrentItem(item);
            return;
        }
    }
    m_sidebar->clearSelection();
    m_sidebar->setCurrentRow(-1);
}

// ----------------------------------------------------------- navigation ---

void MainWindow::openFolders(const QStringList &folders)
{
    if (folders.isEmpty())
        return;
    navigateTo(folders.first());
    for (int i = 1; i < folders.size(); ++i)
        openTab(folders.at(i), false);
}

void MainWindow::showItems(const QStringList &items)
{
    QStringList dirs;
    QHash<QString, QStringList> byDir;
    for (const QString &item : items) {
        const QString dir = QFileInfo(item).absolutePath();
        if (!byDir.contains(dir))
            dirs << dir;
        byDir[dir] << QDir::cleanPath(item);
    }
    if (dirs.isEmpty())
        return;
    if (navigateTo(dirs.first()))
        selectPaths(byDir.value(dirs.first())); // applied once the folder is listed
    for (int i = 1; i < dirs.size(); ++i)
        openTab(dirs.at(i), false);
}

bool MainWindow::navigateTo(const QString &path, bool addToHistory)
{
    // A server address (typed, a bookmark, the command line): connect first.
    if (path == kConnectPlace) {
        m_connectAct->trigger();
        return false;
    }
    if (Remote::isRemoteAddress(path)) {
        connectToServer(path);
        return false; // opens when connected
    }
    // Recent and Starred aren't folders: lists of items from anywhere.
    const bool place = DirectoryModel::isVirtualPath(path);
    const QFileInfo info(path);
    if (!place && (!info.exists() || !info.isDir())) {
        showMessage(tr("Folder not found: %1").arg(path));
        return false;
    }
    if (!place && (!info.isReadable() || !info.isExecutable())) {
        showMessage(tr("You don't have permission to open “%1”").arg(info.fileName()));
        return false;
    }

    const QString clean = place ? path : QDir::cleanPath(info.absoluteFilePath());
    if (clean == m_current)
        return true;

    if (!m_background) {
        // Leaving search here must not re-list the old folder: setPath()
        // below ends the search anyway.
        const QSignalBlocker blocker(m_searchEdit);
        if (m_searchTimer)
            m_searchTimer->stop();
        setSearchActive(false);
        cancelRename();
    }
    m_pendingSelection.clear();
    m_pendingRename.clear();
    if (addToHistory && !m_current.isEmpty()) {
        m_backHistory.push(m_current);
        m_forwardHistory.clear();
    }
    m_current = clean;

    m_thumbnailer->cancelPending(); // the old folder's queued thumbnails
    if (m_quickLook && !m_background)
        m_quickLook->hide();
    m_model->setPath(clean);
    // Recent: newest first. The folder sort comes back when leaving it.
    const bool recent = DirectoryModel::placeOf(clean) == DirectoryModel::Place::Recent;
    if (recent && !m_recentSort) {
        m_sortColumnBefore = m_model->sortColumn();
        m_sortOrderBefore = m_model->sortOrder();
        m_recentSort = true;
        sortBy(DirectoryModel::ModifiedColumn, Qt::DescendingOrder);
    } else if (!recent && m_recentSort) {
        m_recentSort = false;
        sortBy(m_sortColumnBefore, m_sortOrderBefore);
    }
    if (m_analyzing && !m_background) { // Analyze is the first pane's, and the active one's
        if (place)
            setGridMode(m_grid); // Analyze measures folders
        else
            m_usageModel->setRoot(clean); // instant if it's inside what was measured
    }
    m_view->scrollToTop();

    // Tabs belong to the first pane.
    const QString title = titleFor(clean);
    if (m_activePane == 0 && m_tabBar && m_activeTab < m_tabBar->count()) {
        m_tabBar->setTabText(m_activeTab, title);
        m_tabBar->setTabToolTip(m_activeTab, QDir::toNativeSeparators(clean));
        m_tabs[m_activeTab].path = clean;
    }
    updatePaneCaptions();
    if (m_background)
        updateFloatingBar(); // this pane's own; the header shows the other one
    else
        refreshChrome();
    return true;
}

void MainWindow::goBack()
{
    if (m_backHistory.isEmpty())
        return;
    const QString previous = m_current;
    if (navigateTo(m_backHistory.pop(), false))
        m_forwardHistory.push(previous);
    updateActions();
}

void MainWindow::goForward()
{
    if (m_forwardHistory.isEmpty())
        return;
    const QString previous = m_current;
    if (navigateTo(m_forwardHistory.pop(), false))
        m_backHistory.push(previous);
    updateActions();
}

void MainWindow::goUp()
{
    QDir dir(m_current);
    if (dir.cdUp())
        navigateTo(dir.absolutePath());
}

void MainWindow::openIndex(const QModelIndex &viewIndex)
{
    const QModelIndex index = toSource(viewIndex);
    const QString path = m_model->filePath(index);
#if defined(ARGONAUT_DBUS)
    if (m_chooser && !m_model->isDir(index)) { // a file: that's the choice
        if (m_chooser->mode == ChooserRequest::Mode::Save)
            m_chooserBar->setName(QFileInfo(path).fileName());
        if (m_chooser->mode != ChooserRequest::Mode::SaveFiles && !m_chooser->directory)
            chooserAccept();
        return;
    }
#endif
    if (m_model->isDir(index))
        navigateTo(path);
    else
        openFile(path);
}

void MainWindow::updateActions()
{
    if (m_background)
        return; // the header shows the active pane
    m_backAct->setEnabled(!m_backHistory.isEmpty());
    m_forwardAct->setEnabled(!m_forwardHistory.isEmpty());
    const bool place = DirectoryModel::isVirtualPath(m_current);
    m_upAct->setEnabled(!place && !m_current.isEmpty() && !QDir(m_current).isRoot());
    m_analyzeAct->setEnabled(!place);
    if (m_analyzeButton)
        m_analyzeButton->setEnabled(!place);
    m_backButton->setEnabled(m_backAct->isEnabled());
    m_forwardButton->setEnabled(m_forwardAct->isEnabled());
#if defined(ARGONAUT_DBUS)
    keepChooserActionsOff();
#endif
}

// ----------------------------------------------------------- view modes ---

void MainWindow::setGridMode(bool grid)
{
    cancelRename();
    const bool wasAnalyzing = m_analyzing;
    m_analyzing = false;
    if (m_analyzeButton)
        m_analyzeButton->setChecked(false);
    m_grid = grid;
    m_model->setTreeEnabled(m_expandFolders && !grid); // the grid shows only the open folder
    QAbstractItemView *next = grid ? static_cast<QAbstractItemView *>(m_iconView) : m_detailView;
    const bool hadFocus = m_view && m_view->hasFocus();
    const QModelIndex current = m_view ? m_view->currentIndex() : QModelIndex();
    if (!grid) {
        // The grid selects only the first column; the table selects rows.
        QItemSelection rows;
        for (const QModelIndex &index : selectedViewIndexes())
            rows.select(index, index.siblingAtColumn(DirectoryModel::ColumnCount - 1));
        m_applyingSelection = true;
        m_view->selectionModel()->select(rows, QItemSelectionModel::ClearAndSelect);
        m_applyingSelection = false;
    }
    m_viewStack->setCurrentWidget(next);
    m_view = next;
    if (current.isValid())
        m_view->scrollTo(current);
    if (hadFocus || wasAnalyzing)
        m_view->setFocus();
    if (wasAnalyzing) {
        updateEditActions();
        updateFloatingBar();
    }
    updateRenameGeometry();
    updateViewChrome();
}

void MainWindow::updateViewChrome()
{
    if (m_background || !m_viewButton)
        return;
    if (m_zoomInAct) { // grid and list have their own zoom limits
        const int level = m_grid ? m_gridZoom : m_listZoom;
        const int last = int((m_grid ? FileItemDelegate::gridSizes() : FileItemDelegate::listSizes()).size()) - 1;
        m_zoomInAct->setEnabled(level < last);
        m_zoomOutAct->setEnabled(level > 0);
    }
    m_viewButton->setIcon(glyphIcon(m_grid ? Glyph::List : Glyph::Grid, m_theme.fg));
    m_viewButton->setToolTip(m_grid ? tr("List view (Ctrl+2)") : tr("Grid view (Ctrl+1)"));
    if (m_gridAct && !m_analyzing)
        (m_grid ? m_gridAct : m_listAct)->setChecked(true);
}

void MainWindow::setAnalyzeMode(bool on)
{
    if (!on) {
        setGridMode(m_grid);
        return;
    }
    if (m_activePane != 0)
        activatePane(0); // Analyze lives in the first pane
    cancelRename();
    setSearchActive(false); // search is about files; Analyze shows folders
    if (m_quickLook)
        m_quickLook->hide();
    m_analyzing = true;
    m_analyzeAct->setChecked(true);
    m_analyzeButton->setChecked(true);
    m_viewStack->setCurrentWidget(m_analyzeView);
    m_usageModel->setRoot(m_current);
    m_analyzeView->expand(m_usageModel->index(0, 0));
    m_analyzeView->setFocus();
    updateEditActions();
    updateFloatingBar();
}

// Toggling files rebuilds the tree's rows: keep the open folders open.
void MainWindow::setAnalyzeShowFiles(bool show)
{
    QStringList expanded;
    std::function<void(const QModelIndex &)> collect = [&](const QModelIndex &parent) {
        for (int r = 0; r < m_usageModel->rowCount(parent); ++r) {
            const QModelIndex child = m_usageModel->index(r, 0, parent);
            if (m_analyzeView->isExpanded(child)) {
                expanded << m_usageModel->filePath(child);
                collect(child);
            }
        }
    };
    collect(QModelIndex());
    const QString current = selectedAnalyzePath();
    m_usageModel->setShowFiles(show);
    for (const QString &path : std::as_const(expanded))
        m_analyzeView->expand(m_usageModel->indexForPath(path));
    if (!current.isEmpty())
        m_analyzeView->setCurrentIndex(m_usageModel->indexForPath(current));
}

QString MainWindow::selectedAnalyzePath() const
{
    const QModelIndex current = m_analyzeView->currentIndex();
    return current.isValid() ? m_usageModel->filePath(current) : QString();
}

void MainWindow::showAnalyzeMenu(const QPoint &pos)
{
    const QModelIndex index = m_analyzeView->indexAt(pos);
    if (index.isValid())
        m_analyzeView->setCurrentIndex(index);
    const QString path = index.isValid() ? m_usageModel->filePath(index) : m_current;
    QMenu *menu = makeMenu();
    menu->setAttribute(Qt::WA_DeleteOnClose);
    const int kind = index.data(DiskUsageModel::KindRole).toInt();
    if (index.isValid() && kind == DiskUsageModel::Node::File) {
        menu->addAction(tr("Open"), this, [this, path] { openFile(path); });
        menu->addAction(tr("Show in Folder"), this, [this, path] {
            navigateTo(QFileInfo(path).absolutePath());
            setGridMode(m_grid);
            selectPaths({path});
        });
    } else if (index.isValid() && !path.isEmpty()) {
        menu->addAction(tr("Open Folder"), this, [this, path] {
            navigateTo(path);
            setGridMode(m_grid);
        });
        menu->addAction(tr("Analyze This Folder"), this, [this, path] { navigateTo(path); })
            ->setEnabled(path != m_current);
        menu->addAction(tr("Open in New Tab"), this, [this, path] { openTab(path, false); });
    }
    if (index.isValid() && !path.isEmpty()) {
        menu->addAction(tr("Copy Location"), this, [path] {
            QApplication::clipboard()->setText(QDir::toNativeSeparators(path));
        });
        if (path != m_current) {
            menu->addSeparator();
            menu->addAction(tr("Move to Trash"), this, [this, path] { m_ops->trash({path}); });
        }
        menu->addSeparator();
    }
    menu->addAction(m_analyzeFilesAct);
    menu->addAction(tr("Scan Again"), this, [this] { m_usageModel->rescan(); });
    menu->popup(m_analyzeView->viewport()->mapToGlobal(pos));
}

void MainWindow::zoom(int step)
{
    if (m_analyzing)
        return;
    const QList<int> &sizes = m_grid ? FileItemDelegate::gridSizes() : FileItemDelegate::listSizes();
    int &level = m_grid ? m_gridZoom : m_listZoom;
    const int defaultLevel = int(sizes.indexOf(m_grid ? FileItemDelegate::DefaultGridIconSize
                                                     : FileItemDelegate::DefaultListIconSize));
    const int next = step == 0 ? defaultLevel : qBound(0, level + step, int(sizes.size()) - 1);
    if (next == level)
        return;
    level = next;
    applyZoom();
}

void MainWindow::applyZoom()
{
    m_gridDelegate->setIconSize(FileItemDelegate::gridSizes().at(m_gridZoom));
    m_listDelegate->setIconSize(FileItemDelegate::listSizes().at(m_listZoom));
    m_iconView->setGridSize(m_gridDelegate->gridCellSize(m_iconView->fontMetrics()));
    // Keep what you were looking at in view.
    const QModelIndex current = m_view->currentIndex();
    if (current.isValid())
        m_view->scrollTo(current);
    updateRenameGeometry();
    updateViewChrome();
}

void MainWindow::setShowHidden(bool show)
{
    m_showHidden = show;
    updateFilter();
    updateFloatingBar();
}

void MainWindow::setSearchActive(bool active)
{
    if (!m_locationStack)
        return;
    if (m_searchAct->isChecked() != active)
        m_searchAct->setChecked(active); // re-enters with the same value
    if (active) {
        if (m_analyzing)
            setGridMode(m_grid); // search finds files: show the file view
        m_locationStack->setCurrentWidget(m_searchEdit->parentWidget());
        m_searchEdit->setFocus(Qt::ShortcutFocusReason);
        m_searchEdit->selectAll();
    } else if (m_locationStack->currentWidget() != m_pathBar) {
        // Filters end with the search.
        m_typeFilters->actions().first()->setChecked(true);
        m_dateFilters->actions().first()->setChecked(true);
        m_filterButton->setText(tr("Filters"));
        m_model->setSearchFilters(DirectoryModel::TypeFilter::Any, DirectoryModel::DateFilter::Any);
        m_searchEdit->clear();
        m_locationStack->setCurrentWidget(m_pathBar);
        m_view->setFocus();
    }
}

// Search = every item in this folder and below whose name has all the
// typed words (DirectoryModel::setSearch). Typing is debounced a little so
// each key doesn't start a new walk; clearing is immediate.
void MainWindow::updateFilter()
{
    m_model->setShowHidden(m_showHidden);
    const QString query = m_searchEdit ? m_searchEdit->text().simplified() : QString();
    if (query.isEmpty() || !m_searchTimer) {
        if (m_searchTimer)
            m_searchTimer->stop();
        m_model->setSearch(query);
    } else {
        m_searchTimer->start();
    }
    if (m_view) // not during setupUi
        updateFloatingBar();
}

QModelIndex MainWindow::toSource(const QModelIndex &viewIndex) const
{
    return viewIndex.model() == m_proxy ? m_proxy->mapToSource(viewIndex) : viewIndex;
}

QModelIndex MainWindow::toView(const QModelIndex &sourceIndex) const
{
    return m_proxy->mapFromSource(sourceIndex);
}

// -------------------------------------------------------- floating bar ---

void MainWindow::showMessage(const QString &text)
{
    m_message = text;
    m_messageTimer->start();
    updateFloatingBar();
}

void MainWindow::updateFloatingBar()
{
    QString text = m_message;
    if (text.isEmpty() && m_analyzing) {
        if (m_usageModel->isScanning()) {
            text = tr("Measuring… %1 in %2 items")
                       .arg(formatSize(m_usageModel->scannedBytes()),
                            QLocale().toString(m_usageModel->scannedItems()));
        } else {
            const QModelIndex current = m_analyzeView->currentIndex();
            if (current.isValid() && current.parent().isValid()) {
                text = tr("“%1”: %2, %3% of “%4”")
                           .arg(current.data().toString(),
                                formatSize(current.data(DiskUsageModel::BytesRole).toLongLong()))
                           .arg(qRound(current.data(DiskUsageModel::FractionRole).toDouble() * 100))
                           .arg(current.parent().data().toString());
            } else if (m_usageModel->rowCount() > 0) {
                text = tr("%1 on disk in %2 items")
                           .arg(formatSize(m_usageModel->rootBytes()),
                                QLocale().toString(m_usageModel->rootItems()));
                if (m_usageModel->rootIncomplete())
                    text += tr(" (some folders couldn't be read)");
            }
        }
    }
    if (text.isEmpty() && !m_analyzing) {
        const QModelIndexList selected = selectedViewIndexes();
        if (selected.size() == 1) {
            const QModelIndex idx = toSource(selected.first());
            text = m_model->isDir(idx)
                       ? tr("“%1” selected").arg(m_model->fileName(idx))
                       : tr("“%1” selected (%2)")
                             .arg(m_model->fileName(idx),
                                  formatSize(m_model->size(idx)));
        } else if (selected.size() > 1) {
            qint64 bytes = 0;
            for (const QModelIndex &viewIdx : selected) {
                const QModelIndex idx = toSource(viewIdx);
                if (!m_model->isDir(idx))
                    bytes += m_model->size(idx);
            }
            text = tr("%n items selected", nullptr, int(selected.size()));
            if (bytes > 0)
                text += QStringLiteral(" (%1)").arg(formatSize(bytes));
        }
    }
    if (text.isEmpty() && !m_analyzing && m_model->isSearch()) {
        const int found = m_model->rowCount();
        if (m_model->isLoading())
            text = found == 0 ? tr("Searching…") : tr("Searching… %1 found").arg(QLocale().toString(found));
        else if (m_model->searchTruncated())
            text = tr("Showing the first %1 results").arg(QLocale().toString(found));
        else
            text = found == 0   ? tr("No results")
                   : found == 1 ? tr("1 result")
                                : tr("%1 results").arg(QLocale().toString(found));
    }

    if (text.isEmpty()) {
        m_floatingBar->hide();
        return;
    }
    m_floatingBar->setText(text);
    m_floatingBar->adjustSize();
    positionFloatingBar();
    m_floatingBar->show();
    m_floatingBar->raise();
}

void MainWindow::positionFloatingBar()
{
    const int margin = 10;
    const int maxWidth = qMax(100, m_viewStack->width() - 2 * margin);
    if (m_floatingBar->width() > maxWidth)
        m_floatingBar->resize(maxWidth, m_floatingBar->height());
    m_floatingBar->move(m_viewStack->width() - m_floatingBar->width() - margin,
                        m_viewStack->height() - m_floatingBar->height() - margin);
}

// ------------------------------------------------- window / decorations ---

void MainWindow::updateWindowState()
{
    const bool framed = m_csd && !isMaximized() && !isFullScreen();
    if (m_root && m_root->layout())
        m_root->layout()->setContentsMargins(framed ? QMargins(1, 1, 1, 1) : QMargins());
    if (m_maxButton) {
        m_maxButton->setIcon(
            glyphIcon(isMaximized() ? Glyph::Restore : Glyph::Maximize, m_theme.fg));
        m_maxButton->setToolTip(isMaximized() ? tr("Restore") : tr("Maximize"));
    }
}

Qt::Edges MainWindow::edgesAt(const QPoint &pos) const
{
    if (!m_csd || isMaximized() || isFullScreen())
        return {};
    Qt::Edges edges;
    if (pos.x() < ResizeMargin)
        edges |= Qt::LeftEdge;
    else if (pos.x() >= width() - ResizeMargin)
        edges |= Qt::RightEdge;
    if (pos.y() < ResizeMargin)
        edges |= Qt::TopEdge;
    else if (pos.y() >= height() - ResizeMargin)
        edges |= Qt::BottomEdge;
    return edges;
}

void MainWindow::updateResizeCursor(Qt::Edges edges)
{
    if (!edges) {
        if (m_resizeCursor) {
            QGuiApplication::restoreOverrideCursor();
            m_resizeCursor = false;
        }
        return;
    }
    Qt::CursorShape shape = Qt::SizeHorCursor;
    if (edges == (Qt::LeftEdge | Qt::TopEdge) || edges == (Qt::RightEdge | Qt::BottomEdge))
        shape = Qt::SizeFDiagCursor;
    else if (edges == (Qt::RightEdge | Qt::TopEdge) || edges == (Qt::LeftEdge | Qt::BottomEdge))
        shape = Qt::SizeBDiagCursor;
    else if (edges & (Qt::TopEdge | Qt::BottomEdge))
        shape = Qt::SizeVerCursor;

    if (m_resizeCursor)
        QGuiApplication::changeOverrideCursor(shape);
    else
        QGuiApplication::setOverrideCursor(shape);
    m_resizeCursor = true;
}

bool MainWindow::eventFilter(QObject *watched, QEvent *event)
{
    // Split view: input in the other pane makes it the active one; drags
    // and resizes there are handled as that pane, without switching to it.
    if (m_paneCount > 1) {
        const int pane = paneOf(watched);
        if (pane >= 0 && pane != m_activePane) {
            switch (event->type()) {
            case QEvent::MouseButtonPress:
            case QEvent::MouseButtonDblClick:
            case QEvent::ContextMenu:
            case QEvent::KeyPress:
            case QEvent::Wheel:
            case QEvent::FocusIn:
                if (!m_background)
                    activatePane(pane);
                break;
            case QEvent::DragEnter:
            case QEvent::DragMove:
            case QEvent::DragLeave:
            case QEvent::Drop:
            case QEvent::Resize: {
                PaneScope scope(this, pane);
                return eventFilter(watched, event);
            }
            default:
                break;
            }
        }
    }

    // Inline rename: Enter commits, Esc cancels, clicking elsewhere commits
    // (or cancels if the name isn't valid). Enter must be consumed here:
    // QLineEdit passes it on, and the view would then open the item.
    if (m_renameEdit && watched == m_renameEdit) {
        if (event->type() == QEvent::KeyPress) {
            const int key = static_cast<QKeyEvent *>(event)->key();
            if (key == Qt::Key_Escape) {
                cancelRename();
                return true;
            }
            if (key == Qt::Key_Return || key == Qt::Key_Enter) {
                commitRename();
                return true;
            }
        }
        if (event->type() == QEvent::FocusOut
            && static_cast<QFocusEvent *>(event)->reason() != Qt::PopupFocusReason) {
            if (!commitRename())
                cancelRename();
        }
        return QMainWindow::eventFilter(watched, event);
    }

    // Ctrl+wheel zooms. Touchpads send many small steps: act on whole notches.
    if (event->type() == QEvent::Wheel && isFileViewport(watched)) {
        auto *wheel = static_cast<QWheelEvent *>(event);
        if (wheel->modifiers() & Qt::ControlModifier) {
            m_wheelZoom += wheel->angleDelta().y();
            while (qAbs(m_wheelZoom) >= 120) {
                zoom(m_wheelZoom > 0 ? +1 : -1);
                m_wheelZoom -= m_wheelZoom > 0 ? 120 : -120;
            }
            return true;
        }
        m_wheelZoom = 0;
    }

    switch (event->type()) {
    case QEvent::DragEnter:
    case QEvent::DragMove:
    case QEvent::DragLeave:
    case QEvent::Drop:
        if (isFileViewport(watched))
            return handleViewDrag(watched == m_iconView->viewport()
                                      ? static_cast<QAbstractItemView *>(m_iconView)
                                      : m_detailView,
                                  event);
        if (m_sidebar && watched == m_sidebar->viewport())
            return handleSidebarDrag(event);
        if (m_tabBar && watched == m_tabBar) {
            // Dropping onto a tab: into that tab's folder.
            if (event->type() == QEvent::DragLeave)
                return true;
            auto *drop = static_cast<QDropEvent *>(event);
            const int index = m_tabBar->tabAt(drop->position().toPoint());
            const QStringList sources = localPaths(drop->mimeData());
            const QString target = index >= 0 ? m_tabs.at(index).path : QString();
            const Qt::DropAction action =
                target.isEmpty() ? Qt::IgnoreAction
                                 : dropActionFor(sources, target, drop->modifiers(), drop->possibleActions());
            if (action == Qt::IgnoreAction) {
                if (event->type() == QEvent::DragEnter && !sources.isEmpty())
                    drop->acceptProposedAction();
                else
                    drop->ignore();
                return true;
            }
            drop->setDropAction(action);
            drop->accept();
            if (event->type() == QEvent::Drop)
                performDrop(sources, target, action);
            return true;
        }
        break;
    default:
        break;
    }

    // Empty menu bar space behaves like the header bars: drag to move,
    // double-click to maximise.
    if (m_csd && m_menuBar && (watched == m_menuBar || watched == m_menuRow)) {
        auto *me = static_cast<QMouseEvent *>(event);
        // On the bar, a menu title is for its menu; the row has none.
        const bool onMenu = watched == m_menuBar && (event->type() == QEvent::MouseButtonPress
                                                     || event->type() == QEvent::MouseButtonDblClick)
                            && m_menuBar->actionAt(me->position().toPoint());
        switch (event->type()) {
        case QEvent::MouseButtonPress:
            m_menuPressed = me->button() == Qt::LeftButton && !onMenu;
            m_menuPressPos = me->globalPosition().toPoint();
            break;
        case QEvent::MouseMove:
            if (m_menuPressed && (me->buttons() & Qt::LeftButton)
                && (me->globalPosition().toPoint() - m_menuPressPos).manhattanLength()
                       >= QApplication::startDragDistance()) {
                m_menuPressed = false;
                windowHandle()->startSystemMove();
                return true;
            }
            break;
        case QEvent::MouseButtonRelease:
            m_menuPressed = false;
            break;
        case QEvent::MouseButtonDblClick:
            if (!onMenu) {
                isMaximized() ? showNormal() : showMaximized();
                return true;
            }
            break;
        default:
            break;
        }
    }

    if (watched == windowHandle() && event->type() == QEvent::MouseButtonPress) {
        const Qt::MouseButton button = static_cast<QMouseEvent *>(event)->button();
        if (button == Qt::BackButton || button == Qt::ForwardButton) {
            button == Qt::BackButton ? goBack() : goForward();
            return true;
        }
    }
    if (m_csd && watched == windowHandle()) {
        switch (event->type()) {
        case QEvent::MouseMove: {
            auto *me = static_cast<QMouseEvent *>(event);
            if (me->buttons() == Qt::NoButton)
                updateResizeCursor(edgesAt(me->position().toPoint()));
            break;
        }
        case QEvent::MouseButtonPress: {
            auto *me = static_cast<QMouseEvent *>(event);
            const Qt::Edges edges = edgesAt(me->position().toPoint());
            if (me->button() == Qt::LeftButton && edges) {
                windowHandle()->startSystemResize(edges);
                return true;
            }
            break;
        }
        case QEvent::Leave:
            updateResizeCursor({});
            break;
        default:
            break;
        }
    } else if (m_sidebar && watched == m_sidebar->viewport()
               && event->type() == QEvent::MouseButtonPress) {
        auto *me = static_cast<QMouseEvent *>(event);
        QListWidgetItem *item = m_sidebar->itemAt(me->position().toPoint());
        if (item && me->button() == Qt::MiddleButton && !item->data(PathRole).toString().isEmpty()) {
            openTab(item->data(PathRole).toString(), false);
            return true;
        }
        if (item && me->button() == Qt::LeftButton && item->data(EjectRole).toBool()
            && ejectRect(m_sidebar->visualItemRect(item)).contains(me->position().toPoint())) {
            ejectDrive(item->data(PathRole).toString(), item->text());
            return true; // don't also open it
        }
    } else if (m_detailView && watched == m_detailView->viewport()
               && (event->type() == QEvent::MouseButtonPress || event->type() == QEvent::MouseButtonDblClick)
               && static_cast<QMouseEvent *>(event)->button() == Qt::LeftButton
               && [&] { // on a folder's ▸ arrow: open or close it, nothing else
                      const QPoint pos = static_cast<QMouseEvent *>(event)->position().toPoint();
                      const QModelIndex index = m_detailView->indexAt(pos).siblingAtColumn(0);
                      const QRect arrow = m_listDelegate->expanderRect(m_detailView->visualRect(index), index);
                      if (!index.isValid() || !arrow.adjusted(-2, -4, 2, 4).contains(pos))
                          return false;
                      if (event->type() == QEvent::MouseButtonPress) {
                          const QString path = m_model->filePath(toSource(index));
                          if (m_model->isExpanded(path))
                              m_model->collapse(path);
                          else
                              m_model->expand(path);
                      }
                      return true;
                  }()) {
        return true;
    } else if (isFileViewport(watched) && event->type() == QEvent::MouseButtonPress
               && static_cast<QMouseEvent *>(event)->button() == Qt::MiddleButton) {
        auto *view = static_cast<QAbstractItemView *>(static_cast<QWidget *>(watched)->parentWidget());
        const QModelIndex source =
            toSource(view->indexAt(static_cast<QMouseEvent *>(event)->position().toPoint()));
        if (source.isValid() && m_model->isDir(source))
            openTab(m_model->filePath(source), false);
        return true;
    } else if (m_tabBar && watched == m_tabBar && event->type() == QEvent::MouseButtonRelease
               && static_cast<QMouseEvent *>(event)->button() == Qt::MiddleButton) {
        const int index = m_tabBar->tabAt(static_cast<QMouseEvent *>(event)->position().toPoint());
        if (index >= 0)
            closeTab(index);
        return true;
    } else if (m_view
               && ((isFileViewport(watched) && event->type() == QEvent::MouseButtonPress)
                   || ((watched == m_iconView || watched == m_detailView)
                       && event->type() == QEvent::KeyPress))) {
        m_pendingSelection.clear();
        m_pendingRename.clear();
        if (event->type() == QEvent::KeyPress && typeAheadKey(static_cast<QKeyEvent *>(event)))
            return true;
        if (event->type() == QEvent::KeyPress) {
            auto *key = static_cast<QKeyEvent *>(event);
            if (key->key() == Qt::Key_Space && key->modifiers() == Qt::NoModifier) {
                toggleQuickLook();
                return true;
            }
            if (key->key() == Qt::Key_Escape && quickLookVisible()) {
                m_quickLook->hide();
                return true;
            }
            if (watched == m_detailView && handleTreeKey(key))
                return true;
        }
    }
    if (m_renameEdit && isFileViewport(watched) && event->type() == QEvent::Paint)
        updateRenameGeometry(); // item positions are final when the view paints
    if (m_viewStack && watched == m_viewStack && event->type() == QEvent::Resize)
        fitListColumns();
    if (m_viewStack && watched == m_viewStack && event->type() == QEvent::Resize) {
        if (m_floatingBar->isVisible())
            positionFloatingBar();
        if (m_opsPanel->isVisible()) {
            m_opsPanel->reposition();
            m_toast->setAvoidRect(m_opsPanel->geometry());
        }
        if (m_toast->isVisible())
            m_toast->reposition();
        updateRenameGeometry();
    } else if (watched == m_searchEdit && event->type() == QEvent::KeyPress) {
        if (static_cast<QKeyEvent *>(event)->key() == Qt::Key_Escape) {
            setSearchActive(false);
            return true;
        }
    }
    return QMainWindow::eventFilter(watched, event);
}

// ------------------------------------------------------ file operations ---

QStringList MainWindow::selectedPaths() const
{
    QStringList paths;
    for (const QModelIndex &index : selectedViewIndexes())
        paths.append(m_model->filePath(toSource(index)));
    return paths;
}

void MainWindow::openSelection()
{
#if defined(ARGONAUT_DBUS)
    if (m_chooser) {
        const QStringList paths = selectedPaths();
        if (paths.size() == 1 && QFileInfo(paths.first()).isDir() && !m_chooser->directory)
            navigateTo(paths.first());
        else
            chooserAccept();
        return;
    }
#endif
    const QModelIndexList selected = selectedViewIndexes();
    if (selected.size() == 1) {
        openIndex(selected.first());
        return;
    }
    for (const QModelIndex &index : selected) {
        const QModelIndex source = toSource(index);
        if (!m_model->isDir(source))
            openFile(m_model->filePath(source));
    }
}

void MainWindow::copySelection(bool cut)
{
    const QStringList paths = selectedPaths();
    if (paths.isEmpty())
        return;
    FileOperations::setClipboardFiles(paths, cut);
    const int n = int(paths.size());
    if (cut)
        showMessage(n == 1 ? tr("“%1” will be moved when you paste").arg(QFileInfo(paths.first()).fileName())
                           : tr("%1 items will be moved when you paste").arg(n));
    else
        showMessage(n == 1 ? tr("“%1” copied").arg(QFileInfo(paths.first()).fileName())
                           : tr("%1 items copied").arg(n));
}

void MainWindow::paste()
{
    QStringList paths;
    bool cut = false;
    if (!FileOperations::clipboardFiles(&paths, &cut)) {
        showMessage(tr("There is nothing to paste"));
        return;
    }
    if (cut) {
        m_ops->move(paths, m_current);
        QApplication::clipboard()->clear(); // a cut is pasted only once
    } else {
        m_ops->copy(paths, m_current);
    }
}

void MainWindow::trashSelection()
{
    if (m_model->isTrash()) {
        deleteSelection(); // already in the Trash: the only step left is forever
        return;
    }
    const QStringList paths = selectedPaths();
    if (paths.isEmpty())
        return;
    // Servers have no Trash (GVfs folders): offer to delete instead.
    for (const QString &p : paths) {
        if (!Remote::mountOf(p).localPath.isEmpty()) {
            QMessageBox box(QMessageBox::Warning, tr("No Trash on This Server"),
                            paths.size() == 1 ? tr("“%1” can't be moved to the Trash.").arg(QFileInfo(paths.first()).fileName())
                                              : tr("These %1 items can't be moved to the Trash.").arg(paths.size()),
                            QMessageBox::NoButton, this);
            box.setInformativeText(tr("Files on a server can only be deleted forever."));
            QPushButton *cancel = box.addButton(QMessageBox::Cancel);
            QPushButton *remove = box.addButton(tr("Delete Permanently"), QMessageBox::DestructiveRole);
            box.setDefaultButton(cancel);
            box.exec();
            if (box.clickedButton() == remove)
                m_ops->deletePermanently(paths);
            return;
        }
    }
    m_ops->trash(paths);
}

bool MainWindow::confirmPermanentDelete(const QStringList &paths, bool wholeTrash)
{
    QString question;
    if (wholeTrash)
        question = tr("Empty all items from the Trash?");
    else if (paths.size() == 1)
        question = tr("Permanently delete “%1”?").arg(QFileInfo(paths.first()).fileName());
    else
        question = tr("Permanently delete %1 selected items?").arg(paths.size());
    QMessageBox box(QMessageBox::Warning, wholeTrash ? tr("Empty Trash") : tr("Delete Permanently"),
                    question, QMessageBox::NoButton, this);
    box.setInformativeText(wholeTrash ? tr("All items in the Trash will be deleted forever.")
                                      : tr("If you delete an item, it will be gone forever."));
    QPushButton *cancel = box.addButton(QMessageBox::Cancel);
    QPushButton *remove =
        box.addButton(wholeTrash ? tr("Empty Trash") : tr("Delete"), QMessageBox::DestructiveRole);
    box.setDefaultButton(cancel);
    box.exec();
    return box.clickedButton() == remove;
}

void MainWindow::deleteSelection()
{
    const QStringList paths = selectedPaths();
    if (!paths.isEmpty() && confirmPermanentDelete(paths, false))
        m_ops->deletePermanently(paths);
}

void MainWindow::restoreSelection()
{
    if (!m_model->isTrash())
        return;
    const QStringList paths = selectedPaths();
    if (!paths.isEmpty())
        m_ops->restore(paths);
}

void MainWindow::emptyTrash()
{
    bool empty = true;
    for (const QString &dir : Trash::filesDirs()) {
        if (!QDir(dir).isEmpty(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System))
            empty = false;
    }
    if (empty) {
        showMessage(tr("The Trash is already empty"));
        return;
    }
    if (confirmPermanentDelete({}, true))
        m_ops->emptyTrash();
}

// A drop into the Trash (its sidebar entry or the Trash view) trashes.
void MainWindow::performDrop(const QStringList &sources, const QString &targetDir,
                             Qt::DropAction action)
{
    if (Trash::isFilesDir(targetDir))
        m_ops->trash(sources);
    else if (action == Qt::MoveAction)
        m_ops->move(sources, targetDir);
    else
        m_ops->copy(sources, targetDir);
}

void MainWindow::updateEditActions()
{
    if (m_background)
        return; // actions act on the active pane
    const bool hasSelection = !m_analyzing && m_view && m_view->selectionModel()->hasSelection();
    const bool inTrash = m_model && m_model->isTrash();
    // Items in a folder you can't change can be copied, not moved or trashed.
    const bool changeable = hasSelection && (inTrash || canChangeSelection());
    for (QAction *action : {m_openAct, m_copyAct})
        action->setEnabled(hasSelection);
    for (QAction *action : {m_cutAct, m_trashAct})
        action->setEnabled(changeable);
    const bool writable = !m_analyzing && !inTrash && !m_current.isEmpty() && QFileInfo(m_current).isWritable();
    m_deleteAct->setEnabled(changeable);
    m_restoreAct->setEnabled(inTrash && hasSelection);
    if (m_trashRestoreButton)
        m_trashRestoreButton->setText(tr("Restore")); // setDefaultAction resets it
    m_emptyTrashAct->setEnabled(Trash::isSupported());
    m_restoreAllAct->setEnabled(inTrash && m_model->rowCount() > 0);
    m_pasteAct->setEnabled(writable && FileOperations::clipboardHasFiles());
    m_newFolderAct->setEnabled(writable);
    m_newFileAct->setEnabled(writable);
    m_renameAct->setEnabled(!inTrash && changeable);
    m_newDocumentMenu->menuAction()->setEnabled(writable);
    m_organizeAct->setEnabled(writable && !m_model->isSearch() && !m_model->isVirtual());
    const bool fileView = !m_analyzing && m_view && m_model->rowCount() > 0;
    m_selectMatchingAct->setEnabled(fileView);
    m_invertSelectionAct->setEnabled(fileView);
    const bool archives = FileJob::archivesSupported();
    m_compressAct->setVisible(archives);
    m_compressAct->setEnabled(archives && writable && hasSelection);
    const bool extractable = archives && !inTrash && hasSelection && !selectedArchives().isEmpty();
    m_extractHereAct->setVisible(archives);
    m_extractToAct->setVisible(archives);
    m_extractHereAct->setEnabled(extractable && writable);
    m_extractToAct->setEnabled(extractable);
    m_undoAct->setEnabled(m_ops->canUndo());
    m_undoAct->setText(m_ops->undoText());
    m_redoAct->setEnabled(m_ops->canRedo());
    m_redoAct->setText(m_ops->redoText());
    const bool toOther = hasSelection && !inTrash && !otherPaneFolder().isEmpty();
    m_copyToOtherAct->setEnabled(toOther);
    m_moveToOtherAct->setEnabled(toOther && changeable);
#if defined(ARGONAUT_DBUS)
    keepChooserActionsOff();
#endif
}

void MainWindow::showContextMenu(const QPoint &pos)
{
    const QModelIndex index = m_view->indexAt(pos);
    QItemSelectionModel *selection = m_view->selectionModel();
    if (index.isValid() && !selection->isSelected(index))
        selection->setCurrentIndex(index, QItemSelectionModel::ClearAndSelect);
    else if (!index.isValid())
        selection->clearSelection();
    updateEditActions();

    QMenu *menu = makeMenu();
    menu->setAttribute(Qt::WA_DeleteOnClose);
    m_contextFolder = m_current;
    if (index.isValid() && m_model->isDir(toSource(index)))
        m_contextFolder = m_model->filePath(toSource(index));
    if (index.isValid() && m_model->isTrash()) {
        menu->addAction(m_restoreAct);
        menu->addAction(m_deleteAct);
        menu->addSeparator();
        menu->addAction(m_openAct);
        menu->addAction(m_copyAct);
    } else if (!index.isValid() && m_model->isTrash()) {
        menu->addAction(m_restoreAllAct);
        menu->addAction(m_emptyTrashAct);
        menu->addAction(m_hiddenAct);
    } else if (index.isValid()) {
        menu->addAction(m_openAct);
        if (selectedViewIndexes().size() == 1)
            menu->addAction(m_previewAct);
        const auto verdict = ContentType::Verdict(index.data(DirectoryModel::VerdictRole).toInt());
        if ((verdict == ContentType::Verdict::Mismatch || verdict == ContentType::Verdict::Dangerous)
            && selectedViewIndexes().size() == 1) {
            const QString path = m_model->filePath(toSource(index));
            const QString fixed = ContentType::fixedName(QFileInfo(path).fileName(),
                                                         index.data(DirectoryModel::ContentMimeRole).toString());
            if (!fixed.isEmpty())
                menu->addAction(tr("Rename to “%1”").arg(fixed), this, [this, path] { fixExtension(path); });
        }
        if (!m_model->isDir(toSource(index)) && OpenWith::isSupported()) {
            QStringList files;
            for (const QString &path : selectedPaths()) {
                if (!QFileInfo(path).isDir())
                    files << path;
            }
            if (!files.isEmpty())
                menu->addMenu(openWithMenu(files));
        }
#if defined(Q_OS_LINUX)
        {
            // Send by Email… (files only) and Set as Background (one image).
            QStringList files;
            for (const QString &path : selectedPaths()) {
                if (!QFileInfo(path).isDir())
                    files << path;
            }
            if (!files.isEmpty() && files.size() == selectedPaths().size() && !m_model->isTrash())
                menu->addAction(tr("Send by Email…"), this, [this, files] { sendByEmail(files); });
            static const QMimeDatabase mimeDb;
            if (files.size() == 1 && selectedPaths().size() == 1 && !m_model->isTrash()
                && mimeDb.mimeTypeForFile(files.first()).name().startsWith(QLatin1String("image/"))
                && !qEnvironmentVariable("XDG_CURRENT_DESKTOP").isEmpty())
                menu->addAction(tr("Set as Background"), this, [this, image = files.first()] { setAsBackground(image); });
        }
#endif
        if ((m_model->isSearch() || m_model->isVirtual()) && selectedViewIndexes().size() == 1)
            menu->addAction(m_openLocationAct);
        if (m_model->isDir(toSource(index)))
            menu->addAction(m_openInNewTabAct);
        if (m_openTerminalHereAct && m_model->isDir(toSource(index)))
            menu->addAction(m_openTerminalHereAct);
        if (FileJob::archivesSupported()) {
            menu->addSeparator();
            if (m_extractHereAct->isEnabled() || m_extractToAct->isEnabled()) {
                menu->addAction(m_extractHereAct);
                menu->addAction(m_extractToAct);
            }
            menu->addAction(m_compressAct);
        }
        menu->addSeparator();
        bool allStarred = true;
        for (const QString &p : selectedPaths())
            allStarred = allStarred && Starred::instance().isStarred(p);
        m_starAct->setText(allStarred ? tr("Unstar") : tr("Star"));
        menu->addAction(m_starAct);
        if (m_model->place() == DirectoryModel::Place::Recent)
            menu->addAction(m_removeRecentAct);
        menu->addSeparator();
        menu->addAction(m_renameAct);
        menu->addSeparator();
        menu->addAction(m_cutAct);
        menu->addAction(m_copyAct);
        if (m_split) {
            menu->addAction(m_copyToOtherAct);
            menu->addAction(m_moveToOtherAct);
        }
        if (QMenu *scripts = scriptsMenu(selectedPaths())) {
            menu->addSeparator();
            menu->addMenu(scripts);
        }
        menu->addSeparator();
        menu->addAction(m_trashAct);
        menu->addAction(m_deleteAct);
        menu->addSeparator();
        menu->addAction(m_propertiesAct);
    } else {
        menu->addAction(m_newFolderAct);
        menu->addAction(m_newFileAct);
        menu->addMenu(m_newDocumentMenu);
        menu->addSeparator();
        if (m_openTerminalHereAct) {
            menu->addAction(m_openTerminalHereAct);
            menu->addSeparator();
        }
        menu->addAction(m_pasteAct);
        menu->addAction(m_undoAct);
        menu->addAction(m_redoAct);
        menu->addSeparator();
        menu->addAction(m_copyLocationAct);
        menu->addAction(m_hiddenAct);
        menu->addSeparator();
        menu->addAction(m_organizeAct);
        if (QMenu *scripts = scriptsMenu({})) {
            menu->addSeparator();
            menu->addMenu(scripts);
        }
        menu->addSeparator();
        menu->addAction(m_propertiesAct);
    }
    menu->popup(m_view->viewport()->mapToGlobal(pos));
}

QStringList MainWindow::selectedArchives() const
{
    QStringList archives;
    for (const QModelIndex &index : selectedViewIndexes()) {
        ContentType::Result r;
        r.nameMime = index.data(DirectoryModel::NameMimeRole).toString();
        r.contentMime = index.data(DirectoryModel::ContentMimeRole).toString();
        r.verdict = ContentType::Verdict(index.data(DirectoryModel::VerdictRole).toInt());
        // The real type: a ".zip" that is really 7z still extracts; a
        // program named ".zip" doesn't.
        if (r.verdict == ContentType::Verdict::Dangerous || m_model->isDir(toSource(index))
            || !FileOperations::isArchive(ContentType::effectiveMime(r)))
            return {};
        archives << m_model->filePath(toSource(index));
    }
    return archives;
}

void MainWindow::compressSelection()
{
    const QStringList paths = selectedPaths();
    if (paths.isEmpty() || !FileJob::archivesSupported())
        return;
    CompressDialog dialog(paths, m_current, this);
    if (dialog.exec() == QDialog::Accepted)
        m_ops->compress(paths, m_current, dialog.archiveName(), dialog.format());
}

void MainWindow::onJobFinished(FileJob *job)
{
    refreshGitAll();
    const QList<TransferResult> results = job->results();
    const QStringList errors = job->errors();
    const int done = int(results.size());
    const bool userAction = job->property(FileOperations::UserActionProperty).toBool();
    // Results can be in sub-folders, which aren't watched: search again.
    for (int i = 0; i < m_paneCount; ++i) {
        PaneScope scope(this, i);
        if (m_model->isSearch())
            m_model->refresh();
    }
    // Keep the Analyze tree right without measuring everything again: what
    // went away leaves it (its size leaves every folder above), and what
    // arrived (copies, moves, extracted files, a trash undone) is measured
    // and added. Changes made by other programs: "Scan Again".
    for (const TransferResult &r : results) {
        switch (job->kind()) {
        case FileJob::Kind::Trash: // it still exists (in the Trash): measure it there
            m_usageModel->removePath(r.source, DiskUsageModel::sizeOnDisk(r.target));
            break;
        case FileJob::Kind::Delete:
            m_usageModel->removePath(r.source);
            break;
        case FileJob::Kind::Move: // also moves its star (and those of what's inside)
            m_usageModel->removePath(r.source, DiskUsageModel::sizeOnDisk(r.target));
            m_usageModel->addPath(r.target);
            Starred::instance().renamed(r.source, r.target);
            break;
        case FileJob::Kind::Copy:
        case FileJob::Kind::Restore:
        case FileJob::Kind::Extract:
        case FileJob::Kind::Compress:
            m_usageModel->addPath(r.target);
            break;
        }
    }
    if (m_analyzing && m_usageModel->rowCount() == 0)
        m_usageModel->setRoot(m_current); // the shown folder itself went: measure again

    if (job->isCanceled()) {
        showMessage(tr("Canceled"));
    } else if (!userAction) {
        if (done > 0)
            showMessage(job->property(FileOperations::HistoryProperty).toString() == QLatin1String("redo")
                            ? tr("Redone")
                            : tr("Undone"));
    } else if (job->kind() == FileJob::Kind::Trash && done > 0) {
        const QString text =
            done == 1 ? tr("“%1” moved to the Trash").arg(QFileInfo(results.first().source).fileName())
                      : tr("%n items moved to the Trash", nullptr, done);
        m_toast->showMessage(text, tr("Undo"), [this] { m_ops->undo(); });
    } else if (job->kind() == FileJob::Kind::Delete && done > 0) {
        showMessage(done == 1 ? tr("1 item deleted") : tr("%1 items deleted").arg(done));
    } else if (job->kind() == FileJob::Kind::Extract && done > 0) {
        showMessage(done == 1 ? tr("Extracted “%1”").arg(QFileInfo(results.first().target).fileName())
                              : tr("%1 archives extracted").arg(done));
    } else if (job->kind() == FileJob::Kind::Compress && done > 0) {
        showMessage(tr("Created “%1”").arg(QFileInfo(results.first().target).fileName()));
    } else if (job->kind() == FileJob::Kind::Restore && done > 0) {
        const QFileInfo where(results.first().target);
        showMessage(done == 1 ? tr("“%1” restored to “%2”")
                                    .arg(where.fileName(), QDir::toNativeSeparators(where.absolutePath()))
                              : tr("%1 items restored").arg(done));
    } else if (!job->flushedDrives().isEmpty() && done > 0) {
        showMessage(tr("Done — everything is written to “%1”")
                        .arg(job->flushedDrives().join(QStringLiteral("”, “"))));
    } else if (done > 0) {
        const bool copied = job->kind() == FileJob::Kind::Copy;
        if (done == 1)
            showMessage(copied ? tr("1 item copied") : tr("1 item moved"));
        else
            showMessage(copied ? tr("%1 items copied").arg(done) : tr("%1 items moved").arg(done));
    }

    // Highlight what was pasted (or put back by an undo) in each pane
    // showing that folder: with F5/F6 that's the other pane.
    if (job->kind() != FileJob::Kind::Trash) {
        for (int i = 0; i < m_paneCount; ++i) {
            PaneScope scope(this, i);
            QStringList here;
            for (const TransferResult &r : results) {
                if (QFileInfo(r.target).absolutePath() == m_current)
                    here.append(r.target);
            }
            if (!here.isEmpty())
                selectPaths(here);
        }
    }

    if (!errors.isEmpty()) {
        QString text = errors.mid(0, 10).join('\n');
        if (errors.size() > 10)
            text += '\n' + tr("…and %n more", nullptr, int(errors.size() - 10));
        QMessageBox::warning(this, tr("Some items couldn't be processed"), text);
    }
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    if (!m_ops->activeJobs().isEmpty()) {
        QMessageBox box(QMessageBox::Question, tr("Cancel file operations?"),
                        tr("Files are still being copied or moved. Closing now will cancel "
                           "them; items already finished are kept."),
                        QMessageBox::NoButton, this);
        QPushButton *keep = box.addButton(tr("Keep Working"), QMessageBox::RejectRole);
        box.addButton(tr("Cancel and Close"), QMessageBox::AcceptRole);
        box.setDefaultButton(keep);
        box.exec();
        if (box.clickedButton() == keep) {
            event->ignore();
            return;
        }
        m_ops->cancelAll();
    }
#if defined(ARGONAUT_DBUS)
    if (m_chooser) { // an Open/Save window: no answer yet means Cancel
        finishChooser(FileChooserPortal::Cancelled);
        QMainWindow::closeEvent(event);
        return; // the file manager's own settings stay as they were
    }
#endif
    if (m_activePane != 0)
        activatePane(0); // settings describe the first pane
    saveSettings();
    QMainWindow::closeEvent(event);
}

void MainWindow::selectPaths(const QStringList &paths)
{
    m_pendingSelection = paths;
    m_pendingSince.start();
    applyPendingSelection();
}

void MainWindow::applyPendingSelection()
{
    if (m_pendingSelection.isEmpty())
        return;
    if (m_pendingSince.elapsed() > 5000) { // give up quietly
        m_pendingSelection.clear();
        return;
    }

    QItemSelection selection;
    QModelIndex first;
    int found = 0;
    for (const QString &path : std::as_const(m_pendingSelection)) {
        const QModelIndex index = toView(m_model->indexForPath(path));
        if (!index.isValid())
            continue;
        selection.select(index, index.siblingAtColumn(DirectoryModel::ColumnCount - 1));
        if (!first.isValid())
            first = index;
        ++found;
    }
    if (!first.isValid())
        return;

    m_applyingSelection = true;
    QItemSelectionModel *model = m_view->selectionModel();
    model->select(selection, QItemSelectionModel::ClearAndSelect);
    model->setCurrentIndex(first, QItemSelectionModel::NoUpdate);
    m_view->scrollTo(first);
    m_applyingSelection = false;

    if (found == m_pendingSelection.size())
        m_pendingSelection.clear();

    if (!m_pendingRename.isEmpty()) {
        const QModelIndex index = toView(m_model->indexForPath(m_pendingRename));
        if (index.isValid()) {
            m_pendingRename.clear();
            startRename(index);
        }
    }
}

// ------------------------------------------------------------- menu bar ---

QWidget *MainWindow::buildMenuBar()
{
    m_menuBar = new QMenuBar(this);
    m_menuBar->setObjectName("menuBar");
    m_menuBar->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    auto addMenu = [this](const QString &title) {
        QMenu *menu = makeMenu();
        menu->setTitle(title);
        m_menuBar->addMenu(menu);
        return menu;
    };

    QMenu *file = addMenu(tr("&File"));
    file->addAction(m_newFolderAct);
    file->addAction(m_newFileAct);
    file->addMenu(m_newDocumentMenu);
    file->addSeparator();
    file->addAction(m_organizeAct);
    file->addSeparator();
    file->addAction(m_openAct);
    file->addAction(m_propertiesAct);
    file->addSeparator();
    if (FileJob::archivesSupported()) {
        file->addAction(m_compressAct);
        file->addAction(m_extractHereAct);
        file->addAction(m_extractToAct);
        file->addSeparator();
    }
    if (Trash::isSupported()) {
        file->addAction(m_emptyTrashAct);
        file->addSeparator();
    }
    file->addAction(m_newTabAct);
    file->addAction(m_reopenTabAct);
    file->addSeparator();
    file->addAction(m_closeAct);
    file->addAction(m_closeWindowAct);
    file->addAction(m_quitAct);

    QMenu *edit = addMenu(tr("&Edit"));
    edit->addAction(m_undoAct);
    edit->addAction(m_redoAct);
    edit->addSeparator();
    edit->addAction(m_copyToOtherAct);
    edit->addAction(m_moveToOtherAct);
    edit->addSeparator();
    edit->addAction(m_cutAct);
    edit->addAction(m_copyAct);
    edit->addAction(m_pasteAct);
    edit->addSeparator();
    edit->addAction(m_selectAllAct);
    edit->addAction(m_selectMatchingAct);
    edit->addAction(m_invertSelectionAct);
    edit->addSeparator();
    edit->addAction(m_renameAct);
    edit->addAction(m_trashAct);
    edit->addAction(m_deleteAct);
    if (Trash::isSupported())
        edit->addAction(m_restoreAct);
    edit->addSeparator();
    edit->addAction(m_copyLocationAct);
    edit->addSeparator();
    edit->addAction(m_prefsAct);

    QMenu *view = addMenu(tr("&View"));
    view->addAction(m_gridAct);
    view->addAction(m_listAct);
    view->addAction(m_analyzeAct);
    view->addAction(m_analyzeFilesAct);
    view->addSeparator();
    view->addAction(m_splitAct);
    view->addSeparator();
    view->addAction(m_searchContentsAct);
    view->addAction(m_paletteAct);
    view->addSeparator();
    view->addAction(m_zoomInAct);
    view->addAction(m_zoomOutAct);
    view->addAction(m_zoomResetAct);
    view->addSeparator();
    QMenu *sortMenu = makeMenu();
    sortMenu->setTitle(tr("Sort Files"));
    sortMenu->addActions(m_sortColumnActs);
    sortMenu->addSeparator();
    sortMenu->addAction(m_sortAscAct);
    sortMenu->addAction(m_sortDescAct);
    view->addMenu(sortMenu);
    view->addSeparator();
    view->addAction(m_hiddenAct);
    view->addAction(m_sidebarAct);
    if (m_terminalAct)
        view->addAction(m_terminalAct);

    QMenu *go = addMenu(tr("&Go"));
    go->addAction(m_backAct);
    go->addAction(m_forwardAct);
    go->addAction(m_upAct);
    go->addAction(m_homeAct);
    go->addSeparator();
    go->addAction(m_bookmarkAct);
    go->addAction(m_connectAct);
    go->addSeparator();
    go->addAction(m_locationAct);
    go->addAction(m_searchAct);

    QMenu *help = addMenu(tr("&Help"));
    help->addAction(m_guideAct);
    help->addAction(m_shortcutsAct);
    help->addSeparator();
    help->addAction(m_aboutAct);

    m_menuBar->installEventFilter(this);
    return m_menuBar;
}

// ----------------------------------------------------- new items, rename ---

void MainWindow::createNewItem(bool folder)
{
    if (m_current.isEmpty() || !QFileInfo(m_current).isWritable()) {
        showMessage(tr("You can't create items in this folder"));
        return;
    }
    const QDir dir(m_current);
    const QString base = folder ? tr("New Folder") : tr("New File");
    QString path = dir.filePath(base);
    for (int i = 2; QFileInfo(path).exists() || QFileInfo(path).isSymLink(); ++i)
        path = dir.filePath(base + ' ' + QString::number(i));

    bool ok = false;
    if (folder) {
        ok = dir.mkdir(QFileInfo(path).fileName());
    } else {
        QFile file(path);
        ok = file.open(QIODevice::WriteOnly | QIODevice::NewOnly);
    }
    if (!ok) {
        showMessage(folder ? tr("Couldn't create the folder") : tr("Couldn't create the file"));
        return;
    }
    // Undo trashes it (same as undoing a copy).
    m_ops->recordUndo(FileJob::Kind::Copy, {{QString(), path}}, folder ? tr("New Folder") : tr("New File"));
    m_pendingRename = path;
    selectPaths({path});
}

void MainWindow::renameSelection()
{
    const QModelIndexList selected = selectedViewIndexes();
    if (selected.size() == 1)
        startRename(selected.first());
    else if (selected.size() > 1)
        batchRename(selectedPaths());
}

void MainWindow::batchRename(const QStringList &paths)
{
    cancelRename();
    BatchRenameDialog dialog(paths, this);
    if (dialog.exec() != QDialog::Accepted)
        return;
    const QList<Naming::RenameItem> items = dialog.items();
    QString error;
    const QList<TransferResult> steps = Naming::applyBatch(items, &error);
    if (steps.isEmpty()) {
        if (!error.isEmpty())
            showMessage(error + tr(" — nothing was renamed"));
        return;
    }
    for (const TransferResult &step : steps)
        Starred::instance().renamed(step.source, step.target);
    // Undo runs the steps backwards, so chains and swaps come back too.
    m_ops->recordUndo(FileJob::Kind::Move, steps, tr("Rename"));
    QStringList renamed;
    for (const Naming::RenameItem &item : items)
        renamed << QFileInfo(item.path).absolutePath() + '/' + item.newName;
    selectPaths(renamed);
    showMessage(renamed.size() == 1 ? tr("Renamed 1 item") : tr("Renamed %1 items").arg(renamed.size()));
}

// ------------------------------------------------------- selection tools ---

void MainWindow::selectMatching()
{
    if (m_analyzing || !m_view)
        return;
    bool ok = false;
    const QString pattern = QInputDialog::getText(
        this, tr("Select Items Matching"),
        tr("Pattern, e.g. *.jpg or IMG_20??* (text without * or ? matches names containing it):"),
        QLineEdit::Normal, m_lastPattern, &ok);
    if (!ok || pattern.trimmed().isEmpty())
        return;
    m_lastPattern = pattern.trimmed();

    QItemSelection selection;
    QModelIndex first;
    const int rows = m_proxy->rowCount();
    for (int row = 0; row < rows; ++row) {
        const QModelIndex index = m_proxy->index(row, 0);
        if (!Naming::matchesPattern(m_model->fileName(toSource(index)), m_lastPattern))
            continue;
        selection.select(index, index.siblingAtColumn(DirectoryModel::ColumnCount - 1));
        if (!first.isValid())
            first = index;
    }
    QItemSelectionModel *model = m_view->selectionModel();
    model->select(selection, QItemSelectionModel::ClearAndSelect);
    if (!first.isValid()) {
        showMessage(tr("No items match “%1”").arg(m_lastPattern));
        return;
    }
    model->setCurrentIndex(first, QItemSelectionModel::NoUpdate);
    m_view->scrollTo(first);
    m_view->setFocus();
}

void MainWindow::invertSelection()
{
    if (m_analyzing || !m_view)
        return;
    const int rows = m_proxy->rowCount();
    if (rows == 0)
        return;
    QItemSelection all;
    all.select(m_proxy->index(0, 0), m_proxy->index(rows - 1, DirectoryModel::ColumnCount - 1));
    m_view->selectionModel()->select(all, QItemSelectionModel::Toggle);
    m_view->setFocus();
}

// ------------------------------------------------------- new from template ---

void MainWindow::fillNewDocumentMenu()
{
    m_newDocumentMenu->clear();
    for (QMenu *old : m_newDocumentMenu->findChildren<QMenu *>(Qt::FindDirectChildrenOnly))
        old->deleteLater(); // sub-menus from last time
    const QString folder = Templates::folder();
    const QList<Templates::Entry> entries = Templates::list(folder);
    std::function<void(QMenu *, const QList<Templates::Entry> &)> fill =
        [this, &fill](QMenu *menu, const QList<Templates::Entry> &list) {
            QMimeDatabase mimes;
            QFileIconProvider icons;
            for (const Templates::Entry &entry : list) {
                if (entry.isFolder) {
                    // Owned by the top menu, so the next fill deletes it.
                    auto *sub = new QMenu(m_newDocumentMenu);
                    sub->setWindowFlags(sub->windowFlags() | Qt::FramelessWindowHint
                                        | Qt::NoDropShadowWindowHint);
                    sub->setAttribute(Qt::WA_TranslucentBackground);
                    sub->setTitle(entry.name);
                    fill(sub, entry.children);
                    menu->addMenu(sub);
                    continue;
                }
                QAction *action = menu->addAction(entry.name, this, [this, path = entry.path] {
                    createFromTemplate(path);
                });
                action->setIcon(icons.icon(QFileInfo(entry.path)));
            }
        };
    fill(m_newDocumentMenu, entries);
    if (entries.isEmpty()) {
        QAction *none = m_newDocumentMenu->addAction(tr("No Templates"));
        none->setEnabled(false);
    }
    m_newDocumentMenu->addSeparator();
    if (!folder.isEmpty()) {
        m_newDocumentMenu->addAction(tr("Open Templates Folder"), this, [this, folder] { navigateTo(folder); });
    } else {
        // Nothing to open yet: say where documents to offer here belong.
        QAction *hint = m_newDocumentMenu->addAction(
            tr("Put files in %1 to see them here").arg(QDir::toNativeSeparators(QDir::homePath() + QStringLiteral("/Templates"))));
        hint->setEnabled(false);
    }
}

void MainWindow::createFromTemplate(const QString &templatePath)
{
    if (m_current.isEmpty() || !QFileInfo(m_current).isWritable()) {
        showMessage(tr("You can't create items in this folder"));
        return;
    }
    QString error;
    const QString path = Templates::create(templatePath, m_current, &error);
    if (path.isEmpty()) {
        showMessage(error);
        return;
    }
    // Undo trashes it (same as undoing a copy).
    m_ops->recordUndo(FileJob::Kind::Copy, {{templatePath, path}}, tr("New Document"));
    m_pendingRename = path;
    selectPaths({path});
}

void MainWindow::startRename(const QModelIndex &viewIndex)
{
    cancelRename();
    const QModelIndex source = toSource(viewIndex);
    if (!source.isValid())
        return;
    m_renamePath = m_model->filePath(source);
    m_view->scrollTo(viewIndex);

    m_renameEdit = new QLineEdit(m_view->viewport());
    m_renameEdit->setObjectName("renameEdit");
    m_renameEdit->setText(m_model->fileName(source));
    m_renameEdit->setAlignment(m_grid ? Qt::AlignCenter : Qt::AlignLeft | Qt::AlignVCenter);
    m_renameEdit->installEventFilter(this); // Enter / Esc / focus-out
    updateRenameGeometry();
    m_renameEdit->show();
    m_renameEdit->setFocus(Qt::OtherFocusReason);

    // Select the name without its extension, like Nautilus.
    const QString name = m_renameEdit->text();
    int end = int(name.size());
    if (!m_model->isDir(source)) {
        const int dot = int(name.lastIndexOf('.'));
        if (dot > 0) {
            end = dot;
            if (name.left(dot).endsWith(QLatin1String(".tar")))
                end -= 4;
        }
    }
    m_renameEdit->setSelection(0, end);
}

void MainWindow::updateRenameGeometry()
{
    if (!m_renameEdit)
        return;
    const QModelIndex index = toView(m_model->indexForPath(m_renamePath));
    if (!index.isValid()) {
        cancelRename();
        return;
    }
    const FileItemDelegate *delegate = m_grid ? m_gridDelegate : m_listDelegate;
    QRect rect = delegate->nameEditorRect(m_view->visualRect(index), m_view->fontMetrics(), index);
    const int width = m_view->viewport()->width();
    if (rect.right() > width - 3)
        rect.moveRight(width - 3);
    if (rect.left() < 3)
        rect.moveLeft(3);
    m_renameEdit->setGeometry(rect);
}

void MainWindow::cancelRename()
{
    if (!m_renameEdit)
        return;
    QLineEdit *edit = m_renameEdit;
    m_renameEdit = nullptr;
    m_renamePath.clear();
    edit->removeEventFilter(this);
    edit->hide();
    edit->deleteLater();
    m_view->setFocus();
}

// Returns false (and keeps the field open) if the name can't be used.
bool MainWindow::commitRename()
{
    if (!m_renameEdit || m_committingRename)
        return true;
    const QString newName = m_renameEdit->text().trimmed();
    const QFileInfo old(m_renamePath);
    if (newName.isEmpty() || newName == old.fileName()) {
        cancelRename();
        return true;
    }

    QString problem;
    if (newName == QLatin1String(".") || newName == QLatin1String(".."))
        problem = tr("“%1” isn't a valid name").arg(newName);
    else if (newName.contains('/'))
        problem = tr("Names can't contain “/”");
#if defined(Q_OS_WIN)
    else if (newName.contains(QRegularExpression(QStringLiteral(R"([<>:"\\|?*])"))))
        problem = tr("Names can't contain any of: < > : \" \\ | ? *");
#endif
    const QString target = old.absolutePath() + '/' + newName;
    const QFileInfo targetInfo(target);
    if (problem.isEmpty() && (targetInfo.exists() || targetInfo.isSymLink()) && targetInfo != old)
        problem = tr("“%1” already exists").arg(newName);

    bool renamed = false;
    if (problem.isEmpty()) {
        m_committingRename = true;
        renamed = QDir().rename(m_renamePath, target);
        m_committingRename = false;
        if (!renamed)
            problem = tr("Couldn't rename “%1”").arg(old.fileName());
    }
    if (!renamed) {
        showMessage(problem);
        if (m_renameEdit)
            m_renameEdit->setFocus();
        return false;
    }

    Starred::instance().renamed(m_renamePath, target);
    m_ops->recordUndo(FileJob::Kind::Move, {{m_renamePath, target}}, tr("Rename"));
    cancelRename();
    selectPaths({target});
    return true;
}

// ---------------------------------------------------------- file views ---

void MainWindow::setupFileView(QAbstractItemView *view)
{
    view->setModel(m_proxy);
    view->setSelectionMode(QAbstractItemView::ExtendedSelection);
    view->setFrameShape(QFrame::NoFrame);
    view->setMouseTracking(true);
    view->viewport()->setAttribute(Qt::WA_Hover);
    view->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    // Drags start from the view (model provides the uri-list); drops are
    // intercepted in eventFilter -> handleViewDrag.
    view->setDragEnabled(true);
    view->setDragDropMode(QAbstractItemView::DragDrop);
    view->setDefaultDropAction(Qt::MoveAction);
    view->setDropIndicatorShown(false);
    // Qt only delivers drag events to widgets that accept drops; mark the
    // viewport explicitly so they stop there (where eventFilter handles them)
    // with viewport-relative positions.
    view->viewport()->setAcceptDrops(true);
    view->setContextMenuPolicy(Qt::CustomContextMenu);
    // Clicks and keys in the view mean the user is choosing: stop applying a
    // pending "highlight what was pasted" (see eventFilter).
    view->installEventFilter(this);
    view->viewport()->installEventFilter(this);
    connect(view, &QAbstractItemView::activated, this, &MainWindow::openIndex);
    connect(view, &QWidget::customContextMenuRequested, this, &MainWindow::showContextMenu);
    connect(m_thumbnailer, &Thumbnailer::thumbnailReady, view->viewport(),
            qOverload<>(&QWidget::update));
    // Keep an open rename field on its item while scrolling.
    connect(view->verticalScrollBar(), &QScrollBar::valueChanged, this,
            &MainWindow::updateRenameGeometry);
}

QModelIndexList MainWindow::selectedViewIndexes() const
{
    QModelIndexList items;
    if (m_analyzing)
        return items; // the file view is hidden: its selection mustn't be acted on
    for (const QModelIndex &index : m_iconView->selectionModel()->selectedIndexes()) {
        if (index.column() == 0)
            items.append(index);
    }
    return items;
}

bool MainWindow::isFileViewport(const QObject *object) const
{
    return (m_iconView && object == m_iconView->viewport())
           || (m_detailView && object == m_detailView->viewport());
}

void MainWindow::sortBy(int column, Qt::SortOrder order)
{
    m_detailView->sortByColumn(column, order); // updates the header and sorts the model
    updateSortActions();
}

void MainWindow::updateSortActions()
{
    if (m_background)
        return;
    if (m_sortColumnActs.isEmpty())
        return;
    const int column = m_model->sortColumn();
    if (column >= 0 && column < m_sortColumnActs.size())
        m_sortColumnActs.at(column)->setChecked(true);
    (m_model->sortOrder() == Qt::AscendingOrder ? m_sortAscAct : m_sortDescAct)->setChecked(true);
}

// --------------------------------------------------------- drag & drop ---

namespace {

QStringList localPaths(const QMimeData *mime)
{
    QStringList paths;
    if (!mime || !mime->hasUrls())
        return paths;
    for (const QUrl &url : mime->urls()) {
        if (url.isLocalFile())
            paths.append(QDir::cleanPath(url.toLocalFile()));
    }
    return paths;
}

} // namespace

QByteArray MainWindow::deviceOf(const QString &path) const
{
    const QFileInfo info(path);
    const QString dir = info.isDir() ? info.absoluteFilePath() : info.absolutePath();
    auto it = m_deviceCache.constFind(dir);
    if (it == m_deviceCache.constEnd())
        it = m_deviceCache.insert(dir, QStorageInfo(dir).device());
    return *it;
}

// Like Nautilus: same drive moves, another drive copies; Ctrl forces copy,
// Shift forces move. Returns IgnoreAction for drops that make no sense.
Qt::DropAction MainWindow::dropActionFor(const QStringList &sources, const QString &targetDir,
                                         Qt::KeyboardModifiers modifiers,
                                         Qt::DropActions possible) const
{
    if (sources.isEmpty() || targetDir.isEmpty() || !QFileInfo(targetDir).isWritable())
        return Qt::IgnoreAction;
    bool allAlreadyThere = true;
    for (const QString &source : sources) {
        if (targetDir == source || targetDir.startsWith(source + QLatin1Char('/')))
            return Qt::IgnoreAction; // a folder into itself
        if (QFileInfo(source).absolutePath() != targetDir)
            allAlreadyThere = false;
    }

    if (Trash::isFilesDir(targetDir)) {
        for (const QString &source : sources) {
            if (!Trash::filesDirOf(source).isEmpty())
                return Qt::IgnoreAction; // already in the Trash
        }
        return (possible & Qt::MoveAction) ? Qt::MoveAction : Qt::IgnoreAction;
    }

    Qt::DropAction action;
    if (modifiers & Qt::ControlModifier)
        action = Qt::CopyAction;
    else if (modifiers & Qt::ShiftModifier)
        action = Qt::MoveAction;
    else
        action = deviceOf(sources.first()) == deviceOf(targetDir) ? Qt::MoveAction
                                                                   : Qt::CopyAction;
    if (action == Qt::MoveAction && allAlreadyThere)
        return Qt::IgnoreAction; // nothing would change
    if (!(possible & action))
        action = (possible & Qt::CopyAction) ? Qt::CopyAction : Qt::IgnoreAction;
    return action;
}

void MainWindow::setDropTarget(const QModelIndex &viewIndex)
{
    m_gridDelegate->setDropTarget(viewIndex);
    m_listDelegate->setDropTarget(viewIndex);
    m_view->viewport()->update();
}

bool MainWindow::handleViewDrag(QAbstractItemView *view, QEvent *event)
{
    if (event->type() == QEvent::DragLeave) {
        setDropTarget({});
        m_dragScrollTimer->stop();
        m_springTimer->stop();
        m_springPath.clear();
        return true;
    }
    auto *drop = static_cast<QDropEvent *>(event); // DragEnter/DragMove derive from it
    const QStringList sources = localPaths(drop->mimeData());
    if (sources.isEmpty()) {
        drop->ignore();
        return true;
    }

    // Over a folder (that isn't being dragged itself): drop into it.
    // Anywhere else: drop into the open folder.
    const QPoint pos = drop->position().toPoint();
    QModelIndex target = view->indexAt(pos);
    QString targetDir = m_current;
    if (target.isValid()) {
        const QModelIndex source = toSource(target);
        const QString path = m_model->filePath(source);
        if (m_model->isDir(source) && !sources.contains(path))
            targetDir = path;
        else
            target = QModelIndex();
    }
    const Qt::DropAction action =
        dropActionFor(sources, targetDir, drop->modifiers(), drop->possibleActions());

    if (event->type() == QEvent::Drop) {
        setDropTarget({});
        m_dragScrollTimer->stop();
        m_springTimer->stop();
        m_springPath.clear();
        if (action == Qt::IgnoreAction) {
            drop->ignore();
            return true;
        }
        drop->setDropAction(action);
        drop->accept();
        performDrop(sources, targetDir, action);
        return true;
    }

    // DragEnter / DragMove: feedback only. Holding still over a folder opens it.
    const QString spring = target.isValid() ? targetDir : QString();
    if (spring != m_springPath) {
        m_springPath = spring;
        m_springPane = qMax(0, paneOf(view->viewport()));
        if (spring.isEmpty())
            m_springTimer->stop();
        else
            m_springTimer->start();
    }
    setDropTarget(action == Qt::IgnoreAction ? QModelIndex() : target.siblingAtColumn(0));
    const int edge = 32;
    const int height = view->viewport()->height();
    m_dragScrollStep = pos.y() < edge ? -20 : (pos.y() > height - edge ? 20 : 0);
    m_dragScrollView = view;
    if (m_dragScrollStep != 0 && !m_dragScrollTimer->isActive())
        m_dragScrollTimer->start();
    if (action == Qt::IgnoreAction) {
        // Still accept the enter, so we keep getting move events.
        if (event->type() == QEvent::DragEnter)
            drop->acceptProposedAction();
        else
            drop->ignore();
        return true;
    }
    drop->setDropAction(action);
    drop->accept();
    return true;
}

// Dropping onto a sidebar place or drive copies/moves into it.
bool MainWindow::handleSidebarDrag(QEvent *event)
{
    if (event->type() == QEvent::DragLeave || event->type() == QEvent::Drop) {
        m_springTimer->stop();
        m_springPath.clear();
    }
    if (event->type() == QEvent::DragLeave) {
        syncSidebarSelection();
        return true;
    }
    auto *drop = static_cast<QDropEvent *>(event);
    const QStringList sources = localPaths(drop->mimeData());
    QListWidgetItem *item = m_sidebar->itemAt(drop->position().toPoint());

    // Folders dropped on empty space (or a separator) become bookmarks.
    if (!item || item->data(SeparatorRole).toBool()) {
        bool allFolders = !sources.isEmpty();
        for (const QString &source : sources)
            allFolders = allFolders && QFileInfo(source).isDir() && Trash::filesDirOf(source).isEmpty();
        if (!allFolders) {
            if (event->type() == QEvent::DragEnter && !sources.isEmpty())
                drop->acceptProposedAction();
            else
                drop->ignore();
            return true;
        }
        drop->setDropAction(drop->possibleActions() & Qt::LinkAction ? Qt::LinkAction
                                                                     : Qt::CopyAction);
        drop->accept();
        if (event->type() == QEvent::Drop) {
            for (const QString &source : sources)
                Bookmarks::add(source);
            showMessage(sources.size() == 1 ? tr("“%1” added to bookmarks").arg(QFileInfo(sources.first()).fileName())
                                            : tr("%1 folders added to bookmarks").arg(sources.size()));
            scheduleSidebarRefresh();
            updateBookmarkAction();
        }
        return true;
    }
    const QString targetDir = QDir::cleanPath(item->data(PathRole).toString());
    const Qt::DropAction action =
        targetDir.isEmpty()
            ? Qt::IgnoreAction
            : dropActionFor(sources, targetDir, drop->modifiers(), drop->possibleActions());

    if (event->type() == QEvent::Drop) {
        syncSidebarSelection();
        if (action == Qt::IgnoreAction) {
            drop->ignore();
            return true;
        }
        drop->setDropAction(action);
        drop->accept();
        performDrop(sources, targetDir, action);
        return true;
    }

    if (action == Qt::IgnoreAction) {
        syncSidebarSelection();
        if (event->type() == QEvent::DragEnter && !sources.isEmpty())
            drop->acceptProposedAction();
        else
            drop->ignore();
        return true;
    }
    m_sidebar->setCurrentItem(item); // highlight the place under the cursor
    if (targetDir != m_springPath && !Trash::isFilesDir(targetDir)) {
        m_springPath = targetDir; // held still: open it, like Nautilus
        m_springPane = m_activePane;
        m_springTimer->start();
    }
    drop->setDropAction(action);
    drop->accept();
    return true;
}

// ------------------------------------------------------------ terminal ---

#if defined(ARGONAUT_TERMINAL)

QWidget *MainWindow::buildTerminalPanel()
{
    m_terminalPanel = new QWidget(this);
    m_terminalPanel->setObjectName("terminalPanel");
    m_terminalPanel->setAttribute(Qt::WA_StyledBackground);
    auto *layout = new QVBoxLayout(m_terminalPanel);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto *bar = new QWidget(m_terminalPanel);
    bar->setObjectName("terminalBar");
    bar->setAttribute(Qt::WA_StyledBackground);
    auto *row = new QHBoxLayout(bar);
    row->setContentsMargins(14, 3, 6, 3);
    m_terminalTitle = new QLabel(tr("Terminal"), bar);
    m_terminalTitle->setObjectName("terminalTitle");
    m_terminalCloseButton = makeButton(tr("Hide terminal (Ctrl+`)"));
    m_terminalCloseButton->setFixedSize(26, 26);
    connect(m_terminalCloseButton, &QToolButton::clicked, this,
            [this] { m_terminalAct->setChecked(false); });
    row->addWidget(m_terminalTitle, 1);
    row->addWidget(m_terminalCloseButton);
    layout->addWidget(bar);

    m_terminal = new TerminalWidget(m_terminalPanel);
    m_terminal->setPassthroughShortcuts({m_terminalAct->shortcut()});
    connect(m_terminal, &TerminalWidget::titleChanged, this, [this](const QString &title) {
        m_terminalTitle->setText(title.isEmpty() ? tr("Terminal") : title);
    });
    // `exit` closes the panel; the next one starts a fresh shell.
    connect(m_terminal, &TerminalWidget::finished, this, [this] {
        m_terminalTitle->setText(tr("Terminal"));
        m_terminalAct->setChecked(false);
    });
    layout->addWidget(m_terminal, 1);

    m_terminalPanel->hide();
    return m_terminalPanel;
}

// Show the terminal in `directory`: start a shell there, or `cd` the running
// one (only if it's idle at the prompt, never under a running program).
void MainWindow::showTerminal(const QString &folder)
{
    // Recent / Starred aren't folders: the shell starts at home.
    const QString directory = DirectoryModel::isVirtualPath(folder) ? QDir::homePath() : folder;
    if (!m_terminal->isRunning()) {
        if (!m_terminal->start(directory)) {
            showMessage(tr("Couldn't start a terminal"));
            const QSignalBlocker block(m_terminalAct);
            m_terminalAct->setChecked(false);
            m_terminalButton->setChecked(false);
            return;
        }
    } else if (!m_terminal->changeDirectory(directory)) {
        showMessage(tr("A program is running in the terminal, so it stays where it is"));
    }

    if (!m_terminalAct->isChecked()) {
        const QSignalBlocker block(m_terminalAct);
        m_terminalAct->setChecked(true);
        m_terminalButton->setChecked(true);
    }
    if (m_terminalPanel->isHidden()) {
        m_terminalPanel->show();
        if (!m_terminalSized) {
            const int total = m_contentSplitter->height();
            const int height = m_terminalHeight > 80 ? qMin(m_terminalHeight, total - 120)
                                                     : total * 38 / 100;
            m_contentSplitter->setSizes({total - height, height});
            m_terminalSized = true;
        }
    }
    m_terminal->setFocus(Qt::ShortcutFocusReason);
}

void MainWindow::hideTerminal()
{
    const bool hadFocus = m_terminal->hasFocus();
    m_terminalPanel->hide();
    if (hadFocus)
        m_view->setFocus();
}

#else

QWidget *MainWindow::buildTerminalPanel()
{
    return nullptr;
}
void MainWindow::showTerminal(const QString &) {}
void MainWindow::hideTerminal() {}

#endif

// ----------------------------------------------------------- conflicts ---

void MainWindow::askConflict(FileJob *job, const QString &source, const QString &existing)
{
    m_conflicts.append({QPointer<FileJob>(job), source, existing});
    if (!m_conflictOpen)
        showNextConflict();
}

void MainWindow::showNextConflict()
{
    while (!m_conflicts.isEmpty()) {
        const PendingConflict next = m_conflicts.takeFirst();
        if (!next.job)
            continue; // cancelled or finished meanwhile
        m_conflictOpen = true;
        ConflictDialog dialog(next.source, next.existing, this);
        dialog.exec();
        m_conflictOpen = false;
        if (next.job)
            next.job->resolveConflict(dialog.resolution());
    }
}

// ---------------------------------------------------------------- tabs ---

QString MainWindow::tabTitle(const QString &path) const
{
    if (!Trash::filesDirOf(path).isEmpty() && Trash::isFilesDir(path))
        return tr("Trash");
    const QString name = QFileInfo(path).fileName();
    return name.isEmpty() ? QDir::toNativeSeparators(path) : name;
}

void MainWindow::addTabCloseButton(int index)
{
    auto *close = new QToolButton(m_tabBar);
    close->setObjectName("flat");
    close->setFixedSize(20, 20);
    close->setIconSize(QSize(12, 12));
    close->setFocusPolicy(Qt::NoFocus);
    close->setToolTip(tr("Close Tab (Ctrl+W)"));
    close->setIcon(glyphIcon(Glyph::Close, m_theme.dimFg));
    connect(close, &QToolButton::clicked, this, [this, close] {
        for (int i = 0; i < m_tabBar->count(); ++i) {
            if (m_tabBar->tabButton(i, QTabBar::RightSide) == close) {
                closeTab(i);
                return;
            }
        }
    });
    m_tabBar->setTabButton(index, QTabBar::RightSide, close);
}

void MainWindow::updateTabBar()
{
    m_tabBar->setVisible(m_tabs.size() > 1);
}

void MainWindow::saveActiveTab()
{
    if (m_activeTab < 0 || m_activeTab >= m_tabs.size())
        return;
    TabState &tab = m_tabs[m_activeTab];
    tab.path = m_current;
    tab.back = m_backHistory;
    tab.forward = m_forwardHistory;
    tab.selection = selectedPaths();
    tab.scroll = m_view->verticalScrollBar()->value();
}

// New tab right after the current one, like browsers. Background tabs
// (middle-click) don't take you there.
void MainWindow::openTab(const QString &path, bool activate)
{
    if (m_activePane != 0)
        activatePane(0); // tabs belong to the first pane
    saveActiveTab();
    TabState tab;
    tab.path = QDir::cleanPath(path);
    const int index = m_activeTab + 1;
    m_switchingTabs = true;
    m_tabs.insert(index, tab);
    m_tabBar->insertTab(index, tabTitle(tab.path));
    m_tabBar->setTabToolTip(index, QDir::toNativeSeparators(tab.path));
    addTabCloseButton(index);
    m_switchingTabs = false;
    updateTabBar();
    if (activate)
        m_tabBar->setCurrentIndex(index);
    else
        showMessage(tr("Opened “%1” in a new tab").arg(tabTitle(tab.path)));
}

void MainWindow::closeTab(int index)
{
    if (m_activePane != 0)
        activatePane(0); // tabs belong to the first pane
    if (index < 0 || index >= m_tabs.size())
        return;
    if (m_tabs.size() == 1) {
        close(); // the last tab closes the window
        return;
    }
    if (index == m_activeTab)
        saveActiveTab();
    m_closedTabs.append(m_tabs.at(index));
    if (m_closedTabs.size() > 10)
        m_closedTabs.removeFirst();

    const bool wasActive = index == m_activeTab;
    m_switchingTabs = true;
    m_tabs.removeAt(index);
    m_tabBar->removeTab(index);
    m_switchingTabs = false;
    if (wasActive) {
        m_activeTab = -1; // nothing to save: it's gone
        switchToTab(m_tabBar->currentIndex());
    } else if (index < m_activeTab) {
        --m_activeTab;
    }
    updateTabBar();
}

void MainWindow::reopenClosedTab()
{
    if (m_activePane != 0)
        activatePane(0); // tabs belong to the first pane
    if (m_closedTabs.isEmpty())
        return;
    const TabState tab = m_closedTabs.takeLast();
    openTab(tab.path, true);
    // Bring back its history and selection too.
    m_backHistory = tab.back;
    m_forwardHistory = tab.forward;
    m_tabs[m_activeTab].back = tab.back;
    m_tabs[m_activeTab].forward = tab.forward;
    updateActions();
    selectPaths(tab.selection);
    m_pendingScroll = tab.scroll;
}

void MainWindow::switchToTab(int index)
{
    if (m_activePane != 0)
        activatePane(0); // tabs belong to the first pane
    if (m_switchingTabs || index < 0 || index >= m_tabs.size() || index == m_activeTab)
        return;
    saveActiveTab();
    m_activeTab = index;
    const TabState tab = m_tabs.at(index);

    if (tab.path != m_current) {
        if (!navigateTo(tab.path, false)) // its folder may be gone by now
            navigateTo(QDir::homePath(), false);
    } else {
        setWindowTitle(tr("%1 — Argonaut").arg(tabTitle(m_current)));
    }
    m_backHistory = tab.back;
    m_forwardHistory = tab.forward;
    updateActions();
    if (!tab.selection.isEmpty())
        selectPaths(tab.selection);
    else
        m_view->selectionModel()->clearSelection();
    if (m_model->isLoading())
        m_pendingScroll = tab.scroll;
    else
        m_view->verticalScrollBar()->setValue(tab.scroll);
}

// ------------------------------------------------------------ type-ahead ---

// Letters typed in the file view build a search; a pause of a second
// starts a new one. Returns true if the key was used.
bool MainWindow::typeAheadKey(QKeyEvent *event)
{
    if (event->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier))
        return false;
    const bool active = !m_typeAhead.isEmpty() && m_typeAheadTimer.isValid()
                        && m_typeAheadTimer.elapsed() < 1000;
    if (event->key() == Qt::Key_Backspace && active) {
        m_typeAhead.chop(1);
        m_typeAheadTimer.restart();
        if (!m_typeAhead.isEmpty())
            typeAheadFind();
        return true;
    }
    if (event->key() == Qt::Key_Escape && active) {
        m_typeAhead.clear();
        showMessage(QString());
        return true;
    }
    const QString text = event->text();
    if (text.isEmpty() || !text.at(0).isPrint())
        return false;
    if (text == QLatin1String(" ") && !active)
        return false; // a lone space keeps its usual meaning
    if (!active)
        m_typeAhead.clear();
    m_typeAhead += text;
    m_typeAheadTimer.restart();
    typeAheadFind();
    return true;
}

// Select the first item whose name starts with the typed text, or failing
// that contains it (case-insensitive).
void MainWindow::typeAheadFind()
{
    QModelIndex hit;
    const int rows = m_proxy->rowCount();
    for (const bool prefix : {true, false}) {
        for (int row = 0; row < rows && !hit.isValid(); ++row) {
            const QModelIndex index = m_proxy->index(row, 0);
            const QString name = index.data().toString();
            if (prefix ? name.startsWith(m_typeAhead, Qt::CaseInsensitive)
                       : name.contains(m_typeAhead, Qt::CaseInsensitive))
                hit = index;
        }
        if (hit.isValid())
            break;
    }
    if (hit.isValid()) {
        QItemSelectionModel *selection = m_view->selectionModel();
        selection->select(QItemSelection(hit, hit.siblingAtColumn(DirectoryModel::ColumnCount - 1)),
                          QItemSelectionModel::ClearAndSelect);
        selection->setCurrentIndex(hit, QItemSelectionModel::NoUpdate);
        m_view->scrollTo(hit);
        showMessage(tr("Find: %1").arg(m_typeAhead));
    } else {
        showMessage(tr("Find: %1 — no match").arg(m_typeAhead));
    }
}

// ----------------------------------------------------------- open with ---

QString MainWindow::effectiveMimeAt(const QModelIndex &sourceIndex) const
{
    ContentType::Result r;
    r.nameMime = sourceIndex.data(DirectoryModel::NameMimeRole).toString();
    r.contentMime = sourceIndex.data(DirectoryModel::ContentMimeRole).toString();
    r.verdict = ContentType::Verdict(sourceIndex.data(DirectoryModel::VerdictRole).toInt());
    return ContentType::effectiveMime(r);
}

bool MainWindow::quickLookVisible() const
{
    return m_quickLook && m_quickLook->isVisible();
}

void MainWindow::toggleQuickLook()
{
    if (quickLookVisible()) {
        m_quickLook->hide();
        return;
    }
    if (m_analyzing || !m_view->currentIndex().isValid())
        return;
    if (!m_quickLook) {
        m_quickLook = new QuickLook(m_thumbnailer, this);
        connect(m_quickLook, &QuickLook::navigate, this, [this](QKeyEvent *key) {
            QKeyEvent copy(QEvent::KeyPress, key->key(), key->modifiers(), key->text());
            QApplication::sendEvent(m_view, &copy);
        });
        connect(m_quickLook, &QuickLook::openRequested, this, [this](const QString &path) {
            m_quickLook->hide();
            if (QFileInfo(path).isDir())
                navigateTo(path);
            else
                openFile(path);
        });
    }
    // Over the middle of the window, a little smaller than it.
    const QSize size(qBound(480, width() * 3 / 4, 1100), qBound(380, height() * 4 / 5, 900));
    m_quickLook->resize(size);
    m_quickLook->move(mapToGlobal(QPoint((width() - size.width()) / 2, (height() - size.height()) / 2)));
    updateQuickLook();
    m_quickLook->show();
    m_view->setFocus(); // keys keep going to the file view
}

void MainWindow::updateQuickLook()
{
    const QModelIndex view = m_view->currentIndex();
    if (!view.isValid() || !m_quickLook)
        return;
    const QModelIndex source = toSource(view.siblingAtColumn(0));
    QuickLook::Item item;
    item.path = m_model->filePath(source);
    if (item.path == m_quickLook->path() && m_quickLook->isVisible())
        return;
    item.name = m_model->fileName(source);
    item.verdict = ContentType::Verdict(source.data(DirectoryModel::VerdictRole).toInt());
    item.mimeType = m_model->isDir(source) ? QStringLiteral("inode/directory") : effectiveMimeAt(source);
    item.icon = source.data(Qt::DecorationRole).value<QIcon>();
    static const QMimeDatabase db;
    QStringList details;
    details << (m_model->isDir(source) ? tr("Folder") : db.mimeTypeForName(item.mimeType).comment());
    if (!m_model->isDir(source))
        details << formatSize(m_model->size(source));
    details << QLocale().toString(source.data(DirectoryModel::ModifiedRole).toDateTime(), QLocale::ShortFormat);
    item.details = details.join(QStringLiteral(" · "));
    m_quickLook->showItem(item);
}

void MainWindow::rememberOpened(const QStringList &files)
{
    static const QMimeDatabase db;
    for (const QString &file : files)
        RecentFiles::add(file, db.mimeTypeForFile(file, QMimeDatabase::MatchExtension).name());
}

QMenu *MainWindow::openWithMenu(const QStringList &files)
{
    QMenu *menu = makeMenu();
    menu->setTitle(tr("Open With"));
    static const QMimeDatabase db;
    QString mime = db.mimeTypeForFile(files.first()).name();
    if (mime.isEmpty())
        mime = QStringLiteral("application/octet-stream"); // never ask gio about ""
    QString defaultId;
    const QList<OpenWith::App> apps = OpenWith::appsFor(mime, &defaultId);
    int shown = 0;
    for (const OpenWith::App &app : apps) {
        if (shown++ == 8)
            break; // the rest are in "Other Application…"
        QAction *act = menu->addAction(appIcon(app), app.id == defaultId
                                                         ? tr("%1 (default)").arg(app.name)
                                                         : app.name);
        connect(act, &QAction::triggered, this, [this, app, files] {
            if (OpenWith::launch(app, files) && (rememberOpened(files), true))
                showMessage(tr("Opening with %1").arg(app.name));
            else
                showMessage(tr("Couldn't start %1").arg(app.name));
        });
    }
    if (!apps.isEmpty())
        menu->addSeparator();
    menu->addAction(tr("Other Application…"), this, [this, files] { openWithOther(files); });
    return menu;
}

void MainWindow::openWithOther(const QStringList &files)
{
    static const QMimeDatabase db;
    const QMimeType type = db.mimeTypeForFile(files.first());
    const QString mime = type.isValid() ? type.name() : QStringLiteral("application/octet-stream");
    OpenWithDialog dialog(files, mime, type.isValid() ? type.comment() : mime, this);
    if (dialog.exec() != QDialog::Accepted)
        return;
    const OpenWith::App app = dialog.chosen();
    if (app.desktopFile.isEmpty())
        return;
    if (dialog.makeDefault())
        OpenWith::setDefault(mime, app);
    if (OpenWith::launch(app, files) && (rememberOpened(files), true))
        showMessage(tr("Opening with %1").arg(app.name));
    else
        showMessage(tr("Couldn't start %1").arg(app.name));
}

// ------------------------------------------------------ real file types ---

void MainWindow::openFile(const QString &path)
{
    const QFileInfo info(path);
    const qint64 mtime = info.lastModified().toMSecsSinceEpoch();
    ContentType::Result result;
    if (!ContentType::Cache::instance().lookup(path, info.size(), mtime, &result)) {
        result = ContentType::inspect(path); // one file: well under a millisecond
        ContentType::Cache::instance().store(path, info.size(), mtime, result);
    }

    if (result.verdict != ContentType::Verdict::Dangerous)
        rememberOpened({path});
    if (result.verdict == ContentType::Verdict::Dangerous) {
        QMessageBox box(QMessageBox::Warning, tr("This file is a program in disguise"),
                        tr("“%1” is named like a document, but it is really a program.")
                            .arg(info.fileName()),
                        QMessageBox::NoButton, this);
        box.setInformativeText(ContentType::describe(result) + QStringLiteral(".\n\n")
                               + tr("Files like this are a common way to spread malware. "
                                    "Argonaut won't run it."));
        QPushButton *keep = box.addButton(tr("Don't Open"), QMessageBox::RejectRole);
        QPushButton *trash = box.addButton(tr("Move to Trash"), QMessageBox::DestructiveRole);
        QPushButton *asNamed = box.addButton(tr("Open as Named"), QMessageBox::AcceptRole);
        asNamed->setToolTip(tr("Open with the app for its name, without running it"));
        box.setDefaultButton(keep);
        box.exec();
        if (box.clickedButton() == trash)
            m_ops->trash({path});
        else if (box.clickedButton() == asNamed)
            QDesktopServices::openUrl(QUrl::fromLocalFile(path));
        return;
    }

    // Named wrongly or not at all: open with the app for what it really is.
    if ((result.verdict == ContentType::Verdict::Mismatch
         || result.verdict == ContentType::Verdict::Identified)
        && OpenWith::isSupported()) {
        QString defaultId;
        const QList<OpenWith::App> apps = OpenWith::appsFor(result.contentMime, &defaultId);
        for (const OpenWith::App &app : apps) {
            if (app.id == defaultId || defaultId.isEmpty()) {
                if (OpenWith::launch(app, {path})) {
                    showMessage(tr("Opening as %1 with %2")
                                    .arg(QMimeDatabase().mimeTypeForName(result.contentMime).comment(),
                                         app.name));
                    return;
                }
            }
        }
    }
    QDesktopServices::openUrl(QUrl::fromLocalFile(path));
}

// Give a mismatched file the extension its content calls for (undoable).
void MainWindow::fixExtension(const QString &path)
{
    const QFileInfo info(path);
    const ContentType::Result result = ContentType::inspect(path);
    const QString fixed = ContentType::fixedName(info.fileName(), result.contentMime);
    if (fixed.isEmpty())
        return;
    QString target = info.absolutePath() + QLatin1Char('/') + fixed;
    if (QFileInfo::exists(target))
        target = uniqueTargetPath(info.absolutePath(), fixed, false);
    if (!QDir().rename(path, target)) {
        showMessage(tr("Couldn't rename “%1”").arg(info.fileName()));
        return;
    }
    Starred::instance().renamed(path, target);
    m_ops->recordUndo(FileJob::Kind::Move, {{path, target}}, tr("Rename"));
    selectPaths({target});
    showMessage(tr("Renamed to “%1”").arg(QFileInfo(target).fileName()));
}

void MainWindow::keyPressEvent(QKeyEvent *event)
{
#if defined(ARGONAUT_DBUS)
    // Esc that nothing else wanted (rename, search, type-ahead) cancels.
    if (m_chooser && event->key() == Qt::Key_Escape) {
        finishChooser(FileChooserPortal::Cancelled);
        return;
    }
#endif
    QMainWindow::keyPressEvent(event);
}

// ------------------------------------------------------ Open/Save window ---
#if defined(ARGONAUT_DBUS)

void MainWindow::startChooser(const ChooserRequest &request)
{
    using Mode = ChooserRequest::Mode;
    m_chooser = request;

    // What a dialog doesn't need: the menus, terminal, Analyze, split view,
    // tabs, and commands that would open other windows or quit the service.
    // The menu bar stays (empty): its row holds the window buttons at the
    // right and drags or maximises the window like in the file manager.
    if (m_menuBar)
        for (QAction *menu : m_menuBar->actions())
            menu->setVisible(false);
    for (QToolButton *button : {m_terminalButton, m_analyzeButton, m_splitButton, m_menuButton})
        if (button)
            button->hide();
    m_chooserOff = {m_splitAct,      m_terminalAct,   m_openTerminalHereAct, m_analyzeAct,   m_newTabAct,
                    m_reopenTabAct,  m_openInNewTabAct, m_quitAct,           m_prefsAct,     m_paletteAct,
                    m_organizeAct,   m_copyToOtherAct, m_moveToOtherAct,     m_aboutAct,     m_guideAct,
                    m_shortcutsAct};
    m_chooserOff.removeAll(nullptr);
    keepChooserActionsOff();

    m_chooserBar = new ChooserBar(request, this);
    m_contentLayout->addWidget(m_chooserBar);
    connect(m_chooserBar, &ChooserBar::accepted, this, &MainWindow::chooserAccept);
    connect(m_chooserBar, &ChooserBar::rejected, this, [this] { finishChooser(FileChooserPortal::Cancelled); });
    auto applyFilter = [this](int index) {
        std::optional<ChooserRequest::Filter> filter;
        if (index >= 0 && index < m_chooser->filters.size())
            filter = m_chooser->filters.at(index);
        m_proxy->setChooserFilter(filter, m_chooser->directory);
    };
    applyFilter(m_chooserBar->currentFilter());
    connect(m_chooserBar, &ChooserBar::filterChanged, this, applyFilter);

    const auto selection = request.multiple ? QAbstractItemView::ExtendedSelection
                                            : QAbstractItemView::SingleSelection;
    m_iconView->setSelectionMode(selection);
    m_detailView->setSelectionMode(selection);
    connect(m_view->selectionModel(), &QItemSelectionModel::selectionChanged, this, &MainWindow::updateChooser);

    // Attach it to the window of the app that asked (X11 "x11:<hex id>";
    // Wayland handles would need xdg-foreign). Qt's xcb plugin only writes
    // WM_TRANSIENT_FOR for dialog-type windows, so the window becomes one:
    // without that the desktop treats it as unrelated, and it drops behind
    // the app whenever the app comes forward. Not modal: GNOME glues modal
    // dialogs to their parent, and then they can't be moved.
    WId parentId = 0;
    if (request.parentWindow.startsWith(QLatin1String("x11:"))) {
        bool ok = false;
        parentId = WId(request.parentWindow.mid(4).toULongLong(&ok, 16));
        if (!ok)
            parentId = 0;
    }
    if (parentId) {
        setWindowFlag(Qt::Dialog); // re-creates the native window: before anything uses it
        winId();
        m_chooserParent = QWindow::fromWinId(parentId);
        if (m_chooserParent && windowHandle()) {
            connect(this, &QObject::destroyed, m_chooserParent, &QObject::deleteLater);
            windowHandle()->setTransientParent(m_chooserParent);
        }
    }

    // Only the size is remembered. It opens in the middle of the screen with
    // the mouse, i.e. where the app that asked was just clicked.
    const QSize size = QSettings().value("chooser/size").toSize();
    resize(size.isValid() ? size : QSize(960, 620));
    if (QScreen *screen = QGuiApplication::screenAt(QCursor::pos())) {
        const QRect area = screen->availableGeometry();
        resize(this->size().boundedTo(area.size() * 0.9)); // never larger than the screen
        move(area.center() - rect().center());
    }

    // Start where the app asked, else where this app saved or opened last.
    QString start = request.currentFolder;
    if (start.isEmpty() || !QFileInfo(start).isDir())
        start = QSettings().value(chooserFolderKey()).toString();
    if (start.isEmpty() || !QFileInfo(start).isDir())
        start = QDir::homePath();
    if (request.mode == Mode::Save && QFileInfo(request.currentFile).isFile())
        showItems({request.currentFile});
    else
        navigateTo(start);
    refreshChrome(); // the title

    updateChooser();
    if (request.mode == Mode::Save)
        QTimer::singleShot(0, this, [this] { m_chooserBar->focusName(); });
}

void MainWindow::cancelChooser()
{
    finishChooser(FileChooserPortal::Cancelled);
}

QString MainWindow::chooserFolderKey() const
{
    QString app = m_chooser ? m_chooser->appId : QString();
    app.replace(QLatin1Char('/'), QLatin1Char('_'));
    return QStringLiteral("chooser/lastFolder/") + (app.isEmpty() ? QStringLiteral("host") : app);
}

void MainWindow::keepChooserActionsOff()
{
    if (!m_chooser)
        return;
    for (QAction *action : std::as_const(m_chooserOff)) {
        action->setEnabled(false);
        action->setVisible(false);
    }
}

void MainWindow::updateChooser()
{
    if (!m_chooser || !m_chooserBar)
        return;
    using Mode = ChooserRequest::Mode;
    const QStringList selected = selectedPaths();
    if (m_chooser->mode == Mode::Save && selected.size() == 1 && !QFileInfo(selected.first()).isDir())
        m_chooserBar->setName(QFileInfo(selected.first()).fileName()); // save over that one
    bool canAccept = true;
    if (m_chooser->mode == Mode::Open && !m_chooser->directory)
        canAccept = !selected.isEmpty();
    m_chooserBar->setAcceptEnabled(canAccept);
}

void MainWindow::chooserAccept()
{
    if (!m_chooser || m_chooserDone)
        return;
    using Mode = ChooserRequest::Mode;
    const ChooserRequest &request = *m_chooser;
    const QStringList selected = selectedPaths();
    const bool realFolder = !DirectoryModel::isVirtualPath(m_current) && !m_model->isTrash()
                            && QFileInfo(m_current).isDir();

    if (request.mode == Mode::Open) {
        if (m_model->isTrash() && !selected.isEmpty()) {
            showMessage(tr("Restore it from the Trash first"));
            return;
        }
        if (request.directory) {
            QStringList folders;
            for (const QString &path : selected)
                if (QFileInfo(path).isDir())
                    folders.append(path);
            if (folders.isEmpty()) {
                if (!realFolder) {
                    showMessage(tr("Choose a folder"));
                    return;
                }
                folders.append(m_current);
            }
            if (!request.multiple)
                folders = {folders.first()};
            finishChooser(FileChooserPortal::Success, folders);
            return;
        }
        QStringList files;
        for (const QString &path : selected)
            if (!QFileInfo(path).isDir())
                files.append(path);
        if (files.isEmpty()) {
            if (selected.size() == 1)
                navigateTo(selected.first()); // a folder: go in
            else
                showMessage(tr("Choose a file"));
            return;
        }
        finishChooser(FileChooserPortal::Success, files);
        return;
    }

    // Saving: into a real folder you can write to.
    QString folder = m_current;
    if (request.mode == Mode::SaveFiles && selected.size() == 1 && QFileInfo(selected.first()).isDir())
        folder = selected.first();
    else if (!realFolder) {
        showMessage(tr("Choose a folder to save in"));
        return;
    }
    if (!QFileInfo(folder).isWritable()) {
        showMessage(tr("You can't save in this folder"));
        return;
    }
    // The question runs its own event loop: meanwhile the app may give up
    // (Request.Close), which closes and deletes this window, or Save may be
    // pressed again. So one question at a time, and nothing goes on if the
    // window is gone.
    if (m_chooserAsking)
        return;
    QPointer<MainWindow> self(this);
    auto confirmReplace = [this, self](const QStringList &existing) {
        const QFileInfo first(existing.first());
        const QString title = existing.size() == 1 ? tr("Replace “%1”?").arg(first.fileName())
                                                   : tr("Replace %n files?", nullptr, int(existing.size()));
        const QString text = existing.size() == 1
                                 ? tr("A file with this name already exists in “%1”. Saving replaces its contents.")
                                       .arg(first.dir().dirName())
                                 : tr("Files with these names already exist in “%1”. Saving replaces their contents.")
                                       .arg(first.dir().dirName());
        QPointer<QMessageBox> box = new QMessageBox(QMessageBox::Warning, title, text, QMessageBox::NoButton, this);
        QPushButton *cancel = box->addButton(tr("Cancel"), QMessageBox::RejectRole);
        QPushButton *replace = box->addButton(tr("Replace"), QMessageBox::AcceptRole);
        box->setDefaultButton(cancel);
        m_chooserAsking = true;
        box->exec();
        if (!self) // deleted with the window
            return false;
        m_chooserAsking = false;
        const bool yes = box && box->clickedButton() == replace;
        delete box;
        return yes && !m_chooserDone;
    };

    if (request.mode == Mode::Save) {
        const QString name = m_chooserBar->name().trimmed();
        if (name.isEmpty() || name == QLatin1String(".") || name == QLatin1String("..")) {
            m_chooserBar->focusName();
            return;
        }
        QString path = name;
        if (path.startsWith(QLatin1String("~/")))
            path = QDir::homePath() + path.mid(1);
        if (!QDir::isAbsolutePath(path))
            path = QDir(folder).filePath(path);
        path = QDir::cleanPath(path);
        if (QFileInfo(path).isDir()) { // typed a folder's name: go there
            navigateTo(path);
            m_chooserBar->setName(QString());
            return;
        }
        const QFileInfo target(path);
        if (!QFileInfo(target.absolutePath()).isDir()) {
            showMessage(tr("The folder “%1” doesn't exist").arg(target.absolutePath()));
            return;
        }
        if (target.exists() && !confirmReplace({path}))
            return;
        finishChooser(FileChooserPortal::Success, {path});
        return;
    }
    const QStringList paths = request.saveFilesPaths(folder);
    QStringList existing;
    for (const QString &path : paths)
        if (QFileInfo::exists(path))
            existing.append(path);
    if (!existing.isEmpty() && !confirmReplace(existing))
        return;
    finishChooser(FileChooserPortal::Success, paths);
}

void MainWindow::finishChooser(uint response, const QStringList &paths)
{
    if (!m_chooser || m_chooserDone)
        return;
    m_chooserDone = true;
    QVariantMap results;
    if (response == FileChooserPortal::Success) {
        results = m_chooser->results(paths, m_chooserBar->choiceValues(), m_chooserBar->currentFilter());
        if (!DirectoryModel::isVirtualPath(m_current) && !m_model->isTrash())
            QSettings().setValue(chooserFolderKey(), m_current);
    }
    if (!isMaximized())
        QSettings().setValue("chooser/size", size());
    emit chooserFinished(response, results);
    QTimer::singleShot(0, this, &QWidget::close); // not from inside closeEvent
}
#endif
