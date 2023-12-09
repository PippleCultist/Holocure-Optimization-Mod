#include "ScriptFunctions.h"
#include <YYToolkit/shared.hpp>
#include <CallbackManager/CallbackManagerInterface.h>
#include "ModuleMain.h"
#include "BuiltinFunctions.h"
#include "YYTKTypes/YYObjectBase.h"
#include "YYTKTypes/CHashMap.h"
#include "YYTKTypes/RefThing.h"
#include <queue>

RValue scriptExecuteArgsArray[10];
RValue curFuncParamArr[100][10];
int curFuncParamDepth = 0;
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
)
{
	int StructHash = GMLVarIndexMapYYTKHash[structIndex];
	RValue* StructRef = nullptr;

	Self->m_yyvarsMap->FindElement(StructHash, StructRef);
	YYObjectBase* Struct = (YYObjectBase*)(StructRef->m_Object);

	if (Struct->m_yyvarsMap == nullptr || Struct->m_yyvarsMap->m_numUsed == 0)
	{
		isFastExit = true;
		if (ReturnValue != nullptr)
		{
			ReturnValue->m_Kind = Args[returnArgIndex]->m_Kind;
			ReturnValue->m_Real = Args[returnArgIndex]->m_Real;
		}
		return ReturnValue;
	}

	if (initializeVariables != nullptr)
	{
		initializeVariables(Self);
	}

	int baseMobArrHash = GMLVarIndexMapYYTKHash[GML_debuffDisplay];
	RValue* baseMobArrRef = nullptr;
	Self->m_yyvarsMap->FindElement(baseMobArrHash, baseMobArrRef);
	RValue* prevRefArr = static_cast<RefDynamicArrayOfRValue*>(baseMobArrRef->m_Pointer)->m_Array;
	int prevRefArrLength = static_cast<RefDynamicArrayOfRValue*>(baseMobArrRef->m_Pointer)->length;
	RValue* paramArr = static_cast<RefDynamicArrayOfRValue*>(baseMobArrRef->m_Pointer)->m_Array = curFuncParamArr[curFuncParamDepth];
	static_cast<RefDynamicArrayOfRValue*>(baseMobArrRef->m_Pointer)->length = 10;
	curFuncParamDepth++;

	scriptExecuteArgsArray[1].m_Kind = VALUE_ARRAY;
	scriptExecuteArgsArray[1].m_Pointer = baseMobArrRef->m_Pointer;
	scriptExecuteArgsArray[2].m_Kind = VALUE_REAL;
	scriptExecuteArgsArray[2].m_Real = 0;
	scriptExecuteArgsArray[3].m_Kind = VALUE_REAL;
	scriptExecuteArgsArray[3].m_Real = numParams;

	for (int i = 0; i < Struct->m_yyvarsMap->m_curSize; i++)
	{
		if (Struct->m_yyvarsMap->m_pBuckets[i].Hash != 0)
		{
			RValue* curValue = Struct->m_yyvarsMap->m_pBuckets[i].v;
			RValue returnValue;

			scriptExecuteArgsArray[0] = *curValue;

			setFuncParams(paramArr, Args, Self);

			methodCallFunc(&returnValue, Self, Other, 4, scriptExecuteArgsArray);
			if (ReturnValue != nullptr)
			{
				Args[returnArgIndex]->m_Real = returnValue.m_Real;
			}
		}
	}
	static_cast<RefDynamicArrayOfRValue*>(baseMobArrRef->m_Pointer)->m_Array = prevRefArr;
	static_cast<RefDynamicArrayOfRValue*>(baseMobArrRef->m_Pointer)->length = prevRefArrLength;
	curFuncParamDepth--;

	isFastExit = false;
	if (ReturnValue != nullptr)
	{
		ReturnValue->m_Kind = Args[returnArgIndex]->m_Kind;
		ReturnValue->m_Real = Args[returnArgIndex]->m_Real;
	}
	return ReturnValue;
}

RValue* OnCriticalHitFuncBefore(CInstance* Self, CInstance* Other, RValue* ReturnValue, int numArgs, RValue** Args)
{
	auto setFuncParams = [](RValue* paramArr, RValue** Args, CInstance* Self) {
		paramArr[0].m_Kind = VALUE_OBJECT;
		paramArr[0].m_Object = Self;
		paramArr[1] = *(Args[0]);
		paramArr[2] = *(Args[1]);
		paramArr[3] = *(Args[2]);
	};

	int numParams = 4;
	VariableNames structIndex = GML_onCriticalHit;
	int returnArgIndex = 2;
	bool isFastExit = false;

	RunStructFunctions(Self, Other, ReturnValue, numArgs, Args, setFuncParams, nullptr, numParams, structIndex, returnArgIndex, isFastExit);
	callbackManagerInterfacePtr->CancelOriginalFunction();
	return nullptr;
}

