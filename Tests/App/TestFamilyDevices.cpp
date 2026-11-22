// The devices asked for on 19 November (decision 0093): Midular and MIDI Production
// Center with their own REAPER scripts handing them a phrase.
#include "Check.h"
#include "BuiltinInstruments.h"
#include "Controller.h"
#include "Edit.h"
#include "Engine.h"
#include "JsfxDevice.h"
#include "PluginManager.h"
#include "ReaperScripts.h"

using namespace kal;
using namespace juce;

namespace
{
const JsfxSpec& spec (const char* id)
{
    for (auto& s : jsfxDevices()) if (String (s.id) == id) return s;
    return jsfxDevices().front();
}

// The song's position, as the engine gives every plug-in: 120 BPM in 4/4, stopped.
struct StoppedAt120 : AudioPlayHead
{
    int64 samples = 0;
    Optional<PositionInfo> getPosition() const override
    {
        PositionInfo i;
        i.setBpm (120.0);
        i.setTimeSignature (TimeSignature { 4, 4 });
        i.setTimeInSamples (samples);
        i.setTimeInSeconds ((double) samples / 48000.0);
        i.setPpqPosition ((double) samples / 24000.0);
        i.setIsPlaying (false);
        return i;
    }
};

// The device's plug-in played a block at a time, as the audio thread does,
// with `held` keys down; the notes it sent come back.
std::vector<int> play (AudioPluginInstance& p, int blocks, std::initializer_list<int> held = {}, bool press = false)
{
    static StoppedAt120 head;
    p.setPlayHead (&head);
    std::vector<int> pitches;
    AudioBuffer<float> b (2, 512);
    for (int i = 0; i < blocks; ++i)
    {
        MidiBuffer m;
        if (press && i == 0) for (int k : held) m.addEvent (MidiMessage::noteOn (1, k, (uint8) 100), 0);
        b.clear();
        p.processBlock (b, m);
        head.samples += 512;
        for (const auto md : m)
        {
            const auto msg = md.getMessage();
            if (msg.isNoteOn()) pitches.push_back (msg.getNoteNumber());
        }
    }
    return pitches;
}

// A script's deferred work run with the plug-in playing between, until it's done.
void settle (ReaperScripts& scripts, AudioPluginInstance& p)
{
    for (int i = 0; i < 100; ++i)
    {
        play (p, 1);
        bool waiting = false;
        for (auto& r : scripts.runs) { r->step(); waiting = waiting || r->waiting(); }
        if (! waiting) return;
    }
}
} // namespace

