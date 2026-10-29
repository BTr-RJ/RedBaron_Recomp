#include <rex/ui/window.h>
#ifdef _WIN32
#include <Windows.h>
#endif

namespace redbaron {
void ApplyWindowBranding(rex::ui::Window* window) {
  if (!window) return;
  window->SetTitle("RedBaron_Recomp");
#ifdef _WIN32
  // RCDATA preserves the complete ICO; the normal ICON resource brands Explorer.
  const auto module = GetModuleHandleW(nullptr);
  if (const auto resource = FindResourceW(module, MAKEINTRESOURCEW(2), MAKEINTRESOURCEW(10))) {
    if (const auto data = LoadResource(module, resource)) {
      window->SetIcon(LockResource(data), SizeofResource(module, resource));
    }
  }
#endif
}
}
