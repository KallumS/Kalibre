#include "DeviceRack.h"

#include "Modulation.h"
#include "MorphProcessor.h"

namespace kal
{
using namespace juce;

//==============================================================================
class DevicePanel : public Component
{
public:
    DevicePanel (Controller& ctrl, Id dev) : c (ctrl), id (dev)
    {
        for (auto* b : { &power, &bypass, &menu }) addAndMakeVisible (b);
        power.setTitle ("Plug-in On");
        bypass.setTitle ("Bypass");
        menu.setTitle ("Plug-in Menu");
        power.onClick = [this] { c.setDeviceActive (id, ! device()->active); };
        bypass.onClick = [this] { c.apply ("Bypass", [this] (Project& p) { if (auto* d = p.device (id)) d->bypassed = ! d->bypassed; }); };
        menu.onClick = [this] { showMenu(); };
        update();
    }

    ~DevicePanel() override { editor.reset(); }

    Device* device() { return c.project.device (id); }

    void update()
    {
        auto* d = device();
        if (! d) return;
        power.setToggleState (d->active, dontSendNotification);
        bypass.setToggleState (d->bypassed, dontSendNotification);
        auto* inst = c.engine.instance (id);
        if (! d->active || ! inst) { editor.reset(); natural = {}; return; }
        if (! editor || editorFor != inst)
        {
            editor.reset();
            editorFor = inst;
            AudioProcessorEditor* e = inst->hasEditor() ? inst->createEditorIfNeeded() : nullptr;
            if (! e) e = new GenericAudioProcessorEditor (*inst);
            editor.reset (e);
            natural = editor->getBounds();
            addAndMakeVisible (*editor);
        }
    }

    // The width this panel takes at a height.
    int widthFor (int height)
    {
        const int header = theme::px (30);
        if (! editor || natural.isEmpty()) return theme::px (160);
        const float s = jlimit (0.25f, 3.0f, (float) (height - header) / (float) jmax (1, natural.getHeight()));
        return jmax (theme::px (160), roundToInt ((float) natural.getWidth() * s));
    }