TEST ("family devices: Midular takes the chosen clip's notes through its own importer, and plays them from a key")
{
    PluginManager plugins;
    Engine engine (plugins);
    Controller c (engine);
    c.project = Project();
    const Id t = edit::addTrack (c.project, TrackKind::Track, "Phrase");
    const Id dev = edit::addDevice (c.project, t, jsfxRef (spec ("midular")), DeviceRole::MidiFx);
    const Id clip = edit::addMidiClip (c.project, t, PPQ * 4, PPQ * 4);
    for (int i = 0; i < 4; ++i) edit::addNote (c.project, clip, { 0, i * PPQ, PPQ / 2, 72 + i * 3, 100 });
    c.project.clip (clip)->name = "Up in Thirds";
    c.refresh();
    auto* inst = engine.instance (dev);
    CHECK (inst != nullptr);
    if (! inst) return;
    CHECK (inst->getParameters().size() >= 57);   // the importer looks for its 57th, Import Ticket
    inst->prepareToPlay (48000, 512);
    play (*inst, 4);

    // Nothing chosen: said so, nothing run.
    c.selectedClips.clear();
    c.selectedClip = 0;
    CHECK (! c.loadChosenClipsInto (dev));
    CHECK (c.message.contains ("Choose a MIDI clip"));

    c.selectClip (clip, false);
    c.selectedClips = { clip };
    CHECK (c.loadChosenClipsInto (dev));
    settle (*c.reaperScripts, *inst);
    CHECK (c.reaperScripts->runs.empty() || ! c.reaperScripts->runs.front()->waiting());
    // The plug-in took it: the mailbox says so (state 3, its ticket acknowledged).
    const double* state = jsfxGmem ("Midular", 2);
    const double* ticket = jsfxGmem ("Midular", 3);
    const double* ack = jsfxGmem ("Midular", 4);
    CHECK (state && *state == 3.0);
    CHECK (ticket && ack && *ticket == *ack && *ack > 0);
    CHECK (! c.message.contains ("hasn"));   // no "hasn't picked it up yet"

    // Holding the root key plays the phrase: its pitches, in their thirds.
    const auto heard = play (*inst, 200, { 60 }, true);
    std::set<int> pitches (heard.begin(), heard.end());
    for (int p : { 72, 75, 78, 81 }) if (! pitches.count (p)) check::fail (__FILE__, __LINE__, "Midular didn't play " + std::to_string (p));

    // Saved with the song: a fresh one from the state plays it too.
    MemoryBlock saved;
    inst->getStateInformation (saved);
    auto again = makeJsfx (jsfxRef (spec ("midular")));
    again->prepareToPlay (48000, 512);
    again->setStateInformation (saved.getData(), (int) saved.getSize());
    // (It reads the phrase back only if @serialize's file_avail says how many
    // values are left, as REAPER's does: ysfx's said 1, mended in cmake/App.cmake.)
    CHECK (jsfxVariable (*again, "md_ser_has") == 1.0);
    const auto heardAgain = play (*again, 200, { 60 }, true);
    std::set<int> pitchesAgain (heardAgain.begin(), heardAgain.end());
    CHECK (pitchesAgain.count (72) && pitchesAgain.count (81));
}

TEST ("family devices: MIDI Production Center loads the chosen clip, from its menu and from its own Load Item button")
{
    PluginManager plugins;
    Engine engine (plugins);
    Controller c (engine);
    c.project = Project();
    const Id t = edit::addTrack (c.project, TrackKind::Track, "Pads");
    const Id dev = edit::addDevice (c.project, t, jsfxRef (spec ("midi-production-center")), DeviceRole::MidiFx);
    const Id clip = edit::addMidiClip (c.project, t, 0, PPQ * 4);
    for (int i = 0; i < 8; ++i) edit::addNote (c.project, clip, { 0, i * PPQ / 2, PPQ / 4, 40 + i, 110 });
    c.refresh();
    auto* inst = engine.instance (dev);
    CHECK (inst != nullptr);
    if (! inst) return;
    inst->prepareToPlay (48000, 512);
    play (*inst, 4);

    // A new one plays its demo groove from the first pad, as in REAPER.
    {
        auto fresh = makeJsfx (jsfxRef (spec ("midi-production-center")));
        fresh->prepareToPlay (48000, 512);
        play (*fresh, 4);
        CHECK (! play (*fresh, 100, { 36 }, true).empty());
    }

    // The background loader runs while the song has one.
    c.reaperScripts->sync();
    CHECK (c.reaperScripts->backgroundLoader() != nullptr);

    // From the device's menu.
    c.selectedClips = { clip };
    const double before = jsfxGmem ("MIDIProductionCenter", 10) ? *jsfxGmem ("MIDIProductionCenter", 10) : -1;
    CHECK (c.loadChosenClipsInto (dev));
    settle (*c.reaperScripts, *inst);
    const double* ackSeq = jsfxGmem ("MIDIProductionCenter", 10);
    const double* seq = jsfxGmem ("MIDIProductionCenter", 0);
    CHECK (ackSeq && seq && *ackSeq == *seq && *ackSeq > before);
    CHECK (! c.message.contains ("didn"));
    // The phrase went onto the pads: the first plays from the start of the first slice
    // (each pad 16 values from PAD; its region at 14 and 15). Before the pad stride
    // was given a name of its own, the stutter switch (p_st) set it to 0 (P_ST):
    // EEL2's names don't mind case, so every pad was one empty pad.
    {
        const auto pad = (unsigned) jsfxVariable (*inst, "PAD");
        CHECK (jsfxVariable (*inst, "PAD_STRIDE") == 16.0);
        CHECK (jsfxMemory (*inst, pad + 15) - jsfxMemory (*inst, pad + 14) > 0.4);
    }

    // Its own Load Item button: it asks through gmem, the loader answers.
    double id = 0, lo = 0, hi = 0;
    for (auto* p : inst->getParameters()) if (p->getName (64) == "Instance ID") jsfxSliderValue (*p, id, lo, hi);
    CHECK (id > 0);
    *jsfxGmem ("MIDIProductionCenter", 20) = id;
    *jsfxGmem ("MIDIProductionCenter", 21) = *jsfxGmem ("MIDIProductionCenter", 21) + 1;
    const double seqBefore = *jsfxGmem ("MIDIProductionCenter", 0);
    for (int i = 0; i < 5; ++i) { c.reaperScripts->backgroundLoader()->step(); play (*inst, 1); }
    CHECK (*jsfxGmem ("MIDIProductionCenter", 0) == seqBefore + 1);
    CHECK (*jsfxGmem ("MIDIProductionCenter", 10) == seqBefore + 1);
    CHECK (*jsfxGmem ("MIDIProductionCenter", 30) > 0);   // its heartbeat: "Loader on"

    // Bank A's first pad (note 36) plays the first slice: its notes come out.
    const auto heard = play (*inst, 100, { 36 }, true);
    CHECK (! heard.empty());

    // Gone from the song, the loader stops.
    c.apply ("Remove", [dev] (Project& p) { edit::removeDevice (p, dev); });
    c.reaperScripts->sync();
    CHECK (c.reaperScripts->backgroundLoader() == nullptr);
}

