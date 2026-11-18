/*
    CrashContext - what Kalibre was doing, for the crash report (decision 0073).

    The audio thread notes the plug-in it's inside, as each one processes
    (TimedPlugin): if Kalibre crashes there, the report names it. A pointer
    per thread (the crash is reported on the thread that crashed; the audio
    plays on several, decision 0078) to a name that lives as long as the plug-in.
*/

#pragma once

#include <atomic>

namespace kal::crashcontext
{
// The plug-in this thread is in, or null: each audio thread its own (decision 0078).
inline thread_local const char* audioPlugin = nullptr;
} // namespace kal::crashcontext
