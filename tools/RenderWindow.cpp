// Draws the real window, with a demo song, into PNGs: one per view of the
// lower zone. Run on CI so every change can be looked at (decision 0023).
//
//   KalibreRender out-folder [scale]

#include "../Tests/App/Demo.h"
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
