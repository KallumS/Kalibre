// Drum Pads (decision 0128): 128 pads, a note each; samples and plug-ins;
// volume, pan, mute, solo and choke groups; its window of sixteen; undo.
#include "Check.h"
#include "BuiltinInstruments.h"
#include "Controller.h"
#include "DrumPadsDevice.h"
#include "Edit.h"
#include "Engine.h"
#include "PatternEditors.h"
#include "Theme.h"

using namespace kal;
using namespace juce;

namespace
{
File padsFolder()
{
    auto f = File::getSpecialLocation (File::tempDirectory).getChildFile ("KalibreTests").getChildFile ("drum pads");
    f.createDirectory();
    return f;
}

// A sound: `seconds` at 48 kHz, every sample `level`.
File flat (const String& name, double seconds, float level)
{
    const auto f = padsFolder().getChildFile (name + ".wav");
    f.deleteFile();
    AudioBuffer<float> b (2, (int) (seconds * 48000));
    for (int c = 0; c < 2; ++c) for (int i = 0; i < b.getNumSamples(); ++i) b.setSample (c, i, level);
    WavAudioFormat wav;
    std::unique_ptr<OutputStream> os (f.createOutputStream().release());
    auto w = wav.createWriterFor (os, AudioFormatWriterOptions().withSampleRate (48000).withNumChannels (2).withBitsPerSample (32));
    w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
    return f;
}

std::string path (const File& f) { return f.getFullPathName().toStdString(); }

// One block of 512 with these notes on, at these samples.
AudioBuffer<float> hit (DrumPadsProcessor& d, std::vector<std::pair<int, int>> notesAt, int velocity = 127)
{
    AudioBuffer<float> buf (2, 512);
    MidiBuffer m;
    for (auto [note, at] : notesAt) m.addEvent (MidiMessage::noteOn (1, note, (uint8) velocity), at);
    d.processBlock (buf, m);
    return buf;
}

std::vector<DragAndDropTarget*> tilesOf (Component& ed)
{
    std::vector<DragAndDropTarget*> out;
    for (auto* ch : ed.getChildren()) if (auto* t = dynamic_cast<DragAndDropTarget*> (ch)) out.push_back (t);
    return out;
}

Button* buttonNamed (Component& in, const String& text)
{
    for (auto* ch : in.getChildren())
    {
        if (auto* b = dynamic_cast<Button*> (ch); b && b->getButtonText() == text) return b;
        if (auto* b = buttonNamed (*ch, text)) return b;
    }
    return nullptr;
}
} // namespace

TEST ("drum pads: each of the 128 pads plays on its own note, as loud as the hit and its volume, panned (0128)")
{
    PluginManager plugins;
    DrumPadsProcessor d (plugins);
    d.prepareToPlay (48000, 512);
    auto k = d.kit();
    k.pads[0].sample = path (flat ("low", 0.1, 0.5f));
    k.pads[36].sample = path (flat ("kick", 0.1, 0.5f));
    k.pads[36].volumeDb = -6.0;
    k.pads[36].pan = -1.0;
    k.pads[127].sample = path (flat ("high", 0.1, 0.25f));
    d.setKit (k);
    CHECK (d.padSounds (0) && d.padSounds (36) && d.padSounds (127));
    CHECK (! d.padSounds (37));
    auto buf = hit (d, { { 36, 100 } });
    CHECK_EQ (buf.getSample (0, 99), 0.0f);
    const float want = 0.5f * Decibels::decibelsToGain (-6.0f) * MathConstants<float>::sqrt2;   // full left, constant power
    CHECK (std::abs (buf.getSample (0, 100) - want) < 1e-5f);
    CHECK (std::abs (buf.getSample (1, 100)) < 1e-7f);
    d.reset();
    buf = hit (d, { { 127, 0 } }, 64);
    CHECK (std::abs (buf.getSample (0, 0) - 0.25f * 64.0f / 127.0f) < 1e-6f);
    CHECK (std::abs (buf.getSample (1, 0) - 0.25f * 64.0f / 127.0f) < 1e-6f);
    d.reset();
    buf = hit (d, { { 0, 0 } });
    CHECK_EQ (buf.getSample (0, 0), 0.5f);
    // A note with no sound on its pad: nothing.
    d.reset();
    buf = hit (d, { { 60, 0 } });
    CHECK_EQ (buf.getMagnitude (0, 512), 0.0f);
}

