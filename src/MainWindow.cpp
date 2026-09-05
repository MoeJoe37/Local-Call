#include "nlohmann/json.hpp"
using json = nlohmann::json;
#include "MainWindow.h"
#include "Helpers.h"
#include "UiTheme.h"
#include "SignalingClient.h"
#include "NotificationWindow.h"
#include "InputDialog.h"
#include "GroupCreateDialog.h"
#include "GroupManageDialog.h"
#include "ChatView.h"
#include "ChatBubble.h"
#include "ChatComposer.h"
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
#include <QFrame>
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


// ══════════════════════════════════════════════════════════════════════════════
//  BUILD SIDEBAR
// ══════════════════════════════════════════════════════════════════════════════

static QLabel* makeStatusDot(QWidget* parent, bool online = false) {
    auto* dot = new QLabel(parent);
    dot->setObjectName("statusDot");
    dot->setFixedSize(10, 10);
    UiTheme::setClass(dot, online ? "on" : "off");
    return dot;
}

static QWidget* makeSectionHeader(const QString& text, QWidget* parent) {
    auto* lbl = new QLabel(text.toUpper(), parent);
    lbl->setProperty("class", "sectionHeader");
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

    m_security = new SecurityManager(this);
    if (!m_security->loadOrCreate()) {
        QMessageBox::warning(this, "Security",
            "Could not create the local Ed25519 identity. Secure calls will be disabled for this session.");
    }

    setWindowTitle("Local Call");
    setMinimumSize(900, 580);
    resize(1100, 680);

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
    auto* btnEdit = new QPushButton("", sideTop);
    UiTheme::applyIcon(btnEdit, "edit", 16);
    btnEdit->setFixedSize(28, 28);
    btnEdit->setToolTip("Edit profile");
    btnEdit->setObjectName("btnEditProfile");
    sideTopLayout->addWidget(m_lblMyName, 1);
    sideTopLayout->addWidget(btnEdit);
    sideLayout->addWidget(sideTop);

    // Discover button
    auto* discBtn = new QPushButton("Discover Peers", sidebar);
    UiTheme::applyIcon(discBtn, "discover", 16);
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
    reqLbl->setProperty("class", "sectionHeader");
    m_lblRequestCount = new QLabel("0", reqHeaderW);
    m_lblRequestCount->setObjectName("requestBadge");
    reqHeaderL->addWidget(reqLbl, 1);
    reqHeaderL->addWidget(m_lblRequestCount);
    reqSecLayout->addWidget(reqHeaderW);

    // Inline list — items built by rebuildRequestsList()
    m_requestsList = new QListWidget(m_requestsSection);
    m_requestsList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_requestsList->setResizeMode(QListView::Adjust);
    m_requestsList->setUniformItemSizes(false);
    m_requestsList->setObjectName("requestsList");
    reqSecLayout->addWidget(m_requestsList);

    // Divider below the section
    auto* reqDivider = new QWidget(m_requestsSection);
    reqDivider->setFixedHeight(1);
    reqDivider->setObjectName("requestsDivider");
    reqSecLayout->addWidget(reqDivider);

    sideLayout->addWidget(m_requestsSection);
    m_requestsSection->setVisible(false);   // hidden until requests arrive

    // ── FRIENDS section ───────────────────────────────────────────────────────
    sideLayout->addWidget(makeSectionHeader("Friends", sidebar));
    m_friendsList = new QListWidget(sidebar);
    m_friendsList->setObjectName("friendsList");
    m_friendsList->setContextMenuPolicy(Qt::CustomContextMenu);
    m_friendsList->setSpacing(2);
    sideLayout->addWidget(m_friendsList, 1);

    // Groups section
    auto* grpHeader = new QHBoxLayout();
    grpHeader->setContentsMargins(14,8,8,2);
    auto* grpLbl = new QLabel("GROUPS", sidebar);
    grpLbl->setProperty("class", "sectionHeader");
    auto* btnNewGrp = new QPushButton("", sidebar);
    UiTheme::applyIcon(btnNewGrp, "add", 16);
    btnNewGrp->setFixedSize(24, 24);
    btnNewGrp->setIconSize(QSize(16,16));
    btnNewGrp->setToolTip("New group");
    btnNewGrp->setObjectName("btnNewGroup");
    grpHeader->addWidget(grpLbl, 1);
    grpHeader->addWidget(btnNewGrp);
    auto* grpHeaderWidget = new QWidget(sidebar);
    grpHeaderWidget->setLayout(grpHeader);
    sideLayout->addWidget(grpHeaderWidget);

    m_groupsList = new QListWidget(sidebar);
    m_groupsList->setObjectName("groupsList");
    m_groupsList->setContextMenuPolicy(Qt::CustomContextMenu);
    m_groupsList->setSpacing(2);
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
    discHeader->setObjectName("discoverHeader");
    auto* discHeaderLayout = new QHBoxLayout(discHeader);
    discHeaderLayout->setContentsMargins(16,12,12,12);
    auto* discTitle = new QLabel("Discover Peers", discHeader);
    discTitle->setObjectName("discoverTitle");
    m_btnRefresh = new QPushButton("Scan", discHeader);
    UiTheme::applyIcon(m_btnRefresh, "refresh", 16);
    m_btnRefresh->setObjectName("btnRefresh");
    discHeaderLayout->addWidget(discTitle, 1);
    discHeaderLayout->addWidget(m_btnRefresh);

    // Spinner: cycles arrow frames until the next peersUpdated signal
    m_refreshAniTimer = new QTimer(this);
    m_refreshAniTimer->setInterval(120);
    connect(m_refreshAniTimer, &QTimer::timeout, this, [this]() {
        static const QString frames[] = {
            "Scanning .   ", "Scanning ..  ",
            "Scanning ... ", "Scanning ...."
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
    ipRow->setObjectName("ipRow");
    auto* ipLayout = new QHBoxLayout(ipRow);
    ipLayout->setContentsMargins(12,8,12,8);
    ipLayout->setSpacing(6);
    m_ipSearchInput = new QLineEdit(ipRow);
    m_ipSearchInput->setPlaceholderText("Add by IP address…");
    m_ipSearchInput->setObjectName("ipSearchInput");
    auto* btnAddIp = new QPushButton("", ipRow);
    UiTheme::applyIcon(btnAddIp, "send", 16);
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
    auto* btnBack = new QPushButton("", chatHeader);
    UiTheme::applyIcon(btnBack, "back", 20);
    btnBack->setFixedSize(34, 34);
    btnBack->setIconSize(QSize(20,20));
    btnBack->setObjectName("headerBackBtn");
    m_chatStatusDot = makeStatusDot(chatHeader);
    m_chatName = new QLabel("", chatHeader);
    m_chatName->setObjectName("chatTitle");
    auto* btnChatVoice = new QPushButton("", chatHeader);
    auto* btnChatVideo = new QPushButton("", chatHeader);
    auto* btnChatScreen = new QPushButton("", chatHeader);
    UiTheme::applyIcon(btnChatVoice, "call", 22);
    UiTheme::applyIcon(btnChatVideo, "video", 22);
    UiTheme::applyIcon(btnChatScreen, "screen", 22);
    btnChatVoice->setToolTip("Voice call");
    btnChatVideo->setToolTip("Video call");
    btnChatScreen->setToolTip("Share screen");
    for (auto* b : {btnChatVoice, btnChatVideo, btnChatScreen}) {
        b->setFixedSize(36, 36);
        b->setIconSize(QSize(22,22));
        b->setObjectName("headerActionBtn");
    }
    m_btnChatVoice = btnChatVoice;
    m_btnChatVideo = btnChatVideo;
    m_btnChatScreen = btnChatScreen;
    chatHeaderLayout->addWidget(btnBack);
    chatHeaderLayout->addWidget(m_chatStatusDot);
    chatHeaderLayout->addSpacing(6);
    chatHeaderLayout->addWidget(m_chatName, 1);
    // Ping is intentionally not shown in the active chat header. It is shown
    // only beside each friend in the sidebar and inside call/screen-share windows.
    m_chatPingLabel = nullptr;
    chatHeaderLayout->addWidget(btnChatVoice);
    chatHeaderLayout->addWidget(btnChatVideo);
    chatHeaderLayout->addWidget(btnChatScreen);
    chatLayout->addWidget(chatHeader);

    // Read-only banner
    m_chatReadOnlyBanner = new QWidget();
    m_chatReadOnlyBanner->setObjectName("readOnlyBanner");
    auto* roBannerLayout = new QHBoxLayout(m_chatReadOnlyBanner);
    roBannerLayout->setContentsMargins(14,8,14,8);
    auto* roIcon = new QLabel(m_chatReadOnlyBanner);
    roIcon->setPixmap(UiTheme::icon("lock").pixmap(16,16));
    roBannerLayout->addWidget(roIcon);
    roBannerLayout->addWidget(new QLabel("This conversation is read-only. You are no longer friends.", m_chatReadOnlyBanner));
    chatLayout->addWidget(m_chatReadOnlyBanner);
    m_chatReadOnlyBanner->setVisible(false);

    // Message area
    m_chatView = new ChatView(m_panelChat);
    chatLayout->addWidget(m_chatView, 1);

    // Composer — one card holding attach, input, voice and send, plus a
    // permanently reserved status band, so typing/upload notices and the reply
    // chip can never shove the message list around.
    m_chatComposer = new ChatComposer(m_panelChat);
    m_chatComposer->setPlaceholderText("Type a message…  (Shift+Enter for a new line)");
    chatLayout->addWidget(m_chatComposer);

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
    auto* btnGrpBack = new QPushButton("", groupHeader);
    UiTheme::applyIcon(btnGrpBack, "back", 20);
    btnGrpBack->setFixedSize(34, 34);
    btnGrpBack->setIconSize(QSize(20,20));
    btnGrpBack->setObjectName("headerBackBtn");
    m_groupName = new QLabel("", groupHeader);
    m_groupName->setObjectName("groupTitle");
    auto* btnGrpVoice  = new QPushButton("", groupHeader);
    UiTheme::applyIcon(btnGrpVoice, "call", 22);
    btnGrpVoice->setFixedSize(36, 36);
    btnGrpVoice->setIconSize(QSize(22,22));
    btnGrpVoice->setObjectName("headerActionBtn");
    grpHeaderLayout->addWidget(btnGrpBack);
    grpHeaderLayout->addWidget(m_groupName, 1);
    grpHeaderLayout->addWidget(btnGrpVoice);
    grpMainLayout->addWidget(groupHeader);

    m_groupView = new ChatView(grpMain);
    m_groupView->setShowAvatars(true);      // several speakers; faces help
    grpMainLayout->addWidget(m_groupView, 1);

    m_groupComposer = new ChatComposer(grpMain);
    m_groupComposer->setPlaceholderText("Message group…  (Shift+Enter for a new line)");
    grpMainLayout->addWidget(m_groupComposer);

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
    m_grpMemberList->setObjectName("grpMemberList");
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
            menu.addAction(UiTheme::icon("chat"), "View History",      this, [this]{ onFriendClicked(m_friendsList->currentItem()); });
            menu.addSeparator();
            menu.addAction(UiTheme::icon("delete"), "Delete Conversation",this, &MainWindow::onCtxDeleteConversation);
            menu.addSeparator();
            menu.addAction(UiTheme::icon("close"), "Delete from List",   this, &MainWindow::onCtxDeleteFormerFriend);
        } else {
            // Active friend — full menu
            menu.addAction(UiTheme::icon("chat"), "Open Chat",          this, [this]{ onFriendClicked(m_friendsList->currentItem()); });
            menu.addAction(UiTheme::icon("call"), "Voice Call",         this, &MainWindow::onCtxVoiceCall);
            menu.addAction(UiTheme::icon("video"), "Video Call",         this, &MainWindow::onCtxVideoCall);
            menu.addSeparator();
            menu.addAction(UiTheme::icon("delete"), "Delete Conversation",this, &MainWindow::onCtxDeleteConversation);
            menu.addSeparator();
            menu.addAction(UiTheme::icon("close"), "Remove Friend",       this, &MainWindow::onCtxRemoveFriend);
        }
        menu.exec(m_friendsList->mapToGlobal(p));
    });
    connect(m_groupsList, &QListWidget::customContextMenuRequested, this, [this](const QPoint& p){
        auto* item = m_groupsList->itemAt(p);
        if (!item) return;
        m_groupsList->setCurrentItem(item);
        QMenu menu;
        
        menu.addAction(UiTheme::icon("chat"), "Open Group Chat",    this, [this]{ onGroupClicked(m_groupsList->currentItem()); });
        menu.addAction(UiTheme::icon("call"), "Group Call",         this, [this]{ onGroupVoiceCall(); });
        menu.addSeparator();
        menu.addAction(UiTheme::icon("delete"), "Delete Conversation",this, &MainWindow::onCtxDeleteConversation);
        menu.addSeparator();
        menu.addAction(UiTheme::icon("settings"), "Manage Group",       this, &MainWindow::onCtxManageGroup);
        auto* leaveAct = menu.addAction(UiTheme::icon("close"), "Leave Group", this, &MainWindow::onCtxLeaveGroup);
        menu.exec(m_groupsList->mapToGlobal(p));
    });

    // ── Message right-click: Copy / Reply / Forward / Delete ────────────────
    // Message text is rich text now (URLs are clickable), so the plain string
    // comes off the bubble instead of out of the label.
    auto installMsgMenu = [this](ChatView* view, ChatComposer* composer) {
        view->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(view, &QListWidget::customContextMenuRequested, this,
                [this, view, composer](const QPoint& p) {
            QListWidgetItem* item = view->itemAt(p);
            if (!item) return;
            if (!item->isSelected()) view->setCurrentItem(item);

            ChatBubble* bubble = view->firstSelectedBubble();
            const QString text = bubble ? bubble->plainText() : QString();

            QMenu menu;
            auto* copyAct = menu.addAction(UiTheme::icon("copy"), "Copy Message", this, [text]() {
                QGuiApplication::clipboard()->setText(text);
            });
            copyAct->setEnabled(!text.isEmpty());
            menu.addAction(UiTheme::icon("back"), "Reply", this, [bubble, composer]() {
                if (bubble) composer->setReplyTarget(bubble->timestamp(),
                                                     bubble->senderName(),
                                                     bubble->plainText());
            });
            auto* fwdAct = menu.addAction(UiTheme::icon("send"), "Forward", this,
                                          [text, composer]() { composer->setText(text); });
            fwdAct->setEnabled(!text.isEmpty());
            menu.addSeparator();
            menu.addAction(UiTheme::icon("delete"), "Delete Message",
                           this, &MainWindow::onCtxDeleteMessages);
            menu.exec(view->mapToGlobal(p));
        });
    };
    installMsgMenu(m_chatView,  m_chatComposer);
    installMsgMenu(m_groupView, m_groupComposer);

    connect(btnBack,    &QPushButton::clicked, this, &MainWindow::onDiscoverClicked);
    connect(btnGrpBack, &QPushButton::clicked, this, &MainWindow::onDiscoverClicked);

    // ── Chat composer ───────────────────────────────────────────────────────
    connect(m_chatComposer, &ChatComposer::sendRequested, this, &MainWindow::sendChatText);
    connect(m_chatComposer, &ChatComposer::attachRequested, this, [this](bool imagesOnly) {
        if (imagesOnly) onChatSendImage();
        else            onChatSendFile();
    });
    connect(m_chatComposer, &ChatComposer::typing, this, [this]() {
        if (!m_activeFriend) return;
        // Debounced — one Typing signal per burst, not one per keystroke.
        if (m_typingDebounce && !m_typingDebounce->isActive()) {
            SigMsg sig = buildSig(SigType::Typing);
            sig.target_id = m_activeFriend->id;
            sendDirectSignal(QString::fromStdString(m_activeFriend->ip), sig, false);
        }
        if (m_typingDebounce) m_typingDebounce->start();
    });
    connect(m_chatView, &ChatView::replyRequested, this,
            [this](int64_t ts, const QString& name, const QString& snippet) {
        m_chatComposer->setReplyTarget(ts, name, snippet);
    });
    connect(m_chatView, &ChatView::deleteRequested, this, &MainWindow::onCtxDeleteMessages);

    connect(btnChatVoice,  &QPushButton::clicked, this, &MainWindow::onChatVoiceCall);
    connect(btnChatVideo,  &QPushButton::clicked, this, &MainWindow::onChatVideoCall);
    connect(btnChatScreen, &QPushButton::clicked, this, [this]() {
        if (m_activeFriend) sendCallInvite(m_activeFriend, "screen");
    });

    // ── Group composer ──────────────────────────────────────────────────────
    connect(m_groupComposer, &ChatComposer::sendRequested, this, &MainWindow::sendGroupText);
    connect(m_groupComposer, &ChatComposer::attachRequested, this, [this](bool imagesOnly) {
        if (imagesOnly) onGroupSendImage();
        else            onGroupSendFile();
    });
    connect(m_groupView, &ChatView::replyRequested, this,
            [this](int64_t ts, const QString& name, const QString& snippet) {
        m_groupComposer->setReplyTarget(ts, name, snippet);
    });
    connect(m_groupView, &ChatView::deleteRequested, this, &MainWindow::onCtxDeleteMessages);
    connect(btnGrpVoice, &QPushButton::clicked, this, &MainWindow::onGroupVoiceCall);
    connect(btnNewGrp,   &QPushButton::clicked, this, &MainWindow::onNewGroup);

    // Voice notes — click to start, click again to stop and send. That beats
    // press-and-hold on Windows touchpads, where the release event can be
    // swallowed when the cursor leaves the button.
#ifdef HAS_MULTIMEDIA
    connect(m_chatComposer, &ChatComposer::voiceRecordToggled, this, [this](bool on) {
        if (on) {
            onVoiceNotePress();
            m_chatComposer->setRecording(m_vnRec && m_vnRec->isRecording());
        } else {
            m_chatComposer->setRecording(false);
            onVoiceNoteRelease();
        }
    });
    connect(m_chatComposer, &ChatComposer::voiceRecordCancelled,
            this, &MainWindow::onVoiceNoteCancel);

    connect(m_groupComposer, &ChatComposer::voiceRecordToggled, this, [this](bool on) {
        if (on) {
            onGroupVoiceNotePress();
            m_groupComposer->setRecording(m_vnRecGroup && m_vnRecGroup->isRecording());
        } else {
            m_groupComposer->setRecording(false);
            onGroupVoiceNoteRelease();
        }
    });
    connect(m_groupComposer, &ChatComposer::voiceRecordCancelled,
            this, &MainWindow::onGroupVoiceNoteCancel);
#else
    m_chatComposer ->setVoiceEnabled(false, "Voice notes need Qt Multimedia");
    m_groupComposer->setVoiceEnabled(false, "Voice notes need Qt Multimedia");
#endif

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
        startPingMonitor();

        // ── Typing / upload indicator timers ──────────────────────────────────
        // Debounce: fires 2 s after user stops typing, so we don't spam signals
        m_typingDebounce = new QTimer(this);
        m_typingDebounce->setInterval(2000);
        m_typingDebounce->setSingleShot(true);

        // Auto-hide the "is typing" ghost after 4 s of no new typing signal
        m_typingHideTimer = new QTimer(this);
        m_typingHideTimer->setInterval(4000);
        m_typingHideTimer->setSingleShot(true);
        connect(m_typingHideTimer, &QTimer::timeout, this, [this]() {
            if (m_chatView) m_chatView->setTypingIndicator(false);
        });

        // Auto-hide "is uploading" after 30 s (fallback if upload_end is missed)
        m_uploadHideTimer = new QTimer(this);
        m_uploadHideTimer->setInterval(30000);
        m_uploadHideTimer->setSingleShot(true);
        connect(m_uploadHideTimer, &QTimer::timeout, this, [this]() {
            if (m_chatComposer) m_chatComposer->setStatusText(QString());
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
        QIcon themed = QIcon::fromTheme("localcall");
        if (!themed.isNull()) {
            setWindowIcon(themed);
        } else {
            for (const QString& p : candidates) {
                QIcon ic(p);
                if (!ic.isNull()) { setWindowIcon(ic); break; }
            }
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
#ifdef HAS_MEDIA_AUDIO
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
