#include "DeviceRack.h"
#include "NoteSplitterEditor.h"

#include "Modulation.h"
#include "BuiltinInstruments.h"
#include "HardwareInsert.h"
#include "MorphProcessor.h"
#include "ContainerProcessor.h"
#include "Editors.h"
#include "JsfxDevice.h"
#include "LuaTool/LuaTool.h"
#include "PluginClip.h"

namespace kal
{
using namespace juce;

//==============================================================================
// A plug-in's window in a window of its own, moved out of the rack on request
// (decision 0056). Closing it puts it back in the rack.
class PluginWindow : public DocumentWindow
{
public:
    PluginWindow (const String& title, AudioProcessorEditor* editor, std::function<void()> closed)
        : DocumentWindow (title, theme::ground, DocumentWindow::closeButton), onClosed (std::move (closed))
    {
        setUsingNativeTitleBar (true);
        setContentNonOwned (editor, true);
        content.reset (editor);
        setResizable (editor->isResizable(), false);
        if (auto* k = editor->getConstrainer()) setConstrainer (k);
        centreWithSize (getWidth(), getHeight());
        setVisible (true);
        toFront (true);
    }
    ~PluginWindow() override { clearContentComponent(); content.reset(); }   // the plug-in is told its window is going
    void closeButtonPressed() override { MessageManager::callAsync ([f = onClosed] { if (f) f(); }); }

private:
    editors::Owned content;
    std::function<void()> onClosed;
};

//==============================================================================
class DevicePanel : public Component, private ComponentListener
{
public:
    DevicePanel (Controller& ctrl, Id dev, DeviceRack& r) : c (ctrl), id (dev), rack (r)
    {
        for (auto* b : { &power, &bypass, &menu, &ab, &presets, &window, &match }) addAndMakeVisible (b);
        ab.onClick = [this]
        {
            if (ModifierKeys::currentModifiers.isPopupMenu()) return;
            c.switchAB (id);
        };
        presets.onClick = [this] { presetMenu(); };
        power.setTitle ("Plug-in On");
        bypass.setTitle ("Bypass");
        menu.setTitle ("Plug-in Menu");
        window.setTitle ("Its Own Window");
        match.setTitle ("Match Loudness");
        power.onClick = [this] { c.setDeviceActive (id, ! device()->active); };
        bypass.onClick = [this] { c.apply ("Bypass", [this] (Project& p) { if (auto* d = p.device (id)) d->bypassed = ! d->bypassed; }); };
        match.onClick = [this] { c.apply ("Match Loudness", [this] (Project& p) { if (auto* d = p.device (id)) d->matchLoudness = ! d->matchLoudness; }); };
        window.onClick = [this] { if (rack.isFloating (id)) rack.dock (id); else rack.float_ (id); };
        menu.onClick = [this] { showMenu(); };
        addChildComponent (clip);
        clip.setContent (&holder);
        update();
    }

    ~DevicePanel() override { dropEditor(); }

    Device* device() { return c.project.device (id); }

    void dropEditor()
    {
        if (editor) editor->removeComponentListener (this);
        editor.reset();
        editorFor = nullptr;
        natural = {};
        ownSize = false;
        fittedBox = {};
    }

    void update()
    {
        auto* d = device();
        if (! d) return;
        power.setToggleState (d->active, dontSendNotification);
        bypass.setToggleState (d->bypassed, dontSendNotification);
        match.setToggleState (d->matchLoudness, dontSendNotification);
        // Effects only; the Container has its own, for everything in it.
        const bool matchShown = match.isVisible();
        match.setVisible (d->role == DeviceRole::AudioFx && d->plugin.identifier != "container");
        if (match.isVisible() != matchShown) resized();
        ab.setButtonText (d->onB ? "Set B" : "Set A");
        ab.setTitle (d->onB ? "Settings B" : "Settings A");
        const bool floating = rack.isFloating (id);
        window.setToggleState (floating, dontSendNotification);
        window.setTip (floating ? "Put its window back here, in the rack." : "Move its window out of the rack, into a window of its own.");
        auto* inst = c.engine.instance (id);
        // Moved out to its own window, or not loaded: nothing here.
        if (! d->active || ! inst || floating) { if (editor) { dropEditor(); rack.relayout(); } return; }
        if (! editor || editorFor != inst)
        {
            dropEditor();
            editorFor = inst;
            editor.reset (editors::open (*inst));
            natural = editor->getBounds().withZeroOrigin();
            fittedBox = {};
            holder.addAndMakeVisible (*editor);
            editor->addComponentListener (this);
            rack.relayout();
        }
    }

    void tickMatch()
    {
        auto* d = device();
        if (! d || ! d->matchLoudness) return;
        const float db = c.engine.matchingDb (id);
        match.setTip ("Match Loudness: keeping what comes out as loud as what goes in (short-term, LUFS-S): "
                      + String (db >= 0 ? "+" : "") + String (db, 1) + " dB now.");
    }

    // The width this panel takes at a height: the plug-in as shown, fitted.
    int widthFor (int height)
    {
        const int least = theme::px (330);
        if (! editor || natural.isEmpty()) return least;
        return jmax (least, fitInto (boxFor (height)).shownWidth() + 4);
    }

    // The room a plug-in has in a panel this tall: the zone's height under the
    // header, and the rack's width, so a single wide one fits too.
    Rectangle<int> boxFor (int height) const
    {
        return { 0, 0, jmax (1, rack.fitWidth() - 4), jmax (1, height - theme::px (30) - 4) };
    }

