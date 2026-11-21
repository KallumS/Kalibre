// Kalibre Drums' window (decision 0105): the pads, each with its sound and its
// row of steps; what's along the top; the chosen pad's plug-in beside them.
#include "DrumMachine.h"

#include "Editors.h"
#include "Ideas.h"
#include "PluginManager.h"
#include "Theme.h"

namespace kal
{
using namespace juce;

namespace
{
constexpr int rowHeight = 26;
constexpr int nameWidth = 104, sourceWidth = 150, smallWidth = 26;

String sourceOf (const drumkit::Pad& p)
{
    if (p.hasPlugin()) return String (p.plugin.name);
    if (! p.sample.empty()) return File (String (p.sample)).getFileNameWithoutExtension();
    return {};
}

// A pad's row: its name (click to hear it), its sound (drop a sample or a
// plug-in on the row), M, volume and pan, and its steps.
class PadRow : public Component, public DragAndDropTarget, public FileDragAndDropTarget, public SettableTooltipClient
{
public:
    PadRow (DrumMachine& p, int index, std::function<void (int)> showPlugin) : proc (p), i (index), show (std::move (showPlugin))
    {
        addAndMakeVisible (name);
        name.onClick = [this]
        {
            if (ModifierKeys::currentModifiers.isPopupMenu()) return;
            proc.audition (i);
            if (proc.kit().pads[(size_t) i].hasPlugin()) show (i);
        };
        name.setTitle ("Pad");
        addAndMakeVisible (mute);
        mute.setClickingTogglesState (false);
        mute.onClick = [this] { change ("Mute Pad", [] (drumkit::Pad& pad, drumkit::Kit&) { pad.mute = ! pad.mute; }); };
        for (auto* k : { &volume, &pan })
        {
            k->setSliderStyle (Slider::RotaryHorizontalVerticalDrag);
            k->setTextBoxStyle (Slider::NoTextBox, false, 0, 0);
            k->onDragStart = [this] { before = proc.kit(); };
            k->onValueChange = [this]
            {
                // Heard as it's dragged; one undo step when let go.
                auto k = proc.kit();
                if (i >= (int) k.pads.size()) return;
                k.pads[(size_t) i].volumeDb = volume.getValue();
                k.pads[(size_t) i].pan = pan.getValue();
                proc.setKit (k);
            };
            k->onDragEnd = [this]
            {
                const auto after = proc.kit();
                proc.setKit (before);
                proc.perform ("Pad Volume and Pan", [this, after] { proc.setKit (after); });
            };
            addAndMakeVisible (k);
        }
        volume.setRange (-36.0, 12.0, 0.1);
        volume.setDoubleClickReturnValue (true, 0.0);
        volume.setTitle ("Volume");
        pan.setRange (-1.0, 1.0, 0.01);
        pan.setDoubleClickReturnValue (true, 0.0);
        pan.setTitle ("Pan");
        update();
    }

    void update()
    {
        const auto& k = proc.kit();
        if (i >= (int) k.pads.size()) return;
        const auto& pad = k.pads[(size_t) i];
        name.setButtonText (String (pad.name));
        name.setTip (String (pad.name) + " (" + String (patterns::drumName (pad.note)) + ", note " + String (pad.note)
                     + "): click to hear it. Right-click to rename it, choose its note, a sample or a plug-in, its choke group, or take it out.");
        mute.setToggleState (pad.mute, dontSendNotification);
        volume.setValue (pad.volumeDb, dontSendNotification);
        pan.setValue (pad.pan, dontSendNotification);
        setTooltip (theme::wrapTip ("Drag a sound from the Sounds tab, or a plug-in from the browser, onto the row: it plays this pad."));
        repaint();
    }