RValue* invincibleRef = nullptr;
RValue* OnTakeDamageFuncBefore(CInstance* Self, CInstance* Other, RValue* ReturnValue, int numArgs, RValue** Args)
{
	auto setFuncParams = [](RValue* paramArr, RValue** Args, CInstance* Self) {
		paramArr[0] = *(Args[0]);
		paramArr[1] = *(Args[1]);
		paramArr[2] = *(Args[2]);
		paramArr[3].m_Kind = VALUE_OBJECT;
		paramArr[3].m_Object = Self;
		paramArr[4] = *invincibleRef;
		paramArr[5] = *(Args[3]);
	};

	auto initializeVariables = [](CInstance* Self) {
		int invincibleHash = GMLVarIndexMapYYTKHash[GML_invincible];
		Self->m_yyvarsMap->FindElement(invincibleHash, invincibleRef);
	};

	int numParams = 6;
	VariableNames structIndex = GML_onTakeDamage;
	int returnArgIndex = 0;
	bool isFastExit = false;

	RunStructFunctions(Self, Other, ReturnValue, numArgs, Args, setFuncParams, initializeVariables, numParams, structIndex, returnArgIndex, isFastExit);
	callbackManagerInterfacePtr->CancelOriginalFunction();
	return nullptr;
}

std::unordered_map<int, RValue> onHitEffectsMap;
CInstance* curHitTargetAttackInstance = nullptr;
RValue* ApplyOnHitEffectsFuncBefore(CInstance* Self, CInstance* Other, RValue* ReturnValue, int numArgs, RValue** Args)
{
	if (Args[1]->m_Kind == VALUE_UNDEFINED)
	{
		ReturnValue->m_Kind = Args[0]->m_Kind;
		ReturnValue->m_Real = Args[0]->m_Real;
		callbackManagerInterfacePtr->CancelOriginalFunction();
		return Args[0];
	}
	if (Args[3]->m_Kind == VALUE_UNDEFINED)
	{
		Args[3]->m_Kind = VALUE_REAL;
		Args[3]->m_Real = 0;
	}

	// find a way to get meaningful information out of object instance id instead of not handling it
	if (curHitTargetAttackInstance == nullptr)
	{
		return ReturnValue;
	}

	RValue* isEnemyPtr = nullptr;
	int isEnemyHash = GMLVarIndexMapYYTKHash[GML_isEnemy];
	curHitTargetAttackInstance->m_yyvarsMap->FindElement(isEnemyHash, isEnemyPtr);
	RValue* creatorPtr = nullptr;
	int creatorHash = GMLVarIndexMapYYTKHash[GML_creator];
	curHitTargetAttackInstance->m_yyvarsMap->FindElement(creatorHash, creatorPtr);
	RValue* onHitEffectsPtr = nullptr;
	int onHitEffectsHash = GMLVarIndexMapYYTKHash[GML_onHitEffects];
	curHitTargetAttackInstance->m_yyvarsMap->FindElement(onHitEffectsHash, onHitEffectsPtr);

	RValue* creatorOnHitEffectsPtr = nullptr;
	if (isEnemyPtr == nullptr || (creatorPtr != nullptr && isEnemyPtr->m_Real != 0))
	{
		creatorPtr->m_Object->m_yyvarsMap->FindElement(GMLVarIndexMapYYTKHash[GML_onHitEffects], creatorOnHitEffectsPtr);
	}

	if (onHitEffectsPtr == nullptr)
	{
		ReturnValue->m_Kind = Args[0]->m_Kind;
		ReturnValue->m_Real = Args[0]->m_Real;
		callbackManagerInterfacePtr->CancelOriginalFunction();
		return nullptr;
	}
	bool hasAllHashes = true;
	std::unordered_map<int, RValue*> onHitIdsHashMap;

	CHashMap<int, RValue*>* creatorOnHitEffectMap = nullptr;
	if (creatorOnHitEffectsPtr != nullptr && (creatorOnHitEffectMap = creatorOnHitEffectsPtr->m_Object->m_yyvarsMap) != nullptr)
	{
		for (int i = 0; i < creatorOnHitEffectMap->m_curSize; i++)
		{
			auto curElement = creatorOnHitEffectMap->m_pBuckets[i];
			int curHash = curElement.Hash;
			if (curHash != 0)
			{
				if (onHitEffectsMap.count(curHash) == 0)
				{
					hasAllHashes = false;
					break;
				}
				onHitIdsHashMap[curHash] = curElement.v;
			}
		}
	}

	CHashMap<int, RValue*>* curOnHitEffectsMap = nullptr;

	if (hasAllHashes && (curOnHitEffectsMap = onHitEffectsPtr->m_Object->m_yyvarsMap) != nullptr)
	{
		for (int i = 0; i < curOnHitEffectsMap->m_curSize; i++)
		{
			auto curElement = curOnHitEffectsMap->m_pBuckets[i];
			int curHash = curElement.Hash;
			if (curHash != 0)
			{
				if (onHitEffectsMap.count(curHash) == 0)
				{
					hasAllHashes = false;
					break;
				}
				onHitIdsHashMap[curHash] = curElement.v;
			}
		}
	}

	if (!hasAllHashes)
	{
		return ReturnValue;
	}

	if (onHitIdsHashMap.empty())
	{
		ReturnValue->m_Kind = Args[0]->m_Kind;
		ReturnValue->m_Real = Args[0]->m_Real;
		callbackManagerInterfacePtr->CancelOriginalFunction();
		return nullptr;
	}

	int baseMobArrHash = GMLVarIndexMapYYTKHash[GML_debuffDisplay];
	RValue* baseMobArrRef = nullptr;
	Self->m_yyvarsMap->FindElement(baseMobArrHash, baseMobArrRef);
	RValue* prevRefArr = static_cast<RefDynamicArrayOfRValue*>(baseMobArrRef->m_Pointer)->m_Array;
	int prevRefArrLength = static_cast<RefDynamicArrayOfRValue*>(baseMobArrRef->m_Pointer)->length;
	RValue* paramArr = static_cast<RefDynamicArrayOfRValue*>(baseMobArrRef->m_Pointer)->m_Array = curFuncParamArr[curFuncParamDepth];
	static_cast<RefDynamicArrayOfRValue*>(baseMobArrRef->m_Pointer)->length = 10;
	curFuncParamDepth++;

	scriptExecuteArgsArray[1].m_Kind = VALUE_ARRAY;
	scriptExecuteArgsArray[1].m_Pointer = baseMobArrRef->m_Pointer;
	scriptExecuteArgsArray[2].m_Kind = VALUE_REAL;
	scriptExecuteArgsArray[2].m_Real = 0;
	scriptExecuteArgsArray[3].m_Kind = VALUE_REAL;
	scriptExecuteArgsArray[3].m_Real = 6;

	for (auto& it : onHitIdsHashMap)
	{
		RValue script = onHitEffectsMap[it.first];
		RValue* config = it.second;
		RValue returnValue;

		scriptExecuteArgsArray[0] = script;

		paramArr[0] = *(Args[0]);
		paramArr[1] = *(Args[1]);
		paramArr[2] = *(Args[2]);
		paramArr[3].m_Kind = VALUE_OBJECT;
		paramArr[3].m_Object = Self;
		paramArr[4] = *config;
		paramArr[5] = *(Args[3]);

		methodCallFunc(&returnValue, Self, Other, 4, scriptExecuteArgsArray);
		Args[0]->m_Real = returnValue.m_Real;
	}
	static_cast<RefDynamicArrayOfRValue*>(baseMobArrRef->m_Pointer)->m_Array = prevRefArr;
	static_cast<RefDynamicArrayOfRValue*>(baseMobArrRef->m_Pointer)->length = prevRefArrLength;
	curFuncParamDepth--;

	ReturnValue->m_Kind = Args[0]->m_Kind;
	ReturnValue->m_Real = Args[0]->m_Real;
	callbackManagerInterfacePtr->CancelOriginalFunction();

	return nullptr;
}