    // A plug-in's window fitted into a box, its shape kept (decision 0058): one
    // that can be resized is asked to take the box's size, drawn by itself, so
    // it stays sharp; whatever's still too big, or can't be resized, is shown
    // smaller, the same both ways. Never stretched, and never shown bigger
    // than it is (a picture made bigger blurs).
    struct Fit
    {
        Rectangle<int> editor;   // the size the plug-in's window is, in its own units
        float scale = 1.0f;      // and how much it's shown at
        int shownWidth() const { return roundToInt ((float) editor.getWidth() * scale); }
        int shownHeight() const { return roundToInt ((float) editor.getHeight() * scale); }
    };

    Fit fitInto (Rectangle<int> box)
    {
        if (! editor || natural.isEmpty() || box.isEmpty()) return {};
        if (box == fittedBox) return fitted;
        auto size = natural;
        laying = true;
        if (editor->isResizable() && ! ownSize)
        {
            const float s = jmin ((float) box.getWidth() / (float) natural.getWidth(), (float) box.getHeight() / (float) natural.getHeight());
            Rectangle<int> want (0, 0, jmax (1, roundToInt ((float) natural.getWidth() * s)), jmax (1, roundToInt ((float) natural.getHeight() * s)));
            if (auto* k = editor->getConstrainer())
                k->checkBounds (want, editor->getBounds(), Rectangle<int> (0, 0, 100000, 100000), false, false, true, true);
            editor->setBounds (want);
            size = editor->getBounds().withZeroOrigin();   // what it took: some keep to sizes of their own
        }
        else if (editor->getBounds().withZeroOrigin() != natural) editor->setBounds (natural);
        laying = false;
        editor->setTopLeftPosition (0, 0);
        if (size.isEmpty()) size = natural;
        fitted = { size, jmin (1.0f, (float) box.getWidth() / (float) size.getWidth(), (float) box.getHeight() / (float) size.getHeight()) };
        fittedBox = box;
        return fitted;
    }

    // Where the plug-in's window is drawn: centred in the panel, and only the
    // part inside the rack's view (on the Mac nothing else would hide the rest).
    void placeEditor()
    {
        if (! editor || natural.isEmpty()) { clip.setVisible (false); return; }
        const Point<int> at (body.getX() + (body.getWidth() - fitted.shownWidth()) / 2,
                             body.getY() + (body.getHeight() - fitted.shownHeight()) / 2);
        const auto shown = body.getIntersection (rack.shownArea (*this));
        clip.setVisible (! shown.isEmpty());
        clip.setBounds (shown);
        clip.setContentPlacement (at - shown.getPosition(), fitted.scale);
    }

    void paint (Graphics& g) override
    {
        auto* d = device();
        g.setColour (theme::sunken);
        g.fillRoundedRectangle (getLocalBounds().toFloat(), 6.0f);
        auto header = getLocalBounds().removeFromTop (theme::px (30)).reduced (theme::px (8), 0);
        header.setRight (presets.getX() - theme::px (6));
        g.setColour (d && d->active ? theme::text : theme::textSecondary);
        g.setFont (theme::font (theme::bodyPoints, true));
        if (d)
        {
            const char* roles[] { "MIDI effect", "Instrument", "Effect" };
            g.drawFittedText (String (d->plugin.name), header.removeFromTop (header.getHeight() * 3 / 5), Justification::bottomLeft, 1);
            g.setColour (theme::textSecondary);
            g.setFont (theme::font (theme::smallPoints));
            String sub = String (roles[(int) d->role]) + " " + ui::u8 ("\xc2\xb7") + " " + String (d->plugin.format);
            if (editor && fitted.scale < 0.995f) sub << " " << ui::u8 ("\xc2\xb7") << " " << roundToInt (fitted.scale * 100.0f) << "% to fit";
            if (const auto* from = c.project.track (d->sidechainFrom)) sub << " " << ui::u8 ("\xc2\xb7") << " sidechain: " << String (from->name);
            g.drawFittedText (sub, header, Justification::topLeft, 1);
            const auto body = getLocalBounds().withTrimmedTop (theme::px (30)).reduced (12);
            if (! d->active)
            {
                g.setColour (theme::textSecondary);
                g.setFont (theme::font());
                g.drawFittedText ("Off: unloaded, settings kept. Switch it on to load it again.", body, Justification::centred, 4);
            }
            else if (! c.engine.instance (id))
            {
                g.setColour (theme::warnText);
                g.setFont (theme::font());
                g.drawFittedText ("This plug-in isn't installed, or didn't load. Its settings are kept.", body, Justification::centred, 4);
            }
            else if (rack.isFloating (id))
            {
                g.setColour (theme::textSecondary);
                g.setFont (theme::font());
                g.drawFittedText ("In a window of its own. Close that window, or click the window button above, to bring it back.", body, Justification::centred, 4);
            }
        }
    }

