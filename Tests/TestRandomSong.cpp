// The Random button's song (decision 0101): small enough to play, made of the
// engines' music, the same song from the same seed.
#include "Check.h"
#include "RandomSong.h"

#include <set>

using namespace kal;
using namespace kal::randomsong;

namespace
{
Choice choice (const std::string& name, DeviceRole role, bool drums = false, bool wantsSample = false)
{
    Choice c;
    c.name = name;
    c.ref.format = "Kalibre";
    c.ref.identifier = name;
    c.ref.name = name;
    c.role = role;
    c.drums = drums;
    c.wantsSample = wantsSample;
    return c;
}

Catalogue catalogue (bool withSounds)
{
    Catalogue c;
    c.instruments = { choice ("Kalibre Synth", DeviceRole::Instrument), choice ("Trombolese", DeviceRole::Instrument),
                      choice ("Kalibre Sampler", DeviceRole::Instrument, false, true), choice ("Drum Kit", DeviceRole::Instrument, true) };
    for (int i = 0; i < 6; ++i) c.effects.push_back (choice ("Effect " + std::to_string (i), DeviceRole::AudioFx));
    c.midiEffects = { choice ("Arpeggiator", DeviceRole::MidiFx), choice ("Chord Maker", DeviceRole::MidiFx) };
    if (withSounds)
    {
        Sample loop;
        loop.path = "/s/Break 96bpm.wav"; loop.name = "Break 96bpm"; loop.seconds = 5.0; loop.bpm = 96; loop.drums = true;
        Sample loop2 = loop;
        loop2.path = "/s/Keys 120bpm.wav"; loop2.name = "Keys 120bpm"; loop2.seconds = 4.0; loop2.bpm = 120; loop2.drums = false;
        Sample hit;
        hit.path = "/s/Pluck.wav"; hit.name = "Pluck"; hit.seconds = 0.8; hit.note = 57;
        c.samples = { loop, loop2, hit };
    }
    return c;
}

int countRole (const Project& p, DeviceRole role)
{
    int n = 0;
    for (auto& t : p.tracks) for (auto& d : t.devices) n += d.role == role;
    return n;
}
} // namespace

TEST ("random song: 4 to 8 tracks, effects kept few, drums on a kit, every part with music; the same seed, the same song (0101)")
{
    const auto cat = catalogue (false);
    std::set<size_t> counts;
    for (unsigned seed = 1; seed <= 40; ++seed)
    {
        Options o;
        o.seed = seed;
        const auto r = make (cat, o);
        const auto& p = r.project;
        counts.insert (p.tracks.size());
        CHECK (p.tracks.size() >= 4 && p.tracks.size() <= 8);
        CHECK (countRole (p, DeviceRole::AudioFx) <= 8);
        CHECK (countRole (p, DeviceRole::MidiFx) <= 2);
        CHECK (p.loop.on && p.loop.end == PPQ * 32);
        const double bpm = p.tempo.bpmAt (0);
        CHECK (bpm >= 82 && bpm <= 148);
        for (auto& t : p.tracks)
        {
            int fx = 0;
            for (auto& d : t.devices) fx += d.role == DeviceRole::AudioFx;
            CHECK (fx <= 2);
            CHECK_EQ (t.clips().size(), (size_t) 1);
            if (t.clips().empty()) continue;
            const auto& c = t.clips().front();
            CHECK (c.isMidi());
            CHECK (! c.midi.notes.empty());
            int instruments = 0;
            for (auto& d : t.devices) instruments += d.role == DeviceRole::Instrument;
            CHECK_EQ (instruments, 1);
            // The Sampler only where there's a sound to give it: none here.
            for (auto& d : t.devices) CHECK (d.plugin.name != "Kalibre Sampler");
            if (t.name == "Drums") CHECK (t.devices.back().plugin.name == "Drum Kit" || t.devices[0].plugin.name == "Drum Kit");
        }
        CHECK (! r.summary.empty());
        CHECK_EQ (r.parts.size(), p.tracks.size());
    }
    CHECK (counts.size() >= 3);   // not always the same size
    // Drums, bass, chords and a lead are always there (the engines worked).
    Options o;
    o.seed = 7;
    const auto a = make (cat, o);
    std::set<std::string> names;
    for (auto& t : a.project.tracks) names.insert (t.name);
    for (auto* n : { "Drums", "Bass", "Chords", "Lead" }) CHECK (names.count (n) == 1);
    // The same seed: the same song.
    const auto b = make (cat, o);
    CHECK (a.project.tracks.size() == b.project.tracks.size());
    for (size_t i = 0; i < a.project.tracks.size() && i < b.project.tracks.size(); ++i)
    {
        CHECK (a.project.tracks[i].name == b.project.tracks[i].name);
        CHECK (a.project.tracks[i].clips().front().midi.notes == b.project.tracks[i].clips().front().midi.notes);
    }
}

