#define NOMINMAX
#include <windows.h>
#include <cmath>
#include <cassert>
#include <cstdio>
#include <vector>
#include "../shared-vr-ui/game-native/ship_direct.h"
#include "../shared-vr-ui/game-native/panel_hit.h"
using namespace NativeUI::PanelHit;
struct Node {bool visible=true,bounds=true,shape=true;double x=0,y=0,w=10,h=100;const char* name="[class Canvas]";std::vector<Node*> children;};
static bool __fastcall member(void*,void* p,const char* name,Value* v,bool){
 auto* n=static_cast<Node*>(p);v->iface=(void*)1;
 if(!strcmp(name,"visible")){v->type=2;v->data[0]=n->visible;}
 else if(!strcmp(name,"numChildren")){v->type=3;v->data[0]=n->children.size();}
 else if(!strcmp(name,"constructor")){v->type=8;v->data[0]=(uintptr_t)n;}
 else if(!strcmp(name,"x")||!strcmp(name,"y")||!strcmp(name,"width")||!strcmp(name,"height")){double d=!strcmp(name,"x")?n->x:!strcmp(name,"y")?n->y:!strcmp(name,"width")?n->w:n->h;v->type=5;memcpy(v->data,&d,8);}
 else return false;return true;
}
static bool __fastcall invoke(void*,void* p,Value* v,const char* method,const Value* args,uint64_t count,bool){
 auto* n=static_cast<Node*>(p);v->iface=(void*)1;
 if(!strcmp(method,"toString")){assert(!count);v->type=6;v->data[0]=(uintptr_t)&n->name;}
 else if(!strcmp(method,"hitTestPoint")){assert(count==3);v->type=2;v->data[0]=args[2].data[0]?n->shape:n->bounds;}
 else if(!strcmp(method,"getBounds")){assert(count==1);v->type=8;v->data[0]=(uintptr_t)n;}
 else if(!strcmp(method,"getChildAt")){assert(count==1);v->type=10;v->data[0]=(uintptr_t)n->children.at(args[0].data[0]);}
 else return false;return true;
}
static bool query(Node& root,Node* background,bool& complete){Query q;q.background=(uintptr_t)background;bool hit=q.walk({(void*)1,(uintptr_t)&root,true},0);complete=q.complete;return hit;}
int main(){
 memberOverride=&member;invokeOverride=&invoke;
 Node root,background,button,label,hidden,scroll; background.name="[class Button]";button.name="[class LHSMenuButton]";label.name="[class Label]";scroll.name="[class ScrollPane]";
 bool complete{};root.children={&background,&label};assert(!query(root,&background,complete)&&complete);
 root.children.push_back(&button);assert(query(root,&background,complete)&&complete);
 button.shape=false;assert(!query(root,&background,complete)&&complete);button.shape=true;
 hidden.visible=false;hidden.children={&button};root.children={&background,&hidden};assert(!query(root,&background,complete)&&complete);
 root.children={&background,&scroll};assert(query(root,&background,complete)&&complete);
 root.children={&root};assert(query(root,&background,complete)&&!complete);
 assert(allows(1,1,.5f,.5f,.5f,.5f,20,0));
 assert(!allows(1,2,.5f,.5f,.5f,.5f,20,0));assert(!allows(1,1,.5f,.5f,.5f,.5f,100,0));
 assert(!allows(1,1,.5f,.5f,.5f,.5f,20,-1));assert(!allows(1,1,.5f,.5f,.5f,.5f,20,1));
 assert(!allows(1,1,.6f,.5f,.5f,.5f,20,0));
 root.children={&background,&button};button.bounds=false;button.x=20;button.y=0;
 Query area;area.background=(uintptr_t)&background;area.stage.type=10;area.stage.iface=(void*)1;area.stage.data[0]=(uintptr_t)&root;
 area.coordinates[0].type=3;area.coordinates[0].data[0]=0;area.coordinates[1].type=3;area.coordinates[1].data[0]=10;
 assert(!area.walk({(void*)1,(uintptr_t)&root,true},0)&&area.complete);assert(area.clearance==16);
 assert(allows(1,1,.51f,.5f,.5f,.5f,25,0,.02f,.02f));
 assert(!allows(1,1,.53f,.5f,.5f,.5f,25,0,.02f,.02f));
 assert(!allows(1,1,.51f,.5f,.5f,.5f,25,1,.02f,.02f));
 puts("PASS certified empty area: nearby control bounds, safety margin, stable small motion and control-boundary rejection.");
 puts("PASS UI hit classification: controls, background exclusion, visibility, shape, scroll region, recursion bounds and fresh coordinate ownership.");
}
