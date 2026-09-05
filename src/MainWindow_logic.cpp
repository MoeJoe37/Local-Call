#include "nlohmann/json.hpp"
using json = nlohmann::json;
// MainWindow logic — appended into the same translation unit via #include in main build
// This file contains: panel switching, peer updates, signal dispatch, chat, groups, calls
#include "MainWindow.h"
#include <QApplication>
#include <QProgressBar>
#include <QProcess>
#include <QDir>
#include <QLineEdit>
#include <QPushButton>
#include <QTcpSocket>
#include <QtConcurrent>
#include <QRegularExpression>
#include <QStringList>
#include <QTemporaryDir>
#include <QUuid>
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
#include <QListWidgetItem>
#include <QLabel>
#include <QTextEdit>
#include <QTextOption>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QFileDialog>
#include <QMessageBox>
#include <QFileInfo>
#include <QFile>
#include <QTimer>
#include <QDateTime>
#include <QDesktopServices>
#include <QThread>
#include <QVector>
#include <QUrl>
#include <QIcon>
#include <QElapsedTimer>
#include <QPointer>
#ifdef HAS_MULTIMEDIA
#include <QMediaPlayer>
#include <QAudioOutput>
#endif
#include "MediaSettings.h"

// Forward declaration
static QString buildConvKey(const QString& a, const QString& b);

#include <QPixmap>
#include <QBuffer>
#include <QEvent>
#include <algorithm>

// ══════════════════════════════════════════════════════════════════════════════
//  PANEL SWITCHING
// ══════════════════════════════════════════════════════════════════════════════

void MainWindow::showDiscover()
{
    m_panels->setCurrentWidget(m_panelDiscover);
    m_activeFriend = nullptr;
    m_activeGroup  = nullptr;
    m_friendsList->clearSelection();
    m_groupsList->clearSelection();
}

void MainWindow::showChat(FriendInfo* f)
{
    m_activeFriend = f;
    m_activeGroup  = nullptr;

    f->unreadCount = 0;
    rebuildFriendsList();

    QString key = buildConvKey(m_myId, QString::fromStdString(f->id));
    if (m_chatConvKey != key) {
        m_chatConvKey = key;
        m_chatView->clearMessages();
        auto history = m_chatStore->load(key);
        for (const auto& msg : history) m_chatView->appendMessage(msg);
        m_chatView->scrollToLatest();
    }

    m_chatName->setText(QString::fromStdString(f->name));
    refreshActiveChatPing();
    UiTheme::setClass(m_chatStatusDot, f->isOnline ? "on" : "off");

    // Reset status indicators for the new conversation
    m_chatView->setTypingIndicator(false);
    m_chatComposer->setStatusText(QString());
    m_chatComposer->setProgress(-1);
    m_chatComposer->clearReply();
    if (m_typingHideTimer)  m_typingHideTimer->stop();
    if (m_uploadHideTimer)  m_uploadHideTimer->stop();

    bool isFormer = std::any_of(m_friendMgr->formerFriends().begin(),
                                m_friendMgr->formerFriends().end(),
                                [&](const FriendInfo& x){ return x.id == f->id; });
    setChatReadOnly(isFormer);

    m_panels->setCurrentWidget(m_panelChat);
    if (!isFormer) m_chatComposer->focusInput();
    m_chatView->scrollToLatest();
}

void MainWindow::setChatReadOnly(bool ro)
{
    m_chatReadOnlyBanner->setVisible(ro);
    m_chatComposer->setReadOnly(ro);
    m_btnChatVoice->setEnabled(!ro);
    m_btnChatVideo->setEnabled(!ro);
    if (m_btnChatScreen) m_btnChatScreen->setEnabled(!ro);
}

void MainWindow::showGroupChat(GroupInfo* g)
{
    m_activeGroup  = g;
    m_activeFriend = nullptr;

    QString key = QString("grp-%1").arg(QString::fromStdString(g->groupId));
    if (m_groupConvKey != key) {
        m_groupConvKey = key;
        m_groupView->clearMessages();
        m_groupComposer->clearReply();
        syncGroupMembers(g);
        auto history = m_chatStore->load(key);
        for (const auto& msg : history) m_groupView->appendMessage(msg);
        m_groupView->scrollToLatest();
    }

    m_groupName->setText(QString::fromStdString(g->name));

    m_grpMemberList->clear();
    for (auto* mem : g->members)
        m_grpMemberList->addItem(QString::fromStdString(mem->name));

    m_panels->setCurrentWidget(m_panelGroup);
    m_groupComposer->focusInput();
    m_groupView->scrollToLatest();
}

// ══════════════════════════════════════════════════════════════════════════════
//  SIDEBAR CLICKS
// ══════════════════════════════════════════════════════════════════════════════

void MainWindow::onDiscoverClicked()   { showDiscover(); }

void MainWindow::onFriendClicked(QListWidgetItem* item)
{
    if (!item) return;
    QString id = item->data(Qt::UserRole).toString();
    FriendInfo* f = m_friendMgr->getFriend(id);
    if (!f) {
        // former friend
        for (auto& fi : m_friendMgr->formerFriends())
            if (fi.id == id.toStdString()) { showChat(const_cast<FriendInfo*>(&fi)); return; }
        return;
    }
    showChat(f);
}

void MainWindow::onGroupClicked(QListWidgetItem* item)
{
    if (!item) return;
    QString gid = item->data(Qt::UserRole).toString();
    GroupInfo* g = m_friendMgr->getGroup(gid);
    if (g) showGroupChat(g);
}

// ══════════════════════════════════════════════════════════════════════════════
//  PEER DISCOVERY
// ══════════════════════════════════════════════════════════════════════════════

void MainWindow::onPeersUpdated(QMap<QString, PeerInfo> peers)
{
    m_peers = peers;

    // DHCP tracking: whenever a known friend is seen broadcasting, update their
    // stored IP immediately so we always reach them even after a lease change.
    for (auto it = peers.cbegin(); it != peers.cend(); ++it) {
        QString id = QString::fromStdString(it.value().id);
        QString ip = QString::fromStdString(it.value().ip);
        FriendInfo* f = m_friendMgr->getFriend(id);
        if (f && QString::fromStdString(f->ip) != ip) {
            m_friendMgr->updateFriendIp(id, ip);
            // Also update the name if it changed
            if (f->name != it.value().name) {
                f->name = it.value().name;
                m_friendMgr->saveFriendsDirect();
            }
        }
    }

    stopRefreshAnimation();
    updateFriendOnlineStatus(peers);
    rebuildPeersList();
}

void MainWindow::updateFriendOnlineStatus(const QMap<QString, PeerInfo>& peers)
{
    for (auto& f : m_friendMgr->friends()) {
        auto it = peers.find(QString::fromStdString(f.id));
        bool online = (it != peers.end());
        f.isOnline = online;
        if (online) m_friendMgr->updateFriendIp(QString::fromStdString(f.id),
                                                  QString::fromStdString(it->ip));
    }
    rebuildFriendsList();

    if (m_activeFriend) {
        bool on = m_activeFriend->isOnline;
        UiTheme::setClass(m_chatStatusDot, on ? "on" : "off");
        refreshActiveChatPing();
    }
}

void MainWindow::onDiagLog(const QString& msg) { m_statusLabel->setText(msg); }

void MainWindow::startRefreshAnimation()
{
    if (!m_btnRefresh || !m_refreshAniTimer) return;
    m_refreshAniStep = 0;
    m_btnRefresh->setEnabled(false);
    m_refreshAniTimer->start();
}

void MainWindow::stopRefreshAnimation()
{
    if (!m_btnRefresh || !m_refreshAniTimer) return;
    m_refreshAniTimer->stop();
    m_btnRefresh->setText("Scan");
    m_btnRefresh->setIcon(QIcon(":/icons/refresh.png"));
    m_btnRefresh->setEnabled(true);
}

void MainWindow::onRefresh()
{
    if (!m_discovery) return;
    m_statusLabel->setText("Rescanning…");
    startRefreshAnimation();
    m_discovery->forceRescan();
}

void MainWindow::rebuildPeersList()
{
    m_peerList->clear();
    for (auto it = m_peers.cbegin(); it != m_peers.cend(); ++it) {
        const PeerInfo& p = it.value();
        QString id   = QString::fromStdString(p.id);
        QString name = QString::fromStdString(p.name);
        QString ip   = QString::fromStdString(p.ip);
        if (m_friendMgr->hasFriend(id)) continue;

        auto* item = new QListWidgetItem(m_peerList);

        // Per-row widget: green dot  name  IP  [+]
        auto* w  = new QWidget();
        auto* wl = new QHBoxLayout(w);
        wl->setContentsMargins(10, 6, 8, 6);
        wl->setSpacing(8);

        auto* dot = new QLabel(w);
        dot->setFixedSize(10, 10);
        dot->setObjectName("statusDot");
        UiTheme::setClass(dot, "on");

        auto* lblName = new QLabel(name, w);
        lblName->setObjectName("peerName");
        lblName->setMinimumWidth(0);
        // Show IP on hover — keeps the row uncluttered
        w->setToolTip(name + "  —  " + ip);

        auto* btnAdd = new QPushButton("+", w);
        btnAdd->setFixedSize(30, 30);
        btnAdd->setToolTip("Send friend request to " + name);
        btnAdd->setObjectName("btnAddPeer");

        wl->addWidget(dot);
        wl->addWidget(lblName, 1);
        wl->addWidget(btnAdd);

        // Enough height for 30 px button + 12 px vertical margins — never clip
        w->setMinimumHeight(52);
        item->setSizeHint(QSize(0, 54));   // 0 = defer width to list; 54 px = full row
        m_peerList->setItemWidget(item, w);

        // Capture by value — no currentItem() needed, no crash possible
        connect(btnAdd, &QPushButton::clicked, this, [this, id, ip, name, btnAdd]() {
            if (m_friendMgr->hasFriend(id)) return;
            if (m_sentReqIds.contains(id)) {
                m_statusLabel->setText("Request already sent to " + name + ".");
                return;
            }
            m_sentReqIds.insert(id);
            btnAdd->setEnabled(false);
            btnAdd->setText("");
            btnAdd->setIcon(UiTheme::icon("check"));
            btnAdd->setIconSize(QSize(16,16));

            SigMsg sig = buildSig(SigType::FriendReq);
            sendDirectSignal(ip, sig, true);
            m_statusLabel->setText("Signed friend request sent to " + name + "…");
        });
    }

    bool hasPeers = m_peerList->count() > 0;
    if (m_discStack) m_discStack->setCurrentIndex(hasPeers ? 1 : 0);
}

void MainWindow::onAddPeer()
{
    // Adding is now handled by the per-row '+' button in rebuildPeersList().
    // This slot is kept for API compatibility but does nothing.
}

