#include "Arrange.h"
#include "PluginMenu.h"
#include "TransientQuestion.h"
#include "ToolPicker.h"

#include "Song.h"

#include "Comp.h"
#include "PluginManager.h"

namespace kal
{
using namespace juce;

static Colour trackColour (const Track& t) { return Colour (t.colour); }

//==============================================================================
TrackHeader::TrackHeader (Arrange& a, Controller& ctrl, Id track) : id (track), arrange (a), c (ctrl)
{
    for (theme::HouseButton* b : std::initializer_list<theme::HouseButton*> { &version, &io, &mute, &solo, &arm, &fold }) addAndMakeVisible (b);
    io.setTitle ("Inputs and Outputs");
    io.onClick = [this] { if (c.optionAdds (id)) { refreshButtons(); return; } routingMenu(); };
    addAndMakeVisible (name);
    name.setEditable (false, true);
    name.setFont (theme::font());
    name.setInterceptsMouseClicks (false, false);
    name.onTextChange = [this]
    {
        const auto text = name.getText().toStdString();
        c.apply ("Rename Track", [this, text] (Project& p) { if (auto* t = p.track (id)) t->name = text; });
    };
    mute.setTitle ("Mute");
    solo.setTitle ("Solo");
    arm.setTitle ("Arm for Recording");
    fold.setTitle ("Fold");
    version.setTitle ("Versions");
    // Option-click on any of them adds the track to the selection instead (decision 0053).
    mute.onClick = [this] { if (c.optionAdds (id)) { refreshButtons(); return; } c.apply ("Mute", [this] (Project& p) { if (auto* t = p.track (id)) t->mute = ! t->mute; }); };
    solo.onClick = [this] { if (c.optionAdds (id)) { refreshButtons(); return; } c.apply ("Solo", [this] (Project& p) { if (auto* t = p.track (id)) t->solo = ! t->solo; }); };
    // Click: armed for audio (red); right-click: for MIDI (blue) (decision 0064).
    arm.onClick = [this]
    {
        if (c.optionAdds (id)) { refreshButtons(); return; }
        const bool right = std::exchange (arm.right, false), shift = std::exchange (arm.shift, false);
        c.arm (id, right ? (shift ? ArmFor::MidiInput : ArmFor::Midi) : (shift ? ArmFor::Both : ArmFor::Audio));
        refreshButtons();
    };
    fold.onClick = [this] { if (c.optionAdds (id)) { refreshButtons(); return; } c.apply ("Fold", [this] (Project& p) { if (auto* t = p.track (id)) t->collapsed = ! t->collapsed; }); };
    version.onClick = [this] { if (c.optionAdds (id)) { refreshButtons(); return; } versionMenu(); };
}

// A toggle pressed with Option chose the track instead: its state goes back.
void TrackHeader::refreshButtons()
{
    if (const auto* t = c.project.track (id)) update (*t);
}

void TrackHeader::update (const Track& t)
{
    // Not while it's being typed: setting a label's text closes its editor, and the
    // arrangement refreshes just after the double-click that opened it (found in the UAT).
    if (! name.isBeingEdited()) name.setText (t.name, dontSendNotification);
    name.setFont (theme::font());
    mute.setToggleState (t.mute, dontSendNotification);
    solo.setToggleState (t.solo, dontSendNotification);
    arm.setToggleState (t.armed, dontSendNotification);
    // Red for audio, blue for MIDI, half each for both; "In" for MIDI input (decisions 0064, 0065).
    const auto how = t.armFor;
    arm.getProperties().set ("onColour", (int64) (how == ArmFor::Audio || how == ArmFor::Both ? theme::armAudio : theme::armMidi).getARGB());
    if (how == ArmFor::Both) arm.getProperties().set ("onColour2", (int64) theme::armMidi.getARGB());
    else arm.getProperties().remove ("onColour2");
    arm.setButtonText (t.armed && how == ArmFor::MidiInput ? "In" : "R");
    const String ways = "Click: audio (red). Right-click: MIDI, as the instrument plays it (blue). Shift-click: both. Shift-right-click: MIDI as it comes in.";
    arm.setTip (! t.armed ? "Arm for recording. " + ways
                : how == ArmFor::Audio ? "Armed for audio: records the sound coming in. Click again to disarm. " + ways
                : how == ArmFor::Midi ? "Armed for MIDI: records what the instrument is played, arpeggiators and MIDI effects included. Right-click again to disarm. " + ways
                : how == ArmFor::Both ? "Armed for audio and MIDI: the sound coming in, and what the instrument is played. Shift-click again to disarm. " + ways
                                      : "Armed for MIDI input: records the notes as they come in, before any plug-in. Shift-right-click again to disarm. " + ways);
    arm.repaint();
    arm.setVisible (t.holdsClips());
    version.setVisible (t.holdsClips());
    version.setButtonText ("v" + String (t.activeVersion + 1));
    version.setTip ("Version " + String (t.activeVersion + 1) + " of " + String ((int) t.versions.size())
                    + ". Click to choose another, or make a new one.");
    io.setVisible (t.kind != TrackKind::Master);
    io.setTip (routingWords (t));
    fold.setVisible (t.isFolder());
    fold.setButtonText (t.collapsed ? String::fromUTF8 ("\xe2\x96\xb8") : String::fromUTF8 ("\xe2\x96\xbe"));
    depth = c.project.depth (t.id);
    resized();
    repaint();
}

void TrackHeader::paint (Graphics& g)
{
    const auto* t = c.project.track (id);
    if (! t) return;
    const bool chosen = std::find (c.selectedTracks.begin(), c.selectedTracks.end(), id) != c.selectedTracks.end();
    g.fillAll (chosen ? theme::raised : theme::ground);
    g.setColour (trackColour (*t));
    g.fillRect (depth * theme::px (10), 0, theme::px (4), getHeight());
    if (id == c.focusTrack)
    {
        // The track being played: a bar in the accent, shape and colour.
        g.setColour (theme::accent);
        g.fillRect (getWidth() - 3, 0, 3, getHeight());
    }
    if (const auto words = footnote(); words.isNotEmpty())
    {
        g.setColour (theme::textSecondary);
        g.setFont (theme::font (theme::smallPoints));
        g.drawText (words, getLocalBounds().reduced (6, 2).withTrimmedLeft (depth * theme::px (10) + theme::px (t->isFolder() ? 30 : 10)),
                    Justification::bottomLeft, false);
    }
    if (t->delayMs != 0)
    {
        g.setColour (theme::textSecondary);
        g.setFont (theme::font (theme::smallPoints));
        g.drawText ((t->delayMs > 0 ? "+" : "") + ui::number (t->delayMs, 0) + " ms", getLocalBounds().reduced (6, 2), Justification::bottomRight, false);
    }
    g.setColour (theme::rule);
    g.fillRect (0, getHeight() - 1, getWidth(), 1);
}

String TrackHeader::footnote() const
{
    const auto* t = c.project.track (id);
    if (! t) return {};
    if (t->isFolder()) return String ((int) c.project.childrenOf (id).size()) + " inside";
    if (t->frozen) return "Frozen";
    return {};
}

void TrackHeader::resized()
{
    auto r = getLocalBounds().reduced (theme::px (4), theme::px (4)).withTrimmedLeft (depth * theme::px (10) + theme::px (6));
    auto top = r.removeFromTop (theme::px (theme::smallControl));
    if (fold.isVisible()) { fold.setBounds (top.removeFromLeft (theme::px (22))); top.removeFromLeft (2); }
    const int b = theme::px (22);
    for (theme::HouseButton* btn : std::initializer_list<theme::HouseButton*> { &arm, &solo, &mute })
        if (btn->isVisible()) { btn->setBounds (top.removeFromRight (b)); top.removeFromRight (2); }
    if (io.isVisible()) { io.setBounds (top.removeFromRight (theme::px (30))); top.removeFromRight (2); }
    if (version.isVisible()) { version.setBounds (top.removeFromRight (theme::px (30))); top.removeFromRight (4); }
    name.setBounds (top);
}

void TrackHeader::versionMenu()
{
    const auto* t = c.project.track (id);
    if (! t) return;
    PopupMenu m;
    for (size_t i = 0; i < t->versions.size(); ++i)
        m.addItem ((int) i + 1, String (t->versions[i].name.empty() ? "Version " + std::to_string (i + 1) : t->versions[i].name)
                                     + "  (" + String ((int) t->versions[i].clips.size()) + " clips)",
                   true, (int) i == t->activeVersion);
    m.addSeparator();
    const bool room = (int) t->versions.size() < maxVersions;   // four at most (decision 0050)
    m.addItem (1000, "New Version", room);
    m.addItem (1001, "New Empty Version", room);
    m.addItem (1002, ui::u8 ("Rename Version\xe2\x80\xa6"));
    m.addItem (1003, "Delete This Version", t->versions.size() > 1);
    m.showMenuAsync (PopupMenu::Options().withTargetComponent (&version), [this] (int r)
    {
        if (r <= 0) return;
        if (r < 1000) { c.chooseVersion ({ id }, r - 1); return; }
        if (r == 1000) c.newVersion (id);
        if (r == 1001) { c.storeDeviceStates(); c.apply ("New Version", [this] (Project& p) { edit::newEmptyVersion (p, id); }); }
        if (r == 1002)
        {
            auto* w = new AlertWindow ("Rename Version", {}, MessageBoxIconType::NoIcon);
            w->addTextEditor ("name", String (c.project.track (id)->version().name));
            w->addButton ("Rename", 1, KeyPress (KeyPress::returnKey));
            w->addButton ("Cancel", 0, KeyPress (KeyPress::escapeKey));
            ui::present (w, ModalCallbackFunction::create ([this, w] (int ok)
            {
                if (ok != 1) return;
                const auto text = w->getTextEditorContents ("name").toStdString();
                c.apply ("Rename Version", [this, text] (Project& p)
                {
                    if (auto* t2 = p.track (id)) edit::renameVersion (p, id, t2->activeVersion, text);
                });
            }), true);
        }
        if (r == 1003)
        {
            const int v = c.project.track (id)->activeVersion;
            c.apply ("Delete Version", [this, v] (Project& p) { edit::deleteVersion (p, id, v); });
            c.say ("Deleted the version and its clips.", false, true);
        }
    });
}

void TrackHeader::showMenu()
{
    const auto* t = c.project.track (id);
    if (! t) return;
    PopupMenu m, colours, autoModes;
    if (t->holdsClips())
    {
        m.addItem ("New Version", (int) t->versions.size() < maxVersions, false, [this] { c.newVersion (id); });
        if (! t->version().takes.empty())
            m.addItem (t->version().takesShown ? "Hide Takes" : "Show Takes (" + String ((int) t->version().takes.size()) + ")", [this]
            {
                c.apply ("Takes", [this] (Project& p) { auto& v = p.track (id)->version(); v.takesShown = ! v.takesShown; });
            });
        m.addSeparator();
    }
    m.addItem ("Add Track", [this] { c.addTrack (TrackKind::Track); });
    m.addItem ("Add Folder", [this] { c.addTrack (TrackKind::Folder); });
    m.addItem ("Add Return", [this] { c.addTrack (TrackKind::Return); });
    m.addItem ("Add Summing Track", [this] { c.addTrack (TrackKind::Summing); });
    m.addItem ("Add Cue Track", c.project.cue.empty(), false, [this] { c.addTrack (TrackKind::Cue); });
    m.addItem ("Put in a Folder", [this] { c.folderFromSelection(); });
    if (t->parent != 0)
        m.addItem ("Take Out of Its Folder", [this]
        {
            c.apply ("Take Out of Folder", [this] (Project& p)
            {
                auto* tr = p.track (id);
                const auto* parent = tr ? p.track (tr->parent) : nullptr;
                const Id grand = parent ? parent->parent : 0;
                edit::moveTrack (p, id, 0, grand);
            });
        });
    m.addSeparator();
    m.addItem (ui::u8 ("Rename\xe2\x80\xa6"), [this] { name.showEditor(); });
    for (int i = 0; i < theme::trackColours().size(); ++i)
    {
        const auto col = theme::trackColours()[i];
        PopupMenu::Item item ("Colour " + String (i + 1));
        item.setColour (col).setTicked (col.getARGB() == t->colour).setAction ([this, col]
        {
            c.apply ("Track Colour", [this, col] (Project& p) { if (auto* tr = p.track (id)) tr->colour = col.getARGB(); });
        });
        colours.addItem (item);
    }
    m.addSubMenu ("Colour", colours);
    m.addItem (ui::u8 ("Track Delay\xe2\x80\xa6"), [this]
    {
        auto* w = new AlertWindow ("Track Delay", "Milliseconds. Negative plays the track early, for sounds that are slow to start.", MessageBoxIconType::NoIcon);
        w->addTextEditor ("ms", ui::number (c.project.track (id)->delayMs, 0));
        w->addButton ("Set", 1, KeyPress (KeyPress::returnKey));
        w->addButton ("Cancel", 0, KeyPress (KeyPress::escapeKey));
        ui::present (w, ModalCallbackFunction::create ([this, w] (int ok)
        {
            if (ok != 1) return;
            const double ms = jlimit (-2000.0, 2000.0, w->getTextEditorContents ("ms").getDoubleValue());
            c.apply ("Track Delay", [this, ms] (Project& p) { if (auto* tr = p.track (id)) tr->delayMs = ms; });
        }), true);
    });
    const char* modes[] { "Off", "Read", "Touch", "Latch", "Write", "Trim" };
    for (int i = 0; i < 6; ++i)
        autoModes.addItem (modes[i], true, (int) t->autoMode == i, [this, i]
        {
            c.apply ("Automation Mode", [this, i] (Project& p) { if (auto* tr = p.track (id)) tr->autoMode = (AutoMode) i; });
        });
    m.addSubMenu ("Automation", autoModes);
    if (t->holdsClips())
    {
        PopupMenu maps;
        maps.addItem ("None", true, t->articulationMap == 0, [this] { c.apply ("Articulation Map", [this] (Project& p) { if (auto* tr = p.track (id)) tr->articulationMap = 0; }); });
        for (auto& map : c.project.articulationMaps)
            maps.addItem (String (map.name), true, map.id == t->articulationMap, [this, mid = map.id]
            {
                c.apply ("Articulation Map", [this, mid] (Project& p) { if (auto* tr = p.track (id)) tr->articulationMap = mid; });
            });
        m.addSubMenu ("Articulation Map", maps);
    }
    if (t->kind != TrackKind::Master)
    {
        // Track groups (decision 0037): volume, pan, mute, solo and arm move together.
        PopupMenu groups;
        groups.addItem ("None", true, t->group == 0, [this] { c.apply ("Group", [this] (Project& p) { for (auto tid : c.selectedTracks) if (auto* tr = p.track (tid)) tr->group = 0; if (auto* tr = p.track (id)) tr->group = 0; }); });
        for (int g = 1; g <= 8; ++g)
        {
            int members = 0;
            for (auto& o : c.project.tracks) if (o.group == g) ++members;
            groups.addItem ("Group " + String (g) + (members ? " (" + String (members) + ")" : String()), true, t->group == g, [this, g]
            {
                c.apply ("Group", [this, g] (Project& p)
                {
                    if (auto* tr = p.track (id)) tr->group = g;
                    for (auto tid : c.selectedTracks) if (auto* tr = p.track (tid)) tr->group = g;
                });
            });
        }
        m.addSubMenu ("Group", groups);
    }
    m.addSeparator();
    if (t->holdsClips())
        m.addItem (t->frozen ? "Unfreeze" : "Freeze", [this, frozen = t->frozen] { c.freeze (id, ! frozen); });
    m.addItem ("Lock", true, t->locked, [this] { c.apply ("Lock", [this] (Project& p) { if (auto* tr = p.track (id)) tr->locked = ! tr->locked; }); });
    m.addItem ("Duplicate", [this] { c.duplicateSelectedTracks(); });
    m.addItem ("Delete", [this] { c.deleteSelectedTracks(); });
    m.showMenuAsync (PopupMenu::Options());
}

// The I/O button beside M, S and R (decision 0074): where the track listens
// and where it plays to, in one menu, each choice ticked.
void TrackHeader::routingMenu()
{
    const auto* t = c.project.track (id);
    if (! t || t->kind == TrackKind::Master) return;
    PopupMenu m;
    if (t->holdsClips())
    {
        m.addSectionHeader ("Audio In");
        const int inputs = c.engine.devices().getCurrentAudioDevice()
                             ? c.engine.devices().getCurrentAudioDevice()->getActiveInputChannels().countNumberOfSetBits() : 2;
        auto setInput = [this] (int ch, bool stereo)
        {
            c.apply ("Input", [this, ch, stereo] (Project& p) { if (auto* tr = p.track (id)) { tr->input.audioChannel = ch; tr->input.stereo = stereo; } });
        };
        m.addItem ("None", true, t->input.audioChannel < 0, [setInput] { setInput (-1, true); });
        for (int ch = 0; ch + 1 < jmax (2, inputs); ch += 2)
            m.addItem ("Inputs " + String (ch + 1) + "-" + String (ch + 2) + " (Stereo)", true,
                       t->input.audioChannel == ch && t->input.stereo, [setInput, ch] { setInput (ch, true); });
        PopupMenu mono;
        for (int ch = 0; ch < jmax (2, inputs); ++ch)
            mono.addItem ("Input " + String (ch + 1), true, t->input.audioChannel == ch && ! t->input.stereo, [setInput, ch] { setInput (ch, false); });
        m.addSubMenu ("Mono", mono, true, nullptr, t->input.audioChannel >= 0 && ! t->input.stereo);

        m.addSectionHeader ("MIDI In");
        auto setMidi = [this] (std::string dev)
        {
            c.apply ("MIDI Input", [this, dev] (Project& p) { if (auto* tr = p.track (id)) tr->input.midiDevice = dev; });
        };
        m.addItem ("Every Keyboard", true, t->input.midiDevice == "All", [setMidi] { setMidi ("All"); });
        m.addItem ("None", true, t->input.midiDevice == "None", [setMidi] { setMidi ("None"); });
        for (auto& d : MidiInput::getAvailableDevices())
            m.addItem (d.name, true, t->input.midiDevice == d.identifier.toStdString(), [setMidi, idf = d.identifier.toStdString()] { setMidi (idf); });
        PopupMenu midiCh;
        for (int ch = 0; ch <= 16; ++ch)
            midiCh.addItem (ch == 0 ? String ("Every Channel") : "Channel " + String (ch), true, t->input.midiChannel == ch, [this, ch]
            {
                c.apply ("MIDI Channel", [this, ch] (Project& p) { if (auto* tr = p.track (id)) tr->input.midiChannel = ch; });
            });
        m.addSubMenu ("MIDI Channel: " + (t->input.midiChannel == 0 ? String ("Every") : String (t->input.midiChannel)), midiCh);
        m.addItem ("MPE (a Channel for Each Note)", true, t->mpe, [this]
        {
            c.apply ("MPE", [this] (Project& p) { if (auto* tr = p.track (id)) tr->mpe = ! tr->mpe; });
            if (const auto* tr = c.project.track (id); tr && tr->mpe)
                c.say ("MPE is on: each note plays on its own channel with its own bend, pressure and slide. Draw them in the piano roll's Note lanes.");
        });
        PopupMenu monitor;
        const char* mon[] { "Auto (while armed)", "On", "Off" };
        for (int i = 0; i < 3; ++i)
            monitor.addItem (mon[i], true, (int) t->monitor == i, [this, i]
            {
                c.apply ("Monitoring", [this, i] (Project& p) { if (auto* tr = p.track (id)) tr->monitor = (Monitor) i; });
            });
        m.addSubMenu (String ("Monitor Input: ") + mon[(int) t->monitor], monitor);
    }

    m.addSectionHeader ("Out");
    m.addItem (t->parent ? "Its Folder" : "The Master", true, t->output == 0, [this]
    {
        c.apply ("Output", [this] (Project& p) { if (auto* tr = p.track (id)) tr->output = 0; });
    });
    for (auto& r : c.project.tracks)
        if ((r.kind == TrackKind::Return || r.isFolder()) && r.id != id && r.id != t->parent && edit::canRoute (c.project, id, r.id))
            m.addItem (String (r.name), true, t->output == r.id, [this, rid = r.id]
            {
                c.apply ("Output", [this, rid] (Project& p) { if (auto* tr = p.track (id)) tr->output = rid; });
            });
    PopupMenu sends;
    for (auto& r : c.project.tracks)
        if (r.kind == TrackKind::Return && r.id != id)
            sends.addItem (String (r.name), edit::canRoute (c.project, id, r.id), false, [this, rid = r.id]
            {
                c.apply ("Add Send", [this, rid] (Project& p) { edit::addSend (p, id, rid); });
            });
    if (sends.getNumItems() == 0) sends.addItem ("Add a Return first", false, false, [] {});
    m.addSubMenu ("Send To", sends);
    m.showMenuAsync (PopupMenu::Options().withTargetComponent (&io));
}

// In words, for the I/O button's tooltip.
String TrackHeader::routingWords (const Track& t) const
{
    String in, out;
    if (t.holdsClips())
    {
        in = t.input.audioChannel < 0 ? String ("no audio")
           : t.input.stereo ? "inputs " + String (t.input.audioChannel + 1) + "-" + String (t.input.audioChannel + 2)
                            : "input " + String (t.input.audioChannel + 1);
        String midi = t.input.midiDevice == "All" ? String ("every keyboard") : t.input.midiDevice == "None" ? String ("no MIDI") : String ("one keyboard");
        if (t.input.midiChannel > 0) midi << ", channel " << t.input.midiChannel;
        in = "In: " + in + "; " + midi + ". ";
    }
    const auto* o = c.project.track (t.output);
    out = "Out: " + (o ? String (o->name) : t.parent ? String ("its folder") : String ("the master")) + ".";
    return in + out + " Click to change.";
}

// The track panel's own menu, under the tracks: a new one goes at the end,
// outside any folder.
// Fix Note Timings and Volume (decision 0085): how, then the track is rendered quietly and its notes fixed.
AlertWindow* Arrange::fixNotesDialog (const Controller::NoteFixOptions& o)
{
    auto* w = new AlertWindow ("Fix Note Timings and Volume",
                               "Kalibre renders the track quietly, finds where each note is really heard and how loud, deletes the render, "
                               "and fixes the notes so each lands where it's written and matches the others of its articulation.",
                               MessageBoxIconType::NoIcon);
    w->addComboBox ("timing", { "Give Each Note Its Own Delay (It Stays on the Grid)", "Move the Notes", "Leave the Timing Alone" }, "Timing");
    w->addComboBox ("volume", { "CC7 (Volume)", "CC11 (Expression)", "Velocity", "Leave the Volume Alone" }, "Volume");
    w->addComboBox ("align", { "The Attack", "The Very Start of the Sound" }, "Line Up");
    w->getComboBoxComponent ("timing")->setSelectedItemIndex ((int) o.timing, dontSendNotification);
    w->getComboBoxComponent ("volume")->setSelectedItemIndex ((int) o.volume, dontSendNotification);
    w->getComboBoxComponent ("align")->setSelectedItemIndex (o.toAttack ? 0 : 1, dontSendNotification);
    w->addButton ("Fix", 1, KeyPress (KeyPress::returnKey));
    w->addButton ("Cancel", 0, KeyPress (KeyPress::escapeKey));
    return w;
}

void Arrange::fixNotes (Id clip)
{
    auto* w = fixNotesDialog (c.lastNoteFix);
    ui::present (w, ModalCallbackFunction::create ([this, w, clip] (int ok)
    {
        if (ok != 1) return;
        Controller::NoteFixOptions chosen;
        chosen.timing = (Controller::NoteFixOptions::Timing) jmax (0, w->getComboBoxComponent ("timing")->getSelectedItemIndex());
        chosen.volume = (Controller::NoteFixOptions::Volume) jmax (0, w->getComboBoxComponent ("volume")->getSelectedItemIndex());
        chosen.toAttack = w->getComboBoxComponent ("align")->getSelectedItemIndex() != 1;
        c.lastNoteFix = chosen;
        c.fixNoteTimingsAndVolume (clip, chosen);
    }), true);
}

void Arrange::trackPanelMenu()
{
    auto add = [this] (TrackKind k) { return [this, k] { c.focusTrack = 0; c.addTrack (k); }; };
    PopupMenu m;
    m.addItem ("Add Track", add (TrackKind::Track));
    m.addItem ("Add Folder", add (TrackKind::Folder));
    m.addItem ("Add Return", add (TrackKind::Return));
    m.addItem ("Add Summing Track", add (TrackKind::Summing));
    m.addItem ("Add Cue Track", c.project.cue.empty(), false, add (TrackKind::Cue));
    m.showMenuAsync (PopupMenu::Options());
}

void TrackHeader::mouseDown (const MouseEvent& e)
{
    dragging = false;
    if (e.mods.isPopupMenu())
    {
        if (std::find (c.selectedTracks.begin(), c.selectedTracks.end(), id) == c.selectedTracks.end()) c.selectTracks ({ id });
        showMenu();
        return;
    }
    // Shift-click a range, Option-click adds, Command-click adds or takes away (decision 0053).
    c.chooseTrack (id, e.mods, c.arrangeOrder());
}

void TrackHeader::mouseDrag (const MouseEvent& e)
{
    if (e.getDistanceFromDragStart() > 6) dragging = true;
}

void TrackHeader::mouseUp (const MouseEvent& e)
{
    if (dragging) arrange.reorder (id, e.getEventRelativeTo (&arrange).y);
    dragging = false;
}

void TrackHeader::mouseDoubleClick (const MouseEvent&) { name.showEditor(); }

//==============================================================================
Arrange::Arrange (Controller& ctrl) : c (ctrl)
{
    headerColumn.setInterceptsMouseClicks (false, true);
    addAndMakeVisible (headerColumn);
    addAndMakeVisible (vbar);
    addAndMakeVisible (hbar);
    vbar.setAutoHide (false);
    hbar.setAutoHide (false);
    vbar.addListener (this);
    hbar.addListener (this);
    setWantsKeyboardFocus (false);
    setTitle ("Tracks");
    c.addChangeListener (this);
    // The playhead follows the screen's own refresh, not a timer of its own: a timer beats
    // against the screen and the audio's blocks, and the playhead stuttered (0121).
    vblank = std::make_unique<VBlankAttachment> (this, [this] { timerCallback(); });
    changeListenerCallback (nullptr);
}

Arrange::~Arrange() { c.removeChangeListener (this); }

void Arrange::scrollBarMoved (ScrollBar* bar, double v)
{
    if (bar == &vbar) { scrollY = (int) v; layoutRows(); }
    else timeline.startTick = v;
    repaint();
}

int Arrange::rulerHeight() const
{
    return theme::px (36) + sectionLaneHeight() + (c.showChordLane ? theme::px (24) : 0) + (c.showTempoLane ? theme::px (48) : 0);
}

Rectangle<int> Arrange::laneArea() const
{
    return getLocalBounds().withTrimmedTop (rulerHeight()).withTrimmedLeft (headerWidth())
                           .withTrimmedRight (theme::px (12)).withTrimmedBottom (theme::px (12));
}

void Arrange::layoutRows()
{
    rows.clear();
    int y = 0;
    std::set<Id> shown;
    for (auto& t : c.project.tracks)
    {
        if (t.hidden || c.project.hiddenByFolder (t.id)) continue;
        shown.insert (t.id);
        const int h = laneHeightOf (t);
        rows.push_back ({ t.id, y, h, -1 });
        y += h;
        for (size_t i = 0; i < t.automation.size(); ++i)
            if (t.automation[i].shown)
            {
                rows.push_back ({ t.id, y, theme::px (48), (int) i });
                y += theme::px (48);
            }
        // Takes, oldest first, when shown (decision 0029).
        if (t.holdsClips() && t.version().takesShown)
            for (size_t i = 0; i < t.version().takes.size(); ++i)
            {
                rows.push_back ({ t.id, y, theme::px (32), -1, (int) i });
                y += theme::px (32);
            }
    }
    contentHeight = y;

    // Headers: one per track row, made or reused.
    for (int i = headers.size(); --i >= 0;)
        if (! shown.count (headers[i]->id)) headers.remove (i);
    const int top = rulerHeight();
    headerColumn.setBounds (0, top, headerWidth(), jmax (0, getHeight() - top));
    for (auto& r : rows)
    {
        if (! r.isClips()) continue;
        TrackHeader* h = nullptr;
        for (auto* x : headers) if (x->id == r.track) h = x;
        if (! h) { h = headers.add (new TrackHeader (*this, c, r.track)); headerColumn.addAndMakeVisible (h); }
        h->setBounds (0, r.y - scrollY, headerWidth(), r.h);
        h->setVisible (r.y - scrollY + r.h > 0 && r.y - scrollY < headerColumn.getHeight());
        if (auto* t = c.project.track (r.track)) h->update (*t);
    }
    const auto lanes = laneArea();
    vbar.setRangeLimits (0, jmax (contentHeight + theme::px (80), lanes.getHeight()));
    vbar.setCurrentRange (scrollY, lanes.getHeight(), dontSendNotification);
    const double end = (double) jmax (c.project.endTick(), c.project.tempo.barStart (8)) + c.project.tempo.barStart (8);
    hbar.setRangeLimits (0, end);
    hbar.setCurrentRange (timeline.startTick, lanes.getWidth() / timeline.pixelsPerTick, dontSendNotification);
}

void Arrange::resized()
{
    const auto lanes = laneArea();
    vbar.setBounds (getWidth() - theme::px (12), lanes.getY(), theme::px (12), lanes.getHeight());
    hbar.setBounds (lanes.getX(), getHeight() - theme::px (12), lanes.getWidth(), theme::px (12));
    layoutRows();
}

void Arrange::changeListenerCallback (ChangeBroadcaster*)
{
    // The chord lane is read again only when the notes might have changed.
    const auto stamp = (uint32) c.history.size() * 7919u + (uint32) c.project.nextId;
    if (stamp != chordsFor && c.showChordLane)
    {
        chordsFor = stamp;
        chords = harmony::chordsAlong (c.project, 0, c.project.endTick(), true);   // only the clips asked for (0071)
        chordSounding = -1;   // found again on the next refresh
    }
    layoutRows();
    repaint();
}

void Arrange::timerCallback()
{
    // Follow playback: page along when the playhead leaves the view.
    if (c.engine.isPlaying() && c.followPlayback && drag == Drag::None)
    {
        const auto lanes = laneArea();
        const float x = timeline.x (c.playhead());
        // Scrolling (click Follow) keeps the playhead near the left; turning the page
        // (Shift-click) waits for it to reach the right edge, then shows the next screenful (0070).
        const float edge = c.followPages ? 1.0f : 0.85f;
        if (x > (float) lanes.getWidth() * edge || x < 0)
        {
            const double lead = c.followPages ? 0.0 : lanes.getWidth() * 0.1 / timeline.pixelsPerTick;
            timeline.startTick = std::max (0.0, (double) c.playhead() - lead);
            layoutRows();
            repaint();
            return;
        }
    }
    // The chord lane's lit chord: drawn again whole when another sounds, not
    // only in the strips the playhead passes (the UAT: it was in pieces).
    if (c.showChordLane)
    {
        int now = -1;
        if (c.engine.isPlaying())
        {
            const Tick t = c.playhead();
            for (size_t i = 0; i < chords.size(); ++i)
                if (! chords[i].name.empty() && t >= chords[i].start && t < chords[i].end) { now = (int) i; break; }
        }
        if (now != chordSounding)
        {
            chordSounding = now;
            repaint (0, theme::px (36) + sectionLaneHeight(), getWidth(), theme::px (24));
        }
    }
    // Every refresh of the screen, at its place between ticks and blocks (0121).
    const float x = playheadX();
    if (std::abs (x - lastPlayheadX) > 0.05f)
    {
        // While recording, all between, where the take grows.
        if (c.recordingTake().first >= 0)
            repaint ((int) std::floor (jmin (x, lastPlayheadX)) - 2, 0, (int) std::ceil (std::abs (x - lastPlayheadX)) + 6, getHeight());
        const int top = 0;
        repaint ((int) std::floor (lastPlayheadX) - 2, top, 6, getHeight());
        repaint ((int) std::floor (x) - 2, top, 6, getHeight());
        lastPlayheadX = x;
    }
}

//==============================================================================
const std::vector<float>& Arrange::peaksFor (const String& file)
{
    auto it = peaks.find (file);
    if (it != peaks.end()) return it->second;
    auto& out = peaks[file];
    if (auto audio = c.engine.cache.file (File (file)))
    {
        // One peak per 256 samples.
        const auto& b = audio->buffer;
        const int step = 256;
        for (int i = 0; i < b.getNumSamples(); i += step)
            out.push_back (jmax (b.getMagnitude (0, i, jmin (step, b.getNumSamples() - i)),
                                 b.getMagnitude (1, i, jmin (step, b.getNumSamples() - i))));
    }
    return out;
}

void Arrange::drawRuler (Graphics& g, Rectangle<int> r)
{
    g.setColour (theme::ground);
    g.fillRect (r);
    const auto& tempo = c.project.tempo;
    const auto lanes = laneArea();
    g.saveState();
    g.reduceClipRegion (r.withLeft (lanes.getX()));

    // Loop range: a band along the lower half.
    const auto& loop = c.project.loop;
    auto band = Rectangle<float> (xOf (loop.start), (float) r.getBottom() - theme::px (12), xOf (loop.end) - xOf (loop.start), (float) theme::px (10));
    g.setColour (loop.on ? theme::accent.withAlpha (0.9f) : theme::edge.withAlpha (0.6f));
    g.fillRoundedRectangle (band, 3.0f);
    if (c.project.punch.on)
    {
        auto pr = Rectangle<float> (xOf (c.project.punch.start), (float) r.getY() + 2, xOf (c.project.punch.end) - xOf (c.project.punch.start), 4.0f);
        g.setColour (theme::warn);
        g.fillRect (pr);
    }

    const Tick from = tickAt ((float) lanes.getX()), to = tickAt ((float) lanes.getRight());
    for (int bar = std::max (0, tempo.barAt (from)); tempo.barStart (bar) <= to; ++bar)
    {
        const auto& m = tempo.meterAtBar (bar);
        const float x = xOf (tempo.barStart (bar));
        const float barWidth = (float) (m.barTicks() * timeline.pixelsPerTick);
        const int every = barWidth < 24 ? 8 : barWidth < 48 ? 4 : barWidth < 80 ? 2 : 1;
        if (bar % every == 0)
        {
            g.setColour (theme::textSecondary);
            g.fillRect (x, (float) r.getY() + theme::px (4), 1.0f, (float) theme::px (16));
            g.setColour (theme::text);
            g.setFont (theme::font (theme::smallPoints));
            g.drawText (String (bar + 1), barNumberArea (bar).withY (r.getY() + theme::px (2)), Justification::centredLeft, false);
        }
        if (bar == 0 || tempo.meterAtBar (bar - 1).numerator != m.numerator || tempo.meterAtBar (bar - 1).denominator != m.denominator)
            if (bar > 0 || tempo.meters().size() > 1)
            {
                g.setColour (theme::textSecondary);
                g.drawText (String (m.numerator) + "/" + String (m.denominator), (int) x + 4, r.getY() + theme::px (18), 40, theme::px (12), Justification::centredLeft, false);
            }
    }
    // Markers.
    for (auto& mk : c.project.markers)
    {
        const float x = xOf (mk.tick);
        g.setColour (theme::control);
        Path p;
        p.addTriangle (x, (float) r.getY() + theme::px (20), x + 6, (float) r.getY() + theme::px (14), x, (float) r.getY() + theme::px (8));
        g.fillPath (p);
        g.setColour (theme::text);
        g.drawText (String (mk.name), markerNameArea (mk).withY (r.getY() + theme::px (8)), Justification::centredLeft, true);
    }
    g.restoreState();
    g.setColour (theme::rule);
    g.fillRect (r.getX(), r.getBottom() - 1, r.getWidth(), 1);
}

Rectangle<int> Arrange::barNumberArea (int bar) const
{
    const auto& tempo = c.project.tempo;
    const auto& m = tempo.meterAtBar (bar);
    const float barWidth = (float) (m.barTicks() * timeline.pixelsPerTick);
    const int every = barWidth < 24 ? 8 : barWidth < 48 ? 4 : barWidth < 80 ? 2 : 1;
    if (bar < 0 || bar % every != 0) return {};
    int x = (int) xOf (tempo.barStart (bar)) + 4;
    // A marker's flag on the bar line: the number after it.
    for (auto& mk : c.project.markers)
        if (const float fx = xOf (mk.tick); fx > (float) x - 8 && fx < (float) x + 4) x = jmax (x, (int) fx + 9);
    const int w = GlyphArrangement::getStringWidthInt (theme::font (theme::smallPoints), String (bar + 1)) + 2;
    return { x, theme::px (2), w, theme::px (16) };
}

Rectangle<int> Arrange::markerNameArea (const Marker& mk) const
{
    int x = (int) xOf (mk.tick) + 8;
    // The bar number it would cover, if any: the name goes after it, and
    // stops short of the next one.
    const int bar = c.project.tempo.barAt (mk.tick);
    if (const auto n = barNumberArea (bar); ! n.isEmpty() && n.getRight() > x && n.getX() < x + theme::px (8))
        x = n.getRight() + theme::px (4);
    int w = jmin (theme::px (120), GlyphArrangement::getStringWidthInt (theme::font (theme::smallPoints), String (mk.name)) + 4);
    if (const auto next = barNumberArea (bar + 1); ! next.isEmpty() && next.getX() < x + w)
        w = jmax (theme::px (24), next.getX() - x - 2);
    return { x, theme::px (8), w, theme::px (14) };
}

int Arrange::sectionLaneHeight() const { return c.showSectionLane ? theme::px (20) : 0; }

void Arrange::drawSectionLane (Graphics& g, Rectangle<int> r)
{
    g.setColour (theme::sunken);
    g.fillRect (r);
    g.setColour (theme::textSecondary);
    g.setFont (theme::font (theme::smallPoints));
    g.drawText (c.project.sections.empty() ? "Sections (right-click)" : "Sections", r.withWidth (headerWidth()).reduced (8, 0), Justification::centredLeft, false);
    g.saveState();
    g.reduceClipRegion (r.withLeft (headerWidth()));
    for (size_t i = 0; i < c.project.sections.size(); ++i)
    {
        const auto& sec = c.project.sections[i];
        const auto box = Rectangle<float> (xOf (sec.start) + 1, (float) r.getY() + 2, xOf (sec.end) - xOf (sec.start) - 2, (float) r.getHeight() - 4);
        const auto col = sec.colour ? Colour (sec.colour) : theme::trackColours()[(int) (i % (size_t) theme::trackColours().size())];
        g.setColour (theme::underWords (col));
        g.fillRoundedRectangle (box, 3.0f);
        g.setColour (col);
        g.fillRect (box.withWidth (3.0f));
        g.setColour (theme::text);
        g.drawText (String (sec.name), box.toNearestInt().withTrimmedLeft (8), Justification::centredLeft, true);
    }
    // A section being Shift-dragged: where it would land.
    if (drag == Drag::Section)
        for (auto& sec : c.project.sections)
            if (sec.id == dragSection)
            {
                const auto ghost = Rectangle<float> (xOf (sectionTo) + 1, (float) r.getY() + 1, xOf (sectionTo + (sec.end - sec.start)) - xOf (sectionTo) - 2, (float) r.getHeight() - 2);
                g.setColour (theme::playhead);
                g.drawRoundedRectangle (ghost, 3.0f, 2.0f);
            }
    // Where the video starts (decision 0046).
    if (! c.project.video.file.empty())
    {
        const float x = xOf (c.project.video.start);
        g.setColour (theme::playhead);
        g.fillRect (x, (float) r.getY() + 2, 2.0f, (float) r.getHeight() - 4);
        g.setColour (theme::text);
        g.drawText (ui::u8 ("\xe2\x96\xb6 Video"), Rectangle<float> (x + 4, (float) r.getY(), 80, (float) r.getHeight()).toNearestInt(), Justification::centredLeft, false);
    }
    g.restoreState();
    g.setColour (theme::rule);
    g.fillRect (r.getX(), r.getBottom() - 1, r.getWidth(), 1);
}

void Arrange::sectionMenu (Id id, Tick at)
{
    PopupMenu m;
    const auto& tempo = c.project.tempo;
    if (id == 0)
    {
        const auto& loop = c.project.loop;
        m.addItem ("Add a Section from the Loop Range", loop.end > loop.start, false, [this]
        {
            const Tick a = c.project.loop.start, b = c.project.loop.end;
            c.apply ("Add Section", [a, b] (Project& p) { song::addSection (p, "Section " + std::to_string (p.sections.size() + 1), a, b); });
        });
        const int bar = tempo.barAt (at);
        m.addItem ("Add a Four-Bar Section Here", [this, bar]
        {
            c.apply ("Add Section", [bar] (Project& p)
            {
                song::addSection (p, "Section " + std::to_string (p.sections.size() + 1), p.tempo.barStart (bar), p.tempo.barStart (bar + 4));
            });
        });
        m.showMenuAsync (PopupMenu::Options());
        return;
    }
    const Section* sec = nullptr;
    size_t index = 0;
    for (size_t i = 0; i < c.project.sections.size(); ++i) if (c.project.sections[i].id == id) { sec = &c.project.sections[i]; index = i; }
    if (! sec) return;
    m.addItem (ui::u8 ("Rename\xe2\x80\xa6"), [this, id]
    {
        const Section* s = nullptr;
        for (auto& x : c.project.sections) if (x.id == id) s = &x;
        if (! s) return;
        auto* w = new AlertWindow ("Rename the Section", {}, MessageBoxIconType::NoIcon);
        w->addTextEditor ("name", String (s->name));
        w->addButton ("Rename", 1, KeyPress (KeyPress::returnKey));
        w->addButton ("Cancel", 0, KeyPress (KeyPress::escapeKey));
        ui::present (w, ModalCallbackFunction::create ([this, id, w] (int ok)
        {
            if (ok != 1) return;
            const auto name = w->getTextEditorContents ("name").toStdString();
            c.apply ("Rename Section", [id, name] (Project& p) { for (auto& x : p.sections) if (x.id == id) x.name = name; });
        }), true);
    });
    m.addItem ("Loop It", [this, a = sec->start, b = sec->end]
    {
        c.apply ("Loop Section", [a, b] (Project& p) { p.loop.start = a; p.loop.end = b; p.loop.on = true; });
    });
    m.addSeparator();
    m.addItem ("Duplicate", [this, id] { c.apply ("Duplicate Section", [id] (Project& p) { song::duplicateSection (p, id); }); });
    const Id prev = index > 0 ? c.project.sections[index - 1].id : 0;
    const Id next2 = index + 2 < c.project.sections.size() ? c.project.sections[index + 2].id : 0;
    const bool hasNext = index + 1 < c.project.sections.size();
    m.addItem ("Move Earlier", prev != 0, false, [this, id, prev] { c.apply ("Move Section", [id, prev] (Project& p) { song::moveSection (p, id, prev); }); });
    m.addItem ("Move Later", hasNext, false, [this, id, next2] { c.apply ("Move Section", [id, next2] (Project& p) { song::moveSection (p, id, next2); }); });
    m.addSeparator();
    m.addItem ("Delete the Section and Its Bars", [this, id]
    {
        c.apply ("Delete Section", [id] (Project& p) { song::removeSection (p, id, true); });
        c.say ("The section's bars are gone and the song closed up after it.", false, true);
    });
    m.addItem ("Delete the Section Only", [this, id] { c.apply ("Remove Section", [id] (Project& p) { song::removeSection (p, id, false); }); });
    m.showMenuAsync (PopupMenu::Options());
}

void Arrange::drawChordLane (Graphics& g, Rectangle<int> r)
{
    g.setColour (theme::sunken);
    g.fillRect (r);
    g.setColour (theme::textSecondary);
    g.setFont (theme::font (theme::smallPoints));
    g.drawText ("Chords", r.withWidth (headerWidth()).reduced (8, 0), Justification::centredLeft, false);
    g.saveState();
    g.reduceClipRegion (r.withLeft (laneArea().getX()));
    for (size_t i = 0; i < chords.size(); ++i)
    {
        const auto& ch = chords[i];
        if (ch.name.empty()) continue;
        auto box = Rectangle<float> (xOf (ch.start), (float) r.getY() + 3, jmax (2.0f, xOf (ch.end) - xOf (ch.start) - 2.0f), (float) r.getHeight() - 6);
        const bool sounding = (int) i == chordSounding;
        g.setColour (sounding ? theme::accent : theme::raised);
        g.fillRoundedRectangle (box, 3.0f);
        g.setColour (sounding ? theme::ink : theme::text);
        g.drawFittedText (String (ch.name), box.toNearestInt().reduced (4, 0), Justification::centredLeft, 1, 0.7f);
    }
    g.restoreState();
}

void Arrange::drawTempoLane (Graphics& g, Rectangle<int> r)
{
    g.setColour (theme::sunken);
    g.fillRect (r);
    g.setColour (theme::textSecondary);
    g.setFont (theme::font (theme::smallPoints));
    g.drawText ("Tempo", r.withWidth (headerWidth()).reduced (8, 0), Justification::centredLeft, false);
    const auto& tempo = c.project.tempo;
    double lo = 1e9, hi = 0;
    for (auto& tp : tempo.tempos()) { lo = std::min (lo, tp.bpm); hi = std::max (hi, tp.bpm); }
    lo = std::floor (lo - 10); hi = std::ceil (hi + 10);
    auto yOf = [&] (double bpm) { return (float) r.getBottom() - 4 - (float) ((bpm - lo) / (hi - lo)) * (float) (r.getHeight() - 8); };
    g.saveState();
    g.reduceClipRegion (r.withLeft (laneArea().getX()));
    Path p;
    const auto lanes = laneArea();
    p.startNewSubPath ((float) lanes.getX(), yOf (tempo.bpmAt (tickAt ((float) lanes.getX()))));
    for (float x = (float) lanes.getX(); x < (float) lanes.getRight(); x += 3.0f) p.lineTo (x, yOf (tempo.bpmAt (tickAt (x))));
    g.setColour (theme::control);
    g.strokePath (p, PathStrokeType (1.5f));
    for (auto& tp : tempo.tempos())
    {
        const float x = xOf (tp.tick);
        g.fillEllipse (x - 3.5f, yOf (tp.bpm) - 3.5f, 7.0f, 7.0f);
        g.setColour (theme::text);
        // Kept inside the lane: at small text sizes the top value was cut off.
        g.setFont (theme::font (theme::smallPoints));
        const int labelH = theme::px (12);
        g.drawText (String (tp.bpm, 1), (int) x + 6, jmax (r.getY(), (int) yOf (tp.bpm) - labelH - 2), 60, labelH, Justification::centredLeft, false);
        g.setColour (theme::control);
    }
    g.restoreState();
}

void Arrange::drawClip (Graphics& g, const Track& t, const Clip& clip, Rectangle<float> r, bool chosen)
{
    const auto col = clip.colour ? Colour (clip.colour) : trackColour (t);
    g.setColour (theme::underWords (col, clip.muted ? 0.35f : 0.55f).withAlpha (0.9f));
    g.fillRoundedRectangle (r, 4.0f);
    g.setColour (chosen ? theme::accent : col.withMultipliedBrightness (0.9f));
    g.drawRoundedRectangle (r.reduced (0.5f), 4.0f, chosen ? 2.0f : 1.0f);
    auto title = r.removeFromTop (jmin (16.0f, r.getHeight() * 0.4f));
    g.setColour (theme::text);
    g.setFont (theme::font (theme::smallPoints));
    String label (clip.name);
    if (! clip.isMidi() && clip.audio.warp != WarpMode::Off)
        label << "  " << (clip.audio.warp == WarpMode::Spectral ? "spectral" : clip.audio.warp == WarpMode::Granular ? "granular" : "repitch");
    if (! clip.offlineFx.chain.empty()) label << "  fx";
    // An alias clip (decision 0074): two links of a chain at the top right; edits reach every copy.
    if (clip.link != 0 && title.getWidth() > 30.0f)
    {
        auto badge = title.removeFromRight (jmin (22.0f, title.getHeight() + 6.0f)).reduced (3.0f, jmax (2.0f, title.getHeight() * 0.22f));
        const float w = badge.getWidth() * 0.62f, h = badge.getHeight();
        g.setColour (theme::text);
        g.drawRoundedRectangle ({ badge.getX(), badge.getY(), w, h }, h * 0.5f, 1.3f);
        g.drawRoundedRectangle ({ badge.getRight() - w, badge.getY(), w, h }, h * 0.5f, 1.3f);
    }
    g.drawText (label, title.toNearestInt().reduced (4, 0), Justification::centredLeft, true);

    g.saveState();
    g.reduceClipRegion (r.toNearestInt());
    if (clip.isMidi())
    {
        int lo = 127, hi = 0;
        for (auto& n : clip.midi.notes) { lo = std::min (lo, n.pitch); hi = std::max (hi, n.pitch); }
        if (lo > hi) { g.restoreState(); return; }
        lo -= 2; hi += 2;
        const float rowH = jmax (1.0f, r.getHeight() / (float) (hi - lo + 1));
        g.setColour (theme::text.withAlpha (clip.muted ? 0.4f : 0.9f));
        const Tick content = clip.loopLength > 0 ? clip.loopLength : std::numeric_limits<Tick>::max() / 4;
        for (Tick base = clip.start - clip.offset; base < clip.end(); base += content)
        {
            for (auto& n : clip.midi.notes)
            {
                const Tick a = base + n.start;
                if (a + n.length < clip.start || a >= clip.end()) continue;
                const float x0 = xOf (a), x1 = xOf (std::min (a + n.length, clip.end()));
                g.fillRect (x0, r.getBottom() - (float) (n.pitch - lo + 1) * rowH, jmax (1.0f, x1 - x0 - 1.0f), jmax (1.0f, rowH - 0.5f));
            }
            if (clip.loopLength <= 0) break;
            g.setColour (theme::text.withAlpha (0.25f));
            g.fillRect (xOf (base + content), r.getY(), 1.0f, r.getHeight());
            g.setColour (theme::text.withAlpha (clip.muted ? 0.4f : 0.9f));
        }
    }
    else
    {
        const auto& pk = peaksFor (String (clip.audio.renderedFile.empty() ? clip.audio.file : clip.audio.renderedFile));
        if (! pk.empty())
        {
            const double rate = c.engine.sampleRate();
            const float mid = r.getCentreY(), half = r.getHeight() * 0.5f;
            const double gain = std::pow (10.0, clip.audio.gainDb / 20.0);
            g.setColour (theme::text.withAlpha (clip.muted ? 0.35f : 0.85f));
            const auto& tempo = c.project.tempo;
            const double clipStartSec = tempo.secondsAt (clip.start);
            for (float x = jmax (r.getX(), (float) laneArea().getX()); x < jmin (r.getRight(), (float) laneArea().getRight()); x += 1.0f)
            {
                double src;
                if (clip.audio.warp != WarpMode::Off && clip.audio.sourceBpm > 0)
                    src = clip.audio.sourceStart + (double) (tickAt (x) - (clip.start - clip.offset)) / PPQ * 60.0 / clip.audio.sourceBpm;
                else
                    src = clip.audio.sourceStart + tempo.secondsAt (tickAt (x)) - clipStartSec + (tempo.secondsAt (clip.start) - tempo.secondsAt (clip.start - clip.offset));
                const int i = (int) (src * rate / 256.0);
                if (i < 0 || i >= (int) pk.size()) continue;
                const float h = jmin (1.0f, (float) (pk[(size_t) i] * gain)) * half;
                g.fillRect (x, mid - h, 1.0f, jmax (1.0f, h * 2.0f));
            }
        }
        // Warp markers: drag one to move that moment of the audio.
        for (auto& m : clip.audio.markers)
        {
            const float x = xOf (clip.start - clip.offset + m.tick);
            if (x < r.getX() - 1 || x > r.getRight() + 1) continue;
            g.setColour (theme::control);
            g.fillRect (x - 0.5f, r.getY() - 16.0f, 1.5f, r.getHeight() + 16.0f);
            Path tri;
            tri.addTriangle (x - 4.0f, r.getY() - 16.0f, x + 4.0f, r.getY() - 16.0f, x, r.getY() - 10.0f);
            g.fillPath (tri);
        }
        // Fades: shaded over, with a handle at the top to drag (decision 0030).
        const auto& tempo = c.project.tempo;
        const float full = r.getRight() - r.getX();
        const float inW = jlimit (0.0f, full, xOf (tempo.tickAt (tempo.secondsAt (clip.start) + clip.audio.fadeIn)) - xOf (clip.start));
        const float outW = jlimit (0.0f, full, xOf (clip.end()) - xOf (tempo.tickAt (tempo.secondsAt (clip.end()) - clip.audio.fadeOut)));
        g.setColour (theme::ink.withAlpha (0.45f));
        if (inW > 0.5f)
        {
            Path p;
            p.startNewSubPath (r.getX(), r.getBottom());
            for (int i = 0; i <= 16; ++i)
                p.lineTo (r.getX() + inW * (float) i / 16.0f, r.getBottom() - r.getHeight() * std::sin (MathConstants<float>::halfPi * (float) i / 16.0f));
            p.lineTo (r.getX(), r.getY());
            p.closeSubPath();
            g.fillPath (p);
        }
        if (outW > 0.5f)
        {
            Path p;
            p.startNewSubPath (r.getRight(), r.getBottom());
            for (int i = 0; i <= 16; ++i)
                p.lineTo (r.getRight() - outW * (float) i / 16.0f, r.getBottom() - r.getHeight() * std::sin (MathConstants<float>::halfPi * (float) i / 16.0f));
            p.lineTo (r.getRight(), r.getY());
            p.closeSubPath();
            g.fillPath (p);
        }
        g.setColour (theme::control);
        g.fillRect (Rectangle<float> (7.0f, 7.0f).withCentre ({ r.getX() + inW, r.getY() + 4.0f }));
        g.fillRect (Rectangle<float> (7.0f, 7.0f).withCentre ({ r.getRight() - outW, r.getY() + 4.0f }));
    }
    g.restoreState();
}

void Arrange::drawAutomation (Graphics& g, const Track& t, const AutomationLane& lane, Rectangle<int> r)
{
    g.setColour (theme::sunken);
    g.fillRect (r);
    auto header = Rectangle<int> (0, r.getY(), headerWidth(), r.getHeight());
    g.setColour (theme::ground);
    g.fillRect (header);
    g.setColour (theme::textSecondary);
    g.setFont (theme::font (theme::smallPoints));
    String name = lane.target.kind == AutoTarget::Kind::Volume ? "Volume"
                : lane.target.kind == AutoTarget::Kind::Pan ? "Pan"
                : String (lane.target.paramName);
    if (const auto* d = const_cast<Project&> (c.project).device (lane.target.device)) name = String (d->plugin.name) + ": " + name;
    g.drawFittedText (name, header.reduced (theme::px (14) + c.project.depth (t.id) * theme::px (10), 2), Justification::centredLeft, 2);
    g.setColour (theme::rule);
    g.fillRect (header.getX(), header.getBottom() - 1, getWidth(), 1);

    g.saveState();
    g.reduceClipRegion (r);
    auto yOf = [&] (double v) { return (float) r.getBottom() - 3 - (float) v * (float) (r.getHeight() - 6); };
    Path p;
    bool first = true;
    for (float x = (float) r.getX(); x <= (float) r.getRight(); x += 2.0f)
    {
        const float y = yOf (edit::valueAt (lane, tickAt (x), 0.5));
        if (first) { p.startNewSubPath (x, y); first = false; } else p.lineTo (x, y);
    }
    g.setColour (trackColour (t));
    g.strokePath (p, PathStrokeType (1.5f));
    for (auto& pt : lane.points)
    {
        g.setColour (theme::control);
        g.fillEllipse (xOf (pt.tick) - 3.5f, yOf (pt.value) - 3.5f, 7.0f, 7.0f);
    }
    g.restoreState();
}

void Arrange::drawTake (Graphics& g, const Track& t, int index, Rectangle<int> r)
{
    const auto& v = t.version();
    const auto& take = v.takes[(size_t) index];
    g.setColour (theme::sunken);
    g.fillRect (r);
    auto header = Rectangle<int> (0, r.getY(), headerWidth(), r.getHeight());
    g.setColour (theme::ground);
    g.fillRect (header);
    g.setColour (theme::textSecondary);
    g.setFont (theme::font (theme::smallPoints));
    g.drawText (String (take.name), header.reduced (theme::px (14) + c.project.depth (t.id) * theme::px (10), 2), Justification::centredLeft, true);
    g.setColour (theme::rule);
    g.fillRect (0, r.getBottom() - 1, getWidth(), 1);

    g.saveState();
    g.reduceClipRegion (r);
    for (auto& clip : take.clips)
    {
        Rectangle<float> box (xOf (clip.start), (float) r.getY() + 3, jmax (3.0f, xOf (clip.end()) - xOf (clip.start)), (float) r.getHeight() - 6);
        g.setColour (theme::underWords (Colour (t.colour), 0.3f));
        g.fillRoundedRectangle (box, 3.0f);
    }
    // Where the comp uses this take: brighter, with the accent along its top (it's "on" there).
    for (auto& seg : v.comp)
    {
        if (seg.take != take.id) continue;
        for (auto& clip : take.clips)
        {
            const Tick a = std::max (seg.start, clip.start), b = std::min (seg.end, clip.end());
            if (b <= a) continue;
            Rectangle<float> box (xOf (a), (float) r.getY() + 3, jmax (2.0f, xOf (b) - xOf (a)), (float) r.getHeight() - 6);
            g.setColour (theme::underWords (Colour (t.colour), 0.6f));
            g.fillRect (box);
            g.setColour (theme::accent);
            g.fillRect (box.withHeight (3.0f));
        }
    }
    // A swipe in progress.
    if (drag == Drag::Swipe && swipeTake == index && dragTrack == t.id)
    {
        const float a = xOf (std::min (swipeFrom, swipeTo)), b = xOf (std::max (swipeFrom, swipeTo));
        g.setColour (theme::accent.withAlpha (0.25f));
        g.fillRect (Rectangle<float> (a, (float) r.getY(), jmax (2.0f, b - a), (float) r.getHeight()));
    }
    g.restoreState();
}

void Arrange::paint (Graphics& g)
{
    g.fillAll (theme::roll);
    const auto lanes = laneArea();
    int top = 0;
    drawRuler (g, { 0, top, getWidth(), theme::px (36) });
    top += theme::px (36);
    if (sectionLaneHeight() > 0) { drawSectionLane (g, { 0, top, getWidth(), sectionLaneHeight() }); top += sectionLaneHeight(); }
    if (c.showChordLane) { drawChordLane (g, { 0, top, getWidth(), theme::px (24) }); top += theme::px (24); }
    if (c.showTempoLane) { drawTempoLane (g, { 0, top, getWidth(), theme::px (48) }); top += theme::px (48); }

    g.saveState();
    g.reduceClipRegion (Rectangle<int> (0, lanes.getY(), getWidth(), lanes.getHeight()));

    // Grid.
    const auto& tempo = c.project.tempo;
    const Tick from = tickAt ((float) lanes.getX()), to = tickAt ((float) lanes.getRight());
    for (int bar = std::max (0, tempo.barAt (from)); tempo.barStart (bar) <= to; ++bar)
    {
        const auto& m = tempo.meterAtBar (bar);
        const Tick start = tempo.barStart (bar);
        // The arrangement's grid (the top bar's Grid), as fine as the zoom lets it be seen.
        const Tick step = std::max<Tick> (c.snap ? c.arrangeGrid : PPQ, timeline.lineStep (m, 14.0f));
        for (Tick t = start; t < start + m.barTicks(); t += step)
        {
            g.setColour (t == start ? theme::rule : theme::lane);
            g.fillRect (xOf (t), (float) lanes.getY(), 1.0f, (float) lanes.getHeight());
        }
    }
    if (c.project.loop.on)
    {
        g.setColour (theme::accent.withAlpha (0.04f));
        g.fillRect (Rectangle<float> (xOf (c.project.loop.start), (float) lanes.getY(), xOf (c.project.loop.end) - xOf (c.project.loop.start), (float) lanes.getHeight()));
    }

    for (auto& row : rows)
    {
        const auto* t = c.project.track (row.track);
        if (! t) continue;
        Rectangle<int> rr (lanes.getX(), lanes.getY() + row.y - scrollY, lanes.getWidth(), row.h);
        if (rr.getBottom() < lanes.getY() || rr.getY() > lanes.getBottom()) continue;
        if (row.automation >= 0)
        {
            drawAutomation (g, *t, t->automation[(size_t) row.automation], rr);
            continue;
        }
        if (row.take >= 0)
        {
            drawTake (g, *t, row.take, rr);
            continue;
        }
        const bool chosen = std::find (c.selectedTracks.begin(), c.selectedTracks.end(), t->id) != c.selectedTracks.end();
        if (chosen) { g.setColour (theme::text.withAlpha (0.03f)); g.fillRect (rr); }
        if (t->isFolder())
        {
            // A folder shows where its tracks have clips.
            g.setColour (trackColour (*t).withAlpha (0.25f));
            for (auto& x : c.project.tracks)
                if (c.project.isInside (x.id, t->id))
                    for (auto& clip : x.clips())
                        g.fillRect (Rectangle<float> (xOf (clip.start), (float) rr.getY() + 6, xOf (clip.end()) - xOf (clip.start), (float) rr.getHeight() - 12));
        }
        for (auto& clip : t->clips())
        {
            if (clip.id == dragClip && (drag == Drag::MoveClip || drag == Drag::TrimStart || drag == Drag::TrimEnd) && ! copying) continue;
            if (drag == Drag::MoveClip && ! copying && dragClip != 0 && c.selectedClips.count (dragClip) && c.selectedClips.count (clip.id)) continue;
            const float x0 = xOf (clip.start), x1 = xOf (clip.end());
            if (x1 < (float) lanes.getX() || x0 > (float) lanes.getRight()) continue;
            drawClip (g, *t, clip, { x0, (float) rr.getY() + 2, jmax (3.0f, x1 - x0), (float) rr.getHeight() - 4 },
                      c.selectedClips.count (clip.id) > 0);
        }
        // The take being recorded, growing to the playhead in the arm's colour (the UAT).
        if (t->armed && t->holdsClips())
            if (const auto [from, to] = c.recordingTake(); from >= 0)
            {
                const auto colour = t->armFor == ArmFor::Audio || t->armFor == ArmFor::Both ? theme::armAudio : theme::armMidi;
                const Rectangle<float> take (xOf (from), (float) rr.getY() + 2, jmax (3.0f, xOf (to) - xOf (from)), (float) rr.getHeight() - 4);
                g.setColour (theme::underWords (colour, 0.35f));
                g.fillRoundedRectangle (take, 4.0f);
                g.setColour (colour);
                g.drawRoundedRectangle (take.reduced (0.5f), 4.0f, 1.0f);
                g.setColour (theme::text);
                g.setFont (theme::font (theme::smallPoints));
                g.drawText ("Recording", take.reduced (6, 2).toNearestInt(), Justification::topLeft, false);
            }
        g.setColour (theme::lane);
        g.fillRect (lanes.getX(), rr.getBottom() - 1, lanes.getWidth(), 1);
    }

    // The others chosen with it, where they'd land.
    for (auto& m : movingTogether())
    {
        if (m.clip == dragClip) continue;
        const auto* clip = c.project.clip (m.clip);
        if (! clip) continue;
        for (auto& row : rows)
            if (row.track == m.track && row.isClips())
            {
                Clip ghost = *clip;
                ghost.start = m.start;
                drawClip (g, *c.project.track (m.track), ghost,
                          { xOf (m.start), (float) (lanes.getY() + row.y - scrollY + 2), xOf (m.start + clip->length) - xOf (m.start), (float) row.h - 4 }, true);
            }
    }
    // A clip being dragged, where it would land.
    if (dragClip != 0 && (drag == Drag::MoveClip || drag == Drag::TrimStart || drag == Drag::TrimEnd))
    {
        const Track* owner = nullptr;
        if (const auto* clip = c.project.clip (dragClip, &owner))
            for (auto& row : rows)
                if (row.track == previewTrack && row.isClips())
                {
                    Clip ghost = *clip;
                    ghost.start = previewStart;
                    ghost.length = previewEnd - previewStart;
                    if (drag == Drag::TrimStart) ghost.offset += previewStart - clip->start;
                    drawClip (g, *c.project.track (previewTrack), ghost,
                              { xOf (previewStart), (float) (lanes.getY() + row.y - scrollY + 2), xOf (previewEnd) - xOf (previewStart), (float) row.h - 4 }, true);
                }
    }
    g.restoreState();

    // The header column's ground under the headers' gaps.
    g.setColour (theme::ground);
    g.fillRect (0, lanes.getY() + contentHeight - scrollY, headerWidth(), jmax (0, lanes.getBottom() - (lanes.getY() + contentHeight - scrollY)));
    g.setColour (theme::rule);
    g.fillRect (headerWidth() - 1, rulerHeight(), 1, getHeight());
}

void Arrange::paintOverChildren (Graphics& g)
{
    const auto lanes = laneArea();
    // The cursor (where Play starts) and the playhead.
    const float cx = xOf (c.cursor);
    if (cx >= (float) lanes.getX())
    {
        g.setColour (theme::edge);
        g.fillRect (cx, 0.0f, 1.0f, (float) getHeight() - theme::px (12));
    }
    paintTool (g);
    if (drag == Drag::Section)
    {
        // The bar it would start on, down every track.
        const float x = xOf (sectionTo);
        g.setColour (theme::playhead);
        g.fillRect (x - 1.0f, 0.0f, 2.0f, (float) getHeight() - theme::px (12));
    }
    if (c.engine.isPlaying())
    {
        const float x = lastPlayheadX = playheadX();
        if (x >= (float) lanes.getX())
        {
            g.setColour (theme::playhead);
            g.fillRect (x - 0.5f, 0.0f, 2.0f, (float) getHeight() - theme::px (12));
        }
    }
}

//==============================================================================
const Arrange::Row* Arrange::rowAt (int y) const
{
    const int local = y - laneArea().getY() + scrollY;
    for (auto& r : rows) if (local >= r.y && local < r.y + r.h) return &r;
    return nullptr;
}

const Clip* Arrange::clipAt (Point<int> p, const Track** owner) const
{
    const auto* row = rowAt (p.y);
    if (! row || ! row->isClips()) return nullptr;
    const auto* t = c.project.track (row->track);
    if (! t) return nullptr;
    const Tick at = tickAt ((float) p.x);
    for (auto it = t->clips().rbegin(); it != t->clips().rend(); ++it)
        if (at >= it->start && at < it->end())
        {
            if (owner) *owner = t;
            return &*it;
        }
    return nullptr;
}

String Arrange::getTooltip()
{
    // The Sections lane says how to move a section.
    const auto pos = getMouseXYRelative();
    const int lane = theme::px (36);
    if (sectionLaneHeight() > 0 && pos.y >= lane && pos.y < lane + sectionLaneHeight() && pos.x >= headerWidth())
        return theme::wrapTip ("Click a section to go to it. Shift-drag it to move it, with all its music, to another bar: what's there moves out of its way. Right-click for more.");
    return {};
}

void Arrange::mouseMove (const MouseEvent& e)
{
    const auto lanes = laneArea();
    // What the pointer is over, for the message line (0070).
    const auto* over = lanes.contains (e.getPosition()) ? clipAt (e.getPosition()) : nullptr;
    c.hover (over ? over->id : 0);
    if (! lanes.contains (e.getPosition())) { setMouseCursor (MouseCursor::NormalCursor); return; }
    // A tool's own pointer over the lanes (decision 0092).
    if (c.arrangeTool.tool != tools::Tool::Select && ! (c.arrangeTool.tool == tools::Tool::Draw && over))
    {
        setMouseCursor (toolicons::cursor (c.arrangeTool.tool));
        toolMove (e);
        return;
    }
    if (const auto* clip = over)
    {
        const float edge = (float) theme::px (6);
        const float x0 = xOf (clip->start), x1 = xOf (clip->end());
        if ((float) e.x < x0 + edge || (float) e.x > x1 - edge || e.mods.isShiftDown()) setMouseCursor (MouseCursor::LeftRightResizeCursor);
        else setMouseCursor (MouseCursor::DraggingHandCursor);
    }
    else setMouseCursor (MouseCursor::NormalCursor);
}

void Arrange::mouseExit (const MouseEvent&) { c.hover (0); }

void Arrange::mouseDown (const MouseEvent& e)
{
    drag = Drag::None;
    dragClip = 0;
    copying = false;
    toolDrag.reset();
    // Control-right-click: the toolbox (decision 0092). Control-click alone stays the Mac's right-click.
    if (e.mods.isCtrlDown() && e.mods.isRightButtonDown()) { showToolBox (e.getScreenPosition()); return; }
    // A tool takes the left click (ArrangeTools.cpp); Select mostly leaves it to what follows.
    if (! e.mods.isPopupMenu() && toolDown (e)) return;
    if (! c.range.empty() && ! e.mods.isPopupMenu()) { c.range = {}; c.viewChanged(); }
    const auto lanes = laneArea();
    const int rulerTop = 0, rulerBottom = theme::px (36);

    if (e.y >= rulerTop && e.y < rulerBottom && e.x >= lanes.getX())
    {
        // Upper half of the ruler: locate. Lower half: draw the loop range.
        if (e.y > rulerBottom - theme::px (14) || e.mods.isShiftDown())
        {
            drag = Drag::Loop;
            dragOrigin = c.arrangeSnap (tickAt ((float) e.x));
        }
        else
        {
            drag = Drag::Cursor;
            c.locate (c.arrangeSnap (tickAt ((float) e.x)));
        }
        return;
    }
    if (e.y >= rulerBottom && e.y < rulerBottom + sectionLaneHeight() && e.x >= lanes.getX())
    {
        const Tick at = tickAt ((float) e.x);
        const auto* sec = song::sectionAt (c.project, at);
        if (e.mods.isPopupMenu()) sectionMenu (sec ? sec->id : 0, at);
        else if (sec && e.mods.isShiftDown())
        {
            // Shift-drag: the section, and everything in it, to another bar.
            drag = Drag::Section;
            dragSection = sec->id;
            dragOrigin = at;
            sectionTo = sec->start;
        }
        else if (sec) c.locate (sec->start);
        return;
    }
    if (c.showTempoLane)
    {
        const int tt = theme::px (36) + sectionLaneHeight() + (c.showChordLane ? theme::px (24) : 0);
        if (e.y >= tt && e.y < tt + theme::px (48) && e.x >= lanes.getX())
        {
            drag = Drag::Tempo;
            dragOrigin = c.arrangeSnap (tickAt ((float) e.x));
            return;
        }
    }
    // Right-click under the tracks, in the track panel or the lanes: add one.
    if (e.mods.isPopupMenu() && e.y >= lanes.getY() && e.y < lanes.getBottom() && ! rowAt (e.y)) { trackPanelMenu(); return; }
    if (! lanes.contains (e.getPosition())) return;

    const auto* row = rowAt (e.y);
    if (! row) { c.locate (c.arrangeSnap (tickAt ((float) e.x))); return; }

    if (row->take >= 0)
    {
        if (e.mods.isPopupMenu()) { laneMenu (*row, e.getPosition()); return; }
        // Swiping across a take's lane uses that take there.
        drag = Drag::Swipe;
        dragTrack = row->track;
        swipeTake = row->take;
        swipeFrom = swipeTo = tickAt ((float) e.x);
        return;
    }

    if (row->automation >= 0)
    {
        auto* t = c.project.track (row->track);
        auto& lane = t->automation[(size_t) row->automation];
        Rectangle<int> rr (lanes.getX(), lanes.getY() + row->y - scrollY, lanes.getWidth(), row->h);
        dragLane = lane.id;
        dragTrack = t->id;
        dragPoint = -1;
        for (size_t i = 0; i < lane.points.size(); ++i)
        {
            const float px = xOf (lane.points[i].tick);
            const float py = (float) rr.getBottom() - 3 - (float) lane.points[i].value * (float) (rr.getHeight() - 6);
            if (std::abs (px - (float) e.x) < 6 && std::abs (py - (float) e.y) < 6) dragPoint = (int) i;
        }
        if (e.mods.isPopupMenu())
        {
            if (dragPoint > 0)
            {
                const int idx = dragPoint, laneIndex = row->automation;
                const Id tid = t->id;
                c.apply ("Delete Point", [=] (Project& p)
                {
                    auto& l = p.track (tid)->automation[(size_t) laneIndex];
                    l.points.erase (l.points.begin() + idx);
                });
            }
            else laneMenu (*row, e.getPosition());
            return;
        }
        if (dragPoint < 0)
        {
            const double v = jlimit (0.0, 1.0, (double) ((float) rr.getBottom() - 3 - (float) e.y) / (rr.getHeight() - 6));
            const Tick at = c.arrangeSnap (tickAt ((float) e.x));
            const int laneIndex = row->automation;
            const Id tid = t->id;
            c.apply ("Add Point", [=] (Project& p) { edit::addPoint (p.track (tid)->automation[(size_t) laneIndex], { at, v }); });
            auto& l = c.project.track (tid)->automation[(size_t) laneIndex];
            for (size_t i = 0; i < l.points.size(); ++i) if (l.points[i].tick == at) dragPoint = (int) i;
        }
        drag = Drag::Point;
        dragOrigin = row->automation;
        return;
    }

    const Track* owner = nullptr;
    const auto* clip = clipAt (e.getPosition(), &owner);
    if (clip)
    {
        if (e.mods.isPopupMenu())
        {
            if (! c.selectedClips.count (clip->id)) c.selectClip (clip->id, false);
            clipMenu (clip->id);
            return;
        }
        // Shift-click a range, Option-click adds (Option-drag still copies),
        // Command-click adds or takes away (decision 0053).
        // Select's Click Adds or Takes Away: a plain click is a Command-click (decision 0092).
        const bool adds = c.arrangeTool.tool == tools::Tool::Select && c.arrangeTool.mode() == tools::selectmode::ClickAddsOrRemoves
                          && ! (e.mods.isShiftDown() || e.mods.isAltDown() || e.mods.isCommandDown());
        c.chooseClip (clip->id, adds ? e.mods.withFlags (ModifierKeys::commandModifier) : e.mods);
        c.focusOn (owner->id);
        if (owner->locked || clip->locked) return;
        if (! clip->isMidi())
        {
            // The fade handles sit at the top corners of the waveform, under the name.
            const auto& tempo = c.project.tempo;
            const float title = jmin (16.0f, (float) laneHeightOf (*owner) * 0.4f);
            const float top = (float) (laneArea().getY() + rowAt (e.y)->y - scrollY) + 2.0f + title + 4.0f;
            const float inX = xOf (tempo.tickAt (tempo.secondsAt (clip->start) + clip->audio.fadeIn));
            const float outX = xOf (tempo.tickAt (tempo.secondsAt (clip->end()) - clip->audio.fadeOut));
            const float reach = (float) theme::px (6);
            if (std::abs ((float) e.y - top) <= reach && (std::abs ((float) e.x - inX) <= reach || std::abs ((float) e.x - outX) <= reach))
            {
                drag = std::abs ((float) e.x - inX) <= std::abs ((float) e.x - outX) ? Drag::FadeIn : Drag::FadeOut;
                dragClip = clip->id;
                dragBefore = c.project;
                return;
            }
        }
        if (! clip->isMidi())
            for (size_t i = 0; i < clip->audio.markers.size(); ++i)
            {
                const float mx = xOf (clip->start - clip->offset + clip->audio.markers[i].tick);
                if (std::abs (mx - (float) e.x) <= (float) theme::px (4))
                {
                    drag = Drag::WarpMarker;
                    dragClip = clip->id;
                    dragPoint = (int) i;
                    dragBefore = c.project;
                    return;
                }
            }
        dragClip = clip->id;
        dragTrack = owner->id;
        dragOrigin = tickAt ((float) e.x);
        dragOffset = dragOrigin - clip->start;
        previewStart = clip->start;
        previewEnd = clip->end();
        previewTrack = owner->id;
        const float edge = (float) theme::px (6);
        trimShift = 0;
        shiftTrim = false;
        if ((float) e.x < xOf (clip->start) + edge) drag = Drag::TrimStart;
        else if ((float) e.x > xOf (clip->end()) - edge) drag = Drag::TrimEnd;
        else if (e.mods.isShiftDown() && ! e.mods.isAltDown() && ! e.mods.isCommandDown())
        {
            // Shift-drag anywhere on a clip resizes it from its nearer edge, which
            // follows the pointer from where it was (0095). A Shift-click still chooses.
            drag = (float) e.x < (xOf (clip->start) + xOf (clip->end())) / 2.0f ? Drag::TrimStart : Drag::TrimEnd;
            trimShift = (drag == Drag::TrimStart ? clip->start : clip->end()) - dragOrigin;
            shiftTrim = true;
        }
        else { drag = Drag::MoveClip; copying = e.mods.isAltDown() && ! e.mods.isShiftDown(); }
        return;
    }

    // Empty lane: choose the track (Shift, Option and Command as on its header), put the cursor here.
    c.chooseTrack (row->track, e.mods, c.arrangeOrder());
    if (! (e.mods.isShiftDown() || e.mods.isAltDown() || e.mods.isCommandDown())) c.selectedClips.clear();
    if (e.mods.isPopupMenu()) { laneMenu (*row, e.getPosition()); return; }
    drag = Drag::Cursor;
    c.locate (c.arrangeSnap (tickAt ((float) e.x)));
}

void Arrange::mouseDrag (const MouseEvent& e)
{
    if (toolDrag) { toolDragTo (e); return; }
    const Tick at = tickAt ((float) e.x);
    switch (drag)
    {
        case Drag::Cursor: c.locate (c.arrangeSnap (std::max<Tick> (0, at))); break;
        case Drag::Loop:
        {
            const Tick here = c.arrangeSnap (std::max<Tick> (0, at));
            if (here == dragOrigin) break;
            c.project.loop.start = std::min (dragOrigin, here);
            c.project.loop.end = std::max (dragOrigin, here);
            c.project.loop.on = true;
            c.refresh();
            break;
        }
        case Drag::MoveClip:
        {
            const auto* clip = c.project.clip (dragClip);
            if (! clip) break;
            previewStart = e.mods.isCommandDown() ? std::max<Tick> (0, at - dragOffset) : std::max<Tick> (0, c.arrangeSnap (at - dragOffset));
            previewEnd = previewStart + clip->length;
            copying = e.mods.isAltDown() && ! e.mods.isShiftDown();
            if (const auto* row = rowAt (e.y))
                if (row->isClips() && c.project.track (row->track)->holdsClips()) previewTrack = row->track;
            repaint();
            break;
        }
        case Drag::TrimStart:
        case Drag::TrimEnd:
        {
            const auto* clip = c.project.clip (dragClip);
            if (! clip) break;
            Tick snapped = e.mods.isCommandDown() ? at + trimShift : c.arrangeSnap (at + trimShift);
            snapped = c.zeroEdge (*clip, snapped).tick;   // onto a zero crossing, when that's on (decision 0054)
            if (drag == Drag::TrimStart) previewStart = jlimit<Tick> (0, clip->end() - 1, snapped);
            else previewEnd = std::max (clip->start + 1, snapped);
            repaint();
            break;
        }
        case Drag::Point:
        {
            const auto* row = rowAt (e.y);
            auto* t = c.project.track (dragTrack);
            if (! t || dragPoint < 0) break;
            auto& lane = t->automation[(size_t) dragOrigin];
            if (dragPoint >= (int) lane.points.size()) break;
            const auto lanes = laneArea();
            int rowY = 0, rowH = theme::px (48);
            for (auto& r : rows) if (r.track == dragTrack && r.automation == (int) dragOrigin) { rowY = r.y; rowH = r.h; }
            Rectangle<int> rr (lanes.getX(), lanes.getY() + rowY - scrollY, lanes.getWidth(), rowH);
            ignoreUnused (row);
            auto& pt = lane.points[(size_t) dragPoint];
            pt.value = jlimit (0.0, 1.0, (double) ((float) rr.getBottom() - 3 - (float) e.y) / (rr.getHeight() - 6));
            if (dragPoint > 0)
            {
                const Tick lo = lane.points[(size_t) dragPoint - 1].tick + 1;
                const Tick hi = dragPoint + 1 < (int) lane.points.size() ? lane.points[(size_t) dragPoint + 1].tick - 1 : std::numeric_limits<Tick>::max();
                pt.tick = jlimit (lo, hi, c.arrangeSnap (at));
            }
            c.refresh();
            break;
        }
        case Drag::Tempo:
        {
            const int tt = theme::px (36) + sectionLaneHeight() + (c.showChordLane ? theme::px (24) : 0);
            const double v = 1.0 - jlimit (0.0, 1.0, (double) (e.y - tt) / theme::px (48));
            double lo = 1e9, hi = 0;
            for (auto& tp : c.project.tempo.tempos()) { lo = std::min (lo, tp.bpm); hi = std::max (hi, tp.bpm); }
            lo = std::floor (lo - 10); hi = std::ceil (hi + 10);
            c.project.tempo.setTempoAt (dragOrigin, std::round ((lo + v * (hi - lo)) * 10.0) / 10.0, e.mods.isAltDown());
            c.refresh();
            break;
        }
        case Drag::Swipe:
            swipeTo = tickAt ((float) e.x);
            repaint();
            break;
        case Drag::FadeIn:
        case Drag::FadeOut:
        {
            auto* clip = c.project.clip (dragClip);
            if (! clip) break;
            const auto& tempo = c.project.tempo;
            const double clipStart = tempo.secondsAt (clip->start), clipEnd = tempo.secondsAt (clip->end());
            const double here = tempo.secondsAt (std::max<Tick> (0, at));
            if (drag == Drag::FadeIn) clip->audio.fadeIn = jlimit (0.0, clipEnd - clipStart, here - clipStart);
            else clip->audio.fadeOut = jlimit (0.0, clipEnd - clipStart, clipEnd - here);
            repaint();
            break;
        }
        case Drag::WarpMarker:
        {
            auto* clip = c.project.clip (dragClip);
            if (! clip || dragPoint < 0 || dragPoint >= (int) clip->audio.markers.size()) break;
            const Tick base = clip->start - clip->offset;
            clip->audio.markers[(size_t) dragPoint].tick = std::max<Tick> (0, (e.mods.isCommandDown() ? at : c.arrangeSnap (at)) - base);
            repaint();
            break;
        }
        case Drag::Section:
        {
            const Section* sec = nullptr;
            for (auto& s : c.project.sections) if (s.id == dragSection) sec = &s;
            if (! sec) break;
            // To the nearest bar line.
            const Tick want = std::max<Tick> (0, sec->start + (at - dragOrigin));
            const auto& tempo = c.project.tempo;
            const int b = tempo.barAt (want);
            sectionTo = want - tempo.barStart (b) < tempo.barStart (b + 1) - want ? tempo.barStart (b) : tempo.barStart (b + 1);
            repaint();
            break;
        }
        case Drag::Marker:
        case Drag::None:
            break;
    }
}

std::vector<Arrange::Moving> Arrange::movingTogether() const
{
    std::vector<Moving> out;
    const auto* dragged = c.project.clip (dragClip);
    if (drag != Drag::MoveClip || ! dragged || ! c.selectedClips.count (dragClip) || c.selectedClips.size() < 2) return out;
    Tick shift = previewStart - dragged->start;
    const auto order = c.arrangeOrder();
    auto indexOf = [&order] (Id t) { return (int) (std::find (order.begin(), order.end(), t) - order.begin()); };
    const int trackShift = indexOf (previewTrack) - indexOf (dragTrack);
    for (auto id : c.selectedClips)
        if (const auto* cl = c.project.clip (id)) shift = std::max (shift, -cl->start);   // none before the start
    for (auto id : c.selectedClips)
    {
        const Track* owner = nullptr;
        const auto* cl = c.project.clip (id, &owner);
        if (! cl || ! owner) continue;
        Id to = owner->id;
        const int i = indexOf (owner->id) + trackShift;
        if (trackShift != 0 && i >= 0 && i < (int) order.size())
            if (const auto* t = c.project.track (order[(size_t) i]); t && t->holdsClips()) to = t->id;
        out.push_back ({ id, cl->start + shift, to });
    }
    return out;
}

void Arrange::mouseUp (const MouseEvent& e)
{
    if (toolDrag) { toolUp (e); return; }
    if (drag == Drag::Section)
    {
        const Id id = dragSection;
        const Tick to = sectionTo;
        drag = Drag::None;
        dragSection = 0;
        for (auto& s : c.project.sections)
            if (s.id == id && s.start != to)
            {
                const String name (s.name);
                c.apply ("Move Section", [id, to] (Project& p) { song::moveSectionTo (p, id, to); });
                c.say (name + " moved to bar " + String (c.project.tempo.barAt (to) + 1) + ", with everything in it.", false, true);
            }
        repaint();
        return;
    }
    if (drag == Drag::MoveClip && dragClip)
    {
        const auto* clip = c.project.clip (dragClip);
        const auto together = movingTogether();
        if (clip && ! together.empty() && (previewStart != clip->start || previewTrack != dragTrack || copying))
        {
            // Every chosen clip, one undo step.
            if (copying)
            {
                std::set<Id> made;
                c.apply ("Copy Clips", [&] (Project& p)
                {
                    for (auto& m : together)
                        if (const auto* from = p.clip (m.clip))
                        {
                            Clip copy = *from;
                            copy.start = m.start;
                            for (auto& n : copy.midi.notes) n.id = 0;
                            made.insert (edit::addClip (p, m.track, copy));
                        }
                });
                c.selectedClips = made;
            }
            else
                c.apply ("Move Clips", [together] (Project& p) { for (auto& m : together) edit::moveClip (p, m.clip, m.start, m.track); });
        }
        else if (clip && (previewStart != clip->start || previewTrack != dragTrack || copying))
        {
            const Id id = dragClip, to = previewTrack;
            const Tick start = previewStart;
            if (copying)
            {
                Clip copy = *clip;
                copy.start = start;
                for (auto& n : copy.midi.notes) n.id = 0;
                Id made = 0;
                c.apply ("Copy Clip", [&] (Project& p) { made = edit::addClip (p, to, copy); });
                c.selectClip (made, false);
            }
            else
                c.apply ("Move Clip", [=] (Project& p) { edit::moveClip (p, id, start, to); });
        }
    }
    else if ((drag == Drag::TrimStart || drag == Drag::TrimEnd) && dragClip && ! (shiftTrim && ! e.mouseWasDraggedSinceMouseDown()))
    {
        const Id id = dragClip;
        const Tick a = previewStart, b = previewEnd;
        // The edge moved lands on the very sample of its zero crossing (or on its tick when that's off).
        const auto* clip = c.project.clip (id);
        const bool start = drag == Drag::TrimStart;
        const double fine = clip ? c.zeroEdge (*clip, start ? a : b).fine : 0.0;
        c.apply ("Trim Clip", [=] (Project& p)
        {
            edit::resizeClip (p, id, a, b);
            if (auto* x = p.clip (id)) (start ? x->audio.startFine : x->audio.endFine) = fine;
        });
    }
    else if (drag == Drag::Swipe)
    {
        const auto* t = c.project.track (dragTrack);
        if (t && swipeTake >= 0 && swipeTake < (int) t->version().takes.size())
        {
            const auto& take = t->version().takes[(size_t) swipeTake];
            Tick a = std::min (swipeFrom, swipeTo), b = std::max (swipeFrom, swipeTo);
            if (b - a < timeline.ticksFor (4.0f))
            {
                // A click uses the take's clip under the pointer, all of it.
                for (auto& cl : take.clips) if (a >= cl.start && a < cl.end()) { a = cl.start; b = cl.end(); }
            }
            else { a = c.arrangeSnap (a); b = c.arrangeSnap (b); }
            const Id tid = dragTrack, takeId = take.id;
            if (b > a) c.apply ("Comp", [=] (Project& p) { comp::choose (p, tid, takeId, a, b); });
        }
        swipeTake = -1;
    }
    else if (drag == Drag::FadeIn || drag == Drag::FadeOut)
    {
        if (! (dragBefore == c.project)) c.history.push (dragBefore, "Fade");
        c.refresh();
    }
    else if (drag == Drag::WarpMarker)
    {
        if (! (dragBefore == c.project)) c.history.push (dragBefore, "Move Warp Marker");
        c.refresh();
    }
    else if (drag == Drag::Loop || drag == Drag::Point || drag == Drag::Tempo)
    {
        // These changed the project live while dragging: now they're one undo step.
        c.refresh();
    }
    drag = Drag::None;
    dragClip = 0;
    repaint();
}

void Arrange::mouseDoubleClick (const MouseEvent& e)
{
    const auto lanes = laneArea();
    if (e.y < theme::px (36) && e.x >= lanes.getX())
    {
        const Tick at = c.arrangeSnap (tickAt ((float) e.x));
        c.apply ("Add Marker", [at] (Project& p)
        {
            p.markers.push_back ({ p.newId(), at, "Marker " + std::to_string (p.markers.size() + 1) });
            std::sort (p.markers.begin(), p.markers.end(), [] (const Marker& a, const Marker& b) { return a.tick < b.tick; });
        });
        return;
    }
    if (! lanes.contains (e.getPosition())) return;
    const auto* row = rowAt (e.y);
    if (! row || ! row->isClips()) return;
    const auto* t = c.project.track (row->track);
    if (! t || ! t->holdsClips() || clipAt (e.getPosition())) return;
    const auto& tempo = c.project.tempo;
    const int bar = tempo.barAt (tickAt ((float) e.x));
    c.newMidiClip (t->id, tempo.barStart (bar), tempo.meterAtBar (bar).barTicks());
}

void Arrange::mouseWheelMove (const MouseEvent& e, const MouseWheelDetails& w)
{
    if (e.mods.isCommandDown() || e.mods.isCtrlDown())
    {
        timeline.zoomAround ((float) (e.x - headerWidth()), w.deltaY > 0 ? 1.15 : 1.0 / 1.15);
    }
    else
    {
        if (std::abs (w.deltaX) > 0.0f || e.mods.isShiftDown())
            timeline.startTick = std::max (0.0, timeline.startTick - (e.mods.isShiftDown() ? w.deltaY : w.deltaX) * 400.0 / timeline.pixelsPerTick * 0.1);
        if (! e.mods.isShiftDown() && std::abs (w.deltaY) > 0.0f)
            scrollY = jlimit (0, jmax (0, contentHeight - laneArea().getHeight() + theme::px (80)), scrollY - (int) (w.deltaY * 300));
    }
    layoutRows();
    repaint();
}

void Arrange::mouseMagnify (const MouseEvent& e, float scale)
{
    timeline.zoomAround ((float) (e.x - headerWidth()), scale);
    layoutRows();
    repaint();
}

void Arrange::zoomToFit()
{
    const auto lanes = laneArea();
    timeline.startTick = 0;
    timeline.pixelsPerTick = jlimit (0.0005, 2.0, (double) lanes.getWidth() / (double) jmax<Tick> (PPQ * 4, c.project.endTick()));
    layoutRows();
    repaint();
}

void Arrange::reorder (Id track, int y)
{
    const auto* row = rowAt (y);
    if (! row || row->track == track) return;
    const auto* target = c.project.track (row->track);
    if (! target) return;
    // Dropped on a folder: into it. Otherwise above the track dropped on, at its level.
    if (target->isFolder() && y > laneArea().getY() + row->y - scrollY + row->h / 3)
        c.apply ("Move Track", [=] (Project& p) { edit::moveTrack (p, track, 0, target->id); });
    else
        c.apply ("Move Track", [=, parent = target->parent, before = target->id] (Project& p) { edit::moveTrack (p, track, before, parent); });
}

//==============================================================================
void Arrange::clipMenu (Id id)
{
    const Track* owner = nullptr;
    const auto* clip = c.project.clip (id, &owner);
    if (! clip) return;
    PopupMenu m;
    m.addItem ("Split at Cursor", clip->start < c.cursor && clip->end() > c.cursor, false, [this] { c.splitAtCursor(); });
    m.addItem ("Duplicate", [this] { c.duplicateSelection(); });
    // Alias clips (decision 0074): copies that share their notes or audio, marked with a chain.
    {
        auto chosen = c.selectedClips;
        if (! chosen.count (id)) chosen = { id };
        bool allAlias = true;
        for (auto cid : chosen) if (const auto* x = c.project.clip (cid)) allAlias &= x->link != 0;
        if (! allAlias)
            m.addItem (chosen.size() > 1 ? "Create Alias Clips" : "Create Alias Clip", [this, chosen]
            {
                c.apply ("Create Alias Clip", [chosen] (Project& p) { song::makeAlias (p, chosen); });
                c.say ("An alias clip: duplicate it (Command-D, or Option-drag) and an edit to the notes of any copy is made to them all.", false, true);
            });
        else
            m.addItem ("Make Independent", [this, chosen]
            {
                c.apply ("Make Independent", [chosen] (Project& p) { for (auto cid : chosen) song::unlink (p, cid); });
                c.say ("No longer an alias: edits to it are its own.", false, true);
            });
    }
    m.addItem ("Duplicate as Alias", [this, id]
    {
        Id made = 0;
        c.apply ("Duplicate as Alias", [&] (Project& p) { made = song::duplicateLinked (p, id); });
        if (made) { c.selectedClips = { made }; c.selectedClip = made; }
        c.say ("Alias clips: an edit to the notes or audio of one is made to every copy.");
    });
    m.addItem ("Delete", [this] { c.deleteSelection(); });
    m.addSeparator();
    // On every chosen clip (Shift-click and Option-click choose several, decision 0053), 0071.
    {
        auto chosen = c.selectedClips;
        if (! chosen.count (id)) chosen = { id };
        bool midi = false, allDetected = true;
        for (auto cid : chosen)
            if (const auto* x = c.project.clip (cid); x && x->isMidi()) { midi = true; allDetected &= x->detectChords; }
        const bool several = chosen.size() > 1;
        m.addItem (several ? "Set the Loop to the Clips" : "Set the Loop to the Clip", [this, chosen]
        {
            c.apply ("Loop to Clips", [chosen] (Project& p) { song::loopToClips (p, chosen); });
            c.say ("The loop is set to " + String (c.project.tempo.formatBarBeat (c.project.loop.start)) + " to "
                   + String (c.project.tempo.formatBarBeat (c.project.loop.end)) + ", and on.", false, true);
        });
        m.addItem ("Explode Chords to Tracks", midi, false, [this, chosen] { c.explodeChords (chosen); });
        if (clip->isMidi())
            m.addItem (ui::u8 ("Fix Note Timings and Volume\xe2\x80\xa6"), [this, id] { fixNotes (id); });
        m.addItem ("Bounce in Place", [this, chosen] { c.bounceInPlace (chosen); });
        m.addItem ("Detect Chords", midi, midi && allDetected, [this, chosen, on = ! (midi && allDetected)]
        {
            c.apply (on ? "Detect Chords" : "Hide Chords", [chosen, on] (Project& p) { song::detectChords (p, chosen, on); });
            if (on && ! c.showChordLane) { c.showChordLane = true; c.viewChanged(); }
            c.say (on ? String ("The chords of ") + (chosen.size() > 1 ? "these clips" : "this clip") + " are in the chord lane above the tracks."
                      : String ("Their chords are no longer shown."), false, true);
        });
        // MIDI clips: their notes up or down, every chosen clip at once (the
        // guide had it; the UAT found it missing, decision 0133).
        if (midi)
        {
            PopupMenu notesTranspose;
            for (int s : { 12, 7, 1, -1, -7, -12 })
                notesTranspose.addItem ((s > 0 ? "Up " : "Down ") + String (std::abs (s)), [this, chosen, s]
                {
                    c.apply ("Transpose", [chosen, s] (Project& p)
                    {
                        for (auto cid : chosen)
                            if (auto* x = p.clip (cid); x && x->isMidi())
                            {
                                std::set<Id> all;
                                for (auto& n : x->midi.notes) all.insert (n.id);
                                edit::transpose (p, cid, all, s);
                            }
                    });
                });
            m.addSubMenu ("Transpose", notesTranspose);
        }
    }
    m.addSeparator();
    m.addItem ("Loop Its Content", true, clip->loopLength > 0, [this, id]
    {
        c.apply ("Loop Clip", [id] (Project& p) { edit::loopContent (p, id); });
    });
    // Reverse and Invert (0095), on every chosen clip: MIDI backwards and upside
    // down (its highest note its lowest); audio backwards and its polarity flipped.
    {
        auto chosen = c.selectedClips;
        if (! chosen.count (id)) chosen = { id };
        const bool audio = ! clip->isMidi();
        m.addItem ("Reverse", true, audio && clip->audio.reversed, [this, chosen]
        {
            c.apply ("Reverse", [chosen] (Project& p) { for (auto cid : chosen) edit::reverseClip (p, cid); });
        });
        m.addItem (audio ? "Invert (Polarity, Upside Down)" : "Invert (Upside Down)", true, audio && clip->audio.inverted, [this, chosen]
        {
            c.apply ("Invert", [chosen] (Project& p) { for (auto cid : chosen) edit::invertClip (p, cid); });
        });
        // Humanise (0124): each note's velocity and timing a little different, by chance.
        if (! audio)
            m.addItem ("Humanise", true, false, [this, chosen]
            {
                const auto seed = (unsigned) Random::getSystemRandom().nextInt (1 << 30) + 1u;
                const Tick grid = c.grid;
                c.apply ("Humanise", [chosen, seed, grid] (Project& p)
                {
                    unsigned k = 0;
                    for (auto cid : chosen) edit::humanise (p, cid, grid, seed + k++);
                });
            });
        if (audio)
            m.addItem (ui::u8 ("Split at Transients\xe2\x80\xa6"), clip->loopLength == 0 && ! clip->audio.reversed, false, [this, chosen]
            {
                // How sensitive, with how many clips it makes (0130).
                std::vector<TransientSource> sources;
                for (auto cid : chosen) sources.push_back ({ cid, {} });
                askTransients (c, "Split at Transients", sources, [this, chosen] (double s) { for (auto cid : chosen) c.splitAtTransients (cid, s); });
            });
    }
    m.addItem ("Mute", true, clip->muted, [this, id] { c.apply ("Mute Clip", [id] (Project& p) { if (auto* cl = p.clip (id)) cl->muted = ! cl->muted; }); });
    m.addItem ("Lock", true, clip->locked, [this, id] { c.apply ("Lock Clip", [id] (Project& p) { if (auto* cl = p.clip (id)) cl->locked = ! cl->locked; }); });
    m.addItem ("Consolidate the Chosen Clips", ! c.selectedClips.empty(), false, [this] { c.consolidate (c.selectedClips); });
    if (! clip->isMidi())
    {
        PopupMenu warp, transpose, offline;
        const char* names[] { "Off: Its Own Speed", "Repitch: Like Tape", "Granular: Drums and Textures", "Spectral: Tonal and Mixes" };
        for (int i = 0; i < 4; ++i)
            warp.addItem (names[i], true, (int) clip->audio.warp == i, [this, id, i]
            {
                c.apply ("Warp", [id, i] (Project& p)
                {
                    auto* cl = p.clip (id);
                    if (! cl) return;
                    cl->audio.warp = (WarpMode) i;
                    // Warping with no tempo known: assume it was played at the project's.
                    if (i != 0 && cl->audio.sourceBpm <= 0 && cl->audio.markers.size() < 2)
                        cl->audio.sourceBpm = p.tempo.bpmAt (cl->start);
                });
            });
        warp.addSeparator();
        warp.addItem (ui::u8 ("The File's Tempo\xe2\x80\xa6"), [this, id]
        {
            auto* w = new AlertWindow ("The File's Tempo", "BPM the audio was played at. It's stretched to the project's tempo.", MessageBoxIconType::NoIcon);
            w->addTextEditor ("bpm", String (c.project.clip (id)->audio.sourceBpm, 2));
            w->addButton ("Set", 1, KeyPress (KeyPress::returnKey));
            w->addButton ("Cancel", 0, KeyPress (KeyPress::escapeKey));
            ui::present (w, ModalCallbackFunction::create ([this, id, w] (int ok)
            {
                if (ok != 1) return;
                const double bpm = w->getTextEditorContents ("bpm").getDoubleValue();
                if (bpm < 20 || bpm > 400) return;
                c.apply ("File Tempo", [id, bpm] (Project& p)
                {
                    auto* cl = p.clip (id);
                    if (! cl) return;
                    cl->audio.sourceBpm = bpm;
                    if (cl->audio.warp == WarpMode::Off) cl->audio.warp = WarpMode::Spectral;
                    cl->length = (Tick) std::llround (cl->audio.sourceLength * bpm / 60.0 * PPQ) - cl->offset;
                });
            }), true);
        });
        warp.addItem ("Add a Warp Marker at the Cursor", clip->start <= c.cursor && clip->end() > c.cursor, false, [this, id]
        {
            c.apply ("Warp Marker", [this, id] (Project& p)
            {
                auto* cl = p.clip (id);
                if (! cl) return;
                const Tick rel = c.cursor - (cl->start - cl->offset);
                const double src = AudioCache::sourceSecondsAt (cl->audio, rel);
                cl->audio.markers.push_back ({ src < 0 ? cl->audio.sourceStart + p.tempo.secondsAt (c.cursor) - p.tempo.secondsAt (cl->start - cl->offset) : src, rel });
                if (cl->audio.warp == WarpMode::Off) cl->audio.warp = WarpMode::Spectral;
            });
        });
        m.addSubMenu ("Warp", warp);
        for (int s : { -12, -7, -5, -2, -1, 0, 1, 2, 5, 7, 12 })
            transpose.addItem ((s > 0 ? "+" : "") + String (s), true, std::abs (clip->audio.transpose - s) < 0.01, [this, id, s]
            {
                c.apply ("Transpose", [id, s] (Project& p)
                {
                    auto* cl = p.clip (id);
                    if (! cl) return;
                    cl->audio.transpose = s;
                    if (cl->audio.warp == WarpMode::Off || cl->audio.warp == WarpMode::Repitch) cl->audio.warp = WarpMode::Spectral;
                });
            });
        m.addSubMenu ("Transpose", transpose);
        m.addItem ("Normalise", [this, id] { c.normaliseClip (id); });
        m.addItem ("Strip Silence", [this, id] { c.stripSilence (id); });
        m.addItem ("Quantise Audio to the Grid", [this, id] { c.quantiseAudio (id); });
        m.addItem ("Edit Samples", [this, id] { c.selectClip (id); c.zoneTab = ZoneTab::Editor; c.viewChanged(); });
        m.addItem ("Fade In and Out (10 ms)", [this, id] { c.apply ("Fades", [id] (Project& p) { if (auto* cl = p.clip (id)) { cl->audio.fadeIn = 0.01; cl->audio.fadeOut = 0.01; } }); });

        // Offline effects (decision 0026).
        // Audio effects only, under their heading, by maker (0131).
        offline = pluginmenu::forSlot (c.engine.plugins(), { DeviceRole::AudioFx }, [this, id] (const PluginRef& ref, DeviceRole)
        {
            c.apply ("Offline Effect", [id, ref] (Project& p)
            {
                if (auto* cl = p.clip (id))
                {
                    Device dev;
                    dev.id = p.newId();
                    dev.plugin = ref;
                    cl->offlineFx.chain.push_back (dev);
                }
            });
        });
        PopupMenu fxMenu;
        fxMenu.addSubMenu ("Add", offline);
        for (size_t i = 0; i < clip->offlineFx.chain.size(); ++i)
            fxMenu.addItem ("Remove " + String (clip->offlineFx.chain[i].plugin.name), [this, id, i]
            {
                c.apply ("Remove Offline Effect", [id, i] (Project& p) { if (auto* cl = p.clip (id)) cl->offlineFx.chain.erase (cl->offlineFx.chain.begin() + (long) i); });
            });
        fxMenu.addSeparator();
        fxMenu.addItem ("Edit Their Settings", ! clip->offlineFx.chain.empty(), false, [this, id]
        {
            c.offlineClip = id;
            c.zoneTab = ZoneTab::Devices;
            c.viewChanged();
        });
        fxMenu.addItem ("Render", ! clip->offlineFx.chain.empty(), false, [this, id] { c.renderOfflineFx (id); });
        fxMenu.addItem ("Back to the Original", clip->audio.renderedFile.size() > 0, false, [this, id]
        {
            c.apply ("Original Audio", [id] (Project& p) { if (auto* x = p.clip (id)) x->audio.renderedFile.clear(); });
        });
        m.addSubMenu ("Offline Effects", fxMenu);
    }
    m.showMenuAsync (PopupMenu::Options());
}

void Arrange::laneMenu (const Row& row, Point<int> at)
{
    const auto* t = c.project.track (row.track);
    if (! t) return;
    PopupMenu m;
    const Tick tick = c.arrangeSnap (tickAt ((float) at.x));
    if (row.take >= 0)
    {
        const Id tid = t->id;
        const Id takeId = t->version().takes[(size_t) row.take].id;
        m.addItem ("Use This Take Everywhere", [this, tid, takeId]
        {
            c.apply ("Comp", [=] (Project& p)
            {
                const auto& takes = p.track (tid)->version().takes;
                for (auto& tk : takes) if (tk.id == takeId) { const auto [a, b] = comp::extent (tk); comp::choose (p, tid, takeId, a, b); }
            });
        });
        m.addItem ("Delete This Take", [this, tid, takeId] { c.apply ("Delete Take", [=] (Project& p) { comp::removeTake (p, tid, takeId); }); });
        m.addSeparator();
        m.addItem ("Flatten the Comp", [this, tid]
        {
            c.apply ("Flatten Comp", [=] (Project& p) { comp::flatten (p, tid); });
            c.say ("The comp is now ordinary clips; the takes are gone.", false, true);
        });
        m.addItem ("Hide Takes", [this, tid] { c.apply ("Hide Takes", [=] (Project& p) { p.track (tid)->version().takesShown = false; }); });
        m.showMenuAsync (PopupMenu::Options());
        return;
    }
    if (row.automation >= 0)
    {
        const int laneIndex = row.automation;
        const Id tid = t->id;
        // Copy and paste (decision 0038): the loop range's points, or the bar clicked.
        const auto& loop = c.project.loop;
        const bool useLoop = loop.end > loop.start && tick >= loop.start && tick < loop.end;
        const int clickedBar = c.project.tempo.barAt (tick);
        const Tick from = useLoop ? loop.start : c.project.tempo.barStart (clickedBar);
        const Tick to = useLoop ? loop.end : c.project.tempo.barStart (clickedBar + 1);
        m.addItem (useLoop ? "Copy the Loop Range" : "Copy This Bar", [this, tid, laneIndex, from, to]
        {
            const auto* tr = c.project.track (tid);
            if (! tr || laneIndex >= (int) tr->automation.size()) return;
            c.automationClipboard = edit::copyPoints (tr->automation[(size_t) laneIndex], from, to);
            c.automationClipboardLength = to - from;
            c.say ("Copied " + String ((int) c.automationClipboard.size()) + " points. Right-click where they go and choose Paste.");
        });
        m.addItem ("Paste Here", ! c.automationClipboard.empty(), false, [this, tid, laneIndex, at = c.arrangeSnap (tick)]
        {
            const auto pts = c.automationClipboard;
            const Tick len = c.automationClipboardLength;
            c.apply ("Paste Automation", [=] (Project& p) { edit::pastePoints (p.track (tid)->automation[(size_t) laneIndex], pts, at, len); });
        });
        m.addItem (useLoop ? "Clear the Loop Range" : "Clear This Bar", [this, tid, laneIndex, from, to]
        {
            c.apply ("Clear Automation", [=] (Project& p) { edit::removePointsIn (p.track (tid)->automation[(size_t) laneIndex], from, to - 1); });
        });
        m.addSeparator();
        m.addItem ("Hide This Lane", [this, tid, laneIndex] { c.apply ("Hide Lane", [=] (Project& p) { p.track (tid)->automation[(size_t) laneIndex].shown = false; }); });
        m.addItem ("Delete This Lane", [this, tid, laneIndex] { c.apply ("Delete Lane", [=] (Project& p) { auto& a = p.track (tid)->automation; a.erase (a.begin() + laneIndex); }); });
        m.showMenuAsync (PopupMenu::Options());
        return;
    }
    if (t->holdsClips())
    {
        const auto& tempo = c.project.tempo;
        const int bar = tempo.barAt (tick);
        m.addItem ("New MIDI Clip Here", [this, id = t->id, bar]
        {
            const auto& tm = c.project.tempo;
            c.newMidiClip (id, tm.barStart (bar), tm.meterAtBar (bar).barTicks());
        });
        m.addItem ("Capture MIDI Here", [this, tick] { c.locate (tick); c.captureMidi(); });
        // A sample brought in as a clip for each of its hits (0095).
        m.addItem (ui::u8 ("Import Audio Here, Split at Transients\xe2\x80\xa6"), [this, id = t->id, tick]
        {
            chooser = std::make_unique<FileChooser> ("Import, Split at Transients", File::getSpecialLocation (File::userMusicDirectory),
                                                     "*.wav;*.aif;*.aiff;*.flac;*.mp3;*.ogg;*.m4a;*.caf");
            chooser->launchAsync (FileBrowserComponent::openMode | FileBrowserComponent::canSelectFiles, [this, id, tick] (const FileChooser& fc)
            {
                const auto f = fc.getResult();
                if (f.existsAsFile())   // how sensitive, with how many clips it makes (0130)
                    askTransients (c, "Import, Split at Transients", { { 0, f } }, [this, f, id, tick] (double s) { c.importSplit (f, id, tick, s); });
            });
        });
    }
    m.addItem ("Show Volume Automation", [this, id = t->id]
    {
        c.apply ("Volume Lane", [id] (Project& p)
        {
            AutoTarget target;
            target.kind = AutoTarget::Kind::Volume;
            auto& lane = edit::laneFor (*p.track (id), target, (p.track (id)->volumeDb + 60.0) / 66.0);
            if (lane.id == 0) lane.id = p.newId();
            lane.shown = true;
        });
    });
    m.addItem ("Show Pan Automation", [this, id = t->id]
    {
        c.apply ("Pan Lane", [id] (Project& p)
        {
            AutoTarget target;
            target.kind = AutoTarget::Kind::Pan;
            auto& lane = edit::laneFor (*p.track (id), target, (p.track (id)->pan + 1.0) / 2.0);
            if (lane.id == 0) lane.id = p.newId();
            lane.shown = true;
        });
    });
    for (size_t i = 0; i < t->automation.size(); ++i)
        if (! t->automation[i].shown)
            m.addItem ("Show " + String (t->automation[i].target.paramName.empty() ? "Lane" : t->automation[i].target.paramName), [this, id = t->id, i]
            {
                c.apply ("Show Lane", [id, i] (Project& p) { p.track (id)->automation[i].shown = true; });
            });
    m.addItem ("Automate the Last Touched Control", c.lastTouched.valid(), false, [this] { c.automateLastTouched(); });
    m.showMenuAsync (PopupMenu::Options());
}

//==============================================================================
bool Arrange::isInterestedInFileDrag (const StringArray& files)
{
    for (auto& f : files)
        if (File (f).hasFileExtension ("wav;aif;aiff;flac;mp3;ogg;mid;midi")) return true;
    return false;
}

void Arrange::filesDropped (const StringArray& files, int x, int y)
{
    const auto* row = rowAt (y);
    Id track = row && row->isClips() ? row->track : 0;
    Tick at = c.arrangeSnap (std::max<Tick> (0, tickAt ((float) x)));
    for (auto& f : files)
    {
        c.importFile (File (f), track, at);
        track = 0;   // further files on new tracks
    }
}

bool Arrange::isInterestedInDragSource (const SourceDetails& d)
{
    const auto t = d.description.toString();
    return t.startsWith ("plugin:") || t.startsWith ("file:") || t.startsWith ("drums:");
}

void Arrange::itemDropped (const SourceDetails& d)
{
    const auto* row = rowAt (d.localPosition.y);
    const auto text = d.description.toString();
    if (text.startsWith ("drums:"))
    {
        // Kalibre Drums' pattern (0105), dragged from its window: a clip where it's let go.
        const auto* from = reinterpret_cast<const AudioProcessor*> ((pointer_sized_int) text.fromFirstOccurrenceOf ("drums:", false, false).getHexValue64());
        // A beat is bars long: let go within a beat of a bar line, it starts on the bar (the UAT
        // let go a little after bar 1 and got a sixteenth late); further off, on the grid.
        const Tick at = std::max<Tick> (0, tickAt ((float) d.localPosition.x));
        const int b = c.project.tempo.barAt (at);
        const Tick before = c.project.tempo.barStart (b), after = c.project.tempo.barStart (b + 1);
        const Tick bar = at - before <= after - at ? before : after;
        c.dropDrumPattern (from, row && row->isClips() ? row->track : 0, std::abs (at - bar) <= PPQ ? bar : c.arrangeSnap (at));
        return;
    }
    if (text.startsWith ("file:"))
    {
        filesDropped ({ text.fromFirstOccurrenceOf ("file:", false, false) }, d.localPosition.x, d.localPosition.y);
        return;
    }
    const int index = text.fromFirstOccurrenceOf ("plugin:", false, false).getIntValue();
    const auto types = c.engine.plugins().known().getTypes();
    if (index < 0 || index >= types.size()) return;
    Id track = row ? row->track : 0;
    if (track == 0) track = c.addTrack (TrackKind::Track, types[index].name);
    c.addPlugin (track, types[index]);
}

} // namespace kal