    // What the row paints: its sound's name, and the steps.
    void paint (Graphics& g) override
    {
        const auto& k = proc.kit();
        if (i >= (int) k.pads.size()) return;
        const auto& pad = k.pads[(size_t) i];
        if (dropping) { g.setColour (theme::raised); g.fillRect (getLocalBounds()); }
        const auto src = sourceArea();
        const auto said = sourceOf (pad);
        g.setFont (theme::font (theme::smallPoints));
        if (said.isEmpty())
        {
            g.setColour (theme::textSecondary);
            g.drawFittedText ("Drop a sound or a plug-in", src, Justification::centredLeft, 1);
        }
        else
        {
            g.setColour (proc.padSounds (i) ? theme::text : theme::warnText);
            g.drawFittedText ((pad.hasPlugin() ? String (ui::u8 ("\xe2\x99\xab ")) : String()) + said
                              + (proc.padSounds (i) ? String() : String (" (missing)")), src, Justification::centredLeft, 1);
        }
        const auto steps = stepArea();
        const auto& g8 = k.pattern;
        if (g8.steps <= 0 || i >= (int) g8.lanes.size()) return;
        const float colW = (float) steps.getWidth() / (float) g8.steps;
        const int perBeat = (int) jmax<Tick> (1, PPQ / jmax<Tick> (1, g8.stepTicks));
        const int lit = proc.playingStep.load();
        for (int s = 0; s < g8.steps; ++s)
        {
            auto cell = Rectangle<float> ((float) steps.getX() + (float) s * colW, (float) steps.getY(), colW, (float) steps.getHeight()).reduced (1.5f);
            const int v = g8.lanes[(size_t) i].velocity[(size_t) s];
            g.setColour (v > 0 ? theme::accent.withMultipliedBrightness (0.55f + 0.45f * (float) v / 127.0f)
                               : (s % perBeat == 0 ? theme::rule : theme::raised));
            g.fillRoundedRectangle (cell, 3.0f);
            if (s == lit) { g.setColour (theme::playhead.withAlpha (0.5f)); g.drawRoundedRectangle (cell, 3.0f, 1.5f); }
        }
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (0, 2);
        name.setBounds (r.removeFromLeft (theme::px (nameWidth)));
        r.removeFromLeft (theme::px (sourceWidth) + 4);
        mute.setBounds (r.removeFromLeft (theme::px (smallWidth)));
        volume.setBounds (r.removeFromLeft (theme::px (smallWidth)));
        pan.setBounds (r.removeFromLeft (theme::px (smallWidth)));
    }

    Rectangle<int> sourceArea() const { return { theme::px (nameWidth) + 4, 0, theme::px (sourceWidth), getHeight() }; }
    Rectangle<int> stepArea() const
    {
        const int x = theme::px (nameWidth) + 4 + theme::px (sourceWidth) + 4 + 3 * theme::px (smallWidth) + 8;
        return { x, 2, jmax (0, getWidth() - x), getHeight() - 4 };
    }

    // Steps: click to switch one on or off; drag to paint the same along the row;
    // Option-click for a loud, then a soft hit. One undo step a gesture.
    int stepAt (Point<int> p) const
    {
        const auto a = stepArea();
        const auto& k = proc.kit();
        if (! a.contains (p) || k.pattern.steps <= 0) return -1;
        return jlimit (0, k.pattern.steps - 1, (p.x - a.getX()) * k.pattern.steps / jmax (1, a.getWidth()));
    }

    void mouseDown (const MouseEvent& e) override
    {
        if (e.mods.isPopupMenu()) { menu(); return; }
        if (sourceArea().contains (e.getPosition()) && proc.kit().pads[(size_t) i].hasPlugin()) { show (i); return; }
        const int s = stepAt (e.getPosition());
        if (s < 0) return;
        before = proc.kit();
        auto k = before;
        auto& v = k.pattern.lanes[(size_t) i].velocity[(size_t) s];
        if (e.mods.isAltDown()) v = v == 0 ? 127 : v >= 120 ? 60 : v <= 70 ? 100 : 127;
        else v = v > 0 ? 0 : 100;
        painting = v;
        proc.setKit (k);
    }

    void mouseDrag (const MouseEvent& e) override
    {
        if (painting < 0) return;
        const int s = stepAt (e.getPosition());
        if (s < 0) return;
        auto k = proc.kit();
        auto& v = k.pattern.lanes[(size_t) i].velocity[(size_t) s];
        if (v == painting) return;
        v = painting;
        proc.setKit (k);
    }

    void mouseUp (const MouseEvent&) override
    {
        if (painting < 0) return;
        painting = -1;
        const auto after = proc.kit();
        proc.setKit (before);
        proc.perform ("Steps", [this, after] { proc.setKit (after); });
    }

