using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Runtime.InteropServices;
using System.Threading;
using System.Threading.Tasks;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Data;
using System.Windows.Input;
using System.Windows.Interop;
using System.Windows.Media;
using System.Windows.Media.Imaging;

namespace RedBaron {
    public sealed class Choice {
        public string Label {get;set;} public object Value {get;set;} public bool Enabled {get;set;}
        public override string ToString() {return Label;}
        public Choice(string label,object value,bool enabled=true) {Label=label;Value=value;Enabled=enabled;}
    }
    public sealed class LauncherWindow {
        readonly Window window;
        readonly string[] args;
        Settings settings;
        Capabilities caps=new Capabilities();
        CancellationTokenSource installation;
        bool loading=true, dirty=false, probing=false;
        int playerIndex=0;
        Binding capture;
        WindowLayout layout;
        T Get<T>(string name) where T:FrameworkElement {return (T)window.FindName(name);}
        ComboBox Combo(string name) {return Get<ComboBox>(name);}
        TextBox Text(string name) {return Get<TextBox>(name);}
        TextBlock Label(string name) {return Get<TextBlock>(name);}
        Button Button(string name) {return Get<Button>(name);}
        CheckBox Check(string name) {return Get<CheckBox>(name);}
        static string Val(ComboBox box) {var c=box.SelectedItem as Choice;return c==null?"":Convert.ToString(c.Value);}
        static int Number(ComboBox box) {return Int32.Parse(Val(box));}
        void Choices(string name,IEnumerable<Choice> choices,object selected) {
            var box=Combo(name);box.ItemsSource=choices.ToList();
            foreach(Choice item in box.Items) if(Convert.ToString(item.Value)==Convert.ToString(selected)) {box.SelectedItem=item;break;}
            if(box.SelectedItem==null && box.Items.Count>0) box.SelectedIndex=0;
            var itemStyle=new Style(typeof(ComboBoxItem),window.FindResource(typeof(ComboBoxItem)) as Style);
            itemStyle.Setters.Add(new Setter(UIElement.IsEnabledProperty,new System.Windows.Data.Binding("Enabled")));
            box.ItemContainerStyle=itemStyle;
        }
        void Status(string message,bool error=false) {
            Label("Status").Text=message;
            Label("Status").Foreground=new SolidColorBrush((Color)ColorConverter.ConvertFromString(error?"#FF9A9F":"#B7C6BC"));
        }
        void Changed() {if(!loading) {dirty=true;Status("Unsaved changes.");}}
        [DllImport("dwmapi.dll")] static extern int DwmSetWindowAttribute(IntPtr window,int attr,ref int value,int size);
        public LauncherWindow(Window w,Settings s,string[] a) {
            window=w; settings=s;args=a;
            layout=new WindowLayout(window,Button("ToggleFullscreen"));
            window.SourceInitialized+=(sender,e)=>{int dark=1;DwmSetWindowAttribute(new WindowInteropHelper(window).Handle,20,ref dark,4);layout.Center();};
            Button("ToggleFullscreen").Click+=(sender,e)=>layout.Toggle();
            Button("CloseLauncher").Click+=(sender,e)=>window.Close();
            window.PreviewKeyDown+=(sender,e)=>{
                if(capture==null && (e.Key==Key.F11 || (e.Key==Key.Escape && layout.Fullscreen))) {layout.Toggle();e.Handled=true;}
            };
            ConfigureGrid();
            Choices("Player",Enumerable.Range(1,4).Select(i=>new Choice("Player "+i,i-1)),0);
            Choices("KeyboardPlayer",new[]{new Choice("Share with player 1",0),new Choice("Disabled",-1),new Choice("Exclusive · player 1",1),new Choice("Exclusive · player 2",2),new Choice("Exclusive · player 3",3),new Choice("Exclusive · player 4",4)},s.KeyboardPlayer);
            Choices("Resolution",Settings.Resolutions.Select(r=>new Choice(r.Replace("x"," × "),r)),s.Resolution);
            Choices("DisplayMode",new[]{new Choice("Borderless fullscreen",1),new Choice("Windowed",0)},s.Fullscreen?1:0);
            Choices("RenderScale",Enumerable.Range(1,6).Select(i=>new Choice(i+"× · "+(1280*i)+" × "+(720*i),i)),s.Scale);
            Choices("FsrSharpness",new[]{new Choice("Soft",0),new Choice("Medium",50),new Choice("Sharp (default)",90),new Choice("Maximum",100)},s.FsrSharpness);
            Choices("Anisotropic",new[]{new Choice("Game default",-1),new Choice("Off",0),new Choice("1×",1),new Choice("2×",2),new Choice("4×",3),new Choice("8×",4),new Choice("16×",5)},s.Anisotropic);
            ApplySettings();
            foreach(var name in new[]{"SourcePath","DestinationPath","SdkPath","PlayerName"}) Text(name).TextChanged+=(sender,e)=>{Changed();if(sender==Text("PlayerName")) ValidateName();};
            foreach(var name in new[]{"Resolution","DisplayMode","RenderScale","FrameRate","FsrSharpness","Anisotropic","Antialiasing","Upscaling","Gpu","KeyboardPlayer"}) Combo(name).SelectionChanged+=(sender,e)=>Changed();
            Combo("Upscaling").SelectionChanged+=(sender,e)=>{Combo("FsrSharpness").IsEnabled=Val(Combo("Upscaling"))=="fsr";};
            foreach(var name in new[]{"ShowLauncher","Vsync","DesktopShortcut"}) {Check(name).Checked+=(sender,e)=>Changed();Check(name).Unchecked+=(sender,e)=>Changed();}
            Combo("Player").SelectionChanged+=(sender,e)=>{if(loading)return;CommitGrid();playerIndex=Number(Combo("Player"));ShowPlayer();};
            Combo("Device").SelectionChanged+=(sender,e)=>{if(loading)return;settings.Players[playerIndex].Device=Val(Combo("Device"));Changed();};
            Button("BrowseSource").Click+=(sender,e)=>Browse("SourcePath");
            Button("BrowseDestination").Click+=(sender,e)=>Browse("DestinationPath");
            Button("BrowseSdk").Click+=(sender,e)=>Browse("SdkPath");
            Button("RefreshDevices").Click+=async(sender,e)=>await Refresh();
            Button("Save").Click+=(sender,e)=>Save();
            Button("Play").Click+=(sender,e)=>Play();
            Button("InstallButton").Click+=async(sender,e)=>await Install();
            Button("CancelInstall").Click+=(sender,e)=>{if(installation!=null) installation.Cancel();};
            Button("UseExisting").Click+=async(sender,e)=>await UseExisting();
            Button("RestoreBindings").Click+=(sender,e)=>{settings.Players[playerIndex].Bindings=Actions.Defaults();ShowPlayer();Changed();};
            window.PreviewKeyDown+=CaptureKey;
            window.PreviewMouseDown+=CaptureMouse;
            window.Closing+=(sender,e)=>{
                if(installation!=null) {e.Cancel=true;Status("Cancel the installation and wait for it to stop before closing.",true);return;}
                if(dirty && !args.Contains("--capture")) {
                    var result=MessageBox.Show(window,"Save changes before exiting?","RedBaron_Recomp",MessageBoxButton.YesNoCancel,MessageBoxImage.Question);
                    if(result==MessageBoxResult.Cancel || (result==MessageBoxResult.Yes && !Save())) e.Cancel=true;
                }
            };
            window.Loaded+=async(sender,e)=>{
                await Refresh();
                if(args.Contains("--capture")) await CaptureScreens();
            };
            loading=false;
        }
        void ApplySettings() {
            loading=true;
            Text("SourcePath").Text=settings.Source;Text("DestinationPath").Text=settings.Destination;Text("SdkPath").Text=settings.Sdk;
            Text("PlayerName").Text=settings.PlayerName;
            Check("ShowLauncher").IsChecked=settings.ShowLauncher; Check("DesktopShortcut").IsChecked=settings.DesktopShortcut;Check("Vsync").IsChecked=settings.Vsync;
            Select("Resolution",settings.Resolution);Select("DisplayMode",settings.Fullscreen?1:0);
            Select("RenderScale",settings.Scale);Select("FsrSharpness",settings.FsrSharpness);
            Select("Anisotropic",settings.Anisotropic);Select("KeyboardPlayer",settings.KeyboardPlayer);
            Label("HeaderStatus").Text=File.Exists(settings.Exe)?"Installation found":"Installation required";
            Label("InstallState").Text=File.Exists(settings.Exe)?"Current installation: "+settings.Root:"Select the source and destination folders to begin.";
            ShowPlayer();loading=false;
        }
        void Select(string name,object value) {
            foreach(Choice choice in Combo(name).Items) if(Convert.ToString(choice.Value)==Convert.ToString(value)) {Combo(name).SelectedItem=choice;return;}
        }
        void CommitGrid() {var grid=Get<DataGrid>("Bindings");grid.CommitEdit(DataGridEditingUnit.Cell,true);grid.CommitEdit(DataGridEditingUnit.Row,true);}
        void ConfigureGrid() {
            var grid=Get<DataGrid>("Bindings");
            grid.Columns.Add(new DataGridTextColumn{Header="Game action",Binding=new System.Windows.Data.Binding("Action"),IsReadOnly=true,Width=new DataGridLength(1.3,DataGridLengthUnitType.Star)});
            grid.Columns.Add(new DataGridTextColumn{Header="Keyboard / mouse",Binding=new System.Windows.Data.Binding("Keyboard"){UpdateSourceTrigger=UpdateSourceTrigger.PropertyChanged},Width=new DataGridLength(1,DataGridLengthUnitType.Star)});
            var padOptions=new List<Choice>{new Choice("Unassigned",-1)};
            padOptions.AddRange(Actions.PadNames.Select((n,i)=>new Choice(n,i)));
            grid.Columns.Add(new DataGridComboBoxColumn{Header="Controller input",ItemsSource=padOptions,DisplayMemberPath="Label",SelectedValuePath="Value",SelectedValueBinding=new System.Windows.Data.Binding("Pad"){UpdateSourceTrigger=UpdateSourceTrigger.PropertyChanged},Width=new DataGridLength(1.2,DataGridLengthUnitType.Star)});
            var factory=new FrameworkElementFactory(typeof(Button));factory.SetValue(ContentControl.ContentProperty,"Capture");factory.SetValue(Control.PaddingProperty,new Thickness(8,3,8,3));factory.SetValue(FrameworkElement.MinHeightProperty,28.0);
            factory.AddHandler(System.Windows.Controls.Button.ClickEvent,new RoutedEventHandler((sender,e)=>{
                CommitGrid();capture=((FrameworkElement)sender).DataContext as Binding;
                Label("CaptureHint").Text="Press a key or mouse button. Esc cancels; Delete clears the binding.";
                window.Focus();
            }));
            grid.Columns.Add(new DataGridTemplateColumn{Header="",CellTemplate=new DataTemplate{VisualTree=factory},Width=94});
            grid.CellEditEnding+=(sender,e)=>Changed();
        }
        void ShowPlayer() {
            bool previous=loading;loading=true;
            var options=new List<Choice>{new Choice("Automatic · connection order","auto"),new Choice("No controller","none")};
            options.AddRange(caps.devices.Select((d,i)=>new Choice(d.name+" · "+(i+1),d.key)));
            string selected=settings.Players[playerIndex].Device;
            if(!options.Any(o=>Convert.ToString(o.Value)==selected)) options.Add(new Choice("Disconnected · "+selected,selected));
            Choices("Device",options,selected);
            Get<DataGrid>("Bindings").ItemsSource=settings.Players[playerIndex].Bindings;
            loading=previous;
        }
        void ReadSettings() {
            CommitGrid();
            settings.Source=Text("SourcePath").Text.Trim();settings.Destination=Text("DestinationPath").Text.Trim();settings.Sdk=Text("SdkPath").Text.Trim();
            settings.PlayerName=Text("PlayerName").Text;settings.ShowLauncher=Check("ShowLauncher").IsChecked==true;
            settings.DesktopShortcut=Check("DesktopShortcut").IsChecked==true;settings.Vsync=Check("Vsync").IsChecked==true;
            settings.Resolution=Val(Combo("Resolution"));settings.Fullscreen=Number(Combo("DisplayMode"))==1;
            settings.Scale=Number(Combo("RenderScale"));settings.Fps=Number(Combo("FrameRate"));settings.FsrSharpness=Number(Combo("FsrSharpness"));
            settings.Anisotropic=Number(Combo("Anisotropic"));settings.KeyboardPlayer=Number(Combo("KeyboardPlayer"));
            settings.Antialiasing=Val(Combo("Antialiasing"));settings.Upscaling=Val(Combo("Upscaling"));settings.Adapter=Number(Combo("Gpu"));
        }
        bool Save() {
            try {ReadSettings();Services.Save(settings,caps);dirty=false;Status("Settings saved. They will apply the next time you start the game.");return true;}
            catch(Exception error) {Status(error.Message,true);return false;}
        }
        void Play() {
            if(!Save()) return;
            try {Services.Play(settings).Dispose();window.Close();}
            catch(Exception error) {Status(error.Message,true);}
        }
        async Task Refresh(bool preserve=true) {
            if(probing) return;
            if(preserve && caps.flags.Count>0 && !loading) ReadSettings();
            probing=true;
            Button("RefreshDevices").IsEnabled=false;Button("Save").IsEnabled=false;Button("Play").IsEnabled=false;
            try {
                caps=await Services.Probe(settings);
                bool previous=loading;loading=true;
                var gpus=new List<Choice>{new Choice("Automatic (recommended)",-1)};gpus.AddRange(caps.gpus.Select(g=>new Choice(g.name,g.index)));Choices("Gpu",gpus,settings.Adapter);
                var aa=new[]{new Choice("Off","none"),new Choice("FXAA","fxaa",caps.Supports("swap_post_effect","fxaa")),new Choice("FXAA extreme","fxaa_extreme",caps.Supports("swap_post_effect","fxaa_extreme"))};Choices("Antialiasing",aa,settings.Antialiasing);
                var effects=new[]{new Choice("Default · bilinear","bilinear"),new Choice("AMD FSR 1 · spatial","fsr",caps.Supports("present_effect","fsr")),new Choice("AMD CAS · sharpen","cas",caps.Supports("present_effect","cas")),new Choice("DLSS · not integrated","dlss",false)};Choices("Upscaling",effects,settings.Upscaling);
                Choices("FrameRate",Settings.FrameRates.Select(fps=>new Choice(fps==0?"Game default":fps+" FPS"+(fps>120?" · experimental":""),fps,fps==0||caps.Supports("redbaron_fps",fps.ToString()))),settings.Fps);
                Check("Vsync").IsEnabled=caps.flags.ContainsKey("present_vsync");
                Label("GpuSummary").Text=caps.gpus.Count>0?String.Join(" · ",caps.gpus.Select(g=>g.name)):"No physical GPU detected.";
                Label("UpscalingInfo").Text=caps.Supports("present_effect","fsr")?"FSR 1 reconstructs the final image, including the HUD. Choose a render scale below your output resolution. DLSS and temporal FSR are not integrated yet.":"Update the runtime to enable FSR 1, CAS and monitor VSync. DLSS is not integrated yet.";
                ShowPlayer();loading=previous;
                Status(dirty?"Unsaved changes.":"Ready. "+caps.devices.Count+" controller(s) detected.");
                if(!String.IsNullOrEmpty(caps.inputError)) Status("Controller detection failed: "+caps.inputError,true);
                Button("Save").IsEnabled=true;Button("Play").IsEnabled=File.Exists(settings.Exe);
            } catch(Exception error) {
                Status(error.Message,true); Label("GpuSummary").Text="Complete installation to detect available graphics settings.";
                // First-time installation still works; Save/Play wait for its runtime.
            } finally {probing=false;Button("RefreshDevices").IsEnabled=true;}
        }
        void Browse(string name) {
            using(var dialog=new System.Windows.Forms.FolderBrowserDialog{Description="Select a folder",SelectedPath=Text(name).Text,ShowNewFolderButton=true})
                if(dialog.ShowDialog()==System.Windows.Forms.DialogResult.OK) Text(name).Text=dialog.SelectedPath;
        }
        void ValidateName() {
            string n=Text("PlayerName").Text;
            Label("NameValidation").Text=n.Length+"/15 · Letters A–Z, numbers, internal spaces, hyphens or underscores.";
        }
        void CaptureKey(object sender,KeyEventArgs e) {
            if(capture==null) return;e.Handled=true;
            Key k=e.Key==Key.System?e.SystemKey:e.Key;
            if(k==Key.Escape) {EndCapture(null);return;} if(k==Key.Delete) {EndCapture("");return;}
            if(k==Key.LeftShift || k==Key.RightShift || k==Key.LeftCtrl || k==Key.RightCtrl || k==Key.LeftAlt || k==Key.RightAlt) return;
            string name=k.ToString();
            if(k>=Key.D0 && k<=Key.D9) name=((int)k-(int)Key.D0).ToString();
            if(k>=Key.NumPad0 && k<=Key.NumPad9) name="Numpad"+((int)k-(int)Key.NumPad0);
            if(k==Key.Back) name="Backspace";if(k==Key.Enter) name="Return";
            if(k==Key.PageUp) name="PageUp";if(k==Key.PageDown) name="PageDown";
            name=KeyboardNames.FromKey(k,name);
            string mods="";if((Keyboard.Modifiers&ModifierKeys.Control)!=0) mods+="Ctrl+";if((Keyboard.Modifiers&ModifierKeys.Shift)!=0) mods+="Shift+";if((Keyboard.Modifiers&ModifierKeys.Alt)!=0) mods+="Alt+";
            try {Actions.ValidateKeyboard(mods+name);EndCapture(mods+name);} catch {Label("CaptureHint").Text="Unsupported key. Try another key; Esc cancels.";}
        }
        void CaptureMouse(object sender,MouseButtonEventArgs e) {
            if(capture==null)return;
            string key=e.ChangedButton==MouseButton.Left?"LMB":e.ChangedButton==MouseButton.Right?"RMB":e.ChangedButton==MouseButton.Middle?"MMB":null;
            e.Handled=true;if(key!=null)EndCapture(key);
        }
        void EndCapture(string value) {
            if(value!=null) {capture.Keyboard=value;Get<DataGrid>("Bindings").Items.Refresh();Changed();}
            capture=null;Label("CaptureHint").Text="Capture: keyboard/mouse. Double-click a cell to edit the controller binding.";
        }
        async Task Install() {
            if(installation!=null)return;
            settings.Source=Text("SourcePath").Text.Trim();settings.Destination=Text("DestinationPath").Text.Trim();settings.Sdk=Text("SdkPath").Text.Trim();settings.DesktopShortcut=Check("DesktopShortcut").IsChecked==true;
            installation=new CancellationTokenSource();
            foreach(string name in new[]{"InstallButton","UseExisting","Save","Play","RefreshDevices","BrowseSource","BrowseDestination","BrowseSdk"})Button(name).IsEnabled=false;
            Get<ProgressBar>("InstallProgress").Visibility=Visibility.Visible;Button("CancelInstall").Visibility=Visibility.Visible;Text("InstallLog").Visibility=Visibility.Visible;Text("InstallLog").Clear();
            Status("Installation in progress…");
            try {
                await Services.Install(settings,line=>window.Dispatcher.BeginInvoke(new Action(()=>{
                    Text("InstallLog").AppendText(line+Environment.NewLine);Text("InstallLog").ScrollToEnd();Label("InstallState").Text=line;
                })),installation.Token);
                string target=Path.GetFullPath(settings.Destination);
                bool shortcut=settings.DesktopShortcut;string sdk=settings.Sdk;
                settings=Settings.Load(target);settings.Sdk=sdk;settings.DesktopShortcut=shortcut;
                Json.Write(settings.FileName,settings);
                if(shortcut) Services.CreateShortcut(settings,Path.Combine(target,"RedBaron_Launcher.exe"));
                dirty=false;ApplySettings();await Refresh(false);Status("Installation complete. Click Play to start the game.");
            } catch(OperationCanceledException) {Status("Installation cancelled. Your source files were preserved; choose an empty folder to try again.",true);}
            catch(Exception error) {Status(error.Message,true);}
            finally {
                installation.Dispose();installation=null;
                Get<ProgressBar>("InstallProgress").Visibility=Visibility.Collapsed;Button("CancelInstall").Visibility=Visibility.Collapsed;
                foreach(string name in new[]{"InstallButton","UseExisting","RefreshDevices","BrowseSource","BrowseDestination","BrowseSdk"})Button(name).IsEnabled=true;
                Button("Save").IsEnabled=caps.flags.Count>0;Button("Play").IsEnabled=caps.flags.Count>0 && File.Exists(settings.Exe);
            }
        }
        async Task UseExisting() {
            if(dirty && MessageBox.Show(window,"Discard unsaved changes and switch installations?","RedBaron_Recomp",MessageBoxButton.YesNo,MessageBoxImage.Question)!=MessageBoxResult.Yes)return;
            using(var dialog=new System.Windows.Forms.FolderBrowserDialog{Description="Select the folder containing game and build_cpp_release",SelectedPath=settings.Root}) {
                if(dialog.ShowDialog()!=System.Windows.Forms.DialogResult.OK)return;
                try {
                    var candidate=Settings.Load(dialog.SelectedPath);
                    if(!File.Exists(candidate.Exe) || !File.Exists(Path.Combine(candidate.Root,"game","default.xex")))throw new IOException("This folder does not contain a complete installation.");
                    settings=candidate;dirty=false;ApplySettings();await Refresh(false);
                } catch(Exception error) {Status(error.Message,true);}
            }
        }
        async Task CaptureScreens() {
            int index=Array.IndexOf(args,"--capture");
            if(index+1>=args.Length) {window.Close();return;}
            string folder=Path.GetFullPath(args[index+1]);Directory.CreateDirectory(folder);
            for(int tab=0;tab<4;tab++) {
                Get<TabControl>("Tabs").SelectedIndex=tab;await Task.Delay(200);window.UpdateLayout();
                SaveCapture(Path.Combine(folder,new[]{"install","video","controls","online"}[tab]+".png"));
                if(tab==1) {
                    var item=(TabItem)Get<TabControl>("Tabs").Items[1];
                    ((ScrollViewer)item.Content).ScrollToEnd();await Task.Delay(100);window.UpdateLayout();
                    SaveCapture(Path.Combine(folder,"video_details.png"));
                }
            }
            dirty=false;window.Close();
        }
        void SaveCapture(string path) {
            var client=(FrameworkElement)window.Content;
            var target=new RenderTargetBitmap((int)client.ActualWidth,(int)client.ActualHeight,96,96,PixelFormats.Pbgra32);
            var backdrop=new DrawingVisual();
            using(var context=backdrop.RenderOpen()) context.DrawRectangle(window.Background,null,new Rect(0,0,client.ActualWidth,client.ActualHeight));
            target.Render(backdrop);target.Render(client);
            var encoder=new PngBitmapEncoder();encoder.Frames.Add(BitmapFrame.Create(target));
            using(var file=File.Create(path))encoder.Save(file);
        }
    }
}
