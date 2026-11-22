// DAW Battle Pass inside Kalibre (decision 0111): the musician's script, unchanged,
// on Kalibre's ReaImGui and a `reaper` answered from the song.
#include "Check.h"
#include "BattlePass.h"
#include "Edit.h"
#include "MainComponent.h"

using namespace kal;
using namespace juce;

namespace
{
// The game's state, from the script's own locals: `update`'s upvalues.
const char* reach = R"(
    local function up(fn, name)
        for i = 1, 300 do
            local n, v = debug.getupvalue(fn, i)
            if not n then return nil end
            if n == name then return v end
        end
    end
    local loop = debug.getregistry().kal_deferred
    local update = up(loop, "update")
    st, ss, W, G, save, ui = up(update, "st"), up(update, "ss"), up(update, "W"), up(update, "G"), up(update, "save"), up(update, "ui")
    return tostring(st ~= nil and W ~= nil and save ~= nil)
)";

struct Rig
{
    Rig()
    {
        folder = File::getSpecialLocation (File::tempDirectory).getChildFile ("kalibre-battle-pass-" + String (Random::getSystemRandom().nextInt (1 << 30)));
        folder.deleteRecursively();
        BattlePass::resourceFolder() = folder.getChildFile ("Kalibre");
        BattlePass::reaperFolder() = folder.getChildFile ("REAPER");
    }
    ~Rig() { folder.deleteRecursively(); }
    File folder;
};
} // namespace

TEST ("battle pass: the whole script runs inside Kalibre, its pages drawn, nothing missing from ReaImGui or reaper (0111)")
{
    Rig rig;
    PluginManager plugins;
    Engine engine (plugins);
    Controller c (engine);
    c.project = Project();
    BattlePass bp (c);
    for (int i = 0; i < 3; ++i) bp.frameNow();
    CHECK_EQ (bp.error(), String());
    CHECK_EQ (bp.run (reach), String ("true"));
    // Every page drawn, each without an error.
    for (auto* page : { "pass", "quests", "shop", "gear", "loot", "ideas", "trophies", "stats" })
    {
        bp.run (String ("ui.modals = {}; ui.tab = '") + page + "'; return 'ok'");
        bp.frameNow();
        bp.frameNow();
        CHECK_EQ (bp.error(), String());
    }
    // Saved in Kalibre's folder, as REAPER's Data folder.
    bp.run ("save(); return 'saved'");
    CHECK (rig.folder.getChildFile ("Kalibre/Data/DAW Battle Pass/progress.lua").existsAsFile());
}

TEST ("battle pass: a new track and notes in the song are seen, and earn XP (0111)")
{
    Rig rig;
    PluginManager plugins;
    Engine engine (plugins);
    Controller c (engine);
    c.project = Project();
    BattlePass bp (c);
    bp.frameNow();
    CHECK_EQ (bp.run (reach), String ("true"));
    const auto tracksBefore = bp.run ("return st.stats.tracks").getIntValue();
    const auto notesBefore = bp.run ("return st.stats.notes").getIntValue();
    // A track with a two-note clip, as the musician would make them.
    c.apply ("Add Track", [] (Project& p)
    {
        const Id t = edit::addTrack (p, TrackKind::Track, "Keys");
        Clip cl;
        cl.kind = Clip::Kind::Midi;
        const Id clip = edit::addClip (p, t, cl);
        Note n;
        edit::addNote (p, clip, n);
        n.start = PPQ;
        edit::addNote (p, clip, n);
    });
    // The watcher looks at most twice a second.
    MessageManager::getInstance()->runDispatchLoopUntil (700);
    bp.frameNow();
    CHECK_EQ (bp.error(), String());
    CHECK_EQ (bp.run ("return st.stats.tracks").getIntValue(), tracksBefore + 1);
    CHECK_EQ (bp.run ("return st.stats.notes").getIntValue(), notesBefore + 2);
}