TEST ("random song: loops from the musician's sounds, stretched to the tempo; the Sampler given a sound (0101)")
{
    const auto cat = catalogue (true);
    bool sawLoop = false, sawSampler = false;
    for (unsigned seed = 1; seed <= 60; ++seed)
    {
        Options o;
        o.seed = seed;
        const auto r = make (cat, o);
        for (auto& t : r.project.tracks)
            for (auto& c : t.clips())
                if (! c.isMidi())
                {
                    sawLoop = true;
                    CHECK (c.audio.warp == WarpMode::Spectral);
                    CHECK (c.audio.sourceBpm == 96 || c.audio.sourceBpm == 120);
                    CHECK (c.loopLength > 0 && c.length == PPQ * 32);
                    CHECK (t.devices.size() <= 2);   // a loop has no instrument
                }
        for (auto& [device, path] : r.samplerSamples)
        {
            sawSampler = true;
            CHECK (const_cast<Project&> (r.project).device (device) != nullptr);
            CHECK (path == "/s/Pluck.wav" || path == "/s/Break 96bpm.wav");
        }
    }
    CHECK (sawLoop);
    CHECK (sawSampler);
}


TEST ("random devices: an instrument when asked (a kit for drums), one or two effects, a MIDI effect now and then (0102)")
{
    const auto cat = catalogue (true);
    int midiFx = 0;
    for (unsigned seed = 1; seed <= 80; ++seed)
    {
        const auto d = devices (cat, seed, true, false, true);
        int instruments = 0, effects = 0;
        for (auto& x : d)
        {
            instruments += x.choice.role == DeviceRole::Instrument;
            effects += x.choice.role == DeviceRole::AudioFx;
            midiFx += x.choice.role == DeviceRole::MidiFx;
            CHECK (! x.choice.drums);                              // not a kit for a tonal part
            if (x.choice.wantsSample) CHECK (! x.sample.empty());  // the Sampler always with a sound
        }
        CHECK_EQ (instruments, 1);
        CHECK (effects >= 1 && effects <= 2);
        // Drums: the kit.
        const auto drums = devices (cat, seed, true, true, true);
        for (auto& x : drums) if (x.choice.role == DeviceRole::Instrument) CHECK (x.choice.drums);
        // Effects only, for a track without notes.
        for (auto& x : devices (cat, seed, false, false, false)) CHECK (x.choice.role == DeviceRole::AudioFx);
    }
    CHECK (midiFx > 5 && midiFx < 40);   // about a quarter of the time
}

