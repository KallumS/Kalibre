#include "Theme.h"
#include "PluginClip.h"

#include <juce_audio_processors/juce_audio_processors.h>

namespace kal::theme
{
using namespace juce;

namespace
{
float textScale = 1.0f;
}

float scale() { return textScale; }
void setScale (float s) { textScale = jlimit (1.0f, 2.0f, s); }

double luminance (Colour c)
{
    auto lin = [] (uint8 v)
    {
        const auto s = v / 255.0;
        return s <= 0.04045 ? s / 12.92 : std::pow ((s + 0.055) / 1.055, 2.4);
    };
    return 0.2126 * lin (c.getRed()) + 0.7152 * lin (c.getGreen()) + 0.0722 * lin (c.getBlue());
}

double contrast (Colour a, Colour b)
{
    auto la = luminance (a), lb = luminance (b);
    if (la < lb) std::swap (la, lb);
    return (la + 0.05) / (lb + 0.05);
}

Font font (float points, bool bold)
{
    auto f = Font (FontOptions().withPointHeight (points * textScale));
    return bold ? f.boldened() : f;
}

void drawFocusRing (Graphics& g, Rectangle<float> area, float radius)
{
    g.setColour (focusRing);
    const auto w = (float) focusRingWidth;
    g.drawRoundedRectangle (area.reduced (w * 0.5f), radius, w);
}

String wrapTip (const String& text)
{
    StringArray lines;
    for (auto& paragraph : StringArray::fromLines (text))
    {
        String line;
        for (auto& word : StringArray::fromTokens (paragraph, " ", ""))
        {
            if (line.isNotEmpty() && line.length() + 1 + word.length() > tooltipColumns)
            {
                lines.add (line);
                line.clear();
            }
            line << (line.isEmpty() ? "" : " ") << word;
        }
        lines.add (line);
    }
    return lines.joinIntoString ("\n");
}

//==============================================================================
LookAndFeel::LookAndFeel()
{
    auto scheme = getDarkColourScheme();
    scheme.setUIColour (ColourScheme::windowBackground, ground);
    scheme.setUIColour (ColourScheme::widgetBackground, sunken);
    scheme.setUIColour (ColourScheme::menuBackground, raised);
    scheme.setUIColour (ColourScheme::outline, rule);
    scheme.setUIColour (ColourScheme::defaultText, text);
    scheme.setUIColour (ColourScheme::defaultFill, control);
    scheme.setUIColour (ColourScheme::highlightedText, text);
    scheme.setUIColour (ColourScheme::highlightedFill, rule);
    scheme.setUIColour (ColourScheme::menuText, text);
    setColourScheme (scheme);

    setColour (ResizableWindow::backgroundColourId, ground);
    setColour (DocumentWindow::textColourId, text);
    setColour (TextButton::buttonColourId, control);
    setColour (TextButton::buttonOnColourId, accent);
    setColour (TextButton::textColourOffId, ink);
    setColour (TextButton::textColourOnId, ink);
    setColour (ToggleButton::textColourId, text);
    setColour (ToggleButton::tickColourId, ink);
    setColour (ToggleButton::tickDisabledColourId, edge);
    setColour (Label::textColourId, text);
    setColour (PopupMenu::backgroundColourId, raised);
    setColour (PopupMenu::textColourId, text);
    setColour (PopupMenu::headerTextColourId, textOnRaised);
    setColour (PopupMenu::highlightedBackgroundColourId, rule);
    setColour (PopupMenu::highlightedTextColourId, text);
    setColour (TooltipWindow::backgroundColourId, raised);
    setColour (TooltipWindow::textColourId, text);
    setColour (TooltipWindow::outlineColourId, rule);
    setColour (ScrollBar::thumbColourId, scrollGrab);
    setColour (ScrollBar::trackColourId, sunken);
    setColour (AlertWindow::backgroundColourId, raised);
    setColour (AlertWindow::textColourId, text);
    setColour (AlertWindow::outlineColourId, rule);
    setColour (ComboBox::backgroundColourId, control);
    setColour (ComboBox::textColourId, ink);
    setColour (ComboBox::arrowColourId, ink);
    setColour (ComboBox::outlineColourId, edge);
    setColour (TextEditor::backgroundColourId, sunken);
    setColour (TextEditor::textColourId, text);
    setColour (TextEditor::outlineColourId, edge);
    setColour (TextEditor::focusedOutlineColourId, focusRing);
    setColour (TextEditor::highlightColourId, rule);
    setColour (CaretComponent::caretColourId, text);
    setColour (Slider::thumbColourId, control);
    setColour (Slider::trackColourId, rule);
    setColour (Slider::backgroundColourId, sunken);
    setColour (Slider::textBoxTextColourId, text);
    setColour (Slider::textBoxBackgroundColourId, sunken);
    setColour (Slider::textBoxOutlineColourId, rule);
    setColour (Slider::rotarySliderFillColourId, control);
    setColour (Slider::rotarySliderOutlineColourId, rule);
    setColour (ListBox::backgroundColourId, sunken);
    setColour (ListBox::textColourId, text);
    setColour (TreeView::backgroundColourId, sunken);
    setColour (TreeView::selectedItemBackgroundColourId, rule);
    setColour (TableHeaderComponent::backgroundColourId, raised);
    setColour (TableHeaderComponent::textColourId, text);
    setColour (TableHeaderComponent::outlineColourId, rule);
    setColour (TabbedButtonBar::tabOutlineColourId, rule);
    setColour (TabbedButtonBar::frontOutlineColourId, rule);
    setColour (FileTreeComponent::highlightColourId, rule);
    setColour (DirectoryContentsDisplayComponent::highlightColourId, rule);
    setColour (DirectoryContentsDisplayComponent::textColourId, text);
    setColour (ProgressBar::backgroundColourId, sunken);
    setColour (ProgressBar::foregroundColourId, control);
}

Font LookAndFeel::getTextButtonFont (TextButton&, int) { return font(); }
Font LookAndFeel::getLabelFont (Label& l) { return l.getFont().withHeight (l.getFont().getHeight()); }

int LookAndFeel::getTextButtonWidthToFitText (TextButton& b, int buttonHeight)
{
    const auto w = GlyphArrangement::getStringWidthInt (font(), b.getButtonText());
    return jmax (buttonHeight, w + 2 * px (buttonPadding));
}

void LookAndFeel::drawButtonBackground (Graphics& g, Button& b, const Colour& base, bool highlighted, bool down)
{
    const auto on = b.getToggleState();
    // A button may have an "on" colour of its own (an armed track's, decision 0064).
    const auto own = b.getProperties()["onColour"];
    const bool ownOn = on && ! own.isVoid();
    const auto onFill = ownOn ? Colour ((uint32) (int64) own) : accent;
    auto fill = on ? onFill : (base == findColour (TextButton::buttonColourId) ? control : base);
    if (! b.isEnabled()) fill = rule;
    else if (down) fill = on ? (ownOn ? onFill.darker (0.2f) : accentHeld) : controlHeld;
    else if (highlighted) fill = on ? (ownOn ? onFill.brighter (0.08f) : accentHover) : (fill == control ? controlHover : fill.brighter (0.1f));

    const auto area = b.getLocalBounds().toFloat().reduced (1.0f);
    const auto radius = jmin (cornerRadius, area.getHeight() * 0.3f);
    g.setColour (fill);
    g.fillRoundedRectangle (area, radius);
    // A second "on" colour fills the right half (armed for audio and MIDI, decision 0065).
    if (const auto second = b.getProperties()["onColour2"]; ownOn && ! second.isVoid() && b.isEnabled())
    {
        Colour two ((uint32) (int64) second);
        if (down) two = two.darker (0.2f); else if (highlighted) two = two.brighter (0.08f);
        g.saveState();
        g.reduceClipRegion (area.withLeft (area.getCentreX()).toNearestInt());
        g.setColour (two);
        g.fillRoundedRectangle (area, radius);
        g.restoreState();
    }
    if (b.hasKeyboardFocus (false)) drawFocusRing (g, area, radius);
}

void LookAndFeel::drawButtonText (Graphics& g, TextButton& b, bool, bool)
{
    const auto f = font (b.getHeight() < px (24) ? smallPoints : bodyPoints);
    g.setFont (f);
    g.setColour (b.isEnabled() ? ink : edge);
    const auto area = b.getLocalBounds();
    g.drawText (b.getButtonText(), area.reduced (2, 0), Justification::centred, true);
    // No bar under a chosen button's label: the user didn't want it (16 November).
}

void LookAndFeel::drawTickBox (Graphics& g, Component& c, float x, float y, float w, float h, bool ticked, bool enabled,
                               bool highlighted, bool)
{
    Rectangle<float> box (x, y, w, h);
    g.setColour (ticked ? accent : (highlighted ? frameHover : sunken));
    g.fillRoundedRectangle (box, 3.0f);
    g.setColour (ticked ? accentHeld : edge);
    g.drawRoundedRectangle (box.reduced (0.5f), 3.0f, 1.0f);
    if (ticked)
    {
        Path p;
        p.startNewSubPath (box.getX() + w * 0.22f, box.getCentreY());
        p.lineTo (box.getX() + w * 0.43f, box.getBottom() - h * 0.25f);
        p.lineTo (box.getRight() - w * 0.2f, box.getY() + h * 0.25f);
        g.setColour (enabled ? ink : edge);
        g.strokePath (p, PathStrokeType (2.0f));
    }
    if (c.hasKeyboardFocus (false)) drawFocusRing (g, box.expanded (3.0f), 4.0f);
}

void LookAndFeel::drawToggleButton (Graphics& g, ToggleButton& b, bool highlighted, bool down)
{
    const auto boxSize = (float) jmin (px (16), b.getHeight() - 4);
    drawTickBox (g, b, 2.0f, ((float) b.getHeight() - boxSize) * 0.5f, boxSize, boxSize, b.getToggleState(), b.isEnabled(),
                 highlighted, down);
    g.setColour (b.isEnabled() ? text : textSecondary);
    g.setFont (font());
    g.drawFittedText (b.getButtonText(), b.getLocalBounds().withTrimmedLeft (roundToInt (boxSize) + 8), Justification::centredLeft, 1);
}

Font LookAndFeel::getPopupMenuFont() { return font(); }

void LookAndFeel::drawPopupMenuBackgroundWithOptions (Graphics& g, int w, int h, const PopupMenu::Options&)
{
    g.fillAll (raised);
    g.setColour (rule);
    g.drawRect (0, 0, w, h, 1);
}

Font LookAndFeel::getMenuBarFont (MenuBarComponent&, int, const String&) { return font(); }

void LookAndFeel::drawMenuBarBackground (Graphics& g, int w, int h, bool, MenuBarComponent&)
{
    g.fillAll (ground);
    g.setColour (rule);
    g.fillRect (0, h - 1, w, 1);
}

void LookAndFeel::drawMenuBarItem (Graphics& g, int w, int h, int, const String& t, bool over, bool open, bool,
                                   MenuBarComponent& bar)
{
    if (open || over)
    {
        g.setColour (raised);
        g.fillRect (0, 0, w, h - 1);
    }
    g.setColour (text);
    g.setFont (getMenuBarFont (bar, 0, t));
    g.drawFittedText (t, 0, 0, w, h, Justification::centred, 1);
}

static TextLayout layoutTip (const String& t)
{
    AttributedString s;
    s.setJustification (Justification::topLeft);
    s.append (t, font(), text);
    TextLayout layout;
    layout.createLayoutWithBalancedLineLengths (s, 10000.0f);
    return layout;
}

Rectangle<int> LookAndFeel::getTooltipBounds (const String& tip, Point<int> pos, Rectangle<int> parent)
{
    const auto layout = layoutTip (tip);
    const auto w = (int) std::ceil (layout.getWidth()) + 20;
    const auto h = (int) std::ceil (layout.getHeight()) + 12;
    return Rectangle<int> (pos.x > parent.getCentreX() ? pos.x - (w + 12) : pos.x + 24,
                           pos.y > parent.getCentreY() ? pos.y - (h + 6) : pos.y + 6, w, h).constrainedWithin (parent);
}

void LookAndFeel::drawTooltip (Graphics& g, const String& t, int w, int h)
{
    const auto bounds = Rectangle<int> (w, h).toFloat();
    g.setColour (raised);
    g.fillRoundedRectangle (bounds, 4.0f);
    g.setColour (rule);
    g.drawRoundedRectangle (bounds.reduced (0.5f), 4.0f, 1.0f);
    layoutTip (t).draw (g, bounds.reduced (10.0f, 6.0f));
}

void LookAndFeel::drawScrollbar (Graphics& g, ScrollBar&, int x, int y, int w, int h, bool vertical,
                                 int thumbStart, int thumbSize, bool over, bool down)
{
    g.setColour (sunken);
    g.fillRect (x, y, w, h);
    const auto thumb = vertical ? Rectangle<int> (x, thumbStart, w, thumbSize) : Rectangle<int> (thumbStart, y, thumbSize, h);
    g.setColour (down ? control : (over ? edge : scrollGrab));
    g.fillRoundedRectangle (thumb.toFloat().reduced (2.0f), 3.0f);
}

void LookAndFeel::drawCornerResizer (Graphics& g, int w, int h, bool over, bool dragging)
{
    g.setColour (dragging || over ? control : edge);
    for (float i = 0.3f; i < 1.0f; i += 0.3f)
        g.drawLine ((float) w * i, (float) h, (float) w, (float) h * i, 1.5f);
}

void LookAndFeel::drawStretchableLayoutResizerBar (Graphics& g, int w, int h, bool vertical, bool over, bool dragging)
{
    g.fillAll (ground);
    g.setColour (dragging ? control : (over ? edge : rule));
    if (vertical) g.fillRect (w / 2 - 1, 0, 2, h);
    else g.fillRect (0, h / 2 - 1, w, 2);
    // A grip in the middle, so it reads as something to drag.
    g.setColour (dragging || over ? control : edge);
    const auto c = Point<float> ((float) w * 0.5f, (float) h * 0.5f);
    for (int i = -1; i <= 1; ++i)
        g.fillEllipse (Rectangle<float> (3.0f, 3.0f).withCentre (vertical ? c.translated (0, (float) i * 6) : c.translated ((float) i * 6, 0)));
}

void LookAndFeel::drawComboBox (Graphics& g, int w, int h, bool down, int, int, int, int, ComboBox& box)
{
    const auto area = Rectangle<int> (w, h).toFloat().reduced (1.0f);
    const auto radius = jmin (cornerRadius, area.getHeight() * 0.3f);
    g.setColour (down ? controlHeld : (box.isMouseOver (true) ? controlHover : control));
    g.fillRoundedRectangle (area, radius);
    Path arrow;
    const auto ax = area.getRight() - 12.0f, ay = area.getCentreY();
    arrow.addTriangle (ax - 4.0f, ay - 2.0f, ax + 4.0f, ay - 2.0f, ax, ay + 3.0f);
    g.setColour (ink);
    g.fillPath (arrow);
    if (box.hasKeyboardFocus (true)) drawFocusRing (g, area, radius);
}

Font LookAndFeel::getComboBoxFont (ComboBox& b) { return font (b.getHeight() < px (24) ? smallPoints : bodyPoints); }

void LookAndFeel::positionComboBoxText (ComboBox& box, Label& label)
{
    label.setBounds (4, 0, box.getWidth() - 22, box.getHeight());
    label.setFont (getComboBoxFont (box));
    label.setColour (Label::textColourId, ink);
}

void LookAndFeel::drawLinearSlider (Graphics& g, int x, int y, int w, int h, float pos, float, float,
                                    Slider::SliderStyle style, Slider& s)
{
    const bool vertical = style == Slider::LinearVertical;
    const auto r = Rectangle<int> (x, y, w, h).toFloat();
    const float track = 4.0f;
    auto rail = vertical ? Rectangle<float> (track, r.getHeight()).withCentre (r.getCentre())
                         : Rectangle<float> (r.getWidth(), track).withCentre (r.getCentre());
    g.setColour (sunken);
    g.fillRoundedRectangle (rail, 2.0f);
    g.setColour (edge);
    g.drawRoundedRectangle (rail, 2.0f, 1.0f);
    const float thumbLong = 10.0f, thumbShort = vertical ? jmin (r.getWidth(), 22.0f) : jmin (r.getHeight(), 18.0f);
    auto thumb = vertical ? Rectangle<float> (thumbShort, thumbLong).withCentre ({ r.getCentreX(), pos })
                          : Rectangle<float> (thumbLong, thumbShort).withCentre ({ pos, r.getCentreY() });
    const bool held = s.isMouseButtonDown();
    g.setColour (held ? accent : (s.isMouseOver() ? controlHover : control));
    g.fillRoundedRectangle (thumb, 3.0f);
    g.setColour (ink);
    if (vertical) g.fillRect (thumb.withSizeKeepingCentre (thumb.getWidth() - 6.0f, 1.5f));
    else g.fillRect (thumb.withSizeKeepingCentre (1.5f, thumb.getHeight() - 6.0f));
    if (s.hasKeyboardFocus (false)) drawFocusRing (g, thumb.expanded (3.0f), 4.0f);
}

void LookAndFeel::drawRotarySlider (Graphics& g, int x, int y, int w, int h, float pos, float start, float end, Slider& s)
{
    const auto r = Rectangle<int> (x, y, w, h).toFloat().reduced (3.0f);
    const auto size = jmin (r.getWidth(), r.getHeight());
    const auto c = r.getCentre();
    const auto radius = size * 0.5f;
    const auto angle = start + pos * (end - start);
    Path back;
    back.addCentredArc (c.x, c.y, radius - 2.0f, radius - 2.0f, 0.0f, start, end, true);
    g.setColour (rule);
    g.strokePath (back, PathStrokeType (3.0f, PathStrokeType::curved, PathStrokeType::rounded));
    // A pan knob fills from the centre.
    const bool centred = s.getMinimum() < 0 && s.getMaximum() > 0;
    const float from = centred ? (start + end) * 0.5f : start;
    Path value;
    value.addCentredArc (c.x, c.y, radius - 2.0f, radius - 2.0f, 0.0f, jmin (from, angle), jmax (from, angle), true);
    g.setColour (s.isMouseButtonDown() ? accent : control);
    g.strokePath (value, PathStrokeType (3.0f, PathStrokeType::curved, PathStrokeType::rounded));
    g.setColour (text);
    g.drawLine (c.x, c.y, c.x + (radius - 5.0f) * std::sin (angle), c.y - (radius - 5.0f) * std::cos (angle), 2.0f);
    if (s.hasKeyboardFocus (false)) drawFocusRing (g, r.withSizeKeepingCentre (size + 4, size + 4), size * 0.5f);
}

Label* LookAndFeel::createSliderTextBox (Slider& s)
{
    auto* l = LookAndFeel_V4::createSliderTextBox (s);
    l->setFont (font (smallPoints));
    l->setColour (Label::textColourId, text);
    l->setColour (Label::backgroundColourId, Colours::transparentBlack);
    l->setColour (Label::outlineColourId, Colours::transparentBlack);
    return l;
}

void LookAndFeel::fillTextEditorBackground (Graphics& g, int w, int h, TextEditor&)
{
    g.setColour (sunken);
    g.fillRoundedRectangle (Rectangle<int> (w, h).toFloat(), 4.0f);
}

void LookAndFeel::drawTextEditorOutline (Graphics& g, int w, int h, TextEditor& e)
{
    g.setColour (e.hasKeyboardFocus (true) ? focusRing : edge);
    g.drawRoundedRectangle (Rectangle<int> (w, h).toFloat().reduced (0.5f), 4.0f, e.hasKeyboardFocus (true) ? 2.0f : 1.0f);
}

//==============================================================================
HouseButton::HouseButton (const String& label, const String& tip, bool toggles) : TextButton (label)
{
    setMouseClickGrabsKeyboardFocus (false);
    setWantsKeyboardFocus (true);
    setClickingTogglesState (toggles);
    setTip (tip.isEmpty() ? label : tip);
}

void HouseButton::setTip (const String& tip)
{
    setTooltip (wrapTip (tip));
    setDescription (tip);
    setTitle (getButtonText().length() <= 2 ? tip.upToFirstOccurrenceOf (".", false, false) : getButtonText());
}

int HouseButton::idealWidth (int height)
{
    return getLookAndFeel().getTextButtonWidthToFitText (*this, px (height));
}

bool HouseButton::keyPressed (const KeyPress& key)
{
    if (key == KeyPress::spaceKey && isEnabled()) { triggerClick(); return true; }
    if (key == KeyPress::escapeKey && hasKeyboardFocus (false)) { giveAwayKeyboardFocus(); return true; }
    return TextButton::keyPressed (key);
}

} // namespace kal::theme

