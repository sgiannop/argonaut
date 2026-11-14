#include "HelpDialog.h"

#include "DirectoryModel.h"
#include "FileJob.h"
#include "Theme.h"

#include <QDesktopServices>
#include <QEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QRegularExpression>
#include <QTextBrowser>
#include <QTextDocument>
#include <QUrl>
#include <QVBoxLayout>

namespace {

// "Ctrl+Shift+N" -> key caps. "Ctrl++" is Ctrl and "+".
QString keyCaps(const QString &combo)
{
    QStringList parts = combo.split(QLatin1Char('+'));
    QStringList keys;
    for (int i = 0; i < parts.size(); ++i) {
        if (parts[i].isEmpty()) { // "Ctrl++": the key is "+"
            keys << QStringLiteral("+");
            ++i;
            continue;
        }
        keys << parts[i];
    }
    QStringList caps;
    for (const QString &k : keys)
        caps << QStringLiteral("<span class=\"key\">&nbsp;%1&nbsp;</span>").arg(k.toHtmlEscaped());
    return caps.join(QStringLiteral("<span class=\"plus\"> + </span>"));
}

QString keyCaps(const QKeySequence &sequence)
{
    return keyCaps(sequence.toString(QKeySequence::NativeText));
}

// [[Ctrl+F]] in the guide's text -> key caps.
QString expandKeys(QString html)
{
    static const QRegularExpression re(QStringLiteral(R"(\[\[(.+?)\]\])"));
    QString out;
    qsizetype last = 0;
    auto it = re.globalMatch(html);
    while (it.hasNext()) {
        const auto m = it.next();
        out += html.mid(last, m.capturedStart() - last);
        out += keyCaps(m.captured(1));
        last = m.capturedEnd();
    }
    out += html.mid(last);
    return out;
}

// Rich text can't draw translucent colours (it drops the alpha and paints
// black), and some theme tokens are translucent: blend them first.
QString solid(const QColor &color, const QColor &under)
{
    const qreal a = color.alphaF();
    return QColor::fromRgbF(color.redF() * a + under.redF() * (1 - a), color.greenF() * a + under.greenF() * (1 - a),
                            color.blueF() * a + under.blueF() * (1 - a))
        .name();
}

} // namespace

HelpDialog::HelpDialog(const QList<ShortcutGroup> &shortcuts, QWidget *parent)
    : QDialog(parent), m_shortcuts(shortcuts)
{
    setObjectName("helpDialog");
    setWindowTitle(tr("Argonaut User Guide"));
    setAttribute(Qt::WA_DeleteOnClose);

    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto *side = new QWidget(this);
    side->setObjectName("helpSide");
    side->setFixedWidth(250);
    auto *sideLayout = new QVBoxLayout(side);
    sideLayout->setContentsMargins(12, 14, 12, 12);
    sideLayout->setSpacing(10);
    auto *title = new QLabel(tr("User Guide"), side);
    title->setObjectName("helpTitle");
    sideLayout->addWidget(title);
    m_search = new QLineEdit(side);
    m_search->setObjectName("helpSearch");
    m_search->setPlaceholderText(tr("Search the guide"));
    m_search->setClearButtonEnabled(true);
    sideLayout->addWidget(m_search);
    m_list = new QListWidget(side);
    m_list->setObjectName("helpTopics");
    m_list->setFrameShape(QFrame::NoFrame);
    m_list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_list->setTextElideMode(Qt::ElideRight);
    sideLayout->addWidget(m_list, 1);
    m_noMatch = new QLabel(tr("Nothing found."), side);
    m_noMatch->setObjectName("helpNoMatch");
    m_noMatch->hide();
    sideLayout->addWidget(m_noMatch);
    layout->addWidget(side);

    m_page = new QTextBrowser(this);
    m_page->setObjectName("helpPage");
    m_page->setFrameShape(QFrame::NoFrame);
    m_page->setOpenLinks(false);
    m_page->document()->setDocumentMargin(28);
    layout->addWidget(m_page, 1);

    buildTopics();
    for (const Topic &topic : std::as_const(m_topics)) {
        auto *item = new QListWidgetItem(topic.title, m_list);
        item->setData(Qt::UserRole, topic.id);
    }

    connect(m_list, &QListWidget::currentRowChanged, this, [this] { render(); });
    connect(m_search, &QLineEdit::textChanged, this, &HelpDialog::applySearch);
    connect(m_page, &QTextBrowser::anchorClicked, this, [this](const QUrl &url) {
        if (url.scheme() == QLatin1String("topic"))
            showTopic(url.path().isEmpty() ? url.host() : url.path());
        else
            QDesktopServices::openUrl(url);
    });

    m_list->setCurrentRow(0);
    resize(900, 640);
}

