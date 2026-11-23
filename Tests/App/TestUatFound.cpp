// What the full UAT of 23 November found (decision 0133), each first as a
// test that failed.
#include "Check.h"
#include "Controller.h"
#include "Edit.h"
#include "Engine.h"
#include "PluginManager.h"
#include "Theme.h"
#include "BuiltinInstruments.h"
#include "AgentTools.h"
#include "Keys.h"
#include "MainComponent.h"
#include "PluginMenu.h"
#include "DrumMachine.h"
#include "PatternEditors.h"
#include "DrumPadsDevice.h"
#include "DeviceRack.h"
#include "LowerZone.h"
#include "JsfxDevice.h"
#include "NoteSplitter.h"
#include "MorphProcessor.h"
#include "PianoRoll.h"
#include "NotationView.h"

using namespace kal;
using namespace juce;

TEST ("questions: a question with a field to type in has the field ready, its text chosen, so typing replaces it and no key reaches the shortcuts (UAT)")
{
    // The UAT: Rename the Section opened, "Verse" was typed, and its letters
    // went to the window: S soloed a track, R started recording.
    Component host;
    host.setSize (800, 600);
    host.addToDesktop (0);
    host.setVisible (true);
    ui::setHost (&host);

    auto* w = new AlertWindow ("Rename the Section", {}, MessageBoxIconType::NoIcon);
    w->addTextEditor ("name", "Section 1");
    w->addButton ("Rename", 1);
    w->addButton ("Cancel", 0);
    ui::present (w, ModalCallbackFunction::create ([] (int) {}), true);
    MessageManager::getInstance()->runDispatchLoopUntil (50);
    auto* field = w->getTextEditor ("name");
    CHECK (field != nullptr);
    if (field != nullptr)
    {
        CHECK (field->hasKeyboardFocus (false));
        CHECK_EQ (field->getHighlightedText(), String ("Section 1"));
    }
    w->exitModalState (0);
    MessageManager::getInstance()->runDispatchLoopUntil (50);
    ui::setHost (nullptr);
    host.removeFromDesktop();
}

TEST ("last touched: switching to B and back, loading a preset or a state, isn't touching: the control last moved by hand stays the one automated (UAT)")
{
    // The UAT: Cutoff turned by hand, then Set B, Set A; Automate the Last
    // Touched Control made a lane for Volume, the last parameter A's state set.
    PluginManager plugins;
    Engine engine (plugins);
    Controller c (engine);
    c.project = Project();
    const Id t = edit::addTrack (c.project, TrackKind::Track, "Bass");
    const Id dev = edit::addDevice (c.project, t, synthRef(), DeviceRole::Instrument);
    c.refresh();
    auto* inst = engine.instance (dev);
    CHECK (inst != nullptr);
    if (! inst) return;
    int cutoff = -1;
    for (int i = 0; i < inst->getParameters().size(); ++i)
        if (inst->getParameters()[i]->getName (32) == "Cutoff") cutoff = i;
    CHECK (cutoff >= 0);
    if (cutoff < 0) return;
    auto* p = inst->getParameters()[cutoff];
    p->beginChangeGesture();
    p->setValueNotifyingHost (0.3f);
    p->endChangeGesture();
    MessageManager::getInstance()->runDispatchLoopUntil (20);
    CHECK_EQ (c.lastTouched.param, cutoff);
    c.switchAB (dev);
    MessageManager::getInstance()->runDispatchLoopUntil (20);
    c.switchAB (dev);
    MessageManager::getInstance()->runDispatchLoopUntil (20);
    CHECK_EQ (c.lastTouched.param, cutoff);
    c.undo();
    MessageManager::getInstance()->runDispatchLoopUntil (20);
    CHECK_EQ (c.lastTouched.param, cutoff);
}

TEST ("keys: Home and End are named as the Mac names them, not 'home' and 'end'; forward Delete isn't shown as Delete (UAT)")
{
    Keys k;
    k.add ("a", "Stop and Go to the Start", KeyPress (KeyPress::homeKey), [] {});
    k.add ("b", "Go to the End", KeyPress (KeyPress::endKey), [] {});
    k.add ("c", "Delete", KeyPress (KeyPress::backspaceKey), [] {});
    k.add ("d", "Delete (Forward)", KeyPress (KeyPress::deleteKey), [] {});
    CHECK (k.textFor ("a") != "home" && k.textFor ("a").isNotEmpty());
    CHECK (k.textFor ("b") != "end" && k.textFor ("b").isNotEmpty());
    CHECK (k.textFor ("a").substring (0, 1) != "h");
    CHECK (k.textFor ("d") != k.textFor ("c"));
}

