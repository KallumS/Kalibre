// The menus (decision 0027): in the Mac's own menu bar, so the window spends
// no space on them. Every command is here, with its key beside it.

#include "DeviceRack.h"
#include "MidiLearn.h"
#include "Song.h"
#include "MainComponent.h"

namespace kal
{
using namespace juce;

namespace
{
PopupMenu::Item item (const String& name, std::function<void()> action, const String& keys = {}, bool enabled = true, bool ticked = false)
{
    PopupMenu::Item i (name);
    i.setAction (std::move (action)).setEnabled (enabled).setTicked (ticked);
    i.shortcutKeyDescription = keys;
    return i;
}

String cmd (const char* k)
{
   #if JUCE_MAC
    return ui::u8 ("\xe2\x8c\x98") + k;
   #else
    return String ("Ctrl+") + k;
   #endif
}

String shiftCmd (const char* k)
{
   #if JUCE_MAC
    return ui::u8 ("\xe2\x87\xa7\xe2\x8c\x98") + k;
   #else
    return String ("Ctrl+Shift+") + k;
   #endif
}
} // namespace

StringArray MainComponent::getMenuBarNames()
{
    return { "File", "Edit", "Track", "Transport", "Automation", "View", "Scripts", "Help" };
}

PopupMenu MainComponent::appMenu()
{
    PopupMenu m;
    m.addItem (item (ui::u8 ("Audio and MIDI Settings\xe2\x80\xa6"), [this]
    {
        browserShown = true;
        browser.setVisible (true);
        browser.showTab (Browser::Tab::Audio);
        resized();
    }, cmd (",")));
    m.addItem (item (ui::u8 ("Sync and Control Surfaces\xe2\x80\xa6"), [this]
    {
        browserShown = true;
        browser.setVisible (true);
        browser.showTab (Browser::Tab::Studio);
        resized();
    }));
    m.addItem (item (ui::u8 ("Scan for Plug-ins"), [this]
    {
        c.engine.plugins().scan ([this] (float p, const String& what)
        {
            c.say (p >= 1.0f ? "Scan finished: " + what + "." : "Scanning plug-ins, " + String (roundToInt (p * 100)) + "%: " + what);
        });
    }));
    return m;
}

PopupMenu MainComponent::getMenuForIndex (int index, const String&)
{
    PopupMenu m;
    auto& p = c.project;
    switch (index)
    {
        case 0:   // File
        {
            m.addItem (item ("New", [this] { checkSaved ([this] { c.newProject(); }); }, cmd ("N")));
            m.addItem (item (ui::u8 ("Open\xe2\x80\xa6"), [this] { openFile(); }, cmd ("O")));
            m.addItem (item ("Open the Last Autosave", [this]
            {
                const auto f = c.autosaveFile();
                checkSaved ([this, f]
                {
                    if (! f.existsAsFile()) { c.say ("There's no autosave for this project yet: one is made every three minutes while there are changes."); return; }
                    const auto keep = c.file;
                    if (c.open (f)) { c.file = keep; c.dirty = true; c.say ("Opened the autosave from " + f.getLastModificationTime().toString (true, true, false) + ". Save to keep it."); }
                });
            }));
            m.addSeparator();
            m.addItem (item ("Save", [this] { saveFile(); }, cmd ("S")));
            m.addItem (item (ui::u8 ("Save As\xe2\x80\xa6"), [this] { c.file = File(); saveFile(); }, shiftCmd ("S")));
            m.addSeparator();
            m.addItem (item (ui::u8 ("Import Audio or MIDI\xe2\x80\xa6"), [this]
            {
                chooser = std::make_unique<FileChooser> ("Import", File::getSpecialLocation (File::userMusicDirectory), "*.wav;*.aif;*.aiff;*.flac;*.mp3;*.ogg;*.mid;*.midi");
                chooser->launchAsync (FileBrowserComponent::openMode | FileBrowserComponent::canSelectFiles | FileBrowserComponent::canSelectMultipleItems,
                                      [this] (const FileChooser& fc)
                {
                    Id track = c.focusTrack;
                    for (auto& f : fc.getResults()) { c.importFile (f, track, c.cursor); track = 0; }
                });
            }, cmd ("I")));
            m.addItem (item (ui::u8 ("Export the Mix\xe2\x80\xa6"), [this] { exportAudio (false); }, shiftCmd ("E")));
            PopupMenu loud;
            loud.addItem (item (ui::u8 ("For Streaming (-14 LUFS)\xe2\x80\xa6"), [this] { exportAudio (false, -14.0); }));
            loud.addItem (item (ui::u8 ("For Apple Music (-16 LUFS)\xe2\x80\xa6"), [this] { exportAudio (false, -16.0); }));
            loud.addItem (item (ui::u8 ("For Broadcast (-23 LUFS)\xe2\x80\xa6"), [this] { exportAudio (false, -23.0); }));
            m.addSubMenu ("Export the Mix at a Loudness", loud);
            m.addItem (item (ui::u8 ("Export Every Track\xe2\x80\xa6"), [this] { exportAudio (true); }));
            m.addItem (item (ui::u8 ("Export MIDI\xe2\x80\xa6"), [this] { exportMidi(); }));
           #if ! JUCE_MAC
            m.addSeparator();
            m.addSubMenu ("Kalibre", appMenu());
            m.addItem (item ("Quit", [] { JUCEApplication::getInstance()->systemRequestedQuit(); }, cmd ("Q")));
           #endif
            break;
        }
        case 1:   // Edit
        {
            m.addItem (item (c.history.canUndo() ? "Undo " + String (c.history.undoLabel()) : String ("Undo"), [this] { c.undo(); }, cmd ("Z"), c.history.canUndo()));
            m.addItem (item (c.history.canRedo() ? "Redo " + String (c.history.redoLabel()) : String ("Redo"), [this] { c.redo(); }, shiftCmd ("Z"), c.history.canRedo()));
            m.addSeparator();
            m.addItem (item ("Delete", [this] { c.deleteSelection(); }, ui::u8 ("\xe2\x8c\xab")));
            m.addItem (item ("Duplicate", [this] { c.duplicateSelection(); }, cmd ("D")));
            m.addItem (item ("Split at the Cursor", [this] { c.splitAtCursor(); }, cmd ("E")));
            m.addItem (item ("Choose Every Note", [this] { c.selectAllNotes(); }, cmd ("A"), c.selectedClip != 0));
            m.addSeparator();
            m.addItem (item ("Quantise", [this] { c.quantiseSelection(); }, "Q", c.selectedClip != 0));
            m.addItem (item ("Legato", [this] { const Id cid = c.selectedClip; auto ids = c.selectedNotes; c.apply ("Legato", [=] (Project& pr) { edit::legato (pr, cid, ids); }); }, {}, c.selectedClip != 0));
            m.addItem (item ("Humanise", [this] { const Id cid = c.selectedClip; auto ids = c.selectedNotes; c.apply ("Humanise", [=] (Project& pr) { edit::humanise (pr, cid, ids, PPQ / 48, 8, (unsigned) Time::currentTimeMillis()); }); }, {}, c.selectedClip != 0));
            m.addItem (item ("Join the Chosen Notes", [this] { const Id cid = c.selectedClip; auto ids = c.selectedNotes; c.apply ("Join Notes", [=] (Project& pr) { edit::joinNotes (pr, cid, ids); }); }, {}, c.selectedNotes.size() > 1));
            m.addItem (item ("Split the Chosen Notes at the Cursor", [this]
            {
                const Id cid = c.selectedClip;
                auto ids = c.selectedNotes;
                const auto* cl = c.clip();
                const Tick at = cl ? c.cursor - (cl->start - cl->offset) : 0;
                c.apply ("Split Notes", [=] (Project& pr) { edit::splitNotes (pr, cid, ids, at); });
            }, {}, ! c.selectedNotes.empty()));
            PopupMenu transpose;
            for (int s : { 12, 7, 1, -1, -7, -12 })
                transpose.addItem (item ((s > 0 ? "Up " : "Down ") + String (std::abs (s)), [this, s]
                {
                    const Id cid = c.selectedClip;
                    auto ids = c.selectedNotes;
                    if (ids.empty()) if (auto* cl = c.clip()) for (auto& n : cl->midi.notes) ids.insert (n.id);
                    c.apply ("Transpose", [=] (Project& pr) { edit::transpose (pr, cid, ids, s); });
                }));
            m.addSubMenu ("Transpose", transpose, c.selectedClip != 0);
            m.addItem (item ("Consolidate the Chosen Clips", [this] { auto clips = c.selectedClips; c.apply ("Consolidate", [clips] (Project& pr) { edit::consolidateClips (pr, clips); }); }, {}, c.selectedClips.size() > 1));
            m.addSeparator();
            // The song's shape (decision 0037).
            m.addItem (item ("Ripple Editing", [this] { c.ripple = ! c.ripple; c.say (c.ripple ? "Ripple editing is on: deleting a clip pulls the later clips on its track back." : "Ripple editing is off."); }, {}, true, c.ripple));
            m.addItem (item (ui::u8 ("Insert Bars at the Cursor\xe2\x80\xa6"), [this]
            {
                auto* w = new AlertWindow ("Insert Bars", "Empty bars go in at the cursor's bar; everything after moves later.", MessageBoxIconType::NoIcon);
                w->addTextEditor ("bars", "4");
                w->addButton ("Insert", 1, KeyPress (KeyPress::returnKey));
                w->addButton ("Cancel", 0, KeyPress (KeyPress::escapeKey));
                ui::present (w, ModalCallbackFunction::create ([this, w] (int ok)
                {
                    if (ok != 1) return;
                    const int bars = jlimit (1, 999, w->getTextEditorContents ("bars").getIntValue());
                    const int bar = c.project.tempo.barAt (c.cursor);
                    c.apply ("Insert Bars", [bar, bars] (Project& pr)
                    {
                        const Tick at = pr.tempo.barStart (bar);
                        song::insertTime (pr, at, pr.tempo.barStart (bar + bars) - at);
                    });
                }), true);
            }));
            const bool range = p.loop.end > p.loop.start;
            m.addItem (item ("Delete the Loop Range's Bars", [this] { const Tick a = c.project.loop.start, b = c.project.loop.end; c.apply ("Delete Bars", [a, b] (Project& pr) { song::removeTime (pr, a, b); }); }, {}, range));
            m.addItem (item ("Duplicate the Loop Range", [this] { const Tick a = c.project.loop.start, b = c.project.loop.end; c.apply ("Duplicate Bars", [a, b] (Project& pr) { song::duplicateTime (pr, a, b); }); }, {}, range));
            m.addItem (item ("Add a Section from the Loop Range", [this] { const Tick a = c.project.loop.start, b = c.project.loop.end; c.apply ("Add Section", [a, b] (Project& pr) { song::addSection (pr, "Section " + std::to_string (pr.sections.size() + 1), a, b); }); }, {}, range));
            break;
        }
        case 2:   // Track
        {
            m.addItem (item ("Add Track", [this] { c.addTrack (TrackKind::Track); }, cmd ("T")));
            m.addItem (item ("Add Folder", [this] { c.addTrack (TrackKind::Folder); }, shiftCmd ("T")));
            m.addItem (item ("Add Return", [this] { c.addTrack (TrackKind::Return); }));
            m.addItem (item ("Put the Chosen Tracks in a Folder", [this] { c.folderFromSelection(); }));
            m.addSeparator();
            const auto* t = c.focus();
            m.addItem (item ("New Version", [this] { c.newVersion (c.focusTrack); }, {}, t && t->holdsClips()));
            m.addItem (item (ui::u8 ("Add a Plug-in\xe2\x80\xa6"), [this] { DeviceRack::addMenu (c, c.focusTrack, nullptr); }, {}, t != nullptr));
            m.addItem (item ("Duplicate", [this] { c.duplicateSelectedTracks(); }));
            m.addItem (item ("Delete", [this] { c.deleteSelectedTracks(); }));
            m.addSeparator();
            m.addItem (item ("Arm for Recording", [this] { c.apply ("Arm", [this] (Project& pr) { if (auto* x = pr.track (c.focusTrack)) x->armed = ! x->armed; }); }, {}, t && t->holdsClips(), t && t->armed));
            PopupMenu maps;
            maps.addItem (item (ui::u8 ("New Articulation Map\xe2\x80\xa6"), [this]
            {
                // A starting map for an orchestral library's usual keyswitches, edited in the project.
                c.apply ("New Articulation Map", [] (Project& pr)
                {
                    ArticulationMap m2;
                    m2.id = pr.newId();
                    m2.name = "Strings " + std::to_string (pr.articulationMaps.size() + 1);
                    m2.articulations = {
                        { "Legato", 24, 100, -1, 0, -1, -1, -80, 0xff6FA8DC },
                        { "Sustain", 25, 100, -1, 0, -1, -1, -60, 0xff7FC8B6 },
                        { "Staccato", 26, 100, -1, 0, -1, -1, -20, 0xffE3C26F },
                        { "Spiccato", 27, 100, -1, 0, -1, -1, -15, 0xffE89A6B },
                        { "Pizzicato", 28, 100, -1, 0, -1, -1, -10, 0xffB892D9 },
                        { "Tremolo", 29, 100, -1, 0, -1, -1, -40, 0xffE07A86 },
                    };
                    pr.articulationMaps.push_back (m2);
                });
                c.say ("Made a map with six string articulations, each with its own keyswitch and delay. Set it on a track from the track's menu.");
            }));
            for (auto& map : p.articulationMaps)
            {
                PopupMenu one;
                for (size_t i = 0; i < map.articulations.size(); ++i)
                {
                    const auto& a = map.articulations[i];
                    one.addItem (item (String (a.name) + "  (key " + String (a.keyswitch) + ", " + String (a.delayMs, 0) + ui::u8 (" ms)\xe2\x80\xa6"), [this, mid = map.id, i]
                    {
                        auto* m3 = c.project.articulationMap (mid);
                        if (! m3) return;
                        const auto& art = m3->articulations[i];
                        auto* w = new AlertWindow (String (art.name), "Keyswitch note, and delay in milliseconds (negative plays it early):", MessageBoxIconType::NoIcon);
                        w->addTextEditor ("name", String (art.name), "Name");
                        w->addTextEditor ("key", String (art.keyswitch), "Keyswitch");
                        w->addTextEditor ("delay", String (art.delayMs, 0), "Delay (ms)");
                        w->addButton ("Set", 1, KeyPress (KeyPress::returnKey));
                        w->addButton ("Cancel", 0, KeyPress (KeyPress::escapeKey));
                        ui::present (w, ModalCallbackFunction::create ([this, w, mid, i] (int ok)
                        {
                            if (ok != 1) return;
                            const auto name = w->getTextEditorContents ("name").toStdString();
                            const int key = jlimit (-1, 127, w->getTextEditorContents ("key").getIntValue());
                            const double delay = jlimit (-1000.0, 1000.0, w->getTextEditorContents ("delay").getDoubleValue());
                            c.apply ("Articulation", [=] (Project& pr)
                            {
                                for (auto& mm : pr.articulationMaps)
                                    if (mm.id == mid && i < mm.articulations.size())
                                    {
                                        mm.articulations[i].name = name;
                                        mm.articulations[i].keyswitch = key;
                                        mm.articulations[i].delayMs = delay;
                                    }
                            });
                        }), true);
                    }));
                }
                one.addItem (item ("Add an Articulation", [this, mid = map.id]
                {
                    c.apply ("Add Articulation", [mid] (Project& pr)
                    {
                        for (auto& mm : pr.articulationMaps)
                            if (mm.id == mid) mm.articulations.push_back ({ "Articulation " + std::to_string (mm.articulations.size() + 1), 30 });
                    });
                }));
                maps.addSubMenu (String (map.name), one);
            }
            m.addSubMenu ("Articulation Maps", maps);
            break;
        }
        case 3:   // Transport
        {
            m.addItem (item (c.engine.isPlaying() ? "Stop" : "Play", [this] { c.togglePlay(); }, "Space"));
            m.addItem (item ("Record", [this] { c.record(); }, "R", true, c.engine.isRecording()));
            m.addItem (item ("Go to the Start", [this] { c.goToStart(); }, "Return"));
            m.addSeparator();
            m.addItem (item ("Loop", [this] { c.toggleLoop(); }, "L", true, p.loop.on));
            m.addItem (item ("Punch In and Out", [this] { c.apply ("Punch", [] (Project& pr) { pr.punch.on = ! pr.punch.on; }); }, {}, true, p.punch.on));
            m.addItem (item ("Set the Punch Range to the Loop", [this] { c.apply ("Punch Range", [] (Project& pr) { pr.punch.start = pr.loop.start; pr.punch.end = pr.loop.end; pr.punch.on = true; }); }));
            m.addItem (item ("Metronome", [this] { c.apply ("Metronome", [] (Project& pr) { pr.metronome.on = ! pr.metronome.on; }); }, {}, true, p.metronome.on));
            m.addItem (item ("Count-in", [this] { c.apply ("Count-in", [] (Project& pr) { pr.metronome.countIn = ! pr.metronome.countIn; }); }, {}, true, p.metronome.countIn));
            PopupMenu sub;
            for (int s : { 1, 2, 3, 4 })
                sub.addItem (item (s == 1 ? String ("Beats") : String ("Beats in ") + String (s), [this, s] { c.apply ("Click", [s] (Project& pr) { pr.metronome.subdivision = s; }); }, {}, true, p.metronome.subdivision == s));
            m.addSubMenu ("Metronome Clicks", sub);
            m.addSeparator();
            m.addItem (item ("Capture MIDI", [this] { c.captureMidi(); }, shiftCmd ("R")));
            m.addItem (item ("Capture Audio (the Last 30 Seconds)", [this] { c.captureAudio (30); }));
            m.addSeparator();
            m.addItem (item ("Tap Tempo", [this] { c.tapTempo(); }));
            m.addItem (item ("Ramp the Tempo to Here", [this]
            {
                // The previous tempo point ramps to a new point at the cursor.
                const Tick at = c.snapDown (c.cursor);
                c.apply ("Tempo Ramp", [at] (Project& pr)
                {
                    auto pts = pr.tempo.tempos();
                    for (auto& tp : pts) if (tp.tick < at) tp.rampToNext = false;
                    for (auto it = pts.rbegin(); it != pts.rend(); ++it) if (it->tick < at) { it->rampToNext = true; break; }
                    pr.tempo.setTempos (pts);
                    pr.tempo.setTempoAt (at, pr.tempo.bpmAt (at));
                });
                c.showTempoLane = true;
                c.say ("Added a tempo point at the cursor, ramping from the one before. Drag it in the tempo lane.");
            }));
            break;
        }
        case 4:   // Automation
        {
            m.addItem (item (c.lastTouched.valid() ? "Automate " + c.lastTouched.name : String ("Automate the Last Touched Control"),
                             [this] { c.automateLastTouched(); }, {}, c.lastTouched.valid()));
            PopupMenu mods;
            const char* kinds[] { "LFO", "ADSR Envelope", "ADR Envelope", "Envelope Follower", "Wavetable (Draw or Load a Sample)" };
            for (int k = 0; k < 5; ++k) mods.addItem (item (kinds[k], [this, k] { c.modulateLastTouched ((ModKind) k); }, {}, c.lastTouched.valid()));
            m.addSubMenu (c.lastTouched.valid() ? "Modulate " + c.lastTouched.name : String ("Modulate the Last Touched Control"), mods);
            m.addSeparator();
            m.addItem (item (c.lastTouched.valid() ? "MIDI Learn " + c.lastTouched.name + ui::u8 ("\xe2\x80\xa6") : ui::u8 ("MIDI Learn the Last Touched Control\xe2\x80\xa6"),
                             [this] { c.learnLastTouched(); }, cmd ("L"), c.lastTouched.valid()));
            PopupMenu maps;
            for (size_t i = 0; i < p.midiMappings.size(); ++i)
            {
                const auto& mm = p.midiMappings[i];
                const String key = (mm.controller == midimap::pitchBend ? String ("Pitch bend") : "CC " + String (mm.controller))
                                 + (mm.channel > 0 ? ", channel " + String (mm.channel) : String());
                maps.addItem (item ("Forget " + key + ui::u8 (" \xe2\x86\x92 ") + String (mm.name), [this, i] { c.forgetMapping (i); }));
            }
            if (maps.getNumItems() == 0) maps.addItem (item ("None yet", [] {}, {}, false));
            m.addSubMenu ("MIDI Mappings", maps);
            m.addSeparator();
            const auto* t = c.focus();
            const char* modes[] { "Off", "Read", "Touch", "Latch", "Write", "Trim" };
            for (int i = 0; i < 6; ++i)
                m.addItem (item (String ("Mode: ") + modes[i], [this, i] { c.apply ("Automation Mode", [this, i] (Project& pr) { if (auto* x = pr.track (c.focusTrack)) x->autoMode = (AutoMode) i; }); },
                                 {}, t != nullptr, t && (int) t->autoMode == i));
            break;
        }
        case 5:   // View
        {
            m.addItem (item ("Devices", [this] { c.zoneTab = ZoneTab::Devices; c.viewChanged(); }, "D", true, c.zoneTab == ZoneTab::Devices));
            m.addItem (item ("Editor", [this] { c.zoneTab = ZoneTab::Editor; c.viewChanged(); }, "E", true, c.zoneTab == ZoneTab::Editor));
            m.addItem (item ("Mixer", [this] { c.zoneTab = ZoneTab::Mixer; c.viewChanged(); }, "X", true, c.zoneTab == ZoneTab::Mixer));
            m.addItem (item ("Clip Launcher", [this] { c.zoneTab = ZoneTab::Launcher; c.viewChanged(); }, "G", true, c.zoneTab == ZoneTab::Launcher));
            m.addSeparator();
            m.addItem (item ("Browser", [this] { browserShown = ! browserShown; browser.setVisible (browserShown); resized(); }, cmd ("B"), true, browserShown));
            m.addItem (item ("Section Lane", [this] { c.showSectionLane = ! c.showSectionLane; c.viewChanged(); }, {}, true, c.showSectionLane));
            m.addItem (item ("Chord Lane", [this] { c.showChordLane = ! c.showChordLane; c.viewChanged(); }, {}, true, c.showChordLane));
            m.addItem (item ("Tempo Lane", [this] { c.showTempoLane = ! c.showTempoLane; c.viewChanged(); }, {}, true, c.showTempoLane));
            m.addItem (item ("Shade the Scale in the Piano Roll", [this] { c.apply ("Scale Shading", [] (Project& pr) { pr.scale.highlight = ! pr.scale.highlight; }); }, {}, true, p.scale.highlight));
            m.addItem (item ("Follow Playback", [this] { c.followPlayback = ! c.followPlayback; c.viewChanged(); }, {}, true, c.followPlayback));
            m.addItem (item ("Zoom to Fit", [this] { arrange.zoomToFit(); }, "F"));
            m.addSeparator();
            PopupMenu sizes;
            for (int s : { 100, 125, 150, 200 })
                sizes.addItem (item (String (s) + "%", [this, s] { setTextSize ((float) s / 100.0f); }, s == 100 ? cmd ("0") : String(), true, roundToInt (theme::scale() * 100) == s));
            m.addSubMenu ("Text Size", sizes);
            break;
        }
        case 6:   // Scripts
        {
            const auto folder = c.scriptsFolder();
            Array<File> scripts = folder.findChildFiles (File::findFiles, false, "*.lua");
            scripts.sort();
            for (auto& f : scripts)
                m.addItem (item (f.getFileNameWithoutExtension(), [this, f] { c.runScript (f); }));
            if (scripts.isEmpty()) m.addItem (item ("No scripts yet", [] {}, {}, false));
            m.addSeparator();
            m.addItem (item ("Show the Scripts Folder", [folder] { folder.revealToUser(); }));
            m.addItem (item ("Add the Example Scripts", [this, folder]
            {
                const auto examples = File::getSpecialLocation (File::currentApplicationFile)
                                         .getChildFile ("Contents/Resources/Scripts");
                int copied = 0;
                for (auto& f : examples.findChildFiles (File::findFiles, false, "*.lua"))
                    if (f.copyFileTo (folder.getChildFile (f.getFileName()))) ++copied;
                c.say (copied > 0 ? "Added " + String (copied) + " example scripts to the Scripts menu."
                                  : "The examples are in the Scripts folder of Kalibre's source; copy them to the Scripts folder.");
            }));
            break;
        }
        case 7:   // Help
        {
            m.addItem (item ("Keys", [this] { showShortcuts(); }));
            m.addItem (item ("Kalibre on GitHub", [] { URL ("https://github.com/KallumS/Kalibre").launchInDefaultBrowser(); }));
            break;
        }
        default: break;
    }
    return m;
}

} // namespace kal
