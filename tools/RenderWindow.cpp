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
        c.say ("This is the message line: it stays until dismissed, and can undo what it describes.", false, true);

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
        window.hideBrowser();
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