TEST ("drum pads: a muted pad is silent; soloed pads are the only ones heard (0128)")
{
    PluginManager plugins;
    DrumPadsProcessor d (plugins);
    d.prepareToPlay (48000, 512);
    auto k = d.kit();
    k.pads[36].sample = path (flat ("kick", 0.1, 0.5f));
    k.pads[38].sample = path (flat ("snare", 0.1, 0.25f));
    k.pads[38].mute = true;
    d.setKit (k);
    auto buf = hit (d, { { 36, 0 }, { 38, 0 } });
    CHECK_EQ (buf.getSample (0, 10), 0.5f);          // the kick alone
    k.pads[38].mute = false;
    k.pads[38].solo = true;
    d.setKit (k);
    d.reset();
    buf = hit (d, { { 36, 0 }, { 38, 0 } });
    CHECK_EQ (buf.getSample (0, 10), 0.25f);         // the snare alone
    k.pads[36].solo = true;
    d.setKit (k);
    d.reset();
    buf = hit (d, { { 36, 0 }, { 38, 0 } });
    CHECK_EQ (buf.getSample (0, 10), 0.75f);         // both
}

TEST ("drum pads: a pad in a choke group stops the others in it, fading out in 2 ms; a pad outside it plays on (0128)")
{
    PluginManager plugins;
    DrumPadsProcessor d (plugins);
    d.prepareToPlay (48000, 512);
    auto k = d.kit();
    CHECK (k.pads[42].choke > 0 && k.pads[42].choke == k.pads[46].choke);
    k.pads[46].sample = path (flat ("open hat", 1.0, 0.25f));
    k.pads[42].sample = path (flat ("closed hat", 0.001, 0.0f));   // silent: what's heard is the open hat
    k.pads[49].sample = path (flat ("crash", 1.0, 0.5f));
    d.setKit (k);
    auto buf = hit (d, { { 46, 0 }, { 49, 0 }, { 42, 200 } });
    CHECK_EQ (buf.getSample (0, 199), 0.75f);
    const float during = buf.getSample (0, 230) - 0.5f;               // fading
    CHECK (during > 0.0f && during < 0.25f);
    CHECK_EQ (buf.getSample (0, 200 + 96), 0.5f);                    // 2 ms on: the crash alone
    CHECK_EQ (buf.getSample (0, 511), 0.5f);
    // The fade falls all the way: never louder later than earlier.
    for (int i = 201; i < 296; ++i) CHECK (buf.getSample (0, i) <= buf.getSample (0, i - 1));
    // Out of the group: the open hat rings on.
    k.pads[46].choke = 0;
    d.setKit (k);
    d.reset();
    buf = hit (d, { { 46, 0 }, { 42, 200 } });
    CHECK_EQ (buf.getSample (0, 400), 0.25f);
}

TEST ("drum pads: a pad can be a plug-in, sent its note on each hit; its settings are kept with the kit (0128)")
{
    PluginManager plugins;
    DrumPadsProcessor d (plugins);
    d.prepareToPlay (48000, 512);
    auto k = d.kit();
    k.pads[39].plugin = synthRef();
    k.pads[39].sample = path (flat ("unused", 0.1, 0.5f));   // the plug-in wins
    d.setKit (k);
    auto* synth = d.padPlugin (39);
    CHECK (synth != nullptr);
    if (! synth) return;
    CHECK (d.padSounds (39));
    synth->getParameters()[3]->setValueNotifyingHost (0.81f);
    float loudest = 0;
    for (int b = 0; b < 10; ++b)
    {
        auto buf = hit (d, b == 0 ? std::vector<std::pair<int, int>> { { 39, 0 } } : std::vector<std::pair<int, int>> {}, 110);
        loudest = jmax (loudest, buf.getMagnitude (0, 512));
    }
    CHECK (loudest > 0.01f);
    // Muted, the plug-in is silent.
    k.pads[39].mute = true;
    d.setKit (k);
    CHECK (d.padPlugin (39) == synth);   // the same instance kept
    d.reset();
    float muted = 0;
    for (int b = 0; b < 10; ++b) muted = jmax (muted, hit (d, b == 0 ? std::vector<std::pair<int, int>> { { 39, 0 } } : std::vector<std::pair<int, int>> {}).getMagnitude (0, 512));
    CHECK_EQ (muted, 0.0f);
    // Saved and opened in another: the synth there, with its setting.
    MemoryBlock state;
    d.getStateInformation (state);
    DrumPadsProcessor again (plugins);
    again.prepareToPlay (48000, 512);
    again.setStateInformation (state.getData(), (int) state.getSize());
    CHECK (again.kit().pads[39].hasPlugin());
    CHECK (again.kit().pads[39].mute);
    CHECK (again.padPlugin (39) != nullptr);
    if (again.padPlugin (39)) CHECK (std::abs (again.padPlugin (39)->getParameters()[3]->getValue() - 0.81f) < 0.001f);
}