namespace kal::ui
{
static juce::Component::SafePointer<juce::Component> hostComponent;

void setHost (juce::Component* c) { hostComponent = c; }
juce::Component* host() { return hostComponent.getComponent(); }

namespace
{
// Plug-ins' windows step aside while a question is asked: on the Mac each is a
// native view, drawn over anything Kalibre draws, so it could cover the
// question ("Save the changes?" when quitting) with no way to bring it forward.
struct PluginsAside : public juce::ModalComponentManager::Callback
{
    std::unique_ptr<juce::ModalComponentManager::Callback> inner;
    std::vector<juce::Component::SafePointer<juce::Component>> hidden;

    void hideIn (juce::Component& c)
    {
        for (auto* child : c.getChildren())
        {
            if (! child->isVisible()) continue;
            // A plug-in's window, or what shows one in the rack (decision 0058:
            // on the Mac that holds it in a native view of its own, outside this tree).
            if (dynamic_cast<juce::AudioProcessorEditor*> (child) != nullptr || dynamic_cast<PluginClip*> (child) != nullptr)
            {
                child->setVisible (false);
                hidden.emplace_back (child);
            }
            else hideIn (*child);
        }
    }

    void modalStateFinished (int result) override
    {
        for (auto& h : hidden) if (h) h->setVisible (true);
        if (inner) inner->modalStateFinished (result);
    }
};
} // namespace

void present (juce::AlertWindow* w, juce::ModalComponentManager::Callback* cb, bool deleteWhenDismissed)
{
    auto aside = std::make_unique<PluginsAside>();
    aside->inner.reset (cb);
    if (auto* h = host())
    {
        aside->hideIn (*h);
        h->addAndMakeVisible (w);
        w->setAlwaysOnTop (true);
        w->setBounds (w->getBounds().withCentre (h->getLocalBounds().getCentre()));
    }
    w->enterModalState (true, aside.release(), deleteWhenDismissed);
}
} // namespace kal::ui
