#pragma once
#include <YYToolkit/shared.hpp>
#include "ModuleMain.h"

extern bool isInExecuteAttack;

void DSMapSetBefore(RValue* Result, CInstance* Self, CInstance* Other, int numArgs, RValue* Args);
void VariableStructExistsBefore(RValue* Result, CInstance* Self, CInstance* Other, int numArgs, RValue* Args);
void StringBefore(RValue* Result, CInstance* Self, CInstance* Other, int numArgs, RValue* Args);
void ArrayLengthBefore(RValue* Result, CInstance* Self, CInstance* Other, int numArgs, RValue* Args);
void InstanceCreateDepthBefore(RValue* Result, CInstance* Self, CInstance* Other, int numArgs, RValue* Args);
void ShowDebugMessageBefore(RValue* Result, CInstance* Self, CInstance* Other, int numArgs, RValue* Args);
void InstanceCreateLayerAfter(RValue* Result, CInstance* Self, CInstance* Other, int numArgs, RValue* Args);
void VariableInstanceGetNamesBefore(RValue* Result, CInstance* Self, CInstance* Other, int numArgs, RValue* Args);
void DSListSizeBefore(RValue* Result, CInstance* Self, CInstance* Other, int numArgs, RValue* Args);
void DSListClearBefore(RValue* Result, CInstance* Self, CInstance* Other, int numArgs, RValue* Args);