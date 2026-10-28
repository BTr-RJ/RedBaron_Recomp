using System;
using System.Collections.Generic;
using System.IO;
using System.Globalization;
using System.Linq;
using System.Text;
using System.Text.RegularExpressions;
using System.Web.Script.Serialization;

namespace RedBaron {
    public sealed class Binding {
        public string Action { get; set; }
        public string Key { get; set; }
        public string Keyboard { get; set; }
        public int Pad { get; set; }
    }
    public sealed class Player {
        public string Device { get; set; }
        public List<Binding> Bindings { get; set; }
        public Player() { Device = "auto"; Bindings = Actions.Defaults(); }
    }
    public sealed class Settings {
        public int Version { get; set; }
        public string Root { get; set; }
        public string Source { get; set; }
        public string Destination { get; set; }
        public string Sdk { get; set; }
        public bool ShowLauncher { get; set; }
        public bool DesktopShortcut { get; set; }
        public string PlayerName { get; set; }
        public string Resolution { get; set; }
        public bool Fullscreen { get; set; }
        public bool Vsync { get; set; }
        public int Anisotropic { get; set; }
        public int Adapter { get; set; }
        public int Scale { get; set; }
        public int Fps { get; set; }
        public int FsrSharpness { get; set; }
        public string Antialiasing { get; set; }
        public string Upscaling { get; set; }
        public int KeyboardPlayer { get; set; }
        public List<Player> Players { get; set; }
        public Settings() {
            Version = 1; ShowLauncher = true; PlayerName = "Player";
            Resolution = "1280x720"; Fullscreen = true; Vsync = true;
            Anisotropic = 3; Adapter = -1; Scale = 1;
            FsrSharpness = 90;
            Antialiasing = "none"; Upscaling = "bilinear"; KeyboardPlayer = 0;
            Players = Enumerable.Range(0, 4).Select(i => new Player()).ToList();
        }
        public string Build { get { return Path.Combine(Root, "build_cpp_release"); } }
        public string Exe { get { return Path.Combine(Build, "snoopyflyingace.exe"); } }
        public string Toml { get { return Path.Combine(Build, "snoopyflyingace.toml"); } }
        public string FileName { get { return Path.Combine(Root, "launcher-settings.json"); } }
        public static Settings Load(string root) {
            string file = Path.Combine(root, "launcher-settings.json");
            Settings s = File.Exists(file) ? Json.Read<Settings>(file) : new Settings();
            if (s.Version != 1 || s.Players == null || s.Players.Count != 4)
                throw new InvalidDataException("Incompatible launcher settings. Keep the file and restore a backup.");
            // Display labels come from this version, not from an older saved language.
            // Keep stable binding keys, keyboard choices and controller mappings intact.
            foreach (var player in s.Players) {
                if (player == null || player.Bindings == null) throw new InvalidDataException("The binding list is incomplete.");
                foreach (var binding in player.Bindings) {
                    if (binding == null) throw new InvalidDataException("The binding list is incomplete.");
                    int action = Array.IndexOf(Actions.Keys, binding.Key);
                    if (action >= 0) binding.Action = Actions.Labels[action];
                }
            }
            s.Root = Path.GetFullPath(root);
            s.Source = s.Source ?? Path.Combine(root, "game");
            s.Sdk = s.Sdk ?? Path.Combine(root, "sdk_experimental");
            s.Destination = s.Destination ?? "";
            // Runtime settings are authoritative, including edits made outside the launcher.
            if (File.Exists(s.Toml)) {
                var t = new Toml(File.ReadAllText(s.Toml));
                s.Resolution = t.String("resolution", s.Resolution);
                if (String.IsNullOrEmpty(s.Resolution)) s.Resolution = "1280x720";
                s.Fullscreen = t.Bool("fullscreen", s.Fullscreen);
                s.Vsync = t.Bool("present_vsync", s.Vsync);
                s.Anisotropic = t.Int("anisotropic_override", s.Anisotropic);
                s.Adapter = t.Int("d3d12_adapter", s.Adapter);
                s.Scale = t.Int("resolution_scale", s.Scale);
                s.Fps = t.Int("redbaron_fps", s.Fps);
                s.FsrSharpness = (int)Math.Round(100 - 50 * t.Number("present_fsr_sharpness_reduction", (100-s.FsrSharpness)/50.0));
                s.Antialiasing = t.String("swap_post_effect", s.Antialiasing);
                s.Upscaling = t.String("present_effect", s.Upscaling);
                s.KeyboardPlayer = t.Bool("mnk_mode", true) ? t.Int("redbaron_keyboard_player", 0) : -1;
                int keyboardIndex = Math.Max(0, s.KeyboardPlayer - 1);
                foreach (var b in s.Players[keyboardIndex].Bindings) b.Keyboard = t.String(b.Key, b.Keyboard);
                var slots = t.String("redbaron_pad_slots", "auto;auto;auto;auto").Split(';');
                if (slots.Length != 4) throw new InvalidDataException("redbaron_pad_slots must contain four entries.");
                for (int i = 0; i < 4; i++) {
                    s.Players[i].Device = slots[i];
                    string raw = t.String("redbaron_pad_map_p" + (i + 1), "");
                    if (raw.Length == 0) continue;
                    var values = raw.Split(',');
                    if (values.Length != Actions.Keys.Length) throw new InvalidDataException("Invalid controller mapping in the TOML file.");
                    for (int b = 0; b < values.Length; b++) s.Players[i].Bindings[b].Pad = Int32.Parse(values[b]);
                }
            }
            return s;
        }
        public void Validate() {
            if (!Regex.IsMatch(PlayerName ?? "", @"^[A-Za-z0-9](?:[A-Za-z0-9 _-]{0,13}[A-Za-z0-9_-])?$"))
                throw new InvalidDataException("Name: use 1–15 characters; letters A–Z, numbers, internal spaces, hyphens or underscores.");
            if (!Resolutions.Contains(Resolution))
                throw new InvalidDataException("Choose a resolution from the list.");
            if (KeyboardPlayer < -1 || KeyboardPlayer > 4 || Anisotropic < -1 || Anisotropic > 5 || Scale < 1 || Scale > 6 || FsrSharpness < 0 || FsrSharpness > 100 || !FrameRates.Contains(Fps))
                throw new InvalidDataException("A graphics or input setting is outside the allowed range.");
            var devices = new HashSet<string>();
            for (int p = 0; p < 4; p++) {
                var player = Players[p];
                if (player.Device != "auto" && player.Device != "none") {
                    if (!Regex.IsMatch(player.Device ?? "", @"^[a-f0-9]{32}#[0-9]+$")) throw new InvalidDataException("Invalid controller.");
                    if (!devices.Add(player.Device)) throw new InvalidDataException("The same controller is assigned to more than one player.");
                }
                if (KeyboardPlayer == p + 1 && player.Device != "none" && player.Device != "auto")
                    throw new InvalidDataException("A player reserved for the keyboard cannot use a controller. Select No controller or Automatic.");
                if (player.Bindings == null || player.Bindings.Count != Actions.Keys.Length)
                    throw new InvalidDataException("The binding list is incomplete.");
                for (int b = 0; b < player.Bindings.Count; b++) {
                    var binding = player.Bindings[b];
                    if (binding.Key != Actions.Keys[b] || binding.Pad < -1 || binding.Pad >= Actions.Keys.Length)
                        throw new InvalidDataException("Invalid mapping for player " + (p + 1));
                    Actions.ValidateKeyboard(binding.Keyboard);
                }
            }
        }
        public string RuntimeText(string original) {
            Validate();
            var t = new Toml(original);
            var values = new Dictionary<string,string> {
                {"resolution",Json.Quote(Resolution)}, {"fullscreen", Bool(Fullscreen)},
                // The guest vblank timer and host monitor synchronization are independent.
                {"vsync", "true"}, {"present_vsync", Bool(Vsync)}, {"anisotropic_override", Anisotropic.ToString()},
                {"redbaron_fps", Fps.ToString()}, {"video_mode_refresh_rate", Math.Max(60,Fps).ToString()},
                {"resolution_scale", Scale.ToString()}, {"draw_resolution_scale_x",Scale.ToString()}, {"draw_resolution_scale_y",Scale.ToString()},
                {"window_width", Resolution.Split('x')[0]}, {"window_height", Resolution.Split('x')[1]},
                {"window_size_in_physical_pixels", "true"},
                // Keep the title's original 16:9 layout; scale its render targets separately.
                {"video_mode_width","1280"}, {"video_mode_height","720"},
                {"present_fsr_sharpness_reduction", ((100-FsrSharpness)/50.0).ToString("0.00",CultureInfo.InvariantCulture)},
                {"d3d12_adapter",Adapter.ToString()},
                {"swap_post_effect",Json.Quote(Antialiasing)}, {"present_effect",Json.Quote(Upscaling)},
                {"render_target_path_d3d12","\"rov\""}, {"input_backend","\"sdl\""},
                {"mnk_mode", Bool(KeyboardPlayer >= 0)}, {"mnk_mouse","false"},
                {"redbaron_keyboard_player",Math.Max(0,KeyboardPlayer).ToString()},
                {"redbaron_pad_slots",Json.Quote(String.Join(";",Players.Select(p => p.Device))) }
            };
            for (int p = 0; p < 4; p++) values["redbaron_pad_map_p"+(p+1)] = Json.Quote(String.Join(",", Players[p].Bindings.Select(b=>b.Pad.ToString())));
            foreach (var b in Players[Math.Max(0,KeyboardPlayer-1)].Bindings) values[b.Key] = Json.Quote(b.Keyboard ?? "");
            return t.Patch(values);
        }
        static string Bool(bool value) { return value ? "true" : "false"; }
        public static readonly string[] Resolutions = {"1280x720","1600x900","1920x1080","2560x1440","3440x1440","3840x2160","5120x1440","5120x2880","7680x4320"};
        public static readonly int[] FrameRates = {0,30,60,90,120,144,165,240};
    }

