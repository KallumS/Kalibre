/*
    CrashContext - what Kalibre was doing, for the crash report (decision 0073).

    The audio thread notes the plug-in it's inside, as each one processes
    (TimedPlugin): if Kalibre crashes there, the report names it. Plain
    atomics of pointers to names that live as long as the plug-in.
*/

#pragma once

#include <atomic>

namespace kal::crashcontext
{
inline std::atomic<const char*> audioPlugin { nullptr };   // the plug-in the audio thread is in, or null
} // namespace kal::crashcontext