TEST ("agents: one note added is 'Added 1 note', not '1 notes' (UAT)")
{
    PluginManager plugins;
    Engine engine (plugins);
    Controller c (engine);
    c.project = Project();
    const Id t = edit::addTrack (c.project, TrackKind::Track, "Lead");
    const Id clip = edit::addMidiClip (c.project, t, 0, PPQ * 4);
    c.refresh();
    auto server = makeAgentServer (c);
    const auto reply = server->handle ("{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"tools/call\",\"params\":{\"name\":\"add_notes\",\"arguments\":{\"clip\":"
                                       + std::to_string (clip) + ",\"notes\":[{\"pitch\":60,\"start_beat\":0,\"length_beats\":1}]}}}");
    CHECK (reply.find ("Added 1 note:") != std::string::npos);
}

TEST ("top bar: with tempo changes its label 'Tempo (changes)' fits its box, not 'Tempo (chan' (UAT)")
{
    theme::LookAndFeel look;
    LookAndFeel::setDefaultLookAndFeel (&look);
    {
        PluginManager plugins;
        Engine engine (plugins);
        Controller c (engine);
        c.project = Project();
        MainComponent w (c);
        w.setSize (1440, 900);
        CHECK (w.topBar().tempoLabelFits());
        c.apply ("Tempo", [] (Project& p) { p.tempo.setTempoAt (PPQ * 32, 90.0); });
        MessageManager::getInstance()->runDispatchLoopUntil (100);
        CHECK (w.topBar().tempoLabelFits());
    }
    LookAndFeel::setDefaultLookAndFeel (nullptr);
}

TEST ("export: a mix over full scale written in whole numbers (24-bit) says it clipped, and by how much; at 32-bit float it doesn't (UAT)")
{
    // The UAT: the mix peaked at +12 dBTP; the 24-bit WAV came out clipped,
    // and "Exported Full UAT.wav." said nothing of it.
    const auto folder = File::getSpecialLocation (File::tempDirectory).getChildFile ("kalibre-uat-clip");
    folder.createDirectory();
    const auto click = folder.getChildFile ("click.wav");
    click.deleteFile();
    {
        AudioBuffer<float> b (2, 48000);
        b.clear();
        b.setSample (0, 24000, 1.0f);
        b.setSample (1, 24000, 1.0f);
        WavAudioFormat wav;
        std::unique_ptr<OutputStream> os (click.createOutputStream().release());
        auto w = wav.createWriterFor (os, AudioFormatWriterOptions().withSampleRate (48000).withNumChannels (2).withBitsPerSample (32));
        w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
    }
    PluginManager plugins;
    Engine engine (plugins);
    Controller c (engine);
    c.project = Project();
    for (const char* name : { "One", "Two" })
    {
        const Id t = edit::addTrack (c.project, TrackKind::Track, name);
        Clip clip;
        clip.kind = Clip::Kind::Audio;
        clip.start = 0;
        clip.length = PPQ * 2;
        clip.audio.file = click.getFullPathName().toStdString();
        clip.audio.sourceLength = 1.0;
        edit::addClip (c.project, t, clip);
        c.project.track (t)->pan = 0;
    }
    c.refresh();
    Engine::RenderOptions o;
    o.start = 0;
    o.end = 1.0;
    o.tail = 0;
    o.dither = false;
    o.bitDepth = 24;
    CHECK (c.exportAudio (folder.getChildFile ("loud.wav"), o, false));
    CHECK (c.message.contains ("clip"));
    o.bitDepth = 32;
    CHECK (c.exportAudio (folder.getChildFile ("loud32.wav"), o, false));
    CHECK (! c.message.contains ("clip"));
}

