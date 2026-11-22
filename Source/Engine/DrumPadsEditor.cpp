// Drum Pads' window (decision 0128): sixteen of its 128 pads, four rows of
// four with the lowest note at the bottom left; beside them a map of all 128
// to move through them; the chosen pad's controls; its plug-in beside those.
#include "DrumPadsDevice.h"

#include "Editors.h"
#include "PluginManager.h"
#include "Theme.h"

namespace kal
{
using namespace juce;

namespace
{
constexpr int gap = 6;

String soundOf (const drumpads::Pad& p)
{
    if (p.hasPlugin()) return ui::u8 ("\xe2\x99\xab ") + String (p.plugin.name);
    if (! p.sample.empty()) return File (String (p.sample)).getFileNameWithoutExtension();
    return {};
}

String padName (const drumpads::Kit& k, int pad) { return String (drumpads::nameOf (k, pad)); }
String noteText (int pad) { return String (drumpads::noteName (pad)) + "  (" + String (pad) + ")"; }

String volumeText (double db) { return db <= -35.95 ? String ("-36 dB") : (db > 0 ? "+" : "") + String (db, 1) + " dB"; }
String panText (double pan)
{
    const int v = roundToInt (pan * 100.0);
    return v == 0 ? String ("Centre") : (v < 0 ? "Left " : "Right ") + String (std::abs (v));
}

class DrumPadsEditor;

// One of the sixteen pads shown: click to choose and hear it, right-click for
// its menu, drop a sound or a plug-in on it; M and S.
class Tile : public Component, public DragAndDropTarget, public FileDragAndDropTarget, public SettableTooltipClient
{
public:
    Tile (DrumPadsEditor& e, int s);
    int pad() const;
    void update();
    void paint (Graphics&) override;
    void resized() override;
    void mouseDown (const MouseEvent&) override;
    void mouseWheelMove (const MouseEvent&, const MouseWheelDetails&) override;
    bool isInterestedInDragSource (const SourceDetails& d) override
    {
        const auto t = d.description.toString();
        return t.startsWith ("file:") || t.startsWith ("plugin:");
    }
    void itemDragEnter (const SourceDetails&) override { dropping = true; repaint(); }
    void itemDragExit (const SourceDetails&) override { dropping = false; repaint(); }
    void itemDropped (const SourceDetails&) override;
    bool isInterestedInFileDrag (const StringArray&) override { return true; }
    void fileDragEnter (const StringArray&, int, int) override { dropping = true; repaint(); }
    void fileDragExit (const StringArray&) override { dropping = false; repaint(); }
    void filesDropped (const StringArray&, int, int) override;

