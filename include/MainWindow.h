#pragma once
#include <QMainWindow>
#include <QListWidget>
#include <QStackedWidget>
#include <QList>
#include <QMap>
#include <QSet>
#include <QString>
#include <QPointer>
#include <QEvent>
#include <QMap>
#include "CallTypes.h"
#include "FriendInfo.h"
#include "GroupInfo.h"
#include "ChatMessage.h"
#include "PeerInfo.h"
#include "PeerDiscovery.h"
#include "SignalingServer.h"
#include "FriendManager.h"
class NotificationWindow;
#include "ChatStore.h"
#include "SecurityManager.h"
#ifdef HAS_MULTIMEDIA
#include "VoiceNoteRecorder.h"
#endif
#ifdef HAS_MEDIA_AUDIO
#include "CallWindow.h"
#endif

class QLabel;
class QLineEdit;
class QPushButton;
class QListWidget;
class QListWidgetItem;
class QScrollArea;
class QWidget;
class QProgressBar;
class QTimer;
class ChatView;
class ChatComposer;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow();

protected:
    void closeEvent(QCloseEvent*) override;

private slots:
    // Sidebar
    void onFriendClicked(QListWidgetItem*);
    void onGroupClicked(QListWidgetItem*);
    void onDiscoverClicked();

    // Discovery
    void onPeersUpdated(QMap<QString, PeerInfo> peers);
    void onDiagLog(const QString& msg);
    void onRefresh();
    void onAddPeer();
    void onAddPeerByIp();

    // Signal dispatch
    void onSignalReceived(SigMsg msg, QString ip);

    // Chat send
    void onChatSendFile();
    void onChatSendImage();
    void onVoiceNotePress();
    void onVoiceNoteRelease();
    void onVoiceNoteCancel();
    void onChatVoiceCall();
    void onChatVideoCall();

    // Group send
    void onGroupSendFile();
    void onGroupSendImage();
    void onGroupVoiceNotePress();
    void onGroupVoiceNoteRelease();
    void onGroupVoiceNoteCancel();
    void onGroupVoiceCall();

    // Friend requests
    void onAcceptRequest();
    void onDeclineRequest();

    // Context menus — friends
    void onCtxRemoveFriend();
    void onCtxDeleteFormerFriend();
    void onCtxVoiceCall();
    void onCtxVideoCall();
    void onCtxDeleteConversation();

    // Context menus — groups
    void onCtxLeaveGroup();
    void onCtxManageGroup();

    // Message context menu
    void onCtxDeleteMessages();

    // Groups
    void onNewGroup();

    // Profile
    void onEditProfile();

