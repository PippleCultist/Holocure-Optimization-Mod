#include "BuiltinFunctions.h"
#include <YYToolkit/shared.hpp>
#include <CallbackManager/CallbackManagerInterface.h>
#include "ModuleMain.h"
#include "ScriptFunctions.h"
#include "CodeEvents.h"
#include "YYTKTypes/YYObjectBase.h"
#include "YYTKTypes/CHashMap.h"
#include "YYTKTypes/RefThing.h"

# define M_PI			3.14159265358979323846

void DSMapSetBefore(RValue* Result, CInstance* Self, CInstance* Other, int numArgs, RValue* Args)
{
	int StructHash = GMLVarIndexMapYYTKHash[GML_OnHitEffects];
	RValue* StructRef = nullptr;
	globalInstance->m_yyvarsMap->FindElement(StructHash, StructRef);
	if (abs(Args[0].m_Real - StructRef->m_Real) < 1e-3)
	{
		RValue Res;
		Res = g_ModuleInterface->CallBuiltin("variable_get_hash", { Args[1] });
		int onHitEffectHash = CHashMap<int, RValue>::CalculateHash(static_cast<int>(Res.m_Real)) % (1ll << 31);
		onHitEffectsMap[onHitEffectHash] = Args[2];
	}
}

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

std::unordered_map<size_t, int> stringHashMap;
void VariableStructExistsBefore(RValue* Result, CInstance* Self, CInstance* Other, int numArgs, RValue* Args)
{
	if (isInHitTarget)
	{
		int enemiesInvincMapHash = GMLVarIndexMapYYTKHash[GML_enemiesInvincMap];
		RValue* enemiesInvincMapPtr = nullptr;
		if (Self->m_yyvarsMap->FindElement(enemiesInvincMapHash, enemiesInvincMapPtr) && enemiesInvincMapPtr->m_Pointer == Args[0].m_Pointer)
		{
			Result->m_Kind = VALUE_REAL;
			std::unordered_map<int, uint32_t>& hitEnemyFrameNumberMap = attackHitEnemyFrameNumberMap[Self->i_id];
			int frameNumber = getFrameNumber();
			if (hitEnemyFrameNumberMap.count(hitTargetIndex) == 0 || static_cast<int>(hitEnemyFrameNumberMap[hitTargetIndex]) + attackHitCooldownMap[Self->i_spriteindex] <= frameNumber)
			{
				Result->m_Real = 0;
				hitEnemyFrameNumberMap[hitTargetIndex] = frameNumber;
			}
			else
			{
				Result->m_Real = 1;
			}
			callbackManagerInterfacePtr->CancelOriginalFunction();
			return;
		}
	}
	//For some reason, the second argument can be a real number
	if (Args[0].m_Kind == VALUE_OBJECT && Args[1].m_Kind == VALUE_STRING)
	{
		int fastHash = static_cast<int>(hashFunc(((RefString*)Args[1].m_Pointer)->m_Thing));
		int curHash = 0;

		auto curElementIt = stringHashMap.find(fastHash);

		if (curElementIt == stringHashMap.end())
		{
			curHash = CHashMap<int, RValue>::CalculateHash(static_cast<int>(g_ModuleInterface->CallBuiltin("variable_get_hash", { Args[1] }).m_Real)) % (1ll << 31);
			stringHashMap[fastHash] = curHash;
		}
		else
		{
			curHash = curElementIt->second;
		}

		Result->m_Kind = VALUE_REAL;
		auto curMap = Args[0].m_Object->m_yyvarsMap;
		RValue* curElement = nullptr;
		if (!curMap || !(curMap->FindElement(curHash, curElement)))
		{
			Result->m_Real = 0;
		}
		else
		{
			Result->m_Real = 1;
		}
		callbackManagerInterfacePtr->CancelOriginalFunction();
		return;
	}
	//Need to find a way to also get the optimization working for references
}