    // Dropped: a sound from the Sounds tab or the Finder, or a plug-in from the browser.
    bool isInterestedInDragSource (const SourceDetails& d) override
    {
        const auto t = d.description.toString();
        return t.startsWith ("file:") || t.startsWith ("plugin:");
    }
    void itemDragEnter (const SourceDetails&) override { dropping = true; repaint(); }
    void itemDragExit (const SourceDetails&) override { dropping = false; repaint(); }
    void itemDropped (const SourceDetails& d) override
    {
        dropping = false;
        const auto t = d.description.toString();
        if (t.startsWith ("file:")) { setSample (t.fromFirstOccurrenceOf ("file:", false, false)); return; }
        const int index = t.fromFirstOccurrenceOf ("plugin:", false, false).getIntValue();
        const auto types = proc.pluginManager().known().getTypes();
        if (index >= 0 && index < types.size()) setPlugin (PluginManager::refFor (types[index]));
    }
    bool isInterestedInFileDrag (const StringArray& files) override { return files.size() == 1; }
    void fileDragEnter (const StringArray&, int, int) override { dropping = true; repaint(); }
    void fileDragExit (const StringArray&) override { dropping = false; repaint(); }
    void filesDropped (const StringArray& files, int, int) override { dropping = false; setSample (files[0]); }

    void setSample (const String& path)
    {
        change ("Pad Sample", [path] (drumkit::Pad& pad, drumkit::Kit&) { pad.sample = path.toStdString(); pad.plugin = {}; pad.pluginState.clear(); });
    }
    void setPlugin (const PluginRef& ref)
    {
        change ("Pad Plug-in", [ref] (drumkit::Pad& pad, drumkit::Kit&) { pad.plugin = ref; pad.pluginState.clear(); });
        show (i);
    }

    void menu()
    {
        const auto& pad = proc.kit().pads[(size_t) i];
        PopupMenu m;
        m.addItem ("Rename...", [this] { rename(); });
        PopupMenu notes;
        for (int n = 35; n <= 81; ++n)
            notes.addItem (String (patterns::drumName (n)) + "  (" + String (n) + ")", true, pad.note == n,
                           [this, n] { change ("Pad Note", [n] (drumkit::Pad& p, drumkit::Kit&) { p.note = n; }); });
        m.addSubMenu ("Played by", notes);
        m.addSeparator();
        m.addItem ("Choose a Sample...", [this]
        {
            chooser = std::make_unique<FileChooser> ("Choose a sample for " + String (proc.kit().pads[(size_t) i].name), File(), "*.wav;*.aif;*.aiff;*.flac;*.mp3;*.ogg");
            chooser->launchAsync (FileBrowserComponent::openMode | FileBrowserComponent::canSelectFiles,
                                  [sp = SafePointer<PadRow> (this)] (const FileChooser& fc) { if (sp && fc.getResult().existsAsFile()) sp->setSample (fc.getResult().getFullPathName()); });
        });
        PopupMenu synths;
        const auto types = proc.pluginManager().known().getTypes();
        for (auto& d : types)
            if (d.isInstrument && d.pluginFormatName != "Kalibre")
                synths.addItem (d.name + "  (" + d.manufacturerName + ")", [this, ref = PluginManager::refFor (d)] { setPlugin (ref); });
        m.addSubMenu ("Play It with a Plug-in", synths, synths.getNumItems() > 0);
        if (pad.hasPlugin())
        {
            m.addItem ("Show Its Plug-in", [this] { show (i); });
            PopupMenu pn;
            for (int n = 24; n <= 96; n += 12)
                pn.addItem (MidiMessage::getMidiNoteName (n, true, true, 3), true, pad.pluginNote == n,
                            [this, n] { change ("Plug-in Note", [n] (drumkit::Pad& p, drumkit::Kit&) { p.pluginNote = n; }); });
            m.addSubMenu ("Note Sent to the Plug-in", pn);
        }
        m.addItem ("Clear Its Sound", pad.hasPlugin() || ! pad.sample.empty(), false,
                   [this] { change ("Clear Pad", [] (drumkit::Pad& p, drumkit::Kit&) { p.sample.clear(); p.plugin = {}; p.pluginState.clear(); }); });
        PopupMenu choke;
        for (int c = 0; c <= 4; ++c)
            choke.addItem (c == 0 ? String ("None") : "Group " + String (c), true, pad.choke == c,
                           [this, c] { change ("Choke Group", [c] (drumkit::Pad& p, drumkit::Kit&) { p.choke = c; }); });
        m.addSubMenu ("Choke Group (Stops the Others in It)", choke);
        m.addSeparator();
        m.addItem ("Take Out This Pad", proc.kit().pads.size() > 1, false, [this]
        {
            const int index = i;
            auto k = proc.kit();
            drumkit::removePad (k, index);
            proc.perform ("Take Out Pad", [this, k] { proc.setKit (k); });
        });
        m.showMenuAsync (PopupMenu::Options().withTargetComponent (&name));
    }

