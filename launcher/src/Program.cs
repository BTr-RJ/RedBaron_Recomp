using System;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Security.Cryptography;
using System.Text;
using System.Threading;
using System.Windows;
using System.Windows.Input;
using System.Windows.Markup;

namespace RedBaron {
    static class Program {
        [DllImport("user32.dll")] static extern short GetAsyncKeyState(int key);
        [STAThread] public static int Main(string[] args) {
            try {
                if(args.Contains("--self-test")) return SelfTests.Run(args);
                string root=Services.FindRoot(args);
                using(var sha=SHA256.Create()) {
                    string key=BitConverter.ToString(sha.ComputeHash(Encoding.UTF8.GetBytes(root.ToLowerInvariant()))).Replace("-","");
                    bool created;
                    using(var mutex=new Mutex(true,"Local\\RedBaron_Launcher_"+key,out created)) {
                        if(!created) {MessageBox.Show("Este launcher já está aberto para esta instalação.","RedBaron_Recomp");return 0;}
                        Settings settings=Settings.Load(root);
                        bool force=args.Contains("--settings") || args.Contains("--capture") || GetAsyncKeyState(0x10)<0;
                        if(!force && !settings.ShowLauncher) {
                            try {Services.Play(settings).Dispose();return 0;}
                            catch(Exception error) {MessageBox.Show(error.Message+"\nO launcher será aberto para corrigir a instalação.","RedBaron_Recomp");}
                        }
                        var app=new Application {ShutdownMode=ShutdownMode.OnMainWindowClose};
                        using(var stream=Assembly.GetExecutingAssembly().GetManifestResourceStream("MainWindow.xaml")) {
                            var window=(Window)XamlReader.Load(stream);
                            if(args.Contains("--compact")) {window.Width=920;window.Height=680;}
                            new LauncherWindow(window,settings,args);
                            return app.Run(window);
                        }
                    }
                }
            } catch(Exception error) {
                MessageBox.Show(error.ToString(),"RedBaron_Recomp — não foi possível abrir",MessageBoxButton.OK,MessageBoxImage.Error);
                return 1;
            }
        }
    }
}
