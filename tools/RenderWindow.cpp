// Draws the real window, with a demo song, into PNGs: one per view of the
// lower zone. Run on CI so every change can be looked at (decision 0023).
//
//   KalibreRender out-folder [scale]

#include "../Tests/App/Demo.h"
#include "BuiltinInstruments.h"
#include "ContainerProcessor.h"
#include "Comp.h"
#include "Song.h"
#include "MainComponent.h"
#include "JsfxDevice.h"
#include "LuaTool/LuaTool.h"
#include "NoteSplitter.h"
#include "PianoRoll.h"
#include "ToolPicker.h"
#include "Arrange.h"
#include "ArticulationManagerView.h"

using namespace juce;
using namespace kal;

int main (int argc, char** argv)
{
    ScopedJuceInitialiser_GUI gui;
    theme::LookAndFeel look;
    LookAndFeel::setDefaultLookAndFeel (&look);
    const File out = argc > 1 ? File::getCurrentWorkingDirectory().getChildFile (argv[1]) : File::getCurrentWorkingDirectory();
    const float scale = argc > 2 ? (float) atof (argv[2]) : 1.0f;
    out.createDirectory();
    {
        // A made-up list of plug-ins, some turned off, for the Plug-in Manager (decision 0047).
        const auto pluginFolder = File::getSpecialLocation (File::tempDirectory).getChildFile ("kalibre-render-plugins");
        pluginFolder.deleteRecursively();
        pluginFolder.createDirectory();
        {
            XmlElement xml ("KNOWNPLUGINS");
            auto add = [&] (const char* name, const char* maker, const char* format, const char* file, bool instrument)
            {
                PluginDescription d;
                d.name = name; d.manufacturerName = maker; d.pluginFormatName = format; d.fileOrIdentifier = file;
                d.isInstrument = instrument; d.numInputChannels = instrument ? 0 : 2; d.numOutputChannels = 2;
                d.uniqueId = d.deprecatedUid = String (name).hashCode();
                d.category = instrument ? "Instrument" : "Fx";
                xml.addChildElement (d.createXml().release());
            };
            add ("Pianoteq 9", "Modartt", "VST3", "/Library/Audio/Plug-Ins/VST3/Pianoteq 9.vst3", true);
            add ("Kontakt 8", "Native Instruments", "VST3", "/Library/Audio/Plug-Ins/VST3/Kontakt 8.vst3", true);
            add ("ValhallaFutureVerb", "Valhalla DSP, LLC", "VST3", "/Library/Audio/Plug-Ins/VST3/ValhallaFutureVerb.vst3", false);
            add ("Cthulhu", "Xfer Records", "VST3", "/Library/Audio/Plug-Ins/VST3/Cthulhu.vst3", false);
            add ("SINE Player", "Orchestral Tools", "AudioUnit", "AudioUnit:Synths/aumu,SINE,OrcT", true);
            add ("Scaler 3", "Scaler Music", "AudioUnit", "AudioUnit:Synths/aumu,Scl3,ScMu", true);
            add ("kHs Filter", "Kilohearts", "CLAP", "/Library/Audio/Plug-Ins/CLAP/kHs Filter.clap", false);
            add ("Superior Drummer 3", "Toontrack", "VST3", "/Library/Audio/Plug-Ins/VST3/Superior Drummer 3.vst3", true);
            add ("Shadow Hills Mastering Compressor", "Plugin Alliance", "VST3", "/Library/Audio/Plug-Ins/VST3/Shadow Hills Mastering Compressor.vst3", false);
            for (auto* f : { "/Library/Audio/Plug-Ins/VST3/Reason Rack Plugin.vst3", "/Library/Audio/Plug-Ins/VST3/Old Synth.vst3",
                             "/Library/Audio/Plug-Ins/VST3/Licence Check.vst3", "/Library/Audio/Plug-Ins/VST3/Falcon.vst3" })
                xml.createNewChildElement ("BLACKLISTED")->setAttribute ("id", f);
            auto* blocked = xml.createNewChildElement ("KALIBRE_BLOCKED");
            auto block = [&] (const char* file, int reason, const char* detail)
            {
                auto* e = blocked->createNewChildElement ("FILE");
                e->setAttribute ("file", file); e->setAttribute ("format", "VST3"); e->setAttribute ("reason", reason); e->setAttribute ("detail", detail);
                return e;
            };
            block ("/Library/Audio/Plug-Ins/VST3/Reason Rack Plugin.vst3", 0, "it aborted, signal 6");
            block ("/Library/Audio/Plug-Ins/VST3/Old Synth.vst3", 2, "");
            block ("/Library/Audio/Plug-Ins/VST3/Licence Check.vst3", 1, "still opening after 120 seconds");
            {
                PluginDescription d;
                d.name = "Falcon"; d.manufacturerName = "UVI"; d.pluginFormatName = "VST3"; d.isInstrument = true;
                d.fileOrIdentifier = "/Library/Audio/Plug-Ins/VST3/Falcon.vst3";
                block ("/Library/Audio/Plug-Ins/VST3/Falcon.vst3", 3, "")->addChildElement (d.createXml().release());
            }
            xml.writeTo (pluginFolder.getChildFile ("Plug-ins.xml"));
        }
        PluginManager plugins (pluginFolder);
        Engine engine (plugins);
        Controller c (engine);
        c.project = demo::song();
        {
            // A vocal recorded three times round the loop, to show takes and comping.
            const auto wav = File::getSpecialLocation (File::tempDirectory).getChildFile ("kalibre-render-vocal.wav");
            AudioBuffer<float> b (2, 48000 * 16);
            for (int i = 0; i < b.getNumSamples(); ++i)
            {
                const float env = 0.5f + 0.5f * std::sin ((float) i / 48000.0f * 3.1f);
                const float v = 0.4f * env * std::sin ((float) i * 0.03f) * (std::sin ((float) i * 0.0002f) > -0.3f ? 1.0f : 0.1f);
                b.setSample (0, i, v);
                b.setSample (1, i, v);
            }
            WavAudioFormat wavFormat;
            wav.deleteFile();
            std::unique_ptr<OutputStream> os (wav.createOutputStream().release());
            if (auto w = wavFormat.createWriterFor (os, AudioFormatWriterOptions().withSampleRate (48000).withNumChannels (2).withBitsPerSample (16)))
                w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
            const Id vox = edit::addTrack (c.project, TrackKind::Track, "Vocal");
            c.project.track (vox)->colour = 0xffE07A86;
            Clip rec;
            rec.kind = Clip::Kind::Audio;
            rec.name = "Vocal";
            rec.start = 0;
            rec.audio.file = wav.getFullPathName().toStdString();
            for (auto& pass : comp::audioPasses (rec, 15.0, c.project.tempo, 0, PPQ * 16))
                comp::addTake (c.project, vox, { pass });
            auto& v = c.project.track (vox)->version();
            comp::choose (c.project, vox, v.takes[0].id, PPQ * 4, PPQ * 8);
            v.takesShown = true;
            for (auto& cl : v.clips) { cl.audio.fadeIn = 0.15; cl.audio.fadeOut = 0.3; }
        }
        // Sections and two scenes in the launcher (decision 0037).
        song::addSection (c.project, "Intro", 0, PPQ * 8);
        song::addSection (c.project, "Verse", PPQ * 8, PPQ * 16);
        song::captureScene (c.project, 0, PPQ * 8, "Intro");
        song::captureScene (c.project, PPQ * 8, PPQ * 16, "Verse");
        c.focusTrack = c.project.tracks[1].id;
        c.selectedTracks = { c.focusTrack };
        c.cursor = PPQ * 6;
        c.showTempoLane = false;
        c.refresh();
        MainComponent window (c);
        window.setTextSize (1.0f);
        window.setSize (1440, 900);
        c.say ("This is the message line: what just happened, with Undo; then what is chosen, or what the pointer is over.", false, true);

        auto shoot = [&] (const String& name)
        {
            window.resized();
            MessageManager::getInstance()->runDispatchLoopUntil (50);
            auto image = window.createComponentSnapshot (window.getLocalBounds(), true, scale);
            PNGImageFormat png;
            const auto f = out.getChildFile (name + ".png");
            f.deleteFile();
            FileOutputStream s (f);
            png.writeImageToStream (image, s);
            std::printf ("wrote %s\n", f.getFullPathName().toRawUTF8());
        };
        c.zoneTab = ZoneTab::Devices;
        c.viewChanged();
        shoot ("1-devices");
        const Id keysClip = c.project.tracks[1].clips().front().id;
        c.selectClip (keysClip);
        c.editor = EditorKind::PianoRoll;
        c.viewChanged();
        shoot ("2-piano-roll");
        c.selectClip (c.project.tracks[4].clips().front().id);
        for (auto kind : { EditorKind::Drums, EditorKind::Steps, EditorKind::Euclid })
        {
            c.editor = kind;
            c.zoneTab = ZoneTab::Editor;
            c.viewChanged();
            shoot (kind == EditorKind::Drums ? "3-drums" : kind == EditorKind::Steps ? "4-steps" : "5-euclidean");
        }
        c.selectClip (c.project.tracks[5].clips().front().id);
        c.editor = EditorKind::Notation;
        c.viewChanged();
        shoot ("6-notation");
        // AutoCC (decision 0060): a violin line, Auto pressed, in the piano roll and in notation.
        {
            const Id vt = edit::addTrack (c.project, TrackKind::Track, "Violins 1");
            const Id vc = edit::addMidiClip (c.project, vt, 0, PPQ * 16);
            const int line[][3] { { 0, 6, 67 }, { 6, 2, 69 }, { 8, 4, 71 }, { 13, 3, 72 } };   // start, length (beats), pitch
            for (auto& l : line) { Note n; n.start = PPQ * l[0]; n.length = PPQ * l[1]; n.pitch = l[2]; n.velocity = 96; edit::addNote (c.project, vc, n); }
            c.refresh();
            c.selectClip (vc);
            c.autoCc();
            window.lowerHeight = 440;
            c.editor = EditorKind::PianoRoll;
            c.zoneTab = ZoneTab::Editor;
            c.viewChanged();
            shoot ("19-auto-cc-piano-roll");
            c.editor = EditorKind::Notation;
            c.viewChanged();
            shoot ("20-auto-cc-notation");
            window.lowerHeight = 320;
            edit::removeTrack (c.project, vt);
            c.refresh();
        }
        c.zoneTab = ZoneTab::Mixer;
        c.viewChanged();
        shoot ("7-mixer");
        // Versions in the mixer (decision 0050) and the Cue track after the master (decision 0051).
        {
            const auto keep = c.project;
            for (int i : { 1, 2 })
            {
                const Id tid = c.project.tracks[(size_t) i].id;
                edit::newVersion (c.project, tid);
                edit::newVersion (c.project, tid);
                if (i == 2) edit::newVersion (c.project, tid);
            }
            edit::selectVersion (c.project, c.project.tracks[1].id, 1);
            edit::addTrack (c.project, TrackKind::Summing, "Summing");
            edit::addCue (c.project);
            c.project.cue.front().mono = true;
            c.project.cue.front().pan = -0.3;
            c.selectedTracks = { c.project.tracks[1].id, c.project.tracks[2].id };
            c.focusTrack = c.project.tracks[2].id;
            window.lowerHeight = 420;
            c.refresh();
            shoot ("16-mixer-versions-and-cue");
            window.lowerHeight = 320;
            c.project = keep;
            c.focusTrack = c.project.tracks[1].id;
            c.selectedTracks = { c.focusTrack };
            c.refresh();
        }
        // The vocal's takes, with the band folded so they're in view.
        c.project.tracks[0].collapsed = true;
        c.zoneTab = ZoneTab::Devices;
        c.refresh();
        shoot ("9-takes");
        window.showBrowser (Browser::Tab::Studio);
        c.zoneTab = ZoneTab::Mixer;
        c.viewChanged();
        shoot ("10-studio-and-loudness");
        // The browser's other tabs, for the user guide (docs/USER-GUIDE.md).
        c.zoneTab = ZoneTab::Devices;
        c.viewChanged();
        window.showBrowser (Browser::Tab::Plugins);
        shoot ("31-browser-plugins");
        window.showBrowser (Browser::Tab::Audio);
        shoot ("32-browser-audio");
        window.showBrowser (Browser::Tab::Keys);
        shoot ("33-browser-keys");
        // The Sounds tab (decision 0094): a small library of drums, loops and MIDI.
        {
            const auto lib = File::getSpecialLocation (File::tempDirectory).getChildFile ("kalibre-render-library");
            lib.deleteRecursively();
            const auto sounds = lib.getChildFile ("Sounds");
            WavAudioFormat wavOut;
            auto write = [&] (const String& name, double seconds, std::function<float (double)> fn)
            {
                const auto f = sounds.getChildFile (name);
                f.getParentDirectory().createDirectory();
                AudioBuffer<float> b (2, (int) (seconds * 48000));
                for (int i = 0; i < b.getNumSamples(); ++i) { const float v = fn (i / 48000.0); b.setSample (0, i, v); b.setSample (1, i, v); }
                std::unique_ptr<OutputStream> os (f.createOutputStream().release());
                if (auto w = wavOut.createWriterFor (os, AudioFormatWriterOptions().withSampleRate (48000).withNumChannels (2).withBitsPerSample (24)))
                    w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
            };
            Random rnd (7);
            const double pi = MathConstants<double>::pi;
            write ("Drums/Kick Deep.wav", 0.6, [&] (double t) { return (float) (0.9 * std::exp (-t * 9) * std::sin (2 * pi * (50 + 120 * std::exp (-t * 30)) * t)); });
            write ("Drums/Snare Tight.wav", 0.4, [&] (double t) { return (float) (0.7 * std::exp (-t * 14) * (rnd.nextFloat() * 2 - 1)); });
            write ("Drums/Hat Closed.wav", 0.15, [&] (double t) { return (float) (0.5 * std::exp (-t * 40) * (rnd.nextFloat() * 2 - 1)); });
            write ("Drums/Breakbeat 96bpm.wav", 2.5, [&] (double t)
            {
                const double beat = std::fmod (t * 96 / 60, 1.0), half = std::fmod (t * 96 / 30, 2.0);
                return (float) (0.8 * std::exp (-beat * 7) * std::sin (2 * pi * 60 * t) * (half < 1 ? 1.0 : 0.4) + 0.2 * std::exp (-std::fmod (t * 96 / 30, 1.0) * 10) * (rnd.nextFloat() * 2 - 1));
            });
            write ("Loops/Pad Swell 120bpm.wav", 4.0, [&] (double t) { return (float) (0.5 * std::sin (pi * t / 4) * (std::sin (2 * pi * 220 * t) + 0.5 * std::sin (2 * pi * 277 * t))); });
            write ("Loops/Pluck Arp 120bpm.wav", 2.0, [&] (double t) { return (float) (0.7 * std::exp (-std::fmod (t * 4, 1.0) * 6) * std::sin (2 * pi * 440 * t)); });
            write ("Vocals/Ooh Stack.wav", 1.8, [&] (double t) { return (float) (0.6 * std::sin (pi * t / 1.8) * std::sin (2 * pi * 330 * t + std::sin (2 * pi * 5 * t))); });
            auto midi = [&] (const String& name, std::vector<std::array<int, 3>> notes)   // start, length (sixteenths), pitch
            {
                MidiFile mf;
                mf.setTicksPerQuarterNote (480);
                MidiMessageSequence seq;
                for (auto& n : notes)
                {
                    seq.addEvent (MidiMessage::noteOn (1, n[2], (uint8) 100).withTimeStamp (n[0] * 120));
                    seq.addEvent (MidiMessage::noteOff (1, n[2]).withTimeStamp ((n[0] + n[1]) * 120));
                }
                seq.updateMatchedPairs();
                mf.addTrack (seq);
                const auto f = sounds.getChildFile (name);
                f.getParentDirectory().createDirectory();
                FileOutputStream out (f);
                mf.writeTo (out);
            };
            std::vector<std::array<int, 3>> chords;
            const int prog[4][3] { { 57, 60, 64 }, { 53, 57, 60 }, { 48, 52, 55 }, { 55, 59, 62 } };
            for (int b = 0; b < 4; ++b) for (int k = 0; k < 3; ++k) chords.push_back ({ b * 16, 15, prog[b][k] });
            midi ("MIDI/Chords Am F C G.mid", chords);
            std::vector<std::array<int, 3>> bass;
            const int roots[] { 45, 41, 36, 43 };
            for (int b = 0; b < 4; ++b) for (int i = 0; i < 4; ++i) bass.push_back ({ b * 16 + i * 4, 3, roots[b] + (i == 3 ? 7 : 0) });
            midi ("MIDI/Bass Eighths.mid", bass);
            auto& sv = window.library();
            library::Settings ls;
            ls.folders = { sounds.getFullPathName().toStdString() };
            ls.asked = true;
            for (auto* sub : { "", "/Drums", "/Loops" }) ls.open.insert (sounds.getFullPathName().toStdString() + sub);
            ls.favourites = { sounds.getChildFile ("Drums/Breakbeat 96bpm.wav").getFullPathName().toStdString(),
                              sounds.getChildFile ("Loops").getFullPathName().toStdString() };
            const auto store = lib.getChildFile ("Store");
            store.createDirectory();
            store.getChildFile ("Library Settings.json").replaceWithText (library::toJson (ls));
            sv.useStore (store);
            sv.scanner().waitUntilDone (20000);
            window.browserWidth = 360;
            window.showBrowser (Browser::Tab::Files);
            for (size_t i = 0; i < sv.shown().size(); ++i)
                if (library::nameOf (sv.shown()[i].path) == "Breakbeat 96bpm.wav") sv.list().selectRow ((int) i);
            shoot ("34-sounds");
            sv.settings().flat = true;
            sv.settings().shuffled = true;
            sv.settings().seed = 31;
            sv.refreshRows();
            for (size_t i = 0; i < sv.shown().size(); ++i)
                if (library::nameOf (sv.shown()[i].path) == "Chords Am F C G.mid") sv.list().selectRow ((int) i);
            shoot ("35-sounds-all-items-shuffled");
            sv.settings().flat = false;
            sv.settings().shuffled = false;
            sv.useStore (LibraryView::defaultStore());   // not left pointing at the demo
            lib.deleteRecursively();
            window.browserWidth = 320;
        }
        window.hideBrowser();
        // The lower zone shut (decision 0094): a bar along the bottom to open it from.
        window.hideLowerZone();
        shoot ("36-lower-zone-shut");
        window.showLowerZone();
        // An audio clip in the sample editor (decision 0036).
        for (auto& t : c.project.tracks)
            if (t.name == "Vocal" && ! t.clips().empty()) c.selectClip (t.clips().front().id);
        c.zoneTab = ZoneTab::Editor;
        c.viewChanged();
        shoot ("11-sample-editor");
        c.zoneTab = ZoneTab::Launcher;
        song::launch (c.project, c.project.tracks[1].id, 0, 0);
        c.refresh();
        shoot ("12-launcher");
        // The analysers, while a sine plays out of the master (decision 0044).
        if (auto* f = c.engine.fader (c.project.master.id))
            for (int i = 0; i < Fader::tapSize; ++i)
            {
                const float v = 0.5f * std::sin ((float) i * 0.0628f) + 0.1f * std::sin ((float) i * 0.9f);
                f->tapL[(size_t) i] = v;
                f->tapR[(size_t) i] = v * 0.8f;
            }
        c.zoneTab = ZoneTab::Meters;
        c.viewChanged();
        MessageManager::getInstance()->runDispatchLoopUntil (200);
        shoot ("14-meters");
        // The Plug-in Manager's window (decision 0047), its contents drawn on their own.
        {
            PluginManagerView manager (c);
            manager.setSize (1040, 560);
            manager.refresh();
            MessageManager::getInstance()->runDispatchLoopUntil (50);
            auto image = manager.createComponentSnapshot (manager.getLocalBounds(), true, scale);
            PNGImageFormat png;
            const auto f = out.getChildFile ("15-plugin-manager.png");
            f.deleteFile();
            FileOutputStream s (f);
            png.writeImageToStream (image, s);
            std::printf ("wrote %s\n", f.getFullPathName().toRawUTF8());
        }
        // The Articulation Manager's window (decision 0072): Note Position Fixer's test maps imported.
        {
            c.articulationFolder = File::getSpecialLocation (File::tempDirectory).getChildFile ("KalibreRender-articulations");
            c.articulationFolder.deleteRecursively();
            const File fixtures = File (KALIBRE_SOURCE_DIR).getChildFile ("Tests/fixtures/articulations");
            c.importArticulationMaps ({ fixtures.getChildFile ("Test Strings.expressionmap"), fixtures.getChildFile ("Test Strings.reabank"),
                                        fixtures.getChildFile ("Test Strings.plist") });
            // Two articulations given their own delays (decision 0082).
            c.setArticulationDelay ("Violins", { 0 }, -40);
            c.setArticulationDelay ("Violins", { 2 }, 25);
            ArticulationManagerView manager (c);
            manager.setSize (900, 420);
            manager.refresh();
            for (int i = 0; i < (int) manager.shownRows().size(); ++i)
                if (manager.shownRows()[(size_t) i].map.name == "Violins") manager.choose (i);
            manager.chooseArticulation (0);
            MessageManager::getInstance()->runDispatchLoopUntil (50);
            auto image = manager.createComponentSnapshot (manager.getLocalBounds(), true, scale);
            PNGImageFormat png;
            const auto f = out.getChildFile ("24-articulation-manager.png");
            f.deleteFile();
            FileOutputStream s (f);
            png.writeImageToStream (image, s);
            std::printf ("wrote %s\n", f.getFullPathName().toRawUTF8());
            c.dismiss();
        }
        song::backToArrangement (c.project);
        // The Container (decision 0055): three plug-ins, one on a band, one on the sides.
        {
            const Id t = edit::addTrack (c.project, TrackKind::Track, "Drums Bus");
            const Id dev = edit::addDevice (c.project, t, containerRef(), DeviceRole::AudioFx);
            c.focusTrack = t;
            c.zoneTab = ZoneTab::Devices;
            c.refresh();
            if (auto* box = dynamic_cast<ContainerProcessor*> (engine.instance (dev)))
            {
                auto put = [&] (int i, const char* id, const char* name)
                {
                    PluginRef r { "Kalibre", id, name };
                    String error;
                    box->loadPlugin (i, r, error);
                    box->slot (i).ref.name = name;   // shown as a plug-in would be
                };
                put (0, "test-gain:0", "Pro-Q 3");
                put (1, "test-gain:-3", "Saturn 2");
                put (2, "test-latency:64", "Valhalla Room");
                auto set = [] (RangedAudioParameter* p, float v) { p->setValueNotifyingHost (p->convertTo0to1 (v)); };
                set (box->slot (1).low, 2000.0f);
                set (box->slot (1).high, 9000.0f);
                set (box->slot (1).gainDb, -2.5f);
                set (box->slot (2).width, 2.0f);
                set (box->slot (2).pan, -0.2f);
                box->slot (0).solo->setValueNotifyingHost (0.0f);
                box->matchLoudness->setValueNotifyingHost (1.0f);
                if (box->onSlotsChanged) box->onSlotsChanged();
            }
            window.lowerHeight = 360;
            shoot ("17-container");
            window.lowerHeight = 320;
            edit::removeTrack (c.project, t);
            c.refresh();
        }
        // The Note Splitter (decision 0083): a chord's notes to four string sections.
        {
            const Id st = edit::addTrack (c.project, TrackKind::Track, "Strings Chords");
            const Id dev = edit::addDevice (c.project, st, noteSplitterRef(), DeviceRole::MidiFx);
            std::vector<Id> to;
            for (const char* name : { "Violins I", "Violins II", "Violas" }) to.push_back (edit::addTrack (c.project, TrackKind::Track, name, st));
            to.push_back (0);   // the cellos not chosen yet
            c.project.device (dev)->midiTo = to;
            c.focusTrack = st;
            c.zoneTab = ZoneTab::Devices;
            c.refresh();
            window.lowerHeight = 340;
            shoot ("25-note-splitter");
            window.lowerHeight = 320;
            for (auto id : to) if (id) edit::removeTrack (c.project, id);
            edit::removeTrack (c.project, st);
            c.refresh();
        }
        // Fix Note Timings and Volume's question (decision 0085).
        {
            std::unique_ptr<AlertWindow> w (Arrange::fixNotesDialog ({}));
            w->setVisible (true);
            MessageManager::getInstance()->runDispatchLoopUntil (50);
            auto image = w->createComponentSnapshot (w->getLocalBounds(), true, scale);
            PNGImageFormat png;
            const auto f = out.getChildFile ("27-fix-note-timings-and-volume.png");
            f.deleteFile();
            FileOutputStream s (f);
            png.writeImageToStream (image, s);
            std::printf ("wrote %s\n", f.getFullPathName().toRawUTF8());
        }
        // Each note's own delay in the piano roll's lane (decision 0082).
        {
            const auto& chords = c.project.tracks[1].clips();
            if (! chords.empty())
            {
                const Id clip = chords.front().id;
                int k = 0;
                for (auto& n : c.project.clip (clip)->midi.notes) n.delayMs = (k++ % 3 == 0) ? 40.0 : (k % 3 == 1 ? -25.0 : 0.0);
                c.focusTrack = c.project.tracks[1].id;
                c.selectClip (clip);
                c.refresh();
                PianoRoll roll (c);
                roll.setSize (1100, 420);
                roll.showLane (-6);
                MessageManager::getInstance()->runDispatchLoopUntil (50);
                auto image = roll.createComponentSnapshot (roll.getLocalBounds(), true, scale);
                PNGImageFormat png;
                const auto f = out.getChildFile ("26-note-delay-lane.png");
                f.deleteFile();
                FileOutputStream s (f);
                png.writeImageToStream (image, s);
                std::printf ("wrote %s\n", f.getFullPathName().toRawUTF8());
                for (auto& n : c.project.clip (clip)->midi.notes) n.delayMs = 0;
                c.refresh();
            }
        }
        // The toolbox (decision 0092): Control-right-click over the arrangement and the piano roll.
        {
            auto snap = [&] (Component& comp, const String& name)
            {
                MessageManager::getInstance()->runDispatchLoopUntil (50);
                auto image = comp.createComponentSnapshot (comp.getLocalBounds(), true, scale);
                PNGImageFormat png;
                const auto f = out.getChildFile (name + ".png");
                f.deleteFile();
                FileOutputStream s (f);
                png.writeImageToStream (image, s);
                std::printf ("wrote %s\n", f.getFullPathName().toRawUTF8());
            };
            c.chooseTool (tools::Area::Arrange, tools::Tool::Split);
            {
                ToolBox box (c, tools::Area::Arrange);
                box.hovered = 6;   // the pointer on Mute
                snap (box, "28-toolbox-arrangement");
            }
            c.chooseTool (tools::Area::Roll, tools::Tool::Line, 2);
            {
                ToolBox box (c, tools::Area::Roll);
                snap (box, "29-toolbox-piano-roll");
            }
            // A range on two tracks, and muted notes drawn hollow.
            const auto& chords = c.project.tracks[1].clips();
            if (! chords.empty())
            {
                const Id clip = chords.front().id;
                int k = 0;
                for (auto& n : c.project.clip (clip)->midi.notes) n.muted = (k++ % 4 == 1);
                c.selectClip (clip);
                c.selectedNotes.clear();
                std::vector<Id> shown;
                for (auto& t : c.project.tracks) if (t.name == "Strings" || t.name == "Vocal") shown.push_back (t.id);
                c.range = { shown, PPQ * 5, PPQ * 11 };
                c.zoneTab = ZoneTab::Editor;
                c.editor = EditorKind::PianoRoll;
                c.chooseTool (tools::Area::Arrange, tools::Tool::Range);
                c.refresh();
                shoot ("30-range-and-muted-notes");
                for (auto& n : c.project.clip (clip)->midi.notes) n.muted = false;
                c.range = {};
                c.chooseTool (tools::Area::Arrange, tools::Tool::Select);
                c.chooseTool (tools::Area::Roll, tools::Tool::Select);
                c.zoneTab = ZoneTab::Devices;
                c.refresh();
            }
        }
        // The family's JSFX in the rack (decision 0066): Reese Basser, then Reverberator.
        {
            const Id jt = edit::addTrack (c.project, TrackKind::Track, "Reese");
            for (auto& sp : jsfxDevices())
                if (String (sp.id) == "reese-basser" || String (sp.id) == "reverberator") edit::addDevice (c.project, jt, jsfxRef (sp), sp.role);
            c.focusTrack = jt;
            c.zoneTab = ZoneTab::Devices;
            c.refresh();
            window.lowerHeight = 480;
            for (int i = 0; i < 10; ++i) MessageManager::getInstance()->runDispatchLoopUntil (40);   // their windows draw on a timer
            shoot ("21-jsfx-devices");
            window.lowerHeight = 320;
            edit::removeTrack (c.project, jt);
            c.refresh();
        }
        // Instant Virtual Instrument in the rack (decision 0067), its demo loaded.
        {
            const Id it = edit::addTrack (c.project, TrackKind::Track, "Instant");
            const Id dev = edit::addDevice (c.project, it, instantVirtualInstrumentRef(), DeviceRole::Instrument);
            c.focusTrack = it;
            c.zoneTab = ZoneTab::Devices;
            c.refresh();
            if (auto* inst = engine.instance (dev)) loadInstantVirtualInstrumentDemo (*inst);
            window.lowerHeight = 520;
            for (int i = 0; i < 40; ++i) MessageManager::getInstance()->runDispatchLoopUntil (50);   // its analysis, in the background
            shoot ("22-instant-virtual-instrument");
            window.lowerHeight = 320;
            edit::removeTrack (c.project, it);
            c.refresh();
        }
        // The family's ReaImGui tools in the rack (decision 0068), one at a time.
        for (const char* id : { "starting-blocks", "starting-blocks-notation", "midi-catalogue", "orchestration-helper" })
        {
            const Id tt = edit::addTrack (c.project, TrackKind::Track, "Tools");
            for (auto& sp : luaTools())
                if (String (sp.id) == id) edit::addDevice (c.project, tt, luaToolRef (sp), DeviceRole::MidiFx);
            c.focusTrack = tt;
            c.zoneTab = ZoneTab::Devices;
            c.refresh();
            window.lowerHeight = 560;
            for (int i = 0; i < 12; ++i) MessageManager::getInstance()->runDispatchLoopUntil (40);   // the script draws on a timer
            shoot ("23-" + String (id));
            if (String (id) == "orchestration-helper")
            {
                // Typed into its search box, as from the keyboard.
                std::function<Component* (Component&)> find = [&] (Component& in) -> Component*
                {
                    for (auto* ch : in.getChildren())   // the innermost: the script's own window
                    {
                        if (auto* r = find (*ch)) return r;
                        if (auto* ed = dynamic_cast<AudioProcessorEditor*> (ch); ed && ed->processor.getName() == "Orchestration Helper") return ed;
                    }
                    return nullptr;
                };
                Component* ed = find (window);
                if (ed != nullptr)
                {
                    for (auto ch : String ("trombone")) ed->keyPressed (KeyPress ((int) ch, 0, ch));
                    for (int i = 0; i < 12; ++i) MessageManager::getInstance()->runDispatchLoopUntil (40);
                    shoot ("23-orchestration-helper-search");
                }
                else std::printf ("no Orchestration Helper window found\n");
            }
            window.lowerHeight = 320;
            edit::removeTrack (c.project, tt);
            c.refresh();
        }
        // Kalibre's own synth and sampler, side by side (decision 0039).
        {
            const Id synthTrack = edit::addTrack (c.project, TrackKind::Track, "Synth");
            edit::addDevice (c.project, synthTrack, synthRef(), DeviceRole::Instrument);
            edit::addDevice (c.project, synthTrack, samplerRef(), DeviceRole::Instrument);
            c.focusTrack = synthTrack;
            c.zoneTab = ZoneTab::Devices;
            c.refresh();
            window.lowerHeight = 380;
            shoot ("13-synth-and-sampler");
        // What Export the Mix asks first (0097).
        {
            window.exportAudio (false);
            shoot ("38-export-question");
            for (auto* ch : window.getChildren())
                if (auto* a = dynamic_cast<AlertWindow*> (ch)) a->exitModalState (0);
            MessageManager::getInstance()->runDispatchLoopUntil (50);
        }
            // Kalibre Sampler cutting a drum loop at its hits, a key each (0095).
            {
                const Id slicer = edit::addTrack (c.project, TrackKind::Track, "Slicer");
                const Id dev = edit::addDevice (c.project, slicer, samplerRef(), DeviceRole::Instrument);
                c.focusTrack = slicer;
                c.refresh();
                MessageManager::getInstance()->runDispatchLoopUntil (100);
                const auto loopFile = File::getSpecialLocation (File::tempDirectory).getChildFile ("kalibre-render-break.wav");
                {
                    AudioBuffer<float> b (2, 96000);
                    Random rnd (3);
                    const double pi = MathConstants<double>::pi;
                    for (int i = 0; i < b.getNumSamples(); ++i)
                    {
                        const double t = i / 48000.0, beat = std::fmod (t * 2, 1.0), eighth = std::fmod (t * 4, 1.0);
                        const bool kick = ((int) (t * 2) % 2) == 0;
                        const float v = (float) ((kick ? 0.9 * std::sin (2 * pi * 55 * t) : 0.6 * (rnd.nextFloat() * 2 - 1)) * std::exp (-beat * 9)
                                                 + 0.25 * (rnd.nextFloat() * 2 - 1) * std::exp (-eighth * 30));
                        b.setSample (0, i, v);
                        b.setSample (1, i, v);
                    }
                    loopFile.deleteFile();
                    WavAudioFormat wavOut;
                    std::unique_ptr<OutputStream> os (loopFile.createOutputStream().release());
                    if (auto w = wavOut.createWriterFor (os, AudioFormatWriterOptions().withSampleRate (48000).withNumChannels (2).withBitsPerSample (24)))
                        w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
                }
                if (auto* sampler = dynamic_cast<KalibreSampler*> (c.engine.instance (dev)))
                {
                    String error;
                    sampler->load (loopFile, error);
                    if (auto* mode = dynamic_cast<RangedAudioParameter*> (sampler->getParameters()[KalibreSampler::Mode]))
                        mode->setValueNotifyingHost (mode->convertTo0to1 (1.0f));
                }
                MessageManager::getInstance()->runDispatchLoopUntil (300);
                shoot ("37-sampler-slices");
                edit::removeTrack (c.project, slicer);
                c.focusTrack = synthTrack;
                c.refresh();
                loopFile.deleteFile();
            }
            // A short zone: shown smaller to fit, its shape kept (decision 0058).
            window.lowerHeight = 250;
            shoot ("18-fitted-to-a-short-zone");
            window.lowerHeight = 320;
            c.focusTrack = c.project.tracks[1].id;
        }
        c.project.tracks[0].collapsed = false;
        c.showTempoLane = true;
        c.project.tempo.setTempos ({ { 0, 96, true }, { PPQ * 16, 120, false } });
        window.setTextSize (2.0f);
        c.zoneTab = ZoneTab::Editor;
        c.editor = EditorKind::PianoRoll;
        c.selectClip (c.project.tracks[5].clips().front().id);
        c.refresh();
        shoot ("8-text-200");
        window.setTextSize (1.0f);   // and not left at 200% for the app's next run
    }
    LookAndFeel::setDefaultLookAndFeel (nullptr);
    return 0;
}