void MainWindow::onAddPeerByIp()
{
    if (!m_ipSearchInput) return;
    QString ip = m_ipSearchInput->text().trimmed();
    if (ip.isEmpty()) return;

    // Validate rough IPv4 format
    QRegularExpression ipRx(R"(^\d{1,3}\.\d{1,3}\.\d{1,3}\.\d{1,3}$)");
    if (!ipRx.match(ip).hasMatch()) {
        m_statusLabel->setText("Invalid IP address.");
        return;
    }

    m_statusLabel->setText(QString("Probing %1…").arg(ip));
    m_ipSearchInput->setEnabled(false);

    // Run the TCP probe off the UI thread
    QString myId   = m_myId;
    QString myName = m_myName;
    (void)QtConcurrent::run([this, ip, myId, myName]() {
        QTcpSocket sock;
        sock.connectToHost(ip, MediaSettings::SignalingPort);
        if (!sock.waitForConnected(3000)) {
            QMetaObject::invokeMethod(this, [this, ip](){
                m_statusLabel->setText(QString("No response from %1").arg(ip));
                if (m_ipSearchInput) m_ipSearchInput->setEnabled(true);
            }, Qt::QueuedConnection);
            return;
        }
        // Send a discovery probe to learn their identity
        SigMsg probe;
        probe.type      = SigType::DiscProbe;
        probe.from_id   = myId.toStdString();
        probe.from_name = myName.toStdString();
        probe.ts        = Helpers::nowMs();
        auto enc = SigMsgEncode(probe);
        sock.write(reinterpret_cast<const char*>(enc.data()), enc.size());
        if (!sock.waitForBytesWritten(2000) || !sock.waitForReadyRead(2000)) {
            QMetaObject::invokeMethod(this, [this, ip](){
                m_statusLabel->setText(QString("No response from %1").arg(ip));
                if (m_ipSearchInput) m_ipSearchInput->setEnabled(true);
            }, Qt::QueuedConnection);
            return;
        }
        QByteArray hdr = sock.read(4);
        if (hdr.size() < 4) return;
        uint32_t len = ((uint8_t)hdr[0]<<24)|((uint8_t)hdr[1]<<16)|
                       ((uint8_t)hdr[2]<<8) | (uint8_t)hdr[3];
        if (len == 0 || len > 8192) return;
        QByteArray body;
        while ((uint32_t)body.size() < len) {
            if (!sock.waitForReadyRead(1000)) break;
            body += sock.read(len - body.size());
        }
        try {
            auto msg = json::parse(body.toStdString()).get<SigMsg>();
            if (msg.from_id.empty()) return;
            QString peerId   = QString::fromStdString(msg.from_id);
            QString peerName = QString::fromStdString(msg.from_name);
            QMetaObject::invokeMethod(this, [this, ip, peerId, peerName](){
                if (m_ipSearchInput) { m_ipSearchInput->clear(); m_ipSearchInput->setEnabled(true); }
                if (m_friendMgr->hasFriend(peerId)) {
                    m_statusLabel->setText(peerName + " is already your friend.");
                    return;
                }
                if (m_sentReqIds.contains(peerId)) {
                    m_statusLabel->setText("Request already sent to " + peerName + ".");
                    return;
                }
                m_sentReqIds.insert(peerId);
                SigMsg req = buildSig(SigType::FriendReq);
                sendDirectSignal(ip, req, true);
                m_statusLabel->setText("Signed friend request sent to " + peerName + " (" + ip + ")");
            }, Qt::QueuedConnection);
        } catch (...) {
            QMetaObject::invokeMethod(this, [this](){
                if (m_ipSearchInput) m_ipSearchInput->setEnabled(true);
                m_statusLabel->setText("Could not identify peer.");
            }, Qt::QueuedConnection);
        }
    });
}

// ══════════════════════════════════════════════════════════════════════════════
//  SIGNAL DISPATCH
// ══════════════════════════════════════════════════════════════════════════════

void MainWindow::onSignalReceived(SigMsg msg, QString ip)
{
    // Dispatch on UI thread
    const std::string& t = msg.type;

    // Never process our own direct signaling packets. On some Windows network
    // adapters the host can receive a looped-back TCP/UDP discovery route or a
    // stale friend entry can point to the local IP; without this guard the caller
    // sees the receiver's Answer/Decline popup for their own outgoing call.
    if (!msg.from_id.empty() && msg.from_id == m_myId.toStdString())
        return;

    // Direct/unicast packets now carry target_id.  This prevents a stale IP,
    // looped-back packet, or copied friend entry from making the sender process
    // its own outgoing call/chat signal.  Group packets without target_id still
    // flow normally.
    const bool directTargetedType =
        t == SigType::ChatText || t == SigType::ChatFile || t == SigType::ChatVoice ||
        t == SigType::CallInv  || t == SigType::CallAcc  || t == SigType::CallRej ||
        t == SigType::CallEnd  || t == SigType::RtcOffer || t == SigType::RtcAnswer ||
        t == SigType::RtcIce   || t == SigType::Typing   ||
        t == SigType::UploadStart || t == SigType::UploadEnd;
    if (directTargetedType && msg.target_id && *msg.target_id != m_myId.toStdString())
        return;

    if ((t == SigType::FriendReq || t == SigType::FriendAcc ||
         t == SigType::CallInv || t == SigType::CallAcc || t == SigType::CallRej || t == SigType::CallEnd ||
         t == SigType::RtcOffer || t == SigType::RtcAnswer || t == SigType::RtcIce) &&
        !verifyCriticalSignal(msg)) {
        showToast("Blocked unauthenticated signal",
                  QString::fromStdString(msg.from_name.empty() ? msg.from_id : msg.from_name));
        return;
    }

    // ── Block guard: silently drop chat and call traffic from users we've
    //    unfriended. Friendship-management signals (FriendReq, FriendDel, etc.)
    //    are still processed so the state machine stays consistent.
    if (m_friendMgr->isBlocked(QString::fromStdString(msg.from_id))) {
        if (t == SigType::ChatText || t == SigType::ChatFile || t == SigType::ChatVoice ||
            t == SigType::CallInv  || t == SigType::GrpText  || t == SigType::GrpFile  ||
            t == SigType::GrpVoice || t == SigType::GrpCallInv)
            return;
    }
    if      (t == SigType::FriendReq)  handleFriendReq(msg, ip);
    else if (t == SigType::FriendAcc)  handleFriendAcc(msg, ip);
    else if (t == SigType::FriendRej)  showToast("Declined", QString::fromStdString(msg.from_name) + " declined your request.");
    else if (t == SigType::FriendDel)  handleFriendDel(QString::fromStdString(msg.from_id));
    else if (t == SigType::ChatText || t == SigType::ChatFile || t == SigType::ChatVoice)
        handleChatMsg(msg, ip);
    else if (t == SigType::CallInv)    handleCallInv(msg, ip);
    else if (t == SigType::CallAcc)    handleCallAcc(msg, ip);
    else if (t == SigType::CallRej)    {
        if (m_callingNotif) { m_callingNotif->close(); m_callingNotif = nullptr; }
        showToast("Declined", QString::fromStdString(msg.from_name) + " declined the call.");
    }
    else if (t == SigType::CallEnd)    {
#ifdef HAS_MEDIA_AUDIO
        if (m_callWin) { m_callWin->doClose(); m_callWin = nullptr; }
#endif
    }
    // Screen share invites — treat as a video call invitation (ScreenInv) or
    // teardown (ScreenEnd). C# version sends these; we accept gracefully.
    else if (t == SigType::ScreenInv)  handleCallInv(msg, ip);
    else if (t == SigType::ScreenEnd)  {
#ifdef HAS_MEDIA_AUDIO
        if (m_callWin) { m_callWin->doClose(); m_callWin = nullptr; }
#endif
    }
    else if (t == SigType::GrpInv)     handleGrpInv(msg, ip);
    else if (t == SigType::GrpLeave)   handleGrpLeave(msg);
    else if (t == SigType::GrpText || t == SigType::GrpFile || t == SigType::GrpVoice)
        handleGroupMsg(msg);
    else if (t == SigType::GrpKick)    handleGrpKick(msg);
    else if (t == SigType::GrpDelete)  handleGrpDelete(msg);
    else if (t == SigType::GrpPromote) handleGrpPromote(msg);
    else if (t == SigType::GrpDemote)  handleGrpDemote(msg);
    else if (t == SigType::GrpPerm)    handleGrpPerm(msg);
    else if (t == SigType::GrpAddMember) handleGrpInv(msg, ip);
    // Group call signaling (C# version sends these)
    else if (t == SigType::GrpCallInv) handleCallInv(msg, ip);
    else if (t == SigType::GrpCallAcc) handleCallAcc(msg, ip);
    else if (t == SigType::GrpCallRej) showToast("Declined", QString::fromStdString(msg.from_name) + " declined the group call.");
    else if (t == SigType::GrpCallEnd) {
#ifdef HAS_MEDIA_AUDIO
        if (m_callWin) { m_callWin->doClose(); m_callWin = nullptr; }
#endif
    }
    else if (t == SigType::RtcOffer || t == SigType::RtcAnswer || t == SigType::RtcIce) {
        handleRtcSignal(msg, ip);
    }
    // ── Typing / uploading indicators ────────────────────────────────────────
    else if (t == SigType::Typing) {
        QString fromId = QString::fromStdString(msg.from_id);
        // Only show if this person's chat is currently open
        if (m_activeFriend && m_activeFriend->id == msg.from_id) {
            QString name = QString::fromStdString(msg.from_name);
            // A ghost row inside the list, so it cannot shift the history.
            m_chatView->setTypingIndicator(true, name);
            m_typingHideTimer->start();  // auto-hide after 4 s
        }
    }
    else if (t == SigType::UploadStart) {
        QString fromId = QString::fromStdString(msg.from_id);
        if (m_activeFriend && m_activeFriend->id == msg.from_id) {
            QString name  = QString::fromStdString(msg.from_name);
            QString fname = QString::fromStdString(msg.file_name.value_or("file"));
            m_chatComposer->setStatusText(name + " is uploading " + fname + "…");
            if (m_uploadHideTimer) m_uploadHideTimer->start();
        }
    }
    else if (t == SigType::UploadEnd) {
        QString fromId = QString::fromStdString(msg.from_id);
        if (m_activeFriend && m_activeFriend->id == msg.from_id) {
            m_chatComposer->setStatusText(QString());
            if (m_uploadHideTimer) m_uploadHideTimer->stop();
        }
    }
}

// ── Friend flow ───────────────────────────────────────────────────────────────

void MainWindow::handleFriendReq(const SigMsg& msg, const QString& ip)
{
    QString fromId = QString::fromStdString(msg.from_id);
    if (m_friendMgr->isBlocked(fromId)) return;

    // Already a friend — they may have missed our FriendAcc; resend silently
    if (m_friendMgr->hasFriend(fromId)) {
        SigMsg acc = buildSig(SigType::FriendAcc);
        sendDirectSignal(ip, acc, true);
        return;
    }

    // Duplicate — already in inbox
    if (m_friendMgr->hasPending(fromId)) return;

    // Store in persistent inbox
    PendingRequest req;
    req.fromId   = msg.from_id;
    req.fromName = msg.from_name;
    req.fromIp   = ip.toStdString();
    if (msg.auth_public_key)  req.authPublicKey = *msg.auth_public_key;
    if (msg.auth_fingerprint) req.authFingerprint = *msg.auth_fingerprint;
    m_friendMgr->addPending(req);
    rebuildRequestsList();
    refreshRequestsBadge();

    // Simple informational toast — user acts via the Requests section in sidebar
    showToast("Friend Request",
              QString::fromStdString(msg.from_name) + " wants to connect — see Requests.");
}

