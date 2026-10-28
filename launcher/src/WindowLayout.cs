using System;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Interop;
using System.Windows.Media;

namespace RedBaron {
    // Window placement uses the current monitor's working area and WPF DPI units.
    sealed class WindowLayout {
        readonly Window window;
        readonly Button toggle;
        Rect restore;
        WindowState restoreState;
        public bool Fullscreen {get;private set;}
        public WindowLayout(Window window,Button toggle) {this.window=window;this.toggle=toggle;}
        Rect MonitorArea(bool working) {
            var monitor=System.Windows.Forms.Screen.FromHandle(new WindowInteropHelper(window).Handle);
            var pixels=working?monitor.WorkingArea:monitor.Bounds;
            var source=PresentationSource.FromVisual(window);
            var transform=source!=null && source.CompositionTarget!=null?source.CompositionTarget.TransformFromDevice:Matrix.Identity;
            var topLeft=transform.Transform(new Point(pixels.Left,pixels.Top));
            var bottomRight=transform.Transform(new Point(pixels.Right,pixels.Bottom));
            return new Rect(topLeft,bottomRight);
        }
        public void Center() {
            var area=MonitorArea(true);
            double width=Math.Min(window.Width,Math.Max(320,area.Width-32));
            double height=Math.Min(window.Height,Math.Max(240,area.Height-32));
            window.MinWidth=Math.Min(window.MinWidth,width);
            window.MinHeight=Math.Min(window.MinHeight,height);
            window.Width=width;window.Height=height;
            window.WindowStartupLocation=WindowStartupLocation.Manual;
            window.Left=area.Left+(area.Width-width)/2;
            window.Top=area.Top+(area.Height-height)/2;
        }
        public void Toggle() {
            if(!Fullscreen) {
                restoreState=window.WindowState;
                restore=window.WindowState==WindowState.Normal?new Rect(window.Left,window.Top,window.Width,window.Height):window.RestoreBounds;
                var area=MonitorArea(false);
                window.WindowState=WindowState.Normal;window.WindowStyle=WindowStyle.None;window.ResizeMode=ResizeMode.NoResize;
                window.Left=area.Left;window.Top=area.Top;window.Width=area.Width;window.Height=area.Height;
                Fullscreen=true;toggle.Content="Restore";toggle.ToolTip="Exit fullscreen (F11 or Esc)";
            } else {
                window.WindowStyle=WindowStyle.SingleBorderWindow;window.ResizeMode=ResizeMode.CanResize;
                window.Left=restore.Left;window.Top=restore.Top;window.Width=restore.Width;window.Height=restore.Height;
                window.WindowState=restoreState;
                Fullscreen=false;toggle.Content="Fullscreen";toggle.ToolTip="Fullscreen (F11)";
            }
        }
    }
}