TEST ("plug-in slots: with nothing scanned, a slot's menu offers Kalibre's own devices of its kind, never one that holds plug-ins itself (the musician's answer to the UAT)")
{
    PluginManager plugins;
    auto names = [] (const PopupMenu& m)
    {
        StringArray out;
        std::function<void (const PopupMenu&)> walk = [&] (const PopupMenu& x)
        {
            for (PopupMenu::MenuItemIterator it (x); it.next();)
            {
                if (it.getItem().subMenu) walk (*it.getItem().subMenu);
                else if (! it.getItem().isSectionHeader && ! it.getItem().isSeparator) out.add (it.getItem().text);
            }
        };
        walk (m);
        return out;
    };
    const auto instruments = names (pluginmenu::forSlot (plugins, { DeviceRole::Instrument }, [] (const PluginRef&, DeviceRole) {}));
    CHECK (instruments.contains ("Kalibre Synth"));
    CHECK (instruments.contains ("Kalibre Sampler"));
    CHECK (instruments.contains ("Trombolese"));
    CHECK (! instruments.joinIntoString ("|").contains ("Drum Pads"));
    CHECK (! instruments.joinIntoString ("|").contains ("X/Y Morph"));
    CHECK (! instruments.joinIntoString ("|").contains ("Scan for plug-ins first"));
    const auto effects = names (pluginmenu::forSlot (plugins, { DeviceRole::AudioFx }, [] (const PluginRef&, DeviceRole) {}));
    CHECK (effects.contains ("Reverberator"));
    CHECK (! effects.joinIntoString ("|").contains ("Container"));
    CHECK (! effects.joinIntoString ("|").contains ("Multiband"));
    CHECK (! effects.contains ("Kalibre Synth"));
    // And one chosen plays in a slot: Reverberator in a Container.
    PluginRef chosen;
    for (const auto& d : pluginmenu::ownDevices())
        if (d.name == "Reverberator") chosen = d.ref;
    String error;
    CHECK (plugins.create (chosen, 48000, 512, error) != nullptr);
}

TEST ("drums: right-clicking a pad's name opens its menu, as right-clicking the row does (UAT)")
{
    theme::LookAndFeel look;
    LookAndFeel::setDefaultLookAndFeel (&look);
    {
        PluginManager plugins;
        DrumMachine d (plugins);
        std::unique_ptr<AudioProcessorEditor> ed (d.createEditor());
        ed->setSize (1100, 400);
        ed->addToDesktop (0);
        ed->setVisible (true);
        Button* name = nullptr;
        std::function<void (Component&)> find = [&] (Component& in)
        {
            for (auto* ch : in.getChildren())
            {
                if (auto* b = dynamic_cast<Button*> (ch); b && b->getButtonText() == "Kick" && ! name) name = b;
                find (*ch);
            }
        };
        find (*ed);
        CHECK (name != nullptr);
        if (name)
        {
            const MouseEvent e (Desktop::getInstance().getMainMouseSource(), { 5.0f, 5.0f },
                                ModifierKeys (ModifierKeys::rightButtonModifier | ModifierKeys::popupMenuClickModifier),
                                MouseInputSource::defaultPressure, 0, 0, 0, 0, name, name, Time::getCurrentTime(), { 5.0f, 5.0f },
                                Time::getCurrentTime(), 1, false);
            static_cast<Component*> (name)->mouseDown (e);
            MessageManager::getInstance()->runDispatchLoopUntil (50);
            CHECK (Component::getCurrentlyModalComponent() != nullptr);
            PopupMenu::dismissAllActiveMenus();
            MessageManager::getInstance()->runDispatchLoopUntil (50);
        }
        d.editorBeingDeleted (ed.get());
    }
    LookAndFeel::setDefaultLookAndFeel (nullptr);
}

TEST ("drums: the pattern dragged to the timeline keeps its swing and gate, so the drum grid shows them and editing keeps them (UAT)")
{
    PluginManager plugins;
    Engine engine (plugins);
    Controller c (engine);
    c.project = Project();
    const Id t = edit::addTrack (c.project, TrackKind::Track, "Drums");
    const Id dev = edit::addDevice (c.project, t, drumsRef(), DeviceRole::Instrument);
    c.refresh();
    auto* d = dynamic_cast<DrumMachine*> (engine.instance (dev));
    CHECK (d != nullptr);
    if (! d) return;
    auto k = d->kit();
    k.pattern.lanes[0].velocity[1] = 120;
    k.pattern.swing = 0.3;
    k.pattern.gate = 0.5;
    d->setKit (k);
    const Id made = c.dropDrumPattern (d, t, 0);
    const auto* clip = c.project.clip (made);
    CHECK (clip != nullptr);
    if (! clip) return;
    CHECK (std::abs (clip->drumsFeel.swing - 0.3) < 1.0e-9);
    CHECK (std::abs (clip->drumsFeel.gate - 0.5) < 1.0e-9);
}