    DrumPadsEditor& ed;
    const int slot;
    theme::HouseButton mute { "M", "Mute this pad.", true };
    theme::HouseButton solo { "S", "Solo this pad: only soloed pads are heard.", true };
    int lit = 0;          // frames left of its light after a hit
    bool dropping = false;
};

// All 128 pads, a row of four to a line, the highest at the top: those with a
// sound lit, the soloed in the accent; the sixteen shown framed. Click or drag to show others.
class Overview : public Component, public SettableTooltipClient
{
public:
    explicit Overview (DrumPadsEditor& e) : ed (e)
    {
        setTooltip (theme::wrapTip ("All 128 pads, the highest at the top. Click or drag to show sixteen others; or scroll over the pads."));
        setTitle ("Pads");
    }
    void paint (Graphics&) override;
    void mouseDown (const MouseEvent& e) override { moveTo (e.position.y); }
    void mouseDrag (const MouseEvent& e) override { moveTo (e.position.y); }
    void mouseWheelMove (const MouseEvent&, const MouseWheelDetails&) override;
    void moveTo (float y);
    float rowHeight() const { return (float) getHeight() / (float) (drumpads::padCount / drumpads::perRow); }
    DrumPadsEditor& ed;
    std::array<int, drumpads::padCount> lit {};
};

// A drop target itself, handing a drop to the pad under the point: on the Mac the
// window sits in a native view of its own, which Kalibre's drags reach only
// through its panel in the rack (0116).
class DrumPadsEditor : public AudioProcessorEditor, public DragAndDropTarget, private Timer
{
public:
    explicit DrumPadsEditor (DrumPadsProcessor& p) : AudioProcessorEditor (p), proc (p), overview (*this)
    {
        addAndMakeVisible (overview);
        for (int s = 0; s < drumpads::shown; ++s) addAndMakeVisible (tiles.add (new Tile (*this, s)));
        for (auto* b : { &randomSamples, &unsolo, &rename, &chooseSample, &pluginButton, &clearSound, &chokeButton, &pluginNote, &mute, &solo })
            addAndMakeVisible (b);
        randomSamples.onClick = [this] { fill(); };
        unsolo.onClick = [this] { kitChange ("Unsolo All", [] (drumpads::Kit& k) { for (auto& pad : k.pads) pad.solo = false; }); };
        rename.onClick = [this] { askName (proc.selected); };
        chooseSample.onClick = [this] { chooseFile (proc.selected); };
        pluginButton.onClick = [this] { pluginMenu (proc.selected).showMenuAsync (PopupMenu::Options().withTargetComponent (&pluginButton)); };
        clearSound.onClick = [this] { clear (proc.selected); };
        chokeButton.onClick = [this] { chokeMenu (proc.selected).showMenuAsync (PopupMenu::Options().withTargetComponent (&chokeButton)); };
        pluginNote.onClick = [this] { pluginNoteMenu (proc.selected).showMenuAsync (PopupMenu::Options().withTargetComponent (&pluginNote)); };
        for (auto* b : { &mute, &solo }) b->setClickingTogglesState (false);
        mute.onClick = [this] { toggleMute (proc.selected); };
        solo.onClick = [this] { toggleSolo (proc.selected); };
        for (auto* k : { &volume, &pan })
        {
            k->setSliderStyle (Slider::RotaryHorizontalVerticalDrag);
            k->setTextBoxStyle (Slider::NoTextBox, false, 0, 0);
            k->onDragStart = [this] { dragging = true; before = proc.kit(); };
            k->onValueChange = [this] { knobMoved(); };
            k->onDragEnd = [this]
            {
                dragging = false;
                const auto after = proc.kit();
                proc.setKit (before);
                proc.perform ("Pad Volume and Pan", [this, after] { proc.setKit (after); });
            };
            addAndMakeVisible (k);
        }
        volume.setRange (-36.0, 12.0, 0.1);
        volume.setDoubleClickReturnValue (true, 0.0);
        volume.setTitle ("Volume");
        volume.setTooltip ("The chosen pad's volume, -36 to +12 dB. Double-click for 0 dB.");
        pan.setRange (-1.0, 1.0, 0.01);
        pan.setDoubleClickReturnValue (true, 0.0);
        pan.setTitle ("Pan");
        pan.setTooltip ("The chosen pad's pan. Double-click for the centre.");
        proc.onKitChanged = [sp = SafePointer<DrumPadsEditor> (this)] { if (sp) sp->kitChanged(); };
        setResizable (true, false);
        update();
        setSize (1120, theme::px (300));
        startTimerHz (20);
    }

    ~DrumPadsEditor() override
    {
        proc.onKitChanged = nullptr;
        editors::close (inner.getComponent());
    }

    //==========================================================================
    // What every pad's controls do, here and in its tile.
    void select (int pad)
    {
        proc.selected = jlimit (0, drumpads::padCount - 1, pad);
        update();
        showPlugin();
    }

    void setView (int first)
    {
        const int v = drumpads::clampView (first);
        if (v == proc.kit().view) return;
        proc.setView (v);
        update();
    }

    void scroll (float wheelY) { setView (proc.kit().view + (wheelY > 0 ? drumpads::perRow : -drumpads::perRow)); }

    void change (int pad, const String& label, std::function<void (drumpads::Pad&)> f)
    {
        auto k = proc.kit();
        if (pad < 0 || pad >= drumpads::padCount) return;
        f (k.pads[(size_t) pad]);
        apply (label, k);
    }