#include "LuaTool/LuaTool.h"

TEST ("family devices: Good Idea's own engine makes an idea; it auditions, plays with the song and goes into the song, drums on channel 10")
{
    PluginManager plugins;
    Engine engine (plugins);
    Controller c (engine);
    c.project = Project();
    const Id t = edit::addTrack (c.project, TrackKind::Track, "Ideas");
    PluginRef ref;
    for (auto& s : luaTools()) if (String (s.id) == "good-idea") ref = luaToolRef (s);
    CHECK (ref.identifier == "tool:good-idea");
    const Id dev = edit::addDevice (c.project, t, ref, DeviceRole::MidiFx);
    c.refresh();
    auto* inst = engine.instance (dev);
    CHECK (inst != nullptr);
    if (! inst) return;
    CHECK_EQ (luaToolError (*inst), String());

    // An idea of each kind from Good Idea's engine, as New Idea makes one.
    const String make =
        "local T = dofile('<embedded>/tools/good-idea/gi_theory.lua') "
        "local I = dofile('<embedded>/tools/good-idea/gi_idea.lua').init(T) "
        "local st = I.newState() st.kind = %KIND% st.seed = 7 I.clampState(st) "
        "idea = I.make(st, I.meter(4, 4), st.seed) return #idea.block.notes";
    for (auto kind : { "'Motif'", "'Drums'" })
    {
        const auto n = luaToolRun (*inst, make.replace ("%KIND%", kind));
        if (n.getIntValue() <= 0) check::fail (__FILE__, __LINE__, (String ("no notes for ") + kind + ": " + n).toStdString());
    }
    // The drums: on channel 10 (9 from 0).
    CHECK_EQ (luaToolRun (*inst, "for _, n in ipairs(idea.block.notes) do if (n.chan or 0) ~= 9 then return 'no' end end return 'yes'"), String ("yes"));

    // Audition through the device's MIDI out, on channel 10.
    const String place = "local P = dofile('<embedded>/tools/good-idea/gi_place.lua') ";
    CHECK_EQ (luaToolRun (*inst, place + "return tostring(P.previewStart(idea.block, 120))"), String ("true"));
    inst->prepareToPlay (48000, 512);
    AudioBuffer<float> b (2, 512);
    int ons = 0, onTen = 0;
    for (int i = 0; i < 200; ++i)
    {
        MidiBuffer m;
        inst->processBlock (b, m);
        for (const auto meta : m)
            if (meta.getMessage().isNoteOn()) { ++ons; onTen += meta.getMessage().getChannel() == 10 ? 1 : 0; }
    }
    CHECK (ons > 0);
    CHECK_EQ (onTen, ons);

    // Play with REAPER is the device's Play with the Song.
    luaToolRun (*inst, place + "P.follow(idea.block, true)");
    CHECK_EQ (luaToolRun (*inst, "return tostring(host.withSong())"), String ("true"));
    luaToolRun (*inst, place + "P.follow(idea.block, false)");

    // Into the song at the playhead, one undo step; its notes keep their channel.
    c.locate (PPQ * 4);
    const auto steps = c.history.size();
    CHECK_EQ (luaToolRun (*inst, place + "return P.insert(idea.block)"), String ("0"));
    CHECK_EQ (c.history.size(), steps + 1);
    const auto& clips = c.project.track (t)->clips();
    CHECK_EQ (clips.size(), size_t (1));
    if (! clips.empty())
    {
        CHECK_EQ (clips[0].start, PPQ * 4);
        CHECK (! clips[0].midi.notes.empty());
        for (auto& n : clips[0].midi.notes) CHECK_EQ (n.channel, 9);
    }
}