TEST ("drum pads: an edit is one undo step; on its track the drum grid's rows are the pads with sounds, and its clips open there (0128)")
{
    theme::LookAndFeel look;
    LookAndFeel::setDefaultLookAndFeel (&look);
    {
        PluginManager plugins;
        Engine engine (plugins);
        Controller c (engine);
        c.project = Project();
        const Id t = edit::addTrack (c.project, TrackKind::Track, "Pads");
        const Id dev = edit::addDevice (c.project, t, drumPadsRef(), DeviceRole::Instrument);
        const Id clip = edit::addMidiClip (c.project, t, 0, PPQ * 4);
        c.refresh();
        auto* d = dynamic_cast<DrumPadsProcessor*> (engine.instance (dev));
        CHECK (d != nullptr);
        if (! d) return;
        CHECK_EQ (c.deviceOf (d), dev);
        const auto steps = c.history.size();
        auto k = d->kit();
        k.pads[36].sample = path (flat ("kick", 0.1, 0.5f));
        k.pads[38].sample = path (flat ("snare", 0.1, 0.5f));
        k.pads[60].sample = path (flat ("perc", 0.1, 0.5f));
        k.pads[60].name = "Shaker Loop Hit";
        d->perform ("Pad Sample", [d, k] { d->setKit (k); });
        CHECK_EQ (c.history.size(), steps + 1);
        c.undo();
        CHECK (d->kit().pads[36].sample.empty());
        c.redo();
        CHECK (d->padSounds (60));

        const auto kit = c.drumKitOn (t);
        CHECK (kit.has_value());
        if (kit)
        {
            CHECK_EQ (kit->pads.size(), (size_t) 3);
            CHECK_EQ (kit->pads[0].note, 36);
            CHECK_EQ (kit->pads[2].name, std::string ("Shaker Loop Hit"));
        }
        c.editor = EditorKind::PianoRoll;
        c.selectClip (clip);
        CHECK (c.editor == EditorKind::Drums);
        DrumGrid grid (c);
        grid.setSize (900, 400);
        CHECK_EQ (grid.preferredHeight(), 3 * theme::px (24) + 8 + theme::px (34));
    }
    LookAndFeel::setDefaultLookAndFeel (nullptr);
}

