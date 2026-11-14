#include "PreferencesDialog.h"

#include "Claude.h"
#include "DefaultFileManager.h"
#include "KeyStore.h"
#include "GitStatus.h"
#include "MainWindow.h"
#if defined(ARGONAUT_DBUS)
#include "PortalConfig.h"
#endif
#include "Theme.h"
#include "Thumbnailer.h"

#include <QApplication>
#include <QButtonGroup>
#include <QLineEdit>
#include <QScrollArea>
#include <QScrollBar>
#include <QCheckBox>
#include <QSettings>
#include <QComboBox>
#include <QPushButton>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QToolButton>
#include <QVBoxLayout>

namespace {

constexpr QSize PreviewSize(112, 72);

// A tiny drawing of the main window in the given theme.
void drawMockWindow(QPainter &p, const Theme &t, const QPainterPath &clip)
{
    p.save();
    p.setClipPath(clip, Qt::IntersectClip);
    const QRectF r(0, 0, PreviewSize.width(), PreviewSize.height());
    p.fillRect(r, t.window);
    p.fillRect(QRectF(0, 0, 34, r.height()), t.sidebar);

    QColor bar = t.fg;
    bar.setAlpha(70);
    for (int i = 0; i < 4; ++i) {
        QPainterPath row;
        row.addRoundedRect(QRectF(6, 14 + i * 12, i == 0 ? 22 : 18, 5), 2.5, 2.5);
        p.fillPath(row, i == 0 ? t.fg : bar);
    }

    QPainterPath pill;
    pill.addRoundedRect(QRectF(42, 7, 52, 9), 4.5, 4.5);
    p.fillPath(pill, t.pathBg);

    for (int row = 0; row < 2; ++row) {
        for (int col = 0; col < 3; ++col) {
            QPainterPath tile;
            tile.addRoundedRect(QRectF(46 + col * 21, 26 + row * 22, 14, 11), 2.5, 2.5);
            p.fillPath(tile, t.accent);
            QPainterPath label;
            label.addRoundedRect(QRectF(46 + col * 21, 40 + row * 22, 14, 3), 1.5, 1.5);
            p.fillPath(label, bar);
        }
    }
    p.restore();
}

QIcon previewIcon(ThemeMode mode)
{
    const qreal dpr = qApp->devicePixelRatio();
    QPixmap pm(PreviewSize * dpr);
    pm.setDevicePixelRatio(dpr);
    pm.fill(Qt::transparent);

    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    QPainterPath frame;
    frame.addRoundedRect(QRectF(0.5, 0.5, PreviewSize.width() - 1, PreviewSize.height() - 1), 8, 8);

    const Theme light = Theme::forMode(ThemeMode::Light);
    const Theme dark = Theme::forMode(ThemeMode::Dark);
    if (mode == ThemeMode::System) {
        // Split diagonally: light top-left, dark bottom-right.
        QPainterPath lightHalf;
        lightHalf.moveTo(0, 0);
        lightHalf.lineTo(PreviewSize.width(), 0);
        lightHalf.lineTo(0, PreviewSize.height());
        lightHalf.closeSubpath();
        QPainterPath darkHalf;
        darkHalf.addRect(QRectF(QPointF(0, 0), QSizeF(PreviewSize)));
        darkHalf = darkHalf.subtracted(lightHalf);
        drawMockWindow(p, light, frame.intersected(lightHalf));
        drawMockWindow(p, dark, frame.intersected(darkHalf));
    } else {
        drawMockWindow(p, Theme::forMode(mode), frame);
    }

    p.setPen(QPen(QColor(0, 0, 0, 50), 1));
    p.setBrush(Qt::NoBrush);
    p.drawPath(frame);
    p.end();
    return QIcon(pm);
}

} // namespace

