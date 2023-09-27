#define YYSDK_PLUGIN
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

//		PrintMessage(CLR_GRAY, "- &%s = 0x%p", Name, TargetFuncPointer);
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

void HookScriptFunction(const char* scriptFunctionName, void* detourFunction, void** origScript)
{
	RValue Result;
	RValue arg{};
	arg.Kind = VALUE_STRING;
	arg.String = RefString::Alloc(scriptFunctionName, strlen(scriptFunctionName));
	assetGetIndexFunc(&Result, nullptr, nullptr, 1, &arg);

	int scriptFunctionIndex = static_cast<int>(Result.Real) - 100000;

	CScript* CScript = scriptList(scriptFunctionIndex);

	Hook(
		detourFunction,
		(void*)(CScript->s_pFunc->pScriptFunc),
		origScript,
		scriptFunctionName
	);
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

static int enemyVarIndexMap[1001];
static int baseMobVarIndexMap[1001];
static int attackIndexMap[1001];

static std::vector<std::pair<long long, int>> sortVec;

static long long totTime = 0;

static std::unordered_map<int, std::function<void(YYTKCodeEvent* pCodeEvent, CInstance* Self, CInstance* Other, CCode* Code, RValue* Res, int Flags)>> codeFuncTable;

static std::unordered_map<int, std::function<void(CInstance* Self)>> behaviourFuncTable;

RefString tempVar = RefString("Test", 4, false);

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
	testStruct(): Self(nullptr), Other(nullptr), ReturnValue(nullptr), numArgs(0), Args(nullptr)
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


YYRValue* RunStructFunctions(CInstance* Self, CInstance* Other, YYRValue* ReturnValue, int numArgs, YYRValue** Args, auto& setFuncParams, int numParams, int structIndex, int returnArgIndex)
{
	int StructHash = baseMobVarIndexMap[structIndex];
	RValue* StructRef = nullptr;
	Self->m_yyvarsMap->FindElement(StructHash, StructRef);
	YYObjectBase* Struct = StructRef->Object;

	if (!(Struct->m_yyvarsMap) || Struct->m_yyvarsMap->m_numUsed == 0)
	{
		ReturnValue->Kind = Args[returnArgIndex]->Kind;
		ReturnValue->Real = Args[returnArgIndex]->Real;
		return Args[returnArgIndex];
	}

	int baseMobArrHash = baseMobVarIndexMap[1000];
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
			Args[returnArgIndex]->Real = returnValue.Real;
		}
	}
	baseMobArrRef->RefArray->m_Array = prevRefArr;
	baseMobArrRef->RefArray->length = prevRefArrLength;
	curFuncParamDepth--;

	ReturnValue->Kind = Args[returnArgIndex]->Kind;
	ReturnValue->Real = Args[returnArgIndex]->Real;
	return Args[returnArgIndex];
}

ScriptFunc origOnTakeDamageScript = nullptr;
static long long onTakeDamageFuncTime = 0;
static int onTakeDamageNumTimes = 0;

YYRValue* OnTakeDamageFuncDetour(CInstance* Self, CInstance* Other, YYRValue* ReturnValue, int numArgs, YYRValue** Args)
{
	auto start = std::chrono::high_resolution_clock::now();

	int invincibleHash = baseMobVarIndexMap[6];
	RValue* invincibleRef = nullptr;
	Self->m_yyvarsMap->FindElement(invincibleHash, invincibleRef);

	auto setFuncParams = [invincibleRef](RValue* paramArr, YYRValue** Args, CInstance* Self) {
		paramArr[0] = *(Args[0]);
		paramArr[1] = *(Args[1]);
		paramArr[2] = *(Args[2]);
		paramArr[3].Kind = VALUE_OBJECT;
		paramArr[3].Instance = Self;
		paramArr[4] = *invincibleRef;
		paramArr[5] = *(Args[3]);
	};

	int numParams = 6;
	int structIndex = 3;
	int returnArgIndex = 0;

	RunStructFunctions(Self, Other, ReturnValue, numArgs, Args, setFuncParams, numParams, structIndex, returnArgIndex);

	auto end = std::chrono::high_resolution_clock::now();
	onTakeDamageFuncTime += std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
	onTakeDamageNumTimes++;
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
	auto start = std::chrono::high_resolution_clock::now();
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
		ReturnValue->Kind = Args[0]->Kind;
		ReturnValue->Real = Args[0]->Real;
		auto end = std::chrono::high_resolution_clock::now();
		applyOnHitEffectsFuncTime += std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
		applyOnHitEffectsNumTimes++;
		return Args[0];
	}

	RValue* isEnemyPtr = nullptr;
	int isEnemyHash = attackIndexMap[0];
	curHitTargetAttackInstance->m_yyvarsMap->FindElement(isEnemyHash, isEnemyPtr);
	RValue* creatorPtr = nullptr;
	int creatorHash = attackIndexMap[1];
	curHitTargetAttackInstance->m_yyvarsMap->FindElement(creatorHash, creatorPtr);
	RValue* onHitEffectsPtr = nullptr;
	int onHitEffectsHash = attackIndexMap[3];
	curHitTargetAttackInstance->m_yyvarsMap->FindElement(onHitEffectsHash, onHitEffectsPtr);
	
	RValue* creatorOnHitEffectsPtr = nullptr;
	if (isEnemyPtr == nullptr || (creatorPtr != nullptr && isEnemyPtr->Real != 0))
	{
		creatorPtr->Object->m_yyvarsMap->FindElement(attackIndexMap[3], creatorOnHitEffectsPtr);
	}

	if (onHitEffectsPtr == nullptr)
	{
		ReturnValue->Kind = Args[0]->Kind;
		ReturnValue->Real = Args[0]->Real;
		auto end = std::chrono::high_resolution_clock::now();
		applyOnHitEffectsFuncTime += std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
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
		auto end = std::chrono::high_resolution_clock::now();
		applyOnHitEffectsSlowFuncTime += std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
		applyOnHitEffectsSlowNumTimes++;
		return Res;
	}

	if (onHitIdsHashMap.empty())
	{
		ReturnValue->Kind = Args[0]->Kind;
		ReturnValue->Real = Args[0]->Real;
		return Args[0];
	}

	int baseMobArrHash = baseMobVarIndexMap[1000];
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

	auto end = std::chrono::high_resolution_clock::now();
	applyOnHitEffectsFuncTime += std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
	applyOnHitEffectsNumTimes++;
	return Args[0];
};