void MainWindow::handleFriendAcc(const SigMsg& msg, const QString& ip)
{
    QString fromId = QString::fromStdString(msg.from_id);
    if (m_friendMgr->hasFriend(fromId)) return;

    m_sentReqIds.remove(fromId);
    FriendInfo f;
    f.id   = msg.from_id;
    f.name = msg.from_name;
    f.ip   = ip.toStdString();
    if (msg.auth_public_key)  f.authPublicKey = *msg.auth_public_key;
    if (msg.auth_fingerprint) f.authFingerprint = *msg.auth_fingerprint;
    commitAddFriend(f);
    showToast("Friend added", QString::fromStdString(msg.from_name) + " accepted your request.");
}

void MainWindow::handleFriendDel(const QString& fromId)
{
    m_friendMgr->removeFriend(fromId);
    rebuildFriendsList();

    if (m_activeFriend && m_activeFriend->id == fromId.toStdString())
        setChatReadOnly(true);

    showToast("Removed", "A friend removed you. Chat history is now read-only.");
}

void MainWindow::commitAddFriend(const FriendInfo& f)
{
    m_friendMgr->addFriend(f);
    rebuildFriendsList();
    refreshRequestsBadge();
}

// ── Chat messages ─────────────────────────────────────────────────────────────

void MainWindow::handleChatMsg(const SigMsg& msg, const QString& ip)
{
    QString fromId = QString::fromStdString(msg.from_id);
    m_friendMgr->updateFriendIp(fromId, ip);
    FriendInfo* f = m_friendMgr->getFriend(fromId);
    if (!f) return;

    // ── Chunked file reassembly ───────────────────────────────────────────────
    if (msg.transfer_id && msg.chunk_index && msg.total_chunks) {
        QString tid = QString::fromStdString(*msg.transfer_id);
        int idx     = *msg.chunk_index;
        int total   = *msg.total_chunks;

        auto& tr = m_pendingTransfers[tid];
        if (tr.totalChunks == 0) {
            tr.totalChunks = total;
            tr.fileSize    = msg.file_size.value_or(0);
            tr.fileName    = QString::fromStdString(msg.file_name.value_or("file"));
            tr.mime        = QString::fromStdString(msg.mime.value_or("application/octet-stream"));
            tr.fromId      = fromId;
            tr.fromName    = QString::fromStdString(msg.from_name);
        }
        if (msg.data) {
            auto decoded = Helpers::base64Decode(*msg.data);
            tr.chunks[idx] = QByteArray(reinterpret_cast<const char*>(decoded.data()),
                                        (int)decoded.size());
        }

        // Show download progress if this conversation is open
        if (m_activeFriend && m_activeFriend->id == msg.from_id) {
            m_chatComposer->setProgress((int)(((int64_t)tr.chunks.size() * 100) / total));
            m_chatComposer->setStatusText("Receiving " + tr.fileName + "…");
        }

        if ((int)tr.chunks.size() < total) return;  // still waiting

        // All chunks arrived — hide progress
        m_chatComposer->setProgress(-1);
        m_chatComposer->setStatusText(QString());

        // All chunks arrived — assemble
        QByteArray full;
        full.reserve((int)tr.fileSize);
        for (int i = 0; i < total; ++i) full.append(tr.chunks.value(i));

        std::vector<uint8_t> data(full.begin(), full.end());
        std::string mime = tr.mime.toStdString();
        const bool isVoice = mime.rfind("audio/", 0) == 0 ||
                             tr.fileName.compare("voice_note.wav", Qt::CaseInsensitive) == 0;
        MessageKind kind = isVoice ? MessageKind::VoiceNote
                                   : (Helpers::isImage(mime) ? MessageKind::Image : MessageKind::File);

        ChatMessage cm;
        cm.kind      = kind;
        cm.fromId    = tr.fromId.toStdString();
        cm.fromName  = tr.fromName.toStdString();
        cm.isMine    = false;
        cm.fileName  = tr.fileName.toStdString();
        cm.mime      = mime;
        cm.data      = data;
        cm.timestamp = msg.ts;

        const QString completedFromName = tr.fromName;
        const QString completedFileName = tr.fileName;
        m_pendingTransfers.remove(tid);

        QString key = buildConvKey(m_myId, fromId);
        m_chatStore->append(key, cm);
        if (m_activeFriend && m_activeFriend->id == msg.from_id) {
            m_chatView->appendMessage(cm);
        } else {
            f->unreadCount++;
            rebuildFriendsList();
            showToast(completedFromName, completedFileName);
        }
        return;
    }

    // ── Single-message (text / voice note) ───────────────────────────────────
    ChatMessage cm = sigToMessage(msg, false);
    QString key = buildConvKey(m_myId, fromId);
    m_chatStore->append(key, cm);

    if (m_activeFriend && m_activeFriend->id == msg.from_id) {
        m_chatView->setTypingIndicator(false);
        m_chatView->appendMessage(cm);
    } else {
        f->unreadCount++;
        rebuildFriendsList();
        showToast(QString::fromStdString(msg.from_name), msg.text.value_or("attachment").c_str());
    }
}

// ── Calls ─────────────────────────────────────────────────────────────────────

/// How long an unanswered call keeps ringing. Both sides use the same value so
/// the caller's "Calling…" panel and the callee's invite expire together.
static constexpr int kRingTimeoutMs = 45 * 1000;

void MainWindow::handleCallInv(const SigMsg& msg, const QString& ip)
{
    if (msg.from_id == m_myId.toStdString()) return;
#ifdef HAS_MEDIA_AUDIO
    // If we are already placing a call, never show an incoming Answer/Decline
    // popup on this side.  This also protects against stale IPs that loop an
    // outgoing invitation back to the caller.
    if (m_callingNotif || !m_pendingCallMode.isEmpty()) {
        SigMsg busy = buildSig(SigType::CallRej);
        busy.target_id = msg.from_id;
        sendDirectSignal(ip, busy, false);
        return;
    }
    if (m_callWin) {
        SigMsg busy = buildSig(SigType::CallRej);
        busy.target_id = msg.from_id;
        sendDirectSignal(ip, busy, false);
        return;
    }
#endif
    m_friendMgr->updateFriendIp(QString::fromStdString(msg.from_id), ip);
    QString mode = QString::fromStdString(msg.mode.value_or("voice"));
    if (msg.type == SigType::ScreenInv) mode = "screen";
    QString name = QString::fromStdString(msg.from_name);

    const bool isGroupCall = (msg.type == SigType::GrpCallInv);
    SigMsg accMsg = buildSig(isGroupCall ? SigType::GrpCallAcc : SigType::CallAcc);
    SigMsg rejMsg = buildSig(isGroupCall ? SigType::GrpCallRej : SigType::CallRej);
    accMsg.group_id = msg.group_id;
    accMsg.group_name = msg.group_name;
    accMsg.target_id = msg.from_id;
    rejMsg.group_id = msg.group_id;
    rejMsg.group_name = msg.group_name;
    rejMsg.target_id = msg.from_id;

    auto* notif = new NotificationWindow(
        "Incoming call",
        QString("%1 is calling (%2).").arg(name, mode),
        {
            {"Answer", [this, ip, name, mode, accMsg]() mutable {
                sendDirectSignal(ip, accMsg, true);
                const CallMode callMode = (mode == "screen") ? CallMode::VideoScreen
                                           : (mode == "video") ? CallMode::VideoCamera
                                                               : CallMode::Voice;
                openCallWindow(ip, name, callMode, false);
            }, NotificationWindow::Button::Accept},
            {"Decline", [this, ip, rejMsg]() mutable {
                sendDirectSignal(ip, rejMsg, false);
            }, NotificationWindow::Button::Reject}
        }
    );
    notif->show();

    // Stop ringing after 45 s and tell the caller, so an unattended machine
    // does not sit with a live invite on screen indefinitely.
    QPointer<NotificationWindow> ringing(notif);
    QTimer::singleShot(kRingTimeoutMs, this, [this, ringing, ip, rejMsg]() mutable {
        if (!ringing) return;
        sendDirectSignal(ip, rejMsg, false);
        ringing->close();
    });
}

void MainWindow::handleCallAcc(const SigMsg& msg, const QString& ip)
{
    if (msg.from_id == m_myId.toStdString()) return;
    if (msg.target_id && *msg.target_id != m_myId.toStdString()) return;
    if (m_pendingCallMode.isEmpty()) return;

    // Dismiss the caller's "Calling…" dialog before opening the call window.
    // Important: use the real source IP from the accepted TCP connection, not
    // an old saved friend IP.  Stale IPs were the main reason media packets went
    // to the wrong machine or looped back to the caller.
    if (m_callingNotif) { m_callingNotif->close(); m_callingNotif = nullptr; }

    FriendInfo* f = m_friendMgr->getFriend(QString::fromStdString(msg.from_id));
    if (!f) return;
    m_friendMgr->updateFriendIp(QString::fromStdString(msg.from_id), ip);
    const CallMode mode = (m_pendingCallMode == "screen") ? CallMode::VideoScreen
                         : (m_pendingCallMode == "video")  ? CallMode::VideoCamera
                                                           : CallMode::Voice;
    m_pendingCallMode.clear();
    openCallWindow(ip, QString::fromStdString(f->name), mode, true);
}

void MainWindow::handleRtcSignal(const SigMsg& msg, const QString& ip)
{
#ifdef HAS_MEDIA_AUDIO
    Q_UNUSED(ip);
    // RTC messages are accepted only after the normal call invitation/acceptance
    // state machine has opened a call window. This prevents stray or looped-back
    // RTC packets from popping up a call on the wrong side.
    if (m_callWin) m_callWin->handleRtcSignal(msg);
#else
    Q_UNUSED(msg); Q_UNUSED(ip);
#endif
}

void MainWindow::openCallWindow(const QString& ip, const QString& name, CallMode mode, bool initiator)
{
#ifdef HAS_MEDIA_AUDIO
    if (m_callWin) return;
    m_callWin = new CallWindow(ip, name, mode, m_myId, m_myName, initiator, this);
    connect(m_callWin, &CallWindow::rtcSignalReady, this, [this, ip](SigMsg msg) {
        sendDirectSignal(ip, msg, true);
    });
    connect(m_callWin, &CallWindow::hangupRequested, this, [this, ip]() {
        FriendInfo* f = nullptr;
        for (auto& fi : m_friendMgr->friends())
            if (fi.ip == ip.toStdString()) { f = &fi; break; }
        if (f) {
            SigMsg sig = buildSig(SigType::CallEnd);
            sig.target_id = f->id;
            sendDirectSignal(ip, sig, true);
        }
        m_callWin = nullptr;
    });
    connect(m_callWin, &QDialog::destroyed, this, [this](){ m_callWin = nullptr; });
    m_callWin->show();
#else
    QMessageBox::information(this, "Not available",
        "Voice/video calls require Qt Multimedia and/or OpenCV.\n"
        "See README.md for build instructions.");
#endif
}

