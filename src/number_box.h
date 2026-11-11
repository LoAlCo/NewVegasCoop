// New Vegas Co-op by Zyrcxies (https://github.com/Zyrcxies/NewVegasCoop). Reusing this code? See LICENSE: credit and the same license are required.

#pragma once

#include "coop/gifts.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

// A box for picking a number that works with a keyboard and with a controller: type the number (Enter confirms, Esc
// cancels), or step it with the box's buttons, which a pad picks like any other message box.
// Buttons, top to bottom: confirm, a special value (such as Free or All), -10, -1, +1, +10, +100, and a last one
// (Cancel, or something else the caller names).
class NumberBox
{
public:
    struct Setup
    {
        int question = -1;
        std::string confirm = "OK";
        std::string special;
        int32_t specialValue = 0;
        std::string last = "Cancel";
        int32_t start = 0, min = 0, max = 0;
        // The box's text, given the number it stands at now and what's been typed.
        std::function<std::string(int32_t value, const std::string& typed)> text;
    };
    enum class Result { Open, Confirmed, Cancelled, Last, NotANumber };

    bool Start(const Setup& setup);
    // Call every frame while open. On Confirmed, `value` is the number picked.
    Result Update(int32_t& value);
    bool IsOpen() const { return m_open; }
    void Stop();

    static constexpr int kButtons = 8;
    static constexpr int32_t kSteps[5] = {-10, -1, 1, 10, 100};
    static int32_t Clamp(int64_t v, int32_t lo, int32_t hi)
    {
        return static_cast<int32_t>(v < lo ? lo : v > hi ? hi : v);
    }
    // The number after pressing button `button` (2-6 step it, 1 is the special value), kept within min and max.
    static int32_t Pressed(int button, int32_t now, const Setup& s)
    {
        if (button == 1)
            return Clamp(s.specialValue, s.min, s.max);
        if (button >= 2 && button < 2 + 5)
            return Clamp(int64_t(now) + kSteps[button - 2], s.min, s.max);
        return now;
    }
    // What the box stands at: the typed number when something is typed, otherwise `stepped`. False if what's typed
    // isn't a number.
    static bool Current(const std::string& typed, int32_t stepped, const Setup& s, int32_t& out)
    {
        size_t a = typed.find_first_not_of(' '), b = typed.find_last_not_of(' ');
        std::string t = a == std::string::npos ? std::string() : typed.substr(a, b - a + 1);
        out = stepped;
        if (t.empty())
            return true;
        int32_t n = 0;
        switch (coop::ParseAmount(t, s.max, n))
        {
        case coop::Amount::Ok: out = Clamp(n, s.min, s.max); return true;
        case coop::Amount::Zero: out = Clamp(0, s.min, s.max); return true;
        case coop::Amount::TooMany: out = s.max; return true;
        default: return false;
        }
    }

private:
    bool Ask();
    Setup m_setup;
    int32_t m_value = 0;
    std::string m_shown;
    bool m_open = false;
};