TRoutine origDSMapSetScript = nullptr;
void DSMapSetDetour(RValue* Result, CInstance* Self, CInstance* Other, int numArgs, RValue* Args)
{
	int StructHash = attackIndexMap[2];
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
static long long applyKnockbackFuncTime = 0;
static int applyKnockbackNumTimes = 0;

YYRValue* ApplyKnockbackFuncDetour(CInstance* Self, CInstance* Other, YYRValue* ReturnValue, int numArgs, YYRValue** Args)
{
	auto start = std::chrono::high_resolution_clock::now();
	YYRValue* res = origApplyKnockbackScript(Self, Other, ReturnValue, numArgs, Args);
	auto end = std::chrono::high_resolution_clock::now();
	applyKnockbackFuncTime += std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
	applyKnockbackNumTimes++;
	return res;
};

ScriptFunc origApplyStatusEffectsScript = nullptr;
static long long applyStatusEffectsFuncTime = 0;
static int applyStatusEffectsNumTimes = 0;

YYRValue* ApplyStatusEffectsFuncDetour(CInstance* Self, CInstance* Other, YYRValue* ReturnValue, int numArgs, YYRValue** Args)
{
	auto start = std::chrono::high_resolution_clock::now();
	YYRValue* res = origApplyStatusEffectsScript(Self, Other, ReturnValue, numArgs, Args);
	auto end = std::chrono::high_resolution_clock::now();
	applyStatusEffectsFuncTime += std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
	applyStatusEffectsNumTimes++;
	return res;
};

ScriptFunc origOnTakeDamageAfterScript = nullptr;
static long long onTakeDamageAfterFuncTime = 0;
static int onTakeDamageAfterNumTimes = 0;

YYRValue* OnTakeDamageAfterFuncDetour(CInstance* Self, CInstance* Other, YYRValue* ReturnValue, int numArgs, YYRValue** Args)
{
	auto start = std::chrono::high_resolution_clock::now();

	int invincibleHash = baseMobVarIndexMap[6];
	RValue* invincibleRef = nullptr;
	Self->m_yyvarsMap->FindElement(invincibleHash, invincibleRef);

	auto setFuncParams = [invincibleRef](RValue* paramArr, YYRValue** Args, CInstance* Self) {
		paramArr[0] = *(Args[0]);
		paramArr[1] = *(Args[1]);
		paramArr[2] = *(Args[2]);
		paramArr[3].Kind = VALUE_OBJECT;
		paramArr[3].Instance = Self;
		paramArr[4] = *invincibleRef;
		paramArr[5] = *(Args[3]);
	};

	int numParams = 6;
	int structIndex = 4;
	int returnArgIndex = 0;

	RunStructFunctions(Self, Other, ReturnValue, numArgs, Args, setFuncParams, numParams, structIndex, returnArgIndex);
	
	auto end = std::chrono::high_resolution_clock::now();
	onTakeDamageAfterFuncTime += std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
	onTakeDamageAfterNumTimes++;
	return Args[0];
};

ScriptFunc origOnDodgeScript = nullptr;
static long long onDodgeFuncTime = 0;
static int onDodgeNumTimes = 0;

YYRValue* OnDodgeFuncDetour(CInstance* Self, CInstance* Other, YYRValue* ReturnValue, int numArgs, YYRValue** Args)
{
	auto start = std::chrono::high_resolution_clock::now();

	auto setFuncParams = [](RValue* paramArr, YYRValue** Args, CInstance* Self) {
		paramArr[0] = *(Args[0]);
		paramArr[1] = *(Args[1]);
		paramArr[2] = *(Args[2]);
		paramArr[3].Kind = VALUE_OBJECT;
		paramArr[3].Instance = Self;
	};

	int numParams = 4;
	int structIndex = 5;
	int returnArgIndex = 0;

	RunStructFunctions(Self, Other, ReturnValue, numArgs, Args, setFuncParams, numParams, structIndex, returnArgIndex);

	auto end = std::chrono::high_resolution_clock::now();
	onDodgeFuncTime += std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
	onDodgeNumTimes++;
	return Args[0];
};

ScriptFunc origApplyDamageScript = nullptr;
static long long applyDamageFuncTime = 0;
static int applyDamageNumTimes = 0;

static bool isInApplyDamageFunc = false;

YYRValue* ApplyDamageFuncDetour(CInstance* Self, CInstance* Other, YYRValue* ReturnValue, int numArgs, YYRValue** Args)
{
	isInApplyDamageFunc = true;
	auto start = std::chrono::high_resolution_clock::now();
	YYRValue* res = origApplyDamageScript(Self, Other, ReturnValue, numArgs, Args);
	auto end = std::chrono::high_resolution_clock::now();
	applyDamageFuncTime += std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
	applyDamageNumTimes++;
	isInApplyDamageFunc = false;
	return res;
};

ScriptFunc origHitNumberScript = nullptr;
static long long hitNumberFuncTime = 0;
static int hitNumberNumTimes = 0;

YYRValue* HitNumberFuncDetour(CInstance* Self, CInstance* Other, YYRValue* ReturnValue, int numArgs, YYRValue** Args)
{
	auto start = std::chrono::high_resolution_clock::now();
	YYRValue* res = origHitNumberScript(Self, Other, ReturnValue, numArgs, Args);
	auto end = std::chrono::high_resolution_clock::now();
	hitNumberFuncTime += std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
	hitNumberNumTimes++;
	return res;
};

ScriptFunc origOnKillingHitScript = nullptr;
static long long onKillingHitFuncTime = 0;
static int onKillingHitNumTimes = 0;

YYRValue* OnKillingHitFuncDetour(CInstance* Self, CInstance* Other, YYRValue* ReturnValue, int numArgs, YYRValue** Args)
{
	auto start = std::chrono::high_resolution_clock::now();
	YYRValue* res = origOnKillingHitScript(Self, Other, ReturnValue, numArgs, Args);
	auto end = std::chrono::high_resolution_clock::now();
	onKillingHitFuncTime += std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
	onKillingHitNumTimes++;
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
	auto start = std::chrono::high_resolution_clock::now();
	YYRValue* res = origSoundPlayScript(Self, Other, ReturnValue, numArgs, Args);
	auto end = std::chrono::high_resolution_clock::now();
	soundPlayFuncTime += std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
	soundPlayNumTimes++;
	return res;
};

ScriptFunc origApplyHitEffectScript = nullptr;
static long long applyHitEffectFuncTime = 0;
static int applyHitEffectNumTimes = 0;

YYRValue* ApplyHitEffectFuncDetour(CInstance* Self, CInstance* Other, YYRValue* ReturnValue, int numArgs, YYRValue** Args)
{
	auto start = std::chrono::high_resolution_clock::now();
	YYRValue* res = origApplyHitEffectScript(Self, Other, ReturnValue, numArgs, Args);
	auto end = std::chrono::high_resolution_clock::now();
	applyHitEffectFuncTime += std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
	applyHitEffectNumTimes++;
	return res;
};

ScriptFunc origDieScript = nullptr;
static long long dieFuncTime = 0;
static int dieNumTimes = 0;

YYRValue* DieFuncDetour(CInstance* Self, CInstance* Other, YYRValue* ReturnValue, int numArgs, YYRValue** Args)
{
	auto start = std::chrono::high_resolution_clock::now();
	YYRValue* res = origDieScript(Self, Other, ReturnValue, numArgs, Args);
	auto end = std::chrono::high_resolution_clock::now();
	dieFuncTime += std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
	dieNumTimes++;
	return res;
};

ScriptFunc origTakeDamageScript = nullptr;
static long long takeDamageFuncTime = 0;
static int takeDamageNumTimes = 0;

std::chrono::steady_clock::time_point takeDamageStartTime;

YYRValue* TakeDamageFuncDetour(CInstance* Self, CInstance* Other, YYRValue* ReturnValue, int numArgs, YYRValue** Args)
{
	CInstance* prevCurHitTargetAttackInstance = curHitTargetAttackInstance;
	if (!isInHitTarget)
	{
		curHitTargetAttackInstance = nullptr;
	}
	bool prevIsInHitTarget = isInHitTarget;
	isInHitTarget = false;
	auto start = std::chrono::high_resolution_clock::now();
	YYRValue* res = nullptr;
//	for (int i = 0; i < 10000; i++)
	{
		res = origTakeDamageScript(Self, Other, ReturnValue, numArgs, Args);
	}
	auto end = std::chrono::high_resolution_clock::now();
	takeDamageFuncTime += std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
	takeDamageNumTimes++;
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
	auto start = std::chrono::high_resolution_clock::now();
	YYRValue* res = nullptr;
//	for (int i = 0; i < 10000; i++)
	{
		res = origCalculateDamageScript(Self, Other, ReturnValue, numArgs, Args);
	}
	auto end = std::chrono::high_resolution_clock::now();
	calculateDamageFuncTime += std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
	calculateDamageNumTimes++;
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
	auto start = std::chrono::high_resolution_clock::now();
	YYRValue* res = nullptr;
//	for (int i = 0; i < 100000; i++)
	{
		res = origHitTargetScript(Self, Other, ReturnValue, numArgs, Args);
	}
	auto end = std::chrono::high_resolution_clock::now();
	hitTargetFuncTime += std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
	hitTargetNumTimes++;
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
	auto start = std::chrono::high_resolution_clock::now();
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
		auto end = std::chrono::high_resolution_clock::now();
		VariableStructExistsFastFuncTime += std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
		VariableStructExistsFastNumTimes++;
		return;
	}

	//Need to find a way to also get the optimization working for references
	origVariableStructExistsScript(Result, Self, Other, numArgs, Args);
	auto end = std::chrono::high_resolution_clock::now();
	VariableStructExistsSlowFuncTime += std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
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

ScriptFunc origOnCriticalHitScript = nullptr;
static long long onCriticalHitFuncTime = 0;
static int onCriticalHitNumTimes = 0;

static RValue funcParamArr;

YYRValue* OnCriticalHitFuncDetour(CInstance* Self, CInstance* Other, YYRValue* ReturnValue, int numArgs, YYRValue** Args)
{
	auto start = std::chrono::high_resolution_clock::now();

	auto setFuncParams = [](RValue* paramArr, YYRValue** Args, CInstance* Self) {
		paramArr[0].Kind = VALUE_OBJECT;
		paramArr[0].Instance = Self;
		paramArr[1] = *(Args[0]);
		paramArr[2] = *(Args[1]);
		paramArr[3] = *(Args[2]);
	};

	int numParams = 4;
	int structIndex = 2;
	int returnArgIndex = 2;

	RunStructFunctions(Self, Other, ReturnValue, numArgs, Args, setFuncParams, numParams, structIndex, returnArgIndex);

	auto end = std::chrono::high_resolution_clock::now();
	onCriticalHitFuncTime += std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
	onCriticalHitNumTimes++;
	return Args[2];
};

ScriptFunc origAfterCriticalHitScript = nullptr;
static long long afterCriticalHitQuickFuncTime = 0;
static int afterCriticalHitQuickNumTimes = 0;
static long long afterCriticalHitSlowFuncTime = 0;
static int afterCriticalHitSlowNumTimes = 0;

YYRValue* AfterCriticalHitFuncDetour(CInstance* Self, CInstance* Other, YYRValue* ReturnValue, int numArgs, YYRValue** Args)
{
	auto start = std::chrono::high_resolution_clock::now();

	auto setFuncParams = [](RValue* paramArr, YYRValue** Args, CInstance* Self) {
		paramArr[0].Kind = VALUE_OBJECT;
		paramArr[0].Instance = Self;
		paramArr[1] = *(Args[0]);
		paramArr[2] = *(Args[1]);
		paramArr[3] = *(Args[2]);
	};

	int numParams = 4;
	int structIndex = 1;
	int returnArgIndex = 2;

	RunStructFunctions(Self, Other, ReturnValue, numArgs, Args, setFuncParams, numParams, structIndex, returnArgIndex);

	auto end = std::chrono::high_resolution_clock::now();
	afterCriticalHitSlowFuncTime += std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
	afterCriticalHitSlowNumTimes++;
	return Args[2];
};

ScriptFunc origBeforeDamageCalculationScript = nullptr;
static long long beforeDamageCalculationSlowFuncTime = 0;
static int beforeDamageCalculationSlowNumTimes = 0;
static long long beforeDamageCalculationQuickFuncTime = 0;
static int beforeDamageCalculationQuickNumTimes = 0;

YYRValue* BeforeDamageCalculationFuncDetour(CInstance* Self, CInstance* Other, YYRValue* ReturnValue, int numArgs, YYRValue** Args)
{
	auto start = std::chrono::high_resolution_clock::now();

	auto setFuncParams = [](RValue* paramArr, YYRValue** Args, CInstance* Self) {
		paramArr[0] = *(Args[0]);
		paramArr[1] = *(Args[1]);
		paramArr[2] = *(Args[2]);
	};

	int numParams = 4;
	int structIndex = 0;
	int returnArgIndex = 2;

	RunStructFunctions(Self, Other, ReturnValue, numArgs, Args, setFuncParams, numParams, structIndex, returnArgIndex);

	auto end = std::chrono::high_resolution_clock::now();
	beforeDamageCalculationSlowFuncTime += std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
	beforeDamageCalculationSlowNumTimes++;
	return Args[2];
};

ScriptFunc origDropEXPScript = nullptr;
static long long dropEXPFuncTime = 0;
static int dropEXPNumTimes = 0;

YYRValue* DropEXPFuncDetour(CInstance* Self, CInstance* Other, YYRValue* ReturnValue, int numArgs, YYRValue** Args)
{
	auto start = std::chrono::high_resolution_clock::now();
	YYRValue* res = origDropEXPScript(Self, Other, ReturnValue, numArgs, Args);
	auto end = std::chrono::high_resolution_clock::now();
	dropEXPFuncTime += std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
	dropEXPNumTimes++;
	return res;
};

int hashCoords(int xPos, int yPos)
{
	return xPos * 4129 + yPos * 4127;
}

bool isInPickupRange(float xCur, float xOther, float yCur, float yOther, float pickupRange)
{
	return ((xCur - xOther) * (xCur - xOther) + (yCur - yOther) * (yCur - yOther)) <= pickupRange * pickupRange;
}

// This callback is registered on EVT_PRESENT and EVT_ENDSCENE, so it gets called every frame on DX9 / DX11 games.
YYTKStatus FrameCallback(YYTKEventBase* pEvent, void* OptionalArgument)
{
	FrameNumber++;

	if (FrameNumber % 60 == 0)
	{
		YYRValue Result;
		PrintMessage(CLR_DEFAULT, "Number of Enemies Alive: %d, %d, %d"
			, NumberOfEnemiesCreated - NumberOfEnemyDeadCreated, numEnemyStepSkip, numEnemyStepCall);
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
		if (hitTargetFuncTime > 1000)
		{
			outFile << "hitTargetFuncTime: " << hitTargetFuncTime << " num times: " << hitTargetNumTimes << "\n";
			outFile << "calculateDamageFuncTime: " << calculateDamageFuncTime << " num times: " << calculateDamageNumTimes << "\n";
			outFile << "beforeDamageCalculationQuickFuncTime: " << beforeDamageCalculationQuickFuncTime << " num times: " << beforeDamageCalculationQuickNumTimes << "\n";
			outFile << "beforeDamageCalculationSlowFuncTime: " << beforeDamageCalculationSlowFuncTime << " num times: " << beforeDamageCalculationSlowNumTimes << "\n";
			outFile << "onCriticalHitFuncTime: " << onCriticalHitFuncTime << " num times: " << onCriticalHitNumTimes << "\n";
			outFile << "afterCriticalHitQuickFuncTime: " << afterCriticalHitQuickFuncTime << " num times: " << afterCriticalHitQuickNumTimes << "\n";
			outFile << "afterCriticalHitSlowFuncTime: " << afterCriticalHitSlowFuncTime << " num times: " << afterCriticalHitSlowNumTimes << "\n";
			outFile << "takeDamageFuncTime: " << takeDamageFuncTime << " num times: " << takeDamageNumTimes << "\n";
			outFile << "onTakeDamage func time: " << onTakeDamageFuncTime << " num times: " << onTakeDamageNumTimes << "\n";
			outFile << "applyOnHitEffectsFuncTime: " << applyOnHitEffectsFuncTime << " num times: " << applyOnHitEffectsNumTimes << "\n";
			outFile << "applyOnHitEffectsSlowFuncTime: " << applyOnHitEffectsSlowFuncTime << " num times: " << applyOnHitEffectsSlowNumTimes << "\n";
			outFile << "applyKnockbackFuncTime: " << applyKnockbackFuncTime << " num times: " << applyKnockbackNumTimes << "\n";
			outFile << "applyStatusEffectsFuncTime: " << applyStatusEffectsFuncTime << " num times: " << applyStatusEffectsNumTimes << "\n";
			outFile << "onTakeDamageAfterFuncTime: " << onTakeDamageAfterFuncTime << " num times: " << onTakeDamageAfterNumTimes << "\n";
			outFile << "onDodgeFuncTime: " << onDodgeFuncTime << " num times: " << onDodgeNumTimes << "\n";
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
	onTakeDamageNumTimes = 0;
	onTakeDamageFuncTime = 0;
	applyOnHitEffectsFuncTime = 0;
	applyOnHitEffectsNumTimes = 0;
	applyKnockbackFuncTime = 0;
	applyKnockbackNumTimes = 0;
	applyStatusEffectsFuncTime = 0;
	applyStatusEffectsNumTimes = 0;
	onTakeDamageAfterFuncTime = 0;
	onTakeDamageAfterNumTimes = 0;
	onDodgeFuncTime = 0;
	onDodgeNumTimes = 0;
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
	onCriticalHitFuncTime = 0;
	onCriticalHitNumTimes = 0;
	afterCriticalHitQuickFuncTime = 0;
	afterCriticalHitQuickNumTimes = 0;
	afterCriticalHitSlowFuncTime = 0;
	afterCriticalHitSlowNumTimes = 0;
	dropEXPFuncTime = 0;
	dropEXPNumTimes = 0;
	beforeDamageCalculationQuickFuncTime = 0;
	beforeDamageCalculationQuickNumTimes = 0;
	beforeDamageCalculationSlowFuncTime = 0;
	beforeDamageCalculationSlowNumTimes = 0;
	applyOnHitEffectsSlowFuncTime = 0;
	applyOnHitEffectsSlowNumTimes = 0;
	VariableStructExistsFastFuncTime = 0;
	VariableStructExistsFastNumTimes = 0;
	VariableStructExistsSlowFuncTime = 0;
	VariableStructExistsSlowNumTimes = 0;
	totTime = 0;
	ProfilerMap.clear();
	funcParams.clear();

	// Tell the core the handler was successful.
	return YYTK_OK;
}

// This callback is registered on EVT_CODE_EXECUTE, so it gets called every game function call.
YYTKStatus CodeCallback(YYTKEventBase* pEvent, void* OptionalArgument)
{

	YYTKCodeEvent* pCodeEvent = dynamic_cast<decltype(pCodeEvent)>(pEvent);

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

	if (codeFuncTable.count(Code->i_CodeIndex) != 0)
	{
		if (codeFuncTable[Code->i_CodeIndex] != nullptr)
		{
			auto start = std::chrono::high_resolution_clock::now();
			codeFuncTable[Code->i_CodeIndex](pCodeEvent, Self, Other, Code, Res, Flags);
			auto end = std::chrono::high_resolution_clock::now();
			if (ProfilerMap.count(Code->i_CodeIndex) == 0)
			{
				ProfilerMap[Code->i_CodeIndex] = 0;
			}
			totTime += std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
			ProfilerMap[Code->i_CodeIndex] += std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
		}
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
				PickupRangePotentialUpdate = true;
				FrameNumber = 0;
				LastRainbowEXPFrameNumber = 0;
				NumberOfEnemiesCreated = 0;
				NumberOfEnemyDeadCreated = 0;
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
					PrintMessage(CLR_DEFAULT, "New pickup range: %d", currentPlayer.pickupRange);
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
		else if (_strcmpi(Code->i_pName, "gml_Object_obj_MobManager_Other_12") == 0)
		{
			behaviourFuncTable.clear();
			auto MobManager_Other_12 = [](YYTKCodeEvent* pCodeEvent, CInstance* Self, CInstance* Other, CCode* Code, RValue* Res, int Flags) {
				pCodeEvent->Call(Self, Other, Code, Res, Flags);
				YYRValue Result;
				CallBuiltin(Result, "variable_instance_get", Self, Other, { (long long)Self->i_id, "behaviours" });
				YYRValue ResultOne;
				YYRValue ResultTwo;
				CallBuiltin(ResultOne, "struct_get", Self, Other, { Result, "collideWithPlayer" });
				CallBuiltin(ResultTwo, "struct_get", Self, Other, { ResultOne, "Script" });
				auto collideWithPlayerFunc = [](CInstance* Self) {
				};
				behaviourFuncTable[(int)(void*)ResultTwo.Object] = collideWithPlayerFunc;
				CallBuiltin(ResultOne, "struct_get", Self, Other, { Result, "followPlayer" });
				CallBuiltin(ResultTwo, "struct_get", Self, Other, { ResultOne, "Script" });
				auto followPlayerFunc = [](CInstance* Self) {
					if (currentPlayer.gmlInstance.xPos > Self->i_x)
					{
						Self->i_imagescalex = abs(Self->i_imagescalex);
					}
					else
					{
						Self->i_imagescalex = -abs(Self->i_imagescalex);
					}
					RValue* SPDRef = nullptr;
					Self->m_yyvarsMap->FindElement(enemyVarIndexMap[14], SPDRef);
					RValue* directionMovingRef = nullptr;
					Self->m_yyvarsMap->FindElement(enemyVarIndexMap[15], directionMovingRef);
					RValue* directionChangeTimeRef = nullptr;
					Self->m_yyvarsMap->FindElement(enemyVarIndexMap[8], directionChangeTimeRef);
					double curDirectionAngle = directionMovingRef->Real;
					if (directionChangeTimeRef->Real == 0)
					{
						curDirectionAngle = 180.0 / M_PI * atan2(currentPlayer.gmlInstance.yPos - Self->i_y, currentPlayer.gmlInstance.xPos - Self->i_x);
						if (curDirectionAngle < 0)
						{
							curDirectionAngle += 360;
						}
						directionChangeTimeRef->Real = 3;
						directionMovingRef->Real = curDirectionAngle;
					}
					if (curDirectionAngle >= 180)
					{
						curDirectionAngle -= 360;
					}
					double speed = SPDRef->Real;
					Self->i_x += cos(curDirectionAngle * M_PI / 180.0) * speed;
					Self->i_y += sin(curDirectionAngle * M_PI / 180.0) * speed;
				};
				behaviourFuncTable[(int)(void*)ResultTwo.Object] = followPlayerFunc;
				CallBuiltin(ResultOne, "struct_get", Self, Other, { Result, "powerScaling" });
				CallBuiltin(ResultTwo, "struct_get", Self, Other, { ResultOne, "Script" });
				auto powerScalingFunc = [](CInstance* Self) {
				};
				behaviourFuncTable[(int)(void*)ResultTwo.Object] = powerScalingFunc;
			};
			MobManager_Other_12(pCodeEvent, Self, Other, Code, Res, Flags);
			codeFuncTable[Code->i_CodeIndex] = MobManager_Other_12;
		}
		else if (_strcmpi(Code->i_pName, "gml_Object_obj_Enemy_Step_0") == 0)
		{
			auto Enemy_Step_0 = [](YYTKCodeEvent* pCodeEvent, CInstance* Self, CInstance* Other, CCode* Code, RValue* Res, int Flags) {
				RValue* collidedCDRef = nullptr;
				Self->m_yyvarsMap->FindElement(enemyVarIndexMap[0], collidedCDRef);
				RValue* lifeTimeRef = nullptr;
				Self->m_yyvarsMap->FindElement(enemyVarIndexMap[1], lifeTimeRef);
				if (collidedCDRef->Real > 0.5 && lifeTimeRef->Real > 1.5)
				{
					RValue* behaviourStructRef = nullptr;
					Self->m_yyvarsMap->FindElement(enemyVarIndexMap[2], behaviourStructRef);
					if (behaviourStructRef->Object->m_yyvarsMap)
					{
						std::vector<int> funcAddressList;
						bool hasAllScripts = true;

						for (int i = 0; i < behaviourStructRef->Object->m_yyvarsMap->m_curSize; i++)
						{
							if (behaviourStructRef->Object->m_yyvarsMap->m_pBuckets[i].Hash != 0)
							{
								RValue* enemyBehaviour = behaviourStructRef->Object->m_yyvarsMap->m_pBuckets[i].v;
								//find the bucket that contains the script function reference
								if (enemyBehaviourScriptBucket == -1)
								{
									int maxID = -1;
									int pos = -1;
									for (int j = 0; j < enemyBehaviour->Object->m_yyvarsMap->m_curSize; j++)
									{
										if (enemyBehaviour->Object->m_yyvarsMap->m_pBuckets[j].Hash != 0 && enemyBehaviour->Object->m_yyvarsMap->m_pBuckets[j].k > maxID)
										{
											maxID = enemyBehaviour->Object->m_yyvarsMap->m_pBuckets[j].k;
											pos = j;
										}
									}
									enemyBehaviourScriptBucket = pos;
								}
								RValue* behaviourScript = enemyBehaviour->Object->m_yyvarsMap->m_pBuckets[enemyBehaviourScriptBucket].v;

								if (behaviourFuncTable.count((int)(void*)(behaviourScript->Object)) == 0)
								{
									hasAllScripts = false;
									break;
								}
								funcAddressList.push_back((int)(void*)(behaviourScript->Object));
							}
						}

						if (hasAllScripts)
						{
							YYRValue Result;
							RValue* isDeadRef = nullptr;
							Self->m_yyvarsMap->FindElement(enemyVarIndexMap[3], isDeadRef);
							RValue* tangibleRef = nullptr;
							Self->m_yyvarsMap->FindElement(enemyVarIndexMap[4], tangibleRef);
							RValue* moveOutsideCDRef = nullptr;
							Self->m_yyvarsMap->FindElement(enemyVarIndexMap[5], moveOutsideCDRef);
							RValue* isKnockbackRef = nullptr;
							Self->m_yyvarsMap->FindElement(enemyVarIndexMap[6], isKnockbackRef);
							RValue* completeStopRef = nullptr;
							Self->m_yyvarsMap->FindElement(enemyVarIndexMap[7], completeStopRef);
							if (moveOutsideCDRef->Real >= 0.5)
							{
								moveOutsideCDRef->Real--;
							}
							if (completeStopRef->Real < 0.5)
							{
								//TODO: Implement speed check

								RValue* directionChangeTimeRef = nullptr;
								Self->m_yyvarsMap->FindElement(enemyVarIndexMap[8], directionChangeTimeRef);
								if (directionChangeTimeRef->Real > 0.5)
								{
									directionChangeTimeRef->Real--;
								}
								if (collidedCDRef->Real > 0.5)
								{
									collidedCDRef->Real--;
								}
								RValue* lockFacingRef = nullptr;
								Self->m_yyvarsMap->FindElement(enemyVarIndexMap[9], lockFacingRef);
								RValue* turnTimerRef = nullptr;
								Self->m_yyvarsMap->FindElement(enemyVarIndexMap[10], turnTimerRef);
								RValue* canMoveRef = nullptr;
								Self->m_yyvarsMap->FindElement(enemyVarIndexMap[11], canMoveRef);
								if (lockFacingRef->Real > 0.5 && turnTimerRef->Real < 0.5 && canMoveRef->Real > 0.5)
								{
									if (currentPlayer.gmlInstance.xPos > Self->i_x)
									{
										Self->i_imagescalex = abs(Self->i_imagescalex);
									}
									else
									{
										Self->i_imagescalex = -abs(Self->i_imagescalex);
									}
									turnTimerRef->Real = 15;
								}
								if (turnTimerRef->Real > 0.5)
								{
									turnTimerRef->Real--;
								}

								//TODO: add check for knockback timer
								//
								{
									for (int funcAddress : funcAddressList)
									{
										behaviourFuncTable[funcAddress](Self);
									}
								}
								RValue* hitCDTimerRef = nullptr;
								Self->m_yyvarsMap->FindElement(enemyVarIndexMap[12], hitCDTimerRef);
								if (hitCDTimerRef->Real > 0.5)
								{
									hitCDTimerRef->Real--;
								}
								RValue* aliveForRef = nullptr;
								Self->m_yyvarsMap->FindElement(enemyVarIndexMap[13], aliveForRef);
								aliveForRef->Real++;
							}
							numEnemyStepSkip++;
							pCodeEvent->Cancel(true);
						}
						else
						{
							numEnemyStepCall++;
							pCodeEvent->Call(Self, Other, Code, Res, Flags);
						}

					}
				}
				else
				{
					numEnemyStepCall++;
					pCodeEvent->Call(Self, Other, Code, Res, Flags);
				}
			};
			Enemy_Step_0(pCodeEvent, Self, Other, Code, Res, Flags);
			codeFuncTable[Code->i_CodeIndex] = Enemy_Step_0;
		}
		else if (_strcmpi(Code->i_pName, "gml_Object_obj_Enemy_Draw_0") == 0)
		{
			auto Enemy_Draw_0 = [pCodeEvent](YYTKCodeEvent* pCodeEvent, CInstance* Self, CInstance* Other, CCode* Code, RValue* Res, int Flags) {
				if (Self->i_x < CameraXPos - 32 || Self->i_x > CameraXPos + 640 + 32 || Self->i_y < CameraYPos - 32 || Self->i_y > CameraYPos + 640 + 32)
				{
					pCodeEvent->Cancel(true);
				}
				else
				{
					pCodeEvent->Call(Self, Other, Code, Res, Flags);
				}
			};
			Enemy_Draw_0(pCodeEvent, Self, Other, Code, Res, Flags);
			codeFuncTable[Code->i_CodeIndex] = Enemy_Draw_0;
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
				if (attackHitCooldownMap.count(Self->i_spriteindex) == 0)
				{
					YYRValue Result;
					CallBuiltin(Result, "variable_instance_get", Self, Other, { (long long)Self->i_id, "hitCD" });
					attackHitCooldownMap[Self->i_spriteindex] = static_cast<int>(Result);
				}
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
		CodeCallback,						// The function to register as a callback
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

	TRoutine globalScript;
	GetFunctionByName("@@GlobalScope@@", globalScript);
	{
		RValue Res;
		globalScript(&Res, nullptr, nullptr, 0, nullptr);
		globalInstancePtr = Res.Instance;
	}

	GetFunctionByName("variable_get_hash", variableGetHashFunc);

	std::vector<const char*> varNames;
	varNames.push_back("collidedCD");
	varNames.push_back("lifeTime");
	varNames.push_back("behaviours");
	varNames.push_back("isDead");
	varNames.push_back("tangible");
	varNames.push_back("moveOutsideCD");
	varNames.push_back("isKnockback");
	varNames.push_back("completeStop");
	varNames.push_back("directionChangeTime");
	varNames.push_back("lockFacing");
	varNames.push_back("turnTimer");
	varNames.push_back("canMove");
	varNames.push_back("hitCDTimer");
	varNames.push_back("aliveFor");
	varNames.push_back("SPD");
	varNames.push_back("directionMoving");
	for (int i = 0; i < varNames.size(); i++)
	{
		RValue Res;
		RValue arg{};
		arg.Kind = VALUE_STRING;
		arg.String = RefString::Alloc(varNames[i], strlen(varNames[i]));
		variableGetHashFunc(&Res, nullptr, nullptr, 1, &arg);
		enemyVarIndexMap[i] = CHashMap<int, RValue>::CalculateHash(static_cast<int>(Res.Real)) % (1ll << 31);
	}
	varNames.clear();

	varNames.push_back("beforeDamageCalculation");
	varNames.push_back("afterCriticalHit");
	varNames.push_back("onCriticalHit");
	varNames.push_back("onTakeDamage");
	varNames.push_back("onTakeDamageAfter");
	varNames.push_back("onDodge");
	varNames.push_back("invincible");
	for (int i = 0; i < varNames.size(); i++)
	{
		RValue Res;
		RValue arg{};
		arg.Kind = VALUE_STRING;
		arg.String = RefString::Alloc(varNames[i], strlen(varNames[i]));
		variableGetHashFunc(&Res, nullptr, nullptr, 1, &arg);
		baseMobVarIndexMap[i] = CHashMap<int, RValue>::CalculateHash(static_cast<int>(Res.Real)) % (1ll << 31);
	}
	CallBuiltin(Result, "variable_get_hash", nullptr, nullptr, { "debuffDisplay" });
	baseMobVarIndexMap[1000] = CHashMap<int, RValue>::CalculateHash(static_cast<int>(Result.Real)) % (1ll << 31);
	varNames.clear();

	varNames.push_back("isEnemy");
	varNames.push_back("creator");
	varNames.push_back("OnHitEffects");
	varNames.push_back("onHitEffects");
	for (int i = 0; i < varNames.size(); i++)
	{
		RValue Res;
		RValue arg{};
		arg.Kind = VALUE_STRING;
		arg.String = RefString::Alloc(varNames[i], strlen(varNames[i]));
		variableGetHashFunc(&Res, nullptr, nullptr, 1, &arg);
		attackIndexMap[i] = CHashMap<int, RValue>::CalculateHash(static_cast<int>(Res.Real)) % (1ll << 31);
	}

	CallBuiltin(Result, "show_debug_overlay", nullptr, nullptr, { true });

	outFile.open("test.txt");
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