void MainWindow::sendCallInvite(FriendInfo* f, const QString& mode)
{
#ifdef HAS_MEDIA_AUDIO
    if (m_callWin || m_callingNotif) {
        showToast("Call already active", "Finish or cancel the current call first.");
        return;
    }
#endif
    if (f->id == m_myId.toStdString()) {
        showToast("Invalid call", "This contact points to your own LocalCall identity.");
        return;
    }
    const QString peerIpForGuard = QString::fromStdString(f->ip);
    if (peerIpForGuard == QLatin1String("127.0.0.1") || peerIpForGuard == QLatin1String("localhost")) {
        showToast("Invalid call", "This contact points to the local computer, not the other PC.");
        return;
    }
    for (const auto& local : Helpers::localIPv4Addresses(false)) {
        if (peerIpForGuard == QString::fromStdString(local)) {
            showToast("Invalid call", "This contact IP is one of your own PC's IP addresses. Rescan or re-add the other PC.");
            return;
        }
    }
    if (!f->isOnline) {
        QMessageBox::information(this, "Offline",
            QString::fromStdString(f->name) + " is offline.");
        return;
    }
    m_pendingCallMode = mode;
    SigMsg sig = buildSig(SigType::CallInv);
    sig.mode = mode.toStdString();
    sig.target_id = f->id;

    QString peerIp   = QString::fromStdString(f->ip);
    QString peerName = QString::fromStdString(f->name);

    sendDirectSignal(peerIp, sig, true);

    // Show a persistent "Calling…" panel for the CALLER only — NOT the
    // Answer/Decline notification that belongs on the receiver's side.
    // The dialog is dismissed automatically when the peer answers, declines,
    // or when the caller clicks Cancel.
    SigMsg cancelMsg = buildSig(SigType::CallRej);   // reuse CallRej as a cancel signal
    cancelMsg.target_id = f->id;
    if (m_callingNotif) m_callingNotif->close();     // guard: don't stack multiples

    m_callingNotif = new NotificationWindow(
        mode == "screen" ? "Screen share…" : (mode == "video" ? "Video call…" : "Voice call…"),
        QString("Calling %1…\nWaiting for them to answer.").arg(peerName),
        {
            {"Cancel", [this, peerIp, cancelMsg]() mutable {
                sendDirectSignal(peerIp, cancelMsg, false);
                m_pendingCallMode.clear();
                m_callingNotif = nullptr;
            }, NotificationWindow::Button::Reject}
        },
        this
    );
    connect(m_callingNotif, &QObject::destroyed, this, [this]() {
        m_callingNotif = nullptr;
    });
    m_callingNotif->show();

    // Give up on the same 45 s deadline the callee uses, so the two sides can
    // never disagree about whether the call is still ringing.
    QPointer<NotificationWindow> calling(m_callingNotif);
    QTimer::singleShot(kRingTimeoutMs, this, [this, calling, peerIp, peerName, cancelMsg]() mutable {
        if (!calling) return;
        sendDirectSignal(peerIp, cancelMsg, false);
        m_pendingCallMode.clear();
        calling->close();
        showToast("No answer", QString("%1 did not pick up.").arg(peerName));
    });
}

// ── Groups ─────────────────────────────────────────────────────────────────────

void MainWindow::handleGrpInv(const SigMsg& msg, const QString& ip)
{
    if (!msg.group_id || !msg.group_name) return;
    QString grpName = QString::fromStdString(*msg.group_name);

    SigMsg accMsg = buildSig(SigType::GrpAcc);
    accMsg.group_id   = msg.group_id;
    accMsg.group_name = msg.group_name;

    auto* notif = new NotificationWindow(
        "Group Invite",
        QString("%1 invited you to \"%2\".").arg(QString::fromStdString(msg.from_name), grpName),
        {
            {"Join", [this, msg, ip, accMsg]() mutable {
                GroupInfo g;
                g.groupId = *msg.group_id;
                g.name    = *msg.group_name;
                g.ownerId = msg.owner_id.value_or(msg.from_id);
                if (msg.members) {
                    for (const auto& m : *msg.members) {
                        g.memberIds.push_back(m.id);
                        FriendInfo* f = m_friendMgr->getFriend(QString::fromStdString(m.id));
                        if (f) g.members.push_back(f);
                    }
                }
                m_friendMgr->addGroup(g);
                rebuildGroupsList();
                SignalingClient::send(ip, accMsg);
            }},
            {"Decline", nullptr}
        }
    );
    notif->show();
}

void MainWindow::handleGrpLeave(const SigMsg& msg)
{
    if (!msg.group_id) return;
    GroupInfo* g = m_friendMgr->getGroup(QString::fromStdString(*msg.group_id));
    if (!g) return;
    std::string fromId = msg.from_id;
    g->memberIds.erase(std::remove(g->memberIds.begin(), g->memberIds.end(), fromId), g->memberIds.end());
    g->members.erase(std::remove_if(g->members.begin(), g->members.end(),
        [&](FriendInfo* f){ return f->id == fromId; }), g->members.end());
    if (m_activeGroup && m_activeGroup->groupId == *msg.group_id) {
        m_grpMemberList->clear();
        for (auto* m : g->members)
            m_grpMemberList->addItem(QString::fromStdString(m->name));
    }
}

void MainWindow::handleGroupMsg(const SigMsg& msg)
{
    if (!msg.group_id) return;
    GroupInfo* grp = m_friendMgr->getGroup(QString::fromStdString(*msg.group_id));
    if (grp) {
        auto perms = grp->getPermissions(msg.from_id);
        if (msg.type == SigType::GrpFile && !perms.canSendFiles)    return;
        if (msg.type == SigType::GrpText && !perms.canSendMessages) return;
    }

    // ── Chunked file reassembly (group) ───────────────────────────────────────
    if (msg.transfer_id && msg.chunk_index && msg.total_chunks) {
        QString tid   = QString::fromStdString(*msg.transfer_id);
        int     idx   = *msg.chunk_index;
        int     total = *msg.total_chunks;

        auto& tr = m_pendingTransfers[tid];
        if (tr.totalChunks == 0) {
            tr.totalChunks = total;
            tr.fileSize    = msg.file_size.value_or(0);
            tr.fileName    = QString::fromStdString(msg.file_name.value_or("file"));
            tr.mime        = QString::fromStdString(msg.mime.value_or("application/octet-stream"));
            tr.fromId      = QString::fromStdString(msg.from_id);
            tr.fromName    = QString::fromStdString(msg.from_name);
            tr.isGroup     = true;
            tr.groupId     = QString::fromStdString(*msg.group_id);
        }
        if (msg.data) {
            auto decoded = Helpers::base64Decode(*msg.data);
            tr.chunks[idx] = QByteArray(reinterpret_cast<const char*>(decoded.data()),
                                        (int)decoded.size());
        }
        if ((int)tr.chunks.size() < total) return;

        // Assemble
        QByteArray full;
        full.reserve((int)tr.fileSize);
        for (int i = 0; i < total; ++i) full.append(tr.chunks.value(i));

        std::vector<uint8_t> data(full.begin(), full.end());
        std::string mime = tr.mime.toStdString();
        const bool isVoice = mime.rfind("audio/", 0) == 0 ||
                             tr.fileName.compare("voice_note.wav", Qt::CaseInsensitive) == 0;
        MessageKind kind = isVoice ? MessageKind::VoiceNote
                                   : (Helpers::isImage(mime) ? MessageKind::Image : MessageKind::File);

        ChatMessage cm;
        cm.kind      = kind;
        cm.fromId    = tr.fromId.toStdString();
        cm.fromName  = tr.fromName.toStdString();
        cm.isMine    = false;
        cm.fileName  = tr.fileName.toStdString();
        cm.mime      = mime;
        cm.data      = data;
        cm.timestamp = msg.ts;

        const QString completedGroupId = tr.groupId;
        const QString completedFromName = tr.fromName;
        const QString completedFileName = tr.fileName;
        m_pendingTransfers.remove(tid);

        QString key = "grp-" + completedGroupId;
        m_chatStore->append(key, cm);
        if (m_activeGroup && m_activeGroup->groupId == completedGroupId.toStdString()) {
            m_groupView->appendMessage(cm);
        } else {
            showToast(QString("[Group] ") + completedFromName, completedFileName);
        }
        return;
    }

    // ── Single message (text / voice note) ───────────────────────────────────
    ChatMessage cm = sigToMessage(msg, false);
    QString key = "grp-" + QString::fromStdString(*msg.group_id);
    m_chatStore->append(key, cm);

    if (m_activeGroup && m_activeGroup->groupId == *msg.group_id) {
        m_groupView->appendMessage(cm);
    } else {
        showToast(QString("[Group] ") + QString::fromStdString(msg.from_name),
                  msg.text.value_or("attachment").c_str());
    }
}

void MainWindow::handleGrpKick(const SigMsg& msg)
{
    if (!msg.group_id) return;
    if (msg.target_id && *msg.target_id == m_myId.toStdString()) {
        GroupInfo* g = m_friendMgr->getGroup(QString::fromStdString(*msg.group_id));
        QString gname = g ? QString::fromStdString(g->name) : "group";
        m_friendMgr->removeGroup(QString::fromStdString(*msg.group_id));
        rebuildGroupsList();
        if (m_activeGroup && m_activeGroup->groupId == *msg.group_id) showDiscover();
        showToast("Removed from group", QString("You were removed from \"%1\".").arg(gname));
    } else {
        GroupInfo* g = m_friendMgr->getGroup(QString::fromStdString(*msg.group_id));
        if (!g || !msg.target_id) return;
        std::string tid = *msg.target_id;
        g->memberIds.erase(std::remove(g->memberIds.begin(), g->memberIds.end(), tid), g->memberIds.end());
        g->members.erase(std::remove_if(g->members.begin(), g->members.end(),
            [&](FriendInfo* f){ return f->id == tid; }), g->members.end());
        if (m_activeGroup && m_activeGroup->groupId == *msg.group_id) {
            m_grpMemberList->clear();
            for (auto* m : g->members)
                m_grpMemberList->addItem("● " + QString::fromStdString(m->name));
        }
    }
}

void MainWindow::handleGrpDelete(const SigMsg& msg)
{
    if (!msg.group_id) return;
    GroupInfo* g = m_friendMgr->getGroup(QString::fromStdString(*msg.group_id));
    QString gname = g ? QString::fromStdString(g->name) : "group";
    m_friendMgr->removeGroup(QString::fromStdString(*msg.group_id));
    rebuildGroupsList();
    if (m_activeGroup && m_activeGroup->groupId == *msg.group_id) showDiscover();
    showToast("Group deleted", QString("The group \"%1\" was deleted by the owner.").arg(gname));
}

void MainWindow::handleGrpPromote(const SigMsg& msg)
{
    if (!msg.group_id || !msg.target_id) return;
    GroupInfo* g = m_friendMgr->getGroup(QString::fromStdString(*msg.group_id));
    if (!g) return;
    if (std::find(g->helperIds.begin(), g->helperIds.end(), *msg.target_id) == g->helperIds.end())
        g->helperIds.push_back(*msg.target_id);
    m_friendMgr->saveGroups();
    if (*msg.target_id == m_myId.toStdString())
        showToast("Promoted!", QString("You are now a helper in \"%1\".").arg(QString::fromStdString(g->name)));
}

void MainWindow::handleGrpDemote(const SigMsg& msg)
{
    if (!msg.group_id || !msg.target_id) return;
    GroupInfo* g = m_friendMgr->getGroup(QString::fromStdString(*msg.group_id));
    if (!g) return;
    g->helperIds.erase(std::remove(g->helperIds.begin(), g->helperIds.end(), *msg.target_id), g->helperIds.end());
    m_friendMgr->saveGroups();
}

void MainWindow::handleGrpPerm(const SigMsg& msg)
{
    if (!msg.group_id || !msg.target_id) return;
    GroupInfo* g = m_friendMgr->getGroup(QString::fromStdString(*msg.group_id));
    if (!g) return;
    auto& p = g->permissions[*msg.target_id];
    if (msg.perm_msg)  p.canSendMessages = *msg.perm_msg;
    if (msg.perm_file) p.canSendFiles    = *msg.perm_file;
    if (msg.perm_call) p.canStartCalls   = *msg.perm_call;
    m_friendMgr->saveGroups();
}

