#pragma once
namespace OnFootMovement {
struct InteractionLatch {
 bool wasDown=true,active=false;
 void cancel(){wasDown=true;active=false;}
 bool update(bool down,bool allowed){
  if(!down){wasDown=false;active=false;return false;}
  if(!wasDown)active=allowed;
  if(!allowed)active=false;
  wasDown=true;return active;
 }
};
}
