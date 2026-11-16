#include "OfflineFxView.h"

namespace kal
{
using namespace juce;

OfflineFxView::OfflineFxView (Controller& ctrl) : c (ctrl)
{
    setTitle ("Offline Effects");
    addAndMakeVisible (viewport);
    viewport.setViewedComponent (&strip, false);
    viewport.setScrollBarsShown (false, true);
    for (auto* b : { &render, &done }) addAndMakeVisible (b);
    render.onClick = [this] { const Id id = clip; keepSettings(); c.renderOfflineFx (id); };
    done.onClick = [this] { close(); c.offlineClip = 0; c.viewChanged(); };
}

OfflineFxView::~OfflineFxView() { close(); }

void OfflineFxView::open (Id clipId)
{
    if (clipId == clip) return;
    close();
    const auto* cl = c.project.clip (clipId);
    if (! cl) return;
    clip = clipId;
    const double rate = jmax (8000.0, c.engine.sampleRate());
    for (auto& d : cl->offlineFx.chain)
    {
        auto slot = std::make_unique<Slot>();
        slot->name = String (d.plugin.name);
        String error;
        slot->plugin = c.engine.plugins().create (d.plugin, rate, 512, error);
        if (slot->plugin)
        {
            if (! d.state.empty())
            {
                MemoryBlock m;
                if (m.fromBase64Encoding (d.state)) slot->plugin->setStateInformation (m.getData(), (int) m.getSize());
            }
            slot->plugin->setPlayConfigDetails (2, 2, rate, 512);
            slot->plugin->prepareToPlay (rate, 512);
            AudioProcessorEditor* e = slot->plugin->hasEditor() ? slot->plugin->createEditorIfNeeded() : nullptr;
            if (! e) e = new GenericAudioProcessorEditor (*slot->plugin);
            slot->editor.reset (e);
            slot->natural = e->getBounds();
            strip.addAndMakeVisible (*e);
        }
        else slot->name << " (" << error << ")";
        slots.push_back (std::move (slot));
    }
    resized();
    repaint();
}

void OfflineFxView::keepSettings()
{
    if (clip == 0) return;
    std::vector<std::string> states;
    for (auto& s : slots)
    {
        std::string st;
        if (s->plugin)
        {
            MemoryBlock m;
            s->plugin->getStateInformation (m);
            st = m.toBase64Encoding().toStdString();
        }
        states.push_back (st);
    }
    const Id id = clip;
    c.apply ("Offline Effect Settings", [id, states] (Project& p)
    {
        if (auto* cl = p.clip (id))
            for (size_t i = 0; i < cl->offlineFx.chain.size() && i < states.size(); ++i)
                if (! states[i].empty()) cl->offlineFx.chain[i].state = states[i];
    });
}

void OfflineFxView::close()
{
    if (clip != 0) keepSettings();
    for (auto& s : slots)
    {
        s->editor.reset();
        if (s->plugin) s->plugin->releaseResources();
    }
    slots.clear();
    clip = 0;
}

void OfflineFxView::paint (Graphics& g)
{
    g.fillAll (theme::sunken);
    const auto* cl = c.project.clip (clip);
    g.setColour (theme::text);
    g.setFont (theme::font (theme::bodyPoints, true));
    g.drawText ("Offline effects of " + (cl ? String (cl->name.empty() ? "the clip" : cl->name) : String ("the clip")),
                getLocalBounds().removeFromTop (theme::px (32)).reduced (theme::px (10), 0), Justification::centredLeft, true);
    g.setFont (theme::font (theme::smallPoints));
    g.setColour (theme::textSecondary);
    int x = 0;
    for (auto& s : slots)
    {
        const int w = s->editor ? s->editor->getWidth() : theme::px (200);
        g.drawText (s->name, viewport.getX() + x - viewport.getViewPositionX() + 4, viewport.getY() - theme::px (16), w, theme::px (14), Justification::centredLeft, true);
        x += w + theme::px (8);
    }
}

void OfflineFxView::resized()
{
    auto bar = getLocalBounds().removeFromTop (theme::px (32)).reduced (theme::px (8), theme::px (4));
    done.setBounds (bar.removeFromRight (done.idealWidth (theme::smallControl)));
    bar.removeFromRight (theme::px (6));
    render.setBounds (bar.removeFromRight (render.idealWidth (theme::smallControl)));
    auto area = getLocalBounds().withTrimmedTop (theme::px (50)).reduced (theme::px (8), 0);
    viewport.setBounds (area);
    // Each window fitted to the zone's height, like the track's own (decision 0002).
    int x = 0;
    const int h = area.getHeight() - viewport.getScrollBarThickness();
    for (auto& s : slots)
    {
        if (! s->editor || s->natural.isEmpty()) { x += theme::px (200); continue; }
        const float k = jlimit (0.25f, 3.0f, (float) h / (float) s->natural.getHeight());
        if (s->editor->isResizable()) s->editor->setBounds (x, 0, roundToInt ((float) s->natural.getWidth() * k), h);
        else { s->editor->setScaleFactor (k); s->editor->setTopLeftPosition (x, 0); }
        x += roundToInt ((float) s->natural.getWidth() * k) + theme::px (8);
    }
    strip.setSize (jmax (x, area.getWidth()), h);
}

} // namespace kal