TEST ("random settings: volume, tuning and pitch controls are left alone, by the names real plug-ins give them; the sound's other controls aren't (0104)")
{
    using randomsong::whyKept;
    auto kept = [] (const char* n, const char* why) { CHECK_EQ (whyKept (n), std::string (why)); };
    // Volume: Kalibre's own, Vital, Surge XT, Serum, Sylenth1, OB-Xf, Calf, LSP, compressors, limiters.
    for (auto* n : { "Volume", "Master Volume", "MasterVol", "Main Vol", "A Vol", "Volume A1", "Output", "Output Gain", "Gain", "Level",
                     "Sub Level", "Sample Level", "Voice Amplitude", "Osc 1 Volume", "Pre-Filter Gain", "VCA Gain", "Send FX 1 Return",
                     "Out", "In", "Input", "Input Gain", "Trim", "Makeup", "Ceiling", "Fader", "Attenuation", "FXlevel", "OutGain",
                     "EG1 To Amp", "Vel->Amp", "Env->Amp", "Filter1 To Amplifier", "Expression", "Global Volume", "EQ Low Gain" })
        kept (n, "volume");
    // Tuning and pitch: Serum's OCT, SEM, FIN, CRS; Sylenth1's Note; Dexed; Surge; OB-Xf; Auto-Tune; Valhalla's Shift.
    for (auto* n : { "Tune", "Master Tune Adj", "Osc 2 Pitch", "Osc 2 Fine", "Fine", "Coarse", "A Crs", "A Sem", "A Octave", "Osc1Pitch",
                     "Transpose", "Voice Transpose", "Detune", "Unison Detune", "Semitones", "Octave", "Oct", "Cents", "Note A1", "Root Note",
                     "Pitch Bend Range", "Pitchbend", "Pbend", "Pitch Wheel", "Speed", "Tape Speed", "Frequency shift", "Shift", "Key", "Scale",
                     "Retune Speed", "Vibrato", "Vib Rate", "LFO 1 to Osc 1 Pitch", "EnvToPitchAmount", "Osc Drift", "Wow", "Flutter",
                     "FineTune2", "OSC: Tune1", "Lfo Pm Depth", "Frequency Coarse", "P: Carrier Frq", "Osc 1 Frequency", "Op 3 Ratio",
                     "Osc 2 Keytrack", "Sample Keytrack" })
        kept (n, "pitch");
    // A sound source's own on switch, which could leave it silent.
    for (auto* n : { "Sample Switch", "Sub Switch", "Osc 1 On", "Noise Enable", "Layer 2 Active" }) kept (n, "silence");
    // Not the sound: presets, monitoring, quality, MIDI, performance, mute, solo, bypass.
    for (auto* n : { "Bypass", "Mute", "Osc 1 Solo", "Noise Mute", "Initialize Patch", "Next Patch", "Save Patch", "Undo", "Randomize Patch",
                     "Delta", "S/C-Listen", "Audition Left", "Oversampling", "Lookahead", "HQ", "Polyphony", "Voices", "Mod Wheel",
                     "MIDI Channel", "Sync BPM", "Sidechain", "Polarity", "Analyzer Smoothing", "Show mix overlay", "MPE Mode" })
        kept (n, "not sound");
    // Wired to anything.
    for (auto* n : { "#001", "123", "Param 12", "Parameter A", "Macro 1", "Modulation 2 Amount", "Modulation 1 Destination" }) kept (n, "unknown");
    // A unit says tuning.
    CHECK_EQ (whyKept ("Spread", "ct"), std::string ("pitch"));
    CHECK_EQ (whyKept ("Offset", " st"), std::string ("pitch"));
    CHECK (whyKept ("Cutoff", " semitones").empty());   // Vital measures cutoffs in semitones
    // A limiter's threshold is its volume; a compressor's is the sound.
    CHECK_EQ (whyKept ("Threshold", "", "Limiter No6"), std::string ("volume"));
    CHECK_EQ (whyKept ("Threshold", "", "Ozone Maximizer"), std::string ("volume"));
    CHECK (whyKept ("Threshold", "", "Pro-C 2").empty());

    // The sound: chance may change these. Some look like the above and aren't.
    for (auto* n : { "Cutoff", "Resonance", "Attack", "Decay", "Sustain", "Release", "Amp Attack", "AmpEnv Release", "Osc Mix", "Wave 1",
                     "Noise Color", "Key Track", "Key Follow", "Glide", "Portamento", "Portamento Scale", "Env Amount", "Grain Size",
                     "Density", "Spray", "Drive", "Reverb Mix", "Delay Feedback", "LFO Rate", "Mod Freq", "Breath", "Stereo Width",
                     "Centre Frequency", "Center", "Octopus", "Fineness", "Voltage", "Fade In", "BitShift", "Speed Mode", "Phase Randomization",
                     "606 St", "Osc1 FM/PM amount", "Unison Voices", "Formant", "Ratio", "Knee", "Threshold", "Chorus Frequency",
                     "Delay Switch", "Filter Cutoff", "Filter1 SEM Transition", "Velocity Track", "Distortion Amount", "Wavetable Position", "Mix" })
    {
        const auto why = whyKept (n);
        if (! why.empty()) std::printf ("    %s: %s\n", n, why.c_str());
        CHECK (why.empty());
    }
}

TEST ("random settings: pitch tools and utilities have no Random; synths and effects do (0104)")
{
    using randomsong::whyNoRandom;
    for (auto* n : { "Auto-Tune Pro", "Little AlterBoy", "Melodyne", "MAutoPitch", "Pitch Shifter", "Harmonizer", "GTune", "Tuner" })
        CHECK_EQ (whyNoRandom (n), std::string ("a pitch tool"));
    for (auto* n : { "Gain", "Utility", "Trim", "Youlean Loudness Meter", "SPAN Spectrum", "Analyzer", "Oscilloscope" })
        CHECK_EQ (whyNoRandom (n), std::string ("a utility"));
    for (auto* n : { "Serum", "Vital", "Tunefish 4", "Kalibre Synth", "Pro-Q 3", "Valhalla VintageVerb", "Decapitator", "OTT", "Surge XT" })
        CHECK (whyNoRandom (n).empty());
}