void HelpDialog::showTopic(const QString &id)
{
    if (!m_search->text().isEmpty())
        m_search->clear(); // shows every topic again
    for (int row = 0; row < m_list->count(); ++row) {
        if (m_list->item(row)->data(Qt::UserRole).toString() == id) {
            m_list->setCurrentRow(row);
            return;
        }
    }
}

void HelpDialog::changeEvent(QEvent *event)
{
    QDialog::changeEvent(event);
    if (event->type() == QEvent::ApplicationPaletteChange || event->type() == QEvent::PaletteChange)
        render(); // the page's colours come from the theme
}

void HelpDialog::render()
{
    QListWidgetItem *item = m_list->currentItem();
    if (!item || item->isHidden()) {
        m_page->clear();
        return;
    }
    const QString id = item->data(Qt::UserRole).toString();
    for (const Topic &topic : std::as_const(m_topics)) {
        if (topic.id != id)
            continue;
        const Theme t = currentTheme();
        m_page->document()->setDefaultStyleSheet(QStringLiteral(R"(
            body { color: %1; font-size: 10.5pt; }
            h1 { font-size: 20pt; font-weight: bold; margin-bottom: 4px; }
            h2 { font-size: 13pt; font-weight: bold; margin-top: 22px; margin-bottom: 6px; }
            p, li { line-height: 140%; }
            p.lead { color: %2; font-size: 11.5pt; margin-bottom: 14px; }
            a { color: %3; text-decoration: none; }
            .key { background-color: %4; color: %1; font-family: monospace; font-weight: bold; }
            .plus { color: %2; }
            .note { color: %2; }
            .tip { background-color: %5; }
            td.group { font-weight: bold; color: %3; padding-top: 16px; }
            td.keys { white-space: nowrap; }
            tr.odd { background-color: %5; }
        )")
                                                     .arg(solid(t.fg, t.window), solid(t.dimFg, t.window),
                                                          solid(t.accent, t.window), solid(t.pathBg, t.window),
                                                          solid(t.sidebar, t.window)));
        m_page->setHtml(pageHtml(topic));
        break;
    }
    applySearch();
}

QString HelpDialog::pageHtml(const Topic &topic) const
{
    QString body = topic.id == QLatin1String("shortcuts") ? shortcutsHtml() : expandKeys(topic.html);
    return QStringLiteral("<html><body><h1>%1</h1>%2</body></html>").arg(topic.title.toHtmlEscaped(), body);
}

QString HelpDialog::shortcutsHtml() const
{
    QString html = expandKeys(tr("<p class=\"lead\">Every shortcut, taken from the menus themselves. "
                                 "Most commands are also in the menu bar and the right-click menu.</p>"));
    html += QStringLiteral("<table width=\"100%\" cellspacing=\"0\" cellpadding=\"6\">");
    for (const ShortcutGroup &group : m_shortcuts) {
        html += QStringLiteral("<tr><td class=\"group\" colspan=\"2\">%1</td></tr>").arg(group.title.toHtmlEscaped());
        int n = 0;
        for (const Shortcut &s : group.shortcuts) {
            QStringList keys;
            for (const QKeySequence &k : s.keys) {
                if (!k.isEmpty())
                    keys << keyCaps(k);
            }
            QString shown = keys.join(QStringLiteral(" <span class=\"note\">%1</span> ").arg(tr("or")));
            if (!s.note.isEmpty())
                shown = expandKeys(s.note);
            if (shown.isEmpty())
                continue;
            html += QStringLiteral("<tr class=\"%1\"><td>%2</td><td class=\"keys\">%3</td></tr>")
                        .arg(n++ % 2 ? QStringLiteral("odd") : QStringLiteral("even"), s.action.toHtmlEscaped(), shown);
        }
    }
    html += QStringLiteral("</table>");
    return html;
}