    void kitChange (const String& label, std::function<void (drumpads::Kit&)> f)
    {
        auto k = proc.kit();
        f (k);
        apply (label, k);
    }

    void apply (const String& label, const drumpads::Kit& k)
    {
        proc.perform (label, [this, k]
        {
            StringArray problems;
            proc.setKit (k, &problems);
            if (! problems.isEmpty() && DrumPadsProcessor::say) DrumPadsProcessor::say (problems.joinIntoString (" "), true);
        });
    }

    void setSample (int pad, const String& path)
    {
        change (pad, "Pad Sample", [path] (drumpads::Pad& p) { p.sample = path.toStdString(); p.plugin = {}; p.pluginState.clear(); });
        select (pad);
    }

    // Several sounds dropped at once: on this pad and those above it.
    void setSamples (int pad, const StringArray& paths)
    {
        auto k = proc.kit();
        int at = pad;
        for (auto& path : paths)
        {
            if (at >= drumpads::padCount) break;
            auto& p = k.pads[(size_t) at++];
            p.sample = path.toStdString();
            p.plugin = {};
            p.pluginState.clear();
        }
        apply (paths.size() == 1 ? "Pad Sample" : "Pad Samples", k);
        select (pad);
    }

    void setPlugin (int pad, const PluginRef& ref)
    {
        change (pad, "Pad Plug-in", [ref] (drumpads::Pad& p) { p.plugin = ref; p.pluginState.clear(); });
        select (pad);
    }

    void clear (int pad)
    {
        change (pad, "Clear Pad", [] (drumpads::Pad& p) { p.sample.clear(); p.plugin = {}; p.pluginState.clear(); });
    }

    void toggleMute (int pad) { change (pad, "Mute Pad", [] (drumpads::Pad& p) { p.mute = ! p.mute; }); }
    void toggleSolo (int pad) { change (pad, "Solo Pad", [] (drumpads::Pad& p) { p.solo = ! p.solo; }); }

    void chooseFile (int pad)
    {
        chooser = std::make_unique<FileChooser> ("Choose a sample for " + padName (proc.kit(), pad), File(), "*.wav;*.aif;*.aiff;*.flac;*.mp3;*.ogg");
        chooser->launchAsync (FileBrowserComponent::openMode | FileBrowserComponent::canSelectFiles,
                              [sp = SafePointer<DrumPadsEditor> (this), pad] (const FileChooser& fc)
                              { if (sp && fc.getResult().existsAsFile()) sp->setSample (pad, fc.getResult().getFullPathName()); });
    }

    void askName (int pad)
    {
        auto* w = new AlertWindow ("Rename the Pad", {}, MessageBoxIconType::NoIcon);
        w->addTextEditor ("name", padName (proc.kit(), pad));
        w->addButton ("Rename", 1, KeyPress (KeyPress::returnKey));
        w->addButton ("Cancel", 0, KeyPress (KeyPress::escapeKey));
        ui::present (w, ModalCallbackFunction::create ([sp = SafePointer<DrumPadsEditor> (this), w, pad] (int r)
        {
            if (r != 1 || ! sp) return;
            const auto n = w->getTextEditorContents ("name").trim().toStdString();
            sp->change (pad, "Rename Pad", [n] (drumpads::Pad& p) { p.name = n; });   // empty: its note's name again
        }), true);
    }

    PopupMenu pluginMenu (int pad)
    {
        PopupMenu synths;
        for (auto& d : proc.pluginManager().known().getTypes())
            if (d.isInstrument && d.pluginFormatName != "Kalibre")
                synths.addItem (d.name + "  (" + d.manufacturerName + ")", [this, pad, ref = PluginManager::refFor (d)] { setPlugin (pad, ref); });
        if (synths.getNumItems() == 0) synths.addItem ("No Instruments Scanned Yet", false, false, [] {});
        return synths;
    }

