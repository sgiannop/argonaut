#pragma once

#include "Theme.h"

#include <QAbstractItemModel>
#include <QElapsedTimer>
#include <QHash>
#include <QMainWindow>
#include <QPointer>
#include <QStack>
#include <QString>

#if defined(ARGONAUT_DBUS)
#include "FileChooserRequest.h"
#include <optional>
class ChooserBar;
class QVBoxLayout;
class QWindow;
#endif

class DirectoryModel;
class DiskUsageModel;
class QuickLook;
class FileFilterProxy;
class FileItemDelegate;
class FileJob;
class FileOperations;
class OperationsPanel;
class TerminalWidget;
class Toast;
class PathBar;
class Thumbnailer;
class QAction;
class QLabel;
class QLineEdit;
class QAbstractItemView;
class QListView;
class QTreeView;
class QListWidget;
class QMenu;
class QMenuBar;
class QModelIndex;
class QSplitter;
class QStackedWidget;
class QTabBar;
class QTimer;
class QToolButton;
class QKeyEvent;
class QHBoxLayout;
class QVBoxLayout;

class QHeaderView;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);

    // Show the folder at `path`. Returns false if it doesn't exist.
    bool navigateTo(const QString &path, bool addToHistory = true);
    // The first folder here, the others in tabs ("Open folder" from other apps).
    void openFolders(const QStringList &folders);
    // Files or folders shown selected in the folder that holds them ("Show
    // in folder"); items in other folders open in tabs.
    void showItems(const QStringList &items);

#if defined(ARGONAUT_DBUS)
    // Open/Save for another app (`argonaut --portal`): call right after
    // construction. The window then answers once, through chooserFinished
    // (FileChooserPortal's response and results), and closes.
    void startChooser(const ChooserRequest &request);
    void cancelChooser(); // the app gave up (Request.Close)

signals:
    void chooserFinished(uint response, const QVariantMap &results);
#endif

