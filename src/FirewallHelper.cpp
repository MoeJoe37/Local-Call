#include "FirewallHelper.h"
#include "MediaSettings.h"
#include <QString>
#include <QProcess>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>

#ifdef Q_OS_WIN
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#  include <shellapi.h>

namespace FirewallHelper {

static QString appName() { return QStringLiteral("Local Call"); }

static QString quoted(QString value)
{
    value.replace(QChar('\"'), QStringLiteral("\\\""));
    return QStringLiteral("\"") + value + QStringLiteral("\"");
}

static void netsh(const QString& args)
{
    // Run through cmd.exe so netsh receives quoted rule names and quoted program
    // paths intact. The old args.split(' ') broke names like "Local Call Audio In"
    // and paths under "Program Files", so firewall rules were often not created.
    QProcess p;
    p.setProgram(QStringLiteral("cmd"));
    p.setArguments({QStringLiteral("/c"), QStringLiteral("netsh ") + args});
    p.start();
    p.waitForFinished(10000);
}

static void rm(const QString& name)
{
    netsh(QStringLiteral("advfirewall firewall delete rule name=%1").arg(quoted(name)));
}

static void add(const QString& name, const QString& proto,
                const QString& dir, const QString& port)
{
    QString field = (dir == QLatin1String("in")) ? QStringLiteral("localport")
                                                   : QStringLiteral("remoteport");
    netsh(QStringLiteral("advfirewall firewall add rule name=%1 dir=%2 action=allow protocol=%3 %4=%5")
          .arg(quoted(name), dir, proto, field, port));
}

static void addProgramRule(const QString& name, const QString& dir, const QString& exePath)
{
    if (exePath.isEmpty() || !QFileInfo::exists(exePath)) return;
    rm(name);
    netsh(QStringLiteral("advfirewall firewall add rule name=%1 dir=%2 action=allow program=%3 enable=yes")
          .arg(quoted(name), dir, quoted(QDir::toNativeSeparators(exePath))));
}

static bool ruleExists(const QString& name)
{
    QProcess p;
    p.setProgram(QStringLiteral("cmd"));
    p.setArguments({QStringLiteral("/c"),
                    QStringLiteral("netsh advfirewall firewall show rule name=") + quoted(name)});
    p.start();
    p.waitForFinished(5000);
    const QByteArray out = p.readAllStandardOutput() + p.readAllStandardError();
    return out.contains(name.toUtf8()) && !out.contains("No rules match");
}

static bool rulesExist()
{
    // The media path uses WebRTC/libdatachannel, which allocates dynamic UDP
    // ports. A fixed-port rule may exist from an older build but still leave
    // calls blocked, so require the program-wide rule specifically.
    return ruleExists(appName() + QStringLiteral(" App In")) &&
           ruleExists(appName() + QStringLiteral(" Media TCP In"));
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
    QString mp = QString::number(MediaSettings::MediaUdpPort);
    QString tp = QString::number(MediaSettings::MediaTcpPort);

    rm(appName() + " UDP");      add(appName() + " UDP",      "UDP", "in",  bp);
    rm(appName() + " UDP Out");  add(appName() + " UDP Out",  "UDP", "out", bp);
    rm(appName() + " TCP");      add(appName() + " TCP",      "TCP", "in",  sp);
    rm(appName() + " TCP Out");  add(appName() + " TCP Out",  "TCP", "out", sp);
    rm(appName() + " Media In"); add(appName() + " Media In", "UDP", "in",  mp);
    rm(appName() + " Media Out");add(appName() + " Media Out","UDP", "out", mp);
    rm(appName() + " Media TCP In"); add(appName() + " Media TCP In", "TCP", "in",  tp);
    rm(appName() + " Media TCP Out");add(appName() + " Media TCP Out","TCP", "out", tp);

    // WebRTC/libdatachannel uses ICE and dynamic UDP ports. Program-wide rules
    // are required on clean Windows installs, especially when the app is packaged
    // through Inno Setup and run from Program Files.
    const QString appExe = QCoreApplication::applicationFilePath();
    const QDir appDir(QFileInfo(appExe).absolutePath());
    const QString launcherExe = appDir.filePath(QStringLiteral("LocalCall.exe"));
    addProgramRule(appName() + " App In",       "in",  appExe);
    addProgramRule(appName() + " App Out",      "out", appExe);
    addProgramRule(appName() + " Launcher In",  "in",  launcherExe);
    addProgramRule(appName() + " Launcher Out", "out", launcherExe);
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

namespace FirewallHelper {
// Linux, macOS, Android use their own permission models; the OS-level firewall
// (iptables/nftables/pf/Android iptables) is managed by the user or the system.
// Qt's QUdpSocket and QTcpServer will bind correctly without any extra setup.

void ensureRules()    { /* no-op on non-Windows */ }
void runFirewallSetup(){ /* no-op on non-Windows */ }

#endif

} // namespace FirewallHelper
