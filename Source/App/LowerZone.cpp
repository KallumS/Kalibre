#include "LowerZone.h"

namespace kal
{
using namespace juce;

LowerZone::LowerZone (Controller& ctrl)
    : c (ctrl), devices (ctrl), roll (ctrl), drums (ctrl), steps (ctrl), euclid (ctrl), notation (ctrl), mixer (ctrl), sample (ctrl)
{
    for (auto* b : { &devicesTab, &editorTab, &mixerTab })
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
    for (auto* b : { &gridButton, &snapButton, &quantise, &legato }) addChildComponent (b);
    for (auto* comp : std::initializer_list<Component*> { &devices, &roll, &drumsView, &steps, &euclidView, &notation, &mixer, &sample })
        addChildComponent (comp);
    drumsView.setViewedComponent (&drums, false);
    euclidView.setViewedComponent (&euclid, false);
    for (auto* v : { &drumsView, &euclidView }) v->setScrollBarsShown (true, false);

    devicesTab.onClick = [this] { c.zoneTab = ZoneTab::Devices; show(); };
    editorTab.onClick = [this] { c.zoneTab = ZoneTab::Editor; show(); };
    mixerTab.onClick = [this] { c.zoneTab = ZoneTab::Mixer; show(); };
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
    if (c.zoneTab != shownTab || c.editor != shownEditor || audioChosen() != shownAudio) show();
    snapButton.setToggleState (c.snap, dontSendNotification);
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
    // An audio clip is edited in the sample editor; the note editors are for MIDI.
    const bool audio = c.zoneTab == ZoneTab::Editor && shownAudio;
    const bool editing = c.zoneTab == ZoneTab::Editor && ! audio;
    for (auto* b : { &rollTab, &drumsTab, &stepsTab, &euclidTab, &notationTab, &gridButton, &snapButton, &quantise, &legato })
        b->setVisible (editing);
    sample.setVisible (audio);
    if (audio) sample.load();
    rollTab.setToggleState (c.editor == EditorKind::PianoRoll, dontSendNotification);
    drumsTab.setToggleState (c.editor == EditorKind::Drums, dontSendNotification);
    stepsTab.setToggleState (c.editor == EditorKind::Steps, dontSendNotification);
    euclidTab.setToggleState (c.editor == EditorKind::Euclid, dontSendNotification);
    notationTab.setToggleState (c.editor == EditorKind::Notation, dontSendNotification);
    snapButton.setToggleState (c.snap, dontSendNotification);

    devices.setVisible (c.zoneTab == ZoneTab::Devices);
    mixer.setVisible (c.zoneTab == ZoneTab::Mixer);
    roll.setVisible (editing && c.editor == EditorKind::PianoRoll);
    drumsView.setVisible (editing && c.editor == EditorKind::Drums);
    steps.setVisible (editing && c.editor == EditorKind::Steps);
    euclidView.setVisible (editing && c.editor == EditorKind::Euclid);
    notation.setVisible (editing && c.editor == EditorKind::Notation);

    const char* names[] { "1/1", "1/2", "1/4", "1/8", "1/16", "1/32" };
    const Tick values[] { PPQ * 4, PPQ * 2, PPQ, PPQ / 2, PPQ / 4, PPQ / 8 };
    String label = String (c.grid) + " ticks";
    for (int i = 0; i < 6; ++i)
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
    const char* names[] { "1/1", "1/2", "1/4", "1/8", "1/16", "1/32" };
    const Tick values[] { PPQ * 4, PPQ * 2, PPQ, PPQ / 2, PPQ / 4, PPQ / 8 };
    for (int i = 0; i < 6; ++i)
        m.addItem (names[i], true, c.grid == values[i], [this, v = values[i]] { c.grid = v; show(); c.viewChanged(); });
    m.addSeparator();
    for (int i = 1; i < 6; ++i)
        m.addItem (String (names[i]) + " Triplet", true, c.grid == values[i] * 2 / 3, [this, v = values[i] * 2 / 3] { c.grid = v; show(); c.viewChanged(); });
    m.showMenuAsync (PopupMenu::Options().withTargetComponent (&gridButton));
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
    bar.removeFromLeft (theme::px (12));
    if (c.zoneTab == ZoneTab::Editor && ! shownAudio)
    {
        for (auto* b : { &rollTab, &drumsTab, &stepsTab, &euclidTab, &notationTab }) place (*b);
        bar.removeFromLeft (theme::px (12));
        for (auto* b : { &gridButton, &snapButton, &quantise, &legato }) place (*b);
    }
    for (auto* comp : std::initializer_list<Component*> { &devices, &roll, &drumsView, &steps, &euclidView, &notation, &mixer })
        comp->setBounds (r);
    sample.setBounds (r);
    // These two scroll when there's more than fits.
    const int inner = r.getWidth() - drumsView.getScrollBarThickness();
    drums.setSize (inner, jmax (r.getHeight(), drums.preferredHeight()));
    euclid.setSize (inner, jmax (r.getHeight(), euclid.preferredHeight()));
}

} // namespace kal