#include "Editors.h"
#include "HostedProcessor.h"
#include "PluginProcessor.h"   // ScaleView's own (ThirdParty/ScaleView/Source)

namespace
{
// Each render in an engine of its own, so no plug-in carries anything from one to the next.
AudioBuffer<float> renderSong (const Project& p, double seconds, const String& name)
{
    PluginManager plugins;
    Engine engine (plugins);
    const auto dir = File::getSpecialLocation (File::tempDirectory).getChildFile ("kalibre-family-devices");
    dir.createDirectory();
    const auto out = dir.getChildFile (name + ".wav");
    Engine::RenderOptions o;
    o.start = 0; o.end = seconds; o.tail = 0; o.bitDepth = 32;
    String error;
    CHECK (engine.render (p, dir, out, o, {}, error));
    AudioFormatManager fm;
    fm.registerBasicFormats();
    std::unique_ptr<AudioFormatReader> r (fm.createReaderFor (out));
    AudioBuffer<float> b (2, r ? (int) r->lengthInSamples : 0);
    if (r) r->read (&b, 0, b.getNumSamples(), 0, true, true);
    return b;
}
} // namespace

TEST ("family devices: ScaleView names the chord played through it, and leaves the sound exactly as it was")
{
    // The chord held, as its window reads it.
    auto sv = makeScaleView();
    CHECK (sv != nullptr);
    sv->prepareToPlay (48000, 512);
    AudioBuffer<float> b (2, 512);
    b.clear();
    MidiBuffer m;
    for (int k : { 60, 63, 67, 70 }) m.addEvent (MidiMessage::noteOn (1, k, (uint8) 100), 0);
    sv->processBlock (b, m);
    int passed = 0;
    for (const auto meta : m) passed += meta.getMessage().isNoteOn() ? 1 : 0;
    CHECK_EQ (passed, 4);   // the notes go on to the instrument
    auto* hosted = dynamic_cast<HostedProcessor*> (sv.get());
    auto* view = hosted ? dynamic_cast<ScaleViewProcessor*> (&hosted->inner()) : nullptr;
    CHECK (view != nullptr);
    if (view) CHECK_EQ (view->getHeldNotes(), (std::vector<int> { 60, 63, 67, 70 }));
    {
        editors::Owned e (editors::open (*sv));
        CHECK (e != nullptr && e->getWidth() > 100);
    }

    // In a song, before Kalibre Synth: the render is the same, sample for sample.
    Project p;
    const Id t = edit::addTrack (p, TrackKind::Track, "Keys");
    edit::addDevice (p, t, synthRef(), DeviceRole::Instrument);
    const Id clip = edit::addMidiClip (p, t, 0, PPQ * 4);
    for (int k : { 60, 63, 67, 70 }) edit::addNote (p, clip, { 0, 0, PPQ * 2, k, 100 });
    const auto without = renderSong (p, 2.0, "without-scaleview");
    edit::addDevice (p, t, scaleViewRef(), DeviceRole::MidiFx);
    const auto with = renderSong (p, 2.0, "with-scaleview");
    CHECK (without.getNumSamples() > 0 && without.getNumSamples() == with.getNumSamples());
    float worst = 0, loudest = 0;
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < jmin (without.getNumSamples(), with.getNumSamples()); ++i)
        {
            worst = jmax (worst, std::abs (without.getSample (ch, i) - with.getSample (ch, i)));
            loudest = jmax (loudest, std::abs (with.getSample (ch, i)));
        }
    CHECK (loudest > 0.01f);
    CHECK_EQ (worst, 0.0f);
}

