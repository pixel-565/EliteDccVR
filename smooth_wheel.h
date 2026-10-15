#pragma once
struct SmoothBrowserWheel
{
    unsigned long long previous = 0;
    double remainder = 0;
    int direction = 0;
    void reset()
    {
        previous = 0;
        remainder = 0;
        direction = 0;
    }
    int update(float axis, unsigned long long now)
    {
        if (!std::isfinite(axis) || std::abs(axis) <= .22f)
        {
            reset();
            return 0;
        }
        int sign = axis > 0 ? 1 : -1;
        if (direction != sign)
        {
            reset();
            direction = sign;
        }
        double seconds = previous ? std::min(0.05, double(now - previous) / 1000.) : 1. / 90.;
        previous = now;
        double amount = std::clamp((std::abs(axis) - .22) / .78, 0., 1.);
        remainder += sign * 900. * amount * amount * seconds;
        int result = static_cast<int>(remainder);
        remainder -= result;
        return result;
    }
};
