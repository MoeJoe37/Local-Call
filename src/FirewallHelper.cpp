#include "FirewallHelper.h"
#include "MediaSettings.h"
#include <QString>
#include <QProcess>
#include <QCoreApplication>

#ifdef Q_OS_WIN
#include <windows.h>
#include <shellapi.h>
#endif

namespace FirewallHelper {

// ── Windows implementation ────────────────────────────────────────────────────
#ifdef Q_OS_WIN

static const QString APP_NAME = "Local Call";

static void netsh(const QString& args)
{
    QProcess p;
    p.setProgram("netsh");
    p.setArguments(args.split(' '));
    p.start();
    p.waitForFinished(5000);
}

static void rm(const QString& name)
{
    netsh(QString("advfirewall firewall delete rule name=\"%1\"").arg(name));
}

static void add(const QString& name, const QString& proto,
                const QString& dir, const QString& port)
{
    QString field = (dir == "in") ? "localport" : "remoteport";
    netsh(QString("advfirewall firewall add rule name=\"%1\" dir=%2 action=allow protocol=%3 %4=%5")
          .arg(name, dir, proto, field, port));
}

static bool rulesExist()
{
    QProcess p;
    p.start("netsh", {"advfirewall", "firewall", "show", "rule",
                      QString("name=\"%1 Audio In\"").arg(APP_NAME)});
    p.waitForFinished(3000);
    return p.readAllStandardOutput().contains(APP_NAME.toUtf8());
}

static bool isElevated()
{
    BOOL elevated = FALSE;
    HANDLE token = nullptr;
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
        TOKEN_ELEVATION elev{};
        DWORD size;
        if (GetTokenInformation(token, TokenElevation, &elev, sizeof(elev), &size))
            elevated = elev.TokenIsElevated;
        CloseHandle(token);
    }
    return elevated;
}

static void addRules()
{
    QString bp = QString::number(MediaSettings::BroadcastPort);
    QString sp = QString::number(MediaSettings::SignalingPort);
    QString ap = QString::number(MediaSettings::MediaAudioPort);
    QString vp = QString::number(MediaSettings::MediaVideoPort);

    rm(APP_NAME + " UDP");      add(APP_NAME + " UDP",      "UDP", "in",  bp);
    rm(APP_NAME + " UDP Out");  add(APP_NAME + " UDP Out",  "UDP", "out", bp);
    rm(APP_NAME + " TCP");      add(APP_NAME + " TCP",      "TCP", "in",  sp);
    rm(APP_NAME + " TCP Out");  add(APP_NAME + " TCP Out",  "TCP", "out", sp);
    rm(APP_NAME + " Audio In"); add(APP_NAME + " Audio In", "UDP", "in",  ap);
    rm(APP_NAME + " Audio Out");add(APP_NAME + " Audio Out","UDP", "out", ap);
    rm(APP_NAME + " Video In"); add(APP_NAME + " Video In", "UDP", "in",  vp);
    rm(APP_NAME + " Video Out");add(APP_NAME + " Video Out","UDP", "out", vp);
}

static void elevateAndAdd()
{
    QString exe = QCoreApplication::applicationFilePath();
    SHELLEXECUTEINFOW sei{};
    sei.cbSize       = sizeof(sei);
    sei.lpVerb       = L"runas";
    sei.lpFile       = reinterpret_cast<LPCWSTR>(exe.utf16());
    sei.lpParameters = L"/firewall";
    sei.nShow        = SW_HIDE;
    sei.fMask        = SEE_MASK_NOCLOSEPROCESS;
    if (ShellExecuteExW(&sei) && sei.hProcess) {
        WaitForSingleObject(sei.hProcess, 15000);
        CloseHandle(sei.hProcess);
    } else {
        addRules();
    }
}

void ensureRules()
{
    if (rulesExist()) return;
    if (isElevated()) addRules();
    else              elevateAndAdd();
}

void runFirewallSetup()
{
    addRules();
}

#else // ── Non-Windows: no firewall management needed ──────────────────────────
// Linux, macOS, Android use their own permission models; the OS-level firewall
// (iptables/nftables/pf/Android iptables) is managed by the user or the system.
// Qt's QUdpSocket and QTcpServer will bind correctly without any extra setup.

void ensureRules()    { /* no-op on non-Windows */ }
void runFirewallSetup(){ /* no-op on non-Windows */ }

#endif

} // namespace FirewallHelper