RValue* ApplyKnockbackFuncBefore(CInstance* Self, CInstance* Other, RValue* ReturnValue, int numArgs, RValue** Args)
{
	// find a way to get meaningful information out of object instance id instead of not handling it
	if (curHitTargetAttackInstance == nullptr)
	{
		return ReturnValue;
	}

	RValue* knockbackPtr = nullptr;
	int knockbackHash = GMLVarIndexMapYYTKHash[GML_knockback];
	curHitTargetAttackInstance->m_yyvarsMap->FindElement(knockbackHash, knockbackPtr);

	if (knockbackPtr == nullptr || knockbackPtr->m_Object->m_yyvarsMap == nullptr)
	{
		callbackManagerInterfacePtr->CancelOriginalFunction();
		return nullptr;
	}

	RValue* durationPtr = nullptr;
	int durationHash = GMLVarIndexMapYYTKHash[GML_duration];
	knockbackPtr->m_Object->m_yyvarsMap->FindElement(durationHash, durationPtr);

	if (durationPtr == nullptr)
	{
		callbackManagerInterfacePtr->CancelOriginalFunction();
		return nullptr;
	}

	RValue* isKnockbackPtr = nullptr;
	int isKnockbackHash = GMLVarIndexMapYYTKHash[GML_isKnockback];
	Self->m_yyvarsMap->FindElement(isKnockbackHash, isKnockbackPtr);
	RValue* isKnockbackDurationPtr = nullptr;
	isKnockbackPtr->m_Object->m_yyvarsMap->FindElement(durationHash, isKnockbackDurationPtr);
	if (isKnockbackDurationPtr == nullptr || isKnockbackDurationPtr->m_Real != 0)
	{
		callbackManagerInterfacePtr->CancelOriginalFunction();
		return nullptr;
	}

	RValue* knockbackImmunePtr = nullptr;
	int knockbackImmuneHash = GMLVarIndexMapYYTKHash[GML_knockbackImmune];
	Self->m_yyvarsMap->FindElement(knockbackImmuneHash, knockbackImmunePtr);
	if (knockbackImmunePtr == nullptr || knockbackImmunePtr->m_Real != 0)
	{
		callbackManagerInterfacePtr->CancelOriginalFunction();
		return nullptr;
	}

	return ReturnValue;
}