RValue* hitTargetText = nullptr;
void StringBefore(RValue* Result, CInstance* Self, CInstance* Other, int numArgs, RValue* Args)
{
	if (isInHitTarget)
	{
		*Result = RValue("test", g_ModuleInterface);
		callbackManagerInterfacePtr->CancelOriginalFunction();
		return;
	}
}

void ArrayLengthBefore(RValue* Result, CInstance* Self, CInstance* Other, int numArgs, RValue* Args)
{
	if (Args[0].m_Kind == VALUE_UNSET)
	{
		Result->m_Kind = VALUE_REAL;
		Result->m_Real = 0;
		callbackManagerInterfacePtr->CancelOriginalFunction();
		return;
	}
	if (Self->i_objectindex == objEnemyIndex)
	{
		auto selfMap = Self->m_yyvarsMap;
		int behaviourKeysHash = GMLVarIndexMapYYTKHash[GML_behaviourKeys];
		RValue* behaviourKeysPtr = nullptr;
		selfMap->FindElement(behaviourKeysHash, behaviourKeysPtr);
		if (behaviourKeysPtr->m_Pointer == Args[0].m_Pointer)
		{
			{
				int behavioursHash = GMLVarIndexMapYYTKHash[GML_behaviours];
				RValue* behavioursPtr = nullptr;
				selfMap->FindElement(behavioursHash, behavioursPtr);
				auto behavioursMap = behavioursPtr->m_Object->m_yyvarsMap;

				if (behavioursMap != nullptr && behavioursMap->m_numUsed != 0)
				{
					int baseMobArrHash = GMLVarIndexMapYYTKHash[GML_debuffDisplay];
					RValue* baseMobArrRef = nullptr;
					selfMap->FindElement(baseMobArrHash, baseMobArrRef);
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
					scriptExecuteArgsArray[3].m_Real = 2;

					paramArr[0].m_Kind = VALUE_OBJECT;
					paramArr[0].m_Object = Self;

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
								if (static_cast<int>(canMovePtr->m_Real) != 0 && static_cast<int>(dontMovePtr->m_Real) == 0)
								{
									RValue setStruct;
									setStruct.m_Kind = VALUE_OBJECT;
									setStruct.m_Object = Self;
									RValue setVal;
									setVal.m_Kind = VALUE_REAL;
									setVal.m_Real = 1;
									g_ModuleInterface->CallBuiltin("struct_set_from_hash", { setStruct, GMLVarIndexMapGMLHash[GML_normalMovement], setVal });
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
									double curDirectionAngle = directionMovingPtr->m_Real;
									if (static_cast<int>(directionChangeTimePtr->m_Real) == 0)
									{
										curDirectionAngle = 180.0 / M_PI * atan2(currentPlayer.gmlInstance.yPos - Self->i_y, currentPlayer.gmlInstance.xPos - Self->i_x);
										if (curDirectionAngle < 0)
										{
											curDirectionAngle += 360;
										}
										directionChangeTimePtr->m_Real = 3;
										g_ModuleInterface->CallBuiltin("struct_set_from_hash", { setStruct, GMLVarIndexMapGMLHash[GML_directionChangeTime], *directionChangeTimePtr });
										directionMovingPtr->m_Real = curDirectionAngle;
										g_ModuleInterface->CallBuiltin("struct_set_from_hash", { setStruct, GMLVarIndexMapGMLHash[GML_directionMoving], *directionMovingPtr });
									}
									if (curDirectionAngle >= 180)
									{
										curDirectionAngle -= 360;
									}
									double speed = SPDPtr->m_Real;
									Self->i_x += static_cast<float>(cos(curDirectionAngle * M_PI / 180.0) * speed);
									setVal.m_Real = Self->i_y + sin(curDirectionAngle * M_PI / 180.0) * speed;
									g_ModuleInterface->CallBuiltin("struct_set_from_hash", { setStruct, GMLVarIndexMapGMLHash[GML_y], setVal });
								}
							}
							else
							{
								RValue* behaviourPtr = curElement->v;
								RValue* ScriptPtr = nullptr;
								RValue* configPtr = nullptr;

								auto behaviourPtrMap = behaviourPtr->m_Object->m_yyvarsMap;
								behaviourPtrMap->FindElement(GMLVarIndexMapYYTKHash[GML_Script], ScriptPtr);
								behaviourPtrMap->FindElement(GMLVarIndexMapYYTKHash[GML_config], configPtr);

								scriptExecuteArgsArray[0] = *ScriptPtr;
								paramArr[1] = *configPtr;
								methodCallFunc(&returnValue, Self, Other, 4, scriptExecuteArgsArray);
							}
						}
					}
					static_cast<RefDynamicArrayOfRValue*>(baseMobArrRef->m_Pointer)->m_Array = prevRefArr;
					static_cast<RefDynamicArrayOfRValue*>(baseMobArrRef->m_Pointer)->length = prevRefArrLength;
					curFuncParamDepth--;
				}
			}
			Result->m_Kind = VALUE_REAL;
			Result->m_Real = 0;
			callbackManagerInterfacePtr->CancelOriginalFunction();
			return;
		}
	}
}