TEST ("family devices: Trombolese plays a phrase in a song: it speaks, slides between held notes, stays finite, and keeps its settings")
{
    auto inst = makeInstrument (tromboleseRef());
    CHECK (inst != nullptr);
    if (! inst) return;
    CHECK (inst->getParameters().size() >= 7);
    inst->prepareToPlay (48000, 512);
    AudioBuffer<float> b (2, 512);
    float loudest = 0;
    bool finite = true;
    auto run = [&] (int blocks, std::initializer_list<MidiMessage> at0 = {})
    {
        for (int i = 0; i < blocks; ++i)
        {
            MidiBuffer m;
            if (i == 0) for (auto& msg : at0) m.addEvent (msg, 0);
            b.clear();
            inst->processBlock (b, m);
            for (int ch = 0; ch < 2; ++ch)
                for (int s = 0; s < 512; ++s)
                {
                    const float x = b.getSample (ch, s);
                    finite = finite && std::isfinite (x);
                    loudest = jmax (loudest, std::abs (x));
                }
        }
    };
    run (40, { MidiMessage::noteOn (1, 55, (uint8) 100) });
    CHECK (loudest > 0.01f);
    run (40, { MidiMessage::noteOn (1, 60, (uint8) 100), MidiMessage::noteOff (1, 55) });   // legato up a fourth
    run (20, { MidiMessage::controllerEvent (1, 1, 100) });                                  // the mod wheel turns the bore toward a cone
    run (60, { MidiMessage::noteOff (1, 60) });
    CHECK (finite);
    CHECK (loudest < 4.0f);
    // Let go, it falls silent.
    float tail = 0;
    run (20);
    for (int ch = 0; ch < 2; ++ch) tail = jmax (tail, b.getMagnitude (ch, 0, 512));
    CHECK (tail < 1e-4f);

    // Its knobs are saved.
    inst->getParameters()[0]->setValueNotifyingHost (0.7f);
    MemoryBlock state;
    inst->getStateInformation (state);
    auto again = makeInstrument (tromboleseRef());
    again->setStateInformation (state.getData(), (int) state.getSize());
    CHECK (std::abs (again->getParameters()[0]->getValue() - 0.7f) < 1e-3f);

    // In a song, rendered: there's sound where the notes are.
    Project p;
    const Id t = edit::addTrack (p, TrackKind::Track, "Trombolese");
    edit::addDevice (p, t, tromboleseRef(), DeviceRole::Instrument);
    const Id clip = edit::addMidiClip (p, t, 0, PPQ * 4);
    edit::addNote (p, clip, { 0, 0, PPQ * 2, 57, 100 });
    edit::addNote (p, clip, { 0, PPQ * 2, PPQ * 2, 62, 100 });
    const auto song = renderSong (p, 2.0, "trombolese");
    CHECK (song.getMagnitude (0, 0, song.getNumSamples()) > 0.01f);
}