RValue* ApplyStatusEffectsFuncBefore(CInstance* Self, CInstance* Other, RValue* ReturnValue, int numArgs, RValue** Args)
{
	// find a way to get meaningful information out of object instance id instead of not handling it
	if (curHitTargetAttackInstance == nullptr)
	{
		return ReturnValue;
	}

	RValue* isEnemyPtr = nullptr;
	int isEnemyHash = GMLVarIndexMapYYTKHash[GML_isEnemy];
	curHitTargetAttackInstance->m_yyvarsMap->FindElement(isEnemyHash, isEnemyPtr);

	int statusEffectsLength = 0;
	if (isEnemyPtr == nullptr)
	{
		RValue* creatorPtr = nullptr;
		int creatorHash = GMLVarIndexMapYYTKHash[GML_creator];
		curHitTargetAttackInstance->m_yyvarsMap->FindElement(creatorHash, creatorPtr);
		RValue* creatorStatusEffects = nullptr;
		int statusEffectsHash = GMLVarIndexMapYYTKHash[GML_statusEffects];
		creatorPtr->m_Object->m_yyvarsMap->FindElement(statusEffectsHash, creatorStatusEffects);
		RValue* statusEffects = nullptr;
		curHitTargetAttackInstance->m_yyvarsMap->FindElement(statusEffectsHash, statusEffects);
		if (creatorStatusEffects->m_Object->m_yyvarsMap != nullptr)
		{
			statusEffectsLength += creatorStatusEffects->m_Object->m_yyvarsMap->m_numUsed;
		}
		if (statusEffects->m_Object->m_yyvarsMap != nullptr)
		{
			statusEffectsLength += statusEffects->m_Object->m_yyvarsMap->m_numUsed;
		}
	}
	else
	{
		RValue* statusEffects = nullptr;
		int statusEffectsHash = GMLVarIndexMapYYTKHash[GML_statusEffects];
		curHitTargetAttackInstance->m_yyvarsMap->FindElement(statusEffectsHash, statusEffects);
		if (statusEffects->m_Object->m_yyvarsMap != nullptr)
		{
			statusEffectsLength += statusEffects->m_Object->m_yyvarsMap->m_numUsed;
		}
	}

	if (statusEffectsLength == 0)
	{
		callbackManagerInterfacePtr->CancelOriginalFunction();
		return nullptr;
	}

	return ReturnValue;
}

RValue* OnTakeDamageAfterFuncBefore(CInstance* Self, CInstance* Other, RValue* ReturnValue, int numArgs, RValue** Args)
{
	auto setFuncParams = [](RValue* paramArr, RValue** Args, CInstance* Self) {
		paramArr[0] = *(Args[0]);
		paramArr[1] = *(Args[1]);
		paramArr[2] = *(Args[2]);
		paramArr[3].m_Kind = VALUE_OBJECT;
		paramArr[3].m_Object = Self;
		paramArr[4] = *invincibleRef;
		paramArr[5] = *(Args[3]);
	};

	auto initializeVariables = [](CInstance* Self) {
		int invincibleHash = GMLVarIndexMapYYTKHash[GML_invincible];
		Self->m_yyvarsMap->FindElement(invincibleHash, invincibleRef);
	};

	int numParams = 6;
	VariableNames structIndex = GML_onTakeDamageAfter;
	int returnArgIndex = 0;
	bool isFastExit = false;

	RunStructFunctions(Self, Other, ReturnValue, numArgs, Args, setFuncParams, initializeVariables, numParams, structIndex, returnArgIndex, isFastExit);

	callbackManagerInterfacePtr->CancelOriginalFunction();
	return nullptr;
}

RValue* OnDodgeFuncBefore(CInstance* Self, CInstance* Other, RValue* ReturnValue, int numArgs, RValue** Args)
{
	auto setFuncParams = [](RValue* paramArr, RValue** Args, CInstance* Self) {
		paramArr[0] = *(Args[0]);
		paramArr[1] = *(Args[1]);
		paramArr[2] = *(Args[2]);
		paramArr[3].m_Kind = VALUE_OBJECT;
		paramArr[3].m_Object = Self;
	};

	int numParams = 4;
	VariableNames structIndex = GML_onDodge;
	int returnArgIndex = 0;
	bool isFastExit = false;

	RunStructFunctions(Self, Other, ReturnValue, numArgs, Args, setFuncParams, nullptr, numParams, structIndex, returnArgIndex, isFastExit);

	callbackManagerInterfacePtr->CancelOriginalFunction();
	return nullptr;
}

int numDamageText = 0;
RValue* HitNumberFuncBefore(CInstance* Self, CInstance* Other, RValue* ReturnValue, int numArgs, RValue** Args)
{
	RValue* showDamageTextPtr = nullptr;
	int showDamageTextHash = GMLVarIndexMapYYTKHash[GML_showDamageText];
	globalInstance->m_yyvarsMap->FindElement(showDamageTextHash, showDamageTextPtr);
	if (showDamageTextPtr->m_Real == 0)
	{
		callbackManagerInterfacePtr->CancelOriginalFunction();
		return nullptr;
	}

	if (numDamageText >= 100)
	{
		callbackManagerInterfacePtr->CancelOriginalFunction();
		return nullptr;
	}
	return ReturnValue;
}

