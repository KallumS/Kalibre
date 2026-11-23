#include "TransientQuestion.h"

#include "AudioTools.h"
#include "Edit.h"
#include "Theme.h"

namespace kal
{
using namespace juce;

namespace
{
// Each source's every possible hit, found once; at any sensitivity, its hits are quick to count.
struct Looked
{
    Id clip = 0;
    std::vector<audiotools::HitCandidate> candidates;
    double rate = 0;
};

class SensitivityPanel : public Component
{
public:
    SensitivityPanel (Controller& ctrl, std::vector<Looked> l) : c (ctrl), looked (std::move (l))
    {
        slider.setSliderStyle (Slider::LinearHorizontal);
        slider.setTextBoxStyle (Slider::NoTextBox, false, 0, 0);
        slider.setRange (0.0, 100.0, 1.0);
        slider.setValue (c.transientSensitivity * 100.0, dontSendNotification);
        slider.setDoubleClickReturnValue (true, 50.0);
        slider.setTitle ("Sensitivity");
        slider.setTooltip (theme::wrapTip ("Higher finds more hits, the quiet ones too; lower, only the strongest. Double-click for the middle."));
        slider.onValueChange = [this] { count(); };
        addAndMakeVisible (slider);
        setSize (theme::px (420), theme::px (84));
        count();
    }

    double sensitivity() const { return slider.getValue() / 100.0; }

    void paint (Graphics& g) override
    {
        g.setFont (theme::font());
        g.setColour (theme::text);
        auto r = getLocalBounds();
        g.drawText ("Sensitivity  " + String (roundToInt (slider.getValue())) + "%", r.removeFromTop (theme::px (20)), Justification::centredLeft);
        r.removeFromTop (theme::px (24));   // the slider
        g.setFont (theme::font (theme::smallPoints));
        g.setColour (theme::textSecondary);
        auto ends = r.removeFromTop (theme::px (16));
        g.drawText ("Fewer hits", ends, Justification::centredLeft);
        g.drawText ("More hits", ends, Justification::centredRight);
        g.setFont (theme::font());
        g.setColour (theme::text);
        g.drawText (said, r, Justification::centredLeft);
    }

    void resized() override
    {
        auto r = getLocalBounds();
        r.removeFromTop (theme::px (20));
        slider.setBounds (r.removeFromTop (theme::px (24)));
    }

private:
    Controller& c;
    std::vector<Looked> looked;
    Slider slider;
    String said;

    // How many clips it makes at this sensitivity: the cuts the split itself would make.
    void count()
    {
        size_t clips = 0;
        for (auto& l : looked)
        {
            std::vector<double> seconds;
            for (int s : audiotools::hitsAt (l.candidates, sensitivity(), l.rate)) seconds.push_back (s / l.rate);
            if (l.clip != 0) clips += edit::cutsAtSource (c.project, l.clip, seconds).size() + 1;
            else clips += (size_t) std::count_if (seconds.begin(), seconds.end(), [] (double s) { return s > 0.01; }) + 1;
        }
        said = clips <= looked.size() ? String ("No hits found: nothing is split. Raise the sensitivity to find quieter ones.")
                                      : "Makes " + String ((int) clips) + " clips, one for each hit.";
        setDescription (said);   // VoiceOver reads it too
        repaint();
    }
};
} // namespace

void askTransients (Controller& c, const String& title, std::vector<TransientSource> sources, std::function<void (double)> go)
{
    std::vector<Looked> looked;
    for (auto& s : sources)
    {
        const auto file = s.clip != 0 ? [&] { const auto* cl = c.project.clip (s.clip); return cl ? c.fileOf (*cl, false) : File(); }() : s.file;
        AudioBuffer<float> buf;
        double rate = 0;
        if (! Controller::readAudio (file, buf, rate) || rate <= 0) continue;
        looked.push_back ({ s.clip, audiotools::hitCandidates (buf.getArrayOfReadPointers(), buf.getNumChannels(), buf.getNumSamples(), rate), rate });
    }
    if (looked.empty()) { c.say ("Kalibre can't read that audio to find its hits.", true); return; }
    auto panel = std::make_shared<SensitivityPanel> (c, std::move (looked));
    auto* w = new AlertWindow (title, "How readily a hit is found: higher finds more, the quiet ones too.", MessageBoxIconType::NoIcon);
    w->addCustomComponent (panel.get());
    w->addButton ("Split", 1, KeyPress (KeyPress::returnKey));
    w->addButton ("Cancel", 0, KeyPress (KeyPress::escapeKey));
    ui::present (w, ModalCallbackFunction::create ([&c, panel, go] (int r)
    {
        if (r != 1) return;
        const double s = panel->sensitivity();
        c.setTransientSensitivity (s);
        go (s);
    }), true);
}

} // namespace kal