    void resized() override
    {
        auto header = getLocalBounds().removeFromTop (theme::px (30)).reduced (theme::px (6), theme::px (4));
        menu.setBounds (header.removeFromRight (theme::px (26)));
        header.removeFromRight (3);
        bypass.setBounds (header.removeFromRight (theme::px (30)));
        header.removeFromRight (3);
        power.setBounds (header.removeFromRight (theme::px (30)));
        header.removeFromRight (3);
        window.setBounds (header.removeFromRight (theme::px (30)));
        header.removeFromRight (6);
        if (match.isVisible()) { match.setBounds (header.removeFromRight (match.idealWidth (theme::smallControl))); header.removeFromRight (3); }
        ab.setBounds (header.removeFromRight (ab.idealWidth (theme::smallControl)));
        header.removeFromRight (3);
        presets.setBounds (header.removeFromRight (presets.idealWidth (theme::smallControl)));

        body = getLocalBounds().withTrimmedTop (theme::px (30)).reduced (2);
        if (! editor || natural.isEmpty()) { clip.setVisible (false); return; }
        const auto f = fitInto (boxFor (getHeight()));
        holder.setSize (f.editor.getWidth(), f.editor.getHeight());
        placeEditor();
        repaint();
        // A plug-in that took another size than expected needs another width.
        if (widthFor (getHeight()) != getWidth()) rack.relayout();
    }

    // Presets (decision 0039): saved ones, then the plug-in's own programs.
    void presetMenu()
    {
        auto* d = device();
        if (! d) return;
        auto* inst = c.engine.instance (id);
        PopupMenu m;
        m.addItem (ui::u8 ("Save Preset\xe2\x80\xa6"), inst != nullptr, false, [this]
        {
            auto* w = new AlertWindow ("Save a Preset", "A name for the plug-in's settings as they are now.", MessageBoxIconType::NoIcon);
            w->addTextEditor ("name", "My Preset");
            w->addButton ("Save", 1, KeyPress (KeyPress::returnKey));
            w->addButton ("Cancel", 0, KeyPress (KeyPress::escapeKey));
            ui::present (w, ModalCallbackFunction::create ([this, w] (int ok)
            {
                if (ok == 1 && w->getTextEditorContents ("name").trim().isNotEmpty()) c.savePreset (id, w->getTextEditorContents ("name").trim());
            }), true);
        });
        m.addSeparator();
        const auto files = c.presetsFor (d->plugin);
        if (files.isEmpty()) m.addSectionHeader ("No Saved Presets Yet");
        else m.addSectionHeader ("Saved Presets");
        for (auto& f : files)
            m.addItem (f.getFileNameWithoutExtension(), inst != nullptr, false, [this, f] { c.loadPreset (id, f); });
        if (inst && inst->getNumPrograms() > 1)
        {
            PopupMenu programs;
            for (int i = 0; i < inst->getNumPrograms() && i < 512; ++i)
            {
                const auto name = inst->getProgramName (i);
                programs.addItem (name.isEmpty() ? "Program " + String (i + 1) : name, true, inst->getCurrentProgram() == i, [this, i] { c.loadProgram (id, i); });
            }
            m.addSubMenu ("The Plug-in's Own", programs);
        }
        m.addSeparator();
        m.addItem (d->onB ? "Copy B to A" : "Copy A to B", inst != nullptr, false, [this] { c.copyToOther (id); });
        m.showMenuAsync (PopupMenu::Options().withTargetComponent (&presets));
    }

    void showMenu()
    {
        auto* d = device();
        if (! d) return;
        Track* owner = nullptr;
        c.project.device (id, &owner);
        PopupMenu m;
        const int index = owner ? (int) (std::find_if (owner->devices.begin(), owner->devices.end(), [this] (const Device& x) { return x.id == id; }) - owner->devices.begin()) : 0;
        m.addItem ("Move Left", index > 0, false, [this, index] { c.apply ("Move Plug-in", [this, index] (Project& p) { edit::moveDevice (p, id, index - 1); }); });
        m.addItem ("Move Right", owner && index + 1 < (int) owner->devices.size(), false, [this, index] { c.apply ("Move Plug-in", [this, index] (Project& p) { edit::moveDevice (p, id, index + 1); }); });
        m.addSeparator();
        // Sidechain (decision 0031): only for plug-ins with a second input.
        if (c.engine.sidechainChannels (id) > 0 && owner)
        {
            PopupMenu sc;
            sc.addItem ("None", true, d->sidechainFrom == 0, [this] { c.apply ("Sidechain", [this] (Project& p) { if (auto* x = p.device (id)) x->sidechainFrom = 0; }); });
            for (auto& t : c.project.tracks)
                if (t.id != owner->id)
                    sc.addItem (String (t.name), edit::canRoute (c.project, t.id, owner->id), d->sidechainFrom == t.id, [this, tid = t.id]
                    {
                        c.apply ("Sidechain", [this, tid] (Project& p) { if (auto* x = p.device (id)) x->sidechainFrom = tid; });
                    });
            m.addSubMenu ("Sidechain From", sc);
            m.addSeparator();
        }
        // An instrument's further outputs, as tracks of their own (decision 0040).
        if (d->role == DeviceRole::Instrument && owner)
            if (auto* inst = c.engine.instance (id); inst && inst->getTotalNumOutputChannels() > 2)
            {
                const int pairs = inst->getTotalNumOutputChannels() / 2;
                int made = 0;
                for (auto& t : c.project.tracks) if (t.sourceDevice == id) ++made;
                m.addItem ("Its Other Outputs as Tracks (" + String (pairs - 1) + ")", made == 0, false, [this, pairs, ownerId = owner->id]
                {
                    const String name (device()->plugin.name);
                    c.apply ("Instrument Outputs", [this, pairs, ownerId, name] (Project& p)
                    {
                        Id after = ownerId;
                        const auto* o = p.track (ownerId);
                        const Id parent = o ? o->parent : 0;
                        for (int k = 1; k < pairs; ++k)
                        {
                            const Id t = edit::addTrack (p, TrackKind::Track, (name + " " + String (k * 2 + 1) + "-" + String (k * 2 + 2)).toStdString(), after, parent);
                            if (auto* tr = p.track (t)) { tr->sourceDevice = id; tr->sourcePair = k; if (o) tr->colour = o->colour; }
                            after = t;
                        }
                    });
                    c.say ("Each of the instrument's outputs now has a track: mix and process them separately.", false, true);
                });
                m.addSeparator();
            }
        // Midular and MIDI Production Center take a phrase from a MIDI clip (decision 0093).
        if (d->plugin.identifier == "jsfx:midular" || d->plugin.identifier == "jsfx:midi-production-center")
        {
            m.addItem ("Load the Chosen MIDI Clip", [this] { c.loadChosenClipsInto (id); });
            m.addSeparator();
        }
        m.addItem ("Automate the Last Touched Control", c.lastTouched.device == id, false, [this] { c.automateLastTouched(); });
        PopupMenu mods;
        const char* kinds[] { "LFO", "ADSR", "ADR", "Envelope Follower", "Wavetable" };
        for (int k = 0; k < 5; ++k) mods.addItem (kinds[k], c.lastTouched.device == id, false, [this, k] { c.modulateLastTouched ((ModKind) k); });
        m.addSubMenu ("Modulate the Last Touched Control", mods);
        m.addSeparator();
        m.addItem (d->active ? "Deactivate (Unload)" : "Activate", [this] { c.setDeviceActive (id, ! device()->active); });
        m.addItem ("Remove", [this] { c.apply ("Remove Plug-in", [this] (Project& p) { edit::removeDevice (p, id); }); });
        m.showMenuAsync (PopupMenu::Options().withTargetComponent (&menu));
    }