TEST ("drum pads: its window shows sixteen pads, the lowest at the bottom left; any of the 128 can be shown; sounds dropped on a pad play it (0128)")
{
    theme::LookAndFeel look;
    LookAndFeel::setDefaultLookAndFeel (&look);
    {
        PluginManager plugins;
        Engine engine (plugins);
        Controller c (engine);
        c.project = Project();
        const Id t = edit::addTrack (c.project, TrackKind::Track, "Pads");
        const Id dev = edit::addDevice (c.project, t, drumPadsRef(), DeviceRole::Instrument);
        c.refresh();
        auto* d = dynamic_cast<DrumPadsProcessor*> (engine.instance (dev));
        CHECK (d != nullptr);
        if (! d) return;
        std::unique_ptr<AudioProcessorEditor> ed (d->createEditor());
        ed->setSize (1120, 300);
        auto tiles = tilesOf (*ed);
        CHECK_EQ (tiles.size(), (size_t) 16);
        if (tiles.size() != 16) return;
        auto* first = dynamic_cast<Component*> (tiles[0]);
        auto* last = dynamic_cast<Component*> (tiles[15]);
        CHECK (first->getY() > last->getY() && first->getX() < last->getX());   // 36 at the bottom left, 51 at the top right

        // A sound from the Sounds tab dropped on the snare (the third pad shown: 38).
        const auto steps = c.history.size();
        const auto snare = flat ("Dropped Snare", 0.2, 0.3f);
        DragAndDropTarget::SourceDetails drop (var ("file:" + snare.getFullPathName()), nullptr, {});
        CHECK (tiles[2]->isInterestedInDragSource (drop));
        tiles[2]->itemDropped (drop);
        CHECK_EQ (String (d->kit().pads[38].sample), snare.getFullPathName());
        CHECK (d->padSounds (38));
        CHECK_EQ (c.history.size(), steps + 1);
        CHECK_EQ (d->selected, 38);

        // The top sixteen: the last pad shown is 127; three sounds from the Finder fill it and those above it, as far as there are pads.
        d->setView (200);
        CHECK_EQ (d->kit().view, 112);
        auto* fd = dynamic_cast<FileDragAndDropTarget*> (tiles[13]);
        CHECK (fd != nullptr);
        if (! fd) return;
        StringArray three { flat ("a", 0.1, 0.1f).getFullPathName(), flat ("b", 0.1, 0.1f).getFullPathName(), flat ("c", 0.1, 0.1f).getFullPathName(),
                            flat ("d", 0.1, 0.1f).getFullPathName() };
        fd->filesDropped (three, 0, 0);
        CHECK (String (d->kit().pads[125].sample).endsWith ("a.wav"));
        CHECK (String (d->kit().pads[127].sample).endsWith ("c.wav"));
        CHECK_EQ (c.history.size(), steps + 2);
        // The view isn't an undo step, but goes with the song.
        MemoryBlock state;
        d->getStateInformation (state);
        DrumPadsProcessor other (plugins);
        other.setStateInformation (state.getData(), (int) state.getSize());
        CHECK_EQ (other.kit().view, 112);

        // M and S on a tile: one undo step each.
        d->setView (36);
        ed->resized();
        auto* m = buttonNamed (*dynamic_cast<Component*> (tiles[0]), "M");
        CHECK (m != nullptr);
        if (m) { m->triggerClick(); MessageManager::getInstance()->runDispatchLoopUntil (20); }
        CHECK (d->kit().pads[36].mute);
        CHECK_EQ (c.history.size(), steps + 3);
        auto* unsolo = buttonNamed (*ed, "Unsolo All");
        CHECK (unsolo != nullptr && ! unsolo->isEnabled());
        d->editorBeingDeleted (ed.get());
    }
    LookAndFeel::setDefaultLookAndFeel (nullptr);
}

TEST ("drum pads: Random Samples gives each of the sixteen pads shown a drum of its kind, one undo step (0128)")
{
    theme::LookAndFeel look;
    LookAndFeel::setDefaultLookAndFeel (&look);
    {
        PluginManager plugins;
        Engine engine (plugins);
        Controller c (engine);
        c.project = Project();
        const Id t = edit::addTrack (c.project, TrackKind::Track, "Pads");
        const Id dev = edit::addDevice (c.project, t, drumPadsRef(), DeviceRole::Instrument);
        c.refresh();
        auto* d = dynamic_cast<DrumPadsProcessor*> (engine.instance (dev));
        CHECK (d != nullptr);
        if (! d) return;
        library::Index ix;
        for (auto* name : { "Kick 1", "Snare 1", "HH Closed", "HH Open", "Pad Long" })
        {
            const auto f = flat (name, String (name) == "Pad Long" ? 8.0 : 0.2, 0.3f);
            library::Item it;
            it.path = f.getFullPathName().toStdString();
            it.readable = true;
            it.seconds = String (name) == "Pad Long" ? 8.0 : 0.2;
            ix.items[it.path] = it;
        }
        c.soundsIndex = [&ix] { return &ix; };
        std::unique_ptr<AudioProcessorEditor> ed (d->createEditor());
        auto* fill = buttonNamed (*ed, "Random Samples");
        CHECK (fill != nullptr);
        if (! fill) return;
        const auto steps = c.history.size();
        fill->triggerClick();
        MessageManager::getInstance()->runDispatchLoopUntil (50);
        CHECK_EQ (c.history.size(), steps + 1);
        const auto& kit = d->kit();
        CHECK (String (kit.pads[36].sample).contains ("Kick"));
        CHECK (String (kit.pads[38].sample).contains ("Snare"));
        CHECK (String (kit.pads[42].sample).contains ("HH Closed"));
        CHECK (String (kit.pads[46].sample).contains ("HH Open"));
        for (auto& p : kit.pads) CHECK (! String (p.sample).contains ("Pad Long"));
        c.undo();
        CHECK (d->kit().pads[36].sample.empty());
        d->editorBeingDeleted (ed.get());
    }
    LookAndFeel::setDefaultLookAndFeel (nullptr);
}