// ══════════════════════════════════════════════════════════════════════════════
//  CHAT SEND
// ══════════════════════════════════════════════════════════════════════════════

void MainWindow::sendChatText(const QString& text, int64_t replyToTs,
                              const QString& replyName, const QString& replySnippet)
{
    if (text.isEmpty() || !m_activeFriend) return;
    if (!m_friendMgr->hasFriend(QString::fromStdString(m_activeFriend->id))) return;

    // Stop typing debounce timer so we don't send a spurious typing signal after send
    if (m_typingDebounce) m_typingDebounce->stop();

    SigMsg sig = buildSig(SigType::ChatText);
    sig.text = text.toStdString();
    sig.target_id = m_activeFriend->id;
    if (replyToTs != 0) {
        sig.reply_to_ts   = replyToTs;
        sig.reply_name    = replyName.toStdString();
        sig.reply_snippet = replySnippet.toStdString();
    }
    SignalingClient::send(QString::fromStdString(m_activeFriend->ip), sig);

    ChatMessage cm;
    cm.kind         = MessageKind::Text;
    cm.fromId       = m_myId.toStdString();
    cm.fromName     = m_myName.toStdString();
    cm.text         = text.toStdString();
    cm.isMine       = true;
    cm.timestamp    = Helpers::nowMs();
    cm.replyToTs    = replyToTs;
    cm.replyName    = replyName.toStdString();
    cm.replySnippet = replySnippet.toStdString();
    m_chatStore->append(m_chatConvKey, cm);
    m_chatView->appendMessage(cm);
}

void MainWindow::onChatSendFile()  { sendFile(false, false); }
void MainWindow::onChatSendImage() { sendFile(false, true);  }

void MainWindow::onVoiceNotePress()
{
#ifdef HAS_MULTIMEDIA
    if (!m_activeFriend || !m_friendMgr->hasFriend(QString::fromStdString(m_activeFriend->id))) return;
    if (m_vnRec && m_vnRec->start()) return;
    showToast("Microphone unavailable", "Could not start recording a voice note.");
#endif
}

void MainWindow::onVoiceNoteRelease()
{
#ifdef HAS_MULTIMEDIA
    if (!m_vnRec || !m_activeFriend || !m_vnRec->isRecording()) return;
    QByteArray wav = m_vnRec->stop();
    if (wav.size() < 100) {
        showToast("Voice note", "Hold the button a little longer to record.");
        return;
    }

    std::vector<uint8_t> wavVec(wav.begin(), wav.end());

    // Voice notes must arrive in order and without flooding the signaling
    // listener.  Older builds launched one background TCP task per chunk, which
    // could overload the receiver and make the note appear to do nothing.  Build
    // signed chunks on the UI thread, then send them sequentially on one worker.
    const QString peerIp = QString::fromStdString(m_activeFriend->ip);
    const QString transferId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const int chunkSize = 8 * 1024;
    const int totalChunks = (wav.size() + chunkSize - 1) / chunkSize;
    QVector<SigMsg> chunks;
    chunks.reserve(totalChunks);
    for (int i = 0; i < totalChunks; ++i) {
        const int offset = i * chunkSize;
        const int size = qMin(chunkSize, wav.size() - offset);
        QByteArray chunk = wav.mid(offset, size);
        std::vector<uint8_t> chunkVec(chunk.begin(), chunk.end());

        SigMsg sig = buildSig(SigType::ChatVoice);
        sig.file_name = "voice_note.wav";
        sig.mime = "audio/wav";
        sig.target_id = m_activeFriend->id;
        sig.data = Helpers::base64Encode(chunkVec);
        sig.transfer_id = transferId.toStdString();
        sig.chunk_index = i;
        sig.total_chunks = totalChunks;
        sig.file_size = static_cast<int64_t>(wav.size());
        signSignal(sig);
        chunks.append(sig);
    }
    (void)QtConcurrent::run([peerIp, chunks]() {
        for (const SigMsg& sig : chunks) {
            (void)SignalingClient::sendReliableBlocking(peerIp, sig, 4, 120);
            QThread::msleep(15);
        }
    });

    ChatMessage cm;
    cm.kind     = MessageKind::VoiceNote;
    cm.fromId   = m_myId.toStdString();
    cm.fromName = m_myName.toStdString();
    cm.isMine   = true;
    cm.fileName = "voice_note.wav";
    cm.data     = wavVec;
    cm.timestamp = Helpers::nowMs();
    m_chatStore->append(m_chatConvKey, cm);
    m_chatView->appendMessage(cm);
#endif // HAS_MULTIMEDIA
}

/// Stop the recorder and throw the audio away. VoiceNoteRecorder has no cancel
/// of its own — stop() is the only way to release the microphone.
void MainWindow::onVoiceNoteCancel()
{
#ifdef HAS_MULTIMEDIA
    if (m_vnRec && m_vnRec->isRecording()) (void)m_vnRec->stop();
#endif
}

void MainWindow::onChatVoiceCall() { if (m_activeFriend) sendCallInvite(m_activeFriend, "voice"); }
void MainWindow::onChatVideoCall() { if (m_activeFriend) sendCallInvite(m_activeFriend, "video"); }

// ══════════════════════════════════════════════════════════════════════════════
//  GROUP SEND
// ══════════════════════════════════════════════════════════════════════════════

void MainWindow::sendGroupText(const QString& text, int64_t replyToTs,
                               const QString& replyName, const QString& replySnippet)
{
    if (text.isEmpty() || !m_activeGroup) return;

    auto myPerms = m_activeGroup->getPermissions(m_myId.toStdString());
    if (!myPerms.canSendMessages &&
        !m_activeGroup->isOwner(m_myId.toStdString()) &&
        !m_activeGroup->isHelper(m_myId.toStdString()))
    { showToast("Restricted", "You cannot send messages in this group."); return; }

    SigMsg sig = buildSig(SigType::GrpText);
    sig.text     = text.toStdString();
    sig.group_id = m_activeGroup->groupId;
    if (replyToTs != 0) {
        sig.reply_to_ts   = replyToTs;
        sig.reply_name    = replyName.toStdString();
        sig.reply_snippet = replySnippet.toStdString();
    }
    broadcastToGroup(m_activeGroup, sig);

    ChatMessage cm;
    cm.kind         = MessageKind::Text;
    cm.fromId       = m_myId.toStdString();
    cm.fromName     = m_myName.toStdString();
    cm.text         = text.toStdString();
    cm.isMine       = true;
    cm.timestamp    = Helpers::nowMs();
    cm.replyToTs    = replyToTs;
    cm.replyName    = replyName.toStdString();
    cm.replySnippet = replySnippet.toStdString();
    m_chatStore->append(m_groupConvKey, cm);
    m_groupView->appendMessage(cm);
}

void MainWindow::onGroupSendFile()  { sendFile(true, false); }
void MainWindow::onGroupSendImage() { sendFile(true, true);  }

void MainWindow::onGroupVoiceNotePress()
{
#ifdef HAS_MULTIMEDIA
    if (!m_activeGroup) return;
    if (m_vnRecGroup && m_vnRecGroup->start()) return;
    showToast("Microphone unavailable", "Could not start recording a voice note.");
#endif
}

void MainWindow::onGroupVoiceNoteRelease()
{
#ifdef HAS_MULTIMEDIA
    if (!m_vnRecGroup || !m_activeGroup || !m_vnRecGroup->isRecording()) return;
    QByteArray wav = m_vnRecGroup->stop();
    if (wav.size() < 100) {
        showToast("Voice note", "Hold the button a little longer to record.");
        return;
    }

    std::vector<uint8_t> wavVec(wav.begin(), wav.end());

    // Group voice notes are also sent sequentially to avoid flooding every
    // recipient with concurrent TCP connections.
    const QString transferId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const int chunkSize = 8 * 1024;
    const int totalChunks = (wav.size() + chunkSize - 1) / chunkSize;
    QVector<SigMsg> chunks;
    chunks.reserve(totalChunks);
    for (int i = 0; i < totalChunks; ++i) {
        const int offset = i * chunkSize;
        const int size = qMin(chunkSize, wav.size() - offset);
        QByteArray chunk = wav.mid(offset, size);
        std::vector<uint8_t> chunkVec(chunk.begin(), chunk.end());

        SigMsg sig = buildSig(SigType::GrpVoice);
        sig.file_name = "voice_note.wav";
        sig.mime      = "audio/wav";
        sig.data      = Helpers::base64Encode(chunkVec);
        sig.group_id  = m_activeGroup->groupId;
        sig.transfer_id = transferId.toStdString();
        sig.chunk_index = i;
        sig.total_chunks = totalChunks;
        sig.file_size = static_cast<int64_t>(wav.size());
        signSignal(sig);
        chunks.append(sig);
    }
    QStringList targets;
    for (const auto& mid : m_activeGroup->memberIds) {
        FriendInfo* f = m_friendMgr->getFriend(QString::fromStdString(mid));
        if (f && !f->ip.empty()) targets << QString::fromStdString(f->ip);
    }
    (void)QtConcurrent::run([targets, chunks]() {
        for (const QString& ip : targets) {
            for (const SigMsg& sig : chunks) {
                (void)SignalingClient::sendReliableBlocking(ip, sig, 4, 120);
                QThread::msleep(15);
            }
        }
    });

    ChatMessage cm;
    cm.kind     = MessageKind::VoiceNote;
    cm.fromId   = m_myId.toStdString();
    cm.fromName = m_myName.toStdString();
    cm.isMine   = true;
    cm.fileName = "voice_note.wav";
    cm.data     = wavVec;
    cm.timestamp = Helpers::nowMs();
    m_chatStore->append(m_groupConvKey, cm);
    m_groupView->appendMessage(cm);
#endif // HAS_MULTIMEDIA
}

/// Same as onVoiceNoteCancel, for the group recorder.
void MainWindow::onGroupVoiceNoteCancel()
{
#ifdef HAS_MULTIMEDIA
    if (m_vnRecGroup && m_vnRecGroup->isRecording()) (void)m_vnRecGroup->stop();
#endif
}

void MainWindow::onGroupVoiceCall()
{
    if (!m_activeGroup) return;
    for (const auto& mid : m_activeGroup->memberIds) {
        FriendInfo* f = m_friendMgr->getFriend(QString::fromStdString(mid));
        if (f && f->isOnline) {
            SigMsg sig = buildSig(SigType::GrpCallInv);
            sig.group_id   = m_activeGroup->groupId;
            sig.group_name = m_activeGroup->name;
            sig.mode       = "voice";
            SignalingClient::send(QString::fromStdString(f->ip), sig);
        }
    }
    showToast("Group call started", "Invites sent to all online members.");
}

// ══════════════════════════════════════════════════════════════════════════════
//  SEND FILE
// ══════════════════════════════════════════════════════════════════════════════

