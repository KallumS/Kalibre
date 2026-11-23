#include "KeysPanel.h"

namespace kal
{
using namespace juce;

KeysPanel::KeysPanel (Keys& k) : keys (k)
{
    setTitle ("Keys");
    setWantsKeyboardFocus (true);
    list.setModel (this);
    list.setRowHeight (theme::px (24));
    list.setTitle ("Commands and their keys");
    list.setWantsKeyboardFocus (false);
    addAndMakeVisible (list);
    for (auto* b : { &resetOne, &resetAll }) addAndMakeVisible (b);
    resetOne.onClick = [this]
    {
        const int row = list.getSelectedRow();
        if (row >= 0 && row < (int) keys.all().size()) keys.reset (keys.all()[(size_t) row].id);
        refresh();
    };
    resetAll.onClick = [this] { keys.resetAll(); refresh(); };
}

void KeysPanel::refresh()
{
    list.updateContent();
    list.repaint();
    repaint();
}

void KeysPanel::paintListBoxItem (int row, Graphics& g, int w, int h, bool selected)
{
    if (row < 0 || row >= (int) keys.all().size()) return;
    const auto& c = keys.all()[(size_t) row];
    if (selected) { g.setColour (theme::rule); g.fillRect (0, 0, w, h); }
    g.setColour (theme::text);
    g.setFont (theme::font());
    g.drawText (c.name, 8, 0, w - 90, h, Justification::centredLeft, true);
    g.setColour (row == capturing ? theme::text : theme::textSecondary);
    const auto key = row == capturing ? String ("Press a key") + ui::u8 ("\xe2\x80\xa6") : keys.textFor (c.id);
    g.drawText (key.isEmpty() ? ui::u8 ("\xe2\x80\x94") : key, w - 88, 0, 80, h, Justification::centredRight, false);
}

void KeysPanel::listBoxItemClicked (int row, const MouseEvent&)
{
    capturing = row;
    grabKeyboardFocus();
    refresh();
}

bool KeysPanel::keyPressed (const KeyPress& k)
{
    if (capturing < 0 || capturing >= (int) keys.all().size()) return false;
    const auto id = keys.all()[(size_t) capturing].id;
    capturing = -1;
    if (k.getKeyCode() == KeyPress::escapeKey) { refresh(); return true; }
    if (k.getKeyCode() == KeyPress::deleteKey || k.getKeyCode() == KeyPress::backspaceKey) keys.set (id, {});
    else keys.set (id, KeyPress (k.getKeyCode(), k.getModifiers().withoutMouseButtons(), 0));
    refresh();
    return true;
}

void KeysPanel::paint (Graphics& g)
{
    g.fillAll (theme::ground);
    g.setColour (theme::textSecondary);
    g.setFont (theme::font (theme::smallPoints));
    g.drawFittedText ("Click a command, then press its new key. Esc leaves it; Delete takes its key away.",
                      getLocalBounds().removeFromTop (theme::px (34)), Justification::topLeft, 2);
}

void KeysPanel::resized()
{
    auto r = getLocalBounds();
    r.removeFromTop (theme::px (36));
    auto bottom = r.removeFromBottom (theme::px (28));
    resetOne.setBounds (bottom.removeFromLeft (resetOne.idealWidth (theme::smallControl)));
    bottom.removeFromLeft (6);
    resetAll.setBounds (bottom.removeFromLeft (resetAll.idealWidth (theme::smallControl)));
    r.removeFromBottom (6);
    list.setBounds (r);
}

} // namespace kal