    Controller& c;
    Id id;
    theme::HouseButton power { ui::u8 ("\xe2\x8f\xbb"), "Switch the plug-in off (unloaded, settings kept) or on." };
    theme::HouseButton bypass { "B", "Bypass: let the sound through untouched, still loaded." };
    theme::HouseButton menu { ui::u8 ("\xe2\x80\xa6"), "Move, remove, automate or modulate." };
    theme::HouseButton ab { "Set A", "Compare two settings: click to switch between A and B. Copy one to the other in Presets." };
    theme::HouseButton presets { "Presets", "Save the settings as a preset, or load one." };
    theme::HouseButton window { ui::u8 ("\xe2\xa7\x89"), "Move its window out of the rack, into a window of its own.", true };
    theme::HouseButton match { "Match", "Match Loudness: keep what comes out as loud as what goes in, by short-term loudness (LUFS-S, K-weighted).", true };
    DeviceRack& rack;
    Component holder;   // the plug-in's window, at (0, 0); before the clip, which shows it
    PluginClip clip;
    editors::Owned editor;
    AudioPluginInstance* editorFor = nullptr;
    Rectangle<int> natural;   // the size the plug-in's window asks for
    Rectangle<int> body;      // where it's shown, under the header
    Rectangle<int> fittedBox;
    Fit fitted;
    bool ownSize = false;     // it chose a size itself: kept, only shown smaller
    bool laying = false;

    // The plug-in resized its own window (a bigger view, a panel opened, its
    // corner dragged): that's its size now, shown smaller if it must be.
    void componentMovedOrResized (Component& comp, bool, bool wasResized) override
    {
        if (laying || ! wasResized || &comp != editor.get()) return;
        natural = editor->getBounds().withZeroOrigin();
        ownSize = true;
        fittedBox = {};
        rack.relayout();
    }
};

//==============================================================================
class ModulatorPanel : public Component
{
public:
    ModulatorPanel (Controller& ctrl, Id track, Id mod) : c (ctrl), trackId (track), id (mod)
    {
        auto knob = [this] (Slider& s, const String& name, double lo, double hi, double step)
        {
            s.setSliderStyle (Slider::RotaryHorizontalVerticalDrag);
            s.setTextBoxStyle (Slider::TextBoxBelow, false, theme::px (48), theme::px (14));
            s.setRange (lo, hi, step);
            s.setTitle (name);
            s.setTooltip (name);
            s.onDragStart = [this] { before = c.project; };
            s.onValueChange = [this] { push(); };
            s.onDragEnd = [this] { if (! (before == c.project)) c.history.push (before, "Modulator"); };
            addAndMakeVisible (s);
        };
        knob (a, "Rate or attack", 0.0, 1.0, 0.001);
        knob (b, "Decay or release", 0.0, 1.0, 0.001);
        knob (s3, "Sustain or gain", 0.0, 1.0, 0.001);
        knob (amount, "Amount", -1.0, 1.0, 0.01);
        addAndMakeVisible (shape);
        addAndMakeVisible (remove);
        remove.onClick = [this]
        {
            c.apply ("Remove Modulator", [this] (Project& p)
            {
                if (auto* t = p.track (trackId))
                    t->modulators.erase (std::remove_if (t->modulators.begin(), t->modulators.end(), [this] (const Modulator& m) { return m.id == id; }), t->modulators.end());
            });
        };
        shape.onClick = [this] { shapeMenu(); };
        pull();
    }

    Modulator* mod()
    {
        if (auto* t = c.project.track (trackId))
            for (auto& m : t->modulators) if (m.id == id) return &m;
        return nullptr;
    }