    PopupMenu chokeMenu (int pad)
    {
        PopupMenu m;
        const int now = proc.kit().pads[(size_t) pad].choke;
        for (int c = 0; c <= drumpads::chokeGroups; ++c)
            m.addItem (c == 0 ? String ("None") : "Group " + String (c), true, now == c,
                       [this, pad, c] { change (pad, "Choke Group", [c] (drumpads::Pad& p) { p.choke = c; }); });
        return m;
    }

    PopupMenu pluginNoteMenu (int pad)
    {
        PopupMenu m;
        const int now = proc.kit().pads[(size_t) pad].pluginNote;
        for (int n = 24; n <= 96; n += 12)
            m.addItem (String (drumpads::noteName (n)), true, now == n,
                       [this, pad, n] { change (pad, "Plug-in Note", [n] (drumpads::Pad& p) { p.pluginNote = n; }); });
        m.addItem ("The Pad's Own Note (" + String (drumpads::noteName (pad)) + ")", true, now == pad,
                   [this, pad] { change (pad, "Plug-in Note", [pad] (drumpads::Pad& p) { p.pluginNote = pad; }); });
        return m;
    }

    void menu (int pad, Component& target)
    {
        select (pad);
        const auto& p = proc.kit().pads[(size_t) pad];
        PopupMenu m;
        m.addItem ("Rename...", [this, pad] { askName (pad); });
        m.addSeparator();
        m.addItem ("Choose a Sample...", [this, pad] { chooseFile (pad); });
        m.addSubMenu ("Play It with a Plug-in", pluginMenu (pad));
        if (p.hasPlugin()) m.addSubMenu ("Note Sent to the Plug-in", pluginNoteMenu (pad));
        m.addItem ("Clear Its Sound", p.hasSound(), false, [this, pad] { clear (pad); });
        m.addSeparator();
        m.addSubMenu ("Choke Group (Stops the Others in It)", chokeMenu (pad));
        m.addItem ("Mute", true, p.mute, [this, pad] { toggleMute (pad); });
        m.addItem ("Solo", true, p.solo, [this, pad] { toggleSolo (pad); });
        m.showMenuAsync (PopupMenu::Options().withTargetComponent (&target));
    }

    // Random Samples: a drum of its kind for each pad shown from the Sounds tab's folders.
    void fill()
    {
        const auto* index = DrumPadsProcessor::sounds ? DrumPadsProcessor::sounds() : nullptr;
        if (! index || index->items.empty())
        {
            if (DrumPadsProcessor::say) DrumPadsProcessor::say ("Random Samples chooses from your sounds: choose their folders in the browser's Sounds tab first.", true);
            return;
        }
        auto k = proc.kit();
        std::vector<int> shown;
        for (int s = 0; s < drumpads::shown; ++s) shown.push_back (k.view + s);
        const auto chosen = drumpads::randomSamples (k, shown, *index, (unsigned) Random::getSystemRandom().nextInt (1 << 30) + 1u);
        int filled = 0, synths = 0;
        for (size_t i = 0; i < shown.size() && i < chosen.size(); ++i)
        {
            auto& p = k.pads[(size_t) shown[i]];
            if (p.hasPlugin()) { ++synths; continue; }
            if (chosen[i].empty()) continue;
            p.sample = chosen[i];
            ++filled;
        }
        if (filled == 0)
        {
            if (DrumPadsProcessor::say) DrumPadsProcessor::say ("No drum sounds were found in your folders for these pads.", true);
            return;
        }
        apply ("Random Samples", k);
        if (DrumPadsProcessor::say)
            DrumPadsProcessor::say ("Random Samples: " + String (filled) + (filled == 1 ? " pad" : " pads") + " given a sound from your folders"
                                    + (synths > 0 ? ", " + String (synths) + " with plug-ins kept" : String()) + ". Click again for others; Undo for the last.", false);
    }

    // The knobs: heard as they're dragged, one undo step when let go; a double-click one step of its own.
    void knobMoved()
    {
        auto k = proc.kit();
        auto& p = k.pads[(size_t) proc.selected];
        p.volumeDb = volume.getValue();
        p.pan = pan.getValue();
        if (dragging) { proc.setKit (k); return; }
        apply ("Pad Volume and Pan", k);
    }