void InstanceCreateDepthBefore(RValue* Result, CInstance* Self, CInstance* Other, int numArgs, RValue* Args)
{
	if (static_cast<int>(Args[3].m_Real) == objDamageTextIndex)
	{
		numDamageText++;
	}
}

void ShowDebugMessageBefore(RValue* Result, CInstance* Self, CInstance* Other, int numArgs, RValue* Args)
{
	callbackManagerInterfacePtr->CancelOriginalFunction();
}

void* curMob = nullptr;
void InstanceCreateLayerAfter(RValue* Result, CInstance* Self, CInstance* Other, int numArgs, RValue* Args)
{
	if (isInSpawnMobFunc)
	{
		curMob = Result->m_Pointer;
	}
}

bool isInExecuteAttack = false;
void VariableInstanceGetNamesBefore(RValue* Result, CInstance* Self, CInstance* Other, int numArgs, RValue* Args)
{
	if (isInDieFunc)
	{
		Result->m_Kind = VALUE_UNSET;
		callbackManagerInterfacePtr->CancelOriginalFunction();
		return;
	}
	if (isInSpawnMobFunc || (isInActualSpawnMobFunc && !isInExecuteAttack))
	{
		int spriteIndexHash = GMLVarIndexMapYYTKHash[GML_sprite_index];
		RValue* spriteIndexPtr = nullptr;
		if (Args[0].m_Object->m_yyvarsMap->FindElement(spriteIndexHash, spriteIndexPtr))
		{
			RValue setStruct;
			if (isInActualSpawnMobFunc)
			{
				setStruct.m_Kind = VALUE_OBJECT;
				setStruct.m_Object = Self;
			}
			else
			{
				setStruct.m_Kind = VALUE_REF;
				setStruct.m_Pointer = curMob;
			}
			auto configMap = Args[0].m_Object->m_yyvarsMap;
			if (configMap != nullptr)
			{
				int curSize = configMap->m_curSize;
				for (int i = 0; i < curSize; i++)
				{
					int curHash = configMap->m_pBuckets[i].Hash;
					if (curHash != 0 && curHash != spriteIndexHash)
					{
						g_ModuleInterface->CallBuiltin("struct_set_from_hash", { setStruct, configMap->m_pBuckets[i].k, *(configMap->m_pBuckets[i].v) });
					}
				}
			}
			curMob = nullptr;
			Result->m_Kind = VALUE_UNSET;
			callbackManagerInterfacePtr->CancelOriginalFunction();
			return;
		}
	}
	if (isInExecuteAttack)
	{
		if (Args[0].m_Kind == VALUE_OBJECT)
		{
			if (Self->i_objectindex == objPreCreateIndex)
			{
				RValue* setStruct = nullptr;
				Self->m_yyvarsMap->FindElement(GMLVarIndexMapYYTKHash[GML_config], setStruct);
				auto configMap = Args[0].m_Object->m_yyvarsMap;
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
								CreateEmptyStruct(Self, Args[0].m_Object, &newStruct);
								DeepStructCopy(Self, configMap->m_pBuckets[i].v, &newStruct);
								g_ModuleInterface->CallBuiltin("struct_set_from_hash", { *setStruct, configMap->m_pBuckets[i].k, newStruct });
							}
							else
							{
								g_ModuleInterface->CallBuiltin("struct_set_from_hash", { *setStruct, configMap->m_pBuckets[i].k, *(configMap->m_pBuckets[i].v) });
							}
						}
					}
				}
				Result->m_Kind = VALUE_UNSET;
				callbackManagerInterfacePtr->CancelOriginalFunction();
				return;
			}
			else if (Self->i_objectindex == objAttackIndex)
			{
				RValue setStruct;
				setStruct.m_Kind = VALUE_OBJECT;
				setStruct.m_Object = Self;
				auto configMap = Args[0].m_Object->m_yyvarsMap;
				if (configMap != nullptr)
				{
					int curSize = configMap->m_curSize;
					for (int i = 0; i < curSize; i++)
					{
						int curHash = configMap->m_pBuckets[i].Hash;
						if (curHash != 0)
						{
							g_ModuleInterface->CallBuiltin("struct_set_from_hash", { setStruct, configMap->m_pBuckets[i].k, *(configMap->m_pBuckets[i].v) });
						}
					}
				}
				Result->m_Kind = VALUE_UNSET;
				callbackManagerInterfacePtr->CancelOriginalFunction();
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
}

