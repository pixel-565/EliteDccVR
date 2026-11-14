#pragma once
#include <cstdint>
#include <vector>
namespace NativeSystem {
struct Vec {float x{},y{},z{};};
// station also classifies carrier markers: native map icons, never sphere anchors.
struct Body {Vec map{};uint64_t id{};uintptr_t object{};bool star{};float radius{};bool station{};};
struct Draw {Vec map{},world{};float radius{};bool star{};unsigned eye{};};
struct Target {Body body{};Vec world{};float radius{};};
bool initialize(uintptr_t base);
void enable(bool value);
bool visible();
void observe(uintptr_t wrapper);
void advance(void* movie);
void submit(const std::vector<Draw>& draws,uint64_t stamp);
bool snapshot(std::vector<Target>& targets,uint64_t& stamp);
void select(const Body& body);
void counters(uint64_t (&values)[5]);
void navigation(float x,float y,bool zoom);
void setNavigationScale(int panPercent,int zoomPercent);
void setNavigationAcceleration(int milliseconds);
void clearNavigation();
void setFocused(bool value);
void navigationCounters(uint64_t (&values)[5]);
}
