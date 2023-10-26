#define YYSDK_PLUGIN
#define ENABLEPROFILER 0
#define MICROSECONDS 1000
#define NANOSECONDS	1
#include "DllMain.hpp"	// Include our header
#include <Windows.h>    // Include Windows's mess.
#include <vector>       // Include the STL vector.
#include <unordered_set>
#include <unordered_map>
#include <queue>
#include <functional>
#include <chrono>
#include <fstream>
#include "Utils/MH/MinHook.h"

using FNScriptData = CScript * (*)(int);
FNScriptData scriptList = nullptr;
TRoutine assetGetIndexFunc;
TRoutine variableGetHashFunc;
CInstance* globalInstancePtr = nullptr;
static int objEnemyIndex = -1;
static int objPreCreateIndex = -1;
static int objAttackIndex = -1;

YYTKStatus MmGetScriptData(FNScriptData& outScript)
{
#ifdef _WIN64

	uintptr_t FuncCallPattern = FindPattern("\xE8\x00\x00\x00\x00\x33\xC9\x0F\xB7\xD3", "x????xxxxx", 0, 0);

	if (!FuncCallPattern)
		return YYTK_INVALIDRESULT;

	uintptr_t Relative = *reinterpret_cast<uint32_t*>(FuncCallPattern + 1);
	Relative = (FuncCallPattern + 5) + Relative;

	if (!Relative)
		return YYTK_INVALIDRESULT;

	outScript = reinterpret_cast<FNScriptData>(Relative);

	return YYTK_OK;
#else
	return YYTK_UNAVAILABLE;
#endif
}

void Hook(void* NewFunc, void* TargetFuncPointer, void** pfnOriginal, const char* Name)
{
	if (TargetFuncPointer)
	{
		auto Status = MH_CreateHook(TargetFuncPointer, NewFunc, pfnOriginal);
		if (Status != MH_OK)
			PrintMessage(
				CLR_RED,
				"Failed to hook function %s (MH Status %s) in %s at line %d",
				Name,
				MH_StatusToString(Status),
				__FILE__,
				__LINE__
			);
		else
			MH_EnableHook(TargetFuncPointer);

		PrintMessage(CLR_GRAY, "- &%s = 0x%p", Name, TargetFuncPointer);
	}
	else
	{
		PrintMessage(
			CLR_RED,
			"Failed to hook function %s (address not found) in %s at line %d",
			Name,
			__FILE__,
			__LINE__
		);
	}
};

inline int getAssetIndexFromName(const char* name)
{
	RValue Result;
	RValue arg{};
	arg.Kind = VALUE_STRING;
	arg.String = RefString::Alloc(name, strlen(name));
	assetGetIndexFunc(&Result, nullptr, nullptr, 1, &arg);
	return static_cast<int>(Result.Real);
}

void HookScriptFunction(const char* scriptFunctionName, void* detourFunction, void** origScript)
{
	int scriptFunctionIndex = getAssetIndexFromName(scriptFunctionName) - 100000;

	CScript* CScript = scriptList(scriptFunctionIndex);

	Hook(
		detourFunction,
		(void*)(CScript->s_pFunc->pScriptFunc),
		origScript,
		scriptFunctionName
	);
}

inline long long StartProfilerTime()
{
	if (ENABLEPROFILER)
	{
		return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::high_resolution_clock::now().time_since_epoch()).count();
	}
	return 0;
}

inline void EndProfilerTime(long long startTime, long long timeType, long long& funcTime, int& numTimes)
{
	if (ENABLEPROFILER)
	{
		funcTime += (std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::high_resolution_clock::now().time_since_epoch()).count() - startTime) / timeType;
		numTimes++;
	}
}

TRoutine structSetFromHashFunc = nullptr;

void StructSetFromHash(CInstance* Self, RValue* setStruct, int hash, RValue* setVal)
{
	RValue Result;
	RValue inputArgs[3];
	inputArgs[0] = *setStruct;
	inputArgs[1].Kind = VALUE_REAL;
	inputArgs[1].Real = hash;
	inputArgs[2] = *setVal;
	structSetFromHashFunc(&Result, Self, nullptr, 3, inputArgs);
}

# define M_PI           3.14159265358979323846
// CallBuiltIn is way too slow to use per frame. Need to investigate if there's a better way to call in built functions.

struct GMLInstance
{
	GMLInstance() : ID(-1), SpriteIndex(-1), InitialSpawnFrame(0), xPos(0), yPos(0)
	{
	}
	GMLInstance(int ID, int SpriteIndex, uint32_t InitialSpawnFrame, float xPos, float yPos) :
		ID(ID), SpriteIndex(SpriteIndex), InitialSpawnFrame(InitialSpawnFrame), xPos(xPos), yPos(yPos)
	{
	}
	int ID;
	int SpriteIndex;
	uint32_t InitialSpawnFrame;
	float xPos;
	float yPos;

};

struct EXPInstance
{
	EXPInstance() : isInitialSpawn(true)
	{
		Range = 0;
	}
	EXPInstance(int Range, GMLInstance gmlInstance) :
		Range(Range), isInitialSpawn(true), gmlInstance(gmlInstance)
	{
	}
	int Range;
	bool isInitialSpawn;
	GMLInstance gmlInstance;
};

struct PlayerInstance
{
	PlayerInstance()
	{
		pickupRange = 0;
	}
	PlayerInstance(int pickupRange, GMLInstance gmlInstance) :
		pickupRange(pickupRange), gmlInstance(gmlInstance)
	{
	}
	int pickupRange;
	GMLInstance gmlInstance;
};

#define NOONE 4

// We save the CodeCallbackHandler attributes here, so we can unregister the callback in the unload routine.
static CallbackAttributes_t* g_pFrameCallbackAttributes = nullptr;
static CallbackAttributes_t* g_pCodeCallbackAttributes = nullptr;
static uint32_t FrameNumber = 0;
static uint32_t LastRainbowEXPFrameNumber = 0;

static uint32_t NumberOfEnemiesCreated = 0;
static uint32_t NumberOfEnemyDeadCreated = 0;
static bool printScripts = true;
static int PlayerScreenXOffset = 320;
static int PlayerScreenYOffset = 196;
static int CameraXPos = -1;
static int CameraYPos = -1;
static int EXPRange = -1;
static bool PickupRangePotentialUpdate = false;
static std::unordered_set<const char*> uniqueCode;
static std::unordered_map<int, EXPInstance> EXPIDMap;
static PlayerInstance currentPlayer;
static GMLInstance currentSummon;

static std::unordered_map<int, long long> ProfilerMap;
static std::unordered_map<int, const char*> codeIndexToName;

static std::unordered_map<int, std::unordered_map<int, uint32_t>> attackHitEnemyFrameNumberMap;
static std::unordered_map<int, int> attackHitCooldownMap;

struct BoundingBox
{
	BoundingBox() : bboxMinX(0), bboxMinY(0), bboxMaxX(0), bboxMaxY(0)
	{
	}

	BoundingBox(float* f_bbox)
	{
		bboxMinX = f_bbox[0];
		bboxMinY = f_bbox[1];
		bboxMaxX = f_bbox[2];
		bboxMaxY = f_bbox[3];
	}

	void updateBoundingBox(float* f_bbox)
	{
		bboxMinX = f_bbox[0];
		bboxMinY = f_bbox[1];
		bboxMaxX = f_bbox[2];
		bboxMaxY = f_bbox[3];
	}

	float bboxMinX, bboxMinY, bboxMaxX, bboxMaxY;
};

static std::unordered_map<int, BoundingBox> previousFrameBoundingBoxMap;
static std::unordered_map<int, int> bucketGridMap;

static int GMLVarIndexMapYYTKHash[1001];
static int GMLVarIndexMapGMLHash[1001];

static std::vector<std::pair<long long, int>> sortVec;

static long long totTime = 0;

static std::unordered_map<int, std::function<void(YYTKCodeEvent* pCodeEvent, CInstance* Self, CInstance* Other, CCode* Code, RValue* Res, int Flags)>> codeFuncTable;

static std::unordered_map<int, std::function<void(CInstance* Self)>> behaviourFuncTable;

RefString tempVar = RefString("Play Modded", 11, false);

static std::ofstream outFile;

static bool testBool = true;

static int bucketGridSize = 32;

static int enemyBehaviourScriptBucket = -1;

static int numEnemyStepSkip = 0;
static int numEnemyStepCall = 0;

static bool isInHitTarget = false;

static TRoutine scriptExecuteFunc;
static RValue scriptExecuteArgsArray[10];

static CScript* spawnMobScript;
static CScript* calculateScoreScript;

static bool hasSetupScriptHooks = false;

static YYRValue tempRet(true);

struct testStruct
{
	testStruct() : Self(nullptr), Other(nullptr), ReturnValue(nullptr), numArgs(0), Args(nullptr)
	{
	}

	testStruct(CInstance* Self, CInstance* Other, YYRValue* ReturnValue, int numArgs, YYRValue** Args) : Self(Self), Other(Other), ReturnValue(ReturnValue), numArgs(numArgs), Args(Args)
	{
	}

	CInstance* Self;
	CInstance* Other;
	YYRValue* ReturnValue;
	int numArgs;
	YYRValue** Args;
};

std::vector<testStruct> funcParams;
static RValue curFuncParamArr[100][10];
static int curFuncParamDepth = 0;

typedef YYRValue* (*ScriptFunc)(CInstance* Self, CInstance* Other, YYRValue* ReturnValue, int numArgs, YYRValue** Args);

using setFuncParamFunctionPtr = void (*)(RValue* paramArr, YYRValue** Args, CInstance* Self);
using initializeVariablesFunctionPtr = void (*)(CInstance* Self);

RValue* invincibleRef = nullptr;

