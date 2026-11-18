#include "NoteSplitterEditor.h"

namespace kal
{
using namespace juce;

String NoteSplitterEditor::noteName (int output, int outputs)
{
    if (output == 0) return outputs == 1 ? "Every note" : "Top note";
    if (output == outputs - 1) return "Bottom note";
    static const char* nth[] { "", "2nd", "3rd", "4th", "5th", "6th", "7th", "8th", "9th", "10th", "11th", "12th", "13th", "14th", "15th", "16th" };
    return String (nth[jlimit (0, 15, output)]) + " note down";
}

NoteSplitterEditor::NoteSplitterEditor (NoteSplitterProcessor& p, Controller& ctrl) : AudioProcessorEditor (p), proc (p), c (ctrl)
{
    setTitle ("Note Splitter");
    for (auto* b : { &fewer, &more, &makeTracks }) addAndMakeVisible (b);
    fewer.setTitle ("Fewer Outputs");
    more.setTitle ("More Outputs");
    fewer.onClick = [this] { if (auto* d = c.project.device (device())) c.setSplitterOutputs (device(), (int) d->midiTo.size() - 1); };
    more.onClick = [this] { if (auto* d = c.project.device (device())) c.setSplitterOutputs (device(), (int) d->midiTo.size() + 1); };
    makeTracks.onClick = [this] { c.makeSplitterTracks (device()); };
    c.addChangeListener (this);
    setSize (theme::px (340), theme::px (200));
    refresh();
}

NoteSplitterEditor::~NoteSplitterEditor() { c.removeChangeListener (this); }

void NoteSplitterEditor::refresh()
{
    const auto* d = c.project.device (device());
    Track* owner = nullptr;
    c.project.device (device(), &owner);
    const int n = d ? (int) d->midiTo.size() : 0;
    if (n != shown)
    {
        shown = n;
        targets.clear();
        for (int k = 0; k < n; ++k)
        {
            auto* b = targets.add (new theme::HouseButton ({}, "The track output " + String (k + 1) + " plays: the chord's "
                                                              + noteName (k, n).toLowerCase() + (k == n - 1 && n > 1 ? " and any below it" : "") + "."));
            b->onClick = [this, k, b]
            {
                const auto* dev = c.project.device (device());
                Track* own = nullptr;
                c.project.device (device(), &own);
                if (! dev || k >= (int) dev->midiTo.size()) return;
                PopupMenu m;
                m.addItem ("None", true, dev->midiTo[(size_t) k] == 0, [this, k] { c.setSplitterTarget (device(), k, 0); });
                m.addSeparator();
                for (auto& t : c.project.tracks)
                    if (t.holdsClips() && t.kind == TrackKind::Track && (! own || t.id != own->id))
                        m.addItem (String (t.name), true, dev->midiTo[(size_t) k] == t.id, [this, k, id = t.id] { c.setSplitterTarget (device(), k, id); });
                m.showMenuAsync (PopupMenu::Options().withTargetComponent (b));
            };
            addAndMakeVisible (b);
        }
        setSize (getWidth(), theme::px (96) + n * theme::px (30));
    }
    for (int k = 0; k < n; ++k)
    {
        const auto* t = c.project.track (d->midiTo[(size_t) k]);
        targets[k]->setButtonText (t ? String (t->name) : String ("Choose a Track") + ui::u8 ("\xe2\x80\xa6"));
        targets[k]->setTitle ("Output " + String (k + 1) + ", " + noteName (k, n) + ": " + (t ? String (t->name) : String ("no track")));
    }
    fewer.setEnabled (n > 1);
    more.setEnabled (n < NoteSplit::maxOutputs);
    bool missing = false;
    if (d) for (auto to : d->midiTo) missing |= c.project.track (to) == nullptr;
    makeTracks.setEnabled (missing && owner != nullptr);
    resized();
    repaint();
}

void NoteSplitterEditor::paint (Graphics& g)
{
    g.fillAll (theme::sunken);
    g.setColour (theme::textSecondary);
    g.setFont (theme::font (theme::smallPoints));
    g.drawFittedText ("A chord's notes, top to bottom, each to a track's MIDI input: the top note to output 1. "
                      "The chord still goes on to this track's own instruments.", textArea, Justification::topLeft, 3);
    g.setColour (theme::text);
    g.setFont (theme::font());
    g.drawText (String (shown) + (shown == 1 ? " output" : " outputs"), countArea, Justification::centredLeft, false);
    // Each output's number and which note it takes.
    for (int k = 0; k < targets.size(); ++k)
    {
        const auto b = targets[k]->getBounds();
        const Rectangle<int> label (textArea.getX(), b.getY(), b.getX() - textArea.getX() - theme::px (8), b.getHeight());
        g.setColour (theme::text);
        g.setFont (theme::font());
        g.drawText (String (k + 1), label.withWidth (theme::px (22)), Justification::centredLeft, false);
        g.setColour (theme::textSecondary);
        g.setFont (theme::font (theme::smallPoints));
        g.drawText (noteName (k, targets.size()), label.withTrimmedLeft (theme::px (22)), Justification::centredLeft, true);
    }
}

void NoteSplitterEditor::resized()
{
    auto r = getLocalBounds().reduced (theme::px (12), theme::px (10));
    textArea = r.withHeight (theme::px (40));
    r.removeFromTop (theme::px (44));
    auto row = r.removeFromTop (theme::px (26));
    countArea = row.removeFromLeft (theme::px (84));
    fewer.setBounds (row.removeFromLeft (theme::px (30)));
    row.removeFromLeft (theme::px (4));
    more.setBounds (row.removeFromLeft (theme::px (30)));
    row.removeFromLeft (theme::px (12));
    makeTracks.setBounds (row.removeFromLeft (makeTracks.idealWidth (theme::smallControl)));
    r.removeFromTop (theme::px (6));
    for (auto* b : targets)
    {
        auto line = r.removeFromTop (theme::px (30)).withTrimmedBottom (theme::px (4));
        b->setBounds (line.withTrimmedLeft (theme::px (130)));
    }
}

} // namespace kal
