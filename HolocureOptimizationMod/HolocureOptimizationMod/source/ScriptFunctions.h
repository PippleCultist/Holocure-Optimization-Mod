#pragma once
#include <YYToolkit/shared.hpp>
#include "ModuleMain.h"

using setFuncParamFunctionPtr = void (*)(RValue* paramArr, RValue** Args, CInstance* Self);
using initializeVariablesFunctionPtr = void (*)(CInstance* Self);

extern int numDamageText;
extern int hitTargetIndex;
extern int numSpawnAdditionalMesh;
extern bool isInHitTarget;
extern bool isInSpawnMobFunc;
extern bool isInDieFunc;
extern bool isInActualSpawnMobFunc;
extern bool isInGLRMeshDestroyAll;
extern std::unordered_map<int, RValue> onHitEffectsMap;
extern RValue curFuncParamArr[100][10];
extern int curFuncParamDepth;
extern RValue scriptExecuteArgsArray[10];

/*
* Helper function that handles running all the functions that are stored in a struct
*/
RValue* RunStructFunctions(
	CInstance* Self,
	CInstance* Other,
	RValue* ReturnValue,
	int numArgs,
	RValue** Args,
	setFuncParamFunctionPtr setFuncParams,
	initializeVariablesFunctionPtr initializeVariables,
	int numParams,
	VariableNames structIndex,
	int returnArgIndex,
	bool& isFastExit
);

void CreateEmptyStruct(CInstance* Self, YYObjectBase* structPtr, RValue* copyToRValue);
void DeepStructCopy(CInstance* Self, RValue* copyFromObject, RValue* copyToObject);
void moveMeshToQueue(CInstance* Self, int listNum);

RValue* OnCriticalHitFuncBefore(CInstance* Self, CInstance* Other, RValue* ReturnValue, int numArgs, RValue** Args);
RValue* OnTakeDamageFuncBefore(CInstance* Self, CInstance* Other, RValue* ReturnValue, int numArgs, RValue** Args);
RValue* ApplyOnHitEffectsFuncBefore(CInstance* Self, CInstance* Other, RValue* ReturnValue, int numArgs, RValue** Args);
RValue* ApplyKnockbackFuncBefore(CInstance* Self, CInstance* Other, RValue* ReturnValue, int numArgs, RValue** Args);
RValue* ApplyStatusEffectsFuncBefore(CInstance* Self, CInstance* Other, RValue* ReturnValue, int numArgs, RValue** Args);
RValue* OnTakeDamageAfterFuncBefore(CInstance* Self, CInstance* Other, RValue* ReturnValue, int numArgs, RValue** Args);
RValue* OnDodgeFuncBefore(CInstance* Self, CInstance* Other, RValue* ReturnValue, int numArgs, RValue** Args);
RValue* HitNumberFuncBefore(CInstance* Self, CInstance* Other, RValue* ReturnValue, int numArgs, RValue** Args);
RValue* PushDamageDataFuncBefore(CInstance* Self, CInstance* Other, RValue* ReturnValue, int numArgs, RValue** Args);
RValue* DieFuncBefore(CInstance* Self, CInstance* Other, RValue* ReturnValue, int numArgs, RValue** Args);
RValue* DieFuncAfter(CInstance* Self, CInstance* Other, RValue* ReturnValue, int numArgs, RValue** Args);
RValue* TakeDamageFuncBefore(CInstance* Self, CInstance* Other, RValue* ReturnValue, int numArgs, RValue** Args);
RValue* TakeDamageFuncAfter(CInstance* Self, CInstance* Other, RValue* ReturnValue, int numArgs, RValue** Args);
RValue* TakeDamageObstacleFuncBefore(CInstance* Self, CInstance* Other, RValue* ReturnValue, int numArgs, RValue** Args);
RValue* TakeDamageObstacleFuncAfter(CInstance* Self, CInstance* Other, RValue* ReturnValue, int numArgs, RValue** Args);
RValue* TakeDamageObstacleTwoFuncBefore(CInstance* Self, CInstance* Other, RValue* ReturnValue, int numArgs, RValue** Args);
RValue* TakeDamageObstacleTwoFuncAfter(CInstance* Self, CInstance* Other, RValue* ReturnValue, int numArgs, RValue** Args);
RValue* CalculateDamageFuncBefore(CInstance* Self, CInstance* Other, RValue* ReturnValue, int numArgs, RValue** Args);
RValue* CalculateDamageFuncAfter(CInstance* Self, CInstance* Other, RValue* ReturnValue, int numArgs, RValue** Args);
RValue* HitTargetFuncBefore(CInstance* Self, CInstance* Other, RValue* ReturnValue, int numArgs, RValue** Args);
RValue* HitTargetFuncAfter(CInstance* Self, CInstance* Other, RValue* ReturnValue, int numArgs, RValue** Args);
RValue* AfterCriticalHitFuncBefore(CInstance* Self, CInstance* Other, RValue* ReturnValue, int numArgs, RValue** Args);
RValue* BeforeDamageCalculationFuncBefore(CInstance* Self, CInstance* Other, RValue* ReturnValue, int numArgs, RValue** Args);
RValue* SpawnMobFuncBefore(CInstance* Self, CInstance* Other, RValue* ReturnValue, int numArgs, RValue** Args);
RValue* SpawnMobFuncAfter(CInstance* Self, CInstance* Other, RValue* ReturnValue, int numArgs, RValue** Args);
RValue* ActualSpawnMobFuncBefore(CInstance* Self, CInstance* Other, RValue* ReturnValue, int numArgs, RValue** Args);
RValue* ActualSpawnMobFuncAfter(CInstance* Self, CInstance* Other, RValue* ReturnValue, int numArgs, RValue** Args);
RValue* VariableStructCopyFuncBefore(CInstance* Self, CInstance* Other, RValue* ReturnValue, int numArgs, RValue** Args);
RValue* GLRMeshCreateFuncBefore(CInstance* Self, CInstance* Other, RValue* ReturnValue, int numArgs, RValue** Args);
RValue* GLRMeshUpdateFuncBefore(CInstance* Self, CInstance* Other, RValue* ReturnValue, int numArgs, RValue** Args);
RValue* GLRMeshSubmeshFuncBefore(CInstance* Self, CInstance* Other, RValue* ReturnValue, int numArgs, RValue** Args);
RValue* GLRMeshDestroyFuncBefore(CInstance* Self, CInstance* Other, RValue* ReturnValue, int numArgs, RValue** Args);
RValue* CanSubmitScoreFuncBefore(CInstance* Self, CInstance* Other, RValue* ReturnValue, int numArgs, RValue** Args);
RValue* GLRMeshDestroyAllFuncBefore(CInstance* Self, CInstance* Other, RValue* ReturnValue, int numArgs, RValue** Args);
RValue* GLRMeshDestroyAllFuncAfter(CInstance* Self, CInstance* Other, RValue* ReturnValue, int numArgs, RValue** Args);
RValue* ExecuteAttackFuncBefore(CInstance* Self, CInstance* Other, RValue* ReturnValue, int numArgs, RValue** Args);
RValue* ExecuteAttackFuncAfter(CInstance* Self, CInstance* Other, RValue* ReturnValue, int numArgs, RValue** Args);