YYRValue* RunStructFunctions(CInstance* Self, CInstance* Other, YYRValue* ReturnValue, int numArgs, YYRValue** Args, setFuncParamFunctionPtr setFuncParams, initializeVariablesFunctionPtr initializeVariables, int numParams, VariableNames structIndex, int returnArgIndex, bool& isFastExit)
{
	int StructHash = GMLVarIndexMapYYTKHash[structIndex];
	RValue* StructRef = nullptr;

	Self->m_yyvarsMap->FindElement(StructHash, StructRef);
	YYObjectBase* Struct = StructRef->Object;

	if (Struct->m_yyvarsMap == nullptr || Struct->m_yyvarsMap->m_numUsed == 0)
	{
		isFastExit = true;
		if (ReturnValue != nullptr)
		{
			ReturnValue->Kind = Args[returnArgIndex]->Kind;
			ReturnValue->Real = Args[returnArgIndex]->Real;
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
	RValue* prevRefArr = baseMobArrRef->RefArray->m_Array;
	int prevRefArrLength = baseMobArrRef->RefArray->length;
	RValue* paramArr = baseMobArrRef->RefArray->m_Array = curFuncParamArr[curFuncParamDepth];
	baseMobArrRef->RefArray->length = 10;
	curFuncParamDepth++;

	scriptExecuteArgsArray[1].Kind = VALUE_ARRAY;
	scriptExecuteArgsArray[1].RefArray = baseMobArrRef->RefArray;
	scriptExecuteArgsArray[2].Kind = VALUE_REAL;
	scriptExecuteArgsArray[2].Real = 0;
	scriptExecuteArgsArray[3].Kind = VALUE_REAL;
	scriptExecuteArgsArray[3].Real = numParams;

	for (int i = 0; i < Struct->m_yyvarsMap->m_curSize; i++)
	{
		if (Struct->m_yyvarsMap->m_pBuckets[i].Hash != 0)
		{
			RValue* curValue = Struct->m_yyvarsMap->m_pBuckets[i].v;
			RValue returnValue;

			scriptExecuteArgsArray[0] = *curValue;

			setFuncParams(paramArr, Args, Self);

			scriptExecuteFunc(&returnValue, Self, Other, 4, scriptExecuteArgsArray);
			if (ReturnValue != nullptr)
			{
				Args[returnArgIndex]->Real = returnValue.Real;
			}
		}
	}
	baseMobArrRef->RefArray->m_Array = prevRefArr;
	baseMobArrRef->RefArray->length = prevRefArrLength;
	curFuncParamDepth--;

	isFastExit = false;
	if (ReturnValue != nullptr)
	{
		ReturnValue->Kind = Args[returnArgIndex]->Kind;
		ReturnValue->Real = Args[returnArgIndex]->Real;
	}
	return ReturnValue;
}

ScriptFunc origOnTakeDamageScript = nullptr;
static long long onTakeDamageFastFuncTime = 0;
static int onTakeDamageFastNumTimes = 0;
static long long onTakeDamageSlowFuncTime = 0;
static int onTakeDamageSlowNumTimes = 0;

YYRValue* OnTakeDamageFuncDetour(CInstance* Self, CInstance* Other, YYRValue* ReturnValue, int numArgs, YYRValue** Args)
{
	auto setFuncParams = [](RValue* paramArr, YYRValue** Args, CInstance* Self) {
		paramArr[0] = *(Args[0]);
		paramArr[1] = *(Args[1]);
		paramArr[2] = *(Args[2]);
		paramArr[3].Kind = VALUE_OBJECT;
		paramArr[3].Instance = Self;
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

	if (isFastExit)
	{
		onTakeDamageFastNumTimes++;
	}
	else
	{
		onTakeDamageSlowNumTimes++;
	}

	return Args[0];
};

ScriptFunc origApplyOnHitEffectsScript = nullptr;
static long long applyOnHitEffectsFuncTime = 0;
static int applyOnHitEffectsNumTimes = 0;
static long long applyOnHitEffectsSlowFuncTime = 0;
static int applyOnHitEffectsSlowNumTimes = 0;

static std::unordered_map<int, RValue> onHitEffectsMap;
static CInstance* curHitTargetAttackInstance = nullptr;

YYRValue* ApplyOnHitEffectsFuncDetour(CInstance* Self, CInstance* Other, YYRValue* ReturnValue, int numArgs, YYRValue** Args)
{
	if (Args[1]->Kind == VALUE_UNDEFINED)
	{
		ReturnValue->Kind = Args[0]->Kind;
		ReturnValue->Real = Args[0]->Real;
		return Args[0];
	}
	if (Args[3]->Kind == VALUE_UNDEFINED)
	{
		Args[3]->Kind = VALUE_REAL;
		Args[3]->Real = 0;
	}

	// find a way to get meaningful information out of object instance id instead of not handling it
	if (curHitTargetAttackInstance == nullptr)
	{
		YYRValue* Res = origApplyOnHitEffectsScript(Self, Other, ReturnValue, numArgs, Args);
		applyOnHitEffectsSlowNumTimes++;
		return Res;
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
	if (isEnemyPtr == nullptr || (creatorPtr != nullptr && isEnemyPtr->Real != 0))
	{
		creatorPtr->Object->m_yyvarsMap->FindElement(GMLVarIndexMapYYTKHash[GML_onHitEffects], creatorOnHitEffectsPtr);
	}

	if (onHitEffectsPtr == nullptr)
	{
		ReturnValue->Kind = Args[0]->Kind;
		ReturnValue->Real = Args[0]->Real;
		applyOnHitEffectsNumTimes++;
		return Args[0];
	}
	bool hasAllHashes = true;
	std::unordered_map<int, RValue*> onHitIdsHashMap;

	CHashMap<int, RValue*>* creatorOnHitEffectMap = nullptr;
	if (creatorOnHitEffectsPtr != nullptr && (creatorOnHitEffectMap = creatorOnHitEffectsPtr->Object->m_yyvarsMap) != nullptr)
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

	if (hasAllHashes && (curOnHitEffectsMap = onHitEffectsPtr->Object->m_yyvarsMap) != nullptr)
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
		YYRValue* Res = origApplyOnHitEffectsScript(Self, Other, ReturnValue, numArgs, Args);
		applyOnHitEffectsSlowNumTimes++;
		return Res;
	}

	if (onHitIdsHashMap.empty())
	{
		ReturnValue->Kind = Args[0]->Kind;
		ReturnValue->Real = Args[0]->Real;
		return Args[0];
	}

	int baseMobArrHash = GMLVarIndexMapYYTKHash[GML_debuffDisplay];
	RValue* baseMobArrRef = nullptr;
	Self->m_yyvarsMap->FindElement(baseMobArrHash, baseMobArrRef);
	RValue* prevRefArr = baseMobArrRef->RefArray->m_Array;
	int prevRefArrLength = baseMobArrRef->RefArray->length;
	RValue* paramArr = baseMobArrRef->RefArray->m_Array = curFuncParamArr[curFuncParamDepth];
	baseMobArrRef->RefArray->length = 10;
	curFuncParamDepth++;

	scriptExecuteArgsArray[1].Kind = VALUE_ARRAY;
	scriptExecuteArgsArray[1].RefArray = baseMobArrRef->RefArray;
	scriptExecuteArgsArray[2].Kind = VALUE_REAL;
	scriptExecuteArgsArray[2].Real = 0;
	scriptExecuteArgsArray[3].Kind = VALUE_REAL;
	scriptExecuteArgsArray[3].Real = 6;

	for (auto& it : onHitIdsHashMap)
	{
		RValue script = onHitEffectsMap[it.first];
		RValue* config = it.second;
		RValue returnValue;

		scriptExecuteArgsArray[0] = script;

		paramArr[0] = *(Args[0]);
		paramArr[1] = *(Args[1]);
		paramArr[2] = *(Args[2]);
		paramArr[3].Kind = VALUE_OBJECT;
		paramArr[3].Instance = Self;
		paramArr[4] = *config;
		paramArr[5] = *(Args[3]);

		scriptExecuteFunc(&returnValue, Self, Other, 4, scriptExecuteArgsArray);
		Args[0]->Real = returnValue.Real;
	}
	baseMobArrRef->RefArray->m_Array = prevRefArr;
	baseMobArrRef->RefArray->length = prevRefArrLength;
	curFuncParamDepth--;

	ReturnValue->Kind = Args[0]->Kind;
	ReturnValue->Real = Args[0]->Real;

	applyOnHitEffectsNumTimes++;
	return Args[0];
};

TRoutine origDSMapSetScript = nullptr;
void DSMapSetDetour(RValue* Result, CInstance* Self, CInstance* Other, int numArgs, RValue* Args)
{
	int StructHash = GMLVarIndexMapYYTKHash[GML_OnHitEffects];
	RValue* StructRef = nullptr;
	globalInstancePtr->m_yyvarsMap->FindElement(StructHash, StructRef);
	if (abs(Args[0].Real - StructRef->Real) < 1e-3)
	{
		RValue Res;
		variableGetHashFunc(&Res, nullptr, nullptr, 1, &(Args[1]));
		int onHitEffectHash = CHashMap<int, RValue>::CalculateHash(static_cast<int>(Res.Real)) % (1ll << 31);
		onHitEffectsMap[onHitEffectHash] = Args[2];
	}
	origDSMapSetScript(Result, Self, Other, numArgs, Args);
}

ScriptFunc origApplyKnockbackScript = nullptr;
static long long applyKnockbackFastFuncTime = 0;
static int applyKnockbackFastNumTimes = 0;
static long long applyKnockbackSlowFuncTime = 0;
static int applyKnockbackSlowNumTimes = 0;

YYRValue* ApplyKnockbackFuncDetour(CInstance* Self, CInstance* Other, YYRValue* ReturnValue, int numArgs, YYRValue** Args)
{
	// find a way to get meaningful information out of object instance id instead of not handling it
	if (curHitTargetAttackInstance == nullptr)
	{
		YYRValue* Res = origApplyKnockbackScript(Self, Other, ReturnValue, numArgs, Args);
		applyKnockbackSlowNumTimes++;
		return Res;
	}

	RValue* knockbackPtr = nullptr;
	int knockbackHash = GMLVarIndexMapYYTKHash[GML_knockback];
	curHitTargetAttackInstance->m_yyvarsMap->FindElement(knockbackHash, knockbackPtr);

	if (knockbackPtr == nullptr || knockbackPtr->Object->m_yyvarsMap == nullptr)
	{
		applyKnockbackFastNumTimes++;
		return nullptr;
	}

	RValue* durationPtr = nullptr;
	int durationHash = GMLVarIndexMapYYTKHash[GML_duration];
	knockbackPtr->Object->m_yyvarsMap->FindElement(durationHash, durationPtr);

	if (durationPtr == nullptr)
	{
		applyKnockbackFastNumTimes++;
		return nullptr;
	}

	RValue* isKnockbackPtr = nullptr;
	int isKnockbackHash = GMLVarIndexMapYYTKHash[GML_isKnockback];
	Self->m_yyvarsMap->FindElement(isKnockbackHash, isKnockbackPtr);
	RValue* isKnockbackDurationPtr = nullptr;
	isKnockbackPtr->Object->m_yyvarsMap->FindElement(durationHash, isKnockbackDurationPtr);
	if (isKnockbackDurationPtr == nullptr || isKnockbackDurationPtr->Real != 0)
	{
		applyKnockbackFastNumTimes++;
		return nullptr;
	}

	RValue* knockbackImmunePtr = nullptr;
	int knockbackImmuneHash = GMLVarIndexMapYYTKHash[GML_knockbackImmune];
	Self->m_yyvarsMap->FindElement(knockbackImmuneHash, knockbackImmunePtr);
	if (knockbackImmunePtr == nullptr || knockbackImmunePtr->Real != 0)
	{
		applyKnockbackFastNumTimes++;
		return nullptr;
	}

	YYRValue* res = origApplyKnockbackScript(Self, Other, ReturnValue, numArgs, Args);
	applyKnockbackSlowNumTimes++;
	return res;
};

ScriptFunc origApplyStatusEffectsScript = nullptr;
static long long applyStatusEffectsFastFuncTime = 0;
static int applyStatusEffectsFastNumTimes = 0;
static long long applyStatusEffectsSlowFuncTime = 0;
static int applyStatusEffectsSlowNumTimes = 0;

YYRValue* ApplyStatusEffectsFuncDetour(CInstance* Self, CInstance* Other, YYRValue* ReturnValue, int numArgs, YYRValue** Args)
{
	// find a way to get meaningful information out of object instance id instead of not handling it
	if (curHitTargetAttackInstance == nullptr)
	{
		YYRValue* Res = origApplyStatusEffectsScript(Self, Other, ReturnValue, numArgs, Args);
		applyStatusEffectsSlowNumTimes++;
		return Res;
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
		creatorPtr->Object->m_yyvarsMap->FindElement(statusEffectsHash, creatorStatusEffects);
		RValue* statusEffects = nullptr;
		curHitTargetAttackInstance->m_yyvarsMap->FindElement(statusEffectsHash, statusEffects);
		if (creatorStatusEffects->Object->m_yyvarsMap != nullptr)
		{
			statusEffectsLength += creatorStatusEffects->Object->m_yyvarsMap->m_numUsed;
		}
		if (statusEffects->Object->m_yyvarsMap != nullptr)
		{
			statusEffectsLength += statusEffects->Object->m_yyvarsMap->m_numUsed;
		}
	}
	else
	{
		RValue* statusEffects = nullptr;
		int statusEffectsHash = GMLVarIndexMapYYTKHash[GML_statusEffects];
		curHitTargetAttackInstance->m_yyvarsMap->FindElement(statusEffectsHash, statusEffects);
		if (statusEffects->Object->m_yyvarsMap != nullptr)
		{
			statusEffectsLength += statusEffects->Object->m_yyvarsMap->m_numUsed;
		}
	}

	if (statusEffectsLength == 0)
	{
		applyStatusEffectsFastNumTimes++;
		return Args[0];
	}

	YYRValue* res = origApplyStatusEffectsScript(Self, Other, ReturnValue, numArgs, Args);
	applyStatusEffectsSlowNumTimes++;
	return res;
};

ScriptFunc origOnTakeDamageAfterScript = nullptr;
static long long onTakeDamageAfterFastFuncTime = 0;
static int onTakeDamageAfterFastNumTimes = 0;
static long long onTakeDamageAfterSlowFuncTime = 0;
static int onTakeDamageAfterSlowNumTimes = 0;

YYRValue* OnTakeDamageAfterFuncDetour(CInstance* Self, CInstance* Other, YYRValue* ReturnValue, int numArgs, YYRValue** Args)
{
	auto setFuncParams = [](RValue* paramArr, YYRValue** Args, CInstance* Self) {
		paramArr[0] = *(Args[0]);
		paramArr[1] = *(Args[1]);
		paramArr[2] = *(Args[2]);
		paramArr[3].Kind = VALUE_OBJECT;
		paramArr[3].Instance = Self;
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

	if (isFastExit)
	{
		onTakeDamageAfterFastNumTimes++;
	}
	else
	{
		onTakeDamageAfterSlowNumTimes++;
	}
	return Args[0];
};

ScriptFunc origOnDodgeScript = nullptr;
static long long onDodgeFastFuncTime = 0;
static int onDodgeFastNumTimes = 0;
static long long onDodgeSlowFuncTime = 0;
static int onDodgeSlowNumTimes = 0;

YYRValue* OnDodgeFuncDetour(CInstance* Self, CInstance* Other, YYRValue* ReturnValue, int numArgs, YYRValue** Args)
{
	auto setFuncParams = [](RValue* paramArr, YYRValue** Args, CInstance* Self) {
		paramArr[0] = *(Args[0]);
		paramArr[1] = *(Args[1]);
		paramArr[2] = *(Args[2]);
		paramArr[3].Kind = VALUE_OBJECT;
		paramArr[3].Instance = Self;
	};

	int numParams = 4;
	VariableNames structIndex = GML_onDodge;
	int returnArgIndex = 0;
	bool isFastExit = false;

	RunStructFunctions(Self, Other, ReturnValue, numArgs, Args, setFuncParams, nullptr, numParams, structIndex, returnArgIndex, isFastExit);

	if (isFastExit)
	{
		onDodgeFastNumTimes++;
	}
	else
	{
		onDodgeSlowNumTimes++;
	}

	return Args[0];
};

ScriptFunc origApplyDamageScript = nullptr;
static long long applyDamageFuncTime = 0;
static int applyDamageNumTimes = 0;

static bool isInApplyDamageFunc = false;

YYRValue* ApplyDamageFuncDetour(CInstance* Self, CInstance* Other, YYRValue* ReturnValue, int numArgs, YYRValue** Args)
{
	isInApplyDamageFunc = true;
	long long startTime = StartProfilerTime();
	YYRValue* res = origApplyDamageScript(Self, Other, ReturnValue, numArgs, Args);
	EndProfilerTime(startTime, MICROSECONDS, applyDamageFuncTime, applyDamageNumTimes);
	isInApplyDamageFunc = false;
	return res;
};

ScriptFunc origHitNumberScript = nullptr;
static long long hitNumberFuncTime = 0;
static int hitNumberNumTimes = 0;
static int objDamageTextIndex = -1;

static int numDamageText = 0;

YYRValue* HitNumberFuncDetour(CInstance* Self, CInstance* Other, YYRValue* ReturnValue, int numArgs, YYRValue** Args)
{
	long long startTime = StartProfilerTime();
	RValue* showDamageTextPtr = nullptr;
	int showDamageTextHash = GMLVarIndexMapYYTKHash[GML_showDamageText];
	globalInstancePtr->m_yyvarsMap->FindElement(showDamageTextHash, showDamageTextPtr);
	if (showDamageTextPtr->Real == 0)
	{
		EndProfilerTime(startTime, NANOSECONDS, hitNumberFuncTime, hitNumberNumTimes);
		return nullptr;
	}

	if (numDamageText >= 100)
	{
		EndProfilerTime(startTime, NANOSECONDS, hitNumberFuncTime, hitNumberNumTimes);
		return nullptr;
	}

	bool prevIsInApplyDamageFunc = isInApplyDamageFunc;
	isInApplyDamageFunc = false;
	YYRValue* res = origHitNumberScript(Self, Other, ReturnValue, numArgs, Args);
	EndProfilerTime(startTime, NANOSECONDS, hitNumberFuncTime, hitNumberNumTimes);
	isInApplyDamageFunc = prevIsInApplyDamageFunc;
	return res;
};

ScriptFunc origOnKillingHitScript = nullptr;
static long long onKillingHitFuncTime = 0;
static int onKillingHitNumTimes = 0;

YYRValue* OnKillingHitFuncDetour(CInstance* Self, CInstance* Other, YYRValue* ReturnValue, int numArgs, YYRValue** Args)
{
	bool prevIsInApplyDamageFunc = isInApplyDamageFunc;
	isInApplyDamageFunc = false;
	long long startTime = StartProfilerTime();
	YYRValue* res = origOnKillingHitScript(Self, Other, ReturnValue, numArgs, Args);
	EndProfilerTime(startTime, NANOSECONDS, onKillingHitFuncTime, onKillingHitNumTimes);
	isInApplyDamageFunc = prevIsInApplyDamageFunc;
	return res;
};

ScriptFunc origPushDamageDataScript = nullptr;
static long long pushDamageDataFuncTime = 0;
static int pushDamageDataNumTimes = 0;

YYRValue* PushDamageDataFuncDetour(CInstance* Self, CInstance* Other, YYRValue* ReturnValue, int numArgs, YYRValue** Args)
{
	return ReturnValue;
};

ScriptFunc origSoundPlayScript = nullptr;
static long long soundPlayFuncTime = 0;
static int soundPlayNumTimes = 0;

YYRValue* SoundPlayFuncDetour(CInstance* Self, CInstance* Other, YYRValue* ReturnValue, int numArgs, YYRValue** Args)
{
	bool prevIsInApplyDamageFunc = isInApplyDamageFunc;
	isInApplyDamageFunc = false;
	long long startTime = StartProfilerTime();
	YYRValue* res = origSoundPlayScript(Self, Other, ReturnValue, numArgs, Args);
	EndProfilerTime(startTime, NANOSECONDS, soundPlayFuncTime, soundPlayNumTimes);
	isInApplyDamageFunc = prevIsInApplyDamageFunc;
	return res;
};

ScriptFunc origApplyHitEffectScript = nullptr;
static long long applyHitEffectFuncTime = 0;
static int applyHitEffectNumTimes = 0;

YYRValue* ApplyHitEffectFuncDetour(CInstance* Self, CInstance* Other, YYRValue* ReturnValue, int numArgs, YYRValue** Args)
{
	bool prevIsInApplyDamageFunc = isInApplyDamageFunc;
	isInApplyDamageFunc = false;
	long long startTime = StartProfilerTime();
	YYRValue* res = origApplyHitEffectScript(Self, Other, ReturnValue, numArgs, Args);
	EndProfilerTime(startTime, NANOSECONDS, applyHitEffectFuncTime, applyHitEffectNumTimes);
	isInApplyDamageFunc = prevIsInApplyDamageFunc;
	return res;
};

ScriptFunc origDieScript = nullptr;
static long long dieFuncTime = 0;
static int dieNumTimes = 0;

static bool isInDieFunc = false;

YYRValue* DieFuncDetour(CInstance* Self, CInstance* Other, YYRValue* ReturnValue, int numArgs, YYRValue** Args)
{
	bool prevIsInApplyDamageFunc = isInApplyDamageFunc;
	isInApplyDamageFunc = false;
	isInDieFunc = true;
	long long startTime = StartProfilerTime();
	YYRValue* res = origDieScript(Self, Other, ReturnValue, numArgs, Args);
	EndProfilerTime(startTime, MICROSECONDS, dieFuncTime, dieNumTimes);
	isInDieFunc = false;
	isInApplyDamageFunc = prevIsInApplyDamageFunc;
	return res;
};

ScriptFunc origTakeDamageScript = nullptr;
static long long takeDamageFuncTime = 0;
static int takeDamageNumTimes = 0;

YYRValue* takeDamageFuncArgs[9];

YYRValue* TakeDamageFuncDetour(CInstance* Self, CInstance* Other, YYRValue* ReturnValue, int numArgs, YYRValue** Args)
{
	CInstance* prevCurHitTargetAttackInstance = curHitTargetAttackInstance;
	if (!isInHitTarget)
	{
		curHitTargetAttackInstance = nullptr;
	}
	bool prevIsInHitTarget = isInHitTarget;
	isInHitTarget = false;
	long long startTime = StartProfilerTime();
	YYRValue* res = nullptr;
	if (numArgs < 9)
	{
		for (int i = 0; i < 9; i++)
		{
			if (takeDamageFuncArgs[i] == nullptr)
			{
				takeDamageFuncArgs[i] = new YYRValue();
			}
			if (i < numArgs)
			{
				memcpy(takeDamageFuncArgs[i], Args[i], sizeof(YYRValue));
			}
			else
			{
				takeDamageFuncArgs[i]->Kind = VALUE_UNDEFINED;
			}
		}
		Args = takeDamageFuncArgs;
	}
	//	for (int i = 0; i < 10000; i++)
	{
		res = origTakeDamageScript(Self, Other, ReturnValue, 9, Args);
	}
	EndProfilerTime(startTime, MICROSECONDS, takeDamageFuncTime, takeDamageNumTimes);
	isInHitTarget = prevIsInHitTarget;
	curHitTargetAttackInstance = prevCurHitTargetAttackInstance;
	return res;
};

ScriptFunc origTakeDamageObstacleScript = nullptr;

YYRValue* TakeDamageObstacleFuncDetour(CInstance* Self, CInstance* Other, YYRValue* ReturnValue, int numArgs, YYRValue** Args)
{
	bool prevIsInHitTarget = isInHitTarget;
	isInHitTarget = false;
	YYRValue* res = origTakeDamageObstacleScript(Self, Other, ReturnValue, numArgs, Args);
	isInHitTarget = prevIsInHitTarget;
	return res;
};

ScriptFunc origTakeDamageObstacleTwoScript = nullptr;

YYRValue* TakeDamageObstacleTwoFuncDetour(CInstance* Self, CInstance* Other, YYRValue* ReturnValue, int numArgs, YYRValue** Args)
{
	bool prevIsInHitTarget = isInHitTarget;
	isInHitTarget = false;
	YYRValue* res = origTakeDamageObstacleTwoScript(Self, Other, ReturnValue, numArgs, Args);
	isInHitTarget = prevIsInHitTarget;
	return res;
};

ScriptFunc origCalculateDamageScript = nullptr;
static long long calculateDamageFuncTime = 0;
static int calculateDamageNumTimes = 0;

static bool isInCalculateDamage = false;

YYRValue* CalculateDamageFuncDetour(CInstance* Self, CInstance* Other, YYRValue* ReturnValue, int numArgs, YYRValue** Args)
{
	bool prevIsInHitTarget = isInHitTarget;
	isInHitTarget = false;
	isInCalculateDamage = true;
	long long startTime = StartProfilerTime();
	YYRValue* res = nullptr;
	//	for (int i = 0; i < 10000; i++)
	{
		res = origCalculateDamageScript(Self, Other, ReturnValue, numArgs, Args);
	}
	EndProfilerTime(startTime, MICROSECONDS, calculateDamageFuncTime, calculateDamageNumTimes);
	isInCalculateDamage = false;
	isInHitTarget = prevIsInHitTarget;
	return res;
};

ScriptFunc origHitTargetScript = nullptr;
static long long hitTargetFuncTime = 0;
static int hitTargetNumTimes = 0;

static int hitTargetIndex = -1;

YYRValue* HitTargetFuncDetour(CInstance* Self, CInstance* Other, YYRValue* ReturnValue, int numArgs, YYRValue** Args)
{
	CInstance* prevCurHitTargetAttackInstance = curHitTargetAttackInstance;
	curHitTargetAttackInstance = Self;
	YYRValue Res;
	hitTargetIndex = Args[0]->Object->m_slot;
	YYRValue ResOne;
	isInHitTarget = true;
	long long startTime = StartProfilerTime();
	YYRValue* res = nullptr;
	//	for (int i = 0; i < 100000; i++)
	{
		res = origHitTargetScript(Self, Other, ReturnValue, numArgs, Args);
	}
	EndProfilerTime(startTime, MICROSECONDS, hitTargetFuncTime, hitTargetNumTimes);
	isInHitTarget = false;
	curHitTargetAttackInstance = prevCurHitTargetAttackInstance;
	return res;
};

inline size_t hashFunc(const char* s)
{
	// http://www.cse.yorku.ca/~oz/hash.html
	size_t h = 5381;
	int pos = 0;
	char curChar = '\0';
	while ((curChar = s[pos]) != '\0')
	{
		h = ((h << 5) + h) + curChar;
		pos++;
	}
	return ((h << 5) + h) + pos;
}

static std::unordered_map<size_t, int> stringHashMap;
static long long VariableStructExistsFastFuncTime = 0;
static long long VariableStructExistsFastNumTimes = 0;
static long long VariableStructExistsSlowFuncTime = 0;
static long long VariableStructExistsSlowNumTimes = 0;

TRoutine origVariableStructExistsScript = nullptr;
void VariableStructExistsDetour(RValue* Result, CInstance* Self, CInstance* Other, int numArgs, RValue* Args)
{
	if (isInHitTarget)
	{
		int enemiesInvincMapHash = GMLVarIndexMapYYTKHash[GML_enemiesInvincMap];
		RValue* enemiesInvincMapPtr = nullptr;
		if (Self->m_yyvarsMap->FindElement(enemiesInvincMapHash, enemiesInvincMapPtr) && enemiesInvincMapPtr->Pointer == Args[0].Pointer)
		{
			Result->Kind = VALUE_REAL;
			std::unordered_map<int, uint32_t>& hitEnemyFrameNumberMap = attackHitEnemyFrameNumberMap[Self->i_id];
			if (hitEnemyFrameNumberMap.count(hitTargetIndex) == 0 || hitEnemyFrameNumberMap[hitTargetIndex] + attackHitCooldownMap[Self->i_spriteindex] <= FrameNumber)
			{
				Result->Real = 0;
				hitEnemyFrameNumberMap[hitTargetIndex] = FrameNumber;
			}
			else
			{
				Result->Real = 1;
			}
			return;
		}
	}
	//For some reason, the second argument can be a real number
	if (Args[0].Kind == VALUE_OBJECT && Args[1].Kind == VALUE_STRING)
	{
		int fastHash = hashFunc(Args[1].String->m_Thing);
		int curHash = 0;

		auto curElementIt = stringHashMap.find(fastHash);

		if (curElementIt == stringHashMap.end())
		{
			RValue Res;
			variableGetHashFunc(&Res, nullptr, nullptr, 1, &(Args[1]));
			curHash = CHashMap<int, RValue>::CalculateHash(static_cast<int>(Res.Real)) % (1ll << 31);
			stringHashMap[fastHash] = curHash;
		}
		else
		{
			curHash = curElementIt->second;
		}

		Result->Kind = VALUE_REAL;
		auto curMap = Args[0].Object->m_yyvarsMap;
		RValue* curElement = nullptr;
		if (!curMap || !(curMap->FindElement(curHash, curElement)))
		{
			Result->Real = 0;
		}
		else
		{
			Result->Real = 1;
		}
		VariableStructExistsFastNumTimes++;
		return;
	}

	//Need to find a way to also get the optimization working for references
	origVariableStructExistsScript(Result, Self, Other, numArgs, Args);
	VariableStructExistsSlowNumTimes++;
}

RefString testStr("test", 4, false);

TRoutine origStringScript = nullptr;
void StringDetour(RValue* Result, CInstance* Self, CInstance* Other, int numArgs, RValue* Args)
{
	if (isInHitTarget)
	{
		Result->Kind = VALUE_STRING;
		Result->String = RefString::Assign(&testStr);
		return;
	}
	origStringScript(Result, Self, Other, numArgs, Args);
}

TRoutine origArrayLengthScript = nullptr;
static long long behaviourFuncTime = 0;
static int behaviourNumTimes = 0;
static long long behaviourCollideWithPlayerFuncTime = 0;
static int behaviourCollideWithPlayerNumTimes = 0;
static long long behaviourFollowPlayerFuncTime = 0;
static int behaviourFollowPlayerNumTimes = 0;
static long long behaviourPowerScalingFuncTime = 0;
static int behaviourPowerScalingNumTimes = 0;
void ArrayLengthDetour(RValue* Result, CInstance* Self, CInstance* Other, int numArgs, RValue* Args)
{
	if (Args[0].Kind == VALUE_UNSET)
	{
		Result->Kind = VALUE_REAL;
		Result->Real = 0;
		return;
	}
	if (Self->i_objectindex == objEnemyIndex)
	{
		auto selfMap = Self->m_yyvarsMap;
		int behaviourKeysHash = GMLVarIndexMapYYTKHash[GML_behaviourKeys];
		RValue* behaviourKeysPtr = nullptr;
		selfMap->FindElement(behaviourKeysHash, behaviourKeysPtr);
		if (behaviourKeysPtr->Pointer == Args[0].Pointer)
		{
			{
				int behavioursHash = GMLVarIndexMapYYTKHash[GML_behaviours];
				RValue* behavioursPtr = nullptr;
				selfMap->FindElement(behavioursHash, behavioursPtr);
				auto behavioursMap = behavioursPtr->Object->m_yyvarsMap;

				if (behavioursMap != nullptr && behavioursMap->m_numUsed != 0)
				{
					int baseMobArrHash = GMLVarIndexMapYYTKHash[GML_debuffDisplay];
					RValue* baseMobArrRef = nullptr;
					selfMap->FindElement(baseMobArrHash, baseMobArrRef);
					RValue* prevRefArr = baseMobArrRef->RefArray->m_Array;
					int prevRefArrLength = baseMobArrRef->RefArray->length;
					RValue* paramArr = baseMobArrRef->RefArray->m_Array = curFuncParamArr[curFuncParamDepth];
					baseMobArrRef->RefArray->length = 10;
					curFuncParamDepth++;

					scriptExecuteArgsArray[1].Kind = VALUE_ARRAY;
					scriptExecuteArgsArray[1].RefArray = baseMobArrRef->RefArray;
					scriptExecuteArgsArray[2].Kind = VALUE_REAL;
					scriptExecuteArgsArray[2].Real = 0;
					scriptExecuteArgsArray[3].Kind = VALUE_REAL;
					scriptExecuteArgsArray[3].Real = 2;

					paramArr[0].Kind = VALUE_OBJECT;
					paramArr[0].Instance = Self;

					RValue returnValue;
					int curSize = behavioursMap->m_curSize;
					for (int i = 0; i < curSize; i++)
					{
						auto curElement = &(behavioursMap->m_pBuckets[i]);
						int curHash = curElement->Hash;
						if (curHash != 0 && curHash != GMLVarIndexMapYYTKHash[GML_powerScaling])
						{
							if (curHash == GMLVarIndexMapYYTKHash[GML_followPlayer])
							{
								RValue* canMovePtr = nullptr;
								RValue* dontMovePtr = nullptr;
								selfMap->FindElement(GMLVarIndexMapYYTKHash[GML_canMove], canMovePtr);
								selfMap->FindElement(GMLVarIndexMapYYTKHash[GML_dontMove], dontMovePtr);
								if (static_cast<int>(canMovePtr->Real) != 0 && static_cast<int>(dontMovePtr->Real) == 0)
								{
									RValue setStruct;
									setStruct.Kind = VALUE_OBJECT;
									setStruct.Instance = Self;
									RValue setVal;
									setVal.Kind = VALUE_REAL;
									setVal.Real = 1;
									StructSetFromHash(Self, &setStruct, GMLVarIndexMapGMLHash[GML_normalMovement], &setVal);
									if (currentPlayer.gmlInstance.xPos > Self->i_x)
									{
										Self->i_imagescalex = abs(Self->i_imagescalex);
									}
									else
									{
										Self->i_imagescalex = -abs(Self->i_imagescalex);
									}
									RValue* SPDPtr = nullptr;
									selfMap->FindElement(GMLVarIndexMapYYTKHash[GML_SPD], SPDPtr);
									RValue* directionMovingPtr = nullptr;
									selfMap->FindElement(GMLVarIndexMapYYTKHash[GML_directionMoving], directionMovingPtr);
									RValue* directionChangeTimePtr = nullptr;
									selfMap->FindElement(GMLVarIndexMapYYTKHash[GML_directionChangeTime], directionChangeTimePtr);
									double curDirectionAngle = directionMovingPtr->Real;
									if (static_cast<int>(directionChangeTimePtr->Real) == 0)
									{
										curDirectionAngle = 180.0 / M_PI * atan2(currentPlayer.gmlInstance.yPos - Self->i_y, currentPlayer.gmlInstance.xPos - Self->i_x);
										if (curDirectionAngle < 0)
										{
											curDirectionAngle += 360;
										}
										directionChangeTimePtr->Real = 3;
										StructSetFromHash(Self, &setStruct, GMLVarIndexMapGMLHash[GML_directionChangeTime], directionChangeTimePtr);
										directionMovingPtr->Real = curDirectionAngle;
										StructSetFromHash(Self, &setStruct, GMLVarIndexMapGMLHash[GML_directionMoving], directionMovingPtr);
									}
									if (curDirectionAngle >= 180)
									{
										curDirectionAngle -= 360;
									}
									double speed = SPDPtr->Real;
									Self->i_x += cos(curDirectionAngle * M_PI / 180.0) * speed;
									setVal.Real = Self->i_y + sin(curDirectionAngle * M_PI / 180.0) * speed;
									StructSetFromHash(Self, &setStruct, GMLVarIndexMapGMLHash[GML_y], &setVal);
								}
							}
							else
							{
								RValue* behaviourPtr = curElement->v;
								RValue* ScriptPtr = nullptr;
								RValue* configPtr = nullptr;

								auto behaviourPtrMap = behaviourPtr->Object->m_yyvarsMap;
								behaviourPtrMap->FindElement(GMLVarIndexMapYYTKHash[GML_Script], ScriptPtr);
								behaviourPtrMap->FindElement(GMLVarIndexMapYYTKHash[GML_config], configPtr);

								scriptExecuteArgsArray[0] = *ScriptPtr;
								paramArr[1] = *configPtr;
								scriptExecuteFunc(&returnValue, Self, Other, 4, scriptExecuteArgsArray);
							}
						}
					}
					baseMobArrRef->RefArray->m_Array = prevRefArr;
					baseMobArrRef->RefArray->length = prevRefArrLength;
					curFuncParamDepth--;
				}
			}
			Result->Kind = VALUE_REAL;
			Result->Real = 0;
			return;
		}
	}
	origArrayLengthScript(Result, Self, Other, numArgs, Args);
}

TRoutine origInstanceCreateDepthScript = nullptr;
void InstanceCreateDepthDetour(RValue* Result, CInstance* Self, CInstance* Other, int numArgs, RValue* Args)
{
	if (static_cast<int>(Args[3].Real) == objDamageTextIndex)
	{
		numDamageText++;
	}
	origInstanceCreateDepthScript(Result, Self, Other, numArgs, Args);
}

TRoutine origShowDebugMessageScript = nullptr;
void ShowDebugMessageDetour(RValue* Result, CInstance* Self, CInstance* Other, int numArgs, RValue* Args)
{
	return;
}

TRoutine origInstanceExistsScript = nullptr;
void InstanceExistsDetour(RValue* Result, CInstance* Self, CInstance* Other, int numArgs, RValue* Args)
{
	if (isInApplyDamageFunc && Args[0].Kind == VALUE_REF)
	{
		Result->Kind = VALUE_BOOL;
		Result->Real = 0;
		return;
	}
	origInstanceExistsScript(Result, Self, Other, numArgs, Args);
}

ScriptFunc origOnCriticalHitScript = nullptr;
static long long onCriticalHitFastFuncTime = 0;
static int onCriticalHitFastNumTimes = 0;
static long long onCriticalHitSlowFuncTime = 0;
static int onCriticalHitSlowNumTimes = 0;

static RValue funcParamArr;

YYRValue* OnCriticalHitFuncDetour(CInstance* Self, CInstance* Other, YYRValue* ReturnValue, int numArgs, YYRValue** Args)
{
	auto setFuncParams = [](RValue* paramArr, YYRValue** Args, CInstance* Self) {
		paramArr[0].Kind = VALUE_OBJECT;
		paramArr[0].Instance = Self;
		paramArr[1] = *(Args[0]);
		paramArr[2] = *(Args[1]);
		paramArr[3] = *(Args[2]);
	};

	int numParams = 4;
	VariableNames structIndex = GML_onCriticalHit;
	int returnArgIndex = 2;
	bool isFastExit = false;

	RunStructFunctions(Self, Other, ReturnValue, numArgs, Args, setFuncParams, nullptr, numParams, structIndex, returnArgIndex, isFastExit);

	if (isFastExit)
	{
		onCriticalHitFastNumTimes++;
	}
	else
	{
		onCriticalHitSlowNumTimes++;
	}

	return Args[2];
};

ScriptFunc origAfterCriticalHitScript = nullptr;
static long long afterCriticalHitFastFuncTime = 0;
static int afterCriticalHitFastNumTimes = 0;
static long long afterCriticalHitSlowFuncTime = 0;
static int afterCriticalHitSlowNumTimes = 0;

YYRValue* AfterCriticalHitFuncDetour(CInstance* Self, CInstance* Other, YYRValue* ReturnValue, int numArgs, YYRValue** Args)
{
	auto setFuncParams = [](RValue* paramArr, YYRValue** Args, CInstance* Self) {
		paramArr[0].Kind = VALUE_OBJECT;
		paramArr[0].Instance = Self;
		paramArr[1] = *(Args[0]);
		paramArr[2] = *(Args[1]);
		paramArr[3] = *(Args[2]);
	};

	int numParams = 4;
	VariableNames structIndex = GML_afterCriticalHit;
	int returnArgIndex = 2;
	bool isFastExit = false;

	RunStructFunctions(Self, Other, ReturnValue, numArgs, Args, setFuncParams, nullptr, numParams, structIndex, returnArgIndex, isFastExit);

	if (isFastExit)
	{
		afterCriticalHitFastNumTimes++;
	}
	else
	{
		afterCriticalHitSlowNumTimes++;
	}

	return Args[2];
};

ScriptFunc origBeforeDamageCalculationScript = nullptr;
static long long beforeDamageCalculationSlowFuncTime = 0;
static int beforeDamageCalculationSlowNumTimes = 0;
static long long beforeDamageCalculationFastFuncTime = 0;
static int beforeDamageCalculationFastNumTimes = 0;

YYRValue* BeforeDamageCalculationFuncDetour(CInstance* Self, CInstance* Other, YYRValue* ReturnValue, int numArgs, YYRValue** Args)
{
	auto setFuncParams = [](RValue* paramArr, YYRValue** Args, CInstance* Self) {
		paramArr[0] = *(Args[0]);
		paramArr[1] = *(Args[1]);
		paramArr[2] = *(Args[2]);
	};

	int numParams = 4;
	VariableNames structIndex = GML_beforeDamageCalculation;
	int returnArgIndex = 2;
	bool isFastExit = false;

	RunStructFunctions(Self, Other, ReturnValue, numArgs, Args, setFuncParams, nullptr, numParams, structIndex, returnArgIndex, isFastExit);

	if (isFastExit)
	{
		beforeDamageCalculationFastNumTimes++;
	}
	else
	{
		beforeDamageCalculationSlowNumTimes++;
	}

	return Args[2];
};

ScriptFunc origDropEXPScript = nullptr;
static long long dropEXPFuncTime = 0;
static int dropEXPNumTimes = 0;

YYRValue* DropEXPFuncDetour(CInstance* Self, CInstance* Other, YYRValue* ReturnValue, int numArgs, YYRValue** Args)
{
	long long startTime = StartProfilerTime();
	YYRValue* res = origDropEXPScript(Self, Other, ReturnValue, numArgs, Args);
	EndProfilerTime(startTime, MICROSECONDS, dropEXPFuncTime, dropEXPNumTimes);
	return res;
};

ScriptFunc origSpawnMobScript = nullptr;
static long long spawnMobFuncTime = 0;
static int spawnMobNumTimes = 0;

static bool isInSpawnMobFunc = false;

YYRValue* SpawnMobFuncDetour(CInstance* Self, CInstance* Other, YYRValue* ReturnValue, int numArgs, YYRValue** Args)
{
	isInSpawnMobFunc = true;
	long long startTime = StartProfilerTime();
	YYRValue* res = origSpawnMobScript(Self, Other, ReturnValue, numArgs, Args);
	EndProfilerTime(startTime, MICROSECONDS, spawnMobFuncTime, spawnMobNumTimes);
	isInSpawnMobFunc = false;
	return res;
};

TRoutine variableCloneFunc = nullptr;
TRoutine arrayCreateFunc = nullptr;
YYObjectBase* curStructPtr = nullptr;

void CreateEmptyStruct(CInstance* Self, YYObjectBase* structPtr, RValue* copyToRValue)
{
	RValue inputArg[1];
	inputArg[0].Kind = VALUE_OBJECT;
	inputArg[0].Object = structPtr;
	auto prevMap = structPtr->m_yyvarsMap;
	structPtr->m_yyvarsMap = nullptr;
	variableCloneFunc(copyToRValue, Self, nullptr, 1, inputArg);
	structPtr->m_yyvarsMap = prevMap;
}

void DeepStructCopy(CInstance* Self, RValue* copyFromObject, RValue* copyToObject)
{
	if (copyFromObject->Kind == VALUE_OBJECT)
	{
		auto copyFromMap = copyFromObject->Object->m_yyvarsMap;
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
				if (curRValue->Kind == VALUE_OBJECT && curRValue->Object->m_kind == 0)
				{
					RValue newStruct;
					CreateEmptyStruct(Self, curStructPtr, &newStruct);
					DeepStructCopy(Self, curRValue, &newStruct);
					StructSetFromHash(Self, copyToObject, copyFromMap->m_pBuckets[i].k, &newStruct);
				}
				else if (curRValue->Kind == VALUE_ARRAY)
				{
					RValue newArray;
					RValue inputArg[2];
					inputArg[0].Kind = VALUE_REAL;
					inputArg[0].Real = curRValue->RefArray->length;
					arrayCreateFunc(&newArray, Self, nullptr, 1, inputArg);
					DeepStructCopy(Self, curRValue, &newArray);
					StructSetFromHash(Self, copyToObject, copyFromMap->m_pBuckets[i].k, &newArray);

				}
				else
				{
					StructSetFromHash(Self, copyToObject, copyFromMap->m_pBuckets[i].k, curRValue);
				}
			}
		}
	}
	else if (copyFromObject->Kind = VALUE_ARRAY)
	{
		auto copyFromArr = copyFromObject->RefArray;
		auto copyToArr = copyToObject->RefArray;
		for (int i = 0; i < copyFromArr->length; i++)
		{
			auto curRValue = copyFromArr->m_Array[i];

			if (curRValue.Kind == VALUE_OBJECT && curRValue.Object->m_kind == 0)
			{
				RValue newStruct;
				CreateEmptyStruct(Self, curStructPtr, &newStruct);
				DeepStructCopy(Self, &curRValue, &newStruct);
				copyToArr->m_Array[i].Kind = VALUE_OBJECT;
				copyToArr->m_Array[i].Object = newStruct.Object;
			}
			else if (curRValue.Kind == VALUE_ARRAY)
			{
				RValue newArray;
				RValue inputArg[1];
				inputArg[0].Kind = VALUE_REAL;
				inputArg[0].Real = curRValue.RefArray->length;
				arrayCreateFunc(&newArray, Self, nullptr, 1, inputArg);
				DeepStructCopy(Self, &curRValue, &newArray);
				copyToArr->m_Array[i].Kind = VALUE_ARRAY;
				copyToArr->m_Array[i].RefArray = newArray.RefArray;
			}
			else if (curRValue.Kind == VALUE_STRING)
			{
				copyToArr->m_Array[i].Kind = VALUE_STRING;
				copyToArr->m_Array[i].String = RefString::Alloc(copyFromArr->m_Array[i].String->m_Thing, strlen(copyFromArr->m_Array[i].String->m_Thing));
			}
			else
			{
				memcpy(&(copyToArr->m_Array[i]), &curRValue, sizeof(RValue));
			}
		}
	}
	else
	{
		PrintMessage(CLR_RED, "UNEXPECTED COPY TYPE");
	}
}

ScriptFunc origActualSpawnMobScript = nullptr;
static long long acutalSpawnMobFuncTime = 0;
static int actualSpawnMobNumTimes = 0;

static bool isInActualSpawnMobFunc = false;

YYRValue* ActualSpawnMobFuncDetour(CInstance* Self, CInstance* Other, YYRValue* ReturnValue, int numArgs, YYRValue** Args)
{
	bool prevIsInSpawnMobFunc = isInSpawnMobFunc;
	isInSpawnMobFunc = false;
	isInActualSpawnMobFunc = true;
	long long startTime = StartProfilerTime();
	YYRValue* res = origActualSpawnMobScript(Self, Other, ReturnValue, numArgs, Args);
	EndProfilerTime(startTime, MICROSECONDS, acutalSpawnMobFuncTime, actualSpawnMobNumTimes);
	isInActualSpawnMobFunc = false;
	isInSpawnMobFunc = prevIsInSpawnMobFunc;
	return res;
};

ScriptFunc origVariableStructCopyScript = nullptr;
static long long variableStructCopyFuncTime = 0;
static int variableStructCopyNumTimes = 0;
YYRValue* VariableStructCopyFuncDetour(CInstance* Self, CInstance* Other, YYRValue* ReturnValue, int numArgs, YYRValue** Args)
{
	if (Args[0]->Kind == VALUE_OBJECT)
	{
		long long startTime = StartProfilerTime();
		curStructPtr = Args[0]->Object;

		DeepStructCopy(Self, (RValue*)Args[0], (RValue*)Args[1]);

		curStructPtr = nullptr;
		EndProfilerTime(startTime, MICROSECONDS, variableStructCopyFuncTime, variableStructCopyNumTimes);
		return Args[1];
	}
	long long startTime = StartProfilerTime();
	YYRValue* res = origVariableStructCopyScript(Self, Other, ReturnValue, numArgs, Args);
	EndProfilerTime(startTime, MICROSECONDS, variableStructCopyFuncTime, variableStructCopyNumTimes);
	return res;
};

TRoutine origInstanceCreateLayerScript = nullptr;
void* curMob = nullptr;

void InstanceCreateLayerDetour(RValue* Result, CInstance* Self, CInstance* Other, int numArgs, RValue* Args)
{
	origInstanceCreateLayerScript(Result, Self, Other, numArgs, Args);
	if (isInSpawnMobFunc)
	{
		curMob = Result->Pointer;
	}
}

static bool isInExecuteAttack = false;

TRoutine origVariableInstanceGetNamesScript = nullptr;
void VariableInstanceGetNamesDetour(RValue* Result, CInstance* Self, CInstance* Other, int numArgs, RValue* Args)
{
	if (isInDieFunc)
	{
		Result->Kind = VALUE_UNSET;
		return;
	}
	if (isInSpawnMobFunc || (isInActualSpawnMobFunc && !isInExecuteAttack))
	{
		int spriteIndexHash = GMLVarIndexMapYYTKHash[GML_sprite_index];
		RValue* spriteIndexPtr = nullptr;
		if (Args[0].Object->m_yyvarsMap->FindElement(spriteIndexHash, spriteIndexPtr))
		{
			RValue setStruct;
			if (isInActualSpawnMobFunc)
			{
				setStruct.Kind = VALUE_OBJECT;
				setStruct.Instance = Self;
			}
			else
			{
				setStruct.Kind = VALUE_REF;
				setStruct.Pointer = curMob;
			}
			auto configMap = Args[0].Object->m_yyvarsMap;
			if (configMap != nullptr)
			{
				int curSize = configMap->m_curSize;
				for (int i = 0; i < curSize; i++)
				{
					int curHash = configMap->m_pBuckets[i].Hash;
					if (curHash != 0 && curHash != spriteIndexHash)
					{
						StructSetFromHash(Self, &setStruct, configMap->m_pBuckets[i].k, configMap->m_pBuckets[i].v);
					}
				}
			}
			curMob = nullptr;
			Result->Kind = VALUE_UNSET;
			return;
		}
	}
	if (isInExecuteAttack)
	{
		if (Args[0].Kind == VALUE_OBJECT)
		{
			if (Self->i_objectindex == objPreCreateIndex)
			{
				RValue* setStruct = nullptr;
				Self->m_yyvarsMap->FindElement(GMLVarIndexMapYYTKHash[GML_config], setStruct);
				auto configMap = Args[0].Object->m_yyvarsMap;
				if (configMap != nullptr)
				{
					int curSize = configMap->m_curSize;
					for (int i = 0; i < curSize; i++)
					{
						int curHash = configMap->m_pBuckets[i].Hash;
						if (curHash != 0)
						{
							if (curHash == GMLVarIndexMapYYTKHash[GML_StampVars])
							{
								RValue newStruct;
								CreateEmptyStruct(Self, Args[0].Object, &newStruct);
								DeepStructCopy(Self, configMap->m_pBuckets[i].v, &newStruct);
								StructSetFromHash(Self, setStruct, configMap->m_pBuckets[i].k, &newStruct);
							}
							else
							{
								StructSetFromHash(Self, setStruct, configMap->m_pBuckets[i].k, configMap->m_pBuckets[i].v);
							}
						}
					}
				}
				Result->Kind = VALUE_UNSET;
				return;
			}
			else if (Self->i_objectindex == objAttackIndex)
			{
				RValue setStruct;
				setStruct.Kind = VALUE_OBJECT;
				setStruct.Instance = Self;
				auto configMap = Args[0].Object->m_yyvarsMap;
				if (configMap != nullptr)
				{
					int curSize = configMap->m_curSize;
					for (int i = 0; i < curSize; i++)
					{
						int curHash = configMap->m_pBuckets[i].Hash;
						if (curHash != 0)
						{
							StructSetFromHash(Self, &setStruct, configMap->m_pBuckets[i].k, configMap->m_pBuckets[i].v);
						}
					}
				}
				Result->Kind = VALUE_UNSET;
				return;
			}
			else
			{
				//				PrintMessage(CLR_RED, "UNEXPECTED GET NAMES CALL %d", Self->i_objectindex);
			}
		}
		else
		{
			//			PrintMessage(CLR_RED, "UNEXPECTED ARGS TYPE %d", Args[0].Kind);
		}
	}
	origVariableInstanceGetNamesScript(Result, Self, Other, numArgs, Args);
}

ScriptFunc origGLRLightSetActiveScript = nullptr;

YYRValue* GLRLightSetActiveFuncDetour(CInstance* Self, CInstance* Other, YYRValue* ReturnValue, int numArgs, YYRValue** Args)
{
	return origGLRLightSetActiveScript(Self, Other, ReturnValue, numArgs, Args);
};

ScriptFunc origGLRMeshSubmeshScript = nullptr;

static const char* enemyMeshBoundingBox = "[[-6,-4],[5,-4],[5,-15],[-6,-15]]";
static std::unordered_map<int, int> meshIndices;
static std::queue<int> availableMeshes;

static int numSpawnAdditionalMesh = 20;
static int idealMinAvailableMeshes = 150;

ScriptFunc origGLRMeshCreateScript = nullptr;
YYRValue* GLRMeshInputArgs[3];

YYRValue* GLRMeshCreateFuncDetour(CInstance* Self, CInstance* Other, YYRValue* ReturnValue, int numArgs, YYRValue** Args)
{
	if (Self->i_objectindex == objEnemyIndex && !availableMeshes.empty())
	{
		int meshIndex = availableMeshes.front();
		availableMeshes.pop();

		YYRValue Result;
		if (GLRMeshInputArgs[0] == nullptr)
		{
			GLRMeshInputArgs[0] = new YYRValue();
			GLRMeshInputArgs[1] = new YYRValue();
			GLRMeshInputArgs[2] = new YYRValue();
		}

		GLRMeshInputArgs[0]->Kind = VALUE_REAL;
		GLRMeshInputArgs[0]->Real = meshIndex;
		GLRMeshInputArgs[1]->Kind = VALUE_REAL;
		GLRMeshInputArgs[1]->Real = 1;
		origGLRLightSetActiveScript(Self, Other, &Result, 2, GLRMeshInputArgs);

		ReturnValue->Kind = VALUE_REAL;
		ReturnValue->Real = meshIndex;
		return ReturnValue;
	}
	return origGLRMeshCreateScript(Self, Other, ReturnValue, numArgs, Args);
};

ScriptFunc origGLRMeshUpdateScript = nullptr;

YYRValue* GLRMeshUpdateFuncDetour(CInstance* Self, CInstance* Other, YYRValue* ReturnValue, int numArgs, YYRValue** Args)
{
	int listNum = static_cast<int>(Args[0]->Real);
	if (meshIndices.find(listNum) != meshIndices.end())
	{
		if (meshIndices[listNum] == 1)
		{
			meshIndices[listNum] = 2;
		}
		else
		{
			meshIndices[listNum] = 2;
			return nullptr;
		}
	}
	return origGLRMeshUpdateScript(Self, Other, ReturnValue, numArgs, Args);
};

void moveMeshToQueue(CInstance* Self, int listNum)
{
	if (meshIndices[listNum] == 3)
	{
		return;
	}
	meshIndices[listNum] = 3;
	YYRValue Result;
	if (GLRMeshInputArgs[0] == nullptr)
	{
		GLRMeshInputArgs[0] = new YYRValue();
		GLRMeshInputArgs[1] = new YYRValue();
		GLRMeshInputArgs[2] = new YYRValue();
	}

	GLRMeshInputArgs[0]->Kind = VALUE_REAL;
	GLRMeshInputArgs[0]->Real = listNum;
	GLRMeshInputArgs[1]->Kind = VALUE_REAL;
	GLRMeshInputArgs[1]->Real = 0;
	origGLRLightSetActiveScript(Self, nullptr, &Result, 2, GLRMeshInputArgs);

	availableMeshes.push(listNum);
}

YYRValue* GLRMeshSubmeshFuncDetour(CInstance* Self, CInstance* Other, YYRValue* ReturnValue, int numArgs, YYRValue** Args)
{
	int meshListNum = static_cast<int>(Args[0]->Real);
	if (numSpawnAdditionalMesh > 0 && availableMeshes.size() < idealMinAvailableMeshes && strcmp(enemyMeshBoundingBox, Args[1]->String->m_Thing) == 0)
	{
		while (numSpawnAdditionalMesh > 0 && availableMeshes.size() < idealMinAvailableMeshes)
		{
			numSpawnAdditionalMesh--;
			if (GLRMeshInputArgs[0] == nullptr)
			{
				GLRMeshInputArgs[0] = new YYRValue();
				GLRMeshInputArgs[1] = new YYRValue();
				GLRMeshInputArgs[2] = new YYRValue();
			}
			GLRMeshInputArgs[0]->Kind = VALUE_REAL;
			GLRMeshInputArgs[0]->Real = 0;
			GLRMeshInputArgs[1]->Kind = VALUE_REAL;
			GLRMeshInputArgs[1]->Real = 0;
			GLRMeshInputArgs[2]->Kind = VALUE_REAL;
			GLRMeshInputArgs[2]->Real = 0;
			origGLRMeshCreateScript(Self, Other, ReturnValue, 3, GLRMeshInputArgs);
			int listNum = static_cast<int>(ReturnValue->Real);
			Args[0]->Real = listNum;
			origGLRMeshSubmeshScript(Self, Other, ReturnValue, numArgs, Args);
			meshIndices[listNum] = 2;
			GLRMeshInputArgs[0]->Real = listNum;
			origGLRMeshUpdateScript(Self, Other, ReturnValue, 1, GLRMeshInputArgs);
			moveMeshToQueue(Self, listNum);
		}
	}
	Args[0]->Real = meshListNum;
	if (meshIndices.find(meshListNum) != meshIndices.end())
	{
		return nullptr;
	}
	if (strcmp(enemyMeshBoundingBox, Args[1]->String->m_Thing) == 0)
	{
		meshIndices[meshListNum] = 1;
	}
	return origGLRMeshSubmeshScript(Self, Other, ReturnValue, numArgs, Args);
};

ScriptFunc origGLRMeshDestroyScript = nullptr;

YYRValue* GLRMeshDestroyFuncDetour(CInstance* Self, CInstance* Other, YYRValue* ReturnValue, int numArgs, YYRValue** Args)
{
	int listNum = static_cast<int>(Args[0]->Real);
	if (meshIndices.find(listNum) != meshIndices.end())
	{
		moveMeshToQueue(Self, listNum);
		return nullptr;
	}
	return origGLRMeshDestroyScript(Self, Other, ReturnValue, numArgs, Args);
};

ScriptFunc origCanSubmitScoreScript = nullptr;

YYRValue* CanSubmitScoreFuncDetour(CInstance* Self, CInstance* Other, YYRValue* ReturnValue, int numArgs, YYRValue** Args)
{
	ReturnValue->Kind = VALUE_BOOL;
	ReturnValue->Real = 0;
	return ReturnValue;
};

ScriptFunc origGLRMeshDestroyAllScript = nullptr;
static bool isInGLRMeshDestroyAll = false;

YYRValue* GLRMeshDestroyAllFuncDetour(CInstance* Self, CInstance* Other, YYRValue* ReturnValue, int numArgs, YYRValue** Args)
{
	isInGLRMeshDestroyAll = true;
	YYRValue* Res = origGLRMeshDestroyAllScript(Self, Other, ReturnValue, numArgs, Args);
	isInGLRMeshDestroyAll = false;
	return Res;
};

TRoutine DSListFindValueFunc = nullptr;

TRoutine origDSListSizeScript = nullptr;
void DSListSizeDetour(RValue* Result, CInstance* Self, CInstance* Other, int numArgs, RValue* Args)
{
	if (isInGLRMeshDestroyAll)
	{
		int GLRMeshDynListHash = GMLVarIndexMapYYTKHash[GML_GLR_MESH_DYN_LIST];
		RValue* GLRMeshDynListPtr = nullptr;
		globalInstancePtr->m_yyvarsMap->FindElement(GLRMeshDynListHash, GLRMeshDynListPtr);
		if (static_cast<int>(GLRMeshDynListPtr->Real) == static_cast<int>(Args[0].Real))
		{
			origDSListSizeScript(Result, Self, Other, numArgs, Args);
			int GLRMeshDynListSize = Result->Real;

			RValue inputArgs[2];
			inputArgs[0].Kind = VALUE_REAL;
			inputArgs[1].Kind = VALUE_REAL;
			for (int i = 0; i < GLRMeshDynListSize; i++)
			{
				inputArgs[0].Real = GLRMeshDynListPtr->Real;
				inputArgs[1].Real = i;
				DSListFindValueFunc(Result, Self, Other, 2, inputArgs);
				int listNum = static_cast<int>(Result->Real);
				moveMeshToQueue(Self, listNum);
			}

			Result->Kind = VALUE_REAL;
			Result->Real = 0;
			return;
		}
	}
	origDSListSizeScript(Result, Self, Other, numArgs, Args);
}

TRoutine origDSListClearScript = nullptr;

void DSListClearDetour(RValue* Result, CInstance* Self, CInstance* Other, int numArgs, RValue* Args)
{
	if (isInGLRMeshDestroyAll)
	{
		int GLRMeshDynListHash = GMLVarIndexMapYYTKHash[GML_GLR_MESH_DYN_LIST];
		RValue* GLRMeshDynListPtr = nullptr;
		globalInstancePtr->m_yyvarsMap->FindElement(GLRMeshDynListHash, GLRMeshDynListPtr);
		if (static_cast<int>(GLRMeshDynListPtr->Real) == static_cast<int>(Args[0].Real))
		{
			return;
		}
	}
	origDSListClearScript(Result, Self, Other, numArgs, Args);
}

ScriptFunc origExecuteAttackScript = nullptr;
static long long executeAttackFuncTime = 0;
static int executeAttackNumTimes = 0;

YYRValue* ExecuteAttackFuncDetour(CInstance* Self, CInstance* Other, YYRValue* ReturnValue, int numArgs, YYRValue** Args)
{
	isInExecuteAttack = true;
	long long startTime = StartProfilerTime();
	auto start = std::chrono::high_resolution_clock::now();
	YYRValue* Res = origExecuteAttackScript(Self, Other, ReturnValue, numArgs, Args);
	EndProfilerTime(startTime, MICROSECONDS, executeAttackFuncTime, executeAttackNumTimes);
	isInExecuteAttack = false;
	return Res;
};

int hashCoords(int xPos, int yPos)
{
	return xPos * 4129 + yPos * 4127;
}

bool isInPickupRange(float xCur, float xOther, float yCur, float yOther, float pickupRange)
{
	return ((xCur - xOther) * (xCur - xOther) + (yCur - yOther) * (yCur - yOther)) <= pickupRange * pickupRange;
}

RValue* timePtr = nullptr;

// This callback is registered on EVT_PRESENT and EVT_ENDSCENE, so it gets called every frame on DX9 / DX11 games.
YYTKStatus FrameCallback(YYTKEventBase* pEvent, void* OptionalArgument)
{
	FrameNumber++;

	numSpawnAdditionalMesh = 20;
	if (ENABLEPROFILER)
	{
		if (FrameNumber % 60 == 0)
		{
			YYRValue Result;
			PrintMessage(CLR_DEFAULT, "Number of Enemies Alive: %d, %d, %d"
				, NumberOfEnemiesCreated - NumberOfEnemyDeadCreated, numEnemyStepSkip, numEnemyStepCall);
			PrintMessage(CLR_DEFAULT, "available meshes: %d", availableMeshes.size());
			numEnemyStepSkip = 0;
			numEnemyStepCall = 0;
		}
		if (totTime >= 12000)
		{
			sortVec.clear();
			for (auto it = ProfilerMap.begin(); it != ProfilerMap.end(); it++)
			{
				sortVec.push_back(std::make_pair(it->second, it->first));
			}
			std::sort(sortVec.begin(), sortVec.end(), std::greater<>());
			outFile << "Frame Number: " << FrameNumber << " totTime: " << totTime << "\n";
			if (timePtr != nullptr)
			{
				char buffer[100];
				sprintf(buffer, "%d:%02d:%02d\n", static_cast<int>(timePtr->RefArray->m_Array[0].Real), static_cast<int>(timePtr->RefArray->m_Array[1].Real), static_cast<int>(timePtr->RefArray->m_Array[2].Real));
				outFile << buffer;
			}
			if (behaviourFuncTime > 10)
			{
				outFile << "behaviourFuncTime: " << behaviourFuncTime << " num times: " << behaviourNumTimes << "\n";
				outFile << "behaviourCollideWithPlayerFuncTime: " << behaviourCollideWithPlayerFuncTime << " num times: " << behaviourCollideWithPlayerNumTimes << "\n";
				outFile << "behaviourFollowPlayerFuncTime: " << behaviourFollowPlayerFuncTime << " num times: " << behaviourFollowPlayerNumTimes << "\n";
				outFile << "behaviourPowerScalingFuncTime: " << behaviourPowerScalingFuncTime << " num times: " << behaviourPowerScalingNumTimes << "\n";
			}
			if (executeAttackFuncTime > 1000)
			{
				outFile << "executeAttackFuncTime: " << executeAttackFuncTime << " num times: " << executeAttackNumTimes << "\n";
			}
			if (spawnMobFuncTime > 1000)
			{
				outFile << "spawnMobFuncTime: " << spawnMobFuncTime << " num times: " << spawnMobNumTimes << "\n";
				outFile << "acutalSpawnMobFuncTime: " << acutalSpawnMobFuncTime << " num times: " << actualSpawnMobNumTimes << "\n";
				outFile << "variableStructCopyFuncTime: " << variableStructCopyFuncTime << " num times: " << variableStructCopyNumTimes << "\n";
			}
			if (hitTargetFuncTime > 1000)
			{
				outFile << "hitTargetFuncTime: " << hitTargetFuncTime << " num times: " << hitTargetNumTimes << "\n";
				outFile << "calculateDamageFuncTime: " << calculateDamageFuncTime << " num times: " << calculateDamageNumTimes << "\n";
				outFile << "beforeDamageCalculationFastFuncTime: " << beforeDamageCalculationFastFuncTime << " num times: " << beforeDamageCalculationFastNumTimes << "\n";
				outFile << "beforeDamageCalculationSlowFuncTime: " << beforeDamageCalculationSlowFuncTime << " num times: " << beforeDamageCalculationSlowNumTimes << "\n";
				outFile << "onCriticalHitFastFuncTime: " << onCriticalHitFastFuncTime << " num times: " << onCriticalHitFastNumTimes << "\n";
				outFile << "onCriticalHitSlowFuncTime: " << onCriticalHitSlowFuncTime << " num times: " << onCriticalHitSlowNumTimes << "\n";
				outFile << "afterCriticalHitFastFuncTime: " << afterCriticalHitFastFuncTime << " num times: " << afterCriticalHitFastNumTimes << "\n";
				outFile << "afterCriticalHitSlowFuncTime: " << afterCriticalHitSlowFuncTime << " num times: " << afterCriticalHitSlowNumTimes << "\n";
				outFile << "takeDamageFuncTime: " << takeDamageFuncTime << " num times: " << takeDamageNumTimes << "\n";
				outFile << "onTakeDamageFastFuncTime func time: " << onTakeDamageFastFuncTime << " num times: " << onTakeDamageFastNumTimes << "\n";
				outFile << "onTakeDamageSlowFuncTime func time: " << onTakeDamageSlowFuncTime << " num times: " << onTakeDamageSlowNumTimes << "\n";
				outFile << "applyOnHitEffectsFuncTime: " << applyOnHitEffectsFuncTime << " num times: " << applyOnHitEffectsNumTimes << "\n";
				outFile << "applyOnHitEffectsSlowFuncTime: " << applyOnHitEffectsSlowFuncTime << " num times: " << applyOnHitEffectsSlowNumTimes << "\n";
				outFile << "applyKnockbackFastFuncTime: " << applyKnockbackFastFuncTime << " num times: " << applyKnockbackFastNumTimes << "\n";
				outFile << "applyKnockbackSlowFuncTime: " << applyKnockbackSlowFuncTime << " num times: " << applyKnockbackSlowNumTimes << "\n";
				outFile << "applyStatusEffectsFastFuncTime: " << applyStatusEffectsFastFuncTime << " num times: " << applyStatusEffectsFastNumTimes << "\n";
				outFile << "applyStatusEffectsSlowFuncTime: " << applyStatusEffectsSlowFuncTime << " num times: " << applyStatusEffectsSlowNumTimes << "\n";
				outFile << "onTakeDamageAfterFastFuncTime: " << onTakeDamageAfterFastFuncTime << " num times: " << onTakeDamageAfterFastNumTimes << "\n";
				outFile << "onTakeDamageAfterSlowFuncTime: " << onTakeDamageAfterSlowFuncTime << " num times: " << onTakeDamageAfterSlowNumTimes << "\n";
				outFile << "onDodgeFastFuncTime: " << onDodgeFastFuncTime << " num times: " << onDodgeFastNumTimes << "\n";
				outFile << "onDodgeSlowFuncTime: " << onDodgeSlowFuncTime << " num times: " << onDodgeSlowNumTimes << "\n";
				outFile << "applyDamageFuncTime: " << applyDamageFuncTime << " num times: " << applyDamageNumTimes << "\n";
				outFile << "hitNumberFuncTime: " << hitNumberFuncTime << " num times: " << hitNumberNumTimes << "\n";
				outFile << "onKillingHitFuncTime: " << onKillingHitFuncTime << " num times: " << onKillingHitNumTimes << "\n";
				outFile << "pushDamageDataFuncTime: " << pushDamageDataFuncTime << " num times: " << pushDamageDataNumTimes << "\n";
				outFile << "soundPlayFuncTime: " << soundPlayFuncTime << " num times: " << soundPlayNumTimes << "\n";
				outFile << "applyHitEffectFuncTime: " << applyHitEffectFuncTime << " num times: " << applyHitEffectNumTimes << "\n";
				outFile << "dieFuncTime: " << dieFuncTime << " num times: " << dieNumTimes << "\n";
				outFile << "dropEXPFuncTime: " << dropEXPFuncTime << " num times: " << dropEXPNumTimes << "\n";
				outFile << "VariableStructExistsFastFuncTime: " << VariableStructExistsFastFuncTime << " num times: " << VariableStructExistsFastNumTimes << "\n";
				outFile << "VariableStructExistsSlowFuncTime: " << VariableStructExistsSlowFuncTime << " num times: " << VariableStructExistsSlowNumTimes << "\n";
			}
			long long cumulativeTime = 0;
			for (auto it = sortVec.begin(); it != sortVec.end(); it++)
			{
				if (it->first < 500 || cumulativeTime >= totTime - 2000)
				{
					break;
				}
				cumulativeTime += it->first;
				outFile << codeIndexToName[it->second] << " " << it->first << "\n";
				//PrintMessage(CLR_DEFAULT, "%s %d", codeIndexToName[it->second], it->first);
			}
		}
		for (auto& it : funcParams)
		{
			YYRValue ret;
			PrintMessage(CLR_DEFAULT, "%d %d %d %d %d", it.Self, it.Other, it.ReturnValue, it.numArgs, it.Args);
			//		origTestFunc(it.Self, it.Other, &ret, 1, it.Args);
		}
		onTakeDamageFastNumTimes = 0;
		onTakeDamageFastFuncTime = 0;
		onTakeDamageSlowNumTimes = 0;
		onTakeDamageSlowFuncTime = 0;
		applyOnHitEffectsFuncTime = 0;
		applyOnHitEffectsNumTimes = 0;
		applyKnockbackFastFuncTime = 0;
		applyKnockbackFastNumTimes = 0;
		applyKnockbackSlowFuncTime = 0;
		applyKnockbackSlowNumTimes = 0;
		applyStatusEffectsFastFuncTime = 0;
		applyStatusEffectsFastNumTimes = 0;
		applyStatusEffectsSlowFuncTime = 0;
		applyStatusEffectsSlowNumTimes = 0;
		onTakeDamageAfterFastFuncTime = 0;
		onTakeDamageAfterFastNumTimes = 0;
		onTakeDamageAfterSlowFuncTime = 0;
		onTakeDamageAfterSlowNumTimes = 0;
		onDodgeFastFuncTime = 0;
		onDodgeFastNumTimes = 0;
		onDodgeSlowFuncTime = 0;
		onDodgeSlowNumTimes = 0;
		applyDamageFuncTime = 0;
		applyDamageNumTimes = 0;
		hitNumberFuncTime = 0;
		hitNumberNumTimes = 0;
		onKillingHitFuncTime = 0;
		onKillingHitNumTimes = 0;
		pushDamageDataFuncTime = 0;
		pushDamageDataNumTimes = 0;
		soundPlayFuncTime = 0;
		soundPlayNumTimes = 0;
		applyHitEffectFuncTime = 0;
		applyHitEffectNumTimes = 0;
		dieFuncTime = 0;
		dieNumTimes = 0;
		takeDamageFuncTime = 0;
		takeDamageNumTimes = 0;
		calculateDamageFuncTime = 0;
		calculateDamageNumTimes = 0;
		hitTargetFuncTime = 0;
		hitTargetNumTimes = 0;
		onCriticalHitFastFuncTime = 0;
		onCriticalHitFastNumTimes = 0;
		onCriticalHitSlowFuncTime = 0;
		onCriticalHitSlowNumTimes = 0;
		afterCriticalHitFastFuncTime = 0;
		afterCriticalHitFastNumTimes = 0;
		afterCriticalHitSlowFuncTime = 0;
		afterCriticalHitSlowNumTimes = 0;
		dropEXPFuncTime = 0;
		dropEXPNumTimes = 0;
		beforeDamageCalculationFastFuncTime = 0;
		beforeDamageCalculationFastNumTimes = 0;
		beforeDamageCalculationSlowFuncTime = 0;
		beforeDamageCalculationSlowNumTimes = 0;
		applyOnHitEffectsSlowFuncTime = 0;
		applyOnHitEffectsSlowNumTimes = 0;
		VariableStructExistsFastFuncTime = 0;
		VariableStructExistsFastNumTimes = 0;
		VariableStructExistsSlowFuncTime = 0;
		VariableStructExistsSlowNumTimes = 0;
		spawnMobFuncTime = 0;
		spawnMobNumTimes = 0;
		acutalSpawnMobFuncTime = 0;
		actualSpawnMobNumTimes = 0;
		variableStructCopyFuncTime = 0;
		variableStructCopyNumTimes = 0;
		executeAttackFuncTime = 0;
		executeAttackNumTimes = 0;
		behaviourFuncTime = 0;
		behaviourNumTimes = 0;
		behaviourCollideWithPlayerFuncTime = 0;
		behaviourCollideWithPlayerNumTimes = 0;
		behaviourFollowPlayerFuncTime = 0;
		behaviourFollowPlayerNumTimes = 0;
		behaviourPowerScalingFuncTime = 0;
		behaviourPowerScalingNumTimes = 0;
		totTime = 0;
		ProfilerMap.clear();
		funcParams.clear();
	}

	// Tell the core the handler was successful.
	return YYTK_OK;
}

// This callback is registered on EVT_CODE_EXECUTE, so it gets called every game function call.
YYTKStatus CodeCallback(YYTKCodeEvent* pCodeEvent, void* OptionalArgument)
{
	std::tuple<CInstance*, CInstance*, CCode*, RValue*, int> args = pCodeEvent->Arguments();

	CInstance*	Self	= std::get<0>(args);
	CInstance*	Other	= std::get<1>(args);
	CCode*		Code	= std::get<2>(args);
	RValue*		Res		= std::get<3>(args);
	int			Flags	= std::get<4>(args);

	if (!Code->i_pName)
	{
		return YYTK_INVALIDARG;
	}

	/*
	if (uniqueCode.find(Code->i_pName) == uniqueCode.end())
	{
		uniqueCode.insert(Code->i_pName);
		PrintMessage(CLR_DEFAULT, "%s", Code->i_pName);
	}
	*/
	auto codeFunc = codeFuncTable.find(Code->i_CodeIndex);
	if (codeFunc != codeFuncTable.end())
	{
		long long startTime = StartProfilerTime();
		codeFunc->second(pCodeEvent, Self, Other, Code, Res, Flags);
		long long curTime = 0;
		int numTimes = 0;
		EndProfilerTime(startTime, MICROSECONDS, curTime, numTimes);
		if (ProfilerMap.count(Code->i_CodeIndex) == 0)
		{
			ProfilerMap[Code->i_CodeIndex] = 0;
		}
		totTime += curTime;
		ProfilerMap[Code->i_CodeIndex] += curTime;
	}
	else // Haven't cached the function in the table yet. Run the if statements and assign the function to the code index
	{
		codeIndexToName[Code->i_CodeIndex] = Code->i_pName;
		if (_strcmpi(Code->i_pName, "gml_Object_obj_Enemy_Create_0") == 0)
		{
			auto Enemy_Create_0 = [](YYTKCodeEvent* pCodeEvent, CInstance* Self, CInstance* Other, CCode* Code, RValue* Res, int Flags) {
				NumberOfEnemiesCreated++;
				pCodeEvent->Call(Self, Other, Code, Res, Flags);
			};
			Enemy_Create_0(pCodeEvent, Self, Other, Code, Res, Flags);
			codeFuncTable[Code->i_CodeIndex] = Enemy_Create_0;
		}
		else if (_strcmpi(Code->i_pName, "gml_Object_obj_EnemyDead_Create_0") == 0)
		{
			auto EnemyDead_Create_0 = [](YYTKCodeEvent* pCodeEvent, CInstance* Self, CInstance* Other, CCode* Code, RValue* Res, int Flags) {
				NumberOfEnemyDeadCreated++;
				pCodeEvent->Call(Self, Other, Code, Res, Flags);
			};
			EnemyDead_Create_0(pCodeEvent, Self, Other, Code, Res, Flags);
			codeFuncTable[Code->i_CodeIndex] = EnemyDead_Create_0;
		}
		else if (_strcmpi(Code->i_pName, "gml_Object_obj_Player_Create_0") == 0) // Assume a new game has started
		{
			auto Player_Create_0 = [](YYTKCodeEvent* pCodeEvent, CInstance* Self, CInstance* Other, CCode* Code, RValue* Res, int Flags) {
				{
					RValue Res;
					RValue arg{};
					arg.Kind = VALUE_STRING;
					arg.String = RefString::Alloc("time", 4);
					variableGetHashFunc(&Res, nullptr, nullptr, 1, &arg);
					int timeHash = CHashMap<int, RValue>::CalculateHash(static_cast<int>(Res.Real)) % (1ll << 31);
					globalInstancePtr->m_yyvarsMap->FindElement(timeHash, timePtr);
				}
				PickupRangePotentialUpdate = true;
				FrameNumber = 0;
				LastRainbowEXPFrameNumber = 0;
				NumberOfEnemiesCreated = 0;
				NumberOfEnemyDeadCreated = 0;
				numDamageText = 0;
				EXPIDMap.clear();
				attackHitCooldownMap.clear();
				bucketGridMap.clear();
				previousFrameBoundingBoxMap.clear();
				currentPlayer = PlayerInstance();
				currentSummon = GMLInstance();
				pCodeEvent->Call(Self, Other, Code, Res, Flags);
			};
			Player_Create_0(pCodeEvent, Self, Other, Code, Res, Flags);
			codeFuncTable[Code->i_CodeIndex] = Player_Create_0;
		}
		else if (_strcmpi(Code->i_pName, "gml_Object_obj_Player_Step_0") == 0)
		{
			auto Player_Step_0 = [pCodeEvent](YYTKCodeEvent* pCodeEvent, CInstance* Self, CInstance* Other, CCode* Code, RValue* Res, int Flags) {
				pCodeEvent->Call(Self, Other, Code, Res, Flags);
				if (PickupRangePotentialUpdate)
				{
					YYRValue Result;
					CallBuiltin(Result, "variable_instance_get", Self, Other, { (long long)Self->i_id, "pickupRange" });
					currentPlayer.pickupRange = static_cast<int>(Result);
					PickupRangePotentialUpdate = false;
				}
				currentPlayer.gmlInstance.ID = Self->i_id;
				currentPlayer.gmlInstance.SpriteIndex = Self->i_spriteindex;
				currentPlayer.gmlInstance.xPos = Self->i_x;
				currentPlayer.gmlInstance.yPos = Self->i_y;
				CameraXPos = Self->i_x - PlayerScreenXOffset;
				CameraYPos = Self->i_y - PlayerScreenYOffset;
			};
			Player_Step_0(pCodeEvent, Self, Other, Code, Res, Flags);
			codeFuncTable[Code->i_CodeIndex] = Player_Step_0;
		}
		else if (_strcmpi(Code->i_pName, "gml_Object_obj_EXP_Step_0") == 0)
		{
			auto EXP_Step_0 = [](YYTKCodeEvent* pCodeEvent, CInstance* Self, CInstance* Other, CCode* Code, RValue* Res, int Flags) {
				float pickupRange = EXPIDMap[Self->i_id].Range + (EXPIDMap[Self->i_id].Range * currentPlayer.pickupRange) / 100.0;
				EXPIDMap[Self->i_id].gmlInstance.xPos = Self->i_x;
				EXPIDMap[Self->i_id].gmlInstance.yPos = Self->i_y;
				if (FrameNumber - EXPIDMap[Self->i_id].gmlInstance.InitialSpawnFrame >= 40)
				{
					if (abs(Self->i_speed) < 1e-3 &&
						LastRainbowEXPFrameNumber < EXPIDMap[Self->i_id].gmlInstance.InitialSpawnFrame &&
						!isInPickupRange(currentPlayer.gmlInstance.xPos, Self->i_x, currentPlayer.gmlInstance.yPos, Self->i_y, pickupRange) &&
						!isInPickupRange(currentSummon.xPos, Self->i_x, currentSummon.yPos, Self->i_y, pickupRange))
					{
						pCodeEvent->Cancel(true);
					}
					else
					{
						pCodeEvent->Call(Self, Other, Code, Res, Flags);
					}
				}
				else
				{
					pCodeEvent->Call(Self, Other, Code, Res, Flags);
				}
			};
			EXP_Step_0(pCodeEvent, Self, Other, Code, Res, Flags);
			codeFuncTable[Code->i_CodeIndex] = EXP_Step_0;
		}
		else if (_strcmpi(Code->i_pName, "gml_Object_obj_EXP_Collision_obj_EXP") == 0)
		{
			auto EXP_Collision_obj_EXP = [](YYTKCodeEvent* pCodeEvent, CInstance* Self, CInstance* Other, CCode* Code, RValue* Res, int Flags) {
				//Pretty hacky way of waking up the other exp to see if it's the one that needs to merge
				if (FrameNumber - EXPIDMap[Other->i_id].gmlInstance.InitialSpawnFrame >= 40)
				{
					EXPIDMap[Other->i_id].gmlInstance.InitialSpawnFrame = FrameNumber - 35;
				}
				pCodeEvent->Call(Self, Other, Code, Res, Flags);
			};
			EXP_Collision_obj_EXP(pCodeEvent, Self, Other, Code, Res, Flags);
			codeFuncTable[Code->i_CodeIndex] = EXP_Collision_obj_EXP;
		}
		else if (_strcmpi(Code->i_pName, "gml_Object_obj_EXP_Create_0") == 0)
		{
			auto EXP_Create_0 = [](YYTKCodeEvent* pCodeEvent, CInstance* Self, CInstance* Other, CCode* Code, RValue* Res, int Flags) {
				YYRValue Result;
				pCodeEvent->Call(Self, Other, Code, Res, Flags);
				if (EXPRange == -1)
				{
					CallBuiltin(Result, "variable_instance_get", Self, Other, { (long long)Self->i_id, "range" });
					EXPRange = static_cast<int>(Result);
				}
				EXPIDMap[Self->i_id] = EXPInstance(EXPRange, GMLInstance(Self->i_id, Self->i_spriteindex, FrameNumber, Self->i_x, Self->i_y));
			};
			EXP_Create_0(pCodeEvent, Self, Other, Code, Res, Flags);
			codeFuncTable[Code->i_CodeIndex] = EXP_Create_0;
		}
		else if (_strcmpi(Code->i_pName, "gml_Object_obj_PlayerManager_Alarm_11") == 0)
		{
			auto PlayerManager_Alarm_11 = [](YYTKCodeEvent* pCodeEvent, CInstance* Self, CInstance* Other, CCode* Code, RValue* Res, int Flags) {
				PickupRangePotentialUpdate = true;
				pCodeEvent->Call(Self, Other, Code, Res, Flags);
			};
			PlayerManager_Alarm_11(pCodeEvent, Self, Other, Code, Res, Flags);
			codeFuncTable[Code->i_CodeIndex] = PlayerManager_Alarm_11;
		}
		else if (_strcmpi(Code->i_pName, "gml_Object_obj_PlayerManager_Step_0") == 0)
		{
			auto PlayerManager_Step_0 = [](YYTKCodeEvent* pCodeEvent, CInstance* Self, CInstance* Other, CCode* Code, RValue* Res, int Flags) {
				pCodeEvent->Call(Self, Other, Code, Res, Flags);
			};
			PlayerManager_Step_0(pCodeEvent, Self, Other, Code, Res, Flags);
			codeFuncTable[Code->i_CodeIndex] = PlayerManager_Step_0;
		}
		else if (_strcmpi(Code->i_pName, "gml_Object_obj_EXPAbsorb_Collision_obj_Player") == 0)
		{
			auto EXPAbsorb_Collision_obj_Player = [](YYTKCodeEvent* pCodeEvent, CInstance* Self, CInstance* Other, CCode* Code, RValue* Res, int Flags) {
				LastRainbowEXPFrameNumber = FrameNumber;
				pCodeEvent->Call(Self, Other, Code, Res, Flags);
			};
			EXPAbsorb_Collision_obj_Player(pCodeEvent, Self, Other, Code, Res, Flags);
			codeFuncTable[Code->i_CodeIndex] = EXPAbsorb_Collision_obj_Player;
		}
		else if (_strcmpi(Code->i_pName, "gml_Object_obj_Summon_Create_0") == 0)
		{
			auto Summon_Create_0 = [](YYTKCodeEvent* pCodeEvent, CInstance* Self, CInstance* Other, CCode* Code, RValue* Res, int Flags) {
				currentSummon.xPos = Self->i_x;
				currentSummon.yPos = Self->i_y;
				pCodeEvent->Call(Self, Other, Code, Res, Flags);
			};
			Summon_Create_0(pCodeEvent, Self, Other, Code, Res, Flags);
			codeFuncTable[Code->i_CodeIndex] = Summon_Create_0;
		}
		else if (_strcmpi(Code->i_pName, "gml_Object_obj_Summon_Step_0") == 0)
		{
			auto Summon_Step_0 = [](YYTKCodeEvent* pCodeEvent, CInstance* Self, CInstance* Other, CCode* Code, RValue* Res, int Flags) {
				currentSummon.xPos = Self->i_x;
				currentSummon.yPos = Self->i_y;
				pCodeEvent->Call(Self, Other, Code, Res, Flags);
			};
			Summon_Step_0(pCodeEvent, Self, Other, Code, Res, Flags);
			codeFuncTable[Code->i_CodeIndex] = Summon_Step_0;
		}
		else if (_strcmpi(Code->i_pName, "gml_Object_obj_TextController_Create_0") == 0)
		{
			auto TextController_Create_0 = [](YYTKCodeEvent* pCodeEvent, CInstance* Self, CInstance* Other, CCode* Code, RValue* Res, int Flags) {
				YYRValue Result;
				pCodeEvent->Call(Self, Other, Code, Res, Flags);
				CallBuiltin(Result, "variable_global_get", Self, Other, { "TextContainer" });
				YYRValue tempResult;
				CallBuiltin(tempResult, "struct_get", Self, Other, { Result, "titleButtons" });
				YYRValue tempResultOne;
				CallBuiltin(tempResultOne, "struct_get", Self, Other, { tempResult, "eng" });

				tempResultOne.RefArray->m_Array[0].String = &tempVar;
			};
			TextController_Create_0(pCodeEvent, Self, Other, Code, Res, Flags);
			codeFuncTable[Code->i_CodeIndex] = TextController_Create_0;
		}
		else if (_strcmpi(Code->i_pName, "gml_Object_obj_Attack_Create_0") == 0)
		{
			auto Attack_Create_0 = [](YYTKCodeEvent* pCodeEvent, CInstance* Self, CInstance* Other, CCode* Code, RValue* Res, int Flags) {
				attackHitEnemyFrameNumberMap[Self->i_id] = std::unordered_map<int, uint32_t>();
				pCodeEvent->Call(Self, Other, Code, Res, Flags);
				RValue* hitCDPtr = nullptr;
				Self->m_yyvarsMap->FindElement(GMLVarIndexMapYYTKHash[GML_hitCD], hitCDPtr);
				attackHitCooldownMap[Self->i_spriteindex] = static_cast<int>(hitCDPtr->Real);
			};
			Attack_Create_0(pCodeEvent, Self, Other, Code, Res, Flags);
			codeFuncTable[Code->i_CodeIndex] = Attack_Create_0;
		}
		else if (_strcmpi(Code->i_pName, "gml_Object_obj_Attack_Step_0") == 0)
		{
			auto Attack_Step_0 = [](YYTKCodeEvent* pCodeEvent, CInstance* Self, CInstance* Other, CCode* Code, RValue* Res, int Flags) {
				pCodeEvent->Call(Self, Other, Code, Res, Flags);
			};
			Attack_Step_0(pCodeEvent, Self, Other, Code, Res, Flags);
			codeFuncTable[Code->i_CodeIndex] = Attack_Step_0;
		}
		else if (_strcmpi(Code->i_pName, "gml_Object_obj_Attack_Collision_obj_BaseMob") == 0)
		{
			auto Attack_Collision_obj_BaseMob = [](YYTKCodeEvent* pCodeEvent, CInstance* Self, CInstance* Other, CCode* Code, RValue* Res, int Flags) {
				pCodeEvent->Call(Self, Other, Code, Res, Flags);
			};
			Attack_Collision_obj_BaseMob(pCodeEvent, Self, Other, Code, Res, Flags);
			codeFuncTable[Code->i_CodeIndex] = Attack_Collision_obj_BaseMob;
		}
		else if (_strcmpi(Code->i_pName, "gml_Object_obj_Attack_Destroy_0") == 0)
		{
			auto Attack_Destroy_0 = [](YYTKCodeEvent* pCodeEvent, CInstance* Self, CInstance* Other, CCode* Code, RValue* Res, int Flags) {
				attackHitEnemyFrameNumberMap[Self->i_id].clear();
				attackHitEnemyFrameNumberMap.erase(Self->i_id);
				pCodeEvent->Call(Self, Other, Code, Res, Flags);
			};
			Attack_Destroy_0(pCodeEvent, Self, Other, Code, Res, Flags);
			codeFuncTable[Code->i_CodeIndex] = Attack_Destroy_0;
		}
		else if (_strcmpi(Code->i_pName, "gml_Object_obj_damageText_Alarm_0") == 0)
		{
			auto damageText_Alarm_0 = [](YYTKCodeEvent* pCodeEvent, CInstance* Self, CInstance* Other, CCode* Code, RValue* Res, int Flags) {
				pCodeEvent->Call(Self, Other, Code, Res, Flags);
				RValue* durationPtr = nullptr;
				int durationHash = GMLVarIndexMapYYTKHash[GML_duration];
				Self->m_yyvarsMap->FindElement(durationHash, durationPtr);
				if (durationPtr->Real <= -2)
				{
					numDamageText--;
				}
			};
			damageText_Alarm_0(pCodeEvent, Self, Other, Code, Res, Flags);
			codeFuncTable[Code->i_CodeIndex] = damageText_Alarm_0;
		}
		else // Not using this code function, so just quickly ignore it when it's called next time
		{
			auto UnmodifiedFunc = [](YYTKCodeEvent* pCodeEvent, CInstance* Self, CInstance* Other, CCode* Code, RValue* Res, int Flags) {
				pCodeEvent->Call(Self, Other, Code, Res, Flags);
			};
			UnmodifiedFunc(pCodeEvent, Self, Other, Code, Res, Flags);
			codeFuncTable[Code->i_CodeIndex] = UnmodifiedFunc;
		}
	}
	// Tell the core the handler was successful.
	return YYTK_OK;
}

// Create an entry routine - it must be named exactly this, and must accept these exact arguments.
// It must also be declared DllExport (notice how the other functions are not).
DllExport YYTKStatus PluginEntry(YYTKPlugin* PluginObject)
{
	// Set the unload routine
	PluginObject->PluginUnload = PluginUnload;

	// Print a message to the console
	PrintMessage(CLR_DEFAULT, "[Optimization Plugin] - Hello from PluginEntry!");

	PluginAttributes_t* PluginAttributes = nullptr;

	// Get the attributes for the plugin - this is an opaque structure, as it may change without any warning.
	// If Status == YYTK_OK (0), then PluginAttributes is guaranteed to be valid (non-null).
	if (YYTKStatus Status = PmGetPluginAttributes(PluginObject, PluginAttributes))
	{
		PrintError(__FILE__, __LINE__, "[Optimization Plugin] - PmGetPluginAttributes failed with 0x%x", Status);
		return YYTK_FAIL;
	}

	// Register a callback for frame events
	YYTKStatus Status = PmCreateCallback(
		PluginAttributes,					// Plugin Attributes
		g_pFrameCallbackAttributes,				// (out) Callback Attributes
		FrameCallback,						// The function to register as a callback
		static_cast<EventType>(EVT_PRESENT | EVT_ENDSCENE), // Which events trigger this callback
		nullptr								// The optional argument to pass to the function
	);

	if (Status)
	{
		PrintError(__FILE__, __LINE__, "[Optimization Plugin] - PmCreateCallback failed with 0x%x", Status);
		return YYTK_FAIL;
	}

	// Register a callback for frame events
	Status = PmCreateCallback(
		PluginAttributes,					// Plugin Attributes
		g_pCodeCallbackAttributes,			// (out) Callback Attributes
		reinterpret_cast<FNEventHandler>(CodeCallback),	// The function to register as a callback
		static_cast<EventType>(EVT_CODE_EXECUTE), // Which events trigger this callback
		nullptr								// The optional argument to pass to the function
	);

	if (Status)
	{
		PrintError(__FILE__, __LINE__, "[Optimization Plugin] - PmCreateCallback failed with 0x%x", Status);
		return YYTK_FAIL;
	}

	YYRValue Result;

	GetFunctionByName("asset_get_index", assetGetIndexFunc);
	MH_Initialize();
	MmGetScriptData(scriptList);

	HookScriptFunction("gml_Script_OnTakeDamage_gml_Object_obj_BaseMob_Create_0",				(void*)&OnTakeDamageFuncDetour,				(void**)&origOnTakeDamageScript);
	HookScriptFunction("gml_Script_ApplyOnHitEffects_gml_Object_obj_BaseMob_Create_0",			(void*)&ApplyOnHitEffectsFuncDetour,		(void**)&origApplyOnHitEffectsScript);
	HookScriptFunction("gml_Script_ApplyKnockback_gml_Object_obj_BaseMob_Create_0",				(void*)&ApplyKnockbackFuncDetour,			(void**)&origApplyKnockbackScript);
	HookScriptFunction("gml_Script_ApplyStatusEffects_gml_Object_obj_BaseMob_Create_0",			(void*)&ApplyStatusEffectsFuncDetour,		(void**)&origApplyStatusEffectsScript);
	HookScriptFunction("gml_Script_OnTakeDamageAfter_gml_Object_obj_BaseMob_Create_0",			(void*)&OnTakeDamageAfterFuncDetour,		(void**)&origOnTakeDamageAfterScript);
	HookScriptFunction("gml_Script_OnDodge_gml_Object_obj_BaseMob_Create_0",					(void*)&OnDodgeFuncDetour,					(void**)&origOnDodgeScript);
	HookScriptFunction("gml_Script_ApplyDamage_gml_Object_obj_BaseMob_Create_0",				(void*)&ApplyDamageFuncDetour,				(void**)&origApplyDamageScript);
	HookScriptFunction("gml_Script_HitNumber_gml_Object_obj_BaseMob_Create_0",					(void*)&HitNumberFuncDetour,				(void**)&origHitNumberScript);
	HookScriptFunction("gml_Script_OnKillingHit_gml_Object_obj_BaseMob_Create_0",				(void*)&OnKillingHitFuncDetour,				(void**)&origOnKillingHitScript);
	HookScriptFunction("gml_Script_PushDamageData",												(void*)&PushDamageDataFuncDetour,			(void**)&origPushDamageDataScript);
	HookScriptFunction("gml_Script_soundPlay",													(void*)&SoundPlayFuncDetour,				(void**)&origSoundPlayScript);
	HookScriptFunction("gml_Script_ApplyHitEffect_gml_Object_obj_BaseMob_Create_0",				(void*)&ApplyHitEffectFuncDetour,			(void**)&origApplyHitEffectScript);
	HookScriptFunction("gml_Script_Die_gml_Object_obj_Enemy_Create_0",							(void*)&DieFuncDetour,						(void**)&origDieScript);
	HookScriptFunction("gml_Script_HitTarget_gml_Object_obj_Attack_Create_0",					(void*)&HitTargetFuncDetour,				(void**)&origHitTargetScript);
	HookScriptFunction("gml_Script_TakeDamage_gml_Object_obj_BaseMob_Create_0",					(void*)&TakeDamageFuncDetour,				(void**)&origTakeDamageScript);
	HookScriptFunction("gml_Script_TakeDamage_gml_Object_obj_Obstacle_Create_0",				(void*)&TakeDamageObstacleFuncDetour,		(void**)&origTakeDamageObstacleScript);
	HookScriptFunction("gml_Script_TakeDamage_gml_Object_obj_Obstacle2_Create_0",				(void*)&TakeDamageObstacleTwoFuncDetour,	(void**)&origTakeDamageObstacleTwoScript);
	HookScriptFunction("gml_Script_CalculateDamage_gml_Object_obj_AttackController_Create_0",	(void*)&CalculateDamageFuncDetour,			(void**)&origCalculateDamageScript);
	HookScriptFunction("gml_Script_OnCriticalHit_gml_Object_obj_BaseMob_Create_0",				(void*)&OnCriticalHitFuncDetour,			(void**)&origOnCriticalHitScript);
	HookScriptFunction("gml_Script_AfterCriticalHit_gml_Object_obj_BaseMob_Create_0",			(void*)&AfterCriticalHitFuncDetour,			(void**)&origAfterCriticalHitScript);
	HookScriptFunction("gml_Script_BeforeDamageCalculation_gml_Object_obj_BaseMob_Create_0",	(void*)&BeforeDamageCalculationFuncDetour,	(void**)&origBeforeDamageCalculationScript);
	HookScriptFunction("gml_Script_DropExp_gml_Object_obj_Enemy_Create_0",						(void*)&DropEXPFuncDetour,					(void**)&origDropEXPScript);
	HookScriptFunction("gml_Script_SpawnMob_gml_Object_obj_MobManager_Create_0",				(void*)&SpawnMobFuncDetour,					(void**)&origSpawnMobScript);
	HookScriptFunction("gml_Script_ActualSpawn_SpawnMob_gml_Object_obj_MobManager_Create_0",	(void*)&ActualSpawnMobFuncDetour,			(void**)&origActualSpawnMobScript);
	HookScriptFunction("gml_Script_variable_struct_copy",										(void*)&VariableStructCopyFuncDetour,		(void**)&origVariableStructCopyScript);
	HookScriptFunction("gml_Script_glr_mesh_destroy_all",										(void*)&GLRMeshDestroyAllFuncDetour,		(void**)&origGLRMeshDestroyAllScript);
	HookScriptFunction("gml_Script_glr_mesh_create",											(void*)&GLRMeshCreateFuncDetour,			(void**)&origGLRMeshCreateScript);
	HookScriptFunction("gml_Script_glr_mesh_submesh_add_json",									(void*)&GLRMeshSubmeshFuncDetour,			(void**)&origGLRMeshSubmeshScript);
	HookScriptFunction("gml_Script_glr_mesh_update",											(void*)&GLRMeshUpdateFuncDetour,			(void**)&origGLRMeshUpdateScript);
	HookScriptFunction("gml_Script_glr_light_set_active",										(void*)&GLRLightSetActiveFuncDetour,		(void**)&origGLRLightSetActiveScript);
	HookScriptFunction("gml_Script_glr_mesh_destroy",											(void*)&GLRMeshDestroyFuncDetour,			(void**)&origGLRMeshDestroyScript);
	HookScriptFunction("gml_Script_CanSubmitScore_gml_Object_obj_PlayerManager_Create_0",		(void*)&CanSubmitScoreFuncDetour,			(void**)&origCanSubmitScoreScript);
	HookScriptFunction("gml_Script_ExecuteAttack_gml_Object_obj_AttackController_Create_0",		(void*)&ExecuteAttackFuncDetour,			(void**)&origExecuteAttackScript);
	
	GetFunctionByName("method_call", scriptExecuteFunc);

	TRoutine variableStructExists;
	GetFunctionByName("variable_struct_exists", variableStructExists);
	Hook(
		(void*)&VariableStructExistsDetour,
		(void*)(variableStructExists),
		(void**)&origVariableStructExistsScript,
		"VariableStructExistsScript"
	);

	TRoutine stringScript;
	GetFunctionByName("string", stringScript);
	Hook(
		(void*)&StringDetour,
		(void*)(stringScript),
		(void**)&origStringScript,
		"StringScript"
	);

	TRoutine dsMapSetScript;
	GetFunctionByName("ds_map_set", dsMapSetScript);
	Hook(
		(void*)&DSMapSetDetour,
		(void*)(dsMapSetScript),
		(void**)&origDSMapSetScript,
		"dsMapSetScript"
	);

	TRoutine variableInstanceGetNames;
	GetFunctionByName("variable_instance_get_names", variableInstanceGetNames);
	Hook(
		(void*)&VariableInstanceGetNamesDetour,
		(void*)(variableInstanceGetNames),
		(void**)&origVariableInstanceGetNamesScript,
		"VariableInstanceGetNamesScript"
	);

	TRoutine arrayLengthScript;
	GetFunctionByName("array_length", arrayLengthScript);
	Hook(
		(void*)&ArrayLengthDetour,
		(void*)(arrayLengthScript),
		(void**)&origArrayLengthScript,
		"ArrayLengthScript"
	);

	TRoutine showDebugMessageScript;
	GetFunctionByName("show_debug_message", showDebugMessageScript);
	Hook(
		(void*)&ShowDebugMessageDetour,
		(void*)(showDebugMessageScript),
		(void**)&origShowDebugMessageScript,
		"ShowDebugMessageScript"
	);

	TRoutine instanceCreateDepthScript;
	GetFunctionByName("instance_create_depth", instanceCreateDepthScript);
	Hook(
		(void*)&InstanceCreateDepthDetour,
		(void*)(instanceCreateDepthScript),
		(void**)&origInstanceCreateDepthScript,
		"InstanceCreateDepthScript"
	);

	TRoutine instanceCreateLayerScript;
	GetFunctionByName("instance_create_layer", instanceCreateLayerScript);
	Hook(
		(void*)&InstanceCreateLayerDetour,
		(void*)(instanceCreateLayerScript),
		(void**)&origInstanceCreateLayerScript,
		"InstanceCreateLayerScript"
	);

	TRoutine dsListSizeScript;
	GetFunctionByName("ds_list_size", dsListSizeScript);
	Hook(
		(void*)&DSListSizeDetour,
		(void*)(dsListSizeScript),
		(void**)&origDSListSizeScript,
		"DSListSizeScript"
	);

	TRoutine dsListClearScript;
	GetFunctionByName("ds_list_clear", dsListClearScript);
	Hook(
		(void*)&DSListClearDetour,
		(void*)(dsListClearScript),
		(void**)&origDSListClearScript,
		"DSListClearScript"
	);

	TRoutine globalScript;
	GetFunctionByName("@@GlobalScope@@", globalScript);
	{
		RValue Res;
		globalScript(&Res, nullptr, nullptr, 0, nullptr);
		globalInstancePtr = Res.Instance;
	}

	GetFunctionByName("variable_get_hash", variableGetHashFunc);
	GetFunctionByName("variable_clone", variableCloneFunc);
	GetFunctionByName("struct_set_from_hash", structSetFromHashFunc);
	GetFunctionByName("array_create", arrayCreateFunc);
	GetFunctionByName("ds_list_find_value", DSListFindValueFunc);

	for (int i = 0; i < std::extent<decltype(VariableNamesStringsArr)>::value; i++)
	{
		RValue Res;
		RValue arg{};
		arg.Kind = VALUE_STRING;
		arg.String = RefString::Alloc(VariableNamesStringsArr[i], strlen(VariableNamesStringsArr[i]));
		variableGetHashFunc(&Res, nullptr, nullptr, 1, &arg);
		GMLVarIndexMapGMLHash[i] = static_cast<int>(Res.Real);
		GMLVarIndexMapYYTKHash[i] = CHashMap<int, RValue>::CalculateHash(static_cast<int>(Res.Real)) % (1ll << 31);
	}

	objDamageTextIndex = getAssetIndexFromName("obj_damageText");
	objEnemyIndex = getAssetIndexFromName("obj_Enemy");
	objPreCreateIndex = getAssetIndexFromName("obj_PreCreate");
	objAttackIndex = getAssetIndexFromName("obj_Attack");

	if (ENABLEPROFILER)
	{
		CallBuiltin(Result, "show_debug_overlay", nullptr, nullptr, { true });
		outFile.open("test.txt");
	}

	// Off it goes to the core.
	return YYTK_OK;
}

// The routine that gets called on plugin unload.
// Registered in PluginEntry - you should use this to release resources.
YYTKStatus PluginUnload()
{
	YYTKStatus Removal = PmRemoveCallback(g_pFrameCallbackAttributes);

	// If we didn't succeed in removing the callback.
	if (Removal != YYTK_OK)
	{
		PrintError(__FILE__, __LINE__, "[Optimization Plugin] PmRemoveCallback failed with 0x%x", Removal);
	}

	Removal = PmRemoveCallback(g_pCodeCallbackAttributes);

	// If we didn't succeed in removing the callback.
	if (Removal != YYTK_OK)
	{
		PrintError(__FILE__, __LINE__, "[Optimization Plugin] PmRemoveCallback failed with 0x%x", Removal);
	}

	PrintMessage(CLR_DEFAULT, "[Optimization Plugin] - Goodbye!");

	return YYTK_OK;
}

// Boilerplate setup for a Windows DLL, can just return TRUE.
// This has to be here or else you get linker errors (unless you disable the main method)
BOOL APIENTRY DllMain(
	HMODULE hModule,
	DWORD  ul_reason_for_call,
	LPVOID lpReserved
)
{
	return 1;
}