void MainWindow::sendFile(bool isGroup, bool imagesOnly)
{
    // ── Choose file or folder ─────────────────────────────────────────────────
    QString path;
    bool isFolder = false;
    if (!imagesOnly) {
        // Ask user: file or folder?
        QMessageBox picker(this);
        picker.setWindowTitle("Send");
        picker.setText("What would you like to send?");
        auto* btnFile   = picker.addButton("File",   QMessageBox::AcceptRole);
        auto* btnFolder = picker.addButton("Folder", QMessageBox::AcceptRole);
        picker.addButton("Cancel", QMessageBox::RejectRole);
        picker.exec();
        if (picker.clickedButton() == btnFile) {
            path = QFileDialog::getOpenFileName(this, "Select file");
        } else if (picker.clickedButton() == btnFolder) {
            path     = QFileDialog::getExistingDirectory(this, "Select folder");
            isFolder = true;
        } else {
            return;
        }
    } else {
        QString filter = "Images & Videos (*.png *.jpg *.jpeg *.gif *.bmp *.webp *.mp4 *.mkv *.mov);;All Files (*)";
        path = QFileDialog::getOpenFileName(this, "Select image or video", {}, filter);
    }
    if (path.isEmpty()) return;

    // ── Zip folder into a temp file ───────────────────────────────────────────
    QString sendPath = path;
    QString sendName;
    QSharedPointer<QTemporaryDir> tmpDir; // keeps temp dir alive until send completes

    if (isFolder) {
        QFileInfo fi(path);
        sendName = fi.fileName() + ".zip";
        tmpDir = QSharedPointer<QTemporaryDir>::create();
        if (!tmpDir->isValid()) { QMessageBox::warning(this, "Error", "Could not create temp dir."); return; }
        QString zipPath = tmpDir->path() + "/" + sendName;
        // Use Qt's built-in zip via QProcess (cross-platform: 7z on Windows, zip on Unix)
        // Simpler approach: stream files manually using Qt's QZipWriter (available in Qt 6)
        // We use QProcess with platform zip commands as fallback
        bool zipped = false;
#ifdef Q_OS_WIN
        QProcess p;
        p.setWorkingDirectory(QFileInfo(path).absolutePath());
        p.start("powershell", {"-Command",
            QString("Compress-Archive -Path \"%1\" -DestinationPath \"%2\" -Force").arg(path, zipPath)});
        zipped = p.waitForFinished(60000) && p.exitCode() == 0;
#else
        QProcess p;
        p.setWorkingDirectory(QFileInfo(path).absolutePath());
        p.start("zip", {"-r", zipPath, fi.fileName()});
        zipped = p.waitForFinished(60000) && p.exitCode() == 0;
#endif
        if (!zipped) { QMessageBox::warning(this, "Error", "Could not compress folder."); return; }
        sendPath = zipPath;
    }

    QFileInfo info(sendPath);
    sendName = isFolder ? sendName : info.fileName();

    // ── Read file ─────────────────────────────────────────────────────────────
    QFile f(sendPath);
    if (!f.open(QIODevice::ReadOnly)) { QMessageBox::warning(this, "Error", "Could not read file."); return; }
    QByteArray qdata = f.readAll();
    f.close();

    if (qdata.isEmpty()) { QMessageBox::warning(this, "Error", "File is empty."); return; }

    std::string mime = isFolder ? "application/zip"
                                : Helpers::guessMime(("." + info.suffix()).toStdString());
    MessageKind kind = (!isFolder && Helpers::isImage(mime)) ? MessageKind::Image : MessageKind::File;

    // ── Build local chat message ───────────────────────────────────────────────
    std::vector<uint8_t> data(qdata.begin(), qdata.end());
    ChatMessage cm;
    cm.kind      = kind;
    cm.fromId    = m_myId.toStdString();
    cm.fromName  = m_myName.toStdString();
    cm.isMine    = true;
    cm.fileName  = sendName.toStdString();
    cm.mime      = mime;
    cm.data      = data;
    cm.timestamp = Helpers::nowMs();

    // ── Chunk and send ────────────────────────────────────────────────────────
    // Each chunk is a separate SigMsg with transfer_id, chunk_index, total_chunks.
    // This allows arbitrarily large files with reliable TCP delivery.
    QString transferId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    int chunkSize  = MediaSettings::ChunkSize;
    int totalChunks = (int)(((int64_t)qdata.size() + chunkSize - 1) / chunkSize);

    auto sendChunked = [&](std::function<void(const SigMsg&)> sendFn) {
        for (int i = 0; i < totalChunks; ++i) {
            int offset = i * chunkSize;
            int sz     = std::min(chunkSize, (int)qdata.size() - offset);
            std::vector<uint8_t> chunkData(qdata.begin() + offset, qdata.begin() + offset + sz);

            SigMsg sig;
            sig.type         = isFolder || kind == MessageKind::File
                               ? (cm.kind==MessageKind::Image ? SigType::ChatFile : SigType::ChatFile)
                               : SigType::ChatFile;
            sig.from_id      = m_myId.toStdString();
            sig.from_name    = m_myName.toStdString();
            sig.ts           = cm.timestamp;
            sig.file_name    = cm.fileName;
            sig.mime         = mime;
            sig.data         = Helpers::base64Encode(chunkData);
            sig.transfer_id  = transferId.toStdString();
            sig.chunk_index  = i;
            sig.total_chunks = totalChunks;
            sig.file_size    = (int64_t)qdata.size();
            sendFn(sig);
        }
    };

    if (isGroup && m_activeGroup) {
        // Send UploadStart so group members see "uploading" indicator
        SigMsg startSig = buildSig(SigType::UploadStart);
        startSig.file_name = cm.fileName;
        startSig.group_id  = m_activeGroup->groupId;
        broadcastToGroup(m_activeGroup, startSig);

        // Local upload progress — a hairline on the composer's top edge.
        m_groupComposer->setProgress(0);
        m_groupComposer->setStatusText("Sending " + QString::fromStdString(cm.fileName) + "…");

        sendChunked([&](SigMsg sig){
            sig.type     = SigType::GrpFile;
            sig.group_id = m_activeGroup->groupId;
            broadcastToGroup(m_activeGroup, sig);
            if (totalChunks > 0) {
                int pct = (int)(((sig.chunk_index.value_or(0) + 1) * 100LL) / totalChunks);
                m_groupComposer->setProgress(pct);
                QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
            }
        });

        m_groupComposer->setProgress(-1);
        m_groupComposer->setStatusText(QString());

        // Send UploadEnd
        SigMsg endSig = buildSig(SigType::UploadEnd);
        endSig.group_id = m_activeGroup->groupId;
        broadcastToGroup(m_activeGroup, endSig);

        m_chatStore->append(m_groupConvKey, cm);
        m_groupView->appendMessage(cm);
    } else if (!isGroup && m_activeFriend) {
        QString peerIp = QString::fromStdString(m_activeFriend->ip);

        // Send UploadStart so receiver sees "uploading" indicator
        SigMsg startSig = buildSig(SigType::UploadStart);
        startSig.file_name = cm.fileName;
        startSig.target_id = m_activeFriend->id;
        sendDirectSignal(peerIp, startSig, false);

        // Local upload progress — a hairline on the composer's top edge.
        m_chatComposer->setProgress(0);
        m_chatComposer->setStatusText("Sending " + QString::fromStdString(cm.fileName) + "…");

        sendChunked([&](SigMsg sig){
            sig.type = SigType::ChatFile;
            sig.target_id = m_activeFriend->id;
            sendDirectSignal(peerIp, sig, true);
            if (totalChunks > 0) {
                int pct = (int)(((sig.chunk_index.value_or(0) + 1) * 100LL) / totalChunks);
                m_chatComposer->setProgress(pct);
                QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
            }
        });

        m_chatComposer->setProgress(-1);
        m_chatComposer->setStatusText(QString());

        // Send UploadEnd
        SigMsg endSig = buildSig(SigType::UploadEnd);
        endSig.target_id = m_activeFriend->id;
        sendDirectSignal(peerIp, endSig, false);

        m_chatStore->append(m_chatConvKey, cm);
        m_chatView->appendMessage(cm);
    }
}

// ══════════════════════════════════════════════════════════════════════════════
//  FRIEND REQUESTS (inline in sidebar)
// ══════════════════════════════════════════════════════════════════════════════

void MainWindow::onAcceptRequest()
{
    auto* item = m_requestsList->currentItem();
    if (!item) return;
    QString fromId   = item->data(Qt::UserRole).toString();
    QString fromName = item->data(Qt::UserRole + 1).toString();
    QString fromIp   = item->data(Qt::UserRole + 2).toString();

    FriendInfo f;
    f.id   = fromId.toStdString();
    f.name = fromName.toStdString();
    f.ip   = fromIp.toStdString();
    for (const auto& req : m_friendMgr->pending()) {
        if (req.fromId == fromId.toStdString()) {
            f.authPublicKey = req.authPublicKey;
            f.authFingerprint = req.authFingerprint;
            break;
        }
    }
    m_friendMgr->removePending(fromId);
    commitAddFriend(f);

    SigMsg sig = buildSig(SigType::FriendAcc);
    sendDirectSignal(fromIp, sig, true);
    rebuildRequestsList();
    refreshRequestsBadge();
}

void MainWindow::onDeclineRequest()
{
    auto* item = m_requestsList->currentItem();
    if (!item) return;
    QString fromId = item->data(Qt::UserRole).toString();
    QString fromIp = item->data(Qt::UserRole + 2).toString();

    m_friendMgr->removePending(fromId);
    m_friendMgr->block(fromId);

    SigMsg sig = buildSig(SigType::FriendRej);
    sendDirectSignal(fromIp, sig, false);
    rebuildRequestsList();
    refreshRequestsBadge();
}

// ══════════════════════════════════════════════════════════════════════════════
//  CONTEXT MENUS
// ══════════════════════════════════════════════════════════════════════════════

void MainWindow::onCtxRemoveFriend()
{
    auto* item = m_friendsList->currentItem();
    if (!item) return;
    QString id = item->data(Qt::UserRole).toString();
    FriendInfo* f = m_friendMgr->getFriend(id);
    if (!f) return;

    if (QMessageBox::question(this, "Remove Friend",
        QString("Remove %1?\n\nYou'll keep your chat history (read-only).").arg(QString::fromStdString(f->name)),
        QMessageBox::Yes | QMessageBox::No) != QMessageBox::Yes) return;

    if (!f->ip.empty()) {
        SigMsg sig = buildSig(SigType::FriendDel);
        SignalingClient::sendReliable(QString::fromStdString(f->ip), sig);  // reliable: peer must know
    }
    m_friendMgr->removeFriend(id);
    rebuildFriendsList();

    if (m_activeFriend && m_activeFriend->id == id.toStdString())
        setChatReadOnly(true);
}

void MainWindow::onCtxDeleteFormerFriend()
{
    auto* item = m_friendsList->currentItem();
    if (!item) return;
    QString id = item->data(Qt::UserRole).toString();

    // Only applies to former friends (not in active friends list)
    if (m_friendMgr->getFriend(id)) return;

    bool isFormer = std::any_of(m_friendMgr->formerFriends().begin(),
                                m_friendMgr->formerFriends().end(),
                                [&](const FriendInfo& x){ return x.id == id.toStdString(); });
    if (!isFormer) return;

    QString name;
    for (auto& f : m_friendMgr->formerFriends())
        if (f.id == id.toStdString()) { name = QString::fromStdString(f.name); break; }

    if (QMessageBox::question(this, "Delete from list",
        QString("Permanently remove %1 from your contacts?\n\n"
                "The conversation history will also be deleted.").arg(name),
        QMessageBox::Yes | QMessageBox::No) != QMessageBox::Yes) return;

    // Delete conversation history
    QString key = buildConvKey(m_myId, id);
    m_chatStore->deleteConversation(key);

    // Remove from former friends (and unblock so they can send a new friend request later)
    m_friendMgr->removeFormerFriend(id);
    rebuildFriendsList();

    // Close chat panel if it was showing this former friend
    if (m_activeFriend && m_activeFriend->id == id.toStdString()) {
        m_activeFriend = nullptr;
        showDiscover();
    }
}