    void rename()
    {
        auto* w = new AlertWindow ("Rename the Pad", {}, MessageBoxIconType::NoIcon);
        w->addTextEditor ("name", proc.kit().pads[(size_t) i].name);
        w->addButton ("Rename", 1, KeyPress (KeyPress::returnKey));
        w->addButton ("Cancel", 0, KeyPress (KeyPress::escapeKey));
        ui::present (w, ModalCallbackFunction::create ([sp = SafePointer<PadRow> (this), w] (int r)
        {
            if (r != 1 || ! sp) return;
            const auto n = w->getTextEditorContents ("name").trim().toStdString();
            if (! n.empty()) sp->change ("Rename Pad", [n] (drumkit::Pad& p, drumkit::Kit&) { p.name = n; });
        }), true);
    }

    void change (const String& label, std::function<void (drumkit::Pad&, drumkit::Kit&)> f)
    {
        auto k = proc.kit();
        if (i >= (int) k.pads.size()) return;
        f (k.pads[(size_t) i], k);
        drumkit::matchLanes (k);
        proc.perform (label, [this, k]
        {
            StringArray problems;
            proc.setKit (k, &problems);
            if (! problems.isEmpty() && DrumMachine::say) DrumMachine::say (problems.joinIntoString (" "), true);
        });
    }

    DrumMachine& proc;
    const int i;
    std::function<void (int)> show;
    theme::HouseButton name { "Pad", "" };
    theme::HouseButton mute { "M", "Mute this pad.", true };
    Slider volume, pan;
    drumkit::Kit before;
    int painting = -1;
    bool dropping = false;
    std::unique_ptr<FileChooser> chooser;
};

// Dragged onto the arrangement: the pattern as a clip there (decision 0105).
class DragOut : public theme::HouseButton
{
public:
    explicit DragOut (DrumMachine& p) : HouseButton ("Drag to Timeline", "Drag the pattern onto a track in the arrangement: it becomes a clip "
                                                                          "the drum grid opens. On its own track, the device's own pattern is switched off, so it isn't heard twice."),
                                        proc (p) {}
    void mouseDrag (const MouseEvent& e) override
    {
        if (e.getDistanceFromDragStart() < 6) return;
        if (auto* dc = DragAndDropContainer::findParentDragContainerFor (this))
            if (! dc->isDragAndDropActive())
                dc->startDragging ("drums:" + String::toHexString ((pointer_sized_int) &proc), this);
    }
    DrumMachine& proc;
};

class DrumMachineEditor : public AudioProcessorEditor, private Timer
{
public:
    explicit DrumMachineEditor (DrumMachine& p) : AudioProcessorEditor (p), proc (p), dragOut (p)
    {
        for (auto* b : { &playPattern, &steps, &randomBeat, &randomSamples, &clear, &addPad, (theme::HouseButton*) &dragOut }) addAndMakeVisible (b);
        playPattern.setClickingTogglesState (false);
        playPattern.onClick = [this] { kitChange ("Play Pattern", [] (drumkit::Kit& k) { k.playsPattern = ! k.playsPattern; }); };
        steps.onClick = [this] { stepsMenu(); };
        randomBeat.onClick = [this] { beat(); };
        randomSamples.onClick = [this] { fill(); };
        clear.onClick = [this] { kitChange ("Clear Pattern", [] (drumkit::Kit& k) { for (auto& l : k.pattern.lanes) std::fill (l.velocity.begin(), l.velocity.end(), 0); }); };
        addPad.onClick = [this] { kitChange ("Add Pad", [] (drumkit::Kit& k) { drumkit::addPad (k); }); };
        swing.setSliderStyle (Slider::RotaryHorizontalVerticalDrag);
        swing.setTextBoxStyle (Slider::NoTextBox, false, 0, 0);
        swing.setRange (0.0, 0.75, 0.01);
        swing.setTitle ("Swing");
        swing.setTooltip ("Swing: every second step played later, by up to three quarters of a step.");
        swing.onDragStart = [this] { swingBefore = proc.kit(); };
        swing.onValueChange = [this] { auto k = proc.kit(); k.pattern.swing = swing.getValue(); proc.setKit (k); };
        swing.onDragEnd = [this] { const auto after = proc.kit(); proc.setKit (swingBefore); proc.perform ("Swing", [this, after] { proc.setKit (after); }); };
        addAndMakeVisible (swing);
        proc.onKitChanged = [sp = SafePointer<DrumMachineEditor> (this)] { if (sp) sp->kitChanged(); };
        setResizable (true, false);
        rebuild();
        setSize (1100, preferredHeight());
        startTimerHz (20);
    }

