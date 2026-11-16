// Draws the real window, with a demo song, into PNGs: one per view of the
// lower zone. Run on CI so every change can be looked at (decision 0023).
//
//   KalibreRender out-folder [scale]

#include "../Tests/App/Demo.h"
#include "Comp.h"
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
        PluginManager plugins;
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
