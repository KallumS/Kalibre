#include "StudioPanel.h"

#include "NetworkMidi.h"
#include "McpServer.h"

namespace kal
{
using namespace juce;

StudioPanel::StudioPanel (Controller& ctrl) : c (ctrl)
{
    setTitle ("Studio");
    for (auto* t : { &sendClock, &sendMtc, &link, &linkStartStop, &oscOn, &sendMmc, &followMmc, &networkMidi, &agentsOn })
    {
        addAndMakeVisible (t);
        t->onClick = [this] { store(); };
    }
    sendClock.setTooltip (theme::wrapTip ("Send 24 clock pulses a beat, with Start, Stop and Song Position, to the ports ticked above."));
    sendMtc.setTooltip (theme::wrapTip ("Send MIDI timecode to the same ports, for video and other machines that follow time rather than beats."));
    link.setTooltip (theme::wrapTip ("Share the tempo, and start on the same bar, with Live, apps and other computers on the network."));
    linkStartStop.setTooltip (theme::wrapTip ("With Link on: Play and Stop here start and stop the others in the session, and theirs start and stop Kalibre."));
    mtcRate.addItemList ({ "24 fps", "25 fps", "29.97 fps drop", "30 fps" }, 1);
    sendMmc.setTooltip (theme::wrapTip ("Send start, stop and locate as MIDI Machine Control to the ports ticked above: recorders and other software follow."));
    followMmc.setTooltip (theme::wrapTip ("Play, stop, locate and record when another machine sends MIDI Machine Control."));
    networkMidi.setTooltip (theme::wrapTip ("Switch on the Mac's network MIDI session, so an iPad or another computer can connect. It then shows up as a MIDI port."));
    followMtc.setTooltip (theme::wrapTip ("Chase timecode from a video machine or another computer: Kalibre starts, stops and locates with it."));
    for (auto* box : { &mtcRate, &follow, &mackieIn, &mackieOut, &followMtc, &huiIn, &huiOut })
    {
        addAndMakeVisible (box);
        box->onChange = [this] { store(); };
    }
    follow.setTooltip (theme::wrapTip ("Follow another machine's clock: its tempo, Start, Stop and Song Position move Kalibre."));
    for (auto* e : { &oscPort, &replyHost, &replyPort, &agentsPort })
    {
        addAndMakeVisible (e);
        e->onReturnKey = [this] { store(); };
        e->onFocusLost = [this] { store(); };
    }
    oscPort.setInputRestrictions (5, "0123456789");
    replyPort.setInputRestrictions (5, "0123456789");
    oscPort.setTitle ("OSC port");
    replyHost.setTitle ("Reply to");
    replyPort.setTitle ("Reply port");
    agentsPort.setInputRestrictions (5, "0123456789");
    agentsPort.setTitle ("AI agents' port");
    agentsOn.setTooltip (theme::wrapTip ("AI agents on this Mac (Claude and others that speak MCP) can look at the song and change it: "
                                         "make tracks, add plug-ins, write clips and notes. Each change is one undo step, said in the message line. "
                                         "Nothing from another computer can reach it."));
    for (auto* b : { &copyDesktop, &copyCode }) addAndMakeVisible (b);
    copyDesktop.onClick = [this]
    {
        SystemClipboard::copyTextToClipboard (McpServer::desktopSetup());
        c.say ("Copied. In Claude Desktop: Settings > Developer > Edit Config, paste it in (inside any \"mcpServers\" there already), save, and open Claude again.");
    };
    copyCode.onClick = [this]
    {
        SystemClipboard::copyTextToClipboard (McpServer::claudeCodeSetup());
        c.say ("Copied. Paste it into Terminal and press Return: Claude Code can then reach Kalibre.");
    };
    addAndMakeVisible (rescan);
    rescan.onClick = [this] { build(); };
    build();
    startTimerHz (2);
}

void StudioPanel::build()
{
    ins = MidiInput::getAvailableDevices();
    outs = MidiOutput::getAvailableDevices();
    const auto& s = c.studio;
    clockOuts.clear();
    for (auto& d : outs)
    {
        auto* t = clockOuts.add (new ToggleButton (d.name));
        t->setToggleState (std::find (s.clockOut.begin(), s.clockOut.end(), d.identifier.toStdString()) != s.clockOut.end(), dontSendNotification);
        t->onClick = [this] { store(); };
        addAndMakeVisible (t);
    }
    sendClock.setToggleState (s.sendClock, dontSendNotification);
    sendMtc.setToggleState (s.sendMtc, dontSendNotification);
    mtcRate.setSelectedItemIndex (s.mtcRate, dontSendNotification);
    auto fill = [] (ComboBox& box, const Array<MidiDeviceInfo>& list, const std::string& chosen)
    {
        box.clear (dontSendNotification);
        box.addItem ("None", 1);
        for (int i = 0; i < list.size(); ++i) box.addItem (list[i].name, i + 2);
        box.setSelectedId (1, dontSendNotification);
        for (int i = 0; i < list.size(); ++i)
            if (list[i].identifier.toStdString() == chosen) box.setSelectedId (i + 2, dontSendNotification);
    };
    fill (follow, ins, s.followClock);
    fill (mackieIn, ins, s.mackieIn);
    fill (mackieOut, outs, s.mackieOut);
    fill (followMtc, ins, s.followMtc);
    fill (huiIn, ins, s.huiIn);
    fill (huiOut, outs, s.huiOut);
    sendMmc.setToggleState (s.sendMmc, dontSendNotification);
    followMmc.setToggleState (s.followMmc, dontSendNotification);
    networkMidi.setToggleState (s.networkMidi, dontSendNotification);
    networkMidi.setEnabled (networkmidi::available());
    link.setToggleState (s.link, dontSendNotification);
    linkStartStop.setToggleState (s.linkStartStop, dontSendNotification);
    oscOn.setToggleState (s.oscOn, dontSendNotification);
    agentsOn.setToggleState (s.mcpOn, dontSendNotification);
    agentsPort.setText (String (s.mcpPort), false);
    oscPort.setText (String (s.oscPort), false);
    replyHost.setText (String (s.oscReplyHost), false);
    replyPort.setText (String (s.oscReplyPort), false);
    resized();
    repaint();
}

void StudioPanel::store()
{
    ControlSettings s = c.studio;
    s.clockOut.clear();
    for (int i = 0; i < clockOuts.size() && i < outs.size(); ++i)
        if (clockOuts[i]->getToggleState()) s.clockOut.push_back (outs[i].identifier.toStdString());
    s.sendClock = sendClock.getToggleState();
    s.sendMtc = sendMtc.getToggleState();
    s.mtcRate = jmax (0, mtcRate.getSelectedItemIndex());
    auto chosen = [] (ComboBox& box, const Array<MidiDeviceInfo>& list)
    {
        const int i = box.getSelectedId() - 2;
        return i >= 0 && i < list.size() ? list[i].identifier.toStdString() : std::string();
    };
    s.followClock = chosen (follow, ins);
    s.mackieIn = chosen (mackieIn, ins);
    s.mackieOut = chosen (mackieOut, outs);
    s.followMtc = chosen (followMtc, ins);
    s.huiIn = chosen (huiIn, ins);
    s.huiOut = chosen (huiOut, outs);
    s.sendMmc = sendMmc.getToggleState();
    s.followMmc = followMmc.getToggleState();
    if (networkMidi.getToggleState() != s.networkMidi)
    {
        s.networkMidi = networkMidi.getToggleState();
        if (! networkmidi::setEnabled (s.networkMidi)) c.say ("The network MIDI session couldn't be switched " + String (s.networkMidi ? "on." : "off."), true);
        else if (s.networkMidi) c.say ("Network MIDI is on as " + networkmidi::sessionName().quoted() + ". Connect from the other device, then click Look Again.");
    }
    s.link = link.getToggleState();
    s.linkStartStop = linkStartStop.getToggleState();
    s.oscOn = oscOn.getToggleState();
    s.oscPort = jlimit (1, 65535, oscPort.getText().getIntValue());
    s.oscReplyHost = replyHost.getText().trim().toStdString();
    s.oscReplyPort = jlimit (1, 65535, replyPort.getText().getIntValue());
    s.mcpOn = agentsOn.getToggleState();
    s.mcpPort = jlimit (1024, 65535, agentsPort.getText().getIntValue());
    if (s == c.studio) return;
    c.setStudio (s);
}

void StudioPanel::timerCallback()
{
    String p;
    if (c.engine.syncDevices.linkOn())
    {
        const int n = c.engine.syncDevices.linkPeers();
        p = n == 0 ? "No one else in the session yet." : n == 1 ? "1 other in the session." : String (n) + " others in the session.";
    }
    if (c.studio.followClock.size() > 0 && c.engine.syncDevices.followedBpm() > 0)
        p << (p.isEmpty() ? "" : " ") << "Following " << String (c.engine.syncDevices.followedBpm(), 2) << " BPM.";
    if (p != peers) { peers = p; repaint(); }
    if (agentsOn.getToggleState() != c.studio.mcpOn) agentsOn.setToggleState (c.studio.mcpOn, dontSendNotification);
    const String a = c.agentsListening > 0 ? "Listening on this Mac, port " + String (c.agentsListening) + "." : String ("Off.");
    if (a != agentsStatus) { agentsStatus = a; repaint(); }
}

int StudioPanel::preferredHeight() const
{
    return theme::px (24) * (28 + outs.size()) + theme::px (200);
}

void StudioPanel::paint (Graphics& g)
{
    g.fillAll (theme::ground);
    for (auto& [text, r] : headings)
    {
        g.setColour (text.startsWith ("~") ? theme::textSecondary : theme::text);
        g.setFont (text.startsWith ("~") ? theme::font (theme::smallPoints) : theme::font (theme::bodyPoints, true));
        g.drawFittedText (text.trimCharactersAtStart ("~"), r, Justification::centredLeft, 2);
    }
    if (agentsStatus.isNotEmpty())
    {
        g.setColour (theme::textSecondary);
        g.setFont (theme::font (theme::smallPoints));
        g.drawFittedText (agentsStatus, agentsStatusArea, Justification::centredLeft, 2);
    }
    if (peers.isNotEmpty())
    {
        g.setColour (theme::textSecondary);
        g.setFont (theme::font (theme::smallPoints));
        g.drawFittedText (peers, linkStartStop.getBounds().translated (0, linkStartStop.getHeight()), Justification::centredLeft, 2);
    }
}

void StudioPanel::resized()
{
    headings.clear();
    auto r = getLocalBounds().reduced (theme::px (4), theme::px (4));
    const int row = theme::px (24);
    auto heading = [&] (const String& text) { headings.push_back ({ text, r.removeFromTop (row) }); };
    auto put = [&] (Component& comp) { comp.setBounds (r.removeFromTop (row).reduced (0, 2)); };
    auto labelled = [&] (const String& label, Component& comp)
    {
        auto line = r.removeFromTop (row);
        headings.push_back ({ "~" + label, line.removeFromLeft (theme::px (70)) });
        comp.setBounds (line.reduced (0, 2));
    };
    heading ("Sync");
    heading ("~Send to:");
    for (auto* t : clockOuts) put (*t);
    if (clockOuts.isEmpty()) heading ("~No MIDI outputs found.");
    put (sendClock);
    put (sendMtc);
    labelled ("Timecode", mtcRate);
    labelled ("Follow", follow);
    labelled ("Chase MTC", followMtc);
    put (sendMmc);
    put (followMmc);
    put (networkMidi);
    put (link);
    put (linkStartStop);
    r.removeFromTop (row);
    heading ("Control Surface");
    labelled ("Mackie in", mackieIn);
    labelled ("Mackie out", mackieOut);
    labelled ("HUI in", huiIn);
    labelled ("HUI out", huiOut);
    put (oscOn);
    labelled ("Listen on", oscPort);
    labelled ("Reply to", replyHost);
    labelled ("Reply port", replyPort);
    r.removeFromTop (theme::px (6));
    rescan.setBounds (r.removeFromTop (theme::px (28)).withWidth (rescan.idealWidth()));
    r.removeFromTop (row);
    heading ("AI Agents (MCP)");
    put (agentsOn);
    labelled ("Port", agentsPort);
    agentsStatusArea = r.removeFromTop (row);
    for (auto* b : { &copyDesktop, &copyCode })
    {
        b->setBounds (r.removeFromTop (theme::px (28)).withWidth (jmin (r.getWidth(), b->idealWidth (theme::smallControl))));
        r.removeFromTop (theme::px (4));
    }
}

} // namespace kal