    void pull()
    {
        auto* m = mod();
        if (! m) return;
        switch (m->kind)
        {
            case ModKind::Lfo:
            case ModKind::Wavetable:
                a.setValue (m->sync ? jmap (std::log2 (m->syncBeats), -3.0, 4.0, 0.0, 1.0) : jmap (std::log2 (m->rateHz), -4.0, 5.0, 0.0, 1.0), dontSendNotification);
                b.setValue (m->phase, dontSendNotification);
                s3.setValue (m->bipolar ? 1.0 : 0.0, dontSendNotification);
                break;
            case ModKind::Adsr:
            case ModKind::Adr:
                a.setValue (std::sqrt (m->attack / 2.0), dontSendNotification);
                b.setValue (std::sqrt (m->decay / 4.0), dontSendNotification);
                s3.setValue (m->sustain, dontSendNotification);
                break;
            case ModKind::EnvelopeFollower:
                a.setValue (std::sqrt (m->followAttack / 0.5), dontSendNotification);
                b.setValue (std::sqrt (m->followRelease / 2.0), dontSendNotification);
                s3.setValue (m->followGain / 4.0, dontSendNotification);
                break;
        }
        if (! m->targets.empty()) amount.setValue (m->targets.front().amount, dontSendNotification);
        static const char* shapes[] { "Sine", "Triangle", "Saw Up", "Saw Down", "Square", "Random" };
        shape.setButtonText (m->kind == ModKind::Lfo ? String (shapes[(int) m->shape])
                           : m->kind == ModKind::Wavetable ? String ("Table") : String ("Source"));
        shape.setVisible (m->kind == ModKind::Lfo || m->kind == ModKind::Wavetable || m->kind == ModKind::EnvelopeFollower);
    }

    void push()
    {
        auto* m = mod();
        if (! m) return;
        switch (m->kind)
        {
            case ModKind::Lfo:
            case ModKind::Wavetable:
                if (m->sync) m->syncBeats = std::pow (2.0, std::round (jmap (a.getValue(), -3.0, 4.0)));
                else m->rateHz = std::pow (2.0, jmap (a.getValue(), -4.0, 5.0));
                m->phase = b.getValue();
                m->bipolar = s3.getValue() > 0.5;
                break;
            case ModKind::Adsr:
            case ModKind::Adr:
                m->attack = jmax (0.001, a.getValue() * a.getValue() * 2.0);
                m->decay = jmax (0.001, b.getValue() * b.getValue() * 4.0);
                m->sustain = s3.getValue();
                break;
            case ModKind::EnvelopeFollower:
                m->followAttack = jmax (0.001, a.getValue() * a.getValue() * 0.5);
                m->followRelease = jmax (0.001, b.getValue() * b.getValue() * 2.0);
                m->followGain = s3.getValue() * 4.0;
                break;
        }
        for (auto& t : m->targets) t.amount = amount.getValue();
        c.refresh();
        repaint();
    }

    void shapeMenu()
    {
        auto* m = mod();
        if (! m) return;
        PopupMenu menu;
        if (m->kind == ModKind::Lfo)
        {
            const char* shapes[] { "Sine", "Triangle", "Saw Up", "Saw Down", "Square", "Random (Sample and Hold)" };
            for (int i = 0; i < 6; ++i)
                menu.addItem (shapes[i], true, (int) m->shape == i, [this, i] { c.apply ("LFO Shape", [this, i] (Project&) { if (auto* x = mod()) x->shape = (LfoShape) i; }); pull(); });
            menu.addItem ("Sync to the Tempo", true, m->sync, [this] { c.apply ("LFO Sync", [this] (Project&) { if (auto* x = mod()) x->sync = ! x->sync; }); pull(); });
        }
        if (m->kind == ModKind::Wavetable)
        {
            menu.addItem (ui::u8 ("Load a Sample as the Shape\xe2\x80\xa6"), [this]
            {
                chooser = std::make_unique<FileChooser> ("Choose a sample", File(), "*.wav;*.aif;*.aiff;*.flac;*.mp3");
                chooser->launchAsync (FileBrowserComponent::openMode | FileBrowserComponent::canSelectFiles, [this] (const FileChooser& fc)
                {
                    const auto f = fc.getResult();
                    if (! f.existsAsFile()) return;
                    AudioFormatManager fm;
                    fm.registerBasicFormats();
                    std::unique_ptr<AudioFormatReader> r (fm.createReaderFor (f));
                    if (! r) return;
                    AudioBuffer<float> buf (1, (int) jmin<int64> (r->lengthInSamples, (int64) r->sampleRate * 30));
                    r->read (&buf, 0, buf.getNumSamples(), 0, true, false);
                    auto table = modulation::tableFromSamples (buf.getReadPointer (0), buf.getNumSamples());
                    c.apply ("Wavetable from Sample", [this, table, f] (Project&) { if (auto* x = mod()) { x->table = table; x->tableFile = f.getFullPathName().toStdString(); } });
                });
            });
            menu.addItem ("Sync to the Tempo", true, m->sync, [this] { c.apply ("Sync", [this] (Project&) { if (auto* x = mod()) x->sync = ! x->sync; }); pull(); });
        }
        if (m->kind == ModKind::EnvelopeFollower)
        {
            menu.addItem ("This Track", true, m->sourceTrack == 0, [this] { c.apply ("Follow", [this] (Project&) { if (auto* x = mod()) x->sourceTrack = 0; }); });
            for (auto& t : c.project.tracks)
                if (t.id != trackId)
                    menu.addItem (String (t.name), true, m->sourceTrack == t.id, [this, tid = t.id] { c.apply ("Follow", [this, tid] (Project&) { if (auto* x = mod()) x->sourceTrack = tid; }); });
        }
        menu.showMenuAsync (PopupMenu::Options().withTargetComponent (&shape));
    }

