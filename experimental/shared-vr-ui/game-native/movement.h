#pragma once
#include <cstdint>
namespace OnFootMovement {
bool initialize();
bool configure(bool enabled);
bool configureInteraction(bool enabled);
bool interactionAllowed();
void submitInteraction(bool down);
void clearInteraction();
void submit(float x,float y,bool allowed);
void submitTurn(float x,bool allowed);
void setDeadZones(int movementPercent,int turnPercent);
void neutral();
void setFocused(bool value);
bool ready();
unsigned slot();
void counters(uint64_t (&values)[5]);
}
