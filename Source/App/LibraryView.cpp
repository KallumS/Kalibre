#include "LibraryView.h"

#include "BuiltinInstruments.h"
#include "Edit.h"
#include "Engine.h"

namespace kal
{
using namespace juce;

namespace
{
String u8 (const char* s) { return String::fromUTF8 (s); }

// "0:02.3", "1:04"
String duration (double seconds)
{
    if (seconds < 60) return "0:" + String (seconds, 1).paddedLeft ('0', 4);
    const int s = (int) std::lround (seconds);
    return String (s / 60) + ":" + String (s % 60).paddedLeft ('0', 2);
}

void drawStar (Graphics& g, Rectangle<float> r, bool on)
{
    Path p;
    const float size = jmin (r.getWidth(), r.getHeight()) * 0.42f;
    p.addStar (r.getCentre(), 5, size * 0.45f, size, 0);
    if (on) { g.setColour (theme::accent); g.fillPath (p); }
    else { g.setColour (theme::edge); g.strokePath (p, PathStrokeType (1.2f)); }
}

void drawDisclosure (Graphics& g, Rectangle<float> r, bool open)
{
    Path p;
    const auto c = r.getCentre();
    const float k = jmin (r.getWidth(), r.getHeight()) * 0.22f;
    if (open) p.addTriangle (c.x - k, c.y - k * 0.6f, c.x + k, c.y - k * 0.6f, c.x, c.y + k * 0.8f);
    else p.addTriangle (c.x - k * 0.6f, c.y - k, c.x - k * 0.6f, c.y + k, c.x + k * 0.8f, c.y);
    g.setColour (theme::textSecondary);
    g.fillPath (p);
}

// The waveform, or the notes, of an item, in `r`.
void drawSound (Graphics& g, const library::Item& it, Rectangle<float> r, Colour colour)
{
    g.setColour (colour);
    if (it.kind == library::Kind::Audio)
    {
        if (it.peaks.empty()) return;
        const float mid = r.getCentreY();
        const float w = r.getWidth() / (float) it.peaks.size();
        for (size_t i = 0; i < it.peaks.size(); ++i)
        {
            const float h = jmax (1.0f, (float) it.peaks[i] / 255.0f * r.getHeight() * 0.5f);
            g.fillRect (r.getX() + (float) i * w, mid - h, jmax (1.0f, w), h * 2);
        }
        return;
    }
    if (it.notes.empty() || it.beats <= 0) return;
    int lo = 127, hi = 0;
    for (auto& n : it.notes) { lo = jmin (lo, n.pitch); hi = jmax (hi, n.pitch); }
    const float rowsSpan = (float) jmax (12, hi - lo + 1);
    const float h = jmax (1.5f, r.getHeight() / rowsSpan);
    const double beats = std::ceil (it.beats / 4.0) * 4.0;   // whole bars
    for (auto& n : it.notes)
    {
        const float x = r.getX() + (float) (n.start / beats) * r.getWidth();
        const float wd = jmax (1.5f, (float) (n.length / beats) * r.getWidth() - 0.5f);
        const float y = r.getBottom() - ((float) (n.pitch - lo) + 1.0f) / rowsSpan * r.getHeight();
        g.fillRect (x, y, wd, h);
    }
}

String details (const library::Item& it, const drumsound::Verdict* drum = nullptr)
{
    String s;
    if (it.kind == library::Kind::Midi)
    {
        const int bars = jmax (1, (int) std::ceil (it.beats / 4.0 - 1e-6));
        s << "MIDI " << u8 ("\xc2\xb7") << " " << bars << (bars == 1 ? " bar" : " bars");
    }
    else
    {
        s << duration (it.seconds) << " " << u8 ("\xc2\xb7") << " " << File (it.path).getFileExtension().substring (1).toUpperCase();
    }
    if (it.bpm > 0)
        s << " " << u8 ("\xc2\xb7") << " " << (it.tempoTrusted() ? "" : "about ")
          << String (it.bpm, it.bpm == std::floor (it.bpm) ? 0 : 1) << " BPM";
    // A drum, and which (0106): heard when its name and folders don't say.
    if (drum && drum->drum) s << " " << u8 ("\xc2\xb7") << " " << String (drumsound::nameOf (drum->kind)) << (drum->heard ? " (Heard)" : "");
    return s;
}

// What the scan heard in a sample (0095): how loud, and the note it plays.
String heard (const library::Item& it)
{
    if (it.kind != library::Kind::Audio) return {};
    StringArray parts;
    if (auto loud = library::loudnessText (it); ! loud.empty()) parts.add (String (loud));
    if (auto note = library::noteName (it.note, it.cents); ! note.empty()) parts.add (String (note));
    else if (it.analysis > 0) parts.add ("No One Note");
    if (it.bpmFrom == library::Item::TempoFrom::Heard) parts.add (it.loop ? "Tempo Heard (a Loop)" : "Tempo Heard");
    return parts.joinIntoString (" " + u8 ("\xc2\xb7") + " ");
}
} // namespace

//==============================================================================
// The chosen (or playing) sound, large, with the playhead.
struct LibraryView::Strip : public Component
{
    explicit Strip (LibraryView& v) : view (v) { setTitle ("The chosen sound"); }