TEST ("family devices: each JSFX's window drawn on the screen for a while, then closed, stops cleanly")
{
    for (const char* id : { "midular", "midi-production-center", "reverberator" })
    {
        auto inst = makeJsfx (jsfxRef (spec (id)));
        inst->prepareToPlay (48000, 512);
        {
            editors::Owned e (editors::open (*inst));
            CHECK (e != nullptr);
            if (! e) continue;
            e->addToDesktop (ComponentPeer::windowHasTitleBar);
            e->setVisible (true);
            const auto until = Time::getMillisecondCounter() + 1500;
            while (Time::getMillisecondCounter() < until)
            {
                MessageManager::getInstance()->runDispatchLoopUntil (20);
                play (*inst, 2);
            }
        }
        MessageManager::getInstance()->runDispatchLoopUntil (50);
    }
}

namespace
{
// A value in the JSFX's memory: `array[index]`, `array` a global holding its address.
double jsfxArray (AudioPluginInstance& p, const char* array, int index)
{
    return jsfxMemory (p, (unsigned int) (jsfxVariable (p, array) + index));
}

template <typename T>
T* findChild (Component& root)
{
    if (auto* t = dynamic_cast<T*> (&root)) return t;
    for (auto* child : root.getChildren())
        if (auto* t = findChild<T> (*child)) return t;
    return nullptr;
}

File writeShufflerSample (const String& name, bool clicky)
{
    const auto f = File::getSpecialLocation (File::tempDirectory).getChildFile ("kalibre-shuffler").getChildFile (name);
    f.getParentDirectory().createDirectory();
    f.deleteFile();
    AudioBuffer<float> b (1, 48000);
    Random rnd (3);
    for (int i = 0; i < b.getNumSamples(); ++i)
    {
        const double t = i / 48000.0;
        const double v = clicky ? std::exp (-t * 30) * (rnd.nextFloat() * 2 - 1) * 0.8
                                : 0.6 * std::min (1.0, t * 20) * std::sin (2 * MathConstants<double>::pi * 220 * t);
        b.setSample (0, i, (float) v);
    }
    WavAudioFormat wav;
    std::unique_ptr<OutputStream> os (f.createOutputStream().release());
    if (auto w = wav.createWriterFor (os, AudioFormatWriterOptions().withSampleRate (48000).withNumChannels (1).withBitsPerSample (24)))
        w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
    return f;
}
} // namespace