TEST ("battle pass: an idea won goes into the song at the cursor as one undo step, on the chosen track (0111)")
{
    Rig rig;
    PluginManager plugins;
    Engine engine (plugins);
    Controller c (engine);
    c.project = Project();
    const Id t = edit::addTrack (c.project, TrackKind::Track, "Piano");
    c.focusTrack = t;
    c.cursor = PPQ * 4;
    BattlePass bp (c);
    bp.frameNow();
    CHECK_EQ (bp.run (reach), String ("true"));
    const auto steps = c.history.size();
    CHECK_EQ (bp.run ("return tostring(W.insert({ name = 'Chords: Test', beats = 4, notes = { "
                      "{ start = 0, len = 1, pitch = 60, vel = 90 }, { start = 1, len = 1, pitch = 64, vel = 90 } } }))"),
              String ("0"));   // W.OK
    CHECK_EQ (c.history.size(), steps + 1);
    const auto* track = c.project.track (t);
    CHECK (track != nullptr);
    if (! track) return;
    CHECK_EQ (track->clips().size(), size_t (1));
    if (track->clips().empty()) return;
    const auto& clip = track->clips().front();
    CHECK_EQ (clip.start, PPQ * 4);
    CHECK_EQ (clip.length, PPQ * 4);
    CHECK_EQ (clip.name, std::string ("Chords: Test"));
    CHECK_EQ (clip.midi.notes.size(), size_t (2));
    if (clip.midi.notes.size() == 2)
    {
        CHECK_EQ (clip.midi.notes[0].start, Tick (0));
        CHECK_EQ (clip.midi.notes[1].start, PPQ);
        CHECK_EQ (clip.midi.notes[1].pitch, 64);
    }
    // An arrangement as sections, and a tempo.
    bp.run ("W.addRegions({ { 'Intro', 4 }, { 'Verse', 8 } }); W.setTempo(97); return 'ok'");
    CHECK_EQ (c.project.sections.size(), size_t (2));
    CHECK (std::abs (c.project.tempo.bpmAt (0) - 97.0) < 1.0e-9);
    c.undo();
    CHECK (std::abs (c.project.tempo.bpmAt (0) - 97.0) > 1.0);
}

TEST ("battle pass: progress saved by REAPER's copy is brought over the first time, never over Kalibre's own (0111)")
{
    Rig rig;
    PluginManager plugins;
    Engine engine (plugins);
    Controller c (engine);
    c.project = Project();
    // A first run makes a save to borrow from.
    String saved;
    {
        BattlePass bp (c);
        bp.frameNow();
        bp.run (reach);
        bp.run ("st.coins = 4321; save(); return 'ok'");
        saved = rig.folder.getChildFile ("Kalibre/Data/DAW Battle Pass/progress.lua").loadFileAsString();
    }
    CHECK (saved.contains ("4321"));
    // As if it were REAPER's, and Kalibre had none.
    auto reapers = rig.folder.getChildFile ("REAPER/Data/DAW Battle Pass");
    reapers.createDirectory();
    reapers.getChildFile ("progress.lua").replaceWithText (saved);
    rig.folder.getChildFile ("Kalibre").deleteRecursively();
    {
        BattlePass bp (c);
        bp.frameNow();
        bp.run (reach);
        CHECK_EQ (bp.run ("return st.coins").getIntValue(), 4321);
        bp.run ("st.coins = 5; save(); return 'ok'");
    }
    // Kalibre's own is kept from then on.
    {
        BattlePass bp (c);
        bp.frameNow();
        bp.run (reach);
        CHECK_EQ (bp.run ("return st.coins").getIntValue(), 5);
    }
}

TEST ("battle pass: seven left clicks in a row on the Making Music timer open it; a pause starts the count again (0111)")
{
    SecretClicks s;
    juce::uint32 t = 1000;
    for (int i = 0; i < 6; ++i) CHECK (! s.click (t += 300));
    CHECK (s.click (t += 300));
    // A pause of more than a second and a half: counting starts again.
    for (int i = 0; i < 5; ++i) CHECK (! s.click (t += 300));
    t += 2000;
    for (int i = 0; i < 6; ++i) CHECK (! s.click (t += 300));
    CHECK (s.click (t += 300));

    Rig rig;
    theme::LookAndFeel look;
    LookAndFeel::setDefaultLookAndFeel (&look);
    {
        PluginManager plugins;
        Engine engine (plugins);
        Controller c (engine);
        c.project = Project();
        MainComponent w (c);
        w.setSize (1440, 900);
        w.startBattlePass (0);
        CHECK (w.battlePass != nullptr);
        if (w.battlePass)
        {
            CHECK (! w.battlePass->isShown());
            auto& timer = w.topBar().timerReadout();
            auto click = [&]
            {
                const auto at = timer.getLocalBounds().getCentre().toFloat();
                MouseEvent e (Desktop::getInstance().getMainMouseSource(), at, ModifierKeys (ModifierKeys::leftButtonModifier), 1.0f,
                              0, 0, 0, 0, &timer, &timer, Time::getCurrentTime(), at, Time::getCurrentTime(), 1, false);
                timer.mouseDown (e);
            };
            for (int i = 0; i < 6; ++i) click();
            CHECK (! w.battlePass->isShown());
            click();
            CHECK (w.battlePass->isShown());
            if (auto* win = w.battlePass->window()) CHECK_EQ (win->getName(), String ("DAW Battle Pass"));
            w.battlePass->hide();
            CHECK (! w.battlePass->isShown());
        }
    }
    LookAndFeel::setDefaultLookAndFeel (nullptr);
}

