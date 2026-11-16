#include "PluginManagerView.h"

namespace kal
{
using namespace juce;

namespace
{
const char* columnNames[] { "Name", "Maker", "Format", "Kind", "Status", "File" };
const float columnShares[] { 0.24f, 0.15f, 0.07f, 0.09f, 0.19f, 0.26f };

String formatName (const String& f) { return f == "AudioUnit" ? String ("AU") : f; }

String kindOf (const PluginDescription& d)
{
    const char* role[] { "MIDI Effect", "Instrument", "Effect" };
    return role[(int) PluginManager::roleFor (d)];
}

String status (const PluginManagerView::Row& r) { return r.on ? String ("On") : PluginManager::describe (r.reason); }
} // namespace

PluginManagerView::PluginManagerView (Controller& ctrl) : c (ctrl)
{
    setTitle ("Plug-in Manager");
    list.setModel (this);
    list.setRowHeight (theme::px (24));
    list.setMultipleSelectionEnabled (true);
    list.setTitle ("Plug-ins");
    list.setColour (ListBox::backgroundColourId, theme::sunken);
    list.setColour (ListBox::outlineColourId, theme::rule);
    addAndMakeVisible (list);
    search.setTitle ("Search plug-ins");
    search.setTextToShowWhenEmpty ("Search by name, maker or format", theme::textSecondary);
    search.onTextChange = [this] { refresh(); };
    addAndMakeVisible (search);
    for (auto* b : { &scanNew, &scanAgain, &turnOff, &turnOn, &reveal, &onlyOff }) addAndMakeVisible (b);

    scanNew.onClick = [this]
    {
        if (c.engine.plugins().scanning()) c.engine.plugins().cancelScan();
        else c.scanPlugins();
        updateButtons();
    };
    scanAgain.onClick = [this]
    {
        std::vector<std::pair<String, String>> files;
        for (auto* r : chosen())
        {
            const std::pair<String, String> f { r->format == "AU" ? String ("AudioUnit") : r->format, r->file };
            if (std::find (files.begin(), files.end(), f) == files.end()) files.push_back (f);
        }
        if (! files.empty()) c.scanPlugins (files);
    };
    turnOff.onClick = [this]
    {
        int n = 0;
        StringArray seen;
        for (auto* r : chosen())
            if (r->on && ! seen.contains (r->file))
            {
                seen.add (r->file);
                c.engine.plugins().turnOff (r->format == "AU" ? String ("AudioUnit") : r->format, r->file);
                ++n;
            }
        if (n > 0) c.say (n == 1 ? "Turned off " + chosen().front()->name + ". Projects that use it open without it."
                                 : "Turned off " + String (n) + " plug-ins. Projects that use them open without them.");
        refresh();
    };
    turnOn.onClick = [this]
    {
        std::vector<std::pair<String, String>> again;
        for (auto* r : chosen())
        {
            if (r->on) continue;
            if (r->reason == PluginManager::Reason::TurnedOff) c.engine.plugins().turnOn (r->file);
            else again.push_back ({ r->format == "AU" ? String ("AudioUnit") : r->format, r->file });
        }
        if (! again.empty()) c.scanPlugins (again);
        refresh();
    };
    reveal.onClick = [this]
    {
        auto ch = chosen();
        if (! ch.empty()) File (ch.front()->file).revealToUser();
    };
    onlyOff.onClick = [this] { refresh(); };

    c.engine.plugins().known().addChangeListener (this);
    c.addChangeListener (this);
    refresh();
}

PluginManagerView::~PluginManagerView()
{
    c.engine.plugins().known().removeChangeListener (this);
    c.removeChangeListener (this);
}

void PluginManagerView::changeListenerCallback (ChangeBroadcaster* from)
{
    if (! isShowing()) return;
    if (from == &c) { updateButtons(); return; }
    refresh();
}

void PluginManagerView::refresh()
{
    StringArray keep;
    for (auto* r : chosen()) keep.add (r->format + "|" + r->file + "|" + r->name);

    auto& pm = c.engine.plugins();
    std::vector<Row> all;
    for (auto& d : pm.known().getTypes())
        all.push_back ({ d.name, d.manufacturerName, formatName (d.pluginFormatName), kindOf (d), d.fileOrIdentifier, true, {}, {} });
    for (auto& b : pm.blocked())
    {
        if (! b.types.empty())
        {
            for (auto& d : b.types)
                all.push_back ({ d.name, d.manufacturerName, formatName (d.pluginFormatName), kindOf (d), b.file, false, b.reason, b.detail });
            continue;
        }
        // A file's name, or what an Audio Unit's identifier says it is.
        String name = File::createFileWithoutCheckingPath (b.file).getFileNameWithoutExtension();
        if (b.file.startsWith ("AudioUnit:"))
        {
            name = b.file.fromFirstOccurrenceOf (":", false, false);
            for (auto* f : pm.formats().getFormats())
                if (f->getName() == b.format) name = f->getNameOfPluginFromIdentifier (b.file);
        }
        all.push_back ({ name, {}, formatName (b.format), {}, b.file, false, b.reason, b.detail });
    }

    const auto text = search.getText().trim();
    rows.clear();
    for (auto& r : all)
    {
        if (onlyOff.getToggleState() && r.on) continue;
        if (text.isNotEmpty() && ! (r.name.containsIgnoreCase (text) || r.maker.containsIgnoreCase (text)
                                    || r.format.containsIgnoreCase (text) || r.file.containsIgnoreCase (text))) continue;
        rows.push_back (r);
    }
    auto key = [this] (const Row& r) -> String
    {
        switch (sortColumn)
        {
            case 1: return r.maker;
            case 2: return r.format;
            case 3: return r.kind;
            case 4: return r.on ? String ("~") : status (r);    // what's off first
            case 5: return r.file;
            default: return r.name;
        }
    };
    std::stable_sort (rows.begin(), rows.end(), [&] (const Row& a, const Row& b)
    {
        int d = key (a).compareNatural (key (b));
        if (d == 0) d = a.name.compareNatural (b.name);
        return sortUp ? d < 0 : d > 0;
    });

    list.updateContent();
    SparseSet<int> sel;
    for (int i = 0; i < (int) rows.size(); ++i)
        if (keep.contains (rows[(size_t) i].format + "|" + rows[(size_t) i].file + "|" + rows[(size_t) i].name)) sel.addRange ({ i, i + 1 });
    list.setSelectedRows (sel, dontSendNotification);
    updateButtons();
    list.repaint();
    repaint();
}

std::vector<const PluginManagerView::Row*> PluginManagerView::chosen() const
{
    std::vector<const Row*> out;
    for (int i = 0; i < (int) rows.size(); ++i)
        if (list.isRowSelected (i)) out.push_back (&rows[(size_t) i]);
    return out;
}

void PluginManagerView::updateButtons()
{
    const bool busy = c.engine.plugins().scanning();
    scanNew.setButtonText (busy ? "Stop Scanning" : "Scan for New Plug-ins");
    const auto ch = chosen();
    bool anyOn = false, anyOff = false, aFile = false;
    for (auto* r : ch)
    {
        anyOn |= r->on;
        anyOff |= ! r->on;
        aFile |= File::isAbsolutePath (r->file) && File (r->file).exists();
    }
    scanAgain.setEnabled (! busy && ! ch.empty());
    turnOff.setEnabled (anyOn);
    turnOn.setEnabled (anyOff && ! busy);
    reveal.setEnabled (ch.size() == 1 && aFile);
    resized();
    repaint (infoArea);
}

void PluginManagerView::selectedRowsChanged (int)
{
    updateButtons();
}

String PluginManagerView::getNameForRow (int row)
{
    if (row < 0 || row >= (int) rows.size()) return {};
    const auto& r = rows[(size_t) row];
    return r.name + ", " + r.format + ", " + status (r);
}

std::vector<int> PluginManagerView::columnEdges (int width) const
{
    std::vector<int> x { 0 };
    float at = 0;
    for (auto s : columnShares) { at += s; x.push_back (roundToInt (at * (float) width)); }
    x.back() = width;
    return x;
}

void PluginManagerView::paintListBoxItem (int row, Graphics& g, int w, int h, bool selected)
{
    if (row < 0 || row >= (int) rows.size()) return;
    const auto& r = rows[(size_t) row];
    if (selected) { g.setColour (theme::rule); g.fillRect (0, 0, w, h); }
    else if (row % 2 == 1) { g.setColour (theme::lane); g.fillRect (0, 0, w, h); }
    const auto x = columnEdges (w);
    const String cells[] { r.name, r.maker.isEmpty() ? ui::u8 ("\xe2\x80\x94") : r.maker, r.format,
                           r.kind.isEmpty() ? ui::u8 ("\xe2\x80\x94") : r.kind, status (r), r.file };
    for (int i = 0; i < 6; ++i)
    {
        Colour col = i == 0 ? theme::text : theme::textSecondary;
        if (i == 4) col = r.on ? theme::textSecondary : r.reason == PluginManager::Reason::TurnedOff ? theme::text : theme::warnText;
        g.setColour (col);
        g.setFont (theme::font (i == 5 ? theme::smallPoints : theme::bodyPoints, i == 4 && ! r.on));
        g.drawText (cells[i], x[(size_t) i] + 8, 0, x[(size_t) i + 1] - x[(size_t) i] - 12, h, Justification::centredLeft, true);
    }
}

void PluginManagerView::mouseDown (const MouseEvent& e)
{
    if (! headerArea.contains (e.getPosition())) return;
    const auto x = columnEdges (headerArea.getWidth());
    const int at = e.x - headerArea.getX();
    for (int i = 0; i < 6; ++i)
        if (at >= x[(size_t) i] && at < x[(size_t) i + 1])
        {
            if (sortColumn == i) sortUp = ! sortUp;
            else { sortColumn = i; sortUp = true; }
            refresh();
            return;
        }
}

void PluginManagerView::paint (Graphics& g)
{
    g.fillAll (theme::ground);

    // What's chosen, or how many there are.
    auto& pm = c.engine.plugins();
    const auto ch = chosen();
    String info;
    if (ch.size() == 1)
    {
        const auto& r = *ch.front();
        info = r.name + ": " + (r.on ? String ("on") : PluginManager::describe (r.reason).toLowerCase())
             + (r.detail.isNotEmpty() ? " (" + r.detail + ")" : String()) + ". " + r.file;
    }
    else if (ch.size() > 1) info = String ((int) ch.size()) + " chosen.";
    else
    {
        const int off = (int) pm.blocked().size();
        info = String (pm.known().getNumTypes()) + " plug-ins on" + (off > 0 ? ", " + String (off) + (off == 1 ? " file" : " files") + " turned off" : String())
             + ". A plug-in that crashes, hangs or won't open while being scanned is turned off here, and Kalibre carries on.";
    }
    g.setColour (theme::textSecondary);
    g.setFont (theme::font (theme::smallPoints));
    g.drawFittedText (info, infoArea, Justification::centredLeft, 2);

    // The column heads: click one to sort by it.
    g.setColour (theme::sunken);
    g.fillRect (headerArea);
    const auto x = columnEdges (headerArea.getWidth());
    g.setFont (theme::font (theme::smallPoints, true));
    for (int i = 0; i < 6; ++i)
    {
        const auto cell = Rectangle<int> (headerArea.getX() + x[(size_t) i] + 8, headerArea.getY(), x[(size_t) i + 1] - x[(size_t) i] - 12, headerArea.getHeight());
        g.setColour (sortColumn == i ? theme::text : theme::textSecondary);
        g.drawText (String (columnNames[i]) + (sortColumn == i ? ui::u8 (sortUp ? " \xe2\x96\xb4" : " \xe2\x96\xbe") : String()), cell, Justification::centredLeft, true);
    }
    g.setColour (theme::rule);
    g.fillRect (headerArea.getX(), headerArea.getBottom() - 1, headerArea.getWidth(), 1);
}

void PluginManagerView::resized()
{
    auto r = getLocalBounds().reduced (theme::px (8), theme::px (6));
    auto bar = r.removeFromTop (theme::px (24));
    auto place = [&] (theme::HouseButton& b) { b.setBounds (bar.removeFromLeft (b.idealWidth (theme::smallControl))); bar.removeFromLeft (theme::px (4)); };
    place (scanNew);
    bar.removeFromLeft (theme::px (8));
    place (scanAgain);
    place (turnOff);
    place (turnOn);
    place (reveal);
    bar.removeFromLeft (theme::px (8));
    place (onlyOff);
    search.setBounds (bar.withWidth (jmin (bar.getWidth(), theme::px (260))));
    r.removeFromTop (theme::px (4));
    infoArea = r.removeFromTop (theme::px (30));
    headerArea = r.removeFromTop (theme::px (22));
    list.setBounds (r);
    // The list's columns line up with the heads, its scroll bar aside.
    headerArea.setWidth (r.getWidth() - (list.getVerticalScrollBar().isVisible() ? list.getVerticalScrollBar().getWidth() : 0));
}

//==============================================================================
PluginManagerWindow::PluginManagerWindow (Controller& c)
    : DocumentWindow ("Plug-in Manager", theme::ground, DocumentWindow::closeButton)
{
    setUsingNativeTitleBar (true);
    setContentOwned (new PluginManagerView (c), false);
    setResizable (true, false);
    setResizeLimits (720, 360, 10000, 10000);
    setSize (1040, 600);
}

bool PluginManagerWindow::keyPressed (const KeyPress& k)
{
    if (k == KeyPress ('w', ModifierKeys::commandModifier, 0) || k.getKeyCode() == KeyPress::escapeKey)
    {
        setVisible (false);
        return true;
    }
    return DocumentWindow::keyPressed (k);
}

} // namespace kal
