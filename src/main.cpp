#include <QApplication>
#include <QDir>
#include <QFont>
#include <QIcon>
#include <QStandardPaths>
#include <QMetaType>
#include "MainWindow.h"
#include "FirewallHelper.h"
#include "SigMsg.h"

int main(int argc, char* argv[])
{
    // High-DPI: must be set before QApplication on Qt 5
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    QCoreApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
    QCoreApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);
#endif

    QApplication app(argc, argv);
    app.setApplicationName("LocalCall");
    app.setOrganizationName("LocalCall");
    app.setApplicationVersion("1.0");
    app.setQuitOnLastWindowClosed(true);

    // Register custom types used in cross-thread signals (queued connections).
    // Without this Qt cannot copy the value into the event queue → crash.
    qRegisterMetaType<SigMsg>("SigMsg");

    // ── App icon ─────────────────────────────────────────────────────────────
    // The Windows .rc file embeds the icon into the .exe for Explorer/taskbar.
    // We also set it programmatically so all platforms get it in the title bar
    // and alt-tab switcher.
    {
        const QStringList candidates = {
            QCoreApplication::applicationDirPath() + "/icon.ico",
            QCoreApplication::applicationDirPath() + "/LocalCall.png",
            QCoreApplication::applicationDirPath() + "/../Resources/icon.icns", // macOS bundle
            ":/icon.ico",   // Qt resource system fallback
            ":/icon.png",
        };
        QIcon themed = QIcon::fromTheme("localcall");
        if (!themed.isNull()) {
            app.setWindowIcon(themed);
        } else {
            for (const QString& p : candidates) {
                QIcon ic(p);
                if (!ic.isNull()) { app.setWindowIcon(ic); break; }
            }
        }
    }

    // ── Platform-appropriate font ─────────────────────────────────────────────
#if defined(Q_OS_MACOS)
    app.setFont(QFont("SF Pro Text", 13));
#elif defined(Q_OS_ANDROID)
    app.setFont(QFont("Roboto", 11));
#elif defined(Q_OS_WIN)
    app.setFont(QFont("Segoe UI", 10));
#else   // Linux / FreeBSD / other UNIX
    app.setFont(QFont("Ubuntu", 10));
#endif

    // ── Firewall / port setup (Windows-only) ─────────────────────────────────
#ifdef Q_OS_WIN
    for (int i = 1; i < argc; ++i) {
        if (QString(argv[i]).compare("/firewall", Qt::CaseInsensitive) == 0) {
            FirewallHelper::runFirewallSetup();
            return 0;
        }
    }
    FirewallHelper::ensureRules();
#endif

    MainWindow win;
    win.show();

    return app.exec();
}
