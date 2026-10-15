#pragma once
#include "navigation_data.h"
#include <optional>

namespace Exobiology
{
struct Settings
{
    uint32_t incompleteColor = 0xb9dcf1, completedColor = 0x83c78c;
    uint32_t distanceBelowMinimumColor = 0x65bfff, distanceReadyColor = 0x80da8c;
    float rowGap = 2;
    static std::optional<uint32_t> color(const NavigationData::Json &data, const char *key)
    {
        auto value = NavigationData::text(data, key);
        if (value.size() != 7 || value.front() != '#')
            return std::nullopt;
        uint32_t rgb = 0;
        for (size_t i = 1; i < value.size(); ++i)
        {
            char c = static_cast<char>(std::tolower(static_cast<unsigned char>(value[i])));
            int digit = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
            if (digit < 0)
                return std::nullopt;
            rgb = (rgb << 4) | static_cast<uint32_t>(digit);
        }
        return rgb;
    }
    bool load(const std::filesystem::path &file)
    {
        try
        {
            std::ifstream in(file);
            auto data = NavigationData::Json::parse(in, nullptr, false);
            if (!data.is_object())
                return false;
            if (auto value = color(data, "IncompleteColor"))
                incompleteColor = *value;
            if (auto value = color(data, "CompletedColor"))
                completedColor = *value;
            if (auto value = color(data, "DistanceBelowMinimumColor"))
                distanceBelowMinimumColor = *value;
            if (auto value = color(data, "DistanceReadyColor"))
                distanceReadyColor = *value;
            auto gap = data.find("RowGapPx");
            if (gap != data.end() && gap->is_number())
            {
                double pixels = gap->get<double>();
                if (std::isfinite(pixels) && pixels >= 0 && pixels <= 20)
                    rowGap = static_cast<float>(pixels);
            }
            return true;
        }
        catch (const std::exception &)
        {
            return false;
        }
    }
    static uint32_t rowText(uint32_t background)
    {
        double brightness =
            .299 * ((background >> 16) & 255) + .587 * ((background >> 8) & 255) + .114 * (background & 255);
        return brightness >= 150 ? 0x132530 : 0xeaf1ff;
    }
};
} // namespace Exobiology
