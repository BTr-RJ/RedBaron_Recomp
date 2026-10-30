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
        OnlineSession online;
        ReleaseAsset availableUpdate;
        string updateFolder;
        bool updateBusy, downloadingUpdate;
        bool connectingOnline=false, onlineGame=false, launchingGame=false;
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
            layout=new WindowLayout(window);
            Label("SdkInfo").Text=(Services.HasBundledSdk()?"This launcher includes the ReXGlue SDK and online components. ":"Select an installed compatible ReXGlue SDK. ")+(Services.HasOfflineTools()?"Offline build tools are included. Compatible installed tools are reused; only missing components are installed. ":"Missing tools can be downloaded during installation. ")+"Microsoft Build Tools may request administrator approval. No game files are downloaded.";
            if(!Services.HasInstallerPayload())Label("SdkInfo").Text="Build files are not needed to play or add DLCs. Use the full installer to create another game installation.";
            window.SourceInitialized+=(sender,e)=>{int dark=1;DwmSetWindowAttribute(new WindowInteropHelper(window).Handle,20,ref dark,4);layout.Center();};
            window.PreviewKeyDown+=(sender,e)=>{
                if(capture==null && (e.Key==Key.F11 || (e.Key==Key.Escape && layout.Fullscreen))) {layout.Toggle();e.Handled=true;}
            };
            ConfigureGrid();
            Choices("Player",Enumerable.Range(1,4).Select(i=>new Choice("Player "+i,i-1)),0);
            Choices("KeyboardPlayer",new[]{new Choice("Share with player 1",0),new Choice("Disabled",-1),new Choice("Exclusive · player 1",1),new Choice("Exclusive · player 2",2),new Choice("Exclusive · player 3",3),new Choice("Exclusive · player 4",4)},s.KeyboardPlayer);
            Choices("Resolution",Settings.Resolutions.Select(r=>new Choice(r.Replace("x"," × "),r)),s.Resolution);
            Choices("DisplayMode",new[]{new Choice("Borderless fullscreen",1),new Choice("Windowed",0)},s.Fullscreen?1:0);
            Choices("RenderScale",Enumerable.Range(1,6).Select(i=>new Choice(i+"× · "+(1280*i)+" × "+(720*i),i)),s.Scale);
            Choices("DlssMode",new[]{new Choice("DLAA · native","native_aa"),new Choice("Quality","quality"),new Choice("Balanced","balanced"),new Choice("Performance","performance"),new Choice("Ultra Performance","ultra_performance")},s.DlssMode);
            Choices("FsrSharpness",new[]{new Choice("Soft",0),new Choice("Medium",50),new Choice("Sharp (default)",90),new Choice("Maximum",100)},s.FsrSharpness);
            Choices("Anisotropic",new[]{new Choice("Game default",-1),new Choice("Off",0),new Choice("1×",1),new Choice("2×",2),new Choice("4×",3),new Choice("8×",4),new Choice("16×",5)},s.Anisotropic);
            Choices("OnlineMode",new[]{new Choice("Multiplayer · up to 8 players","multiplayer"),new Choice("Co-op · 2 players","coop")},s.OnlineMode);
            Choices("OnlineLocalPlayers",new[]{new Choice("1 player",1),new Choice("2 players",2)},s.OnlineLocalPlayers);
            ApplySettings();
            foreach(var name in new[]{"SourcePath","DlcSource","DestinationPath","SdkPath","PlayerName","LanPeer","OnlineServer"}) Text(name).TextChanged+=(sender,e)=>{Changed();UpdateInstallAction();if(sender==Text("PlayerName")) ValidateName();};
            foreach(var name in new[]{"OnlineMode","OnlineLocalPlayers"}) Combo(name).SelectionChanged+=(sender,e)=>Changed();
            foreach(var name in new[]{"Resolution","DisplayMode","RenderScale","FrameRate","FsrSharpness","DlssMode","Anisotropic","Antialiasing","Upscaling","Gpu","KeyboardPlayer"}) Combo(name).SelectionChanged+=(sender,e)=>Changed();
            Combo("Gpu").SelectionChanged+=(sender,e)=> {
                if(loading || caps.flags.Count==0) return;
                var selected=Val(Combo("Upscaling"));
                var effects=Combo("Upscaling").ItemsSource as IEnumerable<Choice>;
                if(effects==null) return;
                var updated=effects.Select(c=>new Choice(c.Label,c.Value,c.Value.ToString()=="dlss"?caps.Supports("present_effect","dlss") && caps.DlssAvailable(Number(Combo("Gpu"))):c.Enabled)).ToArray();
                Choices("Upscaling",updated,selected);
            };
            foreach(var name in new[]{"Upscaling","DlssMode","Resolution","DisplayMode"}) Combo(name).SelectionChanged+=(sender,e)=>UpdateDlssControls();
            foreach(var name in new[]{"ShowLauncher","Vsync","DesktopShortcut","ShowFps","Hdr","LanEnabled","EosEnabled"}) {Check(name).Checked+=(sender,e)=>Changed();Check(name).Unchecked+=(sender,e)=>Changed();}
            Combo("Player").SelectionChanged+=(sender,e)=>{if(loading)return;CommitGrid();playerIndex=Number(Combo("Player"));ShowPlayer();};
            Combo("Device").SelectionChanged+=(sender,e)=>{if(loading)return;settings.Players[playerIndex].Device=Val(Combo("Device"));Changed();};
            Button("BrowseSource").Click+=(sender,e)=>Browse("SourcePath");
            Button("BrowseSourceFile").Click+=(sender,e)=>{var dialog=new Microsoft.Win32.OpenFileDialog {Title="Select default.xex or an Xbox 360 STFS game package",Filter="Xbox 360 game files|*.*",CheckFileExists=true};if(dialog.ShowDialog(window)==true)Text("SourcePath").Text=dialog.FileName;};
            Button("BrowseDestination").Click+=(sender,e)=>Browse("DestinationPath");
            Button("BrowseSdk").Click+=(sender,e)=>Browse("SdkPath");
            Button("BrowseDlc").Click+=(sender,e)=>Browse("DlcSource");
            Button("RefreshDevices").Click+=async(sender,e)=>await Refresh();
            Button("Save").Click+=(sender,e)=>Save();
            Button("Play").Click+=(sender,e)=>Play();
            Button("CreateRoom").Click+=async(sender,e)=>await ConnectOnline(false);
            Button("JoinRoom").Click+=async(sender,e)=>await ConnectOnline(true);
            Button("LeaveRoom").Click+=async(sender,e)=>await LeaveOnline();
            Button("CopyRoom").Click+=(sender,e)=>{try {if(online!=null && online.Room.Length>0){Clipboard.SetText(OnlineSession.Invitation(settings.OnlineServer,online.Room));Status("Invitation copied. Send it to your friends.");}}catch(Exception error){Status("Could not copy the invitation: "+error.Message,true);}};
            Button("InstallButton").Click+=async(sender,e)=>await Install();
            Button("CheckTools").Click+=async(sender,e)=>await CheckBuildTools();
            Button("CancelInstall").Click+=(sender,e)=>{if(installation!=null) installation.Cancel();};
            Button("UseExisting").Click+=async(sender,e)=>await UseExisting();
            Button("CheckUpdates").Click+=async(sender,e)=>await CheckForUpdates();
            Button("DownloadUpdate").Click+=async(sender,e)=>await DownloadUpdate();
            Button("OpenUpdates").Click+=(sender,e)=>{if(updateFolder!=null)Process.Start("explorer.exe",Services.QuoteArgument(updateFolder));};
            Button("OpenLogs").Click+=(sender,e)=>{try {string logs=Path.Combine(settings.Root,"launcher-data","logs");Directory.CreateDirectory(logs);Process.Start("explorer.exe",Services.QuoteArgument(logs));}catch(Exception error){Status(error.Message,true);}};
            Button("RestoreBindings").Click+=(sender,e)=>{settings.Players[playerIndex].Bindings=Actions.Defaults();ShowPlayer();Changed();};
            window.PreviewKeyDown+=CaptureKey;
            window.PreviewMouseDown+=CaptureMouse;
            window.Closing+=(sender,e)=>{
                if(connectingOnline || onlineGame || launchingGame) {e.Cancel=true;Status("Wait for the game or room connection before closing the launcher.",true);return;}
                if(installation!=null) {e.Cancel=true;Status("Wait for the installation to finish before closing.",true);return;}
                if(downloadingUpdate) {e.Cancel=true;Status("Wait for the update download to finish before closing.",true);return;}
                if(dirty && !args.Contains("--capture")) {
                    var result=MessageBox.Show(window,"Save changes before exiting?","RedBaron_Recomp",MessageBoxButton.YesNoCancel,MessageBoxImage.Question);
                    if(result==MessageBoxResult.Cancel || (result==MessageBoxResult.Yes && !Save())) e.Cancel=true;
                }
            };
            window.Closed+=(sender,e)=>{if(online!=null)online.Dispose();};
            window.Loaded+=async(sender,e)=>{
                await Refresh();
                await CheckBuildTools();
                if(args.Contains("--capture")) await CaptureScreens();
                else await CheckForUpdates();
            };
            loading=false;
        }
        void ApplySettings() {
            loading=true;
            Text("SourcePath").Text=settings.Source;Text("DestinationPath").Text=settings.Destination;Text("SdkPath").Text=settings.Sdk;
            Text("DlcSource").Text=settings.DlcSource;
            Text("PlayerName").Text=settings.PlayerName;
            Text("LanPeer").Text=settings.LanPeer; Check("LanEnabled").IsChecked=settings.LanEnabled;
            Text("OnlineServer").Text=settings.OnlineServer;
            Check("EosEnabled").IsChecked=settings.EosEnabled;
            Label("EosState").Text=File.Exists(settings.EosConfig) && File.Exists(Path.Combine(settings.Build,"EOSSDK-Win64-Shipping.dll"))
                ? "Online components installed. This test build still requires validation between separate PCs."
                : "Online components are not installed for this game folder.";
            Select("OnlineMode",settings.OnlineMode);Select("OnlineLocalPlayers",settings.OnlineLocalPlayers);
            Check("ShowLauncher").IsChecked=settings.ShowLauncher; Check("DesktopShortcut").IsChecked=settings.DesktopShortcut;Check("Vsync").IsChecked=settings.Vsync;Check("ShowFps").IsChecked=settings.ShowFps;Check("Hdr").IsChecked=settings.Hdr;
            Select("Resolution",settings.Resolution);Select("DisplayMode",settings.Fullscreen?1:0);
            Select("RenderScale",settings.Scale);Select("FsrSharpness",settings.FsrSharpness);Select("DlssMode",settings.DlssMode);
            Select("Anisotropic",settings.Anisotropic);Select("KeyboardPlayer",settings.KeyboardPlayer);
            Label("HeaderStatus").Text=Services.HasInstallation(settings.Root)?"Installation found":"Installation required";
            UpdateInstallAction();
            Label("InstallState").Text=Services.HasInstallation(settings.Root)?"Current installation: "+settings.Root:"Select a game source and an installation folder to begin.";
            ShowPlayer();loading=false;
        }
        async Task CheckBuildTools() {
            Button("CheckTools").IsEnabled=false;
            try {
                var tools=await Services.CheckBuildTools(settings);
                int ready=tools.Count(t=>t.available);
                Get<Expander>("BuildToolsPanel").Header=ready==tools.Count?"Build tools — all required tools installed":"Build tools — "+(tools.Count-ready)+" missing or outdated";
                Label("ToolInventory").Text=String.Join(Environment.NewLine,tools.Select(t=>t.name+": "+(t.available?t.version+" — already installed":"missing or outdated — will be installed")));
            }catch(Exception error){Label("ToolInventory").Text=error.Message;}
            finally {Button("CheckTools").IsEnabled=installation==null;}
        }
        void UpdateDlssControls() {
            bool dlss=Val(Combo("Upscaling"))=="dlss";
            Combo("DlssMode").IsEnabled=dlss;
            Combo("RenderScale").IsEnabled=!dlss;
            Combo("FsrSharpness").IsEnabled=Val(Combo("Upscaling"))=="fsr" || Val(Combo("Upscaling"))=="fsr3";
            Label("DlssInfo").Text=dlss?"Render scale is selected automatically. Quality is recommended at 4K. Lower modes trade detail for speed; Balanced and Performance may share a render scale.":"Select NVIDIA DLSS to choose its quality mode.";
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
            settings.DlcSource=Text("DlcSource").Text.Trim();
            settings.PlayerName=Text("PlayerName").Text;settings.ShowLauncher=Check("ShowLauncher").IsChecked==true;
            settings.LanEnabled=Check("LanEnabled").IsChecked==true;settings.LanPeer=Text("LanPeer").Text.Trim();
            settings.EosEnabled=Check("EosEnabled").IsChecked==true;
            settings.OnlineServer=Text("OnlineServer").Text.Trim();settings.OnlineMode=Val(Combo("OnlineMode"));settings.OnlineLocalPlayers=Number(Combo("OnlineLocalPlayers"));
            settings.DesktopShortcut=Check("DesktopShortcut").IsChecked==true;settings.Vsync=Check("Vsync").IsChecked==true;settings.ShowFps=Check("ShowFps").IsChecked==true;settings.Hdr=Check("Hdr").IsChecked==true;
            settings.Resolution=Val(Combo("Resolution"));settings.Fullscreen=Number(Combo("DisplayMode"))==1;
            settings.DlssMode=Val(Combo("DlssMode"));settings.Scale=Number(Combo("RenderScale"));settings.Fps=Number(Combo("FrameRate"));settings.FsrSharpness=Number(Combo("FsrSharpness"));
            settings.Anisotropic=Number(Combo("Anisotropic"));settings.KeyboardPlayer=Number(Combo("KeyboardPlayer"));
            settings.Antialiasing=Val(Combo("Antialiasing"));settings.Upscaling=Val(Combo("Upscaling"));settings.Adapter=Number(Combo("Gpu"));
        }
        bool Save() {
            try {ReadSettings();Services.Save(settings,caps);Select("RenderScale",settings.Scale);dirty=false;Status("Settings saved. They will apply the next time you start the game.");return true;}
            catch(Exception error) {Status(error.Message,true);return false;}
        }
        async void Play() {
            if(!Save()) return;
            try {
                launchingGame=true;Button("Play").IsEnabled=false;Status("Preparing cached shaders and starting the game…");
                using(var game=Services.Play(settings,online==null?null:online.GameArguments())) {
                    await Services.WaitForGameWindow(game);
                    launchingGame=false;
                    if(online==null) {window.Close();return;}
                    onlineGame=true;window.Hide();
                    try {await Task.Run(()=>game.WaitForExit());}
                    finally {onlineGame=false;window.Show();window.Activate();}
                }
            }
            catch(Exception error) {Status(error.Message,true);}
            finally {launchingGame=false;Button("Play").IsEnabled=caps.flags.Count>0 && File.Exists(settings.Exe);}
        }
        void OnlineControls(bool enabled) {
            foreach(var name in new[]{"CreateRoom","JoinRoom"})Button(name).IsEnabled=enabled;
            foreach(var name in new[]{"OnlineServer","RoomCode","PlayerName"})Text(name).IsEnabled=enabled;
            Combo("OnlineMode").IsEnabled=enabled;Combo("OnlineLocalPlayers").IsEnabled=enabled;
            Button("LeaveRoom").IsEnabled=!enabled && !connectingOnline;
            Button("CopyRoom").IsEnabled=online!=null && online.Room.Length>0;
        }
        async Task ConnectOnline(bool join) {
            if(Check("EosEnabled").IsChecked==true){Status("Turn off in-game matchmaking before connecting to a WebRTC room.",true);return;}
            if(connectingOnline || online!=null)return;
            string room="";
            if(join) {
                try {
                    string invitationServer;
                    room=OnlineSession.ParseInvitation(Text("RoomCode").Text,out invitationServer);
                    if(invitationServer!=null)Text("OnlineServer").Text=invitationServer;
                } catch(Exception error) {Status(error.Message,true);Label("OnlineState").Text=error.Message;return;}
            }
            if(!Save())return;
            if(!caps.flags.ContainsKey("redbaron_p2p_namespace")) {Status("Update the game runtime before connecting online.",true);return;}
            connectingOnline=true;OnlineControls(false);Label("OnlineState").Text="Connecting to room server…";
            var session=new OnlineSession(message=>window.Dispatcher.BeginInvoke(new Action(()=>Label("OnlineState").Text=message)));
            try {
                await session.Start(settings,room);
                online=session;Text("RoomCode").Text=online.Room;
                Status("Online room connected. Follow the host instructions in the Online tab.");
            } catch(Exception error) {session.Dispose();Label("OnlineState").Text=error.Message;Status(error.Message,true);}
            finally {connectingOnline=false;OnlineControls(online==null);}
        }
        async Task LeaveOnline() {
            if(onlineGame || Services.IsGameRunning(settings)) {Status("Close the game before leaving the online room.",true);return;}
            var previous=online;online=null;
            if(previous!=null)await Task.Run(()=>previous.Dispose());
            OnlineControls(true);Label("OnlineState").Text="No room connected.";Status("Left the online room.");
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
                var effects=new[]{new Choice("Default · bilinear","bilinear"),new Choice("AMD FSR 1 · spatial","fsr",caps.Supports("present_effect","fsr")),new Choice("AMD FSR 3.1.5 · experimental","fsr3",caps.Supports("present_effect","fsr3")),new Choice("AMD CAS · sharpen","cas",caps.Supports("present_effect","cas")),new Choice("NVIDIA DLSS · experimental","dlss",caps.Supports("present_effect","dlss") && caps.DlssAvailable(settings.Adapter))};Choices("Upscaling",effects,settings.Upscaling);
                Choices("FrameRate",Settings.FrameRates.Select(fps=>new Choice(fps==0?"Game default":fps+" FPS"+(fps>120?" · experimental":""),fps,fps==0||caps.Supports("redbaron_fps",fps.ToString()))),settings.Fps);
                Check("Vsync").IsEnabled=caps.flags.ContainsKey("present_vsync");
                Label("GpuSummary").Text=caps.gpus.Count>0?String.Join(" · ",caps.gpus.Select(g=>g.name)):"No physical GPU detected.";
                Check("ShowFps").IsEnabled=caps.flags.ContainsKey("redbaron_show_fps");
                Check("Hdr").IsEnabled=caps.flags.ContainsKey("present_hdr");
                Check("LanEnabled").IsEnabled=caps.flags.ContainsKey("redbaron_lan");
                Check("EosEnabled").IsEnabled=caps.eosAvailable;
                if(!caps.eosAvailable)Label("EosState").Text="Update the game build to enable in-game EOS matchmaking.";
                Label("HdrInfo").Text=(caps.hdrActive?"Windows HDR is active on a connected display.":"Windows HDR is off or unavailable. SDR will be used until HDR is active on the game's display.")+" The FPS counter reports the actual HDR10 / SDR output. HDR10 maps the original SDR image to 200-nit white; it does not add HDR lighting.";
                Label("GpuSummary").Text += caps.refreshHz>0?" · Connected displays: up to "+caps.refreshHz+" Hz":"";
                Label("UpscalingInfo").Text="FSR 3.1.5 and DLSS are temporal previews. Moving objects and HUD may leave trails; use FSR 1 or bilinear for stable image quality. No frame generation.";
                ShowPlayer();UpdateDlssControls();loading=previous;
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
        void UpdateInstallAction() {
            string destination=Text("DestinationPath").Text.Trim();
            Button("InstallButton").Content=Services.HasInstallation(settings.Root) || Services.HasInstallation(destination)?"Install DLCs":"Install";
        }
        void ApplyInstallProgress(InstallProgress progress) {
            Get<StackPanel>("InstallProgressPanel").Visibility=Visibility.Visible;
            Get<ProgressBar>("InstallProgress").IsIndeterminate=!progress.Known;
            Get<ProgressBar>("InstallProgress").Value=progress.Percent;
            Label("InstallProgressText").Text=progress.ToString();
        }
        async Task Install() {
            if(installation!=null)return;
            settings.Source=Text("SourcePath").Text.Trim();settings.DlcSource=Text("DlcSource").Text.Trim();
            settings.Destination=Text("DestinationPath").Text.Trim();settings.Sdk=Text("SdkPath").Text.Trim();
            settings.DesktopShortcut=Check("DesktopShortcut").IsChecked==true;
            InstallPlan plan;
            try {plan=Services.PlanInstallation(settings);}catch(Exception error){Status(error.Message,true);return;}
            installation=new CancellationTokenSource();
            string[] controls={"InstallButton","CheckTools","BrowseDlc","UseExisting","Save","Play","RefreshDevices","BrowseSource","BrowseSourceFile","BrowseDestination","BrowseSdk"};
            foreach(string name in controls)Button(name).IsEnabled=false;
            foreach(string name in new[]{"SourcePath","DlcSource","DestinationPath","SdkPath"})Text(name).IsEnabled=false;
            Get<StackPanel>("InstallProgressPanel").Visibility=Visibility.Visible;
            Get<ProgressBar>("InstallProgress").Value=0;Get<ProgressBar>("InstallProgress").IsIndeterminate=true;
            Label("InstallProgressText").Text="Preparing installation…";Button("CancelInstall").Visibility=Visibility.Visible;
            Text("InstallLog").Visibility=Visibility.Visible;Text("InstallLog").Clear();
            string outcome="";bool failed=false;
            Action<string> log=line=>window.Dispatcher.BeginInvoke(new Action(()=>{
                InstallProgress progress;
                if(InstallProgress.TryParse(line,out progress)) {
                    ApplyInstallProgress(progress);
                }else {Text("InstallLog").AppendText(line+Environment.NewLine);Text("InstallLog").ScrollToEnd();Label("InstallState").Text=line;}
            }));
            Status(plan.InstallGame?"Installing game and selected content...":"Adding DLCs to the installed game...");
            try {
                settings=await Services.InstallContent(settings,log,installation.Token);
                dirty=false;outcome=plan.InstallGame?"Installation complete. Click Play to start the game.":"DLC installation complete. Existing game files and saves were preserved.";
            } catch(OperationCanceledException){failed=true;outcome="Installation cancelled. Original files were preserved. Any completed game installation remains available.";}
            catch(Exception error){failed=true;outcome=error.Message;}
            finally {
                installation.Dispose();installation=null;
                Get<ProgressBar>("InstallProgress").IsIndeterminate=false;
                if(!failed)Get<ProgressBar>("InstallProgress").Value=100;
                Label("InstallProgressText").Text=failed?"Installation stopped. See the log for details.":"Installation complete — 100%";
                Button("CancelInstall").Visibility=Visibility.Collapsed;
                foreach(string name in controls)Button(name).IsEnabled=true;
                foreach(string name in new[]{"SourcePath","DlcSource","DestinationPath","SdkPath"})Text(name).IsEnabled=true;
            }
            if(failed && Services.HasInstallation(plan.Root))settings=Settings.Load(plan.Root);
            ApplySettings();await Refresh(false);await CheckBuildTools();Status(outcome,failed);
        }
        async Task CheckForUpdates() {
            if(updateBusy)return;
            updateBusy=true;Button("CheckUpdates").IsEnabled=false;Button("DownloadUpdate").IsEnabled=false;
            Label("UpdateState").Text="Checking GitHub for a public release…";
            try {
                availableUpdate=await Updates.Check();
                if(availableUpdate==null) {
                    Label("UpdateState").Text="No newer public release is available.";
                    Label("UpdateBadge").Visibility=Visibility.Collapsed;
                } else {
                    Label("UpdateState").Text="Available: "+availableUpdate.name+" ("+(availableUpdate.size/(1024*1024))+" MB).";
                    Label("UpdateBadge").Visibility=Visibility.Visible;Button("DownloadUpdate").IsEnabled=true;
                }
            } catch(Exception error) {availableUpdate=null;Label("UpdateState").Text=error.Message;Label("UpdateBadge").Visibility=Visibility.Collapsed;}
            finally {updateBusy=false;Button("CheckUpdates").IsEnabled=true;}
        }
        async Task DownloadUpdate() {
            if(updateBusy || availableUpdate==null)return;
            updateBusy=true;downloadingUpdate=true;Button("CheckUpdates").IsEnabled=false;Button("DownloadUpdate").IsEnabled=false;
            Label("UpdateState").Text="Downloading and verifying the update…";
            try {
                string file=await Updates.Download(availableUpdate,settings.Root);
                updateFolder=Path.GetDirectoryName(file);Button("OpenUpdates").IsEnabled=true;
                Label("UpdateState").Text="Verified package downloaded: "+file;
            } catch(Exception error){Label("UpdateState").Text=error.Message;}
            finally {updateBusy=false;downloadingUpdate=false;Button("CheckUpdates").IsEnabled=true;Button("DownloadUpdate").IsEnabled=availableUpdate!=null;}
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
            int logOption=Array.IndexOf(args,"--progress-log");
            if(logOption>=0 && logOption+1<args.Length) {
                foreach(var record in File.ReadAllLines(args[logOption+1])) {
                    InstallProgress measured;if(InstallProgress.TryParse(record,out measured))ApplyInstallProgress(measured);
                }
            }
            int index=Array.IndexOf(args,"--capture");
            if(index+1>=args.Length) {window.Close();return;}
            string folder=Path.GetFullPath(args[index+1]);Directory.CreateDirectory(folder);
            for(int tab=0;tab<5;tab++) {
                Get<TabControl>("Tabs").SelectedIndex=tab;await Task.Delay(200);window.UpdateLayout();
                SaveCapture(Path.Combine(folder,new[]{"install","video","controls","online","update"}[tab]+".png"));
                if(tab==0 && args.Contains("--capture-tools")) {
                    var panel=Get<Expander>("BuildToolsPanel");panel.IsExpanded=true;
                    var grid=(Grid)((TabItem)Get<TabControl>("Tabs").Items[0]).Content;
                    var scroll=grid.Children.OfType<ScrollViewer>().First();scroll.ScrollToEnd();
                    await Task.Delay(100);window.UpdateLayout();SaveCapture(Path.Combine(folder,"install_tools.png"));
                    panel.IsExpanded=false;scroll.ScrollToHome();
                }
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