TEST ("euclidean: every row's dials line up and fit, each named (Steps, Hits, Rotate) (UAT)")
{
    theme::LookAndFeel look;
    LookAndFeel::setDefaultLookAndFeel (&look);
    {
        PluginManager plugins;
        Engine engine (plugins);
        Controller c (engine);
        c.project = Project();
        EuclidEditor euclid (c);
        euclid.setSize (1100, euclid.preferredHeight());
        std::vector<Slider*> dials;
        for (auto* ch : euclid.getChildren())
            if (auto* s = dynamic_cast<Slider*> (ch)) dials.push_back (s);
        CHECK (dials.size() >= 6);
        std::set<int> xs;
        for (auto* d : dials)
        {
            xs.insert (d->getX());
            CHECK (euclid.getLocalBounds().contains (d->getBounds()));
            CHECK (d->getHeight() == dials.front()->getHeight());
            CHECK (d->getTitle() == "Steps" || d->getTitle() == "Hits" || d->getTitle() == "Rotate");
            CHECK (d->getY() >= theme::px (16));   // room for its name over it
        }
        CHECK_EQ (xs.size(), size_t (3));   // three columns, every row the same
    }
    LookAndFeel::setDefaultLookAndFeel (nullptr);
}

TEST ("drum pads: in a short lower zone every one of the sixteen pads shown is whole, the bottom row too (UAT)")
{
    theme::LookAndFeel look;
    LookAndFeel::setDefaultLookAndFeel (&look);
    {
        PluginManager plugins;
        String error;
        auto inst = plugins.create (drumPadsRef(), 48000, 512, error);
        CHECK (inst != nullptr);
        if (! inst) return;
        std::unique_ptr<AudioProcessorEditor> ed (inst->createEditorIfNeeded());
        ed->setSize (1120, 220);
        int tiles = 0;
        for (auto* ch : ed->getChildren())
            if (ch->getWidth() >= theme::px (100) && ch->getHeight() >= theme::px (30) && ch->getHeight() < theme::px (100)
                && dynamic_cast<Button*> (ch) == nullptr && dynamic_cast<Slider*> (ch) == nullptr)
            {
                ++tiles;
                CHECK (ed->getLocalBounds().contains (ch->getBounds()));
            }
        CHECK (tiles >= 16);
        inst->editorBeingDeleted (ed.get());
    }
    LookAndFeel::setDefaultLookAndFeel (nullptr);
}

TEST ("device rack: every device's name shows over its panel: X/Y Morph, ScaleView, the Note Splitter, and one switched off (UAT)")
{
    theme::LookAndFeel look;
    LookAndFeel::setDefaultLookAndFeel (&look);
    {
        PluginManager plugins;
        Engine engine (plugins);
        Controller c (engine);
        c.project = Project();
        const Id t = edit::addTrack (c.project, TrackKind::Track, "Chords");
        c.focusTrack = t;
        Id ids[4] {};
        c.apply ("Add", [&] (Project& p)
        {
            ids[0] = edit::addDevice (p, t, scaleViewRef(), DeviceRole::MidiFx);
            ids[1] = edit::addDevice (p, t, noteSplitterRef(), DeviceRole::MidiFx);
            ids[2] = edit::addDevice (p, t, morphRef(), DeviceRole::Instrument);
        });
        const Id t2 = edit::addTrack (c.project, TrackKind::Track, "Off");
        c.apply ("Add", [&] (Project& p) { ids[3] = edit::addDevice (p, t2, synthRef(), DeviceRole::Instrument); p.device (ids[3])->active = false; });
        DeviceRack rack (c);
        rack.setSize (1400, 300);
        MessageManager::getInstance()->runDispatchLoopUntil (50);
        for (int i = 0; i < 3; ++i) CHECK (rack.nameFits (ids[i]));
        c.focusTrack = t2;
        c.sendChangeMessage();
        MessageManager::getInstance()->runDispatchLoopUntil (50);
        CHECK (rack.nameFits (ids[3]));
    }
    LookAndFeel::setDefaultLookAndFeel (nullptr);
}