#include "DeviceRack.h"

TEST ("drum pads: a sound dragged from the browser onto its window in the rack plays the pad it's let go over, handed on by the panel (the Mac's native views, 0116, 0128)")
{
    theme::LookAndFeel look;
    LookAndFeel::setDefaultLookAndFeel (&look);
    {
        PluginManager plugins;
        Engine engine (plugins);
        Controller c (engine);
        c.project = Project();
        const Id t = edit::addTrack (c.project, TrackKind::Track, "Pads");
        c.focusTrack = t;
        c.refresh();
        // The window itself takes the drop, for the pad under the point: on the Mac it's all the panel can hand on to.
        {
            DrumPadsProcessor d (plugins);
            std::unique_ptr<AudioProcessorEditor> ed (d.createEditor());
            ed->setSize (1120, 300);
            auto* target = dynamic_cast<DragAndDropTarget*> (ed.get());
            CHECK (target != nullptr);
            auto tiles = tilesOf (*ed);
            if (target && tiles.size() == 16)
            {
                const auto snare = flat ("Window Snare", 0.1, 0.3f);
                const auto at = dynamic_cast<Component*> (tiles[2])->getBounds().getCentre();   // pad 38
                DragAndDropTarget::SourceDetails drop (var ("file:" + snare.getFullPathName()), nullptr, at);
                CHECK (target->isInterestedInDragSource (drop));
                target->itemDropped (drop);
                CHECK_EQ (String (d.kit().pads[38].sample), snare.getFullPathName());
                // Between the pads: nothing.
                DragAndDropTarget::SourceDetails nowhere (var ("file:" + snare.getFullPathName()), nullptr, { 2, 2 });
                target->itemDropped (nowhere);
                CHECK_EQ (drumpads::sounding (d.kit()).size(), (size_t) 1);
            }
            d.editorBeingDeleted (ed.get());
        }
        // In the rack: its panel hands the drop on.
        DeviceRack rack (c);
        rack.setSize (1400, 360);
        rack.addToDesktop (0);
        rack.setVisible (true);
        Id dev = 0;
        c.apply ("Add", [&] (Project& p) { dev = edit::addDevice (p, t, drumPadsRef(), DeviceRole::Instrument); });
        MessageManager::getInstance()->runDispatchLoopUntil (50);
        auto* d = dynamic_cast<DrumPadsProcessor*> (engine.instance (dev));
        CHECK (d != nullptr);
        DragAndDropTarget* panel = nullptr;
        AudioProcessorEditor* ed = nullptr;
        std::function<void (Component&)> walk = [&] (Component& comp)
        {
            if (auto* e = dynamic_cast<AudioProcessorEditor*> (&comp)) { ed = e; return; }
            if (panel == nullptr && &comp != &rack)
                if (auto* t2 = dynamic_cast<DragAndDropTarget*> (&comp)) panel = t2;
            for (auto* ch : comp.getChildren()) walk (*ch);
        };
        walk (rack);
        CHECK (panel != nullptr && ed != nullptr && d != nullptr);
        if (panel && ed && d)
        {
            auto tiles = tilesOf (*ed);
            CHECK_EQ (tiles.size(), (size_t) 16);
            if (tiles.size() == 16)
            {
                // The kick's pad, as the panel sees it.
                auto* kick = dynamic_cast<Component*> (tiles[0]);
                const auto at = dynamic_cast<Component*> (panel)->getLocalPoint (kick, kick->getLocalBounds().getCentre());
                const auto wav = flat ("Rack Kick", 0.1, 0.3f);
                DragAndDropTarget::SourceDetails drop (var ("file:" + wav.getFullPathName()), nullptr, at);
                CHECK (panel->isInterestedInDragSource (drop));
                panel->itemDropped (drop);
                CHECK_EQ (String (d->kit().pads[36].sample), wav.getFullPathName());
            }
        }
        rack.removeFromDesktop();
    }
    LookAndFeel::setDefaultLookAndFeel (nullptr);
}
