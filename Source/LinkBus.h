// LinkBus.h
// A private "audio cable" between PumpKit instances inside the same host program.
// One instance (on the kick's track) sends its level to channel A..H, another one
// (on the bass) listens to that channel. This lets you pick the sidechain source inside
// the plugin itself, like in FL Studio's own Fruity Limiter, without any FL routing.
//
// Only the level (loudness) is sent, which is all the trigger needs. Different instances
// can talk to each other because they all live inside the same loaded plugin file.
#pragma once

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>

namespace pump
{
constexpr int kLinkChannels = 8;           // A..H
constexpr int kLinkSize = 1 << 15;         // samples remembered per channel (power of two)

struct LinkChannel
{
    std::array<std::atomic<float>, kLinkSize> level;   // one non-negative level per sample
    std::atomic<std::uint64_t> writePos { 0 };         // how many samples were ever written
    std::atomic<std::int64_t> lastWriteMs { -1000000 };

    LinkChannel() { for (auto& v : level) v.store (0.0f, std::memory_order_relaxed); }
};

inline LinkChannel& linkChannel (int index)
{
    static LinkChannel channels[kLinkChannels];
    return channels[(index < 0 ? 0 : index >= kLinkChannels ? kLinkChannels - 1 : index)];
}

inline std::int64_t linkNowMs()
{
    using namespace std::chrono;
    return (std::int64_t) duration_cast<milliseconds> (steady_clock::now().time_since_epoch()).count();
}

// the sending side: call once per block with the levels of that block
inline void linkSend (int index, const float* levels, int n)
{
    auto& ch = linkChannel (index);
    std::uint64_t w = ch.writePos.load (std::memory_order_relaxed);
    for (int i = 0; i < n; ++i)
        ch.level[(size_t) ((w + (std::uint64_t) i) & (kLinkSize - 1))].store (levels[i], std::memory_order_relaxed);
    ch.lastWriteMs.store (linkNowMs(), std::memory_order_relaxed);
    ch.writePos.store (w + (std::uint64_t) n, std::memory_order_release);
}

// the listening side: gets the latest n levels. Returns false (and zeros) if nobody is sending.
// If the sender runs before the listener in the same audio cycle there is no delay; if it runs
// after, the sound arrives one audio buffer late.
inline bool linkReceive (int index, float* out, int n)
{
    auto& ch = linkChannel (index);
    const std::uint64_t w = ch.writePos.load (std::memory_order_acquire);
    const bool alive = (linkNowMs() - ch.lastWriteMs.load (std::memory_order_relaxed)) < 300 && w >= (std::uint64_t) n;
    if (! alive)
    {
        for (int i = 0; i < n; ++i) out[i] = 0.0f;
        return false;
    }
    const std::uint64_t start = w - (std::uint64_t) n;
    for (int i = 0; i < n; ++i)
        out[i] = ch.level[(size_t) ((start + (std::uint64_t) i) & (kLinkSize - 1))].load (std::memory_order_relaxed);
    return true;
}
} // namespace pump