RValue* PushDamageDataFuncBefore(CInstance* Self, CInstance* Other, RValue* ReturnValue, int numArgs, RValue** Args)
{
	callbackManagerInterfacePtr->CancelOriginalFunction();
	return nullptr;
}

bool isInDieFunc = false;
RValue* DieFuncBefore(CInstance* Self, CInstance* Other, RValue* ReturnValue, int numArgs, RValue** Args)
{
	isInDieFunc = true;
	return ReturnValue;
}

RValue* DieFuncAfter(CInstance* Self, CInstance* Other, RValue* ReturnValue, int numArgs, RValue** Args)
{
	isInDieFunc = false;
	return ReturnValue;
}

bool isInHitTarget = false;
CInstance* prevCurHitTargetAttackInstance_TakeDamage = nullptr;
bool prevIsInHitTarget_TakeDamage = false;
RValue* TakeDamageFuncBefore(CInstance* Self, CInstance* Other, RValue* ReturnValue, int numArgs, RValue** Args)
{
	prevCurHitTargetAttackInstance_TakeDamage = curHitTargetAttackInstance;
	if (!isInHitTarget)
	{
		curHitTargetAttackInstance = nullptr;
	}
	prevIsInHitTarget_TakeDamage = isInHitTarget;
	isInHitTarget = false;
	return ReturnValue;
}

RValue* TakeDamageFuncAfter(CInstance* Self, CInstance* Other, RValue* ReturnValue, int numArgs, RValue** Args)
{
	isInHitTarget = prevIsInHitTarget_TakeDamage;
	curHitTargetAttackInstance = prevCurHitTargetAttackInstance_TakeDamage;
	return ReturnValue;
}

bool prevIsInHitTarget_TakeDamageObstacle = false;
RValue* TakeDamageObstacleFuncBefore(CInstance* Self, CInstance* Other, RValue* ReturnValue, int numArgs, RValue** Args)
{
	prevIsInHitTarget_TakeDamageObstacle = isInHitTarget;
	isInHitTarget = false;
	return ReturnValue;
}

RValue* TakeDamageObstacleFuncAfter(CInstance* Self, CInstance* Other, RValue* ReturnValue, int numArgs, RValue** Args)
{
	isInHitTarget = prevIsInHitTarget_TakeDamageObstacle;
	return ReturnValue;
}

bool prevIsInHitTarget_TakeDamageObstacleTwo = false;
RValue* TakeDamageObstacleTwoFuncBefore(CInstance* Self, CInstance* Other, RValue* ReturnValue, int numArgs, RValue** Args)
{
	prevIsInHitTarget_TakeDamageObstacleTwo = isInHitTarget;
	isInHitTarget = false;
	return ReturnValue;
}

RValue* TakeDamageObstacleTwoFuncAfter(CInstance* Self, CInstance* Other, RValue* ReturnValue, int numArgs, RValue** Args)
{
	isInHitTarget = prevIsInHitTarget_TakeDamageObstacleTwo;
	return ReturnValue;
}

bool isInCalculateDamage = false;
bool prevIsInHitTarget_CalculateDamage = false;
RValue* CalculateDamageFuncBefore(CInstance* Self, CInstance* Other, RValue* ReturnValue, int numArgs, RValue** Args)
{
	prevIsInHitTarget_CalculateDamage = isInHitTarget;
	isInHitTarget = false;
	isInCalculateDamage = true;
	return ReturnValue;
}

RValue* CalculateDamageFuncAfter(CInstance* Self, CInstance* Other, RValue* ReturnValue, int numArgs, RValue** Args)
{
	isInCalculateDamage = false;
	isInHitTarget = prevIsInHitTarget_CalculateDamage;
	return ReturnValue;
}

int hitTargetIndex = -1;
CInstance* prevCurHitTargetAttackInstance_HitTarget = nullptr;
RValue* HitTargetFuncBefore(CInstance* Self, CInstance* Other, RValue* ReturnValue, int numArgs, RValue** Args)
{
	prevCurHitTargetAttackInstance_HitTarget = curHitTargetAttackInstance;
	curHitTargetAttackInstance = Self;
	hitTargetIndex = Args[0]->m_Object->m_slot;
	isInHitTarget = true;
	return ReturnValue;
}

RValue* HitTargetFuncAfter(CInstance* Self, CInstance* Other, RValue* ReturnValue, int numArgs, RValue** Args)
{
	isInHitTarget = false;
	curHitTargetAttackInstance = prevCurHitTargetAttackInstance_HitTarget;
	return ReturnValue;
}

RValue* AfterCriticalHitFuncBefore(CInstance* Self, CInstance* Other, RValue* ReturnValue, int numArgs, RValue** Args)
{
	auto setFuncParams = [](RValue* paramArr, RValue** Args, CInstance* Self) {
		paramArr[0].m_Kind = VALUE_OBJECT;
		paramArr[0].m_Object = Self;
		paramArr[1] = *(Args[0]);
		paramArr[2] = *(Args[1]);
		paramArr[3] = *(Args[2]);
	};

	int numParams = 4;
	VariableNames structIndex = GML_afterCriticalHit;
	int returnArgIndex = 2;
	bool isFastExit = false;

	RunStructFunctions(Self, Other, ReturnValue, numArgs, Args, setFuncParams, nullptr, numParams, structIndex, returnArgIndex, isFastExit);
	callbackManagerInterfacePtr->CancelOriginalFunction();

	return Args[2];
}

