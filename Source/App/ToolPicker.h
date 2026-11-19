/*
    ToolBox - Control-right-click over the arrangement or the piano roll
    (decision 0092): the tools for that area, at the pointer. Click one to
    use it; click the lit one again (or right-click any with ▾) for its
    modes. It closes once a tool is picked, or with Escape or a click
    elsewhere.

    It's a small window of its own, like a menu, so it shows above plug-ins'
    windows on the Mac (ARCHITECTURE: anything drawn over them must be).

    Also each tool's icon, and the pointer it gives the area.
*/

#pragma once

#include "Controller.h"
#include "Theme.h"
#include "Toolbox.h"

namespace kal
{

namespace toolicons
{
// Drawn in a 24-point square, in `colour`.
void draw (juce::Graphics&, tools::Tool, juce::Rectangle<float>, juce::Colour);
// The pointer for a tool; Select keeps the area's own.
juce::MouseCursor cursor (tools::Tool);
} // namespace toolicons

class ToolBox : public juce::Component
{
public:
    ToolBox (Controller&, tools::Area);

    // Opens it at a point on the screen.
    static void show (Controller&, tools::Area, juce::Point<int> screen);

    void paint (juce::Graphics&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    bool keyPressed (const juce::KeyPress&) override;
    void inputAttemptWhenModal() override { close(); }

    std::vector<tools::Tool> shown;   // this area's tools, in order
    int hovered = -1;
    juce::Rectangle<int> cellBounds (int index) const;
    int columns() const { return 4; }

private:
    Controller& c;
    tools::Area area;
    void pick (tools::Tool, std::optional<int> mode = std::nullopt);
    void modesMenu (tools::Tool);
    void close();
    int cellAt (juce::Point<int>) const;
    int helpHeight() const { return theme::px (44); }
};

} // namespace kal