PreferencesDialog::PreferencesDialog(QWidget *parent)
    : QDialog(parent)
{
    setObjectName("preferences");
    setWindowTitle(tr("Preferences"));

    // The sections scroll when the screen is short.
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    auto *scroll = new QScrollArea(this);
    scroll->setObjectName("prefScroll");
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto *content = new QWidget(scroll);
    content->setObjectName("prefContent");
    scroll->setWidget(content);
    outer->addWidget(scroll);
    auto *layout = new QVBoxLayout(content);
    layout->setContentsMargins(24, 20, 24, 24);
    layout->setSpacing(6);

    auto *heading = new QLabel(tr("Appearance"), this);
    heading->setObjectName("prefHeading");
    auto *hint = new QLabel(tr("Choose how Argonaut looks."), this);
    hint->setObjectName("prefHint");
    layout->addWidget(heading);
    layout->addWidget(hint);
    layout->addSpacing(10);

    auto *cards = new QGridLayout;
    cards->setSpacing(10);
    auto *group = new QButtonGroup(this);
    group->setExclusive(true);

    const ThemeMode current = savedThemeMode();
    const QList<ThemeMode> modes = allThemeModes();
    constexpr int Columns = 4;
    for (int i = 0; i < modes.size(); ++i) {
        const ThemeMode mode = modes.at(i);
        auto *card = new QToolButton(this);
        card->setObjectName("themeCard");
        card->setCheckable(true);
        card->setChecked(mode == current);
        card->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
        card->setText(themeModeName(mode));
        card->setIcon(previewIcon(mode));
        card->setIconSize(PreviewSize);
        card->setCursor(Qt::PointingHandCursor);
        group->addButton(card);
        cards->addWidget(card, i / Columns, i % Columns);
        connect(card, &QToolButton::clicked, this, [mode] { setThemeMode(mode); });
    }
    layout->addLayout(cards);

#if defined(Q_OS_LINUX)
    // Making thumbnails for PDFs and videos reads the files: cheap on the
    // internal disk, slow over USB or the network.
    layout->addSpacing(18);
    auto *thumbsHeading = new QLabel(tr("Thumbnails"), this);
    thumbsHeading->setObjectName("prefHeading");
    auto *thumbsHint = new QLabel(tr("Previews for PDFs, videos, fonts and more, made with the "
                                     "thumbnailers installed on this computer and shared with "
                                     "other apps."),
                                  this);
    thumbsHint->setObjectName("prefHint");
    thumbsHint->setWordWrap(true);
    auto *thumbs = new QComboBox(this);
    thumbs->setObjectName("propCombo");
    thumbs->addItem(tr("Make previews on this computer's disks only"), int(Thumbnailer::Policy::LocalOnly));
    thumbs->addItem(tr("Make previews everywhere, also USB and network drives"), int(Thumbnailer::Policy::Everywhere));
    thumbs->addItem(tr("Only show previews that already exist"), int(Thumbnailer::Policy::Never));
    thumbs->setCurrentIndex(thumbs->findData(int(Thumbnailer::policy())));
    connect(thumbs, &QComboBox::currentIndexChanged, this, [thumbs] {
        Thumbnailer::setPolicy(Thumbnailer::Policy(thumbs->currentData().toInt()));
    });
    layout->addWidget(thumbsHeading);
    layout->addWidget(thumbsHint);
    layout->addSpacing(6);
    layout->addWidget(thumbs);

    // Expandable folders in the list view, like Nautilus.
    layout->addSpacing(18);
    auto *listHeading = new QLabel(tr("List View"), this);
    listHeading->setObjectName("prefHeading");
    auto *expand = new QCheckBox(tr("Expandable folders"), this);
    expand->setObjectName("prefCheck");
    expand->setChecked(QSettings().value("view/expandFolders", false).toBool());
    auto *expandHint = new QLabel(tr("Show an arrow before each folder in the list, to open it in place. "
                                     "→ and ← open and close the selected folder."),
                                  this);
    expandHint->setObjectName("prefHint");
    expandHint->setWordWrap(true);
    connect(expand, &QCheckBox::toggled, this, [](bool on) { MainWindow::setExpandFoldersEverywhere(on); });
    layout->addWidget(listHeading);
    layout->addSpacing(4);
    layout->addWidget(expand);
    layout->addWidget(expandHint);

    // Git badges in repositories.
    layout->addSpacing(18);
    auto *gitHeading = new QLabel(tr("Git"), this);
    gitHeading->setObjectName("prefHeading");
    auto *gitCheck = new QCheckBox(tr("Show Git status in repositories"), this);
    gitCheck->setObjectName("prefCheck");
    gitCheck->setChecked(GitStatus::enabled());
    auto *gitHint = new QLabel(tr("Marks changed (M), added (A), new (U) and conflicting (C) files, and shows "
                                  "the branch in the path bar. Repositories whose settings would run "
                                  "programs are left alone."),
                               this);
    gitHint->setObjectName("prefHint");
    gitHint->setWordWrap(true);
    connect(gitCheck, &QCheckBox::toggled, this, [](bool on) {
        GitStatus::setEnabled(on);
        MainWindow::resetGitEverywhere();
    });
    layout->addWidget(gitHeading);
    layout->addSpacing(4);
    layout->addWidget(gitCheck);
    layout->addWidget(gitHint);

    // Default file manager: Argonaut or Nautilus, switchable both ways.
    layout->addSpacing(18);
    auto *defaultHeading = new QLabel(tr("Default File Manager"), this);
    defaultHeading->setObjectName("prefHeading");
    auto *defaultHint = new QLabel(this);
    defaultHint->setObjectName("prefHint");
    defaultHint->setWordWrap(true);
    auto *defaultButton = new QPushButton(this);
    defaultButton->setObjectName("prefButton");
    defaultButton->setCursor(Qt::PointingHandCursor);
    auto *defaultRow = new QHBoxLayout;
    defaultRow->addWidget(defaultButton);
    defaultRow->addStretch();
    auto refresh = [defaultHint, defaultButton] {
        const bool installed = !DefaultFileManager::installedDesktopFile().isEmpty();
        const bool ours = installed && DefaultFileManager::isArgonautDefault();
        if (!installed) {
            defaultHint->setText(tr("Install Argonaut (cmake --install) to make it the file manager "
                                    "for folders and “Show in folder”."));
            defaultButton->setText(tr("Make Argonaut the Default"));
            defaultButton->setEnabled(false);
        } else if (ours) {
            defaultHint->setText(tr("Argonaut opens your folders, also from other apps, Super+E and "
                                    "“Show in folder”. You can always go back (or run "
                                    "“argonaut --restore-nautilus” in a terminal)."));
            defaultButton->setText(tr("Use Nautilus Again"));
            defaultButton->setEnabled(true);
        } else {
            defaultHint->setText(tr("Another file manager opens your folders now. Argonaut can take "
                                    "over folders, Super+E and “Show in folder”; Nautilus stays "
                                    "installed and you can switch back at any time."));
            defaultButton->setText(tr("Make Argonaut the Default"));
            defaultButton->setEnabled(true);
        }
    };
    refresh();
    connect(defaultButton, &QPushButton::clicked, this, [this, refresh, defaultHint] {
        QString error;
        const bool ok = DefaultFileManager::isArgonautDefault()
                            ? DefaultFileManager::restorePrevious(&error)
                            : DefaultFileManager::makeDefault(&error);
        if (ok && DefaultFileManager::isArgonautDefault())
            DefaultFileManager::startService(); // "Show in folder" now, not after the next login
        refresh();
        if (!ok)
            defaultHint->setText(error);
        adjustSize();
    });
    layout->addWidget(defaultHeading);
    layout->addWidget(defaultHint);
    layout->addSpacing(6);
    layout->addLayout(defaultRow);
#endif
#if defined(ARGONAUT_DBUS)
    // Other apps' Open and Save windows: Argonaut or the desktop's own.
    layout->addSpacing(18);
    auto *chooserHeading = new QLabel(tr("Open and Save Windows"), this);
    chooserHeading->setObjectName("prefHeading");
    auto *chooserHint = new QLabel(this);
    chooserHint->setObjectName("prefHint");
    chooserHint->setWordWrap(true);
    auto *chooserButton = new QPushButton(this);
    chooserButton->setObjectName("prefButton");
    chooserButton->setCursor(Qt::PointingHandCursor);
    auto *chooserRow = new QHBoxLayout;
    chooserRow->addWidget(chooserButton);
    chooserRow->addStretch();
    auto refreshChooser = [chooserHint, chooserButton] {
        if (!PortalConfig::isInstalled()) {
            chooserHint->setText(tr("Install the argonaut-files package to use Argonaut when other apps "
                                    "(Firefox, Signal, Chrome…) open or save files."));
            chooserButton->setText(tr("Use Argonaut for Open and Save"));
            chooserButton->setEnabled(false);
        } else if (PortalConfig::isEnabled()) {
            chooserHint->setText(tr("When other apps open or save files, they show an Argonaut window. "
                                    "Apps that are already running switch the next time you open one."));
            chooserButton->setText(tr("Use the Desktop's Windows Again"));
            chooserButton->setEnabled(true);
        } else {
            chooserHint->setText(tr("Other apps show the desktop's own window to open or save files. "
                                    "Argonaut can show its own instead, with your sidebar, search and views."));
            chooserButton->setText(tr("Use Argonaut for Open and Save"));
            chooserButton->setEnabled(true);
        }
    };
    refreshChooser();
    connect(chooserButton, &QPushButton::clicked, this, [this, refreshChooser, chooserHint] {
        QString error;
        const bool ok = PortalConfig::isEnabled() ? PortalConfig::disable(&error) : PortalConfig::enable(&error);
        if (ok)
            PortalConfig::restartPortal(); // applies now, not after the next login
        refreshChooser();
        if (!ok)
            chooserHint->setText(error);
        adjustSize();
    });
    layout->addWidget(chooserHeading);
    layout->addWidget(chooserHint);
    layout->addSpacing(6);
    layout->addLayout(chooserRow);
#endif

    addClaudeSection(layout);
    layout->addStretch();

    const int width = content->sizeHint().width() + scroll->verticalScrollBar()->sizeHint().width();
    setFixedWidth(width);
    resize(width, qMin(content->sizeHint().height() + 2, 720));
}