RValue* BeforeDamageCalculationFuncBefore(CInstance* Self, CInstance* Other, RValue* ReturnValue, int numArgs, RValue** Args)
{
	auto setFuncParams = [](RValue* paramArr, RValue** Args, CInstance* Self) {
		paramArr[0] = *(Args[0]);
		paramArr[1] = *(Args[1]);
		paramArr[2] = *(Args[2]);
	};

	int numParams = 4;
	VariableNames structIndex = GML_beforeDamageCalculation;
	int returnArgIndex = 2;
	bool isFastExit = false;

	RunStructFunctions(Self, Other, ReturnValue, numArgs, Args, setFuncParams, nullptr, numParams, structIndex, returnArgIndex, isFastExit);
	callbackManagerInterfacePtr->CancelOriginalFunction();

	return Args[2];
}

bool isInSpawnMobFunc = false;
RValue* SpawnMobFuncBefore(CInstance* Self, CInstance* Other, RValue* ReturnValue, int numArgs, RValue** Args)
{
	isInSpawnMobFunc = true;
	return ReturnValue;
}

RValue* SpawnMobFuncAfter(CInstance* Self, CInstance* Other, RValue* ReturnValue, int numArgs, RValue** Args)
{
	isInSpawnMobFunc = false;
	return ReturnValue;
}

YYObjectBase* curStructPtr = nullptr;

void CreateEmptyStruct(CInstance* Self, YYObjectBase* structPtr, RValue* copyToRValue)
{
	RValue inputArg;
	inputArg.m_Kind = VALUE_OBJECT;
	inputArg.m_Pointer = structPtr;
	auto prevMap = structPtr->m_yyvarsMap;
	structPtr->m_yyvarsMap = nullptr;
	*copyToRValue = g_ModuleInterface->CallBuiltin("variable_clone", { inputArg });
	structPtr->m_yyvarsMap = prevMap;
}

void StructSetFromHash(CInstance* Self, RValue* setStruct, int hash, RValue* setVal)
{
	RValue hashRValue;
	hashRValue.m_Kind = VALUE_REAL;
	hashRValue.m_Real = hash;
	g_ModuleInterface->CallBuiltin("struct_set_from_hash", { *setStruct, hashRValue, *setVal });
}

void DeepStructCopy(CInstance* Self, RValue* copyFromObject, RValue* copyToObject)
{
	if (copyFromObject->m_Kind == VALUE_OBJECT)
	{
		auto copyFromMap = copyFromObject->m_Object->m_yyvarsMap;
		if (copyFromMap == nullptr)
		{
			return;
		}
		for (int i = 0; i < copyFromMap->m_curSize; i++)
		{
			int curHash = copyFromMap->m_pBuckets[i].Hash;
			if (curHash != 0)
			{
				RValue* curRValue = copyFromMap->m_pBuckets[i].v;
				//m_kind is 0 when struct and 3 when method
				if (curRValue->m_Kind == VALUE_OBJECT && curRValue->m_Object->m_kind == 0)
				{
					RValue newStruct;
					CreateEmptyStruct(Self, curStructPtr, &newStruct);
					DeepStructCopy(Self, curRValue, &newStruct);
//					printf("set hash %d\n", copyFromMap->m_pBuckets[i].k);
					StructSetFromHash(Self, copyToObject, copyFromMap->m_pBuckets[i].k, &newStruct);
//					g_ModuleInterface->CallBuiltin("struct_set_from_hash", { *copyToObject, RValue(static_cast<double>(copyFromMap->m_pBuckets[i].k)), newStruct});
				}
				else if (curRValue->m_Kind == VALUE_ARRAY)
				{
					RValue newArray;
					RValue inputArg;
					inputArg.m_Kind = VALUE_REAL;
					inputArg.m_Real = static_cast<RefDynamicArrayOfRValue*>(curRValue->m_Pointer)->length;
					newArray = g_ModuleInterface->CallBuiltin("array_create", { inputArg });
					DeepStructCopy(Self, curRValue, &newArray);
					g_ModuleInterface->CallBuiltin("struct_set_from_hash", { *copyToObject, copyFromMap->m_pBuckets[i].k, newArray });
				}
				else
				{
					StructSetFromHash(Self, copyToObject, copyFromMap->m_pBuckets[i].k, curRValue);
//					g_ModuleInterface->CallBuiltin("struct_set_from_hash", { *copyToObject, copyFromMap->m_pBuckets[i].k, *curRValue });
				}
			}
		}
	}
	else if (copyFromObject->m_Kind = VALUE_ARRAY)
	{
		auto copyFromArr = static_cast<RefDynamicArrayOfRValue*>(copyFromObject->m_Pointer);
		auto copyToArr = static_cast<RefDynamicArrayOfRValue*>(copyToObject->m_Pointer);
		for (int i = 0; i < copyFromArr->length; i++)
		{
			auto curRValue = copyFromArr->m_Array[i];

			if (curRValue.m_Kind == VALUE_OBJECT && curRValue.m_Object->m_kind == 0)
			{
				RValue newStruct;
				CreateEmptyStruct(Self, curStructPtr, &newStruct);
				DeepStructCopy(Self, &curRValue, &newStruct);
				copyToArr->m_Array[i].m_Kind = VALUE_OBJECT;
				copyToArr->m_Array[i].m_Object = newStruct.m_Object;
			}
			else if (curRValue.m_Kind == VALUE_ARRAY)
			{
				RValue newArray;
				RValue inputArg;
				inputArg.m_Kind = VALUE_REAL;
				inputArg.m_Real = static_cast<RefDynamicArrayOfRValue*>(curRValue.m_Pointer)->length;
				newArray = g_ModuleInterface->CallBuiltin("array_create", { inputArg });
				DeepStructCopy(Self, &curRValue, &newArray);
				copyToArr->m_Array[i].m_Kind = VALUE_ARRAY;
				copyToArr->m_Array[i].m_Pointer = newArray.m_Pointer;
			}
			else if (curRValue.m_Kind == VALUE_STRING)
			{
				g_ModuleInterface->CallBuiltin("array_set", { *copyToObject, RValue(static_cast<double>(i)), copyFromArr->m_Array[i] });
			}
			else
			{
				memcpy(&(copyToArr->m_Array[i]), &curRValue, sizeof(RValue));
			}
		}
	}
	else
	{
		g_ModuleInterface->Print(CM_RED, "UNEXPECTED COPY TYPE");
	}
}

