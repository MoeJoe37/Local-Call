#include "nlohmann/json.hpp"
using json = nlohmann::json;
#include "MainWindow.h"
#include "Helpers.h"
#include "SignalingClient.h"
#include "NotificationWindow.h"
#include "InputDialog.h"
#include "GroupCreateDialog.h"
#include "GroupManageDialog.h"
#include <QApplication>
#include <QGuiApplication>
#include <QClipboard>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QListWidget>
#include <QListWidgetItem>
#include <QStackedWidget>
#include <QScrollArea>
#include <QDialog>
#include <QProgressBar>
#include <QSplitter>
#include <QMenu>
#include <QAction>
#include <QFileDialog>
#include <QMessageBox>
#include <QCloseEvent>
#include <QFile>
#include <QDateTime>
#include <QTimer>
#include <QPixmap>
#include <QBuffer>
#include <QStyle>
#include <QScreen>
#include <QIcon>
#include <QSettings>
#include <QThreadPool>
#include <algorithm>
#include <iterator>

#ifdef Q_OS_WIN
#include <windows.h>
#include <dwmapi.h>
#pragma comment(lib, "dwmapi.lib")
#endif

// ── Stylesheet ────────────────────────────────────────────────────────────────
// Updated to match the new chat UI style (lavender #CBA6F7 accent, Catppuccin Mocha)
static const QString APP_STYLE = R"(
QMainWindow, QWidget { background: #1E1E2E; color: #CDD6F4; font-size: 13px; }
QSplitter::handle { background: #313244; width: 1px; }

/* Sidebar */
#sidebar { background: #181825; border-right: 1px solid #313244; }
#sidebarTop { background: #181825; border-bottom: 1px solid #313244; padding: 10px; }

/* Sidebar title "Messages" */
QLabel#sidebarTitle { color: #CBA6F7; font-size: 18px; font-weight: bold; padding: 10px 14px; }
QLabel#myName { color: #CBA6F7; font-size: 11px; font-weight: bold; }

QPushButton#discoverBtn {
    background: transparent; color: #A6ADC8; border: none;
    text-align: left; padding: 10px 14px; font-size: 13px;
    border-bottom: 1px solid #313244;
}
QPushButton#discoverBtn:hover { background: #313244; }

/* Section headers */
QLabel.sectionHeader {
    color: #6C7086; font-size: 10px; font-weight: bold;
    padding: 8px 14px 2px 14px; letter-spacing: 1px;
}

/* Friend/group lists */
QListWidget { background: #181825; border: none; outline: none; color: #CDD6F4; font-size: 14px; }
QListWidget::item { padding: 12px 14px; border-bottom: 1px solid #313244; margin: 0; border-radius: 0; }
QListWidget::item:hover    { background: #1E1E2E; }
QListWidget::item:selected { background: #45475A; border-left: 4px solid #CBA6F7; }

/* Peer list (discover) — no padding; content comes from setItemWidget */
#peerList::item { padding: 0; margin: 1px 4px; border-radius: 6px; border-bottom: none; }
#peerList::item:selected { border-left: 4px solid #CBA6F7; background: #45475A; border-radius: 0; }

/* Status badge */
QLabel#statusDot { border-radius: 5px; min-width: 10px; min-height: 10px; max-width: 10px; max-height: 10px; }

/* Request badge */
#requestBadge { background: #CBA6F7; color: #11111B; border-radius: 9px;
                font-size: 10px; font-weight: bold; padding: 1px 6px; }
#requestsSection { background: #1E1E2E; border-radius: 6px; margin: 4px 8px; padding: 6px 8px; }

/* Chat / group panel */
#chatHeader, #groupHeader {
    background: #1E1E2E; border-bottom: 1px solid #313244;
    padding: 10px 16px;
}
QLabel#chatTitle, QLabel#groupTitle {
    font-size: 15px; font-weight: bold; color: #CDD6F4;
}
#chatPanel, #groupPanel { background: #1E1E2E; }
#msgArea { background: #181825; }

/* Message bubbles */
#bubbleMine    { background: #3D2B6B; border-radius: 14px 14px 2px 14px; }
#bubbleTheirs  { background: #242436; border-radius: 14px 14px 14px 2px; }
QLabel#msgText { color: #CDD6F4; font-size: 13px; }
QTextEdit#msgText { color: #CDD6F4; font-size: 13px; }
QLabel#msgMeta { color: #6C7086; font-size: 10px; }
QLabel#msgName { color: #CBA6F7; font-size: 11px; font-weight: bold; }

/* Input bar */
#inputBar { background: #1E1E2E; border-top: 1px solid #313244; padding: 8px 12px; }
QLineEdit#chatInput, QLineEdit#groupInput {
    background: #313244; color: #CDD6F4; border: 1px solid #45475A;
    border-radius: 8px; padding: 10px 14px; font-size: 13px;
}
QLineEdit#chatInput:focus, QLineEdit#groupInput:focus { border-color: #CBA6F7; }
QPushButton.sendBtn {
    background: #CBA6F7; color: #11111B; border: none;
    border-radius: 8px; padding: 10px 20px; font-size: 13px; font-weight: bold;
}
QPushButton.sendBtn:hover { background: #B4BEFE; }
QPushButton.toolBtn {
    background: transparent; color: #A6ADC8; border: none;
    border-radius: 8px; padding: 6px 10px; font-size: 14px;
}
QPushButton.toolBtn:hover { background: #313244; }

/* Read-only banner */
#readOnlyBanner {
    background: #2A1810; color: #FAB387; border-radius: 6px;
    padding: 8px 14px; margin: 6px 12px; font-size: 12px;
}

/* Discover panel */
#discoverPanel { background: #1E1E2E; }
#emptyState { color: #45475A; font-size: 15px; }
QPushButton#addPeerBtn {
    background: #CBA6F7; color: #11111B; border: none;
    border-radius: 8px; padding: 6px 14px; font-size: 12px; font-weight: bold;
}
QPushButton#addPeerBtn:hover { background: #B4BEFE; }

/* Status bar */
#statusBar { background: #181825; border-top: 1px solid #313244; padding: 4px 14px; }
QLabel#statusText { color: #6C7086; font-size: 11px; }

/* Scrollbars */
QScrollBar:vertical { background: #181825; width: 6px; border-radius: 3px; }
QScrollBar::handle:vertical { background: #45475A; border-radius: 3px; min-height: 20px; }
QScrollBar::handle:vertical:hover { background: #585B70; }
QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }
QScrollBar:horizontal { height: 0; }

/* Group member list */
#grpMembers { background: #181825; border-left: 1px solid #313244; min-width: 140px; max-width: 180px; }
QLabel#grpMemberHeader { color: #6C7086; font-size: 10px; font-weight: bold;
                         padding: 8px 12px 2px 12px; letter-spacing: 1px; }

/* Recording indicator */
QLabel#recording { color: #F38BA8; font-size: 11px; font-weight: bold; }

/* Typing / upload status indicator */
QLabel#chatStatusLabel {
    color: #CBA6F7; font-size: 11px; font-style: italic;
    padding: 2px 14px 0 14px;
}
/* Upload / download progress bar */
QProgressBar#uploadBar {
    background: #313244; border: none; border-radius: 3px;
    height: 4px; text-align: center; color: transparent;
}
QProgressBar#uploadBar::chunk { background: #CBA6F7; border-radius: 3px; }