    void paint (Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        g.setColour (theme::sunken);
        g.fillRoundedRectangle (r, 4);
        g.setColour (theme::rule);
        g.drawRoundedRectangle (r.reduced (0.5f), 4, 1);
        const auto path = view.chosenPath();
        const auto* it = view.item (path);
        auto inner = r.reduced (8, 6);
        if (! it)
        {
            g.setColour (theme::textSecondary);
            g.setFont (theme::font (theme::smallPoints));
            g.drawFittedText (view.s.autoPlay ? "Click a sound to hear it. Up and Down hear the next."
                                              : "Auto-Play is off: click a sound to choose it, then click here to hear it.",
                              inner.toNearestInt(), Justification::centredLeft, 2);
            return;
        }
        g.setColour (theme::text);
        g.setFont (theme::font (theme::bodyPoints, true));
        g.drawText (File (path).getFileNameWithoutExtension(), inner.removeFromTop ((float) theme::px (16)), Justification::centredLeft, true);
        g.setColour (theme::textSecondary);
        g.setFont (theme::font (theme::smallPoints));
        const auto drum = view.drumOf (*it);
        g.drawText (details (*it, &drum), inner.removeFromTop ((float) theme::px (14)), Justification::centredLeft, true);
        if (const auto h = heard (*it); h.isNotEmpty())
            g.drawText (h, inner.removeFromTop ((float) theme::px (14)), Justification::centredLeft, true);
        inner.removeFromTop (4);
        drawSound (g, *it, inner, theme::control);
        if (view.preview->playingPath() == String (path))
        {
            const double p = view.preview->progress();
            if (p >= 0)
            {
                g.setColour (theme::playhead);
                const float x = inner.getX() + (float) p * inner.getWidth();
                g.fillRect (x, inner.getY(), 1.5f, inner.getHeight());
            }
        }
    }

    // A click on the chosen sound plays it, or stops it (the way to hear one with Auto-Play off).
    void mouseDown (const MouseEvent&) override { view.playChosen(); }

    LibraryView& view;
};

//==============================================================================
struct LibraryView::Model : public ListBoxModel
{
    explicit Model (LibraryView& v) : view (v) {}

    int getNumRows() override { return (int) view.rows.size(); }

    int indent (const library::Row& r) const { return theme::px (6) + r.depth * theme::px (12); }
    int starWidth() const { return theme::px (20); }

