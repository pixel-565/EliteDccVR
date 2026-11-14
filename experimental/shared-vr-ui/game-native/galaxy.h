#pragma once
#include <cstdint>
#include <vector>
namespace NativeGalaxy {
struct Star {float x{},y{},z{};uint64_t id{};uintptr_t object{};};
bool initialize(uintptr_t base);
void enable(bool value);
bool resolve(float x,float y,float z,Star& result);
void nearby(float x,float y,float z,float radius,std::vector<Star>& result);
void select(const Star& value);
void counters(uint64_t (&values)[5]);
}