/* Message area — selected item highlight for delete */
#msgArea::item:selected { background: #2D2B50; border-radius: 0; border-left: none; }
#msgArea::item:hover    { background: #1E1E2E; border-radius: 0; }

/* Buttons inside chat toolbar */
#chatToolbar QPushButton { background: transparent; color: #A6ADC8; border: none;
    border-radius: 8px; padding: 6px 10px; font-size: 12px; }
#chatToolbar QPushButton:hover { background: #313244; }

/* Call buttons in chat header */
QPushButton#btnCallVoice, QPushButton#btnCallVideo {
    background: #313244; color: #CDD6F4; border: none;
    border-radius: 8px; padding: 6px 12px; font-size: 14px;
}
QPushButton#btnCallVoice:hover, QPushButton#btnCallVideo:hover { background: #45475A; }

/* Context menus */
QMenu { background: #181825; color: #CDD6F4; border: 1px solid #45475A; border-radius: 6px; }
QMenu::item { padding: 8px 16px; }
QMenu::item:selected { background: #CBA6F7; color: #11111B; }
QMenu::separator { height: 1px; background: #45475A; margin: 2px 8px; }
)";

// ══════════════════════════════════════════════════════════════════════════════
//  BUILD SIDEBAR
// ══════════════════════════════════════════════════════════════════════════════

static QLabel* makeStatusDot(QWidget* parent, const QString& color = "#555555") {
    auto* dot = new QLabel(parent);
    dot->setObjectName("statusDot");
    dot->setFixedSize(10, 10);
    dot->setStyleSheet(QString("background: %1; border-radius: 5px;").arg(color));
    return dot;
}

static QWidget* makeSectionHeader(const QString& text, QWidget* parent) {
    auto* lbl = new QLabel(text.toUpper(), parent);
    lbl->setProperty("class", "sectionHeader");
    lbl->setStyleSheet("color: #6C7086; font-size: 10px; font-weight: bold;"
                       " padding: 8px 14px 2px 14px; letter-spacing: 1px;");
    return lbl;
}

// ══════════════════════════════════════════════════════════════════════════════
//  MAIN WINDOW CONSTRUCTOR
// ══════════════════════════════════════════════════════════════════════════════

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent)
{
    // ── Identity — persisted across sessions ──────────────────────────────────
    // The ID must survive restarts so peers recognise us as the same user.
    QSettings settings("LocalCall", "LocalCall");
    m_myId   = settings.value("identity/id",   "").toString();
    m_myName = settings.value("identity/name", "").toString();
    if (m_myId.isEmpty()) {
        m_myId = QString::fromStdString(Helpers::generateId());
        settings.setValue("identity/id", m_myId);
    }
    if (m_myName.isEmpty()) {
        m_myName = QString::fromStdString(Helpers::getFunnyName());
        settings.setValue("identity/name", m_myName);
    }
    m_localIp = QString::fromStdString(Helpers::getLocalIp());

    setWindowTitle("Local Call");
    setMinimumSize(900, 580);
    resize(1100, 680);
    setStyleSheet(APP_STYLE);

#ifdef HAS_MULTIMEDIA
    m_vnRec      = new VoiceNoteRecorder(this);
    m_vnRecGroup = new VoiceNoteRecorder(this);
#else
    m_vnRec      = nullptr;
    m_vnRecGroup = nullptr;
#endif
    m_friendMgr  = new FriendManager(this);
    m_chatStore  = new ChatStore(this);

    // ── Root splitter (sidebar | content) ─────────────────────────────────────
    auto* splitter = new QSplitter(Qt::Horizontal, this);
    splitter->setHandleWidth(1);
    splitter->setChildrenCollapsible(false);
    setCentralWidget(splitter);

    // ══════════════════════════════════════════════════════════════════════════
    //  SIDEBAR
    // ══════════════════════════════════════════════════════════════════════════
    auto* sidebar = new QWidget();
    sidebar->setObjectName("sidebar");
    sidebar->setMinimumWidth(200);
    sidebar->setMaximumWidth(260);
    auto* sideLayout = new QVBoxLayout(sidebar);
    sideLayout->setContentsMargins(0,0,0,0);
    sideLayout->setSpacing(0);

    // Sidebar "Messages" title
    auto* sidebarTitleLabel = new QLabel("Messages", sidebar);
    sidebarTitleLabel->setObjectName("sidebarTitle");
    sideLayout->addWidget(sidebarTitleLabel);

    // Top: name + IP
    auto* sideTop = new QWidget();
    sideTop->setObjectName("sidebarTop");
    auto* sideTopLayout = new QHBoxLayout(sideTop);
    sideTopLayout->setContentsMargins(12,10,8,10);
    m_lblMyName = new QLabel(m_myName + "  ·  " + m_localIp, sideTop);
    m_lblMyName->setObjectName("myName");
    m_lblMyName->setWordWrap(true);
    auto* btnEdit = new QPushButton("✏", sideTop);
    btnEdit->setFixedSize(28, 28);
    btnEdit->setToolTip("Edit profile");
    btnEdit->setStyleSheet("background:transparent;color:#6C7086;border:none;font-size:14px;");
    sideTopLayout->addWidget(m_lblMyName, 1);
    sideTopLayout->addWidget(btnEdit);
    sideLayout->addWidget(sideTop);

    // Discover button
    auto* discBtn = new QPushButton("⌖  Discover Peers", sidebar);
    discBtn->setObjectName("discoverBtn");
    sideLayout->addWidget(discBtn);

    // ── REQUESTS section — inline in sidebar, hidden when empty (mirrors C#) ──
    // Shown/hidden by refreshRequestsBadge(). Contains a header with red badge
    // count + a list of pending requests with Accept/Decline buttons per row.
    m_requestsSection = new QWidget(sidebar);
    auto* reqSecLayout = new QVBoxLayout(m_requestsSection);
    reqSecLayout->setContentsMargins(0, 4, 0, 0);
    reqSecLayout->setSpacing(0);

    // Header row: "REQUESTS" label + red count badge
    auto* reqHeaderW = new QWidget(m_requestsSection);
    auto* reqHeaderL = new QHBoxLayout(reqHeaderW);
    reqHeaderL->setContentsMargins(12, 6, 12, 4);
    auto* reqLbl = new QLabel("REQUESTS", reqHeaderW);
    reqLbl->setStyleSheet("color:#6C7086;font-size:10px;font-weight:bold;letter-spacing:1px;");
    m_lblRequestCount = new QLabel("0", reqHeaderW);
    m_lblRequestCount->setStyleSheet(
        "background:#CBA6F7;color:#11111B;border-radius:7px;"
        "font-size:9px;font-weight:bold;padding:1px 5px;");
    reqHeaderL->addWidget(reqLbl, 1);
    reqHeaderL->addWidget(m_lblRequestCount);
    reqSecLayout->addWidget(reqHeaderW);

    // Inline list — items built by rebuildRequestsList()
    m_requestsList = new QListWidget(m_requestsSection);
    m_requestsList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_requestsList->setResizeMode(QListView::Adjust);
    m_requestsList->setUniformItemSizes(false);
    m_requestsList->setStyleSheet(
        "QListWidget{background:transparent;border:none;}"
        "QListWidget::item{padding:0;margin:1px 4px;border-radius:4px;}");
    reqSecLayout->addWidget(m_requestsList);

    // Divider below the section
    auto* reqDivider = new QWidget(m_requestsSection);
    reqDivider->setFixedHeight(1);
    reqDivider->setStyleSheet("background:#3A1010;margin:4px 12px;");
    reqSecLayout->addWidget(reqDivider);

    sideLayout->addWidget(m_requestsSection);
    m_requestsSection->setVisible(false);   // hidden until requests arrive

    // ── FRIENDS section ───────────────────────────────────────────────────────
    sideLayout->addWidget(makeSectionHeader("Friends", sidebar));
    m_friendsList = new QListWidget(sidebar);
    m_friendsList->setContextMenuPolicy(Qt::CustomContextMenu);
    sideLayout->addWidget(m_friendsList, 1);

    // Groups section
    auto* grpHeader = new QHBoxLayout();
    grpHeader->setContentsMargins(14,8,8,2);
    auto* grpLbl = new QLabel("GROUPS", sidebar);
    grpLbl->setStyleSheet("color:#6C7086;font-size:10px;font-weight:bold;letter-spacing:1px;");
    auto* btnNewGrp = new QPushButton("+", sidebar);
    btnNewGrp->setFixedSize(22, 22);
    btnNewGrp->setToolTip("New group");
    btnNewGrp->setStyleSheet("background:#CBA6F7;color:#11111B;border:none;border-radius:11px;font-size:14px;font-weight:bold;");
    grpHeader->addWidget(grpLbl, 1);
    grpHeader->addWidget(btnNewGrp);
    auto* grpHeaderWidget = new QWidget(sidebar);
    grpHeaderWidget->setLayout(grpHeader);
    sideLayout->addWidget(grpHeaderWidget);

    m_groupsList = new QListWidget(sidebar);
    m_groupsList->setContextMenuPolicy(Qt::CustomContextMenu);
    sideLayout->addWidget(m_groupsList, 1);

    // Status bar
    auto* statusBar = new QWidget(sidebar);
    statusBar->setObjectName("statusBar");
    auto* statusLayout = new QHBoxLayout(statusBar);
    statusLayout->setContentsMargins(14,4,14,4);
    m_statusLabel = new QLabel("Starting…", statusBar);
    m_statusLabel->setObjectName("statusText");
    statusLayout->addWidget(m_statusLabel);
    sideLayout->addWidget(statusBar);

    splitter->addWidget(sidebar);

    // ══════════════════════════════════════════════════════════════════════════
    //  CONTENT AREA
    // ══════════════════════════════════════════════════════════════════════════
    m_panels = new QStackedWidget();
    splitter->addWidget(m_panels);
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);

    // ── Discover Panel ────────────────────────────────────────────────────────
    m_panelDiscover = new QWidget();
    m_panelDiscover->setObjectName("discoverPanel");
    auto* discLayout = new QVBoxLayout(m_panelDiscover);
    discLayout->setContentsMargins(0,0,0,0);
    discLayout->setSpacing(0);

    // Header
    auto* discHeader = new QWidget();
    discHeader->setStyleSheet("background:#1E1E2E;border-bottom:1px solid #313244;");
    auto* discHeaderLayout = new QHBoxLayout(discHeader);
    discHeaderLayout->setContentsMargins(16,12,12,12);
    auto* discTitle = new QLabel("⌖  Discover Peers", discHeader);
    discTitle->setStyleSheet("font-size:15px;font-weight:bold;color:#CDD6F4;");
    m_btnRefresh = new QPushButton("↻  Scan", discHeader);
    m_btnRefresh->setObjectName("btnRefresh");
    m_btnRefresh->setStyleSheet(
        "QPushButton#btnRefresh{"
        "  background:#313244;color:#CDD6F4;border:none;"
        "  border-radius:4px;padding:6px 12px;font-size:13px;}"
        "QPushButton#btnRefresh:hover{background:#45475A;}"
        "QPushButton#btnRefresh:disabled{color:#585b70;}");
    discHeaderLayout->addWidget(discTitle, 1);
    discHeaderLayout->addWidget(m_btnRefresh);

    // Spinner: cycles arrow frames until the next peersUpdated signal
    m_refreshAniTimer = new QTimer(this);
    m_refreshAniTimer->setInterval(120);
    connect(m_refreshAniTimer, &QTimer::timeout, this, [this]() {
        static const QString frames[] = {
            "↻  Scanning ·   ", "↻  Scanning ··  ",
            "↻  Scanning ···  ", "↻  Scanning ···· "
        };
        m_btnRefresh->setText(frames[m_refreshAniStep % 4]);
        ++m_refreshAniStep;
    });
    discLayout->addWidget(discHeader);

    m_emptyState = new QWidget();
    auto* emptyLayout = new QVBoxLayout(m_emptyState);
    auto* emptyLbl = new QLabel("No peers visible on your network.\nMake sure others are running Local Call.", m_emptyState);
    emptyLbl->setObjectName("emptyState");
    emptyLbl->setAlignment(Qt::AlignCenter);
    emptyLayout->addStretch();
    emptyLayout->addWidget(emptyLbl, 0, Qt::AlignCenter);
    emptyLayout->addStretch();

    m_peerList = new QListWidget();
    m_peerList->setObjectName("peerList");
    m_peerList->setVisible(false);
    m_peerList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_peerList->setResizeMode(QListView::Adjust);
    m_peerList->setUniformItemSizes(false);

    m_discStack = new QStackedWidget();
    m_discStack->addWidget(m_emptyState);
    m_discStack->addWidget(m_peerList);
    discLayout->addWidget(m_discStack, 1);

    // ── Add by IP row ─────────────────────────────────────────────────────────
    auto* ipRow = new QWidget();
    ipRow->setStyleSheet("background:#1E1E2E;border-top:1px solid #313244;");
    auto* ipLayout = new QHBoxLayout(ipRow);
    ipLayout->setContentsMargins(12,8,12,8);
    ipLayout->setSpacing(6);
    m_ipSearchInput = new QLineEdit(ipRow);
    m_ipSearchInput->setPlaceholderText("Add by IP address…");
    m_ipSearchInput->setStyleSheet(
        "QLineEdit{background:#313244;color:#CDD6F4;border:1px solid #45475A;"
        "border-radius:4px;padding:5px 8px;font-size:12px;}"
        "QLineEdit:focus{border-color:#CBA6F7;}");
    auto* btnAddIp = new QPushButton("→", ipRow);
    btnAddIp->setObjectName("addPeerBtn");
    btnAddIp->setFixedSize(28, 28);
    btnAddIp->setToolTip("Connect to this IP address");
    ipLayout->addWidget(m_ipSearchInput, 1);
    ipLayout->addWidget(btnAddIp);
    discLayout->addWidget(ipRow);

    m_panels->addWidget(m_panelDiscover);

    // ── Chat Panel ────────────────────────────────────────────────────────────
    m_panelChat = new QWidget();
    m_panelChat->setObjectName("chatPanel");
    auto* chatLayout = new QVBoxLayout(m_panelChat);
    chatLayout->setContentsMargins(0,0,0,0);
    chatLayout->setSpacing(0);

    // Chat header
    auto* chatHeader = new QWidget();
    chatHeader->setObjectName("chatHeader");
    auto* chatHeaderLayout = new QHBoxLayout(chatHeader);
    chatHeaderLayout->setContentsMargins(12,10,12,10);
    auto* btnBack = new QPushButton("←", chatHeader);
    btnBack->setStyleSheet("background:transparent;color:#A6ADC8;border:none;font-size:16px;padding:4px 8px;");
    m_chatStatusDot = makeStatusDot(chatHeader);
    m_chatName = new QLabel("", chatHeader);
    m_chatName->setObjectName("chatTitle");
    auto* btnChatVoice = new QPushButton("📞", chatHeader);
    auto* btnChatVideo = new QPushButton("🎥", chatHeader);
    for (auto* b : {btnChatVoice, btnChatVideo}) {
        b->setStyleSheet("background:transparent;color:#A6ADC8;border:none;font-size:16px;padding:4px 8px;");
    }
    m_btnChatVoice = btnChatVoice;
    m_btnChatVideo = btnChatVideo;
    chatHeaderLayout->addWidget(btnBack);
    chatHeaderLayout->addWidget(m_chatStatusDot);
    chatHeaderLayout->addSpacing(6);
    chatHeaderLayout->addWidget(m_chatName, 1);
    chatHeaderLayout->addWidget(btnChatVoice);
    chatHeaderLayout->addWidget(btnChatVideo);
    chatLayout->addWidget(chatHeader);

    // Read-only banner
    m_chatReadOnlyBanner = new QWidget();
    m_chatReadOnlyBanner->setObjectName("readOnlyBanner");
    auto* roBannerLayout = new QHBoxLayout(m_chatReadOnlyBanner);
    roBannerLayout->setContentsMargins(14,8,14,8);
    roBannerLayout->addWidget(new QLabel("🔒  This conversation is read-only. You are no longer friends.", m_chatReadOnlyBanner));
    chatLayout->addWidget(m_chatReadOnlyBanner);
    m_chatReadOnlyBanner->setVisible(false);

    // Message area
    m_chatMsgList = new QListWidget();
    m_chatMsgList->setObjectName("msgArea");
    m_chatMsgList->setWordWrap(true);
    m_chatMsgList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_chatMsgList->setSelectionMode(QAbstractItemView::ExtendedSelection);
    chatLayout->addWidget(m_chatMsgList, 1);
    
    // Install resize event filter for responsive message layout
    auto* chatResizeFilter = new ListWidgetResizeFilter(m_chatMsgList, this);
    m_chatMsgList->viewport()->installEventFilter(chatResizeFilter);

    // Typing / upload status indicator (hidden by default)
    m_lblChatStatus = new QLabel("", m_panelChat);
    m_lblChatStatus->setObjectName("chatStatusLabel");
    m_lblChatStatus->setVisible(false);
    chatLayout->addWidget(m_lblChatStatus);

    // Outgoing upload progress bar (hidden by default)
    m_chatUploadBar = new QProgressBar(m_panelChat);
    m_chatUploadBar->setObjectName("uploadBar");
    m_chatUploadBar->setFixedHeight(4);
    m_chatUploadBar->setRange(0, 100);
    m_chatUploadBar->setValue(0);
    m_chatUploadBar->setTextVisible(false);
    m_chatUploadBar->setVisible(false);
    chatLayout->addWidget(m_chatUploadBar);
    m_chatToolbar = new QWidget();
    m_chatToolbar->setObjectName("chatToolbar");
    auto* tbLayout = new QHBoxLayout(m_chatToolbar);
    tbLayout->setContentsMargins(12,4,12,0);
    tbLayout->setSpacing(4);
    auto* btnSendImg  = new QPushButton("🖼  Image", m_chatToolbar);
    auto* btnSendFile = new QPushButton("📎  File",  m_chatToolbar);
    auto* btnVoiceNote = new QPushButton("🎤  Hold to Record", m_chatToolbar);
    m_lblRecording = new QLabel("⏺ Recording…", m_chatToolbar);
    m_lblRecording->setObjectName("recording");
    m_lblRecording->setVisible(false);
    tbLayout->addWidget(btnSendImg);
    tbLayout->addWidget(btnSendFile);
    tbLayout->addWidget(btnVoiceNote);
    tbLayout->addWidget(m_lblRecording);
    tbLayout->addStretch();
    chatLayout->addWidget(m_chatToolbar);

    // Input bar
    m_chatInputBar = new QWidget();
    m_chatInputBar->setObjectName("inputBar");
    auto* inputLayout = new QHBoxLayout(m_chatInputBar);
    inputLayout->setContentsMargins(12,6,12,8);
    inputLayout->setSpacing(8);
    m_chatInput = new QLineEdit();
    m_chatInput->setObjectName("chatInput");
    m_chatInput->setPlaceholderText("Type a message…");
    auto* btnSend = new QPushButton("Send", m_chatInputBar);
    btnSend->setProperty("class", "sendBtn");
    inputLayout->addWidget(m_chatInput, 1);
    inputLayout->addWidget(btnSend);
    chatLayout->addWidget(m_chatInputBar);

    m_panels->addWidget(m_panelChat);

    // ── Group Chat Panel ──────────────────────────────────────────────────────
    m_panelGroup = new QWidget();
    m_panelGroup->setObjectName("groupPanel");
    auto* grpPanelLayout = new QHBoxLayout(m_panelGroup);
    grpPanelLayout->setContentsMargins(0,0,0,0);
    grpPanelLayout->setSpacing(0);

    // Main column
    auto* grpMain = new QWidget();
    auto* grpMainLayout = new QVBoxLayout(grpMain);
    grpMainLayout->setContentsMargins(0,0,0,0);
    grpMainLayout->setSpacing(0);

    // Group header
    auto* groupHeader = new QWidget();
    groupHeader->setObjectName("groupHeader");
    auto* grpHeaderLayout = new QHBoxLayout(groupHeader);
    grpHeaderLayout->setContentsMargins(12,10,12,10);
    auto* btnGrpBack = new QPushButton("←", groupHeader);
    btnGrpBack->setStyleSheet("background:transparent;color:#A6ADC8;border:none;font-size:16px;padding:4px 8px;");
    m_groupName = new QLabel("", groupHeader);
    m_groupName->setObjectName("groupTitle");
    auto* btnGrpVoice  = new QPushButton("📞", groupHeader);
    btnGrpVoice->setStyleSheet("background:transparent;color:#A6ADC8;border:none;font-size:16px;padding:4px 8px;");
    grpHeaderLayout->addWidget(btnGrpBack);
    grpHeaderLayout->addWidget(m_groupName, 1);
    grpHeaderLayout->addWidget(btnGrpVoice);
    grpMainLayout->addWidget(groupHeader);

    m_groupMsgList = new QListWidget();
    m_groupMsgList->setObjectName("msgArea");
    m_groupMsgList->setWordWrap(true);
    m_groupMsgList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_groupMsgList->setSelectionMode(QAbstractItemView::ExtendedSelection);
    grpMainLayout->addWidget(m_groupMsgList, 1);
    
    // Install resize event filter for responsive message layout
    auto* grpResizeFilter = new ListWidgetResizeFilter(m_groupMsgList, this);
    m_groupMsgList->viewport()->installEventFilter(grpResizeFilter);

    // Group toolbar
    auto* grpToolbar = new QWidget();
    grpToolbar->setObjectName("chatToolbar");
    auto* grpTbLayout = new QHBoxLayout(grpToolbar);
    grpTbLayout->setContentsMargins(12,4,12,0);
    grpTbLayout->setSpacing(4);
    auto* btnGrpSendImg  = new QPushButton("🖼  Image", grpToolbar);
    auto* btnGrpSendFile = new QPushButton("📎  File",  grpToolbar);
    auto* btnGrpVoiceNote = new QPushButton("🎤  Hold to Record", grpToolbar);
    m_lblGrpRecording = new QLabel("⏺ Recording…", grpToolbar);
    m_lblGrpRecording->setObjectName("recording");
    m_lblGrpRecording->setVisible(false);
    grpTbLayout->addWidget(btnGrpSendImg);
    grpTbLayout->addWidget(btnGrpSendFile);
    grpTbLayout->addWidget(btnGrpVoiceNote);
    grpTbLayout->addWidget(m_lblGrpRecording);
    grpTbLayout->addStretch();
    grpMainLayout->addWidget(grpToolbar);

    // Group input bar
    auto* grpInputBar = new QWidget();
    grpInputBar->setObjectName("inputBar");
    auto* grpInputLayout = new QHBoxLayout(grpInputBar);
    grpInputLayout->setContentsMargins(12,6,12,8);
    grpInputLayout->setSpacing(8);
    m_groupInput = new QLineEdit();
    m_groupInput->setObjectName("groupInput");
    m_groupInput->setPlaceholderText("Message group…");
    auto* btnGrpSend = new QPushButton("Send", grpInputBar);
    btnGrpSend->setProperty("class", "sendBtn");
    grpInputLayout->addWidget(m_groupInput, 1);
    grpInputLayout->addWidget(btnGrpSend);
    grpMainLayout->addWidget(grpInputBar);

    grpPanelLayout->addWidget(grpMain, 1);

    // Member sidebar
    auto* grpMembers = new QWidget();
    grpMembers->setObjectName("grpMembers");
    auto* gmLayout = new QVBoxLayout(grpMembers);
    gmLayout->setContentsMargins(0,0,0,0);
    gmLayout->setSpacing(0);
    auto* gmHeader = new QLabel("MEMBERS", grpMembers);
    gmHeader->setObjectName("grpMemberHeader");
    m_grpMemberList = new QListWidget(grpMembers);
    m_grpMemberList->setStyleSheet("QListWidget{background:transparent;border:none;}QListWidget::item{padding:6px 12px;color:#A6ADC8;font-size:12px;}");
    gmLayout->addWidget(gmHeader);
    gmLayout->addWidget(m_grpMemberList, 1);
    grpPanelLayout->addWidget(grpMembers);

    m_panels->addWidget(m_panelGroup);

    // ── Connect signals ───────────────────────────────────────────────────────
    connect(btnEdit,     &QPushButton::clicked, this, &MainWindow::onEditProfile);
    connect(discBtn,     &QPushButton::clicked, this, &MainWindow::onDiscoverClicked);
    connect(m_btnRefresh,  &QPushButton::clicked, this, &MainWindow::onRefresh);
    connect(btnAddIp,      &QPushButton::clicked, this, &MainWindow::onAddPeerByIp);
    connect(m_ipSearchInput, &QLineEdit::returnPressed, this, &MainWindow::onAddPeerByIp);
    connect(m_friendsList, &QListWidget::itemClicked,  this, &MainWindow::onFriendClicked);
    connect(m_groupsList,  &QListWidget::itemClicked,  this, &MainWindow::onGroupClicked);
    connect(m_friendsList, &QListWidget::customContextMenuRequested, this, [this](const QPoint& p){
        auto* item = m_friendsList->itemAt(p);
        if (!item) return;
        m_friendsList->setCurrentItem(item);
        QString id = item->data(Qt::UserRole).toString();

        QMenu menu;
        

        bool isFormer = !m_friendMgr->getFriend(id) &&
                        std::any_of(m_friendMgr->formerFriends().begin(),
                                    m_friendMgr->formerFriends().end(),
                                    [&](const FriendInfo& x){ return x.id == id.toStdString(); });

        if (isFormer) {
            // Former friend — limited menu: can only view history or permanently delete
            menu.addAction("💬 View History",      this, [this]{ onFriendClicked(m_friendsList->currentItem()); });
            menu.addSeparator();
            menu.addAction("🗑 Delete Conversation",this, &MainWindow::onCtxDeleteConversation);
            menu.addSeparator();
            menu.addAction("✕ Delete from List",   this, &MainWindow::onCtxDeleteFormerFriend);
        } else {
            // Active friend — full menu
            menu.addAction("💬 Open Chat",          this, [this]{ onFriendClicked(m_friendsList->currentItem()); });
            menu.addAction("📞 Voice Call",         this, &MainWindow::onCtxVoiceCall);
            menu.addAction("📹 Video Call",         this, &MainWindow::onCtxVideoCall);
            menu.addSeparator();
            menu.addAction("🗑 Delete Conversation",this, &MainWindow::onCtxDeleteConversation);
            menu.addSeparator();
            menu.addAction("✕ Remove Friend",       this, &MainWindow::onCtxRemoveFriend);
        }
        menu.exec(m_friendsList->mapToGlobal(p));
    });
    connect(m_groupsList, &QListWidget::customContextMenuRequested, this, [this](const QPoint& p){
        auto* item = m_groupsList->itemAt(p);
        if (!item) return;
        m_groupsList->setCurrentItem(item);
        QMenu menu;
        
        menu.addAction("💬 Open Group Chat",    this, [this]{ onGroupClicked(m_groupsList->currentItem()); });
        menu.addAction("📞 Group Call",         this, [this]{ onGroupVoiceCall(); });
        menu.addSeparator();
        menu.addAction("🗑 Delete Conversation",this, &MainWindow::onCtxDeleteConversation);
        menu.addSeparator();
        menu.addAction("⚙ Manage Group",       this, &MainWindow::onCtxManageGroup);
        auto* leaveAct = menu.addAction("✕ Leave Group", this, &MainWindow::onCtxLeaveGroup);
        menu.exec(m_groupsList->mapToGlobal(p));
    });

    // ── Message right-click: Copy / Reply / Forward / Delete ────────────────
    m_chatMsgList->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_chatMsgList, &QListWidget::customContextMenuRequested, this, [this](const QPoint& p){
        auto* item = m_chatMsgList->itemAt(p);
        if (!item) return;
        m_chatMsgList->setCurrentItem(item);
        QMenu menu;
        menu.addAction("📋 Copy Message", this, [this](){
            auto sel = m_chatMsgList->selectedItems();
            if (sel.isEmpty()) return;
            QString text;
            if (auto* w = m_chatMsgList->itemWidget(sel.first())) {
                const auto labels = w->findChildren<QLabel*>("msgText");
                if (!labels.isEmpty()) text = labels.first()->text();
            }
            if (text.isEmpty()) text = sel.first()->text();
            QGuiApplication::clipboard()->setText(text);
        });
        menu.addAction("↩ Reply", this, [this](){
            auto sel = m_chatMsgList->selectedItems();
            if (sel.isEmpty() || !m_chatInput) return;
            QString text;
            if (auto* w = m_chatMsgList->itemWidget(sel.first())) {
                const auto labels = w->findChildren<QLabel*>("msgText");
                if (!labels.isEmpty()) text = labels.first()->text();
            }
            if (text.isEmpty()) text = sel.first()->text();
            if (text.length() > 40) text = text.left(40) + QStringLiteral("…");
            m_chatInput->setFocus();
            m_chatInput->setText("▸ " + text + "  ");
        });
        menu.addAction("↗ Forward", this, [this](){
            auto sel = m_chatMsgList->selectedItems();
            if (sel.isEmpty()) return;
            QString text;
            if (auto* w = m_chatMsgList->itemWidget(sel.first())) {
                const auto labels = w->findChildren<QLabel*>("msgText");
                if (!labels.isEmpty()) text = labels.first()->text();
            }
            if (text.isEmpty()) text = sel.first()->text();
            if (m_chatInput) { m_chatInput->setText(text); m_chatInput->setFocus(); }
        });
        menu.addSeparator();
        menu.addAction("🗑 Delete Message", this, &MainWindow::onCtxDeleteMessages);
        menu.exec(m_chatMsgList->mapToGlobal(p));
    });
    m_groupMsgList->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_groupMsgList, &QListWidget::customContextMenuRequested, this, [this](const QPoint& p){
        auto* item = m_groupMsgList->itemAt(p);
        if (!item) return;
        m_groupMsgList->setCurrentItem(item);
        QMenu menu;
        menu.addAction("📋 Copy Message", this, [this](){
            auto sel = m_groupMsgList->selectedItems();
            if (sel.isEmpty()) return;
            QString text;
            if (auto* w = m_groupMsgList->itemWidget(sel.first())) {
                const auto labels = w->findChildren<QLabel*>("msgText");
                if (!labels.isEmpty()) text = labels.first()->text();
            }
            if (text.isEmpty()) text = sel.first()->text();
            QGuiApplication::clipboard()->setText(text);
        });
        menu.addAction("↩ Reply", this, [this](){
            auto sel = m_groupMsgList->selectedItems();
            if (sel.isEmpty() || !m_groupInput) return;
            QString text;
            if (auto* w = m_groupMsgList->itemWidget(sel.first())) {
                const auto labels = w->findChildren<QLabel*>("msgText");
                if (!labels.isEmpty()) text = labels.first()->text();
            }
            if (text.isEmpty()) text = sel.first()->text();
            if (text.length() > 40) text = text.left(40) + QStringLiteral("…");
            m_groupInput->setFocus();
            m_groupInput->setText("▸ " + text + "  ");
        });
        menu.addAction("↗ Forward", this, [this](){
            auto sel = m_groupMsgList->selectedItems();
            if (sel.isEmpty()) return;
            QString text;
            if (auto* w = m_groupMsgList->itemWidget(sel.first())) {
                const auto labels = w->findChildren<QLabel*>("msgText");
                if (!labels.isEmpty()) text = labels.first()->text();
            }
            if (text.isEmpty()) text = sel.first()->text();
            if (m_groupInput) { m_groupInput->setText(text); m_groupInput->setFocus(); }
        });
        menu.addSeparator();
        menu.addAction("🗑 Delete Message", this, &MainWindow::onCtxDeleteMessages);
        menu.exec(m_groupMsgList->mapToGlobal(p));
    });

    connect(btnBack,    &QPushButton::clicked, this, &MainWindow::onDiscoverClicked);
    connect(btnGrpBack, &QPushButton::clicked, this, &MainWindow::onDiscoverClicked);
    connect(btnSend,    &QPushButton::clicked, this, &MainWindow::onChatSend);
    connect(m_chatInput, &QLineEdit::returnPressed, this, &MainWindow::onChatSend);
    connect(m_chatInput, &QLineEdit::textChanged, this, [this](const QString& txt) {
        if (!m_activeFriend || txt.isEmpty()) return;
        // Send typing signal (debounced — only if not already sent recently)
        if (m_typingDebounce && !m_typingDebounce->isActive()) {
            SigMsg sig = buildSig(SigType::Typing);
            SignalingClient::send(QString::fromStdString(m_activeFriend->ip), sig);
        }
        if (m_typingDebounce) m_typingDebounce->start();
    });
    connect(btnSendImg,  &QPushButton::clicked, this, &MainWindow::onChatSendImage);
    connect(btnSendFile, &QPushButton::clicked, this, &MainWindow::onChatSendFile);
    connect(btnChatVoice, &QPushButton::clicked, this, &MainWindow::onChatVoiceCall);
    connect(btnChatVideo, &QPushButton::clicked, this, &MainWindow::onChatVideoCall);

    connect(btnGrpSend, &QPushButton::clicked, this, &MainWindow::onGroupSend);
    connect(m_groupInput, &QLineEdit::returnPressed, this, &MainWindow::onGroupSend);
    connect(btnGrpSendImg,  &QPushButton::clicked, this, &MainWindow::onGroupSendImage);
    connect(btnGrpSendFile, &QPushButton::clicked, this, &MainWindow::onGroupSendFile);
    connect(btnGrpVoice,    &QPushButton::clicked, this, &MainWindow::onGroupVoiceCall);
    connect(btnNewGrp,      &QPushButton::clicked, this, &MainWindow::onNewGroup);

    // Voice note buttons – press and hold
    btnVoiceNote->setCheckable(false);
    connect(btnVoiceNote, &QPushButton::pressed,  this, &MainWindow::onVoiceNotePress);
    connect(btnVoiceNote, &QPushButton::released, this, &MainWindow::onVoiceNoteRelease);
    btnGrpVoiceNote->setCheckable(false);
    connect(btnGrpVoiceNote, &QPushButton::pressed,  this, &MainWindow::onGroupVoiceNotePress);
    connect(btnGrpVoiceNote, &QPushButton::released, this, &MainWindow::onGroupVoiceNoteRelease);

    // ── Start services (deferred — lets the window paint its first frame first)
    QTimer::singleShot(0, this, [this]() {
        // Re-resolve local IP now that Qt's network stack is fully initialised
        m_localIp = QString::fromStdString(Helpers::getLocalIp());
        m_lblMyName->setText(m_myName + "  ·  " + m_localIp);

        m_discovery = new PeerDiscovery(m_myId, m_myName, this);
        connect(m_discovery, &PeerDiscovery::peersUpdated, this, &MainWindow::onPeersUpdated);
        connect(m_discovery, &PeerDiscovery::diagLog,      this, &MainWindow::onDiagLog);
        m_discovery->start();
        startRefreshAnimation();

        m_sigServer = new SignalingServer(this);
        m_sigServer->setIdentity(m_myId, m_myName);
        connect(m_sigServer, &SignalingServer::messageReceived, this, &MainWindow::onSignalReceived);
        m_sigServer->start();

        // ── Typing / upload indicator timers ──────────────────────────────────
        // Debounce: fires 2 s after user stops typing, so we don't spam signals
        m_typingDebounce = new QTimer(this);
        m_typingDebounce->setInterval(2000);
        m_typingDebounce->setSingleShot(true);

        // Auto-hide "is typing" label after 4 s of no new typing signal
        m_typingHideTimer = new QTimer(this);
        m_typingHideTimer->setInterval(4000);
        m_typingHideTimer->setSingleShot(true);
        connect(m_typingHideTimer, &QTimer::timeout, this, [this]() {
            if (m_lblChatStatus && m_lblChatStatus->text().contains("typing"))
                m_lblChatStatus->setVisible(false);
        });

        // Auto-hide "is uploading" label after 30 s (fallback if upload_end missed)
        m_uploadHideTimer = new QTimer(this);
        m_uploadHideTimer->setInterval(30000);
        m_uploadHideTimer->setSingleShot(true);
        connect(m_uploadHideTimer, &QTimer::timeout, this, [this]() {
            if (m_lblChatStatus && m_lblChatStatus->text().contains("uploading"))
                m_lblChatStatus->setVisible(false);
        });
        // covers both runtime changes (incoming request) and the deferred load().
        connect(m_friendMgr, &FriendManager::pendingChanged, this, [this](){
            rebuildRequestsList();
            refreshRequestsBadge();
        });
        connect(m_friendMgr, &FriendManager::friendsChanged, this, [this](){
            rebuildFriendsList();
        });
        connect(m_friendMgr, &FriendManager::groupsChanged, this, [this](){
            rebuildGroupsList();
        });
    });

    // ── Load persisted data — delayed 80 ms so FriendManager::load() (which is
    //    singleShot(0) inside the constructor) has time to finish first.
    QTimer::singleShot(80, this, [this]() {
        rebuildFriendsList();
        rebuildGroupsList();
        rebuildRequestsList();
        refreshRequestsBadge();
        syncGroupMembers();
    });

    showDiscover();

    // Window icon — same search order as main.cpp so both the taskbar entry
    // and the window title-bar chrome show the icon on every platform.
    {
        const QStringList candidates = {
            QCoreApplication::applicationDirPath() + "/icon.ico",
            QCoreApplication::applicationDirPath() + "/LocalCall.png",
            QCoreApplication::applicationDirPath() + "/../Resources/icon.icns",
            ":/icon.ico",
            ":/icon.png",
        };
        for (const QString& p : candidates) {
            QIcon ic(p);
            if (!ic.isNull()) { setWindowIcon(ic); break; }
        }
    }

    applyDarkTitleBar();
}

MainWindow::~MainWindow() {}

void MainWindow::closeEvent(QCloseEvent* e)
{
    // Stop services first so no new work is queued
    if (m_discovery) m_discovery->stop();
    if (m_sigServer)  m_sigServer->stop();
#if defined(HAS_MULTIMEDIA) || defined(HAS_OPENCV)
    if (m_callWin)    m_callWin->doClose();
#endif

    // Wait up to 1 s for the global thread pool to drain (TCP scan futures).
    // After that, force exit — we never want the process to outlive the window.
    QThreadPool::globalInstance()->waitForDone(1000);

    e->accept();
    QApplication::quit();   // explicit quit so the event loop always exits
}

void MainWindow::applyDarkTitleBar()
{
#ifdef Q_OS_WIN
    HWND hwnd = reinterpret_cast<HWND>(winId());
    BOOL val = TRUE;
    // DWMWA_USE_IMMERSIVE_DARK_MODE = 20 (Windows 10 1903+) or 19 (older)
    if (FAILED(DwmSetWindowAttribute(hwnd, 20, &val, sizeof(val))))
        DwmSetWindowAttribute(hwnd, 19, &val, sizeof(val));
#endif
}
