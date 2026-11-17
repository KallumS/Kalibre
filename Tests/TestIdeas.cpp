// Good Idea's engine, run in Kalibre for the Random buttons (decision 0062).
#include "Check.h"
#include "Ideas.h"
#include "Harmony.h"

#include <algorithm>
#include <set>

using namespace kal;

TEST ("ideas: Good Idea's drums: General MIDI, a kick on every downbeat, as long as asked; a new groove for a new number")
{
    std::set<std::vector<int>> grooves;
    for (int seed = 1; seed <= 30; ++seed)
    {
        const auto idea = ideas::drums (2, 4, 4, seed);
        CHECK (idea.error.empty());
        if (! idea.error.empty()) { std::printf ("    %s\n", idea.error.c_str()); return; }
        CHECK (! idea.notes.empty());
        CHECK_EQ (idea.length, Tick (PPQ * 8));
        CHECK (idea.name.find ("Good Idea " + std::to_string (seed)) == 0);
        for (auto& n : idea.notes)
        {
            CHECK (n.pitch >= 27 && n.pitch <= 87);    // General MIDI's drums
            CHECK (n.start >= 0 && n.start < idea.length);
        }
        for (Tick bar : { Tick (0), Tick (PPQ * 4) })
            CHECK (std::any_of (idea.notes.begin(), idea.notes.end(), [bar] (const Note& n) { return n.start == bar && (n.pitch == 35 || n.pitch == 36); }));
        std::vector<int> shape;
        for (auto& n : idea.notes) { shape.push_back ((int) n.start); shape.push_back (n.pitch); }
        grooves.insert (shape);
    }
    CHECK (grooves.size() > 20);
    // The same number, the same idea.
    const auto a = ideas::drums (4, 4, 4, 4321), b = ideas::drums (4, 4, 4, 4321);
    CHECK (a.notes == b.notes);
}

TEST ("ideas: Good Idea's motif: in the song's key, as long as asked; a new tune for a new number")
{
    ScaleChoice key;
    key.root = 10;   // G
    key.scale = 0;   // major
    std::set<std::vector<int>> tunes;
    for (int seed = 1; seed <= 30; ++seed)
    {
        const auto idea = ideas::motif (key, 2, 4, 4, seed);
        CHECK (idea.error.empty());
        if (! idea.error.empty()) { std::printf ("    %s\n", idea.error.c_str()); return; }
        CHECK (! idea.notes.empty());
        CHECK_EQ (idea.length, Tick (PPQ * 8));
        std::vector<int> shape;
        int outside = 0;
        for (auto& n : idea.notes)
        {
            if (! harmony::inScale (key, n.pitch)) ++outside;
            CHECK (n.start >= 0 && n.start < idea.length);
            shape.push_back ((int) n.start);
            shape.push_back (n.pitch);
        }
        CHECK (outside * 5 <= (int) idea.notes.size());   // a passing note out of the key now and then, no more
        tunes.insert (shape);
    }
    CHECK (tunes.size() > 25);
}
