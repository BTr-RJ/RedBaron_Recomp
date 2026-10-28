using System.Windows.Input;

namespace RedBaron {
    static class KeyboardNames {
        public static string FromKey(Key key,string fallback) {
            switch(key) {
                case Key.OemComma: return "Comma";
                case Key.OemPeriod: return "Period";
                case Key.OemMinus: return "Minus";
                case Key.OemPlus: return "Plus";
                case Key.Oem1: return "Semicolon";
                case Key.Oem2: return "Slash";
                case Key.Oem3: return "Backtick";
                case Key.Oem4: return "LBracket";
                case Key.Oem5: return "Backslash";
                case Key.Oem6: return "RBracket";
                case Key.Oem7: return "Quote";
                default: return fallback;
            }
        }
    }
}