    void paint (Graphics& g) override
    {
        g.setColour (theme::raised);
        g.fillRoundedRectangle (getLocalBounds().toFloat(), 6.0f);
        auto* m = mod();
        if (! m) return;
        auto r = getLocalBounds().reduced (8, 4);
        g.setColour (theme::text);
        g.setFont (theme::font (theme::bodyPoints, true));
        g.drawText (String (m->name), r.removeFromTop (theme::px (18)), Justification::centredLeft, true);
        g.setColour (theme::textSecondary);
        g.setFont (theme::font (theme::smallPoints));
        String targets;
        for (auto& t : m->targets) targets << (targets.isEmpty() ? "" : ", ") << String (t.paramName);
        g.drawFittedText (ui::u8 ("\xe2\x86\x92 ") + targets, r.removeFromTop (theme::px (14)), Justification::centredLeft, 1);
        // Its shape, drawn.
        auto pic = r.removeFromTop (theme::px (34)).toFloat();
        g.setColour (theme::sunken);
        g.fillRoundedRectangle (pic, 3.0f);
        Path p;
        for (int i = 0; i <= 64; ++i)
        {
            const double ph = i / 64.0;
            double v = 0;
            if (m->kind == ModKind::Lfo) v = modulation::lfoShape (m->shape, ph + m->phase, std::sin (ph * 40.0));
            else if (m->kind == ModKind::Wavetable) v = modulation::tableAt (m->table, ph + m->phase);
            else if (m->kind == ModKind::Adsr || m->kind == ModKind::Adr)
            {
                const double t = ph * 2.0, at = jmax (0.01, m->attack), dt = jmax (0.01, m->decay);
                const double sus = m->kind == ModKind::Adsr ? m->sustain : 0.0;
                v = t < at ? t / at : t < at + dt ? 1.0 - (1.0 - sus) * (t - at) / dt : (m->kind == ModKind::Adsr && t < 1.5 ? sus : jmax (0.0, sus - (t - 1.5) * 2.0));
                v = v * 2.0 - 1.0;
            }
            else v = std::sin (ph * 12.0) * std::exp (-ph * 2.0);
            const float x = pic.getX() + pic.getWidth() * (float) ph;
            const float y = pic.getCentreY() - (float) v * pic.getHeight() * 0.42f;
            if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
        }
        g.setColour (theme::control);
        g.strokePath (p, PathStrokeType (1.5f));
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (6, 4);
        auto top = r.removeFromTop (theme::px (18));
        remove.setBounds (top.removeFromRight (theme::px (22)));
        r.removeFromTop (theme::px (14) + theme::px (34) + 2);
        shape.setBounds (r.removeFromTop (theme::px (22)).reduced (0, 1));
        const int w = r.getWidth() / 4;
        for (auto* k : { &a, &b, &s3, &amount }) k->setBounds (r.removeFromLeft (w));
    }