// Claude: the API key (kept in the keyring), what may be sent, the model,
// and what Argonaut has spent. Nothing is sent from here except the free
// key test.
void PreferencesDialog::addClaudeSection(QVBoxLayout *layout)
{
    layout->addSpacing(18);
    auto *heading = new QLabel(tr("Claude"), this);
    heading->setObjectName("prefHeading");
    layout->addWidget(heading);
    auto *intro = new QLabel(tr("Argonaut can ask Claude, Anthropic's AI, to organize a folder. Nothing is sent "
                                "until you start it yourself, and you see the cost first. You need an API key "
                                "from the <a href=\"https://platform.claude.com/\">Claude Console</a>."),
                             this);
    intro->setObjectName("prefHint");
    intro->setWordWrap(true);
    intro->setOpenExternalLinks(true);
    QPalette linkPalette = intro->palette();
    linkPalette.setColor(QPalette::Link, currentTheme().accent);
    intro->setPalette(linkPalette);
    layout->addWidget(intro);
    layout->addSpacing(6);

    // API key.
    auto *keyRow = new QHBoxLayout;
    auto *key = new QLineEdit(this);
    key->setObjectName("connectAddress");
    key->setEchoMode(QLineEdit::Password);
    key->setPlaceholderText(tr("Paste your API key (sk-ant-…)"));
    keyRow->addWidget(key, 1);
    auto *save = new QPushButton(tr("Save"), this);
    auto *test = new QPushButton(tr("Test"), this);
    auto *remove = new QPushButton(tr("Remove"), this);
    for (QPushButton *b : {save, test, remove}) {
        b->setObjectName("prefButton");
        b->setCursor(Qt::PointingHandCursor);
        keyRow->addWidget(b);
    }
    layout->addLayout(keyRow);
    auto *keyStatus = new QLabel(this);
    keyStatus->setObjectName("prefHint");
    keyStatus->setWordWrap(true);
    layout->addWidget(keyStatus);

    auto showStored = [keyStatus, remove, test] {
        KeyStore::Where where = KeyStore::Where::None;
        const QString stored = KeyStore::load(&where);
        if (stored.isEmpty()) {
            keyStatus->setText(tr("No key saved yet."));
        } else {
            keyStatus->setText(tr("A key ending in “…%1” is in %2.").arg(stored.right(4), KeyStore::describe(where)));
        }
        remove->setEnabled(where == KeyStore::Where::Keyring || where == KeyStore::Where::File);
        test->setEnabled(!stored.isEmpty());
    };
    showStored();
    connect(key, &QLineEdit::textChanged, this, [key, test, save] {
        save->setEnabled(!key->text().trimmed().isEmpty());
        if (!key->text().trimmed().isEmpty())
            test->setEnabled(true);
    });
    save->setEnabled(false);
    connect(save, &QPushButton::clicked, this, [key, keyStatus, showStored] {
        QString error;
        KeyStore::Where where;
        if (!KeyStore::save(key->text().trimmed(), &where, &error)) {
            keyStatus->setText(tr("Couldn't save the key: %1").arg(error));
            return;
        }
        key->clear();
        showStored();
    });
    connect(remove, &QPushButton::clicked, this, [keyStatus, showStored] {
        QString error;
        if (!KeyStore::remove(&error))
            keyStatus->setText(error);
        else
            showStored();
    });
    auto *client = new Claude::Client(this);
    connect(test, &QPushButton::clicked, this, [client, key, keyStatus, test] {
        const QString candidate = key->text().trimmed().isEmpty() ? KeyStore::load() : key->text().trimmed();
        test->setEnabled(false);
        keyStatus->setText(tr("Checking the key…"));
        client->testKey(candidate, [keyStatus, test](bool ok, const QString &message) {
            test->setEnabled(true);
            keyStatus->setText((ok ? QStringLiteral("✓ ") : QStringLiteral("✗ ")) + message
                               + (ok ? tr(" (checked without spending anything)") : QString()));
        });
    });

    // What may be sent.
    layout->addSpacing(10);
    auto *modeLabel = new QLabel(tr("What Claude may see"), this);
    modeLabel->setObjectName("propHeading");
    layout->addWidget(modeLabel);
    auto *modeBox = new QComboBox(this);
    modeBox->setObjectName("propCombo");
    for (Claude::Mode m : {Claude::Mode::NamesOnly, Claude::Mode::Ask, Claude::Mode::Auto})
        modeBox->addItem(Claude::modeName(m), int(m));
    modeBox->setCurrentIndex(modeBox->findData(int(Claude::mode())));
    layout->addWidget(modeBox);
    auto *modeHint = new QLabel(this);
    modeHint->setObjectName("prefHint");
    modeHint->setWordWrap(true);
    layout->addWidget(modeHint);
    auto describeMode = [modeHint](Claude::Mode m) {
        switch (m) {
        case Claude::Mode::NamesOnly:
            modeHint->setText(tr("Only names, sizes, dates and file types. No file contents ever leave this computer."));
            break;
        case Claude::Mode::Ask:
            modeHint->setText(tr("Names and details first. When Claude can't place some files, it asks to read their "
                                 "beginning, and you choose."));
            break;
        case Claude::Mode::Auto:
            modeHint->setText(tr("Like above, without asking: Claude reads the beginning of files it can't place."));
            break;
        }
    };
    describeMode(Claude::mode());
    connect(modeBox, &QComboBox::currentIndexChanged, this, [modeBox, describeMode] {
        const auto m = Claude::Mode(modeBox->currentData().toInt());
        Claude::setMode(m);
        describeMode(m);
    });

    // Model.
    layout->addSpacing(10);
    auto *modelLabel = new QLabel(tr("Model"), this);
    modelLabel->setObjectName("propHeading");
    layout->addWidget(modelLabel);
    auto *modelBox = new QComboBox(this);
    modelBox->setObjectName("propCombo");
    for (const Claude::ModelInfo &m : Claude::models()) {
        modelBox->addItem(tr("%1  ($%2 / $%3 per million tokens in / out)")
                              .arg(m.name, QString::number(m.inputPerMTok, 'g', 3), QString::number(m.outputPerMTok, 'g', 3)),
                          m.id);
    }
    modelBox->setCurrentIndex(modelBox->findData(Claude::model()));
    connect(modelBox, &QComboBox::currentIndexChanged, this, [modelBox] { Claude::setModel(modelBox->currentData().toString()); });
    layout->addWidget(modelBox);

    // Spending.
    layout->addSpacing(10);
    auto *spentRow = new QHBoxLayout;
    auto *spent = new QLabel(this);
    spent->setObjectName("prefHint");
    auto showSpent = [spent] { spent->setText(tr("Spent in Argonaut so far: $%1").arg(Claude::spent(), 0, 'f', 2)); };
    showSpent();
    auto *reset = new QPushButton(tr("Reset"), this);
    reset->setObjectName("prefButton");
    connect(reset, &QPushButton::clicked, this, [showSpent] {
        Claude::resetSpent();
        showSpent();
    });
    spentRow->addWidget(spent, 1);
    spentRow->addWidget(reset);
    layout->addLayout(spentRow);
}