TEST ("family devices: Transient Shuffler takes samples dragged in from the Sounds tab, and Print puts the result on a new audio track")
{
    PluginManager plugins;
    Engine engine (plugins);
    Controller c (engine);
    c.project = Project();
    const Id t = edit::addTrack (c.project, TrackKind::Track, "Hits");
    const Id dev = edit::addDevice (c.project, t, jsfxRef (spec ("transient-shuffler")), DeviceRole::AudioFx);
    c.refresh();
    auto* inst = engine.instance (dev);
    CHECK (inst != nullptr);
    if (! inst) return;
    inst->prepareToPlay (48000, 512);
    const auto pad = writeShufflerSample ("Pad.wav", false);
    const auto click = writeShufflerSample ("Click.wav", true);

    editors::Owned e (editors::open (*inst));
    CHECK (e != nullptr);
    if (! e) return;
    e->addToDesktop (ComponentPeer::windowHasTitleBar);
    e->setVisible (true);
    auto waitFor = [&] (std::function<bool()> done, int ms)
    {
        const auto until = Time::getMillisecondCounter() + (uint32) ms;
        while (! done() && Time::getMillisecondCounter() < until)
        {
            MessageManager::getInstance()->runDispatchLoopUntil (20);
            play (*inst, 1);
        }
        return done();
    };

    // Dragged from the Sounds tab: Kalibre's own drag, a file's path, let go
    // over row A, then over row B (the pointer is where it's let go).
    auto* target = findChild<DragAndDropTarget> (*e);
    CHECK (target != nullptr);
    if (! target) return;
    Component* view = nullptr;
    for (auto* child : e->getChildren()) if (child->getWidth() > 100) view = child;
    CHECK (view != nullptr);
    if (! view) return;
    waitFor ([&] { return jsfxVariable (*inst, "rowB_y") > 0; }, 3000);   // laid out
    auto pointAt = [&] (Point<int> p) { Desktop::setMousePosition (view->localPointToGlobal (p)); waitFor ([] { return false; }, 200); };
    for (const auto& f : { pad, click })
    {
        const int row = (int) jsfxVariable (*inst, f == pad ? "rowA_y" : "rowB_y") + 20;
        DragAndDropTarget::SourceDetails d (var ("file:" + f.getFullPathName()), nullptr, view->getPosition() + Point<int> (view->getWidth() / 2, row));
        CHECK (target->isInterestedInDragSource (d));
        target->itemDropped (d);
        waitFor ([&] { return jsfxArray (*inst, "s_len", f == pad ? 0 : 1) > 0; }, 5000);
    }
    CHECK (jsfxArray (*inst, "s_len", 0) > 40000);   // A: the pad, a second long
    CHECK (jsfxArray (*inst, "s_len", 1) > 40000);   // B: the click
    CHECK (waitFor ([&] { return jsfxArray (*inst, "s_len", 2) > 0; }, 5000));   // A with B's attack, made

    // Its own Print button, clicked.
    const double S = jsfxVariable (*inst, "S"), pad0 = jsfxVariable (*inst, "pad"), bw = jsfxVariable (*inst, "bw"),
                 labw = jsfxVariable (*inst, "labw"), rowR = jsfxVariable (*inst, "rowR_y");
    const Point<int> at ((int) (pad0 + bw + 12 * S + (labw - bw - 12 * S) / 2), (int) (rowR + 104 * S + 14 * S));
    auto mouse = [&] (bool down)
    {
        const MouseEvent ev (Desktop::getInstance().getMainMouseSource(), at.toFloat(),
                             down ? ModifierKeys (ModifierKeys::leftButtonModifier) : ModifierKeys(), MouseInputSource::defaultPressure,
                             0.0f, 0.0f, 0.0f, 0.0f, view, view, Time::getCurrentTime(), at.toFloat(), Time::getCurrentTime(), 1, false);
        if (down) { view->mouseMove (ev); view->mouseDown (ev); } else view->mouseUp (ev);
    };
    const size_t tracksBefore = c.project.tracks.size();
    pointAt (at);
    mouse (true);
    waitFor ([] { return false; }, 300);
    mouse (false);
    CHECK (waitFor ([&] { return c.project.tracks.size() > tracksBefore; }, 5000));
    if (c.project.tracks.size() <= tracksBefore) return;
    // A new audio track under the device's own, the result on it as a clip at the cursor.
    const auto& made = c.project.tracks[1];
    CHECK_EQ (made.name, std::string ("Hits Transient Shuffler"));
    CHECK_EQ (made.clips().size(), (size_t) 1);
    if (made.clips().empty()) return;
    const auto& clip = made.clips()[0];
    CHECK (! clip.isMidi());
    CHECK (File (clip.audio.file).existsAsFile());
    CHECK (std::abs (clip.audio.sourceLength - 1.0) < 0.05);
    CHECK_EQ (clip.start, c.cursor);
    // One undo step takes it away.
    c.undo();
    CHECK_EQ (c.project.tracks.size(), tracksBefore);
}

