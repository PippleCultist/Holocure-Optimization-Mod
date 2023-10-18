#pragma once
#define YYSDK_PLUGIN	// Make the API work with plugins
#include "SDK/SDK.hpp"	// Include the SDK headers

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

DllExport YYTKStatus PluginEntry(YYTKPlugin* PluginObject);
YYTKStatus PluginUnload();
YYTKStatus FrameCallback(YYTKEventBase* pEvent, void* OptionalArgument);