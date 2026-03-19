using System;
using System.Windows;

namespace LocalCallPro;

public partial class App : Application
{
    protected override void OnStartup(StartupEventArgs e)
    {
        base.OnStartup(e);

        // If relaunched by FirewallHelper with /firewall arg → add rules and exit
        foreach (var arg in e.Args)
        {
            if (arg.Equals("/firewall", StringComparison.OrdinalIgnoreCase))
            {
                FirewallHelper.RunFirewallSetup();
                Shutdown(0);
                return;
            }
        }

        // Normal startup — ensure firewall rules exist (UAC prompt if needed, once only)
        FirewallHelper.EnsureRules();
    }
}