bool isInActualSpawnMobFunc = false;
bool prevIsInSpawnMobFunc_ActualSpawnMob = false;
RValue* ActualSpawnMobFuncBefore(CInstance* Self, CInstance* Other, RValue* ReturnValue, int numArgs, RValue** Args)
{
	prevIsInSpawnMobFunc_ActualSpawnMob = isInSpawnMobFunc;
	isInSpawnMobFunc = false;
	isInActualSpawnMobFunc = true;
	return ReturnValue;
}

RValue* ActualSpawnMobFuncAfter(CInstance* Self, CInstance* Other, RValue* ReturnValue, int numArgs, RValue** Args)
{
	isInActualSpawnMobFunc = false;
	isInSpawnMobFunc = prevIsInSpawnMobFunc_ActualSpawnMob;
	return ReturnValue;
}

RValue* VariableStructCopyFuncBefore(CInstance* Self, CInstance* Other, RValue* ReturnValue, int numArgs, RValue** Args)
{
	if (Args[0]->m_Kind == VALUE_OBJECT)
	{
		curStructPtr = Args[0]->m_Object;

		DeepStructCopy(Self, (RValue*)Args[0], (RValue*)Args[1]);

		curStructPtr = nullptr;
		callbackManagerInterfacePtr->CancelOriginalFunction();
		return nullptr;
	}
	return ReturnValue;
}

const char* enemyMeshBoundingBox = "[[-6,-4],[5,-4],[5,-15],[-6,-15]]";
std::unordered_map<int, int> meshIndices;
std::queue<int> availableMeshes;
RValue* GLRMeshInputArgs[3];

RValue* GLRMeshCreateFuncBefore(CInstance* Self, CInstance* Other, RValue* ReturnValue, int numArgs, RValue** Args)
{
	if (Self->i_objectindex == objEnemyIndex && !availableMeshes.empty())
	{
		int meshIndex = availableMeshes.front();
		availableMeshes.pop();

		RValue Result;
		if (GLRMeshInputArgs[0] == nullptr)
		{
			GLRMeshInputArgs[0] = new RValue();
			GLRMeshInputArgs[1] = new RValue();
			GLRMeshInputArgs[2] = new RValue();
		}

		GLRMeshInputArgs[0]->m_Kind = VALUE_REAL;
		GLRMeshInputArgs[0]->m_Real = meshIndex;
		GLRMeshInputArgs[1]->m_Kind = VALUE_REAL;
		GLRMeshInputArgs[1]->m_Real = 1;
		origGLRLightSetActiveScript(Self, Other, &Result, 2, GLRMeshInputArgs);

		ReturnValue->m_Kind = VALUE_REAL;
		ReturnValue->m_Real = meshIndex;
		callbackManagerInterfacePtr->CancelOriginalFunction();
		return nullptr;
	}
	return ReturnValue;
}

RValue* GLRMeshUpdateFuncBefore(CInstance* Self, CInstance* Other, RValue* ReturnValue, int numArgs, RValue** Args)
{
	int listNum = static_cast<int>(Args[0]->m_Real);
	if (meshIndices.find(listNum) != meshIndices.end())
	{
		if (meshIndices[listNum] == 1)
		{
			meshIndices[listNum] = 2;
		}
		else
		{
			meshIndices[listNum] = 2;
			callbackManagerInterfacePtr->CancelOriginalFunction();
			return nullptr;
		}
	}
	return ReturnValue;
}

