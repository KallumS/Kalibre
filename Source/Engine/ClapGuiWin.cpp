// A CLAP plug-in's own window on Windows (decision 0129): a child window of
// Kalibre's, held by a JUCE HWNDComponent, that the plug-in is given to draw
// in, as ClapGuiMac.mm gives it an NSView. Made once the component is on the
// screen, as a child window needs its parent.
#ifndef NOMINMAX
 #define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
 #define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include "ClapFormat.h"

#include <clap/clap.h>
#include <juce_gui_extra/juce_gui_extra.h>

namespace kal::clap
{
namespace
{
class WinHost : public NativeHost
{
public:
    ~WinHost() override
    {
        holder.setHWND (nullptr);
        if (hwnd != nullptr) DestroyWindow (hwnd);
    }

    juce::Component& component() override { return holder; }

    void* handle() override
    {
        if (hwnd == nullptr)
            if (auto* peer = holder.getPeer())
            {
                hwnd = CreateWindowExW (0, L"STATIC", L"", WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
                                        0, 0, juce::jmax (1, holder.getWidth()), juce::jmax (1, holder.getHeight()),
                                        (HWND) peer->getNativeHandle(), nullptr, GetModuleHandleW (nullptr), nullptr);
                if (hwnd != nullptr) holder.setHWND (hwnd);
            }
        return hwnd;
    }

    const char* api() const override { return CLAP_WINDOW_API_WIN32; }

private:
    juce::HWNDComponent holder;
    HWND hwnd = nullptr;
};
} // namespace

std::unique_ptr<NativeHost> makeNativeHost() { return std::make_unique<WinHost>(); }

} // namespace kal::clap