    //==========================================================================
    void kitChanged()
    {
        update();
        if (proc.padPlugin (proc.selected) != shownPlugin) showPlugin();
    }

    void update()
    {
        const auto& k = proc.kit();
        const auto& p = k.pads[(size_t) proc.selected];
        for (auto* t : tiles) t->update();
        unsolo.setEnabled (drumpads::anySolo (k));
        mute.setToggleState (p.mute, dontSendNotification);
        solo.setToggleState (p.solo, dontSendNotification);
        volume.setValue (p.volumeDb, dontSendNotification);
        pan.setValue (p.pan, dontSendNotification);
        chokeButton.setButtonText ((p.choke == 0 ? String ("No Choke Group") : "Choke Group " + String (p.choke)) + ui::u8 (" \xe2\x96\xbe"));
        pluginNote.setButtonText ("Sends " + String (drumpads::noteName (p.pluginNote)) + ui::u8 (" \xe2\x96\xbe"));
        pluginNote.setVisible (p.hasPlugin());
        clearSound.setEnabled (p.hasSound());
        overview.repaint();
        resized();
        repaint();
    }

    void showPlugin()
    {
        editors::close (inner.getComponent());
        inner = nullptr;
        shownPlugin = proc.padPlugin (proc.selected);
        if (shownPlugin && shownPlugin->hasEditor())
        {
            inner = editors::open (*shownPlugin);
            if (inner) addAndMakeVisible (*inner);
        }
        resized();
        repaint();
    }

    void timerCallback() override
    {
        const int view = proc.kit().view;
        for (int i = 0; i < drumpads::padCount; ++i)
        {
            const bool hit = proc.takeHit (i);
            auto& o = overview.lit[(size_t) i];
            if (hit || o > 0)
            {
                o = hit ? 4 : o - 1;
                overview.repaint();
            }
            if (i >= view && i < view + drumpads::shown)
            {
                auto* t = tiles[i - view];
                if (hit || t->lit > 0) { t->lit = hit ? 4 : t->lit - 1; t->repaint(); }
            }
        }
    }

