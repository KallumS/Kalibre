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
