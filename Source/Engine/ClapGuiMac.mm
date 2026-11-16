// The Mac's home for a CLAP plug-in's window: an NSView inside the lower zone.

#include "ClapFormat.h"

#include <clap/ext/gui.h>
#include <juce_gui_extra/juce_gui_extra.h>

#import <AppKit/AppKit.h>

namespace kal::clap
{
namespace
{
class MacHost : public NativeHost
{
public:
    MacHost()
    {
        view = [[NSView alloc] initWithFrame: NSMakeRect (0, 0, 400, 300)];
        [view setAutoresizesSubviews: YES];
        component_.setView (view);
    }

    ~MacHost() override
    {
        component_.setView (nullptr);
        [view release];
    }

    juce::Component& component() override { return component_; }
    void* handle() override { return (void*) view; }
    const char* api() const override { return CLAP_WINDOW_API_COCOA; }

private:
    juce::NSViewComponent component_;
    NSView* view = nil;
};
} // namespace

std::unique_ptr<NativeHost> makeNativeHost() { return std::make_unique<MacHost>(); }

} // namespace kal::clap