    //==========================================================================
    void paint (Graphics& g) override
    {
        g.fillAll (theme::sunken);
        const auto& k = proc.kit();
        g.setFont (theme::font (theme::smallPoints));
        g.setColour (theme::textSecondary);
        g.drawText ("Pads " + String (drumpads::noteName (k.view)) + " to " + String (drumpads::noteName (k.view + drumpads::shown - 1))
                        + "  (" + String (k.view) + " to " + String (k.view + drumpads::shown - 1) + ")",
                    rangeArea, Justification::centredLeft, true);
        // The chosen pad: its name, its note, its sound.
        const auto& p = k.pads[(size_t) proc.selected];
        auto h = headArea;
        g.setColour (theme::text);
        g.setFont (theme::font (theme::bodyPoints, true));
        g.drawFittedText (padName (k, proc.selected), h.removeFromTop (h.getHeight() / 2), Justification::centredLeft, 1);
        g.setFont (theme::font (theme::smallPoints));
        g.setColour (theme::textSecondary);
        g.drawFittedText ("Played by " + noteText (proc.selected), h, Justification::centredLeft, 1);
        const auto said = soundOf (p);
        g.setColour (said.isEmpty() ? theme::textSecondary : proc.padSounds (proc.selected) ? theme::text : theme::warnText);
        g.drawFittedText (said.isEmpty() ? String ("No sound: drop one on the pad") : said + (proc.padSounds (proc.selected) ? String() : String (" (missing)")),
                          soundArea, Justification::centredLeft, 1);
        g.setColour (theme::textSecondary);
        g.drawFittedText ("Volume", volumeLabel.withHeight (volumeLabel.getHeight() / 2), Justification::centredLeft, 1);
        g.drawFittedText ("Pan", panLabel.withHeight (panLabel.getHeight() / 2), Justification::centredLeft, 1);
        g.setColour (theme::text);
        g.drawFittedText (volumeText (p.volumeDb), volumeLabel.withTrimmedTop (volumeLabel.getHeight() / 2), Justification::centredLeft, 1);
        g.drawFittedText (panText (p.pan), panLabel.withTrimmedTop (panLabel.getHeight() / 2), Justification::centredLeft, 1);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (8);
        auto top = r.removeFromTop (theme::px (24));
        rangeArea = top.removeFromLeft (theme::px (200));
        randomSamples.setBounds (top.removeFromLeft (randomSamples.idealWidth (theme::smallControl)));
        top.removeFromLeft (gap);
        unsolo.setBounds (top.removeFromLeft (unsolo.idealWidth (theme::smallControl)));
        r.removeFromTop (8);

        overview.setBounds (r.removeFromLeft (theme::px (48)));
        r.removeFromLeft (10);
        const int tileH = jmax (theme::px (48), (r.getHeight() - 3 * gap) / 4);
        const int tileW = jlimit (theme::px (120), theme::px (170), tileH * 2 + theme::px (20));
        auto grid = r.removeFromLeft (4 * tileW + 3 * gap);
        for (int s = 0; s < drumpads::shown; ++s)
        {
            const int row = 3 - s / drumpads::perRow, col = s % drumpads::perRow;   // the lowest note at the bottom left
            tiles[s]->setBounds (grid.getX() + col * (tileW + gap), grid.getY() + row * (tileH + gap), tileW, tileH);
        }
        r.removeFromLeft (14);

        auto strip = r.removeFromLeft (theme::px (250));
        const int line = theme::px (22);
        headArea = strip.removeFromTop (theme::px (36));
        soundArea = strip.removeFromTop (theme::px (20));
        strip.removeFromTop (gap);
        auto pair = [&] (Component& a, Component& b)
        {
            auto row = strip.removeFromTop (line);
            a.setBounds (row.removeFromLeft ((row.getWidth() - gap) / 2));
            row.removeFromLeft (gap);
            b.setBounds (row);
            strip.removeFromTop (gap);
        };
        pair (chooseSample, pluginButton);
        pair (clearSound, rename);
        auto knobs = strip.removeFromTop (theme::px (40));
        volume.setBounds (knobs.removeFromLeft (theme::px (40)));
        volumeLabel = knobs.removeFromLeft (theme::px (82));
        pan.setBounds (knobs.removeFromLeft (theme::px (40)));
        panLabel = knobs;
        strip.removeFromTop (gap);
        auto row = strip.removeFromTop (line);
        mute.setBounds (row.removeFromLeft (theme::px (28)));
        row.removeFromLeft (4);
        solo.setBounds (row.removeFromLeft (theme::px (28)));
        row.removeFromLeft (gap);
        chokeButton.setBounds (row);
        strip.removeFromTop (gap);
        pluginNote.setBounds (strip.removeFromTop (line));

        if (inner)
        {
            r.removeFromLeft (10);
            if (inner->isResizable()) inner->setBounds (r);
            else inner->setTopLeftPosition (r.getTopLeft());
        }
    }

    void mouseWheelMove (const MouseEvent&, const MouseWheelDetails& w) override { scroll (w.deltaY); }

    Tile* tileAt (Point<int> p) const
    {
        for (auto* t : tiles) if (t->getBounds().contains (p)) return t;
        return nullptr;
    }
    void lightDrop (Tile* over)
    {
        for (auto* t : tiles)
            if (t->dropping != (t == over)) { t->dropping = t == over; t->repaint(); }
    }
    bool isInterestedInDragSource (const SourceDetails& d) override
    {
        const auto t = d.description.toString();
        return t.startsWith ("file:") || t.startsWith ("plugin:");
    }
    void itemDragEnter (const SourceDetails& d) override { lightDrop (tileAt (d.localPosition)); }
    void itemDragMove (const SourceDetails& d) override { lightDrop (tileAt (d.localPosition)); }
    void itemDragExit (const SourceDetails&) override { lightDrop (nullptr); }
    void itemDropped (const SourceDetails& d) override
    {
        lightDrop (nullptr);
        if (auto* t = tileAt (d.localPosition)) t->itemDropped (d);
    }

