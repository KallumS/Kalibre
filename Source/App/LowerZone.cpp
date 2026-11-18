#include "LowerZone.h"

namespace kal
{
using namespace juce;

LowerZone::LowerZone (Controller& ctrl)
    : c (ctrl), devices (ctrl), roll (ctrl), drums (ctrl), steps (ctrl), euclid (ctrl), notation (ctrl), mixer (ctrl), sample (ctrl), launcher (ctrl), offline (ctrl), meters (ctrl), videoView (ctrl)
{
    for (auto* b : { &devicesTab, &editorTab, &mixerTab, &launcherTab, &metersTab, &videoTab })
    {
        b->setRadioGroupId (1);
        b->setClickingTogglesState (true);
        addAndMakeVisible (b);
    }
    for (auto* b : { &rollTab, &drumsTab, &stepsTab, &euclidTab, &notationTab })
    {
        b->setRadioGroupId (2);
        b->setClickingTogglesState (true);
        addChildComponent (b);
    }
    for (auto* b : { &gridButton, &snapButton, &quantise, &legato, &selectButton, &ccLanes, &autoCc }) addChildComponent (b);
    selectButton.onClick = [this] { selectMenu(); };
    ccLanes.onClick = [this] { CcLanes::lanesMenu (c, &ccLanes); };
    autoCc.onClick = [this] { c.autoCc(); };
    for (auto* comp : std::initializer_list<Component*> { &devices, &roll, &drumsView, &steps, &euclidView, &notation, &mixer, &sample, &launcher, &offline, &meters, &videoView })
        addChildComponent (comp);
    drumsView.setViewedComponent (&drums, false);
    euclidView.setViewedComponent (&euclid, false);
    for (auto* v : { &drumsView, &euclidView }) v->setScrollBarsShown (true, false);

    devicesTab.onClick = [this] { c.zoneTab = ZoneTab::Devices; show(); };
    editorTab.onClick = [this] { c.zoneTab = ZoneTab::Editor; show(); };
    mixerTab.onClick = [this] { c.zoneTab = ZoneTab::Mixer; show(); };
    launcherTab.onClick = [this] { c.zoneTab = ZoneTab::Launcher; show(); };
    metersTab.onClick = [this] { c.zoneTab = ZoneTab::Meters; show(); };
    videoTab.onClick = [this] { c.zoneTab = ZoneTab::Video; show(); };
    rollTab.onClick = [this] { c.editor = EditorKind::PianoRoll; show(); };
    drumsTab.onClick = [this] { c.editor = EditorKind::Drums; show(); };
    stepsTab.onClick = [this] { c.editor = EditorKind::Steps; show(); };
    euclidTab.onClick = [this] { c.editor = EditorKind::Euclid; show(); };
    notationTab.onClick = [this] { c.editor = EditorKind::Notation; show(); };
    gridButton.onClick = [this] { gridMenu(); };
    snapButton.onClick = [this] { c.snap = snapButton.getToggleState(); c.viewChanged(); };
    quantise.onClick = [this] { c.quantiseSelection(); };
    legato.onClick = [this]
    {
        const Id cid = c.selectedClip;
        auto ids = c.selectedNotes;
        c.apply ("Legato", [=] (Project& p) { edit::legato (p, cid, ids); });
    };
    c.addChangeListener (this);
    show();
}

LowerZone::~LowerZone() { c.removeChangeListener (this); }

void LowerZone::changeListenerCallback (ChangeBroadcaster*)
{
    if (c.zoneTab != shownTab || c.editor != shownEditor || audioChosen() != shownAudio || c.offlineClip != offline.shown()
        || videoTab.isVisible() == c.project.video.file.empty()) show();
    snapButton.setToggleState (c.snap, dontSendNotification);
    updateAuto();
}

// Auto works only on a track that names an instrument AutoCC knows (decision 0060).
void LowerZone::updateAuto()
{
    const auto found = c.autoCcFor();
    autoCc.setEnabled (found.family != autocc::Family::None);
    autoCc.setTip (found.family != autocc::Family::None
                       ? "Auto: draw CC1, CC11, CC7 and CC21 as AutoCC plays " + String (autocc::familyName (found.family))
                             + " (from " + String (found.from).quoted() + "), replacing what those lanes have."
                       : String ("Auto draws the CC lanes as AutoCC would play the notes. It needs a track named for its instrument, such as Violins, Horns or Flute."));
}

bool LowerZone::audioChosen() const
{
    const auto* cl = c.project.clip (c.selectedClip);
    return cl && ! cl->isMidi();
}

void LowerZone::show()
{
    shownTab = c.zoneTab;
    shownEditor = c.editor;
    shownAudio = audioChosen();
    devicesTab.setToggleState (c.zoneTab == ZoneTab::Devices, dontSendNotification);
    editorTab.setToggleState (c.zoneTab == ZoneTab::Editor, dontSendNotification);
    mixerTab.setToggleState (c.zoneTab == ZoneTab::Mixer, dontSendNotification);
    launcherTab.setToggleState (c.zoneTab == ZoneTab::Launcher, dontSendNotification);
    launcher.setVisible (c.zoneTab == ZoneTab::Launcher);
    metersTab.setToggleState (c.zoneTab == ZoneTab::Meters, dontSendNotification);
    meters.setVisible (c.zoneTab == ZoneTab::Meters);
    // The Video tab is there when the song has a video (decision 0046).
    if (c.zoneTab == ZoneTab::Video && c.project.video.file.empty()) c.zoneTab = ZoneTab::Devices;
    videoTab.setVisible (! c.project.video.file.empty());
    videoTab.setToggleState (c.zoneTab == ZoneTab::Video, dontSendNotification);
    videoView.setVisible (c.zoneTab == ZoneTab::Video);
    // An audio clip is edited in the sample editor; the note editors are for MIDI.
    const bool audio = c.zoneTab == ZoneTab::Editor && shownAudio;
    const bool editing = c.zoneTab == ZoneTab::Editor && ! audio;
    for (auto* b : { &rollTab, &drumsTab, &stepsTab, &euclidTab, &notationTab, &gridButton, &snapButton, &quantise, &legato, &selectButton })
        b->setVisible (editing);
    const bool withLanes = editing && (c.editor == EditorKind::PianoRoll || c.editor == EditorKind::Notation);
    ccLanes.setVisible (withLanes);
    autoCc.setVisible (withLanes);
    updateAuto();
    sample.setVisible (audio);
    if (audio) sample.load();
    rollTab.setToggleState (c.editor == EditorKind::PianoRoll, dontSendNotification);
    drumsTab.setToggleState (c.editor == EditorKind::Drums, dontSendNotification);
    stepsTab.setToggleState (c.editor == EditorKind::Steps, dontSendNotification);
    euclidTab.setToggleState (c.editor == EditorKind::Euclid, dontSendNotification);
    notationTab.setToggleState (c.editor == EditorKind::Notation, dontSendNotification);
    snapButton.setToggleState (c.snap, dontSendNotification);

    // A clip's offline effects take the Devices tab while they're being set (decision 0039).
    if (c.offlineClip != 0 && ! c.project.clip (c.offlineClip)) c.offlineClip = 0;
    const bool offlineShown = c.zoneTab == ZoneTab::Devices && c.offlineClip != 0;
    if (offlineShown) offline.open (c.offlineClip);
    else if (offline.shown() != 0) offline.close();
    offline.setVisible (offlineShown);
    devices.setVisible (c.zoneTab == ZoneTab::Devices && ! offlineShown);
    mixer.setVisible (c.zoneTab == ZoneTab::Mixer);
    roll.setVisible (editing && c.editor == EditorKind::PianoRoll);
    drumsView.setVisible (editing && c.editor == EditorKind::Drums);
    steps.setVisible (editing && c.editor == EditorKind::Steps);
    euclidView.setVisible (editing && c.editor == EditorKind::Euclid);
    notation.setVisible (editing && c.editor == EditorKind::Notation);

    const char* names[] { "1/1", "1/2", "1/4", "1/8", "1/16", "1/32", "1/64" };
    const Tick values[] { PPQ * 4, PPQ * 2, PPQ, PPQ / 2, PPQ / 4, PPQ / 8, PPQ / 16 };
    String label = String (c.grid) + " ticks";
    for (int i = 0; i < 7; ++i)
    {
        if (values[i] == c.grid) label = names[i];
        if (values[i] * 2 / 3 == c.grid) label = String (names[i]) + "T";
    }
    gridButton.setButtonText (label);
    resized();
    repaint();
}

void LowerZone::gridMenu()
{
    PopupMenu m;
    const char* names[] { "1/1", "1/2", "1/4", "1/8", "1/16", "1/32", "1/64" };
    const Tick values[] { PPQ * 4, PPQ * 2, PPQ, PPQ / 2, PPQ / 4, PPQ / 8, PPQ / 16 };
    for (int i = 0; i < 7; ++i)
        m.addItem (names[i], true, c.grid == values[i], [this, v = values[i]] { c.grid = v; show(); c.viewChanged(); });
    m.addSeparator();
    for (int i = 1; i < 7; ++i)
        m.addItem (String (names[i]) + " Triplet", true, c.grid == values[i] * 2 / 3, [this, v = values[i] * 2 / 3] { c.grid = v; show(); c.viewChanged(); });
    m.showMenuAsync (PopupMenu::Options().withTargetComponent (&gridButton));
}

// Select (decision 0075): the piano roll and notation by chord, beat and line;
// the pattern editors their own notes by beat.
void LowerZone::selectMenu()
{
    using W = noteselect::What;
    PopupMenu m;
    std::optional<NoteFrom> only;
    if (c.editor == EditorKind::Drums) only = NoteFrom::Drums;
    if (c.editor == EditorKind::Steps) only = NoteFrom::Steps;
    if (c.editor == EditorKind::Euclid) only = NoteFrom::Euclid;
    auto add = [&] (const String& name, W w) { m.addItem (name, [this, w, only] { c.selectNotes (w, only); }); };
    const bool chordsToo = c.editor == EditorKind::PianoRoll || c.editor == EditorKind::Notation;
    if (chordsToo)
    {
        m.addSectionHeader ("In Each Chord");
        add ("Select Roots", W::Root);
        add ("Select 3rds", W::Third);
        add ("Select 5ths", W::Fifth);
        add ("Select 7ths", W::Seventh);
        add ("Select 9ths", W::Ninth);
        add ("Select 11ths", W::Eleventh);
        add ("Select 13ths", W::Thirteenth);
        m.addSectionHeader ("In Time");
    }
    add ("Select Every Beat", W::OnBeat);
    add ("Select Every Off Beat", W::OffBeat);
    if (chordsToo)
    {
        m.addSectionHeader ("Along the Clip");
        add ("Select the Bottom Notes", W::Bottom);
        add ("Select the Top Notes", W::Top);
    }
    m.addSeparator();
    m.addItem ("Select All", c.clip() != nullptr, false, [this, only]
    {
        c.selectedNotes.clear();
        if (auto* cl = c.clip()) for (auto& n : cl->midi.notes) if (! only || n.from == *only) c.selectedNotes.insert (n.id);
        c.sendChangeMessage();
    });
    m.addItem ("Select None", ! c.selectedNotes.empty(), false, [this] { c.selectedNotes.clear(); c.sendChangeMessage(); });
    if (chordsToo)
    {
        m.addSeparator();
        m.addItem ("Explode Chords to Tracks", c.clip() != nullptr, false, [this] { c.explodeChords ({ c.selectedClip }); });
    }
    m.showMenuAsync (PopupMenu::Options().withTargetComponent (&selectButton));
}

void LowerZone::paint (Graphics& g)
{
    g.fillAll (theme::ground);
    g.setColour (theme::rule);
    g.fillRect (0, theme::px (36) - 1, getWidth(), 1);
}

void LowerZone::resized()
{
    auto r = getLocalBounds();
    auto bar = r.removeFromTop (theme::px (36)).reduced (theme::px (8), theme::px (4));
    auto place = [&] (theme::HouseButton& b) { const int w = b.idealWidth (theme::smallControl); b.setBounds (bar.removeFromLeft (w)); bar.removeFromLeft (theme::px (4)); };
    place (devicesTab);
    place (editorTab);
    place (mixerTab);
    place (launcherTab);
    place (metersTab);
    if (videoTab.isVisible()) place (videoTab);
    bar.removeFromLeft (theme::px (12));
    if (c.zoneTab == ZoneTab::Editor && ! shownAudio)
    {
        for (auto* b : { &rollTab, &drumsTab, &stepsTab, &euclidTab, &notationTab }) place (*b);
        bar.removeFromLeft (theme::px (12));
        for (auto* b : { &gridButton, &snapButton, &quantise, &legato, &selectButton }) place (*b);
        if (ccLanes.isVisible())
        {
            bar.removeFromLeft (theme::px (12));
            place (ccLanes);
            place (autoCc);
        }
    }
    for (auto* comp : std::initializer_list<Component*> { &devices, &roll, &drumsView, &steps, &euclidView, &notation, &mixer, &launcher, &offline, &meters, &videoView })
        comp->setBounds (r);
    sample.setBounds (r);
    // These two scroll when there's more than fits.
    const int inner = r.getWidth() - drumsView.getScrollBarThickness();
    drums.setSize (inner, jmax (r.getHeight(), drums.preferredHeight()));
    euclid.setSize (inner, jmax (r.getHeight(), euclid.preferredHeight()));
}

} // namespace kal