void MainWindow::onCtxVoiceCall()
{
    auto* item = m_friendsList->currentItem();
    if (!item) return;
    FriendInfo* f = m_friendMgr->getFriend(item->data(Qt::UserRole).toString());
    if (f) sendCallInvite(f, "voice");
}

void MainWindow::onCtxVideoCall()
{
    auto* item = m_friendsList->currentItem();
    if (!item) return;
    FriendInfo* f = m_friendMgr->getFriend(item->data(Qt::UserRole).toString());
    if (f) sendCallInvite(f, "video");
}

void MainWindow::onCtxLeaveGroup()
{
    auto* item = m_groupsList->currentItem();
    if (!item) return;
    GroupInfo* g = m_friendMgr->getGroup(item->data(Qt::UserRole).toString());
    if (!g) return;

    for (const auto& mid : g->memberIds) {
        FriendInfo* f = m_friendMgr->getFriend(QString::fromStdString(mid));
        if (f) {
            SigMsg sig = buildSig(SigType::GrpLeave);
            sig.group_id = g->groupId;
            SignalingClient::send(QString::fromStdString(f->ip), sig);
        }
    }
    m_friendMgr->removeGroup(QString::fromStdString(g->groupId));
    rebuildGroupsList();
    if (m_activeGroup && m_activeGroup->groupId == g->groupId) showDiscover();
}

void MainWindow::onCtxManageGroup()
{
    auto* item = m_groupsList->currentItem();
    if (!item) return;
    GroupInfo* g = m_friendMgr->getGroup(item->data(Qt::UserRole).toString());
    if (!g) return;
    syncGroupMembers(g);

    GroupManageDialog dlg(g, m_myId, m_friendMgr->friends(), this);
    dlg.exec();

    for (auto& act : dlg.pendingActions) act();

    if (dlg.wasDeleted) {
        m_friendMgr->removeGroup(QString::fromStdString(g->groupId));
        rebuildGroupsList();
        if (m_activeGroup && m_activeGroup->groupId == g->groupId) showDiscover();
        return;
    }
    m_friendMgr->saveGroups();
}

void MainWindow::onCtxDeleteConversation()
{
    // Works for both friend list and group list — figure out which is active
    QString convKey;
    QString label;

    auto* fItem = m_friendsList->currentItem();
    auto* gItem = m_groupsList->currentItem();

    if (fItem) {
        QString id = fItem->data(Qt::UserRole).toString();
        FriendInfo* f = m_friendMgr->getFriend(id);
        if (!f) return;
        // Build the same sorted key as showChat
        QStringList ids = { m_myId, QString::fromStdString(f->id) };
        ids.sort();
        convKey = ids.join("-");
        label   = QString::fromStdString(f->name);
    } else if (gItem) {
        GroupInfo* g = m_friendMgr->getGroup(gItem->data(Qt::UserRole).toString());
        if (!g) return;
        convKey = "grp-" + QString::fromStdString(g->groupId);
        label   = QString::fromStdString(g->name);
    } else {
        return;
    }

    if (QMessageBox::question(this, "Delete Conversation",
            QString("Delete all messages with %1?\nThis cannot be undone.").arg(label),
            QMessageBox::Yes | QMessageBox::No) != QMessageBox::Yes) return;

    m_chatStore->deleteConversation(convKey);

    // Clear the live view if this conversation is open
    if (convKey == m_chatConvKey) {
        m_chatView->clearMessages();
    } else if (convKey == m_groupConvKey) {
        m_groupView->clearMessages();
    }
}

void MainWindow::onCtxDeleteMessages()
{
    // Whichever panel is showing owns the selection.
    ChatView* view = nullptr;
    QString   convKey;

    if (m_panelChat->isVisible() && m_activeFriend) {
        view    = m_chatView;
        convKey = m_chatConvKey;
    } else if (m_panelGroup->isVisible() && m_activeGroup) {
        view    = m_groupView;
        convKey = m_groupConvKey;
    }
    if (!view || convKey.isEmpty()) return;

    const QList<int64_t> toDelete = view->selectedTimestamps();
    if (toDelete.isEmpty()) return;

    m_chatStore->deleteMessages(convKey, toDelete);

    // Replay the conversation rather than plucking items out of the list:
    // grouping, day separators and run shapes all depend on what's adjacent,
    // so a hole in the middle would leave the survivors mis-shaped.
    view->clearMessages();
    const auto history = m_chatStore->load(convKey);
    for (const auto& msg : history) view->appendMessage(msg);
    view->scrollToLatest();
}

// ══════════════════════════════════════════════════════════════════════════════
//  NEW GROUP
// ══════════════════════════════════════════════════════════════════════════════

void MainWindow::onNewGroup()
{
    QList<FriendInfo*> online;
    for (auto& f : m_friendMgr->friends())
        if (f.isOnline) online.append(&f);

    if (online.isEmpty()) {
        QMessageBox::information(this, "No online friends",
            "You need at least one online friend to create a group.");
        return;
    }

    GroupCreateDialog dlg(online, this);
    if (dlg.exec() != QDialog::Accepted) return;

    GroupInfo g;
    g.name    = dlg.groupName().toStdString();
    g.ownerId = m_myId.toStdString();
    for (auto* f : dlg.selected()) {
        g.memberIds.push_back(f->id);
        g.members.push_back(f);
    }
    m_friendMgr->addGroup(g);
    rebuildGroupsList();

    std::vector<MemberDto> dtos;
    for (auto* f : dlg.selected())
        dtos.push_back({f->id, f->name, f->ip});

    for (auto* f : dlg.selected()) {
        SigMsg sig = buildSig(SigType::GrpInv);
        sig.group_id   = g.groupId;
        sig.group_name = g.name;
        sig.owner_id   = g.ownerId;
        sig.members    = dtos;
        SignalingClient::send(QString::fromStdString(f->ip), sig);
    }
}

// ══════════════════════════════════════════════════════════════════════════════
//  PROFILE
// ══════════════════════════════════════════════════════════════════════════════

void MainWindow::onEditProfile()
{
    InputDialog dlg("Edit Profile", "Display name:", m_myName, this);
    if (dlg.exec() != QDialog::Accepted || dlg.result().trimmed().isEmpty()) return;
    m_myName = dlg.result().trimmed();
    QSettings("LocalCall", "LocalCall").setValue("identity/name", m_myName);
    m_discovery->updateName(m_myName);
    m_sigServer->setIdentity(m_myId, m_myName);
    m_lblMyName->setText(m_myName + "  ·  " + m_localIp);
}


// ══════════════════════════════════════════════════════════════════════════════
//  PING MONITOR
// ══════════════════════════════════════════════════════════════════════════════

void MainWindow::startPingMonitor()
{
    if (m_pingTimer) return;
    m_pingTimer = new QTimer(this);
    m_pingTimer->setInterval(3000);
    connect(m_pingTimer, &QTimer::timeout, this, &MainWindow::refreshPingMetrics);
    m_pingTimer->start();
    QTimer::singleShot(250, this, &MainWindow::refreshPingMetrics);
}

QString MainWindow::friendPingText(const QString& friendId) const
{
    if (!m_friendPingMs.contains(friendId)) return QStringLiteral("Ping --");
    const int ms = m_friendPingMs.value(friendId, -1);
    return ms >= 0 ? QString("%1 ms").arg(ms) : QStringLiteral("Timeout");
}

void MainWindow::refreshActiveChatPing()
{
    // The chat header no longer displays ping. Keep this method as a no-op so
    // existing call sites stay harmless while sidebar/call ping remain active.
}

void MainWindow::setPingForFriend(const QString& friendId, int pingMs)
{
    const int old = m_friendPingMs.value(friendId, -9999);
    if (old == pingMs) {
        refreshActiveChatPing();
        return;
    }
    m_friendPingMs[friendId] = pingMs;
    rebuildFriendsList();
    refreshActiveChatPing();
}

void MainWindow::refreshPingMetrics()
{
    struct Target { QString id; QString ip; bool online; };
    QList<Target> targets;
    for (const auto& f : m_friendMgr->friends()) {
        const QString id = QString::fromStdString(f.id);
        if (!f.isOnline || f.ip.empty()) {
            setPingForFriend(id, -1);
            continue;
        }
        targets.push_back({id, QString::fromStdString(f.ip), f.isOnline});
    }

    QPointer<MainWindow> self(this);
    for (const Target& target : targets) {
        QtConcurrent::run([self, id = target.id, ip = target.ip]() {
            QElapsedTimer timer;
            QTcpSocket socket;
            timer.start();
            socket.connectToHost(ip, MediaSettings::SignalingPort);
            const bool ok = socket.waitForConnected(900);
            const int ms = ok ? static_cast<int>(timer.elapsed()) : -1;
            socket.abort();
            if (!self) return;
            QMetaObject::invokeMethod(self, [self, id, ms]() {
                if (self) self->setPingForFriend(id, ms);
            }, Qt::QueuedConnection);
        });
    }
}

namespace {
QString sidebarInitials(const QString& name)
{
    const QString trimmed = name.trimmed();
    if (trimmed.isEmpty()) return "?";

    QStringList parts = trimmed.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
    QString initials;
    for (const QString& part : parts) {
        if (!part.isEmpty()) initials += part.left(1).toUpper();
        if (initials.size() >= 2) break;
    }
    return initials.isEmpty() ? trimmed.left(1).toUpper() : initials;
}

QWidget* makeConversationListRow(const QString& title,
                                 const QString& subtitle,
                                 const QString& avatarText,
                                 bool online,
                                 int unread,
                                 bool locked,
                                 QWidget* parent,
                                 const QString& pingText = QString(),
                                 const QString& avatarIcon = QString())
{
    auto* row = new QWidget(parent);
    row->setAttribute(Qt::WA_StyledBackground, true);
    row->setObjectName("listRow");

    auto* layout = new QHBoxLayout(row);
    layout->setContentsMargins(10, 7, 10, 7);
    layout->setSpacing(10);

    auto* avatar = new QLabel(avatarText, row);
    avatar->setFixedSize(38, 38);
    avatar->setAlignment(Qt::AlignCenter);
    avatar->setObjectName("rowAvatar");
    UiTheme::setClass(avatar, locked ? "locked" : (online ? "on" : "off"));
    if (!avatarIcon.isEmpty()) {
        avatar->setText(QString());
        avatar->setPixmap(UiTheme::icon(avatarIcon).pixmap(22, 22));
    }
    layout->addWidget(avatar);

    auto* textCol = new QVBoxLayout();
    textCol->setContentsMargins(0, 0, 0, 0);
    textCol->setSpacing(2);

    auto* lblTitle = new QLabel(title, row);
    lblTitle->setObjectName("rowTitle");
    UiTheme::setClass(lblTitle, locked ? "locked" : "normal");
    lblTitle->setTextFormat(Qt::PlainText);
    lblTitle->setWordWrap(false);

    auto* lblSub = new QLabel(subtitle, row);
    lblSub->setObjectName("rowSubtitle");
    UiTheme::setClass(lblSub, online ? "on" : "off");
    lblSub->setTextFormat(Qt::PlainText);
    lblSub->setWordWrap(false);

    textCol->addWidget(lblTitle);
    textCol->addWidget(lblSub);
    layout->addLayout(textCol, 1);

    if (!pingText.isEmpty()) {
        auto* ping = new QLabel(pingText, row);
        ping->setAlignment(Qt::AlignCenter);
        ping->setObjectName("rowPing");
        UiTheme::setClass(ping, online ? "on" : "off");
        layout->addWidget(ping);
    }

    if (unread > 0) {
        auto* badge = new QLabel(QString::number(unread), row);
        badge->setAlignment(Qt::AlignCenter);
        badge->setMinimumWidth(22);
        badge->setFixedHeight(22);
        badge->setObjectName("rowBadge");
        layout->addWidget(badge);
    }

    return row;
}
}

