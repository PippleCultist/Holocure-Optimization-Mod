#include "CodeEvents.h"
#include <YYToolkit/shared.hpp>
#include "CallbackManager/CallbackManagerInterface.h"
#include "ModuleMain.h"
#include "ScriptFunctions.h"
#include "YYTKTypes/YYObjectBase.h"
#include "YYTKTypes/CHashMap.h"

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

bool pickupRangePotentialUpdate = false;
int lastRainbowEXPFrameNumber = 0;
RValue GMLVAR_time;
std::unordered_map<int, EXPInstance> EXPIDMap;
std::unordered_map<int, int> attackHitCooldownMap;
PlayerInstance currentPlayer;
GMLInstance currentSummon;
// Assume a new game has started
void PlayerCreateBefore(std::tuple<CInstance*, CInstance*, CCode*, int, RValue*>& Args)
{
	GMLVAR_time = g_ModuleInterface->CallBuiltin("variable_global_get", { RValue("time", g_ModuleInterface) });
	lastRainbowEXPFrameNumber = 0;
	numDamageText = 0;
	pickupRangePotentialUpdate = true;
	EXPIDMap.clear();
	attackHitCooldownMap.clear();
	currentPlayer = PlayerInstance();
	currentSummon = GMLInstance();
}

void PlayerStepAfter(std::tuple<CInstance*, CInstance*, CCode*, int, RValue*>& Args)
{
	CInstance* Self = std::get<0>(Args);
	if (pickupRangePotentialUpdate)
	{
		currentPlayer.pickupRange = static_cast<int>(g_ModuleInterface->CallBuiltin("variable_instance_get", { RValue((double)Self->i_id), RValue("pickupRange", g_ModuleInterface) }).AsReal());
		pickupRangePotentialUpdate = false;
	}
	currentPlayer.gmlInstance.ID = Self->i_id;
	currentPlayer.gmlInstance.SpriteIndex = Self->i_spriteindex;
	currentPlayer.gmlInstance.xPos = Self->i_x;
	currentPlayer.gmlInstance.yPos = Self->i_y;
}

bool isInPickupRange(double xCur, double xOther, double yCur, double yOther, double pickupRange)
{
	return ((xCur - xOther) * (xCur - xOther) + (yCur - yOther) * (yCur - yOther)) <= pickupRange * pickupRange;
}

inline int getFrameNumber()
{
	RefDynamicArrayOfRValue* timeRefArrayPtr = static_cast<RefDynamicArrayOfRValue*>(GMLVAR_time.m_Pointer);
	RValue* timeArray = timeRefArrayPtr->m_Array;
	return (int)(timeArray[0].AsReal()) * 216000 + (int)(timeArray[1].AsReal()) * 3600 + (int)(timeArray[2].AsReal()) * 60 + (int)(timeArray[3].AsReal());
}

void EXPStepBefore(std::tuple<CInstance*, CInstance*, CCode*, int, RValue*>& Args)
{
	CInstance* Self = std::get<0>(Args);
	double pickupRange = EXPIDMap[Self->i_id].Range + (EXPIDMap[Self->i_id].Range * currentPlayer.pickupRange) / 100.0;
	EXPIDMap[Self->i_id].gmlInstance.xPos = Self->i_x;
	EXPIDMap[Self->i_id].gmlInstance.yPos = Self->i_y;
	if (getFrameNumber() - EXPIDMap[Self->i_id].gmlInstance.InitialSpawnFrame >= 40)
	{
		if (abs(Self->i_speed) < 1e-3 &&
			lastRainbowEXPFrameNumber < (int)EXPIDMap[Self->i_id].gmlInstance.InitialSpawnFrame &&
			!isInPickupRange(currentPlayer.gmlInstance.xPos, Self->i_x, currentPlayer.gmlInstance.yPos, Self->i_y, pickupRange) &&
			!isInPickupRange(currentSummon.xPos, Self->i_x, currentSummon.yPos, Self->i_y, pickupRange))
		{
			callbackManagerInterfacePtr->CancelOriginalFunction();
		}
	}
}