TEST ("modulators: an LFO's knobs are named Rate, Phase, Both Ways and Amount, over each (UAT)")
{
    theme::LookAndFeel look;
    LookAndFeel::setDefaultLookAndFeel (&look);
    {
        PluginManager plugins;
        Engine engine (plugins);
        Controller c (engine);
        c.project = Project();
        const Id t = edit::addTrack (c.project, TrackKind::Track, "Bass");
        const Id dev = edit::addDevice (c.project, t, synthRef(), DeviceRole::Instrument);
        c.focusTrack = t;
        c.refresh();
        auto* inst = engine.instance (dev);
        CHECK (inst != nullptr);
        if (! inst) return;
        auto* p = inst->getParameters()[0];
        p->beginChangeGesture();
        p->setValueNotifyingHost (0.4f);
        p->endChangeGesture();
        MessageManager::getInstance()->runDispatchLoopUntil (20);
        c.modulateLastTouched (ModKind::Lfo);
        DeviceRack rack (c);
        rack.setSize (1400, 300);
        MessageManager::getInstance()->runDispatchLoopUntil (50);
        StringArray titles;
        std::function<void (Component&)> find = [&] (Component& in)
        {
            for (auto* ch : in.getChildren())
            {
                if (auto* s = dynamic_cast<Slider*> (ch)) titles.add (s->getTitle());
                find (*ch);
            }
        };
        find (rack);
        for (auto* name : { "Rate", "Phase", "Both Ways", "Amount" }) CHECK (titles.contains (name));
    }
    LookAndFeel::setDefaultLookAndFeel (nullptr);
}

TEST ("sampler: in a short lower zone it lays itself out, its knobs in one row, big enough to turn, names clear of the knobs (UAT)")
{
    theme::LookAndFeel look;
    LookAndFeel::setDefaultLookAndFeel (&look);
    {
        PluginManager plugins;
        String error;
        auto inst = plugins.create (samplerRef(), 48000, 512, error);
        CHECK (inst != nullptr);
        if (! inst) return;
        std::unique_ptr<AudioProcessorEditor> ed (inst->createEditorIfNeeded());
        // As short as the rack gives it in a short zone: it takes the size, not shown smaller.
        ed->setSize (900, 170);
        CHECK_EQ (ed->getHeight(), 170);
        std::vector<Slider*> knobs;
        std::vector<Label*> labels;
        std::function<void (Component&)> find = [&] (Component& in)
        {
            for (auto* ch : in.getChildren())
            {
                if (! ch->isVisible()) continue;
                if (auto* s = dynamic_cast<Slider*> (ch)) knobs.push_back (s);
                if (auto* l = dynamic_cast<Label*> (ch); l && dynamic_cast<Slider*> (l->getParentComponent()) == nullptr) labels.push_back (l);
                find (*ch);
            }
        };
        find (*ed);
        CHECK (knobs.size() >= 8);
        for (auto* k : knobs)
        {
            CHECK (k->getHeight() >= 40);   // a dial and its value
            const auto kb = ed->getLocalArea (k->getParentComponent(), k->getBounds());
            CHECK (ed->getLocalBounds().contains (kb));
            for (auto* l : labels)
                CHECK (! ed->getLocalArea (l->getParentComponent(), l->getBounds()).intersects (kb));
        }
        inst->editorBeingDeleted (ed.get());
    }
    LookAndFeel::setDefaultLookAndFeel (nullptr);
}

TEST ("sampler: an Off/On knob (Loop) switches with a short drag, not two hundred points (UAT)")
{
    theme::LookAndFeel look;
    LookAndFeel::setDefaultLookAndFeel (&look);
    {
        PluginManager plugins;
        String error;
        auto inst = plugins.create (samplerRef(), 48000, 512, error);
        CHECK (inst != nullptr);
        if (! inst) return;
        std::unique_ptr<AudioProcessorEditor> ed (inst->createEditorIfNeeded());
        ed->setSize (1000, 300);
        Slider* loop = nullptr;
        std::function<void (Component&)> find = [&] (Component& in)
        {
            for (auto* ch : in.getChildren())
            {
                if (auto* s = dynamic_cast<Slider*> (ch); s && s->getTitle() == "Loop") loop = s;
                find (*ch);
            }
        };
        find (*ed);
        CHECK (loop != nullptr);
        if (loop) CHECK (loop->getMouseDragSensitivity() <= 60);
        inst->editorBeingDeleted (ed.get());
    }
    LookAndFeel::setDefaultLookAndFeel (nullptr);
}

TEST ("ReaImGui tools: Good Idea in the rack fills its panel's height, its New Idea and Insert in reach (UAT)")
{
    theme::LookAndFeel look;
    LookAndFeel::setDefaultLookAndFeel (&look);
    {
        PluginManager plugins;
        Engine engine (plugins);
        Controller c (engine);
        c.project = Project();
        const Id t = edit::addTrack (c.project, TrackKind::Track, "Ideas");
        c.focusTrack = t;
        PluginRef goodIdea;
        for (auto& d : pluginmenu::ownDevices()) if (d.name == "Good Idea") goodIdea = d.ref;
        Id dev = 0;
        c.apply ("Add", [&] (Project& p) { dev = edit::addDevice (p, t, goodIdea, DeviceRole::MidiFx); });
        DeviceRack rack (c);
        rack.setSize (1400, 600);
        MessageManager::getInstance()->runDispatchLoopUntil (100);
        rack.resized();
        auto* ed = rack.editorOf (dev);
        CHECK (ed != nullptr);
        if (ed) CHECK (rack.pluginArea (dev).getHeight() >= 600 - theme::px (30) - 40);
    }
    LookAndFeel::setDefaultLookAndFeel (nullptr);
}

TEST ("editor bar: at 1110 points wide (1440 with the browser open) every button shows, CC Lanes and Auto too (UAT)")
{
    theme::LookAndFeel look;
    LookAndFeel::setDefaultLookAndFeel (&look);
    {
        PluginManager plugins;
        Engine engine (plugins);
        Controller c (engine);
        c.project = Project();
        const Id t = edit::addTrack (c.project, TrackKind::Track, "Lead");
        const Id clip = edit::addMidiClip (c.project, t, 0, PPQ * 4);
        c.refresh();
        c.selectClip (clip);
        c.zoneTab = ZoneTab::Editor;
        c.editor = EditorKind::PianoRoll;
        LowerZone zone (c);
        zone.setSize (1110, 400);
        c.sendChangeMessage();
        MessageManager::getInstance()->runDispatchLoopUntil (50);
        zone.resized();
        int shown = 0;
        for (auto* ch : zone.getChildren())
            if (auto* b = dynamic_cast<Button*> (ch); b && b->isVisible())
            {
                ++shown;
                CHECK (zone.getLocalBounds().contains (b->getBounds()));
                CHECK (b->getWidth() >= 30);   // not squeezed to nothing at the end
            }
        CHECK (shown >= 15);
    }
    LookAndFeel::setDefaultLookAndFeel (nullptr);
}

TEST ("piano roll: the lane under the notes, set to Pitch Bend, can be cleared from its menu, one undo step, other lanes kept (UAT)")
{
    PluginManager plugins;
    Engine engine (plugins);
    Controller c (engine);
    c.project = Project();
    const Id t = edit::addTrack (c.project, TrackKind::Track, "Lead");
    const Id clip = edit::addMidiClip (c.project, t, 0, PPQ * 4);
    auto event = [] (Tick at, EventType type, int number, int value)
    {
        MidiEvent e;
        e.tick = at; e.type = type; e.number = number; e.value = value;
        return e;
    };
    auto& evs = c.project.clip (clip)->midi.events;
    evs.push_back (event (0, EventType::PitchBend, 128, 12000));
    evs.push_back (event (PPQ, EventType::PitchBend, 128, 4000));
    evs.push_back (event (PPQ, EventType::Controller, 1, 90));
    c.refresh();
    c.selectClip (clip);
    PianoRoll roll (c);
    roll.setSize (1000, 600);
    roll.showLane (128);
    const auto steps = c.history.size();
    roll.clearLane();
    const auto& now = c.project.clip (clip)->midi.events;
    CHECK_EQ (now.size(), size_t (1));
    CHECK (now.front().type == EventType::Controller);
    CHECK_EQ (c.history.size(), steps + 1);
}

TEST ("notation: four bars fill the staff's width, not a tenth of it each (UAT)")
{
    theme::LookAndFeel look;
    LookAndFeel::setDefaultLookAndFeel (&look);
    {
        PluginManager plugins;
        Engine engine (plugins);
        Controller c (engine);
        c.project = Project();
        const Id t = edit::addTrack (c.project, TrackKind::Track, "Flute");
        const Id clip = edit::addMidiClip (c.project, t, 0, PPQ * 16);
        for (int b = 0; b < 4; ++b) edit::addNote (c.project, clip, { 0, PPQ * 4 * b, PPQ * 4, 72, 90 });
        c.refresh();
        c.selectClip (clip);
        NotationView view (c);
        view.setSize (1400, 500);
        c.sendChangeMessage();
        MessageManager::getInstance()->runDispatchLoopUntil (50);
        view.createComponentSnapshot (view.getLocalBounds());
        const float end = view.xOfTick (PPQ * 16);
        CHECK (end > 1400 * 0.75f);
        CHECK (end <= 1400.0f);
    }
    LookAndFeel::setDefaultLookAndFeel (nullptr);
}