void DSListSizeBefore(RValue* Result, CInstance* Self, CInstance* Other, int numArgs, RValue* Args)
{
	if (isInGLRMeshDestroyAll)
	{
		int GLRMeshDynListHash = GMLVarIndexMapYYTKHash[GML_GLR_MESH_DYN_LIST];
		RValue* GLRMeshDynListPtr = nullptr;
		globalInstance->m_yyvarsMap->FindElement(GLRMeshDynListHash, GLRMeshDynListPtr);
		if (static_cast<int>(GLRMeshDynListPtr->m_Real) == static_cast<int>(Args[0].m_Real))
		{
			origDSListSizeScript(Result, Self, Other, numArgs, Args);
			int GLRMeshDynListSize = static_cast<int>(Result->m_Real);

			RValue inputArgs[2];
			inputArgs[0].m_Kind = VALUE_REAL;
			inputArgs[1].m_Kind = VALUE_REAL;
			for (int i = 0; i < GLRMeshDynListSize; i++)
			{
				inputArgs[0].m_Real = GLRMeshDynListPtr->m_Real;
				inputArgs[1].m_Real = i;
				DSListFindValueFunc(Result, Self, Other, 2, inputArgs);
				int listNum = static_cast<int>(Result->m_Real);
				moveMeshToQueue(Self, listNum);
			}

			Result->m_Kind = VALUE_REAL;
			Result->m_Real = 0;
			callbackManagerInterfacePtr->CancelOriginalFunction();
			return;
		}
	}
}

void DSListClearBefore(RValue* Result, CInstance* Self, CInstance* Other, int numArgs, RValue* Args)
{
	if (isInGLRMeshDestroyAll)
	{
		int GLRMeshDynListHash = GMLVarIndexMapYYTKHash[GML_GLR_MESH_DYN_LIST];
		RValue* GLRMeshDynListPtr = nullptr;
		globalInstance->m_yyvarsMap->FindElement(GLRMeshDynListHash, GLRMeshDynListPtr);
		if (static_cast<int>(GLRMeshDynListPtr->m_Real) == static_cast<int>(Args[0].m_Real))
		{
			callbackManagerInterfacePtr->CancelOriginalFunction();
			return;
		}
	}
}