    void paintListBoxItem (int row, Graphics& g, int w, int h, bool selected) override
    {
        if (row < 0 || row >= (int) view.rows.size()) return;
        const auto& r = view.rows[(size_t) row];
        if (selected) { g.setColour (theme::rule); g.fillRect (0, 0, w, h); }
        const int x = indent (r);
        const bool fav = view.s.favourites.count (r.path) > 0;
        if (r.type == library::Row::Type::Folder)
        {
            drawDisclosure (g, Rectangle<int> (x, 0, starWidth(), h).toFloat(), r.open);
            g.setColour (theme::text);
            g.setFont (theme::font());
            const int countW = theme::px (40);
            g.drawText (library::nameOf (r.path), x + starWidth(), 0, w - x - starWidth() - countW - starWidth(), h, Justification::centredLeft, true);
            g.setColour (theme::textSecondary);
            g.setFont (theme::font (theme::smallPoints));
            g.drawText (String (r.count), w - countW - starWidth(), 0, countW - 4, h, Justification::centredRight, false);
            if (fav) drawStar (g, Rectangle<int> (w - starWidth(), 0, starWidth(), h).toFloat(), true);
            return;
        }
        const auto* it = view.item (r.path);
        drawStar (g, Rectangle<int> (x, 0, starWidth(), h).toFloat(), library::isFavourite (view.s, r.path));
        const int thumbW = jmin (theme::px (64), w / 4);
        const int textX = x + starWidth() + 2;
        const int textW = w - textX - thumbW - 8;
        g.setColour (theme::text);
        g.setFont (theme::font());
        g.drawText (File (r.path).getFileNameWithoutExtension(), textX, 0, textW, h * 3 / 5, Justification::bottomLeft, true);
        if (it)
        {
            g.setColour (theme::textSecondary);
            g.setFont (theme::font (theme::smallPoints));
            const auto drum = view.drumOf (*it);
            g.drawText (details (*it, &drum), textX, h * 3 / 5, textW, h * 2 / 5, Justification::topLeft, true);
            drawSound (g, *it, Rectangle<int> (w - thumbW - 6, 4, thumbW, h - 8).toFloat(), theme::edge);
        }
    }

    void listBoxItemClicked (int row, const MouseEvent& e) override
    {
        if (row < 0 || row >= (int) view.rows.size()) return;
        const auto r = view.rows[(size_t) row];
        if (e.mods.isPopupMenu())
        {
            view.listBox.selectRow (row);
            if (r.type == library::Row::Type::Folder) view.folderMenu (row);
            else view.itemMenu (row);
            return;
        }
        const int x = indent (r);
        if (r.type == library::Row::Type::Folder)
        {
            if (e.x >= e.eventComponent->getWidth() - starWidth() && view.s.favourites.count (r.path) > 0) { view.toggleFavourite (r.path); return; }
            view.toggleOpen (row);
            return;
        }
        if (e.x >= x && e.x < x + starWidth()) { view.toggleFavourite (r.path); return; }
        view.hear (row);
    }

    void listBoxItemDoubleClicked (int row, const MouseEvent& e) override
    {
        if (row < 0 || row >= (int) view.rows.size() || e.mods.isPopupMenu()) return;
        const auto& r = view.rows[(size_t) row];
        if (r.type == library::Row::Type::Item && e.x >= indent (r) + starWidth()) view.putAtCursor (r.path);
    }

    void returnKeyPressed (int row) override
    {
        if (row < 0 || row >= (int) view.rows.size()) return;
        if (view.rows[(size_t) row].type == library::Row::Type::Item) view.putAtCursor (view.rows[(size_t) row].path);
        else view.toggleOpen (row);
    }

    var getDragSourceDescription (const SparseSet<int>& rows) override
    {
        if (rows.isEmpty() || rows[0] >= (int) view.rows.size()) return {};
        const auto& r = view.rows[(size_t) rows[0]];
        return r.type == library::Row::Type::Item ? var ("file:" + String (r.path)) : var();
    }

    String getTooltipForRow (int row) override
    {
        return row >= 0 && row < (int) view.rows.size() ? String (view.rows[(size_t) row].path) : String();
    }

    String getNameForRow (int row) override
    {
        return row >= 0 && row < (int) view.rows.size() ? String (library::nameOf (view.rows[(size_t) row].path)) : String();
    }

    LibraryView& view;
};

//==============================================================================
// Up and Down hear the next sound; Left and Right shut and open folders.
struct LibraryView::List : public ListBox
{
    explicit List (LibraryView& v) : ListBox ("Sounds", nullptr), view (v) {}