    DrumPadsProcessor& proc;
    Overview overview;
    OwnedArray<Tile> tiles;
    theme::HouseButton randomSamples { "Random Samples", "A drum sound for each of the sixteen pads shown, from your Sounds folders: a kick for the kick, "
                                                         "a snare for the snare. Pads playing plug-ins keep them. Click again for others." };
    theme::HouseButton unsolo { "Unsolo All", "Every pad heard again." };
    theme::HouseButton rename { "Rename...", "Give the chosen pad a name of its own." };
    theme::HouseButton chooseSample { "Choose a Sample...", "Play a sound file on the chosen pad. Or drag one from the Sounds tab onto a pad." };
    theme::HouseButton pluginButton { ui::u8 ("Plug-in \xe2\x96\xbe"), "Play the chosen pad with an instrument plug-in, a drum synth. Or drag one from the browser onto a pad." };
    theme::HouseButton clearSound { "Clear", "Take the chosen pad's sound off it." };
    theme::HouseButton chokeButton { "No Choke Group", "Pads in the same choke group stop each other: a closed hat stops the open one." };
    theme::HouseButton pluginNote { "Sends C4", "The note the pad's plug-in is sent each time the pad is played." };
    theme::HouseButton mute { "M", "Mute the chosen pad.", true };
    theme::HouseButton solo { "S", "Solo the chosen pad: only soloed pads are heard.", true };
    Slider volume, pan;
    drumpads::Kit before;
    bool dragging = false;
    Rectangle<int> rangeArea, headArea, soundArea, volumeLabel, panLabel;
    Component::SafePointer<AudioProcessorEditor> inner;
    AudioPluginInstance* shownPlugin = nullptr;
    std::unique_ptr<FileChooser> chooser;
};

//==============================================================================
Tile::Tile (DrumPadsEditor& e, int s) : ed (e), slot (s)
{
    for (auto* b : { &mute, &solo })
    {
        b->setClickingTogglesState (false);
        addAndMakeVisible (b);
    }
    mute.onClick = [this] { ed.toggleMute (pad()); };
    solo.onClick = [this] { ed.toggleSolo (pad()); };
    setTooltip (theme::wrapTip ("Click to hear the pad and see its controls; right-click for its menu. "
                                "Drag a sound from the Sounds tab, or a plug-in from the browser, onto it: it plays this pad."));
}

int Tile::pad() const { return ed.proc.kit().view + slot; }

void Tile::update()
{
    const auto& k = ed.proc.kit();
    const auto& p = k.pads[(size_t) pad()];
    mute.setToggleState (p.mute, dontSendNotification);
    solo.setToggleState (p.solo, dontSendNotification);
    setTitle ("Pad " + padName (k, pad()));
    repaint();
}

void Tile::paint (Graphics& g)
{
    const auto& k = ed.proc.kit();
    const int i = pad();
    const auto& p = k.pads[(size_t) i];
    const auto b = getLocalBounds().toFloat().reduced (1.0f);
    g.setColour (theme::ground);
    g.fillRoundedRectangle (b, theme::cornerRadius);
    const bool chosen = i == ed.proc.selected;
    g.setColour (dropping || lit > 0 ? theme::playhead : chosen ? theme::edge : theme::rule);
    g.drawRoundedRectangle (b, theme::cornerRadius, dropping || lit > 0 || chosen ? 2.0f : 1.0f);

    auto r = getLocalBounds().reduced (8, 4);
    auto top = r.removeFromTop (theme::px (15));
    g.setFont (theme::font (theme::smallPoints));
    g.setColour (theme::textSecondary);
    const auto note = String (drumpads::noteName (i));
    const int noteW = theme::px (30);
    g.drawText (note, top.removeFromRight (noteW), Justification::centredRight, false);
    g.setFont (theme::font (theme::smallPoints, true));
    g.setColour (drumpads::heard (k, i) ? theme::text : theme::textSecondary);
    g.drawFittedText (padName (k, i), top, Justification::centredLeft, 1);
    g.setFont (theme::font (theme::smallPoints));
    const auto said = soundOf (p);
    auto mid = r.removeFromTop (theme::px (15));
    if (said.isEmpty()) { g.setColour (theme::textSecondary); g.drawFittedText ("Empty", mid, Justification::centredLeft, 1); }
    else
    {
        const bool sounds = ed.proc.padSounds (i);
        g.setColour (sounds ? theme::text : theme::warnText);
        g.drawFittedText (said + (sounds ? String() : String (" (missing)")), mid, Justification::centredLeft, 1);
    }
    if (p.choke > 0)
    {
        g.setColour (theme::textSecondary);
        g.drawText ("Choke " + String (p.choke), r.removeFromBottom (theme::px (16)).withTrimmedRight (2 * theme::px (22)), Justification::centredLeft, false);
    }
}

void Tile::resized()
{
    auto r = getLocalBounds().reduced (6, 4);
    auto row = r.removeFromBottom (theme::px (16));
    solo.setBounds (row.removeFromRight (theme::px (20)));
    row.removeFromRight (3);
    mute.setBounds (row.removeFromRight (theme::px (20)));
}

void Tile::mouseDown (const MouseEvent& e)
{
    if (e.mods.isPopupMenu()) { ed.menu (pad(), *this); return; }
    ed.proc.audition (pad());
    ed.select (pad());
}

void Tile::mouseWheelMove (const MouseEvent&, const MouseWheelDetails& w) { ed.scroll (w.deltaY); }

void Tile::itemDropped (const SourceDetails& d)
{
    dropping = false;
    repaint();
    const auto t = d.description.toString();
    if (t.startsWith ("file:")) { ed.setSample (pad(), t.fromFirstOccurrenceOf ("file:", false, false)); return; }
    const int index = t.fromFirstOccurrenceOf ("plugin:", false, false).getIntValue();
    const auto types = ed.proc.pluginManager().known().getTypes();
    if (index >= 0 && index < types.size()) ed.setPlugin (pad(), PluginManager::refFor (types[index]));
}

void Tile::filesDropped (const StringArray& files, int, int)
{
    dropping = false;
    repaint();
    ed.setSamples (pad(), files);
}

//==============================================================================
void Overview::paint (Graphics& g)
{
    const auto& k = ed.proc.kit();
    const float rh = rowHeight(), cw = (float) getWidth() / (float) drumpads::perRow;
    const int rows = drumpads::padCount / drumpads::perRow;
    for (int i = 0; i < drumpads::padCount; ++i)
    {
        const int row = rows - 1 - i / drumpads::perRow, col = i % drumpads::perRow;
        const auto cell = Rectangle<float> ((float) col * cw, (float) row * rh, cw, rh).reduced (1.0f, rh > 4.0f ? 1.0f : 0.5f);
        const auto& p = k.pads[(size_t) i];
        Colour c = theme::raised;
        if (p.hasSound()) c = drumpads::heard (k, i) ? theme::control : theme::scrollGrab;
        if (p.solo) c = theme::accent;
        if (lit[(size_t) i] > 0) c = theme::playhead;
        g.setColour (c);
        g.fillRect (cell);
    }
    // The sixteen shown.
    const int firstRow = rows - 1 - (k.view + drumpads::shown - 1) / drumpads::perRow;
    g.setColour (theme::playhead);
    g.drawRect (Rectangle<float> (0.0f, (float) firstRow * rh, (float) getWidth(), rh * (float) (drumpads::shown / drumpads::perRow)), 1.5f);
}

void Overview::moveTo (float y)
{
    const int rows = drumpads::padCount / drumpads::perRow;
    const int row = jlimit (0, rows - 1, rows - 1 - (int) (y / jmax (1.0f, rowHeight())));   // from the bottom
    ed.setView ((row - 1) * drumpads::perRow);   // the row clicked second from the bottom of the four shown
}

void Overview::mouseWheelMove (const MouseEvent&, const MouseWheelDetails& w) { ed.scroll (w.deltaY); }
} // namespace

AudioProcessorEditor* DrumPadsProcessor::createEditor() { return new DrumPadsEditor (*this); }

} // namespace kal