TEST ("family devices: Trombolese plays the UAT's melody (A4 to G5, at 100 BPM) without blowing up: no louder at its end than through it (found in the UAT)")
{
    Project p;
    p.tempo.setTempoAt (0, 100.0);
    const Id t = edit::addTrack (p, TrackKind::Track, "Lead");
    edit::addDevice (p, t, tromboleseRef(), DeviceRole::Instrument);
    const Id clip = edit::addMidiClip (p, t, 0, PPQ * 16);
    const struct { int pitch; double start, length; } melody[] {
        { 69, 0, 1 }, { 72, 1, 1 }, { 76, 2, 2 }, { 77, 4, 1 }, { 76, 5, .5 }, { 74, 5.5, .5 }, { 72, 6, 2 },
        { 72, 8, 1 }, { 74, 9, 1 }, { 76, 10, 1 }, { 79, 11, 1 }, { 74, 12, 3 }, { 71, 15, 1 },
    };
    for (auto& n : melody)
        edit::addNote (p, clip, { 0, (Tick) std::llround (n.start * PPQ), (Tick) std::llround (n.length * PPQ), n.pitch, 96 });
    const auto song = renderSong (p, 10.0, "trombolese-uat");
    CHECK (song.getNumSamples() > 0);
    if (song.getNumSamples() == 0) return;
    const int second = 48000;
    auto peakOf = [&] (int from, int to)
    {
        float m = 0;
        for (int ch = 0; ch < 2; ++ch) m = jmax (m, song.getMagnitude (ch, from, jmin (to, song.getNumSamples()) - from));
        return m;
    };
    const float through = peakOf (0, 7 * second), end = peakOf (7 * second, 10 * second);
    std::printf ("    trombolese: peak %.3f through the first 7 s, %.3f after\n", through, end);
    CHECK (through > 0.01f);
    CHECK (end < 1.0f);
    CHECK (end < through * 4.0f);
}

TEST ("family devices: Trombolese's notes are about as loud as each other, held alone or slid between (found in the UAT: B4 six times louder)")
{
    auto peakOf = [] (std::vector<std::pair<int, double>> notes, double seconds)
    {
        Project p;
        p.tempo.setTempoAt (0, 100.0);
        const Id t = edit::addTrack (p, TrackKind::Track, "Lead");
        edit::addDevice (p, t, tromboleseRef(), DeviceRole::Instrument);
        const Id clip = edit::addMidiClip (p, t, 0, PPQ * 16);
        double at = 0;
        for (auto& [pitch, beats] : notes)
        {
            edit::addNote (p, clip, { 0, (Tick) std::llround (at * PPQ), (Tick) std::llround (beats * PPQ), pitch, 96 });
            at += beats;
        }
        const auto s = renderSong (p, seconds, "trombolese-level");
        float m = 0;
        for (int ch = 0; ch < 2; ++ch) m = jmax (m, s.getMagnitude (ch, 0, s.getNumSamples()));
        return m;
    };
    float quietest = 1e9f, loudest = 0;
    for (int pitch = 52; pitch <= 79; ++pitch)
    {
        const float m = peakOf ({ { pitch, 3 } }, 2.5);
        quietest = jmin (quietest, m);
        loudest = jmax (loudest, m);
        if (m > 0.3f) std::printf ("    trombolese: note %d peaks at %.3f\n", pitch, m);
    }
    CHECK (quietest > 0.01f);
    CHECK (loudest < 0.3f);   // at its Volume of -6 dB
    // Touching notes slide from one to the next: never louder than the notes themselves.
    for (auto [from, to] : { std::pair { 74, 71 }, { 79, 74 }, { 72, 65 }, { 60, 76 }, { 76, 60 } })
    {
        const float m = peakOf ({ { from, 3 }, { to, 2 } }, 3.5);
        if (m > 0.3f) std::printf ("    trombolese: %d to %d peaks at %.3f\n", from, to, m);
        CHECK (m < 0.3f);
    }
}