    public static class Actions {
        // Order is shared with port/src/pad_mapping.h. Xbox output semantics.
        public static readonly string[] Keys = {"keybind_a","keybind_b","keybind_x","keybind_y","keybind_left_shoulder","keybind_right_shoulder","keybind_back","keybind_start","keybind_lstick_press","keybind_rstick_press","keybind_dpad_up","keybind_dpad_down","keybind_dpad_left","keybind_dpad_right","keybind_left_trigger","keybind_right_trigger","keybind_lstick_up","keybind_lstick_down","keybind_lstick_left","keybind_lstick_right","keybind_rstick_up","keybind_rstick_down","keybind_rstick_left","keybind_rstick_right"};
        public static readonly string[] Labels = {"Boost / confirm","Back / switch weapon","Brake / option","Action / exit aircraft","Switch weapon","Look back","Statistics","Pause / start","Left stick click","Cancel zoom","D-pad ↑","D-pad ↓","D-pad ←","D-pad →","Secondary weapon","Primary weapon","Flight ↑","Flight ↓","Flight ←","Flight →","Stunt ↑","Stunt ↓","Stunt ←","Stunt →"};
        public static readonly string[] PadNames = {"A / bottom button","B / right button","X / left button","Y / top button","LB / left shoulder","RB / right shoulder","View / Back","Menu / Start","LS / left stick click","RS / right stick click","D-pad ↑","D-pad ↓","D-pad ←","D-pad →","LT / left trigger","RT / right trigger","Left stick ↑","Left stick ↓","Left stick ←","Left stick →","Right stick ↑","Right stick ↓","Right stick ←","Right stick →"};
        static readonly string[] DefaultKeys = {"Space","Backspace","C","E","Q,MMB","R","Tab","Return,Escape","F","V","Numpad8","Numpad2","Numpad4","Numpad6","RMB,K","LMB,J","W","S","A","D","Up","Down","Left","Right"};
        public static List<Binding> Defaults() { return Keys.Select((key,i)=>new Binding{Key=key,Action=Labels[i],Keyboard=DefaultKeys[i],Pad=i}).ToList(); }
        public static void ValidateKeyboard(string value) {
            if (String.IsNullOrEmpty(value)) return;
            foreach (string key in value.Split(',')) {
                if (!Regex.IsMatch(key.Trim(), @"^(?:(?:Shift|Ctrl|Alt)\+)*(?:[A-Z0-9]|F(?:[1-9]|1[0-9]|2[0-4])|Numpad[0-9]|Space|Return|Escape|Tab|Backspace|Up|Down|Left|Right|LMB|RMB|MMB|Insert|Delete|Home|End|PageUp|PageDown|Semicolon|Quote|Comma|Period|Slash|Backslash|Minus|Plus|LBracket|RBracket|Backtick)$"))
                    throw new InvalidDataException("Unrecognized key: " + key + ". Use Capture or key names such as W, Space and LMB.");
            }
        }
    }