protected:
    void keyPressEvent(QKeyEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void changeEvent(QEvent *event) override;
    void closeEvent(QCloseEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void setupActions();
    void setupUi();
    QWidget *buildSidebarPane();
    QWidget *buildContentPane();
    QToolButton *makeButton(const QString &toolTip, const char *objectName = "flat");
    // Minimize / maximize / close (client-side decorations), at the end of `row`.
    void addWindowButtons(QHBoxLayout *row);
    QMenu *makeMenu();
    QWidget *buildMenuBar();

    void applyTheme(bool force = false);
    void populateSidebar();
    void scheduleSidebarRefresh(); // drives or bookmarks changed
    void showSidebarMenu(const QPoint &pos);
    void ejectDrive(const QString &root, const QString &name);
    void toggleBookmark();
    void updateBookmarkAction();
    void loadSettings();
    void saveSettings();
    // Typing in the file view jumps to a matching name.
    bool typeAheadKey(QKeyEvent *event);
    // Open With: a submenu of the recommended apps, and the full chooser.
    QMenu *openWithMenu(const QStringList &files);
    // Open a file by what it really is (ContentType), warning about programs
    // in disguise instead of opening them.
    void openFile(const QString &path);
    void fixExtension(const QString &path);
    void openWithOther(const QStringList &files);
    void typeAheadFind();

    // Tabs: one shared view/model; each tab keeps its folder, history,
    // selection and scroll position, restored when it becomes active.
    struct TabState
    {
        QString path;
        QStack<QString> back;
        QStack<QString> forward;
        QStringList selection;
        int scroll = 0;
    };
    void openTab(const QString &path, bool activate);
    void closeTab(int index);
    void reopenClosedTab();
    void switchToTab(int index);
    void saveActiveTab();
    void addTabCloseButton(int index);
    void updateTabBar();
    QString tabTitle(const QString &path) const;
    void syncSidebarSelection();

    void goBack();
    void goForward();
    void goUp();
    void openIndex(const QModelIndex &index);
    void setGridMode(bool grid);
    // The Analyze view: disk usage of the open folder's sub-folders, as a
    // tree (DiskUsageModel). File actions are off while it shows.
    void setAnalyzeMode(bool on);
    void showAnalyzeMenu(const QPoint &pos);
    QString selectedAnalyzePath() const;
    void setAnalyzeShowFiles(bool show);
    void setShowHidden(bool show);
    void setSearchActive(bool active);
    void updateFilter();
    void updateActions();

    void setupFileView(QAbstractItemView *view);

    // Drag and drop (drops are handled here, not by the views/model).
    bool handleViewDrag(QAbstractItemView *view, QEvent *event);
    bool handleSidebarDrag(QEvent *event);
    Qt::DropAction dropActionFor(const QStringList &sources, const QString &targetDir,
                                 Qt::KeyboardModifiers modifiers,
                                 Qt::DropActions possible) const;
    void setDropTarget(const QModelIndex &viewIndex);
    QByteArray deviceOf(const QString &path) const;
    QModelIndexList selectedViewIndexes() const; // one index (column 0) per selected item
    bool isFileViewport(const QObject *object) const;
    void sortBy(int column, Qt::SortOrder order);
    void updateSortActions();

    // View (proxy) <-> DirectoryModel indexes.
    QModelIndex toSource(const QModelIndex &viewIndex) const;
    QModelIndex toView(const QModelIndex &sourceIndex) const;

    // File operations
    QStringList selectedPaths() const;
    void rememberOpened(const QStringList &files); // add to the shared Recent list
    // Quick Look: Space shows the current item; it follows the current item
    // while open.
    void toggleQuickLook();
    void updateQuickLook();
    bool quickLookVisible() const;
    QString effectiveMimeAt(const QModelIndex &sourceIndex) const;
    // Zoom the visible view (grid and list have their own size): +1, -1, or 0 = default.
    void zoom(int step);
    void applyZoom();
    QStringList selectedArchives() const; // empty unless every selected item is an archive
    void compressSelection();
    void openSelection();
    void copySelection(bool cut);
    void paste();
    void trashSelection();
    void deleteSelection(); // permanently, after confirming
    void restoreSelection();
    void emptyTrash();
    bool confirmPermanentDelete(const QStringList &paths, bool wholeTrash);
    void performDrop(const QStringList &sources, const QString &targetDir, Qt::DropAction action);
    void updateEditActions();
    void showContextMenu(const QPoint &pos);
    void createNewItem(bool folder);
    // New Document ▸ (files in the Templates folder), rebuilt when opened.
    void fillNewDocumentMenu();
    void createFromTemplate(const QString &templatePath);
    // Edit → Select Items Matching (Ctrl+S) and Invert Selection.
    void selectMatching();
    void invertSelection();
    // F2 with several items selected.
    void batchRename(const QStringList &paths);

    // Embedded terminal (bottom panel). Built only with libvterm.
    QWidget *buildTerminalPanel();
    void showTerminal(const QString &directory);
    void hideTerminal();

    // Inline rename (F2, and right after New Folder / New File).
    void renameSelection();
    void startRename(const QModelIndex &viewIndex);
    bool commitRename();
    void cancelRename();
    void updateRenameGeometry();
    void onJobFinished(FileJob *job);
    // Name clashes: one "Replace?" question at a time (jobs wait meanwhile).
    void askConflict(FileJob *job, const QString &source, const QString &existing);
    void showNextConflict();

    // Highlight `paths` (in the current folder) once the model has them.
    void selectPaths(const QStringList &paths);
    void applyPendingSelection();

    void updateFloatingBar();
    void showMessage(const QString &text);
    void positionFloatingBar();

    // Client-side decorations (frameless window with our own header bars).
    void updateWindowState();
    Qt::Edges edgesAt(const QPoint &pos) const;
    void updateResizeCursor(Qt::Edges edges);

    Theme m_theme;
    QString m_themeId; // empty until the first applyTheme()
    bool m_csd = false;
    bool m_grid = true;
    bool m_showHidden = false;
    bool m_resizeCursor = false;

    FileOperations *m_ops = nullptr;
    OperationsPanel *m_opsPanel = nullptr;
    Toast *m_toast = nullptr;
    DirectoryModel *m_model = nullptr;
    FileFilterProxy *m_proxy = nullptr; // search filter between model and view
    Thumbnailer *m_thumbnailer = nullptr;
    FileItemDelegate *m_gridDelegate = nullptr; // one per view: each caches its
    FileItemDelegate *m_listDelegate = nullptr; // own sizes and layout
    QAbstractItemView *m_view = nullptr; // the visible one of the two below
    QListView *m_iconView = nullptr;      // grid
    QTreeView *m_detailView = nullptr;    // list with columns
    QTreeView *m_analyzeView = nullptr;   // Analyze: folder sizes as a tree
    DiskUsageModel *m_usageModel = nullptr;
    QAction *m_analyzeAct = nullptr;      // Ctrl+3
    QAction *m_analyzeFilesAct = nullptr; // Analyze: also list the biggest files
    QToolButton *m_analyzeButton = nullptr;
    bool m_analyzing = false;
    QStackedWidget *m_viewStack = nullptr;
    QListWidget *m_sidebar = nullptr;
    QWidget *m_root = nullptr;
    QSplitter *m_splitter = nullptr;
    QStackedWidget *m_locationStack = nullptr;
    PathBar *m_pathBar = nullptr;
    QLineEdit *m_searchEdit = nullptr;
    QTimer *m_searchTimer = nullptr;     // debounces typing before searching sub-folders
    QAction *m_openLocationAct = nullptr; // search results: open the folder that holds it
    QLabel *m_titleLabel = nullptr;
    QLabel *m_floatingBar = nullptr;
    QTimer *m_messageTimer = nullptr;
    QString m_message;

    QToolButton *m_backButton = nullptr;
    QToolButton *m_forwardButton = nullptr;
    QToolButton *m_searchButton = nullptr;
    QToolButton *m_menuButton = nullptr;
    QToolButton *m_viewButton = nullptr;
    QToolButton *m_minButton = nullptr;
    QToolButton *m_maxButton = nullptr;
    QToolButton *m_closeButton = nullptr;

    QAction *m_backAct = nullptr;
    QAction *m_forwardAct = nullptr;
    QAction *m_upAct = nullptr;
    QAction *m_hiddenAct = nullptr;
    QAction *m_searchAct = nullptr;
    QAction *m_copyLocationAct = nullptr;
    QAction *m_prefsAct = nullptr;
    QAction *m_aboutAct = nullptr;
    QAction *m_paletteAct = nullptr;   // Ctrl+Shift+P: run any command by name
    QAction *m_connectAct = nullptr;   // Connect to Server…
    QAction *m_organizeAct = nullptr;  // Organize with Claude…
    QAction *m_restoreAllAct = nullptr;  // Trash: Restore All
    QToolButton *m_trashRestoreAllButton = nullptr;
    // Dragging: hold still over a folder and it opens (or expands in place).
    QTimer *m_springTimer = nullptr;
    QString m_springPath;
    int m_springPane = 0;
    void springOver(const QString &folder, int pane);
    // Right-click extras: Send by Email…, Set as Background, Scripts ▸.
    void sendByEmail(const QStringList &paths);
    void setAsBackground(const QString &image);
    QMenu *scriptsMenu(const QStringList &selection);
    void organizeWithClaude();
    void applyOrganizePlan(const QString &folder, const QList<QPair<QString, QPair<QString, QString>>> &moves);
    // Remote locations: mounts with GVfs (asking for logins), then opens the
    // folder. navigateTo() sends addresses (sftp://…) here.
    void connectToServer(const QString &address);
    void chooseShare(const QString &serverAddress);
    QAction *m_searchContentsAct = nullptr; // search also inside files (search/contents)
    QToolButton *m_filterButton = nullptr;  // search filters: type and date
    QActionGroup *m_typeFilters = nullptr;
    QActionGroup *m_dateFilters = nullptr;
    void applySearchFilters();
    class CommandPalette *m_palette = nullptr;
    void showCommandPalette();
    QAction *m_guideAct = nullptr;     // F1: the built-in user guide
    QAction *m_shortcutsAct = nullptr; // Ctrl+?: the guide's shortcuts page
    void showGuide(const QString &topic);
    QAction *m_openAct = nullptr;
    QAction *m_cutAct = nullptr;
    QAction *m_copyAct = nullptr;
    QAction *m_pasteAct = nullptr;
    QAction *m_trashAct = nullptr;
    QAction *m_undoAct = nullptr;
    QAction *m_redoAct = nullptr;         // Ctrl+Shift+Z / Ctrl+Y
    QAction *m_starAct = nullptr;         // Star / Unstar the selection
    QAction *m_previewAct = nullptr;      // Quick Look (Space, handled in eventFilter)
    QuickLook *m_quickLook = nullptr;     // made on first use
    QAction *m_removeRecentAct = nullptr; // Recent: take items off the list
    // Recent is shown newest first; the folder sort comes back when leaving.
    bool m_recentSort = false;
    int m_sortColumnBefore = 0;
    Qt::SortOrder m_sortOrderBefore = Qt::AscendingOrder;
    QAction *m_zoomInAct = nullptr;       // Ctrl++ / Ctrl+= / Ctrl+wheel
    QAction *m_zoomOutAct = nullptr;      // Ctrl+-
    QAction *m_zoomResetAct = nullptr;    // Ctrl+0
    int m_gridZoom = 1;                   // index into FileItemDelegate::gridSizes()
    int m_listZoom = 1;                   // index into FileItemDelegate::listSizes()
    int m_wheelZoom = 0;                  // Ctrl+wheel delta not used yet (touchpads)
    QAction *m_homeAct = nullptr;
    QAction *m_locationAct = nullptr;
    QAction *m_gridAct = nullptr;
    QAction *m_listAct = nullptr;
    QAction *m_newFolderAct = nullptr;
    QAction *m_newFileAct = nullptr;
    QAction *m_renameAct = nullptr;
    QAction *m_selectAllAct = nullptr;
    QAction *m_selectMatchingAct = nullptr;
    QAction *m_invertSelectionAct = nullptr;
    QMenu *m_newDocumentMenu = nullptr;
    QString m_lastPattern; // Select Items Matching remembers it
    QAction *m_closeAct = nullptr;
    QAction *m_quitAct = nullptr;
    QAction *m_sidebarAct = nullptr;
    QList<QAction *> m_sortColumnActs; // by DirectoryModel::Column
    QAction *m_sortAscAct = nullptr;
    QAction *m_sortDescAct = nullptr;

    struct PendingConflict
    {
        QPointer<FileJob> job;
        QString source;
        QString existing;
    };
    QList<PendingConflict> m_conflicts;
    bool m_conflictOpen = false;

    QString m_typeAhead;
    QElapsedTimer m_typeAheadTimer;
    QList<TabState> m_tabs;
    QList<TabState> m_closedTabs; // for Ctrl+Shift+T
    int m_activeTab = 0;
    bool m_switchingTabs = false; // ignore QTabBar signals while we edit it
    int m_pendingScroll = -1;     // restore a tab's scroll position once loaded
    QTabBar *m_tabBar = nullptr;
    QAction *m_newTabAct = nullptr;
    QAction *m_closeWindowAct = nullptr;
    QAction *m_reopenTabAct = nullptr;
    QAction *m_openInNewTabAct = nullptr;

    QAction *m_propertiesAct = nullptr; // Ctrl+I / Alt+Enter
    QAction *m_compressAct = nullptr;     // archives (when built with libarchive)
    QAction *m_extractHereAct = nullptr;
    QAction *m_extractToAct = nullptr;
    QAction *m_bookmarkAct = nullptr;   // Ctrl+D: bookmark / unbookmark this folder
    QTimer *m_sidebarRefresh = nullptr; // debounces drive/bookmark changes
    int m_mountsFd = -1;                // /proc/self/mounts (Linux): POLLPRI on change
    QStringList m_lastMounts;           // other platforms: polled
    int m_terminalHeight = 0;           // remembered between sessions

    QAction *m_deleteAct = nullptr;     // Delete Permanently…
    QAction *m_restoreAct = nullptr;    // Restore From Trash
    QAction *m_emptyTrashAct = nullptr; // Empty Trash…
    QWidget *m_trashBar = nullptr;      // shown above the files in the Trash
    QToolButton *m_trashRestoreButton = nullptr;
    QToolButton *m_trashEmptyButton = nullptr;
    QAction *m_terminalAct = nullptr;        // null if built without the terminal
    QAction *m_openTerminalHereAct = nullptr;
    QString m_contextFolder; // folder the context menu's "Open in Terminal" uses
    QSplitter *m_contentSplitter = nullptr;  // file views above, terminal below
    QWidget *m_terminalPanel = nullptr;
    TerminalWidget *m_terminal = nullptr;
    QLabel *m_terminalTitle = nullptr;
    QToolButton *m_terminalButton = nullptr;
    QToolButton *m_terminalCloseButton = nullptr;
    bool m_terminalSized = false;

    QMenuBar *m_menuBar = nullptr;
    QPoint m_menuPressPos; // dragging the window by empty menu bar space
    bool m_menuPressed = false;
    QWidget *m_sidebarPane = nullptr;

    QTimer *m_dragScrollTimer = nullptr; // auto-scroll near the view's edges
    int m_dragScrollStep = 0;
    mutable QHash<QString, QByteArray> m_deviceCache; // folder -> storage device

    QLineEdit *m_renameEdit = nullptr;
    QString m_renamePath;
    QString m_pendingRename; // start renaming this once it appears
    bool m_committingRename = false;

    QStack<QString> m_backHistory;
    QStack<QString> m_forwardHistory;
    QString m_current;

    QStringList m_pendingSelection;
    QElapsedTimer m_pendingSince;
    bool m_applyingSelection = false;

    // ----------------------------------------------------- split view ---
    // Each file pane has its own folder, history, model, views and state.
    // The members above (m_model, m_view, m_current…) always belong to the
    // active pane: activatePane() switches them for good, PaneScope only
    // while handling something that happened in the other pane.
    struct Pane
    {
        // Widgets (made once by buildPane; never change).
        DirectoryModel *model = nullptr;
        FileFilterProxy *proxy = nullptr;
        FileItemDelegate *gridDelegate = nullptr;
        FileItemDelegate *listDelegate = nullptr;
        QListView *iconView = nullptr;
        QTreeView *detailView = nullptr;
        QStackedWidget *viewStack = nullptr;
        QLabel *floatingBar = nullptr;
        QTimer *messageTimer = nullptr;
        QWidget *container = nullptr; // caption + views, in m_paneSplitter
        QLabel *caption = nullptr;    // split view: the pane's folder
        // State.
        QAbstractItemView *view = nullptr;
        QString message;
        QString current;
        QStack<QString> back, forward;
        bool grid = true;
        int gridZoom = 1, listZoom = 1;
        QStringList pendingSelection;
        QElapsedTimer pendingSince;
        int pendingScroll = -1;
        QString pendingRename;
        bool recentSort = false;
        int sortColumnBefore = 0;
        Qt::SortOrder sortOrderBefore = Qt::AscendingOrder;
    };
    class PaneScope; // switches to a pane temporarily (see MainWindow.cpp)
    friend class PaneScope;
    Pane m_panes[2];
    int m_paneCount = 0;  // 1, or 2 once the split view was opened
    int m_activePane = 0;
    int m_background = 0; // > 0 while working on a pane that isn't the user's
    bool m_split = false;
    QSplitter *m_paneSplitter = nullptr;
    QAction *m_splitAct = nullptr;        // F3
    QAction *m_copyToOtherAct = nullptr;  // F5
    QAction *m_moveToOtherAct = nullptr;  // F6
    QToolButton *m_splitButton = nullptr;
    QPointer<QAbstractItemView> m_dragScrollView;

    QWidget *buildPane(QWidget *parent, int index);
    void savePane(int index);
    void loadPane(int index);
    void activatePane(int index);
    int paneOf(const QObject *object) const;
    void refreshChrome();     // header, title, sidebar, actions: for the active pane
    void updateViewChrome();  // grid/list button and zoom limits
    void updatePaneCaptions();
    void fitListColumns(); // narrow panes drop Type, then Modified, so names have room
    // Owner / Group / Permissions: hidden unless chosen in the list
    // header's right-click menu (saved as view/listColumns, for both panes).
    void applyListColumns();
    void showListHeaderMenu(QHeaderView *header, const QPoint &pos);
    QStringList m_listColumns;
    // Expandable folders in the list view (Preferences, view/expandFolders).
    // Only the list shows them: switching to the grid closes every folder.
    bool m_expandFolders = false;
    void applyExpandFolders(); // to every pane's model
    void updateGitBadge();     // the branch pill in the path bar
    void refreshGitAll();      // every pane: files may have changed elsewhere
    bool handleTreeKey(QKeyEvent *key);
public:
    // Preferences: turn expandable folders on or off in every window.
    static void setExpandFoldersEverywhere(bool on);
    // Preferences: git badges turned on or off.
    static void resetGitEverywhere();
private:
    // Rename, move and trash need a folder you can change: checked exactly,
    // once per folder of the selected items.
    bool canChangeSelection() const;
    QString titleFor(const QString &path) const;
    void setSplitView(bool on);
    void ensureSecondPane();
    QString otherPaneFolder() const; // where F5/F6 copy and move to, or empty
    void transferToOtherPane(bool move);

#if defined(ARGONAUT_DBUS)
    // Open/Save window (startChooser).
    void chooserAccept();
    void finishChooser(uint response, const QStringList &paths = {});
    void updateChooser(); // selection changed: the name field, the accept button
    void keepChooserActionsOff();
    QString chooserFolderKey() const;
    std::optional<ChooserRequest> m_chooser;
    ChooserBar *m_chooserBar = nullptr;
    bool m_chooserDone = false;
    bool m_chooserAsking = false; // the "Replace?" question is open
    QList<QAction *> m_chooserOff; // commands an Open/Save window doesn't offer
    void attachToChooserParent();
    quint64 m_chooserParentId = 0; // the asking app's X11 window, or 0
#endif
    QVBoxLayout *m_contentLayout = nullptr;
    QWidget *m_menuRow = nullptr; // the menu bar's row; drags the window like the bar
};
