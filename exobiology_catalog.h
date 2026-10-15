#pragma once
#include "navigation_data.h"

namespace Exobiology
{
struct Reference
{
    int64_t price = -1;
    int distance = -1;
};
class Catalog
{
    struct Genus
    {
        std::string symbol, name;
        int distance;
    };
    struct Species
    {
        std::string symbol, name, genus;
        int64_t price;
    };
    std::vector<Genus> genuses;
    std::vector<Species> species;
    static std::string lower(std::string value)
    {
        std::transform(value.begin(), value.end(), value.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return value;
    }
    static std::string symbol(std::string value)
    {
        value = lower(value);
        if (value.starts_with("$"))
            value.erase(0, 1);
        if (value.starts_with("codex_ent_"))
            value.erase(0, 10);
        if (value.ends_with(";"))
            value.pop_back();
        if (value.ends_with("_name"))
            value.resize(value.size() - 5);
        return value;
    }
    static bool prefix(const std::string &value, const std::string &key, char separator)
    {
        return !key.empty() && (value == key || (value.starts_with(key) && value.size() > key.size() &&
                                                 value[key.size()] == separator));
    }

  public:
    bool load(const std::filesystem::path &file)
    {
        genuses.clear();
        species.clear();
        try
        {
            std::ifstream in(file);
            NavigationData::Json data;
            if (!in || !(in >> data) || data.value("schema", 0) != 1)
                return false;
            std::vector<Genus> newGenuses;
            std::vector<Species> newSpecies;
            for (const auto &item : data.at("genuses"))
            {
                int distance = item.at("distance_m").get<int>();
                if (distance <= 0 || distance > 10000)
                    return false;
                newGenuses.push_back({symbol(item.at("symbol")), lower(item.at("name")), distance});
            }
            for (const auto &item : data.at("species"))
            {
                int64_t price = item.at("price_cr").get<int64_t>();
                if (price <= 0 || price > 1000000000)
                    return false;
                newSpecies.push_back(
                    {symbol(item.at("symbol")), lower(item.at("name")), symbol(item.at("genus")), price});
            }
            if (newGenuses.empty() || newSpecies.empty())
                return false;
            genuses = std::move(newGenuses);
            species = std::move(newSpecies);
            return true;
        }
        catch (const std::exception &)
        {
            return false;
        }
    }
    Reference lookup(const std::string &journalSymbol, const std::string &displayName) const
    {
        Reference result;
        auto code = symbol(journalSymbol), name = lower(displayName);
        std::string genus;
        size_t best = 0;
        for (const auto &entry : species)
        {
            bool byCode = prefix(code, entry.symbol, '_');
            bool byName = prefix(name, entry.name, ' ');
            size_t length = byCode ? entry.symbol.size() + 1000 : byName ? entry.name.size() : 0;
            if (length > best)
            {
                best = length;
                result.price = entry.price;
                genus = entry.genus;
            }
        }
        for (const auto &entry : genuses)
            if ((!genus.empty() && entry.symbol == genus) ||
                (genus.empty() && (prefix(code, entry.symbol, '_') || prefix(name, entry.name, ' '))))
            {
                result.distance = entry.distance;
                break;
            }
        return result;
    }
};
inline std::wstring priceText(int64_t price)
{
    if (price < 0)
        return L"Unknown";
    auto value = std::to_wstring(price);
    for (int position = static_cast<int>(value.size()) - 3; position > 0; position -= 3)
        value.insert(static_cast<size_t>(position), L",");
    return value + L" Cr";
}
} // namespace Exobiology
