#include "LauncherView.h"

#include "Edit.h"
#include "Song.h"

namespace kal
{
using namespace juce;

LauncherView::LauncherView (Controller& ctrl) : c (ctrl)
{
    setTitle ("Clip Launcher");
    for (auto* b : { &capture, &addScene, &back }) addAndMakeVisible (b);
    capture.onClick = [this]
    {
        const Tick from = c.project.loop.start, to = c.project.loop.end;
        int made = -1;
        c.apply ("Capture Scene", [&] (Project& p) { made = song::captureScene (p, from, to, "Scene " + std::to_string (song::sceneCount (p) + 1)); });
        c.say (made >= 0 ? "Captured the loop range into scene " + String (made + 1) + "." : String ("Set a loop range first."));
    };
    addScene.onClick = [this]
    {
        c.apply ("Add Scene", [] (Project& p) { p.scenes.resize ((size_t) song::sceneCount (p) + 1); p.scenes.back() = "Scene " + std::to_string (p.scenes.size()); });
    };
    back.onClick = [this] { c.backToArrangement(); };
    c.addChangeListener (this);
    startTimerHz (8);
}

LauncherView::~LauncherView() { c.removeChangeListener (this); }

void LauncherView::changeListenerCallback (ChangeBroadcaster*)
{
    if (isVisible()) { resized(); repaint(); }
}

int LauncherView::sceneWidth() const { return theme::px (120); }
int LauncherView::columnWidth() const { return theme::px (110); }
int LauncherView::rowHeight() const { return theme::px (30); }

Rectangle<int> LauncherView::grid() const
{
    return getLocalBounds().withTrimmedTop (theme::px (36)).reduced (theme::px (8), 0);
}

Rectangle<int> LauncherView::cell (int column, int scene) const
{
    const auto g = grid();
    // Row 0 is the tracks' names; scenes from row 1; the stop row last.
    return { g.getX() + sceneWidth() + column * columnWidth(), g.getY() + (scene + 1) * rowHeight(), columnWidth() - 4, rowHeight() - 4 };
}

void LauncherView::resized()
{
    columns.clear();
    for (auto& t : c.project.tracks)
        if (t.holdsClips() && ! t.hidden && ! c.project.hiddenByFolder (t.id)) columns.push_back (t.id);
    auto bar = getLocalBounds().removeFromTop (theme::px (36)).reduced (theme::px (8), theme::px (4));
    for (auto* b : { &capture, &addScene, &back })
    {
        b->setBounds (bar.removeFromLeft (b->idealWidth (theme::smallControl)));
        bar.removeFromLeft (theme::px (6));
    }
    back.setEnabled (song::anyLaunched (c.project));
}

// The stop row's square mark.
static Rectangle<float> h4 (Rectangle<float> r)
{
    const float s = r.getHeight() * 0.36f;
    return { r.getX() + 7, r.getCentreY() - s / 2, s, s };
}

void LauncherView::paint (Graphics& g)
{
    g.fillAll (theme::sunken);
    const int scenes = song::sceneCount (c.project);
    const Tick now = c.engine.isPlaying() ? c.playhead() : c.cursor;
    g.setFont (theme::font (theme::smallPoints));
    const auto area = grid();
    if (columns.empty())
    {
        g.setColour (theme::textSecondary);
        g.drawFittedText ("Add a track to put clips in the launcher.", area, Justification::centred, 2);
        return;
    }
    for (size_t col = 0; col < columns.size(); ++col)
    {
        const auto* t = c.project.track (columns[col]);
        auto head = cell ((int) col, -1);
        g.setColour (Colour (t->colour));
        g.fillRect (head.removeFromLeft (3));
        g.setColour (theme::text);
        g.drawText (String (t->name), head.reduced (4, 0), Justification::centredLeft, true);
        for (int s = 0; s < scenes; ++s)
        {
            const auto r = cell ((int) col, s).toFloat();
            const bool filled = s < (int) t->slots.size() && t->slots[(size_t) s].length > 0;
            const bool playing = t->launch.slot == s && t->launch.from <= now;
            const bool waiting = t->launch.slot == s && t->launch.from > now;
            if (! filled)
            {
                g.setColour (theme::lane);
                g.fillRoundedRectangle (r, 3.0f);
                continue;
            }
            const auto& clip = t->slots[(size_t) s];
            g.setColour (playing ? theme::accent : Colour (clip.colour ? clip.colour : t->colour).withMultipliedBrightness (0.55f));
            g.fillRoundedRectangle (r, 3.0f);
            if (waiting && (Time::getMillisecondCounter() / 250) % 2 == 0)
            {
                g.setColour (theme::accent);
                g.drawRoundedRectangle (r.reduced (1.0f), 3.0f, 2.0f);
            }
            g.setColour (playing ? theme::ink : theme::text);
            // A play mark, then the name.
            Path tri;
            const float h = r.getHeight() * 0.4f;
            tri.addTriangle (r.getX() + 6, r.getCentreY() - h / 2, r.getX() + 6, r.getCentreY() + h / 2, r.getX() + 6 + h * 0.8f, r.getCentreY());
            g.fillPath (tri);
            g.drawText (clip.name.empty() ? String (clip.isMidi() ? "MIDI" : "Audio") : String (clip.name),
                        r.toNearestInt().withTrimmedLeft ((int) (h + 12)), Justification::centredLeft, true);
        }
        // The stop row.
        const auto stop = cell ((int) col, scenes).toFloat();
        const bool stopped = t->launch.stopped;
        g.setColour (stopped ? theme::accent : theme::control);
        g.fillRoundedRectangle (stop, 3.0f);
        g.setColour (theme::ink);
        g.fillRect (h4 (stop));
        g.drawText ("Stop", stop.toNearestInt().withTrimmedLeft (theme::px (22)), Justification::centredLeft, false);
    }
    for (int s = 0; s < scenes; ++s)
    {
        const Rectangle<int> r (area.getX(), area.getY() + (s + 1) * rowHeight(), sceneWidth() - 6, rowHeight() - 4);
        g.setColour (theme::control);
        g.fillRoundedRectangle (r.toFloat(), 3.0f);
        g.setColour (theme::ink);
        const String name = s < (int) c.project.scenes.size() && ! c.project.scenes[(size_t) s].empty()
                              ? String (c.project.scenes[(size_t) s]) : "Scene " + String (s + 1);
        g.drawText (ui::u8 ("\xe2\x96\xb6 ") + name, r.reduced (6, 0), Justification::centredLeft, true);
    }
    if (scenes == 0)
    {
        g.setColour (theme::textSecondary);
        g.drawFittedText ("No scenes yet. Capture Scene copies the loop range in; Add Scene makes an empty row.",
                          area.withTrimmedTop (rowHeight()), Justification::centred, 2);
    }
}

void LauncherView::mouseDown (const MouseEvent& e)
{
    const int scenes = song::sceneCount (c.project);
    const auto area = grid();
    // A scene's name.
    if (e.x >= area.getX() && e.x < area.getX() + sceneWidth())
    {
        const int s = (e.y - area.getY()) / rowHeight() - 1;
        if (s < 0 || s >= scenes) return;
        if (e.mods.isPopupMenu())
        {
            PopupMenu m;
            m.addItem (ui::u8 ("Rename\xe2\x80\xa6"), [this, s]
            {
                auto* w = new AlertWindow ("Rename the Scene", {}, MessageBoxIconType::NoIcon);
                w->addTextEditor ("name", s < (int) c.project.scenes.size() ? String (c.project.scenes[(size_t) s]) : String());
                w->addButton ("Rename", 1, KeyPress (KeyPress::returnKey));
                w->addButton ("Cancel", 0, KeyPress (KeyPress::escapeKey));
                ui::present (w, ModalCallbackFunction::create ([this, s, w] (int ok)
                {
                    if (ok != 1) return;
                    const auto name = w->getTextEditorContents ("name").toStdString();
                    c.apply ("Rename Scene", [s, name] (Project& p) { if ((int) p.scenes.size() <= s) p.scenes.resize ((size_t) s + 1); p.scenes[(size_t) s] = name; });
                }), true);
            });
            m.addItem ("Delete Scene", [this, s]
            {
                c.apply ("Delete Scene", [s] (Project& p)
                {
                    if (s < (int) p.scenes.size()) p.scenes.erase (p.scenes.begin() + s);
                    for (auto& t : p.tracks)
                    {
                        if (s < (int) t.slots.size()) t.slots.erase (t.slots.begin() + s);
                        if (t.launch.slot == s) t.launch = {};
                        else if (t.launch.slot > s) --t.launch.slot;
                    }
                });
            });
            m.showMenuAsync (PopupMenu::Options());
            return;
        }
        c.launchScene (s);
        return;
    }
    const int col = (e.x - area.getX() - sceneWidth()) / columnWidth();
    const int row = (e.y - area.getY()) / rowHeight() - 1;
    if (col < 0 || col >= (int) columns.size() || row < 0 || row > scenes) return;
    const Id track = columns[(size_t) col];
    if (row == scenes) { c.launchSlot (track, -1); return; }
    const auto* t = c.project.track (track);
    const bool filled = row < (int) t->slots.size() && t->slots[(size_t) row].length > 0;
    if (e.mods.isPopupMenu() || ! filled) { slotMenu (track, row); return; }
    c.focusOn (track);
    c.launchSlot (track, row);
}

void LauncherView::slotMenu (Id track, int scene)
{
    const auto* t = c.project.track (track);
    if (! t) return;
    const bool filled = scene < (int) t->slots.size() && t->slots[(size_t) scene].length > 0;
    PopupMenu m;
    if (filled)
    {
        const Id clipId = t->slots[(size_t) scene].id;
        m.addItem ("Play", [this, track, scene] { c.launchSlot (track, scene); });
        m.addItem ("Edit", [this, track, clipId] { c.focusOn (track); c.selectClip (clipId); c.zoneTab = ZoneTab::Editor; c.viewChanged(); });
        m.addItem ("Copy to the Arrangement at the Cursor", [this, track, scene]
        {
            const Tick at = c.cursor;
            c.apply ("Clip from Slot", [track, scene, at] (Project& p)
            {
                auto* tr = p.track (track);
                if (! tr || scene >= (int) tr->slots.size()) return;
                Clip x = tr->slots[(size_t) scene];
                x.id = p.newId();
                x.start = at;
                for (auto& n : x.midi.notes) n.id = p.newId();
                edit::addClip (p, track, x);
            });
        });
        m.addItem ("Empty the Slot", [this, track, scene] { c.apply ("Empty Slot", [track, scene] (Project& p) { song::clearSlot (p, track, scene); }); });
    }
    else
    {
        const auto* chosen = c.project.clip (c.selectedClip);
        m.addItem ("Put the Chosen Clip Here", chosen != nullptr, false, [this, track, scene]
        {
            const Id from = c.selectedClip;
            c.apply ("Clip to Slot", [track, scene, from] (Project& p)
            {
                if (const auto* x = p.clip (from)) { Clip copy = *x; copy.link = 0; song::setSlot (p, track, scene, copy); }
            });
        });
        m.addItem ("New MIDI Clip (One Bar)", [this, track, scene]
        {
            c.apply ("New Slot Clip", [track, scene] (Project& p)
            {
                Clip x;
                x.kind = Clip::Kind::Midi;
                x.name = "Clip";
                x.length = p.tempo.barStart (1);
                x.loopLength = x.length;
                song::setSlot (p, track, scene, x);
            });
        });
    }
    m.showMenuAsync (PopupMenu::Options());
}

} // namespace kal