    void paint (Graphics& g) override
    {
        auto* d = device();
        g.setColour (theme::sunken);
        g.fillRoundedRectangle (getLocalBounds().toFloat(), 6.0f);
        auto header = getLocalBounds().removeFromTop (theme::px (30)).reduced (theme::px (8), 0);
        header.removeFromRight (theme::px (96));
        g.setColour (d && d->active ? theme::text : theme::textSecondary);
        g.setFont (theme::font (theme::bodyPoints, true));
        if (d)
        {
            const char* roles[] { "MIDI effect", "Instrument", "Effect" };
            g.drawFittedText (String (d->plugin.name), header.removeFromTop (header.getHeight() * 3 / 5), Justification::bottomLeft, 1);
            g.setColour (theme::textSecondary);
            g.setFont (theme::font (theme::smallPoints));
            String sub = String (roles[(int) d->role]) + " " + ui::u8 ("\xc2\xb7") + " " + String (d->plugin.format);
            if (const auto* from = c.project.track (d->sidechainFrom)) sub << " " << ui::u8 ("\xc2\xb7") << " sidechain: " << String (from->name);
            g.drawFittedText (sub, header, Justification::topLeft, 1);
            if (! d->active)
            {
                g.setColour (theme::textSecondary);
                g.setFont (theme::font());
                g.drawFittedText ("Off: unloaded, settings kept. Switch it on to load it again.",
                                  getLocalBounds().withTrimmedTop (theme::px (30)).reduced (12), Justification::centred, 4);
            }
            else if (! c.engine.instance (id))
            {
                g.setColour (theme::warnText);
                g.setFont (theme::font());
                g.drawFittedText ("This plug-in isn't installed, or didn't load. Its settings are kept.",
                                  getLocalBounds().withTrimmedTop (theme::px (30)).reduced (12), Justification::centred, 4);
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
        if (! editor || natural.isEmpty()) return;
        auto area = getLocalBounds().withTrimmedTop (theme::px (30));
        const float s = jlimit (0.25f, 3.0f, (float) area.getHeight() / (float) jmax (1, natural.getHeight()));
        if (editor->isResizable())
        {
            // Resizable plug-ins are given the size, keeping their shape.
            auto w = roundToInt ((float) natural.getWidth() * s), h = area.getHeight();
            if (auto* k = editor->getConstrainer())
            {
                Rectangle<int> b (0, 0, w, h);
                k->checkBounds (b, editor->getBounds(), area, false, false, true, true);
                w = b.getWidth(); h = b.getHeight();
            }
            editor->setBounds (area.getX(), area.getY(), w, h);
        }
        else
        {
            // Others are scaled.
            editor->setScaleFactor (s);
            editor->setTopLeftPosition (area.getTopLeft());
        }
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
    std::unique_ptr<AudioProcessorEditor> editor;
    AudioPluginInstance* editorFor = nullptr;
    Rectangle<int> natural;
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
    strip.addAndMakeVisible (add);
    add.onClick = [this] { addMenu (c, c.focusTrack, &add); };
    MorphProcessor::choosePlugin = [this] (std::function<void (const PluginRef&)> chosen)
    {
        PopupMenu m;
        for (auto& d : c.engine.plugins().known().getTypes())
            m.addItem (d.name + " (" + d.pluginFormatName + ")", [chosen, d] { chosen (PluginManager::refFor (d)); });
        if (m.getNumItems() == 0) m.addItem ("Scan for plug-ins first (Kalibre menu)", false, false, [] {});
        m.showMenuAsync (PopupMenu::Options());
    };
    c.addChangeListener (this);
    rebuild();
}

DeviceRack::~DeviceRack()
{
    MorphProcessor::choosePlugin = nullptr;
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
        const auto role = PluginManager::roleFor (d);
        auto& menu = byMaker[(int) role][d.manufacturerName.isEmpty() ? String ("Other") : d.manufacturerName];
        menu.addItem (d.name + "  (" + d.pluginFormatName + ")", [&c, track, d] { c.addPlugin (track, d); });
    }
    for (auto& [maker, menu] : byMaker[0]) midi.addSubMenu (maker, menu);
    for (auto& [maker, menu] : byMaker[1]) instruments.addSubMenu (maker, menu);
    for (auto& [maker, menu] : byMaker[2]) effects.addSubMenu (maker, menu);
    kalibre.addItem ("X/Y Morph as an Instrument (Samples or Instruments)", [&c, track] { c.addBuiltIn (track, morphRef(), DeviceRole::Instrument); });
    kalibre.addItem ("X/Y Morph as an Effect", [&c, track] { c.addBuiltIn (track, morphRef(), DeviceRole::AudioFx); });
    PopupMenu m;
    m.addSubMenu ("MIDI Effects", midi, midi.getNumItems() > 0);
    m.addSubMenu ("Instruments", instruments, instruments.getNumItems() > 0);
    m.addSubMenu ("Audio Effects", effects, effects.getNumItems() > 0);
    m.addSubMenu ("Kalibre", kalibre);
    if (types.isEmpty())
    {
        m.addSeparator();
        m.addItem ("No plug-ins found yet. Scan for Plug-ins", [&c]
        {
            c.engine.plugins().scan ([&c] (float p, const String& what)
            {
                c.say (p >= 1.0f ? "Scan finished: " + what + "." : "Scanning plug-ins, " + String (roundToInt (p * 100)) + "%: " + what);
            });
        });
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
    if (shape != shownShape) rebuild();
    else for (auto* p : panels) { p->update(); p->repaint(); }
}

void DeviceRack::rebuild()
{
    const auto* t = c.focus();
    String shape;
    panels.clear();
    modulators.clear();
    if (t)
    {
        shape << (int64) t->id << ":";
        for (auto& d : t->devices)
        {
            auto* p = panels.add (new DevicePanel (c, d.id));
            strip.addAndMakeVisible (p);
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
