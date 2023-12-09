#pragma once
#include <YYToolkit/shared.hpp>
#include "CallbackManager/CallbackManagerInterface.h"

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

extern RValue GMLVAR_time;
extern int lastRainbowEXPFrameNumber;
extern std::unordered_map<int, std::unordered_map<int, uint32_t>> attackHitEnemyFrameNumberMap;
extern std::unordered_map<int, int> attackHitCooldownMap;
extern PlayerInstance currentPlayer;

int getFrameNumber();

void PlayerCreateBefore(std::tuple<CInstance*, CInstance*, CCode*, int, RValue*>& Args);
void PlayerStepAfter(std::tuple<CInstance*, CInstance*, CCode*, int, RValue*>& Args);
void EXPStepBefore(std::tuple<CInstance*, CInstance*, CCode*, int, RValue*>& Args);
void EXPCollisionObjEXPBefore(std::tuple<CInstance*, CInstance*, CCode*, int, RValue*>& Args);
void EXPCreateAfter(std::tuple<CInstance*, CInstance*, CCode*, int, RValue*>& Args);
void PlayerManagerAlarm11Before(std::tuple<CInstance*, CInstance*, CCode*, int, RValue*>& Args);
void EXPAbsorbCollisionObjPlayerBefore(std::tuple<CInstance*, CInstance*, CCode*, int, RValue*>& Args);
void SummonCreateBefore(std::tuple<CInstance*, CInstance*, CCode*, int, RValue*>& Args);
void SummonStepBefore(std::tuple<CInstance*, CInstance*, CCode*, int, RValue*>& Args);
void TextControllerCreateAfter(std::tuple<CInstance*, CInstance*, CCode*, int, RValue*>& Args);
void AttackCreateAfter(std::tuple<CInstance*, CInstance*, CCode*, int, RValue*>& Args);
void AttackDestroyBefore(std::tuple<CInstance*, CInstance*, CCode*, int, RValue*>& Args);
void DamageTextAlarmAfter(std::tuple<CInstance*, CInstance*, CCode*, int, RValue*>& Args);