TEST ("random settings: names from the musician's own plug-ins (the Random Settings Report of 508): Serum 2, XILS 4, EchoBoy, HALion, Groove Agent (0104)")
{
    using randomsong::whyKept;
    auto kept = [] (const char* n, const char* unit, const char* why)
    {
        const auto got = whyKept (n, unit);
        if (got != why) std::printf ("    %s [%s]: %s, not %s\n", n, unit, got.c_str(), why);
        CHECK_EQ (got, std::string (why));
    };
    // Tuning, however it's written.
    kept ("Main Tuning", "", "pitch");
    kept ("VCO1 Tuning", "semi-tone", "pitch");
    kept ("Global Tuning ", "semi-tone", "pitch");
    kept ("TuningMode", "", "pitch");
    kept ("Spread", "semitones", "pitch");
    kept ("A Hz Offset", "Hz", "pitch");
    kept ("A Ratio", "", "pitch");
    kept ("Osc 3 Range", "", "pitch");
    kept ("VCO2 Range", "", "pitch");
    kept ("PB Range Up", "", "pitch");
    // Serum's oscillators are A, B and C: switched off, silence.
    kept ("A Enable", "", "silence");
    kept ("B Enable", "", "silence");
    // Expression is volume.
    kept ("Expr. LSB", "", "not sound");
    kept ("Expression", "", "volume");
    // MIDI's controllers and host slots, as Steinberg's instruments name them: not the sound.
    for (auto* n : { "Contr. 14", "SoundCont 5", "Gen.Purp.5", "FootCt LSB", "BrthCt LSB", "DataEntMSB", "DataEntLSB", "Data Incr ",
                     "Non-RegLSB", "Reg MSB", "OmniModeOn", "OmniModOff", "Local Ctrl", "BreathCtrl" })
        kept (n, "", "not sound");
    for (auto* n : { "Automation 160", "Host Automation 24", "Assignable 84", "Unassigned:112", "S10 L3 QC6", "S31 QC9 not in use",
                     "Ch 14 Ins 1 P 9", "Bus 2 Ins 1 P 10", "Slot 8 Source", "Slot 2 Target", "MM Destination 7" })
        kept (n, "", "unknown");

    // Still the sound: a delay's note value, modes beside an output, a filter in semitones.
    for (auto* n : { "Echo1Note", "RhythmNote", "FX2 StDly Right Base Note", "FX3 VChor Output Mode", "Output Limiter Release",
                     "Cutoff", "Filter 2 Key-Trk", "VCO 2 FM Ctrl In 2 Amt", "Legato Sw", "Slope", "A Env: On", "B Arp Enable",
                     "A EQ Mid2 Freq", "B LFO 1 Frequency", "RateMod", "Speed", "FX1 Rotry Amp Expr Mod Mode" })
    {
        const auto why = whyKept (n, std::string (n) == "Cutoff" ? "semitones" : std::string (n) == "RateMod" ? "oct" : std::string (n) == "Speed" ? "ips" : "");
        if (! why.empty()) std::printf ("    %s: %s\n", n, why.c_str());
        CHECK (why.empty());
    }
}

TEST ("random song: the drums are Kalibre Drums when there are drum hits to give its pads; it's named so the app fills them (the musician's answer to the UAT)")
{
    auto cat = catalogue (true);
    auto kit = choice ("Kalibre Drums", DeviceRole::Instrument, true);
    kit.kit = true;
    cat.instruments.push_back (kit);
    Sample kick;
    kick.path = "/s/Kick.wav"; kick.name = "Kick"; kick.seconds = 0.4; kick.drums = true;
    cat.samples.push_back (kick);
    for (unsigned seed = 1; seed <= 20; ++seed)
    {
        Options o;
        o.seed = seed;
        const auto r = make (cat, o);
        for (auto& t : r.project.tracks)
            if (t.name == "Drums" && ! t.clips().empty() && t.clips().front().isMidi())
                for (auto& d : t.devices)
                    if (d.role == DeviceRole::Instrument)
                    {
                        CHECK_EQ (d.plugin.name, std::string ("Kalibre Drums"));
                        bool listed = false;
                        for (auto id : r.drumKits) listed |= id == d.id;
                        CHECK (listed);
                    }
    }
    // With no drum hits, it isn't chosen: an empty kit would be silent.
    const auto none = catalogue (false);
    auto withKit = none;
    withKit.instruments.push_back (kit);
    Options o;
    o.seed = 3;
    CHECK (make (withKit, o).drumKits.empty());
}