void moveMeshToQueue(CInstance* Self, int listNum)
{
	if (meshIndices[listNum] == 3)
	{
		return;
	}
	meshIndices[listNum] = 3;
	RValue Result;
	if (GLRMeshInputArgs[0] == nullptr)
	{
		GLRMeshInputArgs[0] = new RValue();
		GLRMeshInputArgs[1] = new RValue();
		GLRMeshInputArgs[2] = new RValue();
	}

	GLRMeshInputArgs[0]->m_Kind = VALUE_REAL;
	GLRMeshInputArgs[0]->m_Real = listNum;
	GLRMeshInputArgs[1]->m_Kind = VALUE_REAL;
	GLRMeshInputArgs[1]->m_Real = 0;
	origGLRLightSetActiveScript(Self, nullptr, &Result, 2, GLRMeshInputArgs);

	availableMeshes.push(listNum);
}

int numSpawnAdditionalMesh = 20;
int idealMinAvailableMeshes = 150;
RValue* GLRMeshSubmeshFuncBefore(CInstance* Self, CInstance* Other, RValue* ReturnValue, int numArgs, RValue** Args)
{
	int meshListNum = static_cast<int>(Args[0]->m_Real);
	if (numSpawnAdditionalMesh > 0 && availableMeshes.size() < idealMinAvailableMeshes && strcmp(enemyMeshBoundingBox, ((RefString*)Args[1]->m_Pointer)->m_Thing) == 0)
	{
		while (numSpawnAdditionalMesh > 0 && availableMeshes.size() < idealMinAvailableMeshes)
		{
			numSpawnAdditionalMesh--;
			if (GLRMeshInputArgs[0] == nullptr)
			{
				GLRMeshInputArgs[0] = new RValue();
				GLRMeshInputArgs[1] = new RValue();
				GLRMeshInputArgs[2] = new RValue();
			}
			GLRMeshInputArgs[0]->m_Kind = VALUE_REAL;
			GLRMeshInputArgs[0]->m_Real = 0;
			GLRMeshInputArgs[1]->m_Kind = VALUE_REAL;
			GLRMeshInputArgs[1]->m_Real = 0;
			GLRMeshInputArgs[2]->m_Kind = VALUE_REAL;
			GLRMeshInputArgs[2]->m_Real = 0;
			origGLRMeshCreateScript(Self, Other, ReturnValue, 3, GLRMeshInputArgs);
			int listNum = static_cast<int>(ReturnValue->m_Real);
			Args[0]->m_Real = listNum;
			origGLRMeshSubmeshScript(Self, Other, ReturnValue, numArgs, Args);
			meshIndices[listNum] = 2;
			GLRMeshInputArgs[0]->m_Real = listNum;
			origGLRMeshUpdateScript(Self, Other, ReturnValue, 1, GLRMeshInputArgs);
			moveMeshToQueue(Self, listNum);
		}
	}
	Args[0]->m_Real = meshListNum;
	if (meshIndices.find(meshListNum) != meshIndices.end())
	{
		callbackManagerInterfacePtr->CancelOriginalFunction();
		return nullptr;
	}
	if (strcmp(enemyMeshBoundingBox, ((RefString*)Args[1]->m_Pointer)->m_Thing) == 0)
	{
		meshIndices[meshListNum] = 1;
	}
	return ReturnValue;
}

RValue* GLRMeshDestroyFuncBefore(CInstance* Self, CInstance* Other, RValue* ReturnValue, int numArgs, RValue** Args)
{
	int listNum = static_cast<int>(Args[0]->m_Real);
	if (meshIndices.find(listNum) != meshIndices.end())
	{
		moveMeshToQueue(Self, listNum);
		callbackManagerInterfacePtr->CancelOriginalFunction();
		return nullptr;
	}
	return ReturnValue;
}

RValue* CanSubmitScoreFuncBefore(CInstance* Self, CInstance* Other, RValue* ReturnValue, int numArgs, RValue** Args)
{
	ReturnValue->m_Kind = VALUE_BOOL;
	ReturnValue->m_Real = 0;
	callbackManagerInterfacePtr->CancelOriginalFunction();
	return nullptr;
}

bool isInGLRMeshDestroyAll = false;
RValue* GLRMeshDestroyAllFuncBefore(CInstance* Self, CInstance* Other, RValue* ReturnValue, int numArgs, RValue** Args)
{
	isInGLRMeshDestroyAll = true;
	return ReturnValue;
}

RValue* GLRMeshDestroyAllFuncAfter(CInstance* Self, CInstance* Other, RValue* ReturnValue, int numArgs, RValue** Args)
{
	isInGLRMeshDestroyAll = false;
	return ReturnValue;
}

RValue* ExecuteAttackFuncBefore(CInstance* Self, CInstance* Other, RValue* ReturnValue, int numArgs, RValue** Args)
{
	isInExecuteAttack = true;
	return ReturnValue;
}

RValue* ExecuteAttackFuncAfter(CInstance* Self, CInstance* Other, RValue* ReturnValue, int numArgs, RValue** Args)
{
	isInExecuteAttack = false;
	return ReturnValue;
}