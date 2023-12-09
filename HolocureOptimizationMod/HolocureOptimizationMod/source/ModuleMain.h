#pragma once
#include <YYToolkit/shared.hpp>
#include <CallbackManager/CallbackManagerInterface.h>
#define SOME_ENUM(DO) \
	DO(hitCD) \
	DO(isKnockback) \
	DO(Shrimp) \
	DO(beforeDamageCalculation) \
	DO(afterCriticalHit) \
	DO(onCriticalHit) \
	DO(onTakeDamage) \
	DO(onTakeDamageAfter) \
	DO(onDodge) \
	DO(invincible) \
	DO(sprite_index) \
	DO(GLR_MESH_DYN_LIST) \
	DO(debuffDisplay) \
	DO(isEnemy) \
	DO(creator) \
	DO(OnHitEffects) \
	DO(onHitEffects) \
	DO(statusEffects) \
	DO(knockback) \
	DO(duration) \
	DO(knockbackImmune) \
	DO(showDamageText) \
	DO(StampVars) \
	DO(Script) \
	DO(config) \
	DO(enemiesInvincMap) \
	DO(behaviourKeys) \
	DO(behaviours) \
	DO(scripts) \
	DO(effectHappened) \
	DO(followPlayer) \
	DO(powerScaling) \
	DO(collideWithPlayer) \
	DO(SPD) \
	DO(followTarget) \
	DO(canMove) \
	DO(dontMove) \
	DO(normalMovement) \
	DO(directionMoving) \
	DO(directionChangeTime) \
	DO(x) \
	DO(y) \
	DO(image_xscale)

#define MAKE_ENUM(VAR) GML_ ## VAR,
enum VariableNames
{
	SOME_ENUM(MAKE_ENUM)
};

#define MAKE_STRINGS(VAR) #VAR,
const char* const VariableNamesStringsArr[] =
{
	SOME_ENUM(MAKE_STRINGS)
};

using TRoutine = void(__cdecl*)(RValue* Result, CInstance* Self, CInstance* Other, int numArgs, RValue* Args);

extern CallbackManagerInterface* callbackManagerInterfacePtr;
extern YYTKInterface* g_ModuleInterface;
extern RValue GMLVarIndexMapGMLHash[1001];
extern int GMLVarIndexMapYYTKHash[1001];
extern TRoutine methodCallFunc;
extern CInstance* globalInstance;
extern int objEnemyIndex;
extern int objDamageTextIndex;
extern int objPreCreateIndex;
extern int objAttackIndex;

extern PFUNC_YYGMLScript origGLRLightSetActiveScript;
extern PFUNC_YYGMLScript origGLRMeshCreateScript;
extern PFUNC_YYGMLScript origGLRMeshSubmeshScript;
extern PFUNC_YYGMLScript origGLRMeshUpdateScript;

extern TRoutine origDSListSizeScript;
extern TRoutine DSListFindValueFunc;