    bool keyPressed (const KeyPress& k) override
    {
        const int code = k.getKeyCode();
        const int row = getSelectedRow();
        if ((code == KeyPress::leftKey || code == KeyPress::rightKey) && row >= 0 && row < (int) view.rows.size())
        {
            const auto& r = view.rows[(size_t) row];
            if (r.type == library::Row::Type::Folder && r.open == (code == KeyPress::leftKey)) { view.toggleOpen (row); return true; }
            if (r.type == library::Row::Type::Item && code == KeyPress::leftKey)
            {
                // To its folder.
                for (int i = row - 1; i >= 0; --i)
                    if (view.rows[(size_t) i].type == library::Row::Type::Folder && view.rows[(size_t) i].depth < r.depth) { selectRow (i); return true; }
            }
            return true;
        }
        const bool moves = code == KeyPress::upKey || code == KeyPress::downKey || code == KeyPress::pageUpKey
                        || code == KeyPress::pageDownKey || code == KeyPress::homeKey || code == KeyPress::endKey;
        const bool used = ListBox::keyPressed (k);
        if (moves && used)
        {
            const int now = getSelectedRow();
            if (now >= 0 && now < (int) view.rows.size() && view.rows[(size_t) now].type == library::Row::Type::Item) view.hear (now);
        }
        return used;
    }

