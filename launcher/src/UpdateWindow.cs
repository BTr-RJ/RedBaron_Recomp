using System;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Threading;
using System.Threading.Tasks;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Media;

namespace RedBaron {
    // Runs from the verified installer in the owned job, outside the files being replaced.
    static class UpdateWindow {
        public static int Run(string[] args) {
            if(args.Length!=5)throw new ArgumentException("Invalid update handoff.");
            string root=AutomaticUpdate.Full(args[1]),job=AutomaticUpdate.Full(args[2]);
            int parentId;long parentTicks;
            if(!Int32.TryParse(args[3],out parentId)||!Int64.TryParse(args[4],out parentTicks))throw new ArgumentException("Invalid launcher handoff.");
            AutomaticUpdate.ValidateJob(root,job);
            if(!String.Equals(Services.LauncherExe,Path.Combine(job,"package","RedBaron_Launcher.exe"),StringComparison.OrdinalIgnoreCase))
                throw new InvalidDataException("The updater must run from its verified working folder.");
            bool recovering=args[0]=="--recover-update",busy=true,reopen=false;int exit=1;
            var background=(Brush)new BrushConverter().ConvertFromString("#11191F");
            var foreground=(Brush)new BrushConverter().ConvertFromString("#ECF1F4");
            var window=new Window{Title="RedBaron_Recomp — Update",Width=660,Height=460,MinWidth=540,MinHeight=380,WindowStartupLocation=WindowStartupLocation.CenterScreen,Background=background,Foreground=foreground,FontFamily=new FontFamily("Segoe UI"),FontSize=14};
            var panel=new Grid{Margin=new Thickness(26)};
            panel.RowDefinitions.Add(new RowDefinition{Height=GridLength.Auto});panel.RowDefinitions.Add(new RowDefinition{Height=GridLength.Auto});
            panel.RowDefinitions.Add(new RowDefinition{Height=GridLength.Auto});panel.RowDefinitions.Add(new RowDefinition());panel.RowDefinitions.Add(new RowDefinition{Height=GridLength.Auto});
            var heading=new TextBlock{Text=recovering?"Restoring your previous version":"Updating RedBaron_Recomp",FontSize=23,FontWeight=FontWeights.SemiBold,TextWrapping=TextWrapping.Wrap,Margin=new Thickness(0,0,0,12)};
            var status=new TextBlock{Text="Waiting for the launcher to close…",TextWrapping=TextWrapping.Wrap,Margin=new Thickness(0,0,0,12)};
            var progress=new ProgressBar{Height=8,IsIndeterminate=true,Margin=new Thickness(0,0,0,16)};
            var details=new TextBox{IsReadOnly=true,TextWrapping=TextWrapping.Wrap,VerticalScrollBarVisibility=ScrollBarVisibility.Auto,Background=background,Foreground=foreground,BorderThickness=new Thickness(0),FontSize=12};
            var buttons=new StackPanel{Orientation=Orientation.Horizontal,HorizontalAlignment=HorizontalAlignment.Right,Margin=new Thickness(0,16,0,0)};
            var logs=new Button{Content="Open logs",Padding=new Thickness(14,8,14,8),Margin=new Thickness(0,0,10,0)};
            var action=new Button{Content="Cancel update",Padding=new Thickness(14,8,14,8)};buttons.Children.Add(logs);buttons.Children.Add(action);
            FrameworkElement[] elements={heading,status,progress,details,buttons};for(int i=0;i<elements.Length;i++){Grid.SetRow(elements[i],i);panel.Children.Add(elements[i]);}window.Content=panel;
            var cancellation=new CancellationTokenSource();
            logs.Click+=(s,e)=>{using(var p=Process.Start("explorer.exe",Services.QuoteArgument(job))) {}};
            action.Click+=(s,e)=>{if(busy){cancellation.Cancel();action.IsEnabled=false;status.Text="Stopping preparation. Your current installation is being preserved…";}else {if(reopen)AutomaticUpdate.Reopen(root);window.Close();}};
            window.Closing+=(s,e)=>{if(busy){e.Cancel=true;status.Text="Wait for the update to finish, or use Cancel update during preparation.";}};
            object logGate=new object();
            Action<string> log=line=>{
                lock(logGate)File.AppendAllText(Path.Combine(job,"update.log"),line+Environment.NewLine);
                window.Dispatcher.BeginInvoke(new Action(()=>{
                    InstallProgress value;
                    if(InstallProgress.TryParse(line,out value)){status.Text=value.ToString();progress.IsIndeterminate=!value.Known;progress.Value=value.Percent;if(value.stage=="Applying update")action.IsEnabled=false;}
                    else {details.AppendText(line+Environment.NewLine);details.ScrollToEnd();}
                }));
            };
            window.Loaded+=async(s,e)=>{
                Mutex mutex=null;bool acquired=false;
                try {
                    await Task.Run(()=>{
                        try {using(var parent=Process.GetProcessById(parentId))if(parent.StartTime.ToUniversalTime().Ticks==parentTicks && !parent.WaitForExit(30000))throw new IOException("The launcher did not close. Close it and try again.");}
                        catch(ArgumentException) {} // Parent already exited.
                    });
                    mutex=new Mutex(false,AutomaticUpdate.MutexName(root));
                    try {acquired=mutex.WaitOne(0);}catch(AbandonedMutexException){acquired=true;}
                    if(!acquired)throw new IOException("Another launcher is open for this game. Close it and try again.");
                    if(recovering)await Task.Run(()=>AutomaticUpdate.Recover(root,log));
                    else await AutomaticUpdate.Apply(root,job,log,cancellation.Token);
                    exit=0;reopen=true;status.Text=recovering?"Previous version restored.":"Update complete.";
                } catch(OperationCanceledException) {log("Update cancelled before file replacement.");status.Text="Update cancelled. Your previous version is ready to play.";reopen=true;}
                catch(Exception error) {
                    log(error.ToString());status.Text="The update could not finish. Open logs for details.";
                    reopen=!File.Exists(Path.Combine(root,"launcher-data","update-pending.json"));
                    if(reopen)status.Text+=" Your previous installation is available.";
                    else status.Text+=" Close this window and reopen the launcher to retry recovery.";
                } finally {
                    if(acquired)mutex.ReleaseMutex();if(mutex!=null)mutex.Dispose();
                    busy=false;progress.IsIndeterminate=false;action.IsEnabled=true;action.Content=reopen?"Open launcher":"Close";
                    if(exit==0){AutomaticUpdate.Reopen(root);window.Close();}
                }
            };
            var app=new Application{ShutdownMode=ShutdownMode.OnMainWindowClose};app.Run(window);cancellation.Dispose();return exit;
        }
    }
}
