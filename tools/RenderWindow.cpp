// Draws the real window, with a demo song, into PNGs: one per view of the
// lower zone. Run on CI so every change can be looked at (decision 0023).
//
//   KalibreRender out-folder [scale]

#include "../Tests/App/Demo.h"
#include "BuiltinInstruments.h"
#include "DrumMachine.h"
#include "TransientQuestion.h"
#include "DrumPadsDevice.h"
#include "ContainerProcessor.h"
#include "Comp.h"
#include "Song.h"
#include "MainComponent.h"
#include "BattlePass.h"
#include "MultibandProcessor.h"
#include "StereoSplitProcessor.h"
#include "JsfxDevice.h"
#include "LuaTool/LuaTool.h"
#include "NoteSplitter.h"
#include "PianoRoll.h"
#include "ToolPicker.h"
#include "Arrange.h"
#include "ArticulationManagerView.h"
#include "PluginPictures.h"

using namespace juce;
using namespace kal;

namespace
{
// A sound card that's only a clock: the renderer drives it a block at a time,
// so pictures that need Kalibre playing (the step being played) can be taken.
struct RenderDevice : public AudioIODevice
{
    RenderDevice() : AudioIODevice ("Render", "Render") {}
    StringArray getOutputChannelNames() override { return { "L", "R" }; }
    StringArray getInputChannelNames() override { return {}; }
    Array<double> getAvailableSampleRates() override { return { 48000.0 }; }
    Array<int> getAvailableBufferSizes() override { return { 512 }; }
    int getDefaultBufferSize() override { return 512; }
    String open (const BigInteger&, const BigInteger&, double, int) override { return {}; }
    void close() override {}
    bool isOpen() override { return true; }
    void start (AudioIODeviceCallback*) override {}
    void stop() override {}
    bool isPlaying() override { return true; }
    String getLastError() override { return {}; }
    int getCurrentBufferSizeSamples() override { return 512; }
    double getCurrentSampleRate() override { return 48000.0; }
    int getCurrentBitDepth() override { return 32; }
    BigInteger getActiveOutputChannels() const override { return 3; }
    BigInteger getActiveInputChannels() const override { return 0; }
    int getOutputLatencyInSamples() override { return 0; }
    int getInputLatencyInSamples() override { return 0; }
};

void playFor (Engine& e, double seconds)
{
    const int n = 512, blocks = (int) std::ceil (seconds * 48000 / n);
    AudioBuffer<float> block (2, n);
    for (int b = 0; b < blocks; ++b)
    {
        block.clear();
        float* outs[] { block.getWritePointer (0), block.getWritePointer (1) };
        e.audioDeviceIOCallbackWithContext (nullptr, 0, outs, 2, n, {});
    }
}
} // namespace

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
        // Pictures of three of them (0138), made-up windows drawn here: knobs on a panel, keys under an instrument's.
        pictures::setFolder (pluginFolder.getChildFile ("Plug-in Pictures"));
        {
            auto mock = [] (Colour panel, Colour knob, bool keys)
            {
                Image img (Image::RGB, 900, 520, true);
                Graphics g (img);
                g.setGradientFill (ColourGradient (panel.brighter (0.15f), 0, 0, panel.darker (0.3f), 0, 520, false));
                g.fillAll();
                g.setColour (panel.darker (0.5f));
                g.fillRect (0, 0, 900, 48);
                for (int i = 0; i < 6; ++i)
                {
                    const float cx = 90.0f + (float) i * 144.0f, cy = keys ? 150.0f : 230.0f;
                    g.setColour (panel.darker (0.6f));
                    g.fillEllipse (cx - 46, cy - 46, 92, 92);
                    g.setColour (knob);
                    g.drawEllipse (cx - 40, cy - 40, 80, 80, 6.0f);
                    g.drawLine (cx, cy, cx + 30.0f * std::sin ((float) i), cy - 30.0f * std::cos ((float) i), 6.0f);
                }
                if (keys)
                {
                    g.setColour (Colours::white);
                    g.fillRect (20, 330, 860, 170);
                    g.setColour (Colours::black);
                    for (int k = 0; k < 30; ++k) g.fillRect (20 + k * 29, 330, 1, 170);
                    for (int k = 0; k < 30; ++k) if (k % 7 != 2 && k % 7 != 6) g.fillRect (20 + k * 29 + 19, 330, 18, 100);
                }
                return img;
            };
            auto describe = [&] (const String& name)
            {
                for (auto& d : plugins.known().getTypes()) if (d.name == name) return d;
                return PluginDescription();
            };
            pictures::keep (mock (Colour (0xff2b3240), Colour (0xffe0a040), true), describe ("Kontakt 8"));
            pictures::keep (mock (Colour (0xff5a3b2a), Colour (0xfff0d8b0), true), describe ("Pianoteq 9"));
            pictures::keep (mock (Colour (0xff1f3d4a), Colour (0xff70e0d0), false), describe ("ValhallaFutureVerb"));
        }
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
        // Sections (decision 0037).
        song::addSection (c.project, "Intro", 0, PPQ * 8);
        song::addSection (c.project, "Verse", PPQ * 8, PPQ * 16);
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
        // A part of the window, for the homepage's smaller pictures.
        auto shootArea = [&] (const String& name, Rectangle<int> area)
        {
            window.resized();
            MessageManager::getInstance()->runDispatchLoopUntil (50);
            auto image = window.createComponentSnapshot (area.getIntersection (window.getLocalBounds()), true, scale);
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
        // Every theme (decision 0132): the piano roll and the arrangement in each, and all of
        // them on one sheet, two across, each named, for the user guide.
        {
            const int n = (int) theme::palettes().size(), cellW = 720, cellH = 450 + 28;
            Image sheet (Image::RGB, cellW * 2, cellH * ((n + 1) / 2), true);
            Graphics sg (sheet);
            sg.fillAll (Colour (0xff111419));
            for (int i = 0; i < n; ++i)
            {
                window.setTheme (i);
                MessageManager::getInstance()->runDispatchLoopUntil (50);
                const String name (theme::palettes()[(size_t) i].name);
                shoot ("theme-" + String (i) + "-" + name.toLowerCase().replace (" ", "-"));
                window.resized();
                const auto image = window.createComponentSnapshot (window.getLocalBounds(), true, 1.0f);
                const int x = (i % 2) * cellW, y = (i / 2) * cellH;
                sg.setColour (Colour (0xffDDE1E7));
                sg.setFont (theme::font (15.0f));
                sg.drawText (name, x + 10, y, cellW - 20, 28, Justification::centredLeft, false);
                sg.drawImage (image, x, y + 28, cellW, cellH - 28, 0, 0, image.getWidth(), image.getHeight());
            }
            PNGImageFormat png;
            const auto f = out.getChildFile ("themes.png");
            f.deleteFile();
            FileOutputStream st (f);
            png.writeImageToStream (sheet, st);
            std::printf ("wrote %s\n", f.getFullPathName().toRawUTF8());
        }
        window.setTheme (0);
        shoot ("2-piano-roll");
        // The message line saying what's chosen: a note in the Keys' chords.
        c.dismiss();
        if (const auto* kc = c.project.clip (keysClip); kc && ! kc->midi.notes.empty()) c.selectedNotes = { kc->midi.notes.front().id };
        c.viewChanged();
        // The homepage's: the top bar and the message line, with each track's I/O; the
        // top bar's timer; the arm button four ways.
        shootArea ("53-top-bar-and-io", { 0, 0, window.getWidth(), 423 });
        shootArea ("54-top-bar-timer", { 0, 0, window.getWidth(), 120 });
        {
            const ArmFor ways[] { ArmFor::Audio, ArmFor::Midi, ArmFor::Both, ArmFor::MidiInput };
            std::vector<Id> armed;
            for (auto& t : c.project.tracks)
                if (t.holdsClips() && armed.size() < 4) armed.push_back (t.id);
            for (size_t i = 0; i < armed.size(); ++i) { auto* t = c.project.track (armed[i]); t->armed = true; t->armFor = ways[i]; }
            c.refresh();
            window.resized();
            MessageManager::getInstance()->runDispatchLoopUntil (50);
            Rectangle<int> headers;
            std::function<void (Component&)> find = [&] (Component& comp)
            {
                for (auto* ch : comp.getChildren())
                {
                    if (auto* h = dynamic_cast<TrackHeader*> (ch); h && h->isVisible()
                        && std::find (armed.begin(), armed.end(), h->id) != armed.end())
                        headers = headers.isEmpty() ? window.getLocalArea (h, h->getLocalBounds()) : headers.getUnion (window.getLocalArea (h, h->getLocalBounds()));
                    find (*ch);
                }
            };
            find (window);
            if (! headers.isEmpty()) shootArea ("55-arm-four-ways", headers.expanded (4));
            for (auto id : armed) c.project.track (id)->armed = false;
            c.refresh();
        }
        c.selectedNotes.clear();
        // The step sequencer's: a bass line written in steps, a bar after the song, on the Bass.
        Id stepsClip = 0;
        for (auto& t : c.project.tracks)
            if (t.name == "Bass")
            {
                stepsClip = edit::addMidiClip (c.project, t.id, PPQ * 16, PPQ * 4);
                const int line[16] { 45, 0, 45, 57, 0, 45, 52, 0, 43, 0, 43, 55, 0, 48, 50, 52 };
                for (int i = 0; i < 16; ++i)
                    if (line[i] > 0)
                    {
                        Note n;
                        n.start = (PPQ / 4) * i;
                        n.length = PPQ / 8;
                        n.pitch = line[i];
                        n.velocity = 70 + (i % 4 == 0 ? 40 : 0);
                        n.from = NoteFrom::Steps;
                        edit::addNote (c.project, stepsClip, n);
                    }
                c.refresh();
            }
        const Id drumsClip = c.project.tracks[4].clips().front().id;
        for (auto kind : { EditorKind::Drums, EditorKind::Steps, EditorKind::Euclid })
        {
            c.selectClip (kind == EditorKind::Steps && stepsClip ? stepsClip : drumsClip);
            c.editor = kind;
            c.zoneTab = ZoneTab::Editor;
            c.viewChanged();
            shoot (kind == EditorKind::Drums ? "3-drums" : kind == EditorKind::Steps ? "4-steps" : "5-euclidean");
            if (kind == EditorKind::Steps)
            {
                // Playing: the step being played lit, as the homepage shows it.
                RenderDevice device;
                c.engine.audioDeviceAboutToStart (&device);
                MessageManager::getInstance()->runDispatchLoopUntil (20);
                c.refresh();
                c.cursor = c.project.clip (c.selectedClip)->start;
                c.play();
                playFor (c.engine, 1.2);
                window.repaint();
                shoot ("56-steps-playing");
                c.stop();
                c.engine.audioDeviceStopped();
                c.refresh();
            }
        }
        // The bass line in steps goes again: the pictures after are of the demo song as it was.
        if (stepsClip) { edit::removeClips (c.project, { stepsClip }); c.refresh(); }
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
        // A question near the top, under the top bar (0110).
        {
            auto* q = new AlertWindow ("Save the changes to this project?", "If you don't, they're lost.", MessageBoxIconType::NoIcon);
            q->addButton ("Save", 1, KeyPress (KeyPress::returnKey));
            q->addButton ("Don't Save", 2);
            q->addButton ("Cancel", 0, KeyPress (KeyPress::escapeKey));
            ui::present (q, nullptr, true);
            shoot ("44-question-at-the-top");
            q->exitModalState (0);
            MessageManager::getInstance()->runDispatchLoopUntil (50);
        }
        // Split at Transients' question (decision 0130): its Sensitivity, and how many clips it makes.
        {
            Id vocalClip = 0;
            for (auto& t : c.project.tracks)
                if (t.name == "Vocal")
                    for (auto& cl : t.clips()) if (! cl.isMidi() && vocalClip == 0) vocalClip = cl.id;
            if (vocalClip != 0)
            {
                askTransients (c, "Split at Transients", { { vocalClip, {} } }, [] (double) {});
                MessageManager::getInstance()->runDispatchLoopUntil (50);
                shoot ("51-split-at-transients");
                if (auto* m = Component::getCurrentlyModalComponent()) m->exitModalState (0);
                MessageManager::getInstance()->runDispatchLoopUntil (50);
            }
        }
        window.showBrowser (Browser::Tab::Presets);
        c.zoneTab = ZoneTab::Mixer;
        c.viewChanged();
        shoot ("10-presets-and-loudness");
        // The studio and the keys: windows of their own (0110).
        auto shootWindow = [&] (DocumentWindow& w, const String& name)
        {
            w.setVisible (true);
            MessageManager::getInstance()->runDispatchLoopUntil (100);
            auto* content = w.getContentComponent();
            auto image = content->createComponentSnapshot (content->getLocalBounds(), true, scale);
            PNGImageFormat png;
            const auto f = out.getChildFile (name + ".png");
            f.deleteFile();
            FileOutputStream s (f);
            png.writeImageToStream (image, s);
            std::printf ("wrote %s\n", f.getFullPathName().toRawUTF8());
            w.setVisible (false);
        };
        shootWindow (window.showStudio(), "10-studio");
        // The browser's other tabs, for the user guide (docs/USER-GUIDE.md).
        c.zoneTab = ZoneTab::Devices;
        c.viewChanged();
        window.showBrowser (Browser::Tab::Plugins);
        shoot ("31-browser-plugins");
        // A heading folded away (0138).
        for (auto* ch : window.getChildren())
            if (auto* b = dynamic_cast<Browser*> (ch)) b->setShutHeadings ({ false, true, false });
        shoot ("31-browser-plugins-folded");
        for (auto* ch : window.getChildren())
            if (auto* b = dynamic_cast<Browser*> (ch)) b->setShutHeadings ({ false, false, false });
        window.showBrowser (Browser::Tab::Audio);
        shoot ("32-browser-audio");
        shootWindow (window.showKeys(), "33-keys");
        // DAW Battle Pass (0111): its welcome, then each page, in its own window.
        {
            const auto data = File::getSpecialLocation (File::tempDirectory).getChildFile ("kalibre-render-battle-pass");
            data.deleteRecursively();
            BattlePass::resourceFolder() = data.getChildFile ("Kalibre");
            BattlePass::reaperFolder() = data.getChildFile ("REAPER");
            BattlePass pass (c);
            pass.show();
            auto* w = pass.window();
            auto shootPass = [&] (const String& name, int wait = 900)
            {
                MessageManager::getInstance()->runDispatchLoopUntil (wait);
                auto* content = w->getContentComponent();
                auto image = content->createComponentSnapshot (content->getLocalBounds(), true, scale);
                PNGImageFormat png;
                const auto f = out.getChildFile (name + ".png");
                f.deleteFile();
                FileOutputStream st (f);
                png.writeImageToStream (image, st);
                std::printf ("wrote %s\n", f.getFullPathName().toRawUTF8());
            };
            pass.run (R"(
                local function up(fn, name)
                    for i = 1, 300 do local n, v = debug.getupvalue(fn, i); if not n then return end; if n == name then return v end end
                end
                local update = up(debug.getregistry().kal_deferred, "update")
                ui = up(update, "ui")
                return "ok")");
            // The welcome, as it is once its lines have come in (a Debug build draws slowly).
            pass.run ("ui.modals[1].t = 3; return 'ok'");
            shootPass ("45-battle-pass-welcome", 4000);
            for (auto* page : { "pass", "quests", "shop", "gear", "loot", "ideas", "trophies", "stats" })
            {
                pass.run (String ("ui.modals = {}; ui.tab = '") + page + "'; return 'ok'");
                shootPass (String ("46-battle-pass-") + page);
            }
            pass.hide();
            data.deleteRecursively();
        }
        // AI agents, switched on (decision 0099).
        {
            const auto was = c.studio;
            auto on = was;
            on.mcpOn = true;
            c.setStudio (on);
            window.browserWidth = 360;
            window.showAgents();
            MessageManager::getInstance()->runDispatchLoopUntil (700);
            window.showAgents();
            shootWindow (window.showStudio(), "39-ai-agents");
            c.setStudio (was);
            window.browserWidth = 320;
        }
        // Song versions 1 and 2 in the top bar (decision 0103): 2 made and playing.
        {
            c.newSongVersion();
            shoot ("41-song-versions");
            c.undo();
            c.say ("This is the message line: what just happened, with Undo; then what is chosen, or what the pointer is over.", false, true);
        }
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
            // Drums only (0106): each with its kind, read from its name or heard.
            sv.settings().shuffled = false;
            sv.settings().drumsOnly = true;
            sv.refreshRows();
            if (! sv.shown().empty()) sv.list().selectRow (0);
            shoot ("43-sounds-drums");
            sv.settings().drumsOnly = false;
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
        // The Multiband (decision 0126): four bands, plug-ins on two, a drum loop's spectrum.
        {
            const Id t = edit::addTrack (c.project, TrackKind::Track, "Mix Bus");
            const Id dev = edit::addDevice (c.project, t, multibandRef(), DeviceRole::AudioFx);
            c.focusTrack = t;
            c.zoneTab = ZoneTab::Devices;
            c.refresh();
            if (auto* mb = dynamic_cast<MultibandProcessor*> (engine.instance (dev)))
            {
                mb->setBands (4);
                auto put = [&] (int b, int i, const char* id, const char* name)
                {
                    PluginRef r { "Kalibre", id, name };
                    String error;
                    mb->loadPlugin (b, i, r, error);
                };
                put (0, 0, "test-gain:0", "Pro-C 2");
                put (2, 0, "test-gain:-2", "Saturn 2");
                put (2, 1, "test-gain:0", "Pro-Q 3");
                // Something to see: noise and two tones through it.
                AudioBuffer<float> b (2, 512);
                MidiBuffer none;
                Random rnd (3);
                for (int k = 0; k < 8; ++k)
                {
                    for (int i = 0; i < 512; ++i)
                    {
                        const float x = 0.08f * (rnd.nextFloat() - 0.5f) + 0.3f * std::sin ((float) (k * 512 + i) * 0.0131f) + 0.1f * std::sin ((float) (k * 512 + i) * 0.39f);
                        b.setSample (0, i, x);
                        b.setSample (1, i, x);
                    }
                    mb->processBlock (b, none);
                }
            }
            window.lowerHeight = 380;
            shoot ("47-multiband");
            window.lowerHeight = 320;
            edit::removeTrack (c.project, t);
            c.refresh();
        }
        // Stereo Split (decision 0127): Left, Middle, Right, plug-ins on two parts; a mix spread across the speakers.
        {
            const Id t = edit::addTrack (c.project, TrackKind::Track, "Mix Bus");
            const Id dev = edit::addDevice (c.project, t, stereoSplitRef(), DeviceRole::AudioFx);
            c.focusTrack = t;
            c.zoneTab = ZoneTab::Devices;
            c.refresh();
            if (auto* ss = dynamic_cast<StereoSplitProcessor*> (engine.instance (dev)))
            {
                auto put = [&] (int p, int i, const char* id, const char* name)
                {
                    PluginRef r { "Kalibre", id, name };
                    String error;
                    ss->loadPlugin (p, i, r, error);
                };
                put (StereoSplitProcessor::left, 0, "test-gain:0", "ValhallaRoom");
                put (StereoSplitProcessor::middle, 0, "test-gain:0", "Pro-C 2");
                put (StereoSplitProcessor::middle, 1, "test-gain:0", "Pro-Q 3");
                // Something to see: a bass in the middle, a pad leaning left, hats on the right, a little noise.
                AudioBuffer<float> b (2, 512);
                MidiBuffer none;
                Random rnd (5);
                for (int k = 0; k < 12; ++k)
                {
                    for (int i = 0; i < 512; ++i)
                    {
                        const float n = (float) (k * 512 + i);
                        const float bass = 0.35f * std::sin (n * 0.0105f), pad = 0.12f * std::sin (n * 0.061f) + 0.08f * std::sin (n * 0.092f);
                        const float hats = 0.05f * (rnd.nextFloat() - 0.5f) + 0.06f * std::sin (n * 0.71f), air = 0.01f * (rnd.nextFloat() - 0.5f);
                        b.setSample (0, i, bass + pad + 0.15f * hats + air);
                        b.setSample (1, i, bass + 0.35f * pad + hats - air);
                    }
                    ss->processBlock (b, none);
                }
            }
            window.lowerHeight = 380;
            shoot ("48-stereo-split");
            if (auto* ss = dynamic_cast<StereoSplitProcessor*> (engine.instance (dev)))
            {
                ss->setMode (StereoSplitProcessor::Mode::middleSides);
                PluginRef r { "Kalibre", "test-gain:0", "MicroShift" };
                String error;
                ss->loadPlugin (StereoSplitProcessor::sides, 0, r, error);
            }
            shoot ("49-stereo-split-sides");
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
            // Kalibre Drums (decision 0105): a kit of samples and a synth on a pad, a beat in its steps.
            {
                const Id drumTrack = edit::addTrack (c.project, TrackKind::Track, "Drums");
                const Id dev = edit::addDevice (c.project, drumTrack, drumsRef(), DeviceRole::Instrument);
                c.focusTrack = drumTrack;
                c.refresh();
                if (auto* dm = dynamic_cast<DrumMachine*> (c.engine.instance (dev)))
                {
                    auto kit = dm->kit();
                    const char* names[] { "Kick 808", "Snare Tight", "", "HH Closed 2", "HH Open", "Tom Low", "Tom High", "Crash 1" };
                    for (int i = 0; i < 8; ++i)
                    {
                        if (names[i][0] == 0) continue;
                        const auto f = File::getSpecialLocation (File::tempDirectory).getChildFile ("kalibre-render-drums").getChildFile (String (names[i]) + ".wav");
                        f.getParentDirectory().createDirectory();
                        AudioBuffer<float> b (2, 9600);
                        for (int n = 0; n < b.getNumSamples(); ++n)
                            for (int ch = 0; ch < 2; ++ch) b.setSample (ch, n, (float) (0.5 * std::sin (n * 0.05 * (i + 1)) * std::exp (-n / 2000.0)));
                        f.deleteFile();
                        WavAudioFormat wavOut;
                        std::unique_ptr<OutputStream> os (f.createOutputStream().release());
                        if (auto w = wavOut.createWriterFor (os, AudioFormatWriterOptions().withSampleRate (48000).withNumChannels (2).withBitsPerSample (24)))
                            w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
                        kit.pads[(size_t) i].sample = f.getFullPathName().toStdString();
                    }
                    kit.pads[2].plugin = synthRef();
                    kit.pads[0].pan = 0;
                    kit.pads[3].volumeDb = -4;
                    const int hits[8][16] = {
                        { 1,0,0,0, 0,0,0,0, 1,0,1,0, 0,0,0,0 }, { 0,0,0,0, 1,0,0,0, 0,0,0,0, 1,0,0,1 }, { 0,0,0,0, 0,0,0,0, 0,0,0,0, 1,0,0,0 },
                        { 1,0,1,0, 1,0,1,0, 1,0,1,0, 1,0,0,0 }, { 0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,1,0 }, { 0,0,0,0, 0,0,0,0, 0,0,0,0, 0,1,0,0 },
                        { 0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0 }, { 1,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0 } };
                    for (int l = 0; l < 8; ++l)
                        for (int st = 0; st < 16; ++st) kit.pattern.lanes[(size_t) l].velocity[(size_t) st] = hits[l][st] ? (st % 4 == 0 ? 120 : 90) : 0;
                    dm->setKit (kit);
                }
                c.zoneTab = ZoneTab::Devices;
                window.lowerHeight = 380;
                c.refresh();
                MessageManager::getInstance()->runDispatchLoopUntil (300);
                shoot ("42-kalibre-drums");
                edit::removeTrack (c.project, drumTrack);
                c.focusTrack = synthTrack;
                c.refresh();
            }
            // Drum Pads (decision 0128): sixteen of 128 pads, samples and a synth, a pad muted, the kick chosen.
            {
                const Id padTrack = edit::addTrack (c.project, TrackKind::Track, "Pads");
                const Id dev = edit::addDevice (c.project, padTrack, drumPadsRef(), DeviceRole::Instrument);
                c.focusTrack = padTrack;
                c.refresh();
                if (auto* dp = dynamic_cast<DrumPadsProcessor*> (c.engine.instance (dev)))
                {
                    auto kit = dp->kit();
                    const std::pair<int, const char*> sounds[] { { 36, "Kick 808" }, { 37, "Rim Dry" }, { 38, "Snare Tight" }, { 40, "Snare Ghost" },
                                                                 { 42, "HH Closed 2" }, { 44, "HH Pedal" }, { 46, "HH Open" }, { 45, "Tom Low" },
                                                                 { 48, "Tom High" }, { 49, "Crash 1" }, { 51, "Ride Bell" }, { 60, "Shaker" }, { 72, "Vox Hey" } };
                    int n = 0;
                    for (auto& [pad, name] : sounds)
                    {
                        const auto f = File::getSpecialLocation (File::tempDirectory).getChildFile ("kalibre-render-pads").getChildFile (String (name) + ".wav");
                        f.getParentDirectory().createDirectory();
                        AudioBuffer<float> b (2, 4800);
                        for (int i = 0; i < b.getNumSamples(); ++i)
                            for (int ch = 0; ch < 2; ++ch) b.setSample (ch, i, (float) (0.5 * std::sin (i * 0.04 * (++n % 7 + 1)) * std::exp (-i / 1500.0)));
                        f.deleteFile();
                        WavAudioFormat wavOut;
                        std::unique_ptr<OutputStream> os (f.createOutputStream().release());
                        if (auto w = wavOut.createWriterFor (os, AudioFormatWriterOptions().withSampleRate (48000).withNumChannels (2).withBitsPerSample (24)))
                            w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
                        kit.pads[(size_t) pad].sample = f.getFullPathName().toStdString();
                    }
                    kit.pads[39].plugin = synthRef();
                    kit.pads[39].name = "Synth Clap";
                    kit.pads[36].volumeDb = -2.5;
                    kit.pads[36].pan = -0.2;
                    kit.pads[40].mute = true;
                    kit.pads[49].choke = 2;
                    kit.pads[51].choke = 2;
                    dp->setKit (kit);
                    dp->selected = 36;
                }
                c.zoneTab = ZoneTab::Devices;
                window.lowerHeight = 380;
                c.refresh();
                MessageManager::getInstance()->runDispatchLoopUntil (300);
                shoot ("50-drum-pads");
                edit::removeTrack (c.project, padTrack);
                c.focusTrack = synthTrack;
                c.refresh();
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
        // The smaller sizes too (the musician's ask): more of the song in the window.
        window.setTextSize (0.75f);
        shoot ("8-text-75");
        window.setTextSize (0.5f);
        shoot ("8-text-50");
        window.setTextSize (1.0f);   // and not left at 200% for the app's next run
        // The Random button's song (decision 0101), from Kalibre's own instruments.
        c.randomSong ({}, 4242);
        window.hideBrowser();
        window.lowerHeight = 300;
        c.zoneTab = ZoneTab::Mixer;
        c.viewChanged();
        MessageManager::getInstance()->runDispatchLoopUntil (300);
        shoot ("40-random-song");
    }
    LookAndFeel::setDefaultLookAndFeel (nullptr);
    return 0;
}