void HelpDialog::applySearch()
{
    const QString query = m_search->text().trimmed();
    // Show only the topics that mention every word (accents and case don't matter).
    const QStringList words = DirectoryModel::foldForSearch(query).split(QLatin1Char(' '), Qt::SkipEmptyParts);
    int shown = 0;
    int firstShown = -1;
    for (int row = 0; row < m_list->count(); ++row) {
        QListWidgetItem *item = m_list->item(row);
        bool match = true;
        if (!words.isEmpty()) {
            const Topic &topic = m_topics[row];
            QTextDocument doc;
            doc.setHtml(topic.id == QLatin1String("shortcuts") ? shortcutsHtml() : topic.html);
            const QString text = DirectoryModel::foldForSearch(topic.title + QLatin1Char(' ') + doc.toPlainText());
            for (const QString &w : words)
                match = match && text.contains(w);
        }
        item->setHidden(!match);
        if (match) {
            ++shown;
            if (firstShown < 0)
                firstShown = row;
        }
    }
    m_noMatch->setVisible(shown == 0);
    QListWidgetItem *current = m_list->currentItem();
    if ((!current || current->isHidden()) && firstShown >= 0) {
        m_list->setCurrentRow(firstShown); // renders, then comes back here
        return;
    }
    if (shown == 0) {
        m_page->clear();
        return;
    }

    // Highlight the words on the page.
    QList<QTextEdit::ExtraSelection> marks;
    if (!words.isEmpty()) {
        const Theme t = currentTheme();
        QTextCharFormat format;
        QColor mark = t.accent;
        mark.setAlpha(80);
        format.setBackground(mark);
        const QStringList raw = query.split(QLatin1Char(' '), Qt::SkipEmptyParts);
        for (const QString &w : raw) {
            QTextCursor cursor(m_page->document());
            while (true) {
                cursor = m_page->document()->find(w, cursor);
                if (cursor.isNull())
                    break;
                marks.append({cursor, format});
            }
        }
        if (!marks.isEmpty()) { // scroll to the first match
            QTextCursor first = marks.first().cursor;
            first.clearSelection();
            m_page->setTextCursor(first);
            m_page->ensureCursorVisible();
        }
    }
    m_page->setExtraSelections(marks);
}