    LibraryView& view;
};

std::unique_ptr<ListBox> LibraryView::makeList() { return std::make_unique<List> (*this); }

//==============================================================================
File LibraryView::defaultStore()
{
    return File::getSpecialLocation (File::userApplicationDataDirectory)
           #if JUCE_MAC
               .getChildFile ("Application Support")
           #endif
               .getChildFile ("Kalibre");
}

LibraryView::LibraryView (Controller& ctrl)
    : c (ctrl), listPtr (makeList()), listBox (*listPtr)
{
    setTitle ("Sounds");
    preview = std::make_unique<PreviewPlayer> (c.engine);
    preview->nextBeat = [this] { return PreviewPlayer::secondsToNextBeat (c.engine, c.project.tempo); };
    strip = std::make_unique<Strip> (*this);
    addAndMakeVisible (*strip);
    model = std::make_unique<Model> (*this);
    listBox.setModel (model.get());
    listBox.setRowHeight (theme::px (34));
    listBox.setTitle ("Sounds");
    listBox.setColour (ListBox::backgroundColourId, theme::sunken);
    listBox.setColour (ListBox::outlineColourId, theme::rule);
    listBox.setOutlineThickness (1);
    addAndMakeVisible (listBox);

    for (auto* b : { &foldersView, &flatView })
    {
        b->setRadioGroupId (41);
        b->setClickingTogglesState (true);
    }
    for (auto* b : { &foldersView, &flatView, &favouritesOnly, &drumsButton, &orderButton, &syncButton, &autoPlayButton, &editFolders })
        addAndMakeVisible (b);
    addChildComponent (chooseButton);
    foldersView.onClick = [this] { if (s.flat) { s.flat = false; saveSettings(); refreshRows(); } };
    flatView.onClick = [this] { if (! s.flat) { s.flat = true; saveSettings(); refreshRows(); } };
    favouritesOnly.onClick = [this] { s.favouritesOnly = favouritesOnly.getToggleState(); saveSettings(); refreshRows(); };
    syncButton.onClick = [this] { s.sync = syncButton.getToggleState(); saveSettings(); };
    autoPlayButton.onClick = [this]
    {
        s.autoPlay = autoPlayButton.getToggleState();
        if (! s.autoPlay) preview->stop();
        saveSettings();
        strip->repaint();
    };
    orderButton.onClick = [this] { orderMenu(); };
    drumsButton.setClickingTogglesState (false);
    drumsButton.onClick = [this] { drumsMenu(); };
    editFolders.onClick = [this] { if (onEditFolders) onEditFolders(); else chooseFolders(); };
    chooseButton.onClick = [this] { chooseFolders(); };

    useStore (defaultStore());
}

LibraryView::~LibraryView()
{
    stopTimer();
    listBox.setModel (nullptr);
}

void LibraryView::useStore (const File& folder)
{
    store = folder;
    s = {};
    const auto settingsFile = store.getChildFile ("Library Settings.json");
    if (settingsFile.existsAsFile()) library::fromJson (settingsFile.loadFileAsString().toStdString(), s);
    scan = std::make_unique<LibraryScanner> (store.getChildFile ("Library Index.json"));
    scan->onChange = [this] { refreshRows(); };
    refreshRows();
    if (! s.folders.empty()) scan->scan (s.folders);
    startTimerHz (10);
}

void LibraryView::saveSettings()
{
    store.createDirectory();
    store.getChildFile ("Library Settings.json").replaceWithText (library::toJson (s));
}

void LibraryView::settingsChanged()
{
    saveSettings();
    refreshRows();
    rescan();
}

void LibraryView::rescan()
{
    scan->scan (s.folders);
    repaint();
}

void LibraryView::setFilter (const String& text)
{
    filter = text;
    refreshRows();
}

const library::Item* LibraryView::item (const std::string& path) const
{
    const auto& items = scan->index().items;
    const auto it = items.find (path);
    return it != items.end() ? &it->second : nullptr;
}

std::string LibraryView::chosenPath() const
{
    const int row = listBox.getSelectedRow();
    if (row >= 0 && row < (int) rows.size() && rows[(size_t) row].type == library::Row::Type::Item) return rows[(size_t) row].path;
    return preview->playingPath().toStdString();
}

// Drums (0106): every sound, every drum, or one kind of drum.
void LibraryView::drumsMenu()
{
    PopupMenu m;
    m.addItem ("All Sounds", true, ! s.drumsOnly, [this] { s.drumsOnly = false; saveSettings(); refreshRows(); });
    m.addItem ("All Drums", true, s.drumsOnly && s.drumKind < 0, [this] { s.drumsOnly = true; s.drumKind = -1; saveSettings(); refreshRows(); });
    m.addSeparator();
    for (auto k : drumsound::kinds())
        m.addItem (String (drumsound::nameOf (k)) + (k == drumsound::Kind::Percussion ? String() : String ("s")), true, s.drumsOnly && s.drumKind == (int) k,
                   [this, k] { s.drumsOnly = true; s.drumKind = (int) k; saveSettings(); refreshRows(); });
    m.showMenuAsync (PopupMenu::Options().withTargetComponent (&drumsButton));
}

void LibraryView::refreshRows()
{
    const auto chosen = listBox.getSelectedRow() >= 0 && listBox.getSelectedRow() < (int) rows.size() ? rows[(size_t) listBox.getSelectedRow()].path : std::string();
    rows = library::rows (scan->index(), s, filter.toStdString(), &drumCache);
    listBox.updateContent();
    // The same one stays chosen, wherever it is now.
    int again = -1;
    for (size_t i = 0; i < rows.size(); ++i) if (rows[i].path == chosen) { again = (int) i; break; }
    if (again >= 0) listBox.selectRow (again, true, true);
    else listBox.deselectAllRows();
    foldersView.setToggleState (! s.flat, dontSendNotification);
    flatView.setToggleState (s.flat, dontSendNotification);
    favouritesOnly.setToggleState (s.favouritesOnly, dontSendNotification);
    drumsButton.setToggleState (s.drumsOnly, dontSendNotification);
    drumsButton.setButtonText ((s.drumsOnly && s.drumKind >= 0 ? String (drumsound::nameOf ((drumsound::Kind) s.drumKind)) : String ("Drums")) + u8 (" \xe2\x96\xbe"));
    syncButton.setToggleState (s.sync, dontSendNotification);
    autoPlayButton.setToggleState (s.autoPlay, dontSendNotification);
    orderButton.setButtonText (s.shuffled ? u8 ("Shuffled \xe2\x96\xbe") : u8 ("A to Z \xe2\x96\xbe"));
    chooseButton.setVisible (s.folders.empty());
    listBox.repaint();
    strip->repaint();
    repaint();
}

double LibraryView::songBpm() const
{
    const Tick at = c.engine.isPlaying() ? c.project.tempo.tickAt (c.engine.position()) : c.cursor;
    return c.project.tempo.bpmAt (at);
}

void LibraryView::hear (int row, bool play)
{
    if (row < 0 || row >= (int) rows.size() || rows[(size_t) row].type != library::Row::Type::Item) return;
    if (listBox.getSelectedRow() != row) listBox.selectRow (row);
    if (s.autoPlay || play)
    {
        if (const auto* it = item (rows[(size_t) row].path)) preview->play (*it, s.sync, songBpm());
    }
    else if (preview->isPlaying() && preview->playingPath() != String (rows[(size_t) row].path)) preview->stop();
    strip->repaint();
}

void LibraryView::playChosen()
{
    const auto path = chosenPath();
    if (path.empty()) return;
    if (preview->isPlaying() && preview->playingPath() == String (path)) { preview->stop(); strip->repaint(); return; }
    for (size_t i = 0; i < rows.size(); ++i)
        if (rows[i].type == library::Row::Type::Item && rows[i].path == path) { hear ((int) i, true); return; }
}

void LibraryView::toggleOpen (int row)
{
    if (row < 0 || row >= (int) rows.size() || rows[(size_t) row].type != library::Row::Type::Folder) return;
    const auto path = rows[(size_t) row].path;
    if (rows[(size_t) row].open) s.open.erase (path);
    else s.open.insert (path);
    saveSettings();
    refreshRows();
    for (size_t i = 0; i < rows.size(); ++i) if (rows[i].path == path) { listBox.selectRow ((int) i); break; }
}

void LibraryView::toggleFavourite (const std::string& path)
{
    if (s.favourites.count (path) > 0) s.favourites.erase (path);
    else s.favourites.insert (path);
    saveSettings();
    refreshRows();
}

void LibraryView::orderMenu()
{
    PopupMenu m;
    m.addItem ("A to Z", true, ! s.shuffled, [this] { s.shuffled = false; saveSettings(); refreshRows(); });
    m.addItem (s.shuffled ? "Shuffle Again" : "Shuffle", true, s.shuffled, [this]
    {
        s.shuffled = true;
        s.seed = (std::uint32_t) Random::getSystemRandom().nextInt() | 1u;
        saveSettings();
        refreshRows();
        c.say (s.flat ? "Shuffled: every sound in a new order." : "Shuffled: the sounds in each folder in a new order.");
    });
    m.showMenuAsync (PopupMenu::Options().withTargetComponent (&orderButton));
}

void LibraryView::itemMenu (int row)
{
    const auto path = rows[(size_t) row].path;
    const auto* it = item (path);
    if (! it) return;
    const bool midi = it->kind == library::Kind::Midi;
    const auto* focus = c.focus();
    PopupMenu m;
    if (midi)
    {
        m.addItem ("New Track with Kalibre Synth", [this, path] { newTrackFrom (path, NewTrack::Synth); });
        m.addItem ("New Track with Kalibre Sampler", [this, path] { newTrackFrom (path, NewTrack::Sampler); });
    }
    else
    {
        m.addItem ("New Track with Kalibre Sampler", [this, path] { newTrackFrom (path, NewTrack::Sampler); });
        m.addItem ("New Track with the Sample", [this, path] { newTrackFrom (path, NewTrack::Clip); });
    }
    m.addItem ("Put on the Chosen Track at the Cursor", focus != nullptr && focus->holdsClips(), false, [this, path] { putAtCursor (path); });
    if (! library::isMidiExtension (library::extensionOf (path)))
        m.addItem ("Put on the Chosen Track, Split at Transients", focus != nullptr && focus->holdsClips(), false, [this, path]
        {
            preview->stop();
            c.importSplit (File (path), c.focusTrack, c.cursor);
        });
    m.addItem ("Play", [this, row] { hear (row, true); });
    m.addSeparator();
    const bool fav = s.favourites.count (path) > 0;
    m.addItem (fav ? "Remove from Favourites" : "Add to Favourites", [this, path] { toggleFavourite (path); });
    m.addItem (u8 ("Show in Finder"), [path] { File (path).revealToUser(); });
    m.showMenuAsync (PopupMenu::Options());
}

void LibraryView::folderMenu (int row)
{
    const auto path = rows[(size_t) row].path;
    PopupMenu m;
    const bool fav = s.favourites.count (path) > 0;
    m.addItem (fav ? "Remove from Favourites" : "Add to Favourites", [this, path] { toggleFavourite (path); });
    m.addItem (rows[(size_t) row].open ? "Close" : "Open", [this, row] { toggleOpen (row); });
    m.addItem ("Show in Finder", [path] { File (path).revealToUser(); });
    m.showMenuAsync (PopupMenu::Options());
}

bool LibraryView::putAtCursor (const std::string& path)
{
    preview->stop();
    return c.importFile (File (path), c.focusTrack, c.cursor);
}

bool LibraryView::newTrackFrom (const std::string& path, NewTrack how)
{
    const File f (path);
    const auto* it = item (path);
    if (! it) return false;
    preview->stop();
    const String name = f.getFileNameWithoutExtension();
    Id made = 0;
    if (how == NewTrack::Sampler && it->kind == library::Kind::Audio)
    {
        // The sampler, the sample loaded in it, on a new track: one undo step.
        KalibreSampler sampler;
        String error;
        if (! sampler.load (f, error)) { c.say ("Kalibre Sampler couldn't load " + f.getFileName() + ": " + error, true); return false; }
        MemoryBlock state;
        sampler.getStateInformation (state);
        const auto encoded = state.toBase64Encoding().toStdString();
        c.apply ("New Track with Kalibre Sampler", [&] (Project& p)
        {
            made = edit::addTrack (p, TrackKind::Track, name.toStdString());
            const Id d = edit::addDevice (p, made, samplerRef(), DeviceRole::Instrument);
            if (auto* dev = p.device (d)) dev->state = encoded;
        });
    }
    else
    {
        // A clip on a new track, its instrument added in the same undo step.
        const auto ref = how == NewTrack::Synth ? synthRef() : samplerRef();
        const bool instrument = how != NewTrack::Clip;
        const String label = how == NewTrack::Synth ? "New Track with Kalibre Synth" : how == NewTrack::Sampler ? "New Track with Kalibre Sampler" : "New Track with the Sample";
        if (! c.importFile (f, 0, c.cursor, [&] (Project& p)
            {
                made = edit::addTrack (p, TrackKind::Track, name.toStdString());
                if (instrument) edit::addDevice (p, made, ref, DeviceRole::Instrument);
                return made;
            }, label))
            return false;
    }
    if (made == 0 || ! c.project.track (made)) return false;
    c.focusOn (made);
    c.selectedTracks = { made };
    if (how != NewTrack::Clip) c.zoneTab = ZoneTab::Devices;
    c.viewChanged();
    return true;
}

//==============================================================================
void LibraryView::chooseFolders (std::function<void()> then)
{
    chooser = std::make_unique<FileChooser> ("Choose the folders with your samples and MIDI files", File::getSpecialLocation (File::userMusicDirectory));
    chooser->launchAsync (FileBrowserComponent::openMode | FileBrowserComponent::canSelectDirectories | FileBrowserComponent::canSelectMultipleItems,
                          [this, then] (const FileChooser& fc)
    {
        int added = 0;
        for (auto& f : fc.getResults())
        {
            if (! f.isDirectory()) continue;
            const auto path = f.getFullPathName().toStdString();
            if (std::find (s.folders.begin(), s.folders.end(), path) != s.folders.end()) continue;
            s.folders.push_back (path);
            s.open.insert (path);
            ++added;
        }
        s.asked = true;
        if (added > 0)
        {
            settingsChanged();
            c.say (added == 1 ? "Looking through the folder for sounds: they appear in the browser's Sounds tab as they're found."
                              : "Looking through " + String (added) + " folders for sounds: they appear in the browser's Sounds tab as they're found.");
        }
        else saveSettings();
        if (then) then();
    });
}

void LibraryView::askOnFirstRun (std::function<void()> chose)
{
    if (s.asked || ! s.folders.empty()) return;
    auto* w = new AlertWindow ("Where are your samples and MIDI files?",
                               "Kalibre lists the sounds in the folders you choose, in the browser's Sounds tab, to hear and drag into songs. "
                               "Only those folders are looked through. You can change them any time from the Kalibre menu, under Library Folders.",
                               MessageBoxIconType::NoIcon);
    w->addButton (u8 ("Choose Folders\xe2\x80\xa6"), 1, KeyPress (KeyPress::returnKey));
    w->addButton ("Not Now", 0, KeyPress (KeyPress::escapeKey));
    ui::present (w, ModalCallbackFunction::create ([this, chose] (int r)
    {
        s.asked = true;
        saveSettings();
        if (r == 1) chooseFolders (chose);
    }), true);
}

String LibraryView::status() const
{
    if (s.folders.empty()) return "No folders chosen yet.";
    if (scan->openingIndex()) return "Opening the list of your sounds" + u8 ("\xe2\x80\xa6");
    if (scan->scanning())
        return scan->filesToRead() > 0 ? "Reading new sounds: " + String (scan->filesRead()) + " of " + String (scan->filesToRead()) + u8 ("\xe2\x80\xa6")
                                       : "Checking " + String (scan->filesFound()) + " sounds for changes" + u8 ("\xe2\x80\xa6");
    int items = 0;
    for (auto& r : rows) if (r.type == library::Row::Type::Item) ++items;
    int total = 0;
    for (auto& [p, it] : scan->index().items) if (it.readable) ++total;
    const String folders = String ((int) s.folders.size()) + ((int) s.folders.size() == 1 ? " folder" : " folders");
    if (filter.isNotEmpty() || s.favouritesOnly || s.drumsOnly) return String (items) + " of " + String (total) + " sounds";
    return String (total) + " sounds in " + folders;
}

void LibraryView::timerCallback()
{
    const auto now = status();
    if (now != lastStatus) { lastStatus = now; repaint(); }
    if (preview->progress() >= 0 || preview->isPlaying()) strip->repaint();
}

void LibraryView::paint (Graphics& g)
{
    g.setColour (theme::textSecondary);
    g.setFont (theme::font (theme::smallPoints));
    auto r = getLocalBounds();
    const int top = editFolders.getBottom() + theme::px (4);
    g.drawText (lastStatus.isEmpty() ? status() : lastStatus, r.getX(), top, r.getWidth(), theme::px (16), Justification::centredLeft, true);
    if (s.folders.empty())
    {
        g.setColour (theme::text);
        g.setFont (theme::font());
        g.drawFittedText ("Choose the folders with your samples and MIDI files: Kalibre lists every sound in them, to hear and drag into songs.",
                          listBox.getBounds().reduced (theme::px (10)).withHeight (theme::px (60)), Justification::topLeft, 4);
    }
}

void LibraryView::resized()
{
    auto r = getLocalBounds();
    auto row = [&] (std::initializer_list<theme::HouseButton*> buttons)
    {
        auto line = r.removeFromTop (theme::px (24));
        const int gap = 4;
        const int w = (line.getWidth() - gap * ((int) buttons.size() - 1)) / (int) buttons.size();
        for (auto* b : buttons) { b->setBounds (line.removeFromLeft (w)); line.removeFromLeft (gap); }
        r.removeFromTop (4);
    };
    row ({ &foldersView, &flatView, &favouritesOnly, &drumsButton });
    row ({ &orderButton, &syncButton, &autoPlayButton, &editFolders });
    r.removeFromTop (theme::px (20));   // the status line
    strip->setBounds (r.removeFromTop (theme::px (106)));
    r.removeFromTop (theme::px (6));
    listBox.setBounds (r);
    if (chooseButton.isVisible())
        chooseButton.setBounds (listBox.getBounds().reduced (theme::px (10)).withTrimmedTop (theme::px (66)).withHeight (theme::px (26))
                                    .withWidth (chooseButton.idealWidth (theme::smallControl)));
}

} // namespace kal