    ~DrumMachineEditor() override
    {
        proc.onKitChanged = nullptr;
        editors::close (inner.getComponent());
    }

    int preferredHeight() const { return theme::px (40) + (int) proc.kit().pads.size() * theme::px (rowHeight) + 12; }

    void rebuild()
    {
        rows.clear();
        for (int i = 0; i < (int) proc.kit().pads.size(); ++i)
            addAndMakeVisible (rows.add (new PadRow (proc, i, [this] (int k) { showPlugin (k); })));
        update();
        resized();
    }

    void kitChanged()
    {
        // A pad added or taken out: the rows made again once the edit that did it has returned.
        if (rows.size() != (int) proc.kit().pads.size())
            MessageManager::callAsync ([sp = SafePointer<DrumMachineEditor> (this)] { if (sp) { sp->rebuild(); sp->setSize (sp->getWidth(), sp->preferredHeight()); } });
        else { for (auto* r : rows) r->update(); update(); }
        if (shown >= 0 && proc.padPlugin (shown) != shownPlugin) showPlugin (shown);
    }

    void update()
    {
        const auto& k = proc.kit();
        playPattern.setToggleState (k.playsPattern, dontSendNotification);
        steps.setButtonText (String (k.pattern.steps) + " Steps " + ui::u8 ("\xe2\x96\xbe"));
        swing.setValue (k.pattern.swing, dontSendNotification);
    }

    void timerCallback() override
    {
        const int now = proc.playingStep.load();
        if (now != lastStep) { lastStep = now; for (auto* r : rows) r->repaint(); }
    }

    void showPlugin (int i)
    {
        editors::close (inner.getComponent());
        inner = nullptr;
        shown = i;
        shownPlugin = proc.padPlugin (i);
        if (shownPlugin && shownPlugin->hasEditor())
        {
            inner = editors::open (*shownPlugin);
            if (inner) addAndMakeVisible (*inner);
        }
        resized();
        repaint();
    }

    void stepsMenu()
    {
        PopupMenu m;
        const auto& k = proc.kit();
        for (int n : { 8, 12, 16, 24, 32, 64 })
            m.addItem (String (n) + " Steps", true, k.pattern.steps == n, [this, n] { kitChange ("Steps", [n] (drumkit::Kit& kit) { kit.pattern.steps = n; }); });
        m.addSeparator();
        const std::pair<const char*, Tick> lengths[] { { "Each Step an Eighth", PPQ / 2 }, { "Each Step a Sixteenth", PPQ / 4 },
                                                       { "Each Step a Sixteenth Triplet", PPQ / 6 }, { "Each Step a Thirty-Second", PPQ / 8 } };
        for (auto& [label, ticks] : lengths)
            m.addItem (label, true, k.pattern.stepTicks == ticks, [this, t = ticks] { kitChange ("Step Length", [t] (drumkit::Kit& kit) { kit.pattern.stepTicks = t; }); });
        m.showMenuAsync (PopupMenu::Options().withTargetComponent (&steps));
    }

    // Random Beat: a Good Idea groove (decision 0062) on the kit's pads.
    void beat()
    {
        auto k = proc.kit();
        const int bars = jlimit (1, 16, (int) (drumkit::patternLength (k) / (PPQ * 4)));
        const int seed = Random::getSystemRandom().nextInt (ideas::maxSeed) + 1;
        const auto idea = ideas::drums (bars, 4, 4, seed);
        if (! idea.error.empty()) { if (DrumMachine::say) DrumMachine::say ("Good Idea's engine failed: " + String (idea.error), true); return; }
        drumkit::patternFrom (k, idea.notes);
        proc.perform ("Random Beat", [this, k] { proc.setKit (k); });
        if (DrumMachine::say) DrumMachine::say (String (idea.name) + ". " + String (idea.summary), false);
    }