private:
    // Panel switching
    void showDiscover();
    void showChat(FriendInfo* f);
    void showGroupChat(GroupInfo* g);
    void setChatReadOnly(bool ro);

    // Signal handlers
    void handleFriendReq(const SigMsg& msg, const QString& ip);
    void handleFriendAcc(const SigMsg& msg, const QString& ip);
    void handleFriendDel(const QString& fromId);
    void handleChatMsg(const SigMsg& msg, const QString& ip);
    void handleCallInv(const SigMsg& msg, const QString& ip);
    void handleCallAcc(const SigMsg& msg, const QString& ip);
    void handleRtcSignal(const SigMsg& msg, const QString& ip);
    void handleGrpInv(const SigMsg& msg, const QString& ip);
    void handleGrpLeave(const SigMsg& msg);
    void handleGroupMsg(const SigMsg& msg);
    void handleGrpKick(const SigMsg& msg);
    void handleGrpDelete(const SigMsg& msg);
    void handleGrpPromote(const SigMsg& msg);
    void handleGrpDemote(const SigMsg& msg);
    void handleGrpPerm(const SigMsg& msg);

    // Helpers
    void sendCallInvite(FriendInfo* f, const QString& mode);
    void openCallWindow(const QString& ip, const QString& name, CallMode mode, bool initiator = false);
    bool verifyCriticalSignal(const SigMsg& msg) const;
    bool signSignal(SigMsg& msg) const;
    void sendDirectSignal(const QString& ip, SigMsg msg, bool reliable = false) const;
    void sendFile(bool isGroup, bool imagesOnly);
    void broadcastToGroup(GroupInfo* g, const SigMsg& sig);
    void commitAddFriend(const FriendInfo& f);
    void updateFriendOnlineStatus(const QMap<QString, PeerInfo>& peers);
    void refreshRequestsBadge();
    void syncGroupMembers(GroupInfo* single = nullptr);
    void rebuildFriendsList();
    void rebuildGroupsList();
    void rebuildPeersList();
    void rebuildRequestsList();
    /// Sends the composer's current text to the active 1:1 / group conversation.
    void sendChatText(const QString& text, int64_t replyToTs,
                      const QString& replyName, const QString& replySnippet);
    void sendGroupText(const QString& text, int64_t replyToTs,
                       const QString& replyName, const QString& replySnippet);
    void showToast(const QString& title, const QString& body);
    ChatMessage sigToMessage(const SigMsg& sig, bool isMine) const;
    SigMsg buildSig(const std::string& type) const;
    void applyDarkTitleBar();
    void startPingMonitor();
    void refreshPingMetrics();
    void setPingForFriend(const QString& friendId, int pingMs);
    QString friendPingText(const QString& friendId) const;
    void refreshActiveChatPing();

    // ── Refresh animation ─────────────────────────────────────────────────────
    void startRefreshAnimation();
    void stopRefreshAnimation();
    QPushButton* m_btnRefresh      = nullptr;
    QTimer*      m_refreshAniTimer = nullptr;
    int          m_refreshAniStep  = 0;

    // ── Identity ──────────────────────────────────────────────────────────────
    QString m_myId;
    QString m_myName;
    QString m_localIp;

    // ── Services ──────────────────────────────────────────────────────────────
    PeerDiscovery*   m_discovery  = nullptr;
    SignalingServer* m_sigServer  = nullptr;
    FriendManager*   m_friendMgr  = nullptr;
    ChatStore*       m_chatStore  = nullptr;
    SecurityManager* m_security   = nullptr;

    // ── Upload/download progress tracking ────────────────────────────────────
    struct OutgoingTransfer {
        int     totalChunks = 0;
        int     sentChunks  = 0;
        QString fileName;
    };
    QMap<QString, OutgoingTransfer> m_outgoingTransfers; // transferId -> state

    // ── Typing / upload indicator timers ─────────────────────────────────────
    QTimer*   m_typingDebounce   = nullptr;  // fires when user stops typing
    QTimer*   m_typingHideTimer  = nullptr;  // hides remote "is typing" label
    QTimer*   m_uploadHideTimer  = nullptr;  // hides remote "is uploading" label

    // ── State ─────────────────────────────────────────────────────────────────
    FriendInfo* m_activeFriend = nullptr;
    GroupInfo*  m_activeGroup  = nullptr;
    QString     m_chatConvKey;
    QString     m_groupConvKey;
    QString     m_pendingCallMode;
    QPointer<NotificationWindow> m_callingNotif;  // persistent "Calling…" dialog for the caller
    QSet<QString> m_sentReqIds;
    QMap<QString, PeerInfo> m_peers;
    QMap<QString, int> m_friendPingMs;
    QTimer* m_pingTimer = nullptr;

    // Chunked file transfer reassembly
    struct IncomingTransfer {
        QMap<int,QByteArray> chunks;
        int totalChunks  = 0;
        int64_t fileSize = 0;
        QString fileName;
        QString mime;
        QString fromId;
        QString fromName;
        bool    isGroup  = false;
        QString groupId;
    };
    QMap<QString, IncomingTransfer> m_pendingTransfers;

    // Voice note recorders
#ifdef HAS_MULTIMEDIA
    VoiceNoteRecorder* m_vnRec      = nullptr;
    VoiceNoteRecorder* m_vnRecGroup = nullptr;
#else
    void* m_vnRec      = nullptr;
    void* m_vnRecGroup = nullptr;
#endif

    // Call window
#ifdef HAS_MEDIA_AUDIO
    CallWindow* m_callWin = nullptr;
#else
    void* m_callWin = nullptr;
#endif

    // ── UI elements ───────────────────────────────────────────────────────────
    // Sidebar
    QLabel*      m_lblMyName       = nullptr;
    QListWidget* m_friendsList     = nullptr;
    QListWidget* m_groupsList      = nullptr;
    QWidget*     m_requestsSection = nullptr;   // inline section, hidden when empty
    QLabel*      m_lblRequestCount = nullptr;
    QListWidget* m_requestsList    = nullptr;
    QLabel*      m_statusLabel     = nullptr;

    // Panels
    QStackedWidget* m_panels          = nullptr;
    QWidget*        m_panelDiscover   = nullptr;
    QStackedWidget* m_discStack       = nullptr;
    QListWidget*    m_peerList        = nullptr;
    QWidget*        m_emptyState      = nullptr;
    QLineEdit*      m_ipSearchInput   = nullptr;

    QWidget*     m_panelChat          = nullptr;
    QLabel*      m_chatName           = nullptr;
    QLabel*      m_chatStatusDot      = nullptr;
    QLabel*      m_chatPingLabel      = nullptr;
    QWidget*     m_chatReadOnlyBanner = nullptr;
    ChatView*     m_chatView          = nullptr;
    ChatComposer* m_chatComposer      = nullptr;
    QPushButton* m_btnChatVoice       = nullptr;
    QPushButton* m_btnChatVideo       = nullptr;
    QPushButton* m_btnChatScreen      = nullptr;

    QWidget*      m_panelGroup        = nullptr;
    QLabel*       m_groupName         = nullptr;
    ChatView*     m_groupView         = nullptr;
    ChatComposer* m_groupComposer     = nullptr;
    QListWidget*  m_grpMemberList     = nullptr;
};
