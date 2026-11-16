/*
    Swap - handing a whole new object from the message thread to the audio
    thread without a lock or an allocation on the audio side.

    The message thread `post`s a new object. The audio thread `take`s it at
    the start of a block (if there is one) and the one it replaces goes into
    a small lock-free bin, which the message thread empties (`collect`) on a
    timer. The audio thread never deletes and never waits.
*/

#pragma once

#include <juce_core/juce_core.h>

#include <array>
#include <atomic>
#include <memory>

namespace kal
{

template <typename T>
class Swap
{
public:
    ~Swap()
    {
        delete pending.exchange (nullptr);
        delete current;
        collect();
    }

    // Message thread.
    void post (std::unique_ptr<T> next)
    {
        delete pending.exchange (next.release());
        collect();
    }

    void collect()
    {
        int s1, n1, s2, n2;
        fifo.prepareToRead (fifo.getNumReady(), s1, n1, s2, n2);
        for (int i = 0; i < n1; ++i) { delete bin[(size_t) (s1 + i)]; bin[(size_t) (s1 + i)] = nullptr; }
        for (int i = 0; i < n2; ++i) { delete bin[(size_t) (s2 + i)]; bin[(size_t) (s2 + i)] = nullptr; }
        fifo.finishedRead (n1 + n2);
    }

    // Audio thread: the object to use for this block (nullptr until the first post).
    T* take() noexcept
    {
        if (auto* next = pending.exchange (nullptr))
        {
            if (current)
            {
                int s1, n1, s2, n2;
                fifo.prepareToWrite (1, s1, n1, s2, n2);
                if (n1 > 0) bin[(size_t) s1] = current;
                else if (n2 > 0) bin[(size_t) s2] = current;
                else { pending.store (next); return current; }   // bin full: try again next block
                fifo.finishedWrite (1);
            }
            current = next;
        }
        return current;
    }

    // Either thread, read only: what the audio thread used last.
    const T* peek() const noexcept { return current; }

private:
    std::atomic<T*> pending { nullptr };
    T* current = nullptr;
    juce::AbstractFifo fifo { 64 };
    std::array<T*, 64> bin {};
};

} // namespace kal