void HelpDialog::buildTopics()
{
    auto add = [this](const char *id, const QString &title, const QString &html) {
        m_topics.append({QString::fromLatin1(id), title, html});
    };

    add("start", tr("Getting Started"), tr(R"(
<p class="lead">Argonaut is a fast file manager. It works like GNOME Files (Nautilus), shares its
bookmarks, recent files, thumbnails and Trash, and adds a few things of its own.</p>
<h2>The window</h2>
<ul>
<li><b>Sidebar</b> (left, [[F9]] shows or hides it): Recent, Starred, your folders, bookmarks, drives and the Trash.</li>
<li><b>Path bar</b> (top): click a part of the path to go there. Click empty space in it, or press [[Ctrl+L]], to type a path.</li>
<li><b>Files</b>: as a grid with thumbnails ([[Ctrl+1]]) or a list with columns ([[Ctrl+2]]).</li>
<li><b>Floating bar</b> (bottom right): what's selected, its size, and short messages.</li>
</ul>
<h2>Moving around</h2>
<ul>
<li>Double-click a folder, or select it and press [[Enter]].</li>
<li>Back and Forward: [[Alt+Left]] / [[Alt+Right]], or the mouse's side buttons. Up one folder: [[Alt+Up]]. Home: [[Alt+Home]].</li>
<li>Type the start of a name to jump to it.</li>
</ul>
<h2>Tabs and windows</h2>
<ul>
<li>[[Ctrl+T]] opens a tab, [[Ctrl+W]] closes it, [[Ctrl+Shift+T]] brings back the last one you closed.</li>
<li>Middle-click a folder or a sidebar entry to open it in a new tab in the background.</li>
<li>[[Ctrl+Tab]] goes to the next tab and [[Alt+1]]… [[Alt+9]] to a given one.</li>
</ul>
<h2>Any command by name</h2>
<p>Press [[Ctrl+Shift+P]] and type a few letters of what you want: “hid” for Show Hidden Files,
“nf” for New Folder, “sort size”. [[Enter]] runs it, [[Esc]] closes. The commands you used last come first.</p>
<p>See also: <a href="topic:views">Views and sorting</a>, <a href="topic:shortcuts">Keyboard shortcuts</a>.</p>
)"));

    add("views", tr("Views, Sorting and Previews"), tr(R"(
<h2>Grid and list</h2>
<p>[[Ctrl+1]] shows a grid, [[Ctrl+2]] a list. Zoom with [[Ctrl++]] and [[Ctrl+-]], or hold [[Ctrl]]
and turn the mouse wheel. [[Ctrl+0]] goes back to the normal size. Each view remembers its own size.</p>
<h2>Sorting</h2>
<p>Click a column header in the list, or use <b>View → Sort</b>. Folders always come first, and names
are in natural order, so “file2” comes before “file10”.</p>
<h2>Opening folders in place</h2>
<p>Turn on <b>Expandable folders</b> in Preferences ([[Ctrl+,]]) to see an arrow before each folder in the list.
Click it, or press [[Right]], to show the folder's items right below it; [[Left]] closes it again, or goes
to the folder an item is in. You can copy, move, rename and preview items there as anywhere else.
Search, the Trash, Recent and Starred always show a plain list.</p>
<h2>Columns</h2>
<p>The list shows Name, Size, Type and Modified. Right-click the column header to add <b>Owner</b>,
<b>Group</b> and <b>Permissions</b>.</p>
<h2>Hidden files</h2>
<p>[[Ctrl+H]] shows or hides dot-files and backups (names ending in ~).</p>
<h2>Quick Look</h2>
<p>Select a file and press [[Space]] for a preview without opening an app: photos, PDFs, videos, fonts,
text and code, what's inside archives, and folders. The arrow keys move to the next file, [[Enter]]
opens it, and [[Space]] or [[Esc]] closes the preview.</p>
<h2>Split view</h2>
<p>[[F3]] shows two folders side by side. [[F5]] copies the selection to the other side and [[F6]]
moves it; you can also drag between them. Every shortcut acts on the side you're working in.</p>
)"));

    add("select", tr("Selecting and Renaming"), tr(R"(
<h2>Selecting</h2>
<ul>
<li>Click to select, [[Ctrl]]+click to add or remove one item, [[Shift]]+click for a range, or drag a box around items.</li>
<li>[[Ctrl+A]] selects everything, [[Ctrl+Shift+I]] inverts the selection.</li>
<li><b>Select Items Matching</b> ([[Ctrl+S]]): type a pattern such as <code>*.jpg</code> or <code>IMG_20??*</code>.
Upper and lower case don't matter, and text without <code>*</code> or <code>?</code> selects every name that contains it.</li>
</ul>
<h2>Renaming one item</h2>
<p>Press [[F2]] and type. The name is selected without its extension, so you can just type the new name.
[[Enter]] keeps it and [[Esc]] cancels.</p>
<h2>Renaming many items</h2>
<p>Select several items and press [[F2]]:</p>
<ul>
<li><b>Rename using a template</b>: build the name from <code>[Original name]</code> and a number:
<code>[1]</code>, <code>[01]</code> or <code>[001]</code>. Choose the first number and the order (by name or by date).</li>
<li><b>Find and replace text</b>: change part of every name, with or without matching case.</li>
</ul>
<p>Extensions are always kept. The preview shows every new name before anything changes, and a name
that can't be used is shown in red with the reason. One [[Ctrl+Z]] undoes the whole batch.</p>
<h2>New items</h2>
<p>[[Ctrl+Shift+N]] makes a folder and [[Ctrl+Alt+N]] an empty file. <b>File → New Document</b> lists the
files in your Templates folder; put any document there (a letter, a spreadsheet) to reuse it as a starting point.</p>
)"));

    add("files", tr("Copying, Moving and Undo"), tr(R"(
<h2>Copy, cut and paste</h2>
<p>[[Ctrl+C]], [[Ctrl+X]] and [[Ctrl+V]] work as usual, also with files copied in other file managers.</p>
<h2>Drag and drop</h2>
<p>Dropping on the same drive moves, onto another drive copies. Hold [[Ctrl]] to copy or [[Shift]] to move.
Drop onto a folder, a tab, a sidebar entry or the Trash. Hold a dragged item still over a folder (or a sidebar
entry) for a moment and it opens, so you can go deeper before dropping.</p>
<h2>Nothing is ever overwritten</h2>
<p>When a name is taken, Argonaut asks: <b>Replace</b> (the old item goes to the Trash, it isn't deleted),
<b>Skip</b>, <b>Keep Both</b> or, for folders, <b>Merge</b>. Copies inside the same folder become “name (copy)”.</p>
<h2>Progress</h2>
<p>Long operations show a card at the bottom left with the time left and a Cancel button. A cancelled
copy leaves nothing half-finished behind.</p>
<h2>USB sticks</h2>
<p>On removable drives Argonaut says “done” only after the data has really been written to the drive,
so it's safe to unplug it then. Use the ⏏ button in the sidebar to eject it.</p>
<h2>Undo and redo</h2>
<p>[[Ctrl+Z]] undoes copies, moves, trashing, renames, new items, extracting and compressing.
[[Ctrl+Shift+Z]] or [[Ctrl+Y]] redoes. Undoing a copy moves the copies to the Trash; nothing is deleted for good.</p>
<h2>Items you can't change</h2>
<p>Files and folders you aren't allowed to change show a 🔒 badge. If their folder doesn't let you change it,
Cut, Move to Trash, Rename and Delete are greyed out; you can still copy them.</p>
)"));

    add("search", tr("Searching"), tr(R"(
<p>Press [[Ctrl+F]] and type. Argonaut searches the open folder <b>and all its sub-folders</b>, and
results appear as you type.</p>
<ul>
<li>Several words narrow the search down: “invoice 2024” finds names that contain both.</li>
<li>Case and accents don't matter: “cafe” finds “Café”, “θαλασσα” finds “Θάλασσα”.</li>
<li>The Location column (or the tooltip in the grid) shows where each result is. Right-click →
<b>Open Item Location</b> goes there.</li>
<li>Hidden folders are searched only while hidden files are shown. Other drives and links aren't followed.</li>
<li>[[Esc]] or clearing the field shows the folder again.</li>
</ul>
<h2>Filters</h2>
<p><b>Filters</b> next to the search field narrows results by type (folders, documents, images, audio, video,
archives, text and code) and by when they changed. Filters also work without typing anything: “all images changed
in the last 7 days, anywhere below here”. They're cleared when the search closes.</p>
<h2>Inside files</h2>
<p>Click <b>Inside files</b> next to the search field (or View → Search Inside Files) to also find files by
the words in them. The Location column then shows the text around the match.</p>
<ul>
<li>Text files (notes, code, csv, json…) are read wherever you search, on this computer's own disks.</li>
<li>Documents such as PDFs and office files are found through GNOME's search index, in the folders it
indexes (Settings → Search → Search Locations). There, upper and lower case don't matter, but accents do.</li>
</ul>
)"));

    QString places = tr(R"(
<p class="lead">Recent, Starred, bookmarks, drives and the Trash: the sidebar's places.</p>
<h2>Recent</h2>
<p>What you opened lately, in Argonaut or in any other app, newest first. Right-click → <b>Remove from Recent</b>.
If you turned off “remember recent files” in GNOME's privacy settings, Argonaut doesn't add to it.</p>
<h2>Starred</h2>
<p>Right-click → <b>Star</b> marks a file or folder with ★ and lists it under Starred. Stars follow items
when you rename or move them.</p>
<h2>Bookmarks</h2>
<p>[[Ctrl+D]] bookmarks the open folder (press it again to remove it). You can also drop a folder on empty
space in the sidebar. Right-click a bookmark to rename or remove it. Bookmarks are shared with GNOME Files.</p>
<h2>Servers</h2>
<p><b>Connect to Server…</b> (in the sidebar and the Go menu) opens folders on other computers:
<code>sftp://me@example.com</code> (SSH), <code>smb://nas/photos</code> (Windows and NAS shares),
<code>ftp://…</code> and <code>davs://…</code> (WebDAV). You can also type an address in the path bar ([[Ctrl+L]]).
Argonaut asks for a login when the server needs one; <code>smb://server</code> on its own lists its shared folders.</p>
<p>Connected servers appear under the drives; the ⏏ button disconnects. Copying, moving, renaming and
previews work there like anywhere else. Servers have no Trash, so deleting there is permanent (Argonaut asks first).</p>
<h2>Drives</h2>
<p>Plugged-in drives appear in the sidebar. The ⏏ button ejects one safely. While a copy to or from the drive is still running, Argonaut asks you to wait.</p>
)");
#if defined(Q_OS_LINUX)
    places += tr(R"(
<h2>Trash</h2>
<p>[[Delete]] moves items to the Trash, and [[Shift+Delete]] deletes them for good after asking. In the Trash,
<b>Restore</b> puts items back where they came from, <b>Restore All</b> does that for everything, and
<b>Empty Trash</b> deletes everything. The Trash shows
items from every drive, trashed by any app.</p>
)");
#endif
    add("places", tr("Places and Drives"), places);

    if (FileJob::archivesSupported()) {
        add("archives", tr("Archives"), tr(R"(
<h2>Extracting</h2>
<p>Right-click an archive → <b>Extract Here</b> or <b>Extract to…</b>. Argonaut opens zip, 7z, tar.gz/xz/bz2/zst,
rar, iso, deb and more. One item inside comes out as it is; several go into a folder named after the archive.</p>
<p>It's safe: nothing is overwritten, and entries that try to write outside the folder are skipped and reported.
Password-protected archives aren't supported yet.</p>
<h2>Compressing</h2>
<p>Select items and choose <b>Compress…</b>. Pick .zip (works everywhere), .tar.xz (smaller), .tar.gz (fast)
or .7z (smallest). Both directions show progress, can be cancelled and can be undone.</p>
<h2>Looking inside</h2>
<p>[[Space]] on an archive lists what's in it without extracting anything.</p>
)"));
    }

    add("extras", tr("Email, Background and Scripts"), tr(R"(
<ul>
<li><b>Send by Email…</b> (right-click files) opens your email app with the files attached; you write and send
the message there. Folders can't be attached: compress them first.</li>
<li><b>Set as Background</b> (right-click a picture) copies it into your backgrounds folder and puts it on the desktop.</li>
<li><b>Scripts ▸</b> lists the programs in <code>~/.local/share/nautilus/scripts</code>, the same folder GNOME Files
uses, and runs them on the selected items with the same variables (<code>NAUTILUS_SCRIPT_SELECTED_FILE_PATHS</code>…),
so existing scripts work unchanged. Make a script executable to see it there.</li>
</ul>
)"));

    add("types", tr("Real File Types and Safety"), tr(R"(
<p class="lead">Most file managers trust a file's name. Argonaut also looks at what's inside.</p>
<ul>
<li>An <b>amber ! badge</b> means the content doesn't match the name, for example a “.pdf” that is really a web page
from a failed download. Hover over it for details. Right-click → <b>Rename to “…”</b> fixes the name.</li>
<li>A <b>red ! badge</b> means a <b>program disguised as a document</b>. Argonaut warns you and never runs it.</li>
<li>Files open with the app for what they really are, so a mislabelled file still opens correctly.</li>
<li><b>Properties</b> ([[Ctrl+I]]) shows both what the name says and what the file is, and for photos, music,
videos and PDFs their details: size in pixels, camera and exposure, date taken, length, artist, pages…</li>
</ul>
<h2>Badges</h2>
<ul>
<li>★ top right: starred.</li>
<li>🔒 top left: you can't change this item.</li>
<li>↗ bottom left: a link to another file or folder. The tooltip says where it points.</li>
</ul>
<p>Checking types doesn't slow anything down: folders appear at once from the names, the check runs in the
background, and each file is read only once until it changes.</p>
)"));

    add("analyze", tr("Disk Usage"), tr(R"(
<p>[[Ctrl+3]] (or the pie button) shows how much space every folder uses, as a tree with bars, biggest first.
Folders appear while they're measured.</p>
<ul>
<li>Sizes are space on disk, exactly like <code>du</code>; hard links count once, and other drives aren't included.</li>
<li><b>View → Show Files When Analyzing</b> also lists the biggest files in each folder.</li>
<li>Double-click a folder to look inside it; that's instant within what's been measured.</li>
<li>Trashing, moving or copying updates the sizes at once. <b>Scan Again</b> picks up changes made by other programs.</li>
<li>[[Ctrl+1]] or [[Ctrl+2]] goes back to the files.</li>
</ul>
)"));

    add("organize", tr("Organizing with Claude"), tr(R"(
<p class="lead">Claude, Anthropic's AI, can propose a tidy structure for a messy folder and tell you what's in it.</p>
<h2>Before the first time</h2>
<p>Add your Anthropic API key in <b>Preferences → Claude</b> (get one in the Claude Console) and press <b>Test</b>;
that check is free. The key is kept in your keyring.</p>
<h2>Organizing a folder</h2>
<ol>
<li>Open the folder and choose <b>File → Organize with Claude…</b> (or right-click empty space).</li>
<li>Optionally write your wishes (“invoices by year”) and choose what Claude may see. You see the estimated cost
before anything is sent; nothing is sent until you press <b>Start</b>.</li>
<li>When names say too little, Claude may ask to read the beginning of a few files: you choose
<b>Allow</b> or <b>Don't share</b>. Programs are never read.</li>
<li>Read the report, untick any change you don't want, and press <b>Apply</b>.</li>
</ol>
<p>Claude never touches your files: it only proposes. Argonaut creates the folders and moves and renames the items
you kept, never overwriting anything. [[Ctrl+Z]] puts everything back (once for the moves, once more for the new folders).</p>
<h2>Privacy and cost</h2>
<p>What is sent: the names, sizes, dates and types of the folder's items (up to 500), and only if allowed, the beginning
of some files (some text, the first pages of a PDF, a small preview of an image). <b>Preferences → Claude</b> sets the
default (names only, ask, or read when needed) and the model, and shows what you've spent.</p>
)"));

    add("git", tr("Git Repositories"), tr(R"(
<p>Inside a Git repository, Argonaut shows what changed, like a code editor does.</p>
<ul>
<li>A letter after the name in the list (a small tag on the icon in the grid):
<b>M</b> changed, <b>A</b> added, <b>U</b> new and not added yet, <b>C</b> a merge conflict.</li>
<li>A dot on a folder means something inside it changed.</li>
<li>The path bar shows the branch and how many items changed. Hover over it for the repository's folder.</li>
<li>It updates when files change, after you run <code>git add</code> or <code>git commit</code>, and when you come back to the window.</li>
</ul>
<p>For safety, a repository whose own settings would make Git run programs (for example one you downloaded)
is not checked; the path bar then says “git: off”. Very big repositories that take too long are skipped too.
Turn the badges off in Preferences if you don't want them.</p>
)"));

#if defined(ARGONAUT_TERMINAL)
    add("terminal", tr("Terminal"), tr(R"(
<p>[[Ctrl+`]] opens a terminal under the files, in the open folder. When you go to another folder, the
terminal follows, but only while the shell is waiting for a command, so a running program is never interrupted.</p>
<ul>
<li>Right-click a folder → <b>Open in Terminal</b> opens it there.</li>
<li>Shortcuts like [[Ctrl+C]] go to the terminal while it has the focus.</li>
<li>Typing <code>exit</code> closes it; opening it again starts a new shell.</li>
</ul>
)"));
#endif

    QString prefs = tr(R"(
<h2>Themes</h2>
<p><b>Edit → Preferences</b> ([[Ctrl+,]]) has seven themes: Follow System, Light, Dark, Midnight Blue,
Rosé Dawn, Forest and Sunset.</p>
<h2>Thumbnails</h2>
<p>Previews for PDFs, videos and more are made by the thumbnailers installed on your system, in a sandbox
with no network access. By default they're made only for files on the computer's own disks, because
reading files over USB or the network is slow; Preferences can change that.</p>
)");
#if defined(Q_OS_LINUX)
    prefs += tr(R"(
<h2>Default file manager</h2>
<p>Preferences can make Argonaut open your folders everywhere: from other apps, “Show in folder” in browsers,
and the Super+E key. The same button switches back to GNOME Files.</p>
)");
#endif
#if defined(ARGONAUT_DBUS)
    prefs += tr(R"(
<h2>Open and Save windows</h2>
<p>When an app such as Firefox, Signal or Chrome opens or saves a file, it asks the desktop for a window
to pick it. Preferences → <b>Use Argonaut for Open and Save</b> makes that window Argonaut, with your
sidebar, bookmarks, search and views. Pick a file and press <b>Open</b> (or double-click it); to save,
type a name and press <b>Save</b>; Argonaut asks before replacing a file that's already there.
The app's own options (a file type, an encoding…) are at the bottom. [[Esc]] cancels.</p>
<p>It needs the installed package. The same button switches back to the desktop's own windows.</p>
)");
#endif
    add("prefs", tr("Preferences"), prefs);

    add("shortcuts", tr("Keyboard Shortcuts"), QString()); // made from the actions
}