    // Random Samples: a one-shot of its kind for each pad from the Sounds tab's folders.
    void fill()
    {
        const auto* index = DrumMachine::sounds ? DrumMachine::sounds() : nullptr;
        if (! index || index->items.empty())
        {
            if (DrumMachine::say) DrumMachine::say ("Random Samples chooses from your sounds: choose their folders in the browser's Sounds tab first.", true);
            return;
        }
        auto k = proc.kit();
        const auto chosen = drumkit::randomSamples (k, *index, (unsigned) Random::getSystemRandom().nextInt (1 << 30) + 1u);
        int filled = 0, synths = 0;
        for (size_t i = 0; i < k.pads.size() && i < chosen.size(); ++i)
        {
            if (k.pads[i].hasPlugin()) { ++synths; continue; }
            if (chosen[i].empty()) continue;
            k.pads[i].sample = chosen[i];
            ++filled;
        }
        if (filled == 0)
        {
            if (DrumMachine::say) DrumMachine::say ("No one-shot sounds were found in your folders for these pads.", true);
            return;
        }
        proc.perform ("Random Samples", [this, k] { proc.setKit (k); });
        if (DrumMachine::say)
            DrumMachine::say ("Random Samples: " + String (filled) + (filled == 1 ? " pad" : " pads") + " given a sound from your folders"
                              + (synths > 0 ? ", " + String (synths) + " with plug-ins kept" : String()) + ". Click again for others; Undo for the last.", false);
    }

    void kitChange (const String& label, std::function<void (drumkit::Kit&)> f)
    {
        auto k = proc.kit();
        f (k);
        drumkit::matchLanes (k);
        proc.perform (label, [this, k] { proc.setKit (k); });
    }

    void paint (Graphics& g) override
    {
        g.fillAll (theme::sunken);
        g.setColour (theme::textSecondary);
        g.setFont (theme::font (theme::smallPoints));
        g.drawText ("Swing", swing.getBounds().translated (swing.getWidth() + 2, 0).withWidth (40), Justification::centredLeft, false);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (8);
        auto top = r.removeFromTop (theme::px (24));
        auto place = [&] (Component& c, int w) { c.setBounds (top.removeFromLeft (w)); top.removeFromLeft (4); };
        place (playPattern, playPattern.idealWidth (theme::smallControl));
        place (steps, steps.idealWidth (theme::smallControl));
        place (swing, theme::px (24));
        top.removeFromLeft (40);
        place (randomBeat, randomBeat.idealWidth (theme::smallControl));
        place (randomSamples, randomSamples.idealWidth (theme::smallControl));
        place (clear, clear.idealWidth (theme::smallControl));
        place (addPad, addPad.idealWidth (theme::smallControl));
        place (dragOut, dragOut.idealWidth (theme::smallControl));
        r.removeFromTop (8);
        const int stepsWidth = proc.kit().pattern.steps * theme::px (22);
        auto list = r.removeFromLeft (jmin (r.getWidth(), theme::px (nameWidth + sourceWidth + 3 * smallWidth) + 16 + stepsWidth));
        for (auto* row : rows) row->setBounds (list.removeFromTop (theme::px (rowHeight)));
        if (inner)
        {
            r.removeFromLeft (8);
            if (inner->isResizable()) inner->setBounds (r);
            else inner->setTopLeftPosition (r.getTopLeft());
        }
    }

    DrumMachine& proc;
    theme::HouseButton playPattern { "Play Pattern", "Play the pattern below while the song plays, from the song's start, round the loop. "
                                                     "Switch it off when the beat is in clips on the timeline.", true };
    theme::HouseButton steps { "16 Steps", "How many steps the pattern has, and how long each is." };
    theme::HouseButton randomBeat { "Random Beat", "A new groove from Good Idea on these pads, every click (Undo puts the last one back)." };
    theme::HouseButton randomSamples { "Random Samples", "A sound for every pad from your Sounds folders: a kick for the kick, a snare for the snare, "
                                                         "one-shots only. Pads playing plug-ins keep them. Click again for others." };
    theme::HouseButton clear { "Clear", "Take every hit out of the pattern." };
    theme::HouseButton addPad { "+ Pad", "Another pad, on the next drum's note." };
    DragOut dragOut;
    Slider swing;
    drumkit::Kit swingBefore;
    OwnedArray<PadRow> rows;
    Component::SafePointer<AudioProcessorEditor> inner;
    AudioPluginInstance* shownPlugin = nullptr;
    int shown = -1, lastStep = -2;
};
} // namespace

AudioProcessorEditor* DrumMachine::createEditor() { return new DrumMachineEditor (*this); }

} // namespace kal
