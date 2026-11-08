// New Vegas Co-op by Zyrcxies (https://github.com/Zyrcxies/NewVegasCoop). Reusing this code? See LICENSE: credit and the same license are required.

#pragma once

#include <algorithm>
#include <vector>

namespace display
{
    struct Box
    {
        int left, top, right, bottom;
        int Width() const { return right - left; }
        int Height() const { return bottom - top; }
    };

    inline long long OverlapArea(const Box& a, const Box& b)
    {
        const long long wide = (std::min)(a.right, b.right) - (std::max)(a.left, b.left);
        const long long tall = (std::min)(a.bottom, b.bottom) - (std::max)(a.top, b.top);
        return wide > 0 && tall > 0 ? wide * tall : 0;
    }

    inline long long OverlapWithAny(const Box& window, const std::vector<Box>& others)
    {
        long long total = 0;
        for (const Box& other : others)
            total += OverlapArea(window, other);
        return total;
    }

    inline bool FitsInside(const Box& window, const Box& screen)
    {
        return window.left >= screen.left && window.top >= screen.top && window.right <= screen.right
            && window.bottom <= screen.bottom;
    }

    // Where a windowed game should sit at launch. A window already fully on a screen and clear of the other copies
    // stays put. Otherwise it takes the corner that covers the other copies least, the main screen's first on a tie.
    // screens has the main screen first; others holds the windows of other copies of the game on this PC.
    inline Box PickWindowSpot(const Box& window, const std::vector<Box>& screens, const std::vector<Box>& others)
    {
        if (screens.empty())
            return window;
        if (OverlapWithAny(window, others) == 0)
            for (const Box& screen : screens)
                if (FitsInside(window, screen))
                    return window;

        const auto at = [&](int left, int top) {
            return Box{left, top, left + window.Width(), top + window.Height()};
        };
        std::vector<Box> corners;
        for (const Box& screen : screens)
            corners.push_back(at(screen.left, screen.top));
        for (const Box& screen : screens)
            corners.push_back(at((std::max)(screen.left, screen.right - window.Width()),
                (std::max)(screen.top, screen.bottom - window.Height())));

        Box best = corners.front();
        long long bestOverlap = OverlapWithAny(best, others);
        for (const Box& corner : corners)
        {
            const long long overlap = OverlapWithAny(corner, others);
            if (overlap < bestOverlap)
            {
                best = corner;
                bestOverlap = overlap;
            }
        }
        return best;
    }
}
