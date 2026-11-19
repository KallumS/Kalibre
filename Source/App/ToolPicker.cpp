#include "ToolPicker.h"

using namespace juce;

namespace kal
{

//==============================================================================
namespace toolicons
{
namespace
{
using tools::Tool;

// Each icon as lines to stroke and shapes to fill, in a 24 x 24 square.
struct Icon { Path stroke, fill; };

Icon iconFor (Tool t)
{
    Icon i;
    auto& s = i.stroke;
    auto& f = i.fill;
    auto poly = [] (Path& p, std::initializer_list<Point<float>> pts)
    {
        bool first = true;
        for (auto pt : pts) { if (first) p.startNewSubPath (pt); else p.lineTo (pt); first = false; }
        p.closeSubPath();
    };
    switch (t)
    {
        case Tool::Select:
            poly (f, { { 6, 2 }, { 6, 19 }, { 10, 15 }, { 13, 21.5f }, { 15.5f, 20.5f }, { 12.5f, 14 }, { 18, 14 } });
            break;
        case Tool::Range:
            s.startNewSubPath (4, 5);  s.lineTo (4, 19);
            s.startNewSubPath (20, 5); s.lineTo (20, 19);
            s.startNewSubPath (5, 12); s.lineTo (19, 12);
            s.startNewSubPath (8, 9);  s.lineTo (5, 12); s.lineTo (8, 15);
            s.startNewSubPath (16, 9); s.lineTo (19, 12); s.lineTo (16, 15);
            break;
        case Tool::Split:
            s.addEllipse (3.5f, 14.5f, 6, 6);
            s.addEllipse (14.5f, 14.5f, 6, 6);
            s.startNewSubPath (8.5f, 15); s.lineTo (17, 3);
            s.startNewSubPath (15.5f, 15); s.lineTo (7, 3);
            break;
        case Tool::Glue:
            f.addRoundedRectangle (2.5f, 9, 13, 8, 2);
            poly (f, { { 15, 10.5f }, { 20.5f, 12.5f }, { 20.5f, 13.5f }, { 15, 15.5f } });
            f.addEllipse (19.5f, 17, 3, 4);
            break;
        case Tool::Erase:
            poly (s, { { 3, 15 }, { 12, 6 }, { 19, 13 }, { 10, 22 } });
            s.startNewSubPath (7, 11); s.lineTo (14, 18);
            s.startNewSubPath (10, 22); s.lineTo (21, 22);
            break;
        case Tool::Zoom:
            s.addEllipse (3.5f, 3.5f, 12, 12);
            s.startNewSubPath (14, 14); s.lineTo (21, 21);
            s.startNewSubPath (6.5f, 9.5f); s.lineTo (12.5f, 9.5f);
            s.startNewSubPath (9.5f, 6.5f); s.lineTo (9.5f, 12.5f);
            break;
        case Tool::Mute:
            s.addRoundedRectangle (3, 3, 18, 18, 3);
            s.startNewSubPath (8, 8);  s.lineTo (16, 16);
            s.startNewSubPath (16, 8); s.lineTo (8, 16);
            break;
        case Tool::Draw:
            poly (s, { { 3.5f, 20.5f }, { 4.5f, 15.5f }, { 16, 4 }, { 20, 8 }, { 8.5f, 19.5f } });
            s.startNewSubPath (13.5f, 6.5f); s.lineTo (17.5f, 10.5f);
            break;
        case Tool::Line:
            s.startNewSubPath (5, 19); s.lineTo (19, 5);
            f.addEllipse (2.5f, 16.5f, 5, 5);
            f.addEllipse (16.5f, 2.5f, 5, 5);
            break;
        case Tool::Play:
            poly (f, { { 7, 4 }, { 19.5f, 12 }, { 7, 20 } });
            break;
        case Tool::Hand:
            f.addRoundedRectangle (5.5f, 10.5f, 13, 11, 4);
            f.addRoundedRectangle (5.5f, 6, 2.8f, 9, 1.4f);
            f.addRoundedRectangle (9, 3, 2.8f, 12, 1.4f);
            f.addRoundedRectangle (12.5f, 3.5f, 2.8f, 11, 1.4f);
            f.addRoundedRectangle (16, 6, 2.8f, 9, 1.4f);
            s.startNewSubPath (6.5f, 16); s.lineTo (3, 12);
            break;
        case Tool::Drumstick:
            s.startNewSubPath (4, 20); s.lineTo (16.5f, 7.5f);
            f.addEllipse (15, 2.5f, 6, 6);
            s.startNewSubPath (3, 22); s.lineTo (11, 22);
            break;
        case Tool::TimeWarp:
            poly (s, { { 8.5f, 3 }, { 15.5f, 3 }, { 19.5f, 21 }, { 4.5f, 21 } });
            s.startNewSubPath (12, 17); s.lineTo (16.5f, 6);
            f.addEllipse (13, 9, 4, 4);
            break;
    }
    return i;
}

void drawIcon (Graphics& g, const Icon& icon, Rectangle<float> r, Colour colour, float width)
{
    const auto fit = AffineTransform::scale (r.getWidth() / 24.0f, r.getHeight() / 24.0f).translated (r.getX(), r.getY());
    g.setColour (colour);
    g.fillPath (icon.fill, fit);
    g.strokePath (icon.stroke, PathStrokeType (width * r.getWidth() / 24.0f, PathStrokeType::curved, PathStrokeType::rounded), fit);
}
} // namespace

void draw (Graphics& g, tools::Tool t, Rectangle<float> r, Colour colour)
{
    drawIcon (g, iconFor (t), r, colour, 1.7f);
}

MouseCursor cursor (tools::Tool t)
{
    if (t == Tool::Select) return MouseCursor::NormalCursor;
    if (t == Tool::Hand) return MouseCursor::DraggingHandCursor;
    if (t == Tool::Range) return MouseCursor::IBeamCursor;
    // Ink with a light edge, so it shows on the dark ground and on clips; drawn twice the size for sharp screens.
    static std::map<int, MouseCursor> made;
    if (auto it = made.find ((int) t); it != made.end()) return it->second;
    constexpr int size = 48;
    Image im (Image::ARGB, size, size, true);
    {
        Graphics g (im);
        const auto icon = iconFor (t);
        const auto r = Rectangle<float> (0, 0, (float) size, (float) size).reduced (3);
        drawIcon (g, icon, r, theme::playhead, 4.2f);
        // The fills' light edge, then the ink on top.
        const auto fit = AffineTransform::scale (r.getWidth() / 24.0f).translated (r.getX(), r.getY());
        g.setColour (theme::playhead);
        g.strokePath (icon.fill, PathStrokeType (2.5f), fit);
        drawIcon (g, icon, r, theme::ink, 1.7f);
    }
    // Where it points: a pencil's tip, the lens's middle, the cut.
    Point<float> hot { 12, 12 };
    if (t == Tool::Draw || t == Tool::Line || t == Tool::Drumstick) hot = { 4, 20 };
    else if (t == Tool::Erase) hot = { 5, 17 };
    else if (t == Tool::Zoom) hot = { 9.5f, 9.5f };
    else if (t == Tool::Split) hot = { 12, 9 };
    else if (t == Tool::Glue) hot = { 21, 13 };
    else if (t == Tool::Play) hot = { 7, 4 };
    const float k = (float) (size - 6) / 24.0f;
    const MouseCursor mc (ScaledImage (im, 2.0), Point<int> (roundToInt ((3 + hot.x * k) / 2.0f), roundToInt ((3 + hot.y * k) / 2.0f)));
    made.emplace ((int) t, mc);
    return mc;
}
} // namespace toolicons

//==============================================================================
ToolBox::ToolBox (Controller& ctl, tools::Area a) : c (ctl), area (a)
{
    for (auto& i : tools::all())
        if (tools::availableIn (i.tool, area)) shown.push_back (i.tool);
    const int rows = ((int) shown.size() + columns() - 1) / columns();
    setSize (theme::px (8) * 2 + columns() * theme::px (76), theme::px (8) + rows * theme::px (58) + helpHeight());
    setWantsKeyboardFocus (true);
    setTitle ("Tools");
}

void ToolBox::show (Controller& c, tools::Area area, Point<int> screen)
{
    auto* box = new ToolBox (c, area);
    // Its top left a little up and left of the pointer, inside the screen.
    auto r = box->getBounds().withPosition (screen.translated (-theme::px (24), -theme::px (20)));
    if (auto* d = Desktop::getInstance().getDisplays().getDisplayForPoint (screen.toFloat()))
        r = r.constrainedWithin (d->userBounds.toNearestInt());
    box->setBounds (r);
    box->addToDesktop (ComponentPeer::windowIsTemporary | ComponentPeer::windowHasDropShadow);
    box->setVisible (true);
    box->enterModalState (true, nullptr, true);
    box->grabKeyboardFocus();
}

Rectangle<int> ToolBox::cellBounds (int i) const
{
    const int w = theme::px (76), h = theme::px (58);
    return { theme::px (8) + (i % columns()) * w, theme::px (8) + (i / columns()) * h, w, h };
}

int ToolBox::cellAt (Point<int> p) const
{
    for (int i = 0; i < (int) shown.size(); ++i)
        if (cellBounds (i).contains (p)) return i;
    return -1;
}

void ToolBox::paint (Graphics& g)
{
    const auto& state = area == tools::Area::Arrange ? c.arrangeTool : c.rollTool;
    g.fillAll (theme::raised);
    g.setColour (theme::rule);
    g.drawRect (getLocalBounds(), 1);
    for (int i = 0; i < (int) shown.size(); ++i)
    {
        const auto tool = shown[(size_t) i];
        const auto& info = tools::info (tool);
        const auto cell = cellBounds (i).reduced (theme::px (2)).toFloat();
        const bool on = state.tool == tool;
        // The tool in use is lit; the one under the pointer raised.
        if (on) { g.setColour (theme::accent); g.fillRoundedRectangle (cell, theme::cornerRadius); }
        else if (i == hovered) { g.setColour (theme::rule); g.fillRoundedRectangle (cell, theme::cornerRadius); }
        const auto ink = on ? theme::ink : theme::text;
        const float icon = (float) theme::px (22);
        toolicons::draw (g, tool, Rectangle<float> (cell.getCentreX() - icon / 2, cell.getY() + (float) theme::px (6), icon, icon), ink);
        String name (info.name);
        if (! info.modes.empty()) name << ui::u8 (" \xe2\x96\xbe");
        g.setColour (ink);
        g.setFont (theme::font (theme::smallPoints));
        g.drawText (name, cell.withTop (cell.getY() + (float) theme::px (32)).toNearestInt(), Justification::centredTop, true);
    }
    // What the tool under the pointer does (or the one in use), and its mode.
    const auto tool = hovered >= 0 ? shown[(size_t) hovered] : state.tool;
    const auto& info = tools::info (tool);
    String help (info.help);
    if (! info.modes.empty())
    {
        const auto& mode = info.modes[(size_t) state.modeOf (tool)];
        help = String (mode.name) + ": " + mode.help;
        if (tool == state.tool) help << " Click it again for its other modes.";
        else if (hovered >= 0) help = String (info.help) + " Right-click for its modes.";
    }
    auto text = getLocalBounds().removeFromBottom (helpHeight()).reduced (theme::px (10), theme::px (4));
    g.setColour (theme::rule);
    g.fillRect (text.getX(), text.getY() - theme::px (2), text.getWidth(), 1);
    g.setColour (theme::textOnRaised);
    g.setFont (theme::font (theme::smallPoints));
    g.drawFittedText (help, text, Justification::centredLeft, 2, 1.0f);
}

void ToolBox::mouseMove (const MouseEvent& e)
{
    const int now = cellAt (e.getPosition());
    if (now != hovered) { hovered = now; repaint(); }
}

void ToolBox::mouseExit (const MouseEvent&) { hovered = -1; repaint(); }

void ToolBox::mouseUp (const MouseEvent& e)
{
    const int i = cellAt (e.getPosition());
    if (i < 0) return;
    const auto tool = shown[(size_t) i];
    const auto& state = area == tools::Area::Arrange ? c.arrangeTool : c.rollTool;
    const bool hasModes = ! tools::info (tool).modes.empty();
    if (hasModes && (e.mods.isPopupMenu() || state.tool == tool)) modesMenu (tool);
    else pick (tool);
}

bool ToolBox::keyPressed (const KeyPress& k)
{
    if (k == KeyPress::escapeKey) { close(); return true; }
    return false;
}

void ToolBox::pick (tools::Tool t, std::optional<int> mode)
{
    c.chooseTool (area, t, mode);
    close();
}

void ToolBox::modesMenu (tools::Tool t)
{
    const auto& info = tools::info (t);
    const auto& state = area == tools::Area::Arrange ? c.arrangeTool : c.rollTool;
    PopupMenu m;
    m.addSectionHeader (info.name);
    for (int i = 0; i < (int) info.modes.size(); ++i)
        m.addItem (i + 1, info.modes[(size_t) i].name, true, state.tool == t && state.modeOf (t) == i);
    SafePointer<ToolBox> self (this);
    m.showMenuAsync (PopupMenu::Options().withTargetScreenArea (localAreaToGlobal (cellBounds ((int) (std::find (shown.begin(), shown.end(), t) - shown.begin())))),
                     [self, t] (int r)
    {
        if (! self) return;
        if (r > 0) self->pick (t, r - 1);
    });
}

void ToolBox::close()
{
    if (isCurrentlyModal()) exitModalState (0);
    else delete this;
}

} // namespace kal
