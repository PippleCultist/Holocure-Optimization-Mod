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

// CallBuiltIn is way too slow to use per frame. Need to investigate if there's a better way to call in built functions.

// We save the CodeCallbackHandler attributes here, so we can unregister the callback in the unload routine.
static CallbackAttributes_t* g_pFrameCallbackAttributes = nullptr;
static CallbackAttributes_t* g_pCodeCallbackAttributes = nullptr;
static uint32_t FrameNumber = 0;

static uint32_t NumberOfEnemiesCreated = 0;
static uint32_t NumberOfEnemyDeadCreated = 0;
static std::unordered_set<const char*> uniqueCode;

static std::unordered_map<int, long long> ProfilerMap;
static std::unordered_map<int, const char*> codeIndexToName;

static std::vector<std::pair<long long, int>> sortVec;

static long long totTime = 0;

static std::unordered_map<int, std::function<void(YYTKCodeEvent* pCodeEvent, CInstance* Self, CInstance* Other, CCode* Code, RValue* Res, int Flags)>> codeFuncTable;

RefString tempVar = RefString("Test", 4, false);

static std::ofstream outFile;

static bool testBool = true;

// This callback is registered on EVT_PRESENT and EVT_ENDSCENE, so it gets called every frame on DX9 / DX11 games.
YYTKStatus FrameCallback(YYTKEventBase* pEvent, void* OptionalArgument)
{
	FrameNumber++;

	if (FrameNumber % 60 == 0)
	{
		PrintMessage(CLR_DEFAULT, "[Optimization Plugin] - Number of Enemies Alive: %d", NumberOfEnemiesCreated - NumberOfEnemyDeadCreated);
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

	CallBuiltin(Result, "show_debug_overlay", nullptr, nullptr, { true });

	outFile.open("test.txt");
	/*
	for (int i = 0; i < 43; i++)
	{
		CallBuiltin(Result, "shader_get_name", nullptr, nullptr, { (long long)i });
//		PrintMessage(CLR_DEFAULT, "%s", static_cast<const char*>(Result));
		if (_strcmpi("shdrMob", static_cast<const char*>(Result)) == 0)
		{
			PrintMessage(CLR_DEFAULT, "%d %s", i, static_cast<const char*>(Result));
			MobShaderIndex = i;
		}
	}
	*/
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