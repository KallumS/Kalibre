#include "Check.h"
#include "ActivityLog.h"
#include "Edit.h"

using namespace kal;

TEST ("activity log: each thing done is a line, when and what, oldest first; taking them leaves none to write twice")
{
    activity::Log log;
    log.add ("2026-11-25 10:00:01", activity::Kind::New, "A new song");
    log.add ("2026-11-25 10:00:05", activity::Kind::Edit, "Add Note");
    log.add ("2026-11-25 10:00:09", activity::Kind::Agent, "Add Notes");
    const auto text = log.take();
    CHECK (text.find ("2026-11-25 10:00:01  New       A new song\n") != std::string::npos);
    CHECK (text.find ("2026-11-25 10:00:05  Edit      Add Note\n") != std::string::npos);
    CHECK (text.find ("AI agent") != std::string::npos);
    CHECK (text.find ("A new song") < text.find ("Add Note"));
    CHECK (log.empty());
    CHECK (log.take().empty());
}

TEST ("activity log: a message said again straight after isn't written twice; one in between is")
{
    activity::Log log;
    log.add ("t1", activity::Kind::Message, "Saved Song.");
    log.add ("t2", activity::Kind::Message, "Saved Song.");
    log.add ("t3", activity::Kind::Edit, "Mix");
    log.add ("t4", activity::Kind::Message, "Saved Song.");
    const auto text = log.take();
    size_t n = 0;
    for (size_t at = text.find ("Saved Song."); at != std::string::npos; at = text.find ("Saved Song.", at + 1)) ++n;
    CHECK_EQ (n, size_t (2));
}

TEST ("activity log: the file's heading says what it is, for which song, from which Kalibre, in what time")
{
    const auto h = activity::heading ("My Song", "1.2.3", "UTC+01:00");
    CHECK (h.find ("My Song") != std::string::npos);
    CHECK (h.find ("Kalibre 1.2.3") != std::string::npos);
    CHECK (h.find ("UTC+01:00") != std::string::npos);
}

TEST ("activity log: the undo history tells it each step done, undone and redone, by its name")
{
    edit::History h;
    std::vector<std::string> told;
    h.onStep = [&] (edit::History::Step s, const std::string& label)
    {
        told.push_back ((s == edit::History::Step::Done ? "do " : s == edit::History::Step::Undone ? "undo " : "redo ") + label);
    };
    Project p;
    h.push (p, "Add Track");
    h.undo (p);
    h.redo (p);
    CHECK_EQ (told.size(), size_t (3));
    if (told.size() == 3)
    {
        CHECK_EQ (told[0], std::string ("do Add Track"));
        CHECK_EQ (told[1], std::string ("undo Add Track"));
        CHECK_EQ (told[2], std::string ("redo Add Track"));
    }
}
