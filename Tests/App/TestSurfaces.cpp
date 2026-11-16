#include "Check.h"
#include "Demo.h"
#include "Mackie.h"
#include "Surfaces.h"

using namespace kal;
using namespace juce;

TEST ("surfaces: a Mackie fader, knob and buttons move the tracks; the surface hears back")
{
    PluginManager plugins;
    Engine engine (plugins);
    Controller c (engine);
    c.project = demo::song();
    c.refresh();
    Surfaces s (c);
    std::vector<MidiMessage> sent;
    s.sendToSurface = [&sent] (const MidiMessage& m) { sent.push_back (m); };

    const auto ids = s.strips();
    CHECK (ids[0] == c.project.tracks[0].id);
    // Strip 2's fader to 0 dB, touched and let go: one undo step.
    const auto undoBefore = c.history.size();
    s.mackie (MidiMessage::noteOn (1, mackie::note::faderTouch + 1, (uint8) 127));
    const auto f = mackie::fader (1, mackie::dbToFader (-12.0));
    s.mackie (MidiMessage (f[0], f[1], f[2]));
    s.mackie (MidiMessage::noteOn (1, mackie::note::faderTouch + 1, (uint8) 0));
    CHECK (std::abs (c.project.track (ids[1])->volumeDb + 12.0) < 0.05);
    CHECK_EQ (c.history.size(), undoBefore + 1);
    // The V-Pot pans; Mute toggles.
    s.mackie (MidiMessage::controllerEvent (1, 16 + 1, 0x45));   // five clicks left
    CHECK (c.project.track (ids[1])->pan < -0.05);
    s.mackie (MidiMessage::noteOn (1, mackie::note::mute + 1, (uint8) 127));
    CHECK (c.project.track (ids[1])->mute);
    // Banks move the strips along.
    s.mackie (MidiMessage::noteOn (1, mackie::note::channelRight, (uint8) 127));
    CHECK (s.strips()[0] == c.project.tracks[1].id);
    // Feedback: the mute light, the names on the display.
    bool muteLight = false, display = false;
    for (auto& m : sent)
    {
        if (m.isNoteOn() && m.getNoteNumber() == mackie::note::mute && m.getVelocity() == 127) muteLight = true;
        if (m.isSysEx() && m.getSysExDataSize() > 5) display = true;
    }
    CHECK (muteLight);
    CHECK (display);
}

TEST ("surfaces: OSC moves the transport and the tracks")
{
    PluginManager plugins;
    Engine engine (plugins);
    Controller c (engine);
    c.project = demo::song();
    c.refresh();
    Surfaces s (c);
    s.osc (OSCMessage ("/kalibre/track/2/volume", (float) mackie::dbToFader (-6.0)));
    CHECK (std::abs (c.project.tracks[1].volumeDb + 6.0) < 0.05);
    s.osc (OSCMessage ("/kalibre/track/2/solo", (int32) 1));
    CHECK (c.project.tracks[1].solo);
    s.osc (OSCMessage ("/kalibre/tempo", 140.0f));
    CHECK (std::abs (c.project.tempo.bpmAt (0) - 140.0) < 1e-6);
    s.osc (OSCMessage ("/kalibre/locate", 3.0f));
    CHECK_EQ (c.cursor, c.project.tempo.barStart (2));
    s.osc (OSCMessage ("/kalibre/loop", (int32) 1));
    CHECK (c.project.loop.on);
}

#include "Hui.h"

TEST ("surfaces: a HUI fader, mute and play move Kalibre; the surface is pinged and lit")
{
    PluginManager plugins;
    Engine engine (plugins);
    Controller c (engine);
    c.project = demo::song();
    c.refresh();
    Surfaces s (c);
    std::vector<MidiMessage> sent;
    s.sendToHui = [&sent] (const MidiMessage& m) { sent.push_back (m); };
    const auto ids = s.strips();
    auto feed = [&s] (std::uint8_t a, std::uint8_t b, std::uint8_t v) { s.hui (MidiMessage (a, b, v)); };
    // Touch strip 1, move it to -12 dB, let go.
    const auto undoBefore = c.history.size();
    feed (0xB0, 0x0F, 0x01); feed (0xB0, 0x2F, 0x40);
    for (auto& b : hui::fader (1, mackie::dbToFader (-12.0))) feed (b[0], b[1], b[2]);
    feed (0xB0, 0x0F, 0x01); feed (0xB0, 0x2F, 0x00);
    CHECK (std::abs (c.project.track (ids[1])->volumeDb + 12.0) < 0.05);
    CHECK_EQ (c.history.size(), undoBefore + 1);
    feed (0xB0, 0x0F, 0x01); feed (0xB0, 0x2F, 0x40 | hui::port::mute);
    CHECK (c.project.track (ids[1])->mute);
    feed (0xB0, 0x0F, hui::zone::transport); feed (0xB0, 0x2F, 0x40 | hui::port::play);
    CHECK (engine.isPlaying());
    c.stop();
    // Pinged, the mute lit (zone 1, port 2 with 0x40), a name sent.
    bool ping = false, lit = false, name = false;
    for (size_t i = 0; i < sent.size(); ++i)
    {
        const auto& m = sent[i];
        if (m.isNoteOn (true) && m.getNoteNumber() == 0) ping = true;
        if (m.isController() && m.getControllerNumber() == 0x2C && m.getControllerValue() == (0x40 | hui::port::mute) && i > 0
            && sent[i - 1].getControllerValue() == 1) lit = true;
        if (m.isSysEx()) name = true;
    }
    CHECK (lit);
    CHECK (name);
    ignoreUnused (ping);
}