    Controller& c;
    Id trackId, id;
    Slider a, b, s3, amount;
    theme::HouseButton shape { "Shape", "Its shape, or what it follows." };
    theme::HouseButton remove { ui::u8 ("\xc3\x97"), "Remove this modulator." };
    std::unique_ptr<FileChooser> chooser;
    Project before;
};

//==============================================================================
DeviceRack::DeviceRack (Controller& ctrl) : c (ctrl)
{
    setTitle ("Devices");
    addAndMakeVisible (viewport);
    viewport.setViewedComponent (&strip, false);
    viewport.setScrollBarsShown (false, true);
    viewport.moved = [this] { placeEditors(); };
    // Two fingers sideways move the rack, over the plug-ins' windows too.
    swipes = watchSidewaysSwipes (viewport, [this] (float dx)
    {
        if (strip.getWidth() <= viewport.getViewWidth()) return false;
        viewport.setViewPosition (jlimit (0, strip.getWidth() - viewport.getViewWidth(), viewport.getViewPositionX() - roundToInt (dx)), 0);
        return true;
    });
    strip.addAndMakeVisible (add);
    add.onClick = [this] { addMenu (c, c.focusTrack, &add); };
    MorphProcessor::choosePlugin = [this] (std::function<void (const PluginRef&)> chosen)
    {
        PopupMenu m;
        for (auto& d : c.engine.plugins().known().getTypes())
            m.addItem (d.name + " (" + d.pluginFormatName + ")", [chosen, d] { chosen (PluginManager::refFor (d)); });
        if (m.getNumItems() == 0) m.addItem ("Scan for plug-ins first (Kalibre > Plug-in Manager)", false, false, [] {});
        m.showMenuAsync (PopupMenu::Options());
    };
    // The Container takes audio effects (decision 0055).
    ContainerProcessor::choosePlugin = [this] (std::function<void (const PluginRef&)> chosen)
    {
        PopupMenu m;
        std::map<String, PopupMenu> byMaker;
        for (auto& d : c.engine.plugins().known().getTypes())
            if (c.engine.plugins().roleOf (d) == DeviceRole::AudioFx)
                byMaker[d.manufacturerName.isEmpty() ? String ("Other") : d.manufacturerName]
                    .addItem (d.name + "  (" + d.pluginFormatName + ")", [chosen, d] { chosen (PluginManager::refFor (d)); });
        for (auto& [maker, sub] : byMaker) m.addSubMenu (maker, sub);
        if (m.getNumItems() == 0) m.addItem ("Scan for plug-ins first (Kalibre > Plug-in Manager)", false, false, [] {});
        m.showMenuAsync (PopupMenu::Options());
    };
    NoteSplitterProcessor::makeEditor = [this] (NoteSplitterProcessor& p) -> AudioProcessorEditor* { return new NoteSplitterEditor (p, c); };
    c.addChangeListener (this);
    rebuild();
    startTimerHz (2);
}

void DeviceRack::timerCallback()
{
    for (auto* p : panels) p->tickMatch();
}

void DeviceRack::float_ (Id device)
{
    auto* inst = c.engine.instance (device);
    const auto* d = c.project.device (device);
    if (! inst || ! d || windows.count (device)) return;
    // Its editor leaves the rack first: a plug-in has one editor at a time.
    for (auto* p : panels) if (p->id == device) p->dropEditor();
    auto* e = editors::open (*inst);
    Track* owner = nullptr;
    c.project.device (device, &owner);
    const String title = String (d->plugin.name) + (owner ? ui::u8 (" \xe2\x80\x94 ") + String (owner->name) : String());
    windows[device] = std::make_unique<PluginWindow> (title, e, [this, device] { dock (device); });
    windowFor[device] = inst;
    for (auto* p : panels) if (p->id == device) { p->update(); p->repaint(); }
    layoutStrip();
}

void DeviceRack::dock (Id device)
{
    windows.erase (device);
    windowFor.erase (device);
    for (auto* p : panels) if (p->id == device) { p->update(); p->repaint(); }
    layoutStrip();
}

// A window whose plug-in was removed, switched off or replaced closes with it.
void DeviceRack::closeStaleWindows()
{
    for (auto it = windows.begin(); it != windows.end();)
    {
        const auto* d = c.project.device (it->first);
        if (d && d->active && c.engine.instance (it->first) == windowFor[it->first]) { ++it; continue; }
        windowFor.erase (it->first);
        it = windows.erase (it);
    }
}

DeviceRack::~DeviceRack()
{
    stopTimer();
    swipes.reset();
    windows.clear();   // their editors go before the plug-ins
    MorphProcessor::choosePlugin = nullptr;
    ContainerProcessor::choosePlugin = nullptr;
    NoteSplitterProcessor::makeEditor = nullptr;
    c.removeChangeListener (this);
    panels.clear();
}

void DeviceRack::addMenu (Controller& c, Id track, Component* target)
{
    PopupMenu midi, instruments, effects, kalibre;
    const auto types = c.engine.plugins().known().getTypes();
    std::map<String, PopupMenu> byMaker[3];
    for (auto& d : types)
    {
        const auto role = c.engine.plugins().roleOf (d);
        const auto maker = d.manufacturerName.isEmpty() ? String ("Other") : d.manufacturerName;
        byMaker[(int) role][maker].addItem (d.name + "  (" + d.pluginFormatName + ")", [&c, track, d] { c.addPlugin (track, d); });
        // An instrument that sends MIDI (Riffer, Scaler) is a MIDI effect too: listed there as well (0069).
        if (role == DeviceRole::Instrument && c.engine.plugins().sendsMidi (d))
            byMaker[0][maker].addItem (d.name + "  (" + d.pluginFormatName + ")",
                                       [&c, track, d] { c.addPlugin (track, d, DeviceRole::MidiFx); });
    }
    for (auto& [maker, menu] : byMaker[0]) midi.addSubMenu (maker, menu);
    for (auto& [maker, menu] : byMaker[1]) instruments.addSubMenu (maker, menu);
    for (auto& [maker, menu] : byMaker[2]) effects.addSubMenu (maker, menu);
    kalibre.addItem ("Kalibre Synth", [&c, track] { c.addBuiltIn (track, synthRef(), DeviceRole::Instrument); });
    kalibre.addItem ("Kalibre Sampler", [&c, track] { c.addBuiltIn (track, samplerRef(), DeviceRole::Instrument); });
    kalibre.addItem ("Hardware Insert (Outboard Gear)", [&c, track] { c.addBuiltIn (track, hardwareInsertRef(), DeviceRole::AudioFx); });
    kalibre.addSeparator();
    kalibre.addItem ("X/Y Morph as an Instrument (Samples or Instruments)", [&c, track] { c.addBuiltIn (track, morphRef(), DeviceRole::Instrument); });
    kalibre.addItem ("X/Y Morph as an Effect", [&c, track] { c.addBuiltIn (track, morphRef(), DeviceRole::AudioFx); });
    kalibre.addSeparator();
    kalibre.addItem ("Container (Plug-ins on a Band, the Mid or the Sides)", [&c, track] { c.addBuiltIn (track, containerRef(), DeviceRole::AudioFx); });
    kalibre.addItem ("Note Splitter (a Chord's Notes to Several Tracks)  (MIDI Effect)", [&c, track] { c.addBuiltIn (track, noteSplitterRef(), DeviceRole::MidiFx); });
    // The family's JSFX, built in (decision 0066), with Kalibre's own devices (0069).
    kalibre.addSeparator();
    for (auto& s : jsfxDevices())
    {
        const char* kinds[] { "MIDI Effect", "Instrument", "Effect" };
        kalibre.addItem (String (s.name) + "  (" + kinds[(int) s.role] + ")", [&c, track, &s] { c.addBuiltIn (track, jsfxRef (s), s.role); });
    }
    kalibre.addItem ("Instant Virtual Instrument  (Instrument)", [&c, track] { c.addBuiltIn (track, instantVirtualInstrumentRef(), DeviceRole::Instrument); });
    // The family's ReaImGui tools, built in (decision 0068).
    for (auto& s : luaTools())
        kalibre.addItem (String (s.name) + "  (MIDI Effect)", [&c, track, &s] { c.addBuiltIn (track, luaToolRef (s), DeviceRole::MidiFx); });
    PopupMenu m;
    m.addSubMenu ("MIDI Effects", midi, midi.getNumItems() > 0);
    m.addSubMenu ("Instruments", instruments, instruments.getNumItems() > 0);
    m.addSubMenu ("Audio Effects", effects, effects.getNumItems() > 0);
    m.addSubMenu ("Kalibre", kalibre);
    if (types.isEmpty())
    {
        m.addSeparator();
        m.addItem (ui::u8 ("No plug-ins found yet. Plug-in Manager\xe2\x80\xa6"), [&c] { c.showPluginManager(); });
    }
    m.showMenuAsync (PopupMenu::Options().withTargetComponent (target));
}

void DeviceRack::changeListenerCallback (ChangeBroadcaster*)
{
    // Rebuilt only when the devices or modulators changed; otherwise updated in place.
    String shape;
    if (auto* t = c.focus())
    {
        shape << (int64) t->id << ":";
        for (auto& d : t->devices) shape << (int64) d.id << (d.active ? "a" : "o") << (c.engine.instance (d.id) ? "i" : "-") << ",";
        shape << "|";
        for (auto& m : t->modulators) shape << (int64) m.id << ",";
    }
    closeStaleWindows();
    if (shape != shownShape) rebuild();
    else for (auto* p : panels) { p->update(); p->repaint(); }
}

void DeviceRack::rebuild()
{
    const auto* t = c.focus();
    String shape;
    // Panels whose plug-in is still here are kept, with their windows: making a
    // plug-in's window again each time another is added left some blank.
    std::map<Id, std::unique_ptr<DevicePanel>> keep;
    while (! panels.isEmpty())
    {
        std::unique_ptr<DevicePanel> p (panels.removeAndReturn (panels.size() - 1));
        keep[p->id] = std::move (p);
    }
    modulators.clear();
    if (t)
    {
        shape << (int64) t->id << ":";
        for (auto& d : t->devices)
        {
            DevicePanel* p = nullptr;
            if (auto it = keep.find (d.id); it != keep.end()) { p = it->second.release(); keep.erase (it); }
            else p = new DevicePanel (c, d.id, *this);
            panels.add (p);
            strip.addAndMakeVisible (p);
            p->update();
            shape << (int64) d.id << (d.active ? "a" : "o") << (c.engine.instance (d.id) ? "i" : "-") << ",";
        }
        shape << "|";
        for (auto& m : t->modulators)
        {
            auto* p = modulators.add (new ModulatorPanel (c, t->id, m.id));
            strip.addAndMakeVisible (p);
            shape << (int64) m.id << ",";
        }
    }
    shownShape = shape;
    shownTrack = t ? t->id : 0;
    layoutStrip();
    repaint();
}

void DeviceRack::layoutStrip()
{
    const int h = jmax (theme::px (120), viewport.getHeight() - viewport.getScrollBarThickness());
    int x = 0;
    for (auto* m : modulators)
    {
        m->setBounds (x, 0, theme::px (200), h);
        x += theme::px (200) + theme::px (8);
    }
    for (auto* p : panels)
    {
        const int w = p->widthFor (h);
        p->setBounds (x, 0, w, h);
        x += w + theme::px (8);
    }
    add.setBounds (x, theme::px (4), add.idealWidth(), theme::px (28));
    x += add.idealWidth() + theme::px (8);
    strip.setSize (jmax (x, viewport.getWidth()), h);
    placeEditors();
}

juce::Rectangle<int> DeviceRack::pluginArea (Id device) const
{
    for (auto* p : panels)
        if (p->id == device && p->editor)
            return { p->getX() + p->body.getX() + (p->body.getWidth() - p->fitted.shownWidth()) / 2 - viewport.getViewPositionX() + viewport.getX(),
                     p->getY() + p->body.getY() + (p->body.getHeight() - p->fitted.shownHeight()) / 2 + viewport.getY(),
                     p->fitted.shownWidth(), p->fitted.shownHeight() };
    return {};
}

juce::AudioProcessorEditor* DeviceRack::editorOf (Id device) const
{
    for (auto* p : panels) if (p->id == device) return p->editor.get();
    return nullptr;
}

void DeviceRack::placeEditors()
{
    for (auto* p : panels) p->placeEditor();
}

void DeviceRack::paint (Graphics& g)
{
    g.fillAll (theme::ground);
    if (! c.focus())
    {
        g.setColour (theme::textSecondary);
        g.setFont (theme::font());
        g.drawFittedText ("Choose a track to see its plug-ins.", getLocalBounds(), Justification::centred, 2);
    }
}

void DeviceRack::resized()
{
    viewport.setBounds (getLocalBounds().reduced (theme::px (8), theme::px (6)));
    layoutStrip();
}

} // namespace kal
