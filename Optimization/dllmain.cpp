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
#include "SDK/Structures/Documented/CDynamicArray/CDynamicArray.hpp"

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
static int EnemyIndex = -1;
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

static std::unordered_map<int, int> enemyVarIndexMap;

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
	totTime = 0;
	ProfilerMap.clear();

	// Tell the core the handler was successful.
	return YYTK_OK;
}

// This callback is registered on EVT_CODE_EXECUTE, so it gets called every game function call.
YYTKStatus CodeCallback(YYTKEventBase* pEvent, void* OptionalArgument)
{
	YYTKCodeEvent* pCodeEvent = dynamic_cast<decltype(pCodeEvent)>(pEvent);

	std::tuple<CInstance*, CInstance*, CCode*, RValue*, int> args = pCodeEvent->Arguments();

	CInstance* Self = std::get<0>(args);
	CInstance* Other = std::get<1>(args);
	CCode* Code = std::get<2>(args);
	RValue* Res = std::get<3>(args);
	int			Flags = std::get<4>(args);

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
				enemyVarIndexMap.clear();
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
				if (enemyVarIndexMap.empty())
				{
					std::vector<YYRValue> prevVar;
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
						YYRValue Result;
						prevVar.push_back(YYRValue());
						CallBuiltin(prevVar[i], "variable_instance_get", Self, Other, { (long long)Self->i_id, varNames[i] });
						CallBuiltin(Result, "variable_instance_set", Self, Other, { (long long)Self->i_id, varNames[i], varNames[i] });
					}
					for (int i = 0; i < Self->m_yyvarsMap->m_curSize; i++)
					{
						if (Self->m_yyvarsMap->m_pBuckets[i].Hash != 0 && Self->m_yyvarsMap->m_pBuckets[i].v->Kind == VALUE_STRING)
						{
							for (int j = 0; j < varNames.size(); j++)
							{
								if (_strcmpi(varNames[j], Self->m_yyvarsMap->m_pBuckets[i].v->String->m_Thing) == 0)
								{
									enemyVarIndexMap[j] = Self->m_yyvarsMap->m_pBuckets[i].Hash;
									break;
								}
							}
						}
					}
					for (int i = 0; i < varNames.size(); i++)
					{
						YYRValue Result;
						CallBuiltin(Result, "variable_instance_set", Self, Other, { (long long)Self->i_id, varNames[i], prevVar[i] });
					}
				}
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
				if (attackHitEnemyFrameNumberMap[Self->i_id].count(Other->i_id) == 0)
				{
					attackHitEnemyFrameNumberMap[Self->i_id][Other->i_id] = FrameNumber;
					pCodeEvent->Call(Self, Other, Code, Res, Flags);
				}
				else
				{
					if (attackHitEnemyFrameNumberMap[Self->i_id][Other->i_id] + attackHitCooldownMap[Self->i_spriteindex] > FrameNumber)
					{
						pCodeEvent->Cancel(true);
					}
					else
					{
						pCodeEvent->Call(Self, Other, Code, Res, Flags);
					}
				}
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

	/*
	MessageBoxA(0, "This plugin is meant for optimizing gameplay performance. Performance may vary depending on the specs of your computer.",
		"Notice", MB_OK | MB_ICONWARNING | MB_TOPMOST | MB_SETFOREGROUND);
	*/
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

	for (int i = 0; i < 381; i++)
	{
		CallBuiltin(Result, "object_get_name", nullptr, nullptr, { (long long)i });
		if (_strcmpi("obj_Enemy", static_cast<const char*>(Result)) == 0)
		{
			PrintMessage(CLR_DEFAULT, "%d %s", i, static_cast<const char*>(Result));
			EnemyIndex = i;
		}
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