void EXPCollisionObjEXPBefore(std::tuple<CInstance*, CInstance*, CCode*, int, RValue*>& Args)
{
	CInstance* Other = std::get<1>(Args);
	//Pretty hacky way of waking up the other exp to see if it's the one that needs to merge
	int frameNumber = getFrameNumber();
	if (frameNumber - EXPIDMap[Other->i_id].gmlInstance.InitialSpawnFrame >= 40)
	{
		EXPIDMap[Other->i_id].gmlInstance.InitialSpawnFrame = frameNumber - 35;
	}
}

int EXPRange = -1;
void EXPCreateAfter(std::tuple<CInstance*, CInstance*, CCode*, int, RValue*>& Args)
{
	CInstance* Self = std::get<0>(Args);
	if (EXPRange == -1)
	{
		EXPRange = static_cast<int>(g_ModuleInterface->CallBuiltin("variable_instance_get", { RValue((double)Self->i_id), RValue("range", g_ModuleInterface) }).AsReal());
	}
	EXPIDMap[Self->i_id] = EXPInstance(EXPRange, GMLInstance(Self->i_id, Self->i_spriteindex, getFrameNumber(), Self->i_x, Self->i_y));
}

void PlayerManagerAlarm11Before(std::tuple<CInstance*, CInstance*, CCode*, int, RValue*>& Args)
{
	pickupRangePotentialUpdate = true;
}

void EXPAbsorbCollisionObjPlayerBefore(std::tuple<CInstance*, CInstance*, CCode*, int, RValue*>& Args)
{
	lastRainbowEXPFrameNumber = getFrameNumber();
}

void SummonCreateBefore(std::tuple<CInstance*, CInstance*, CCode*, int, RValue*>& Args)
{
	CInstance* Self = std::get<0>(Args);
	currentSummon.xPos = Self->i_x;
	currentSummon.yPos = Self->i_y;
}

void SummonStepBefore(std::tuple<CInstance*, CInstance*, CCode*, int, RValue*>& Args)
{
	CInstance* Self = std::get<0>(Args);
	currentSummon.xPos = Self->i_x;
	currentSummon.yPos = Self->i_y;
}

RValue* titleText = nullptr;
void TextControllerCreateAfter(std::tuple<CInstance*, CInstance*, CCode*, int, RValue*>& Args)
{
	if (titleText == nullptr)
	{
		titleText = new RValue("Play Modded", g_ModuleInterface);
	}
	RValue textContainer = g_ModuleInterface->CallBuiltin("variable_global_get", { RValue("TextContainer", g_ModuleInterface) });
	RValue titleButtons = g_ModuleInterface->CallBuiltin("struct_get", { textContainer, RValue("titleButtons", g_ModuleInterface) });
	RValue eng = g_ModuleInterface->CallBuiltin("struct_get", { titleButtons, RValue("eng", g_ModuleInterface) });
	static_cast<RefDynamicArrayOfRValue*>(eng.m_Pointer)->m_Array[0].m_Pointer = titleText->m_Pointer;
}

std::unordered_map<int, std::unordered_map<int, uint32_t>> attackHitEnemyFrameNumberMap;
void AttackCreateAfter(std::tuple<CInstance*, CInstance*, CCode*, int, RValue*>& Args)
{
	CInstance* Self = std::get<0>(Args);
	attackHitEnemyFrameNumberMap[Self->i_id] = std::unordered_map<int, uint32_t>();
	RValue* hitCDPtr = nullptr;
	Self->m_yyvarsMap->FindElement(GMLVarIndexMapYYTKHash[GML_hitCD], hitCDPtr);
	attackHitCooldownMap[Self->i_spriteindex] = static_cast<int>(hitCDPtr->m_Real);
}

void AttackDestroyBefore(std::tuple<CInstance*, CInstance*, CCode*, int, RValue*>& Args)
{
	CInstance* Self = std::get<0>(Args);
	attackHitEnemyFrameNumberMap[Self->i_id].clear();
	attackHitEnemyFrameNumberMap.erase(Self->i_id);
}

void DamageTextAlarmAfter(std::tuple<CInstance*, CInstance*, CCode*, int, RValue*>& Args)
{
	CInstance* Self = std::get<0>(Args);
	RValue* durationPtr = nullptr;
	int durationHash = GMLVarIndexMapYYTKHash[GML_duration];
	Self->m_yyvarsMap->FindElement(durationHash, durationPtr);
	if (durationPtr->m_Real <= -2)
	{
		numDamageText--;
	}
}