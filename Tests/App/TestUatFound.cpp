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