// ══════════════════════════════════════════════════════════════════════════════
//  REBUILD HELPERS
// ══════════════════════════════════════════════════════════════════════════════

void MainWindow::rebuildFriendsList()
{
    m_friendsList->clear();

    // Active friends
    for (auto& f : m_friendMgr->friends()) {
        const QString id = QString::fromStdString(f.id);
        const QString name = QString::fromStdString(f.name);
        const QString pingText = friendPingText(id);
        const QString subtitle = f.isOnline
            ? QString("Online · %1").arg(QString::fromStdString(f.ip))
            : QString("Offline");

        auto* item = new QListWidgetItem(m_friendsList);
        item->setData(Qt::UserRole, id);
        item->setSizeHint(QSize(0, 56));

        m_friendsList->setItemWidget(
            item,
            makeConversationListRow(name, subtitle, sidebarInitials(name),
                                    f.isOnline, f.unreadCount, false, m_friendsList, pingText));
    }

    // Former friends stay visible as locked read-only conversations.
    for (auto& f : m_friendMgr->formerFriends()) {
        const QString id = QString::fromStdString(f.id);
        const QString name = QString::fromStdString(f.name);

        auto* item = new QListWidgetItem(m_friendsList);
        item->setData(Qt::UserRole, id);
        item->setSizeHint(QSize(0, 56));

        m_friendsList->setItemWidget(
            item,
            makeConversationListRow(name, "Removed contact", "",
                                    false, 0, true, m_friendsList, QString(), "lock"));
    }
}

void MainWindow::rebuildGroupsList()
{
    m_groupsList->clear();
    for (auto& g : m_friendMgr->groups()) {
        const QString id = QString::fromStdString(g.groupId);
        const QString name = QString::fromStdString(g.name);
        const int members = static_cast<int>(g.memberIds.size());
        const QString subtitle = QString("%1 member%2").arg(members).arg(members == 1 ? "" : "s");

        auto* item = new QListWidgetItem(m_groupsList);
        item->setData(Qt::UserRole, id);
        item->setSizeHint(QSize(0, 56));

        m_groupsList->setItemWidget(
            item,
            makeConversationListRow(name, subtitle, "",
                                    false, 0, false, m_groupsList, QString(), "group"));
    }
}

void MainWindow::rebuildRequestsList()
{
    m_requestsList->clear();
    for (const auto& req : m_friendMgr->pending()) {
        auto* item = new QListWidgetItem(m_requestsList);

        auto* w  = new QWidget();
        auto* wl = new QVBoxLayout(w);
        wl->setContentsMargins(10, 6, 10, 6);
        wl->setSpacing(2);

        // Name
        auto* lblName = new QLabel(QString::fromStdString(req.fromName), w);
        lblName->setObjectName("reqName");

        // IP (subtle, like C#)
        auto* lblIp = new QLabel(QString::fromStdString(req.fromIp), w);
        lblIp->setObjectName("reqIp");

        wl->addWidget(lblName);
        wl->addWidget(lblIp);

        // Accept / Decline buttons
        auto* btnRow = new QWidget(w);
        auto* btnLayout = new QHBoxLayout(btnRow);
        btnLayout->setContentsMargins(0, 4, 0, 0);
        btnLayout->setSpacing(6);

        auto* accept  = new QPushButton("Accept", btnRow);
        auto* decline = new QPushButton("Decline", btnRow);
        accept->setIcon(UiTheme::icon("check"));
        accept->setIconSize(QSize(14,14));
        decline->setIcon(UiTheme::icon("close"));
        decline->setIconSize(QSize(14,14));
        accept->setFixedHeight(24);
        decline->setFixedHeight(24);
        accept->setObjectName("reqAccept");
        decline->setObjectName("reqDecline");

        btnLayout->addWidget(accept);
        btnLayout->addWidget(decline);
        btnLayout->addStretch();
        wl->addWidget(btnRow);

        item->setSizeHint(QSize(0, 72));
        m_requestsList->setItemWidget(item, w);

        PendingRequest reqCopy = req;
        connect(accept, &QPushButton::clicked, this, [this, reqCopy]() {
            m_friendMgr->removePending(QString::fromStdString(reqCopy.fromId));
            FriendInfo f;
            f.id = reqCopy.fromId; f.name = reqCopy.fromName; f.ip = reqCopy.fromIp;
            f.authPublicKey = reqCopy.authPublicKey;
            f.authFingerprint = reqCopy.authFingerprint;
            commitAddFriend(f);
            SigMsg sig = buildSig(SigType::FriendAcc);
            sendDirectSignal(QString::fromStdString(reqCopy.fromIp), sig, true);
            rebuildRequestsList();
            refreshRequestsBadge();
        });
        connect(decline, &QPushButton::clicked, this, [this, reqCopy]() {
            m_friendMgr->removePending(QString::fromStdString(reqCopy.fromId));
            m_friendMgr->block(QString::fromStdString(reqCopy.fromId));
            SigMsg sig = buildSig(SigType::FriendRej);
            sendDirectSignal(QString::fromStdString(reqCopy.fromIp), sig, false);
            rebuildRequestsList();
            refreshRequestsBadge();
        });
    }
}

void MainWindow::refreshRequestsBadge()
{
    int count = m_friendMgr->pending().size();
    // Mirrors C# RefreshRequestsBadge exactly:
    // show/hide the whole section; update the red count label
    m_requestsSection->setVisible(count > 0);
    m_lblRequestCount->setText(QString::number(count));
    // Size the list to fit all items (no scrollbar needed)
    m_requestsList->setFixedHeight(std::max(1, count) * 72 + 4);
}

void MainWindow::syncGroupMembers(GroupInfo* single)
{
    auto process = [&](GroupInfo& g) {
        g.members.clear();
        for (const auto& id : g.memberIds) {
            FriendInfo* f = m_friendMgr->getFriend(QString::fromStdString(id));
            if (f) g.members.push_back(f);
        }
    };
    if (single) { process(*single); }
    else         { for (auto& g : m_friendMgr->groups()) process(g); }
}

void MainWindow::broadcastToGroup(GroupInfo* g, const SigMsg& sig)
{
    const bool reliable = sig.transfer_id.has_value();
    for (const auto& mid : g->memberIds) {
        FriendInfo* f = m_friendMgr->getFriend(QString::fromStdString(mid));
        if (f && !f->ip.empty())
            sendDirectSignal(QString::fromStdString(f->ip), sig, reliable);
    }
}

void MainWindow::showToast(const QString& title, const QString& body)
{
    auto* notif = new NotificationWindow(title, body, 4, this);
    notif->show();
}

ChatMessage MainWindow::sigToMessage(const SigMsg& sig, bool isMine) const
{
    ChatMessage cm;
    if (sig.type == SigType::ChatVoice || sig.type == SigType::GrpVoice)
        cm.kind = MessageKind::VoiceNote;
    else if (sig.type == SigType::ChatFile || sig.type == SigType::GrpFile)
        cm.kind = (sig.mime && Helpers::isImage(*sig.mime)) ? MessageKind::Image : MessageKind::File;
    else
        cm.kind = MessageKind::Text;

    cm.fromId   = sig.from_id;
    cm.fromName = sig.from_name;
    cm.text     = sig.text.value_or("");
    cm.fileName = sig.file_name.value_or(
        (cm.kind == MessageKind::VoiceNote) ? "voice_note.wav" : "");
    cm.mime     = sig.mime.value_or(
        (cm.kind == MessageKind::VoiceNote) ? "audio/wav" : "");
    cm.isMine   = isMine;
    cm.timestamp = sig.ts;
    cm.replyToTs    = sig.reply_to_ts.value_or(0);
    cm.replyName    = sig.reply_name.value_or("");
    cm.replySnippet = sig.reply_snippet.value_or("");
    if (sig.data) cm.data = Helpers::base64Decode(*sig.data);
    return cm;
}

SigMsg MainWindow::buildSig(const std::string& type) const
{
    SigMsg sig;
    sig.protocol  = LocalCallProtocol::Name;
    sig.schema    = LocalCallProtocol::Schema;
#ifdef LOCALCALL_VERSION
    sig.app_version = LOCALCALL_VERSION;
#endif
#if defined(Q_OS_WIN)
    sig.platform = "windows";
#elif defined(Q_OS_MACOS)
    sig.platform = "macos";
#elif defined(Q_OS_LINUX)
    sig.platform = "linux";
#elif defined(Q_OS_ANDROID)
    sig.platform = "android";
#else
    sig.platform = "unknown";
#endif
    if (m_security) {
        sig.auth_alg = "ed25519";
        sig.auth_public_key = m_security->publicKeyBase64().toStdString();
        sig.auth_fingerprint = m_security->fingerprint().toStdString();
    }
    sig.type      = type;
    sig.from_id   = m_myId.toStdString();
    sig.from_name = m_myName.toStdString();
    sig.ts        = Helpers::nowMs();
    return sig;
}


// ── Secure signaling helpers ─────────────────────────────────────────────────

bool MainWindow::signSignal(SigMsg& msg) const
{
    return m_security && m_security->signMessage(msg);
}

void MainWindow::sendDirectSignal(const QString& ip, SigMsg msg, bool reliable) const
{
    if (m_security) m_security->signMessage(msg);
    if (reliable) SignalingClient::sendReliable(ip, msg);
    else          SignalingClient::send(ip, msg);
}

bool MainWindow::verifyCriticalSignal(const SigMsg& msg) const
{
    if (!m_security) return false;

    QString expectedPublicKey;
    if (m_friendMgr) {
        if (auto* f = m_friendMgr->getFriend(QString::fromStdString(msg.from_id)))
            expectedPublicKey = QString::fromStdString(f->authPublicKey);
        if (expectedPublicKey.isEmpty()) {
            for (const auto& req : m_friendMgr->pending()) {
                if (req.fromId == msg.from_id) {
                    expectedPublicKey = QString::fromStdString(req.authPublicKey);
                    break;
                }
            }
        }
    }

    const bool ok = m_security->verifyMessage(msg, expectedPublicKey);
    if (!ok) return false;

    if (!msg.auth_fingerprint || !msg.auth_public_key) return false;
    const QString fp = SecurityManager::fingerprintForPublicKey(
        QString::fromStdString(*msg.auth_public_key));
    return fp == QString::fromStdString(*msg.auth_fingerprint);
}

// Helper: canonical conversation key (sorted peer IDs joined by -)
// forward decl
static QString buildConvKey(const QString& a, const QString& b);// defined at bottom
static QString buildConvKey(const QString& a, const QString& b)
{
    return (a < b) ? a + "-" + b : b + "-" + a;
}