    public static class Json {
        public static readonly JavaScriptSerializer Serializer = new JavaScriptSerializer { MaxJsonLength = 8 * 1024 * 1024 };
        public static T Read<T>(string path) { return Serializer.Deserialize<T>(File.ReadAllText(path, Encoding.UTF8)); }
        public static string Quote(string value) { return Serializer.Serialize(value); }
        public static void Write(string path, object value) { AtomicFile.Write(path, Serializer.Serialize(value)); }
    }
    public static class AtomicFile {
        public static void Write(string path, string text) {
            Directory.CreateDirectory(Path.GetDirectoryName(Path.GetFullPath(path)));
            string temp = path + ".tmp-" + Guid.NewGuid().ToString("N");
            try {
                File.WriteAllText(temp, text, new UTF8Encoding(false));
                if (File.Exists(path)) File.Replace(temp, path, path + ".backup-" + DateTime.Now.ToString("yyyyMMdd_HHmmss_fff"));
                else File.Move(temp, path);
            } finally { if (File.Exists(temp)) File.Delete(temp); }
        }
    }
    public sealed class Toml {
        readonly string original;
        readonly Dictionary<string,string> root = new Dictionary<string,string>();
        static readonly Regex Entry = new Regex(@"^\s*([A-Za-z0-9_]+)\s*=\s*(.*)$");
        public Toml(string text) {
            original = text;
            foreach (string line in text.Replace("\r\n","\n").Split('\n')) {
                if (line.TrimStart().StartsWith("[")) break;
                var match = Entry.Match(line);
                if (!match.Success) continue;
                string key = match.Groups[1].Value;
                if (root.ContainsKey(key)) throw new InvalidDataException("Duplicate TOML key: " + key);
                root[key] = match.Groups[2].Value.Trim();
            }
        }
        public string String(string key, string fallback) {
            string raw;
            if (!root.TryGetValue(key, out raw)) return fallback;
            var basic = Regex.Match(raw, "^\"((?:[^\"\\\\]|\\\\.)*)\"");
            if (basic.Success) return Json.Serializer.Deserialize<string>(basic.Value);
            var literal = Regex.Match(raw, "^'([^']*)'");
            if (literal.Success) return literal.Groups[1].Value;
            throw new InvalidDataException("Invalid TOML string for " + key);
        }
        public int Int(string key,int fallback) { string raw; return root.TryGetValue(key,out raw) ? Int32.Parse(raw.Split('#')[0].Trim()) : fallback; }
        public double Number(string key,double fallback) { string raw; return root.TryGetValue(key,out raw) ? Double.Parse(raw.Split('#')[0].Trim(),CultureInfo.InvariantCulture) : fallback; }
        public bool Bool(string key,bool fallback) { string raw; return root.TryGetValue(key,out raw) ? Boolean.Parse(raw.Split('#')[0].Trim()) : fallback; }
        public string Patch(Dictionary<string,string> values) {
            var pending = new Dictionary<string,string>(values);
            var lines = original.Replace("\r\n","\n").Split('\n');
            var output = new List<string>();
            bool inRoot = true;
            foreach (string line in lines) {
                if (inRoot && line.TrimStart().StartsWith("[")) {
                    foreach(var pair in pending) output.Add(pair.Key + " = " + pair.Value);
                    pending.Clear(); inRoot=false;
                }
                var match = Entry.Match(line);
                string value;
                if (inRoot && match.Success && pending.TryGetValue(match.Groups[1].Value,out value)) {
                    output.Add(match.Groups[1].Value + " = " + value);
                    pending.Remove(match.Groups[1].Value);
                } else output.Add(line);
            }
            foreach(var pair in pending) output.Add(pair.Key + " = " + pair.Value);
            return System.String.Join("\r\n",output).TrimEnd() + "\r\n";
        }
    }
}
