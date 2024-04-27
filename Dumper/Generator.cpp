#include "Generator.h"


Generator::FunctionsMap Generator::PredefinedFunctions;
Generator::MemberMap Generator::PredefinedMembers;


void Generator::Init()
{
	ObjectArray::Init();
	FName::Init();

	/* ARK */
	//FName::Init(0x211AF50);
	//Off::InSDK::InitPE(0x43);

	Off::InSDK::InitPE();
	Off::Init();

	/* manual overwrite */
	//ObjectArray::Init(/*GObjects*/, /*ChunkSize*/, /*bIsChunked*/);
	//FName::Init(/*FName::AppendString*/);
	//Off::InSDK::InitPE(/*PEIndex*/);
	//Off::Init();

	InitPredefinedMembers();
	InitPredefinedFunctions();
}

void Generator::GenerateSDK()
{
	std::unordered_map<int32_t, std::vector<int32_t>> ObjectPackages;

	ObjectArray::GetAllPackages(ObjectPackages);

	fs::path DumperFolder(Settings::SDKGenerationPath);
	fs::path GenFolder(DumperFolder / Settings::GameVersion);
	fs::path SDKFolder = GenFolder / "SDK";

	if (!fs::exists(DumperFolder))
	{
		fs::create_directory(DumperFolder);
	}

	if (fs::exists(GenFolder))
	{
		fs::path Old = GenFolder.generic_string() + "_OLD";

		fs::remove_all(Old);

		fs::rename(GenFolder, Old);
	}

	fs::create_directory(GenFolder);
	fs::create_directory(SDKFolder);

	Package::InitAssertionStream(GenFolder);

	for (auto& Pair : ObjectPackages)
	{
		UEObject Object = ObjectArray::GetByIndex(Pair.first);

		if (!Object)
			continue;

		Package Pack(Object);
		Pack.Process(Pair.second);

		if (!Pack.IsEmpty())
		{
			std::string PackageName = Object.GetName();
			std::string FileName = Settings::FilePrefix ? Settings::FilePrefix + PackageName : PackageName;

			if (fs::exists(SDKFolder / (FileName + "_classes.hpp")))
			{
				FileName += "_1";
			}

			FileWriter ClassFile(SDKFolder, FileName, FileWriter::FileType::Class);
			FileWriter StructsFile(SDKFolder, FileName, FileWriter::FileType::Struct);
			FileWriter FunctionFile(SDKFolder, FileName, FileWriter::FileType::Function);
			FileWriter ParameterFile(SDKFolder, FileName, FileWriter::FileType::Parameter);

			ClassFile.WriteClasses(Pack.AllClasses);
			StructsFile.WriteEnums(Pack.AllEnums);
			StructsFile.WriteStructs(Pack.AllStructs);

			if (PackageName == "CoreUObject")
			{
				FunctionFile.Write("\t//Initialize GObjects using InitGObjects()\n\tTUObjectArray* UObject::GObjects = nullptr;\n\n");
			}

			for (auto& Function : Pack.AllFunctions)
			{
				for (auto& Pairs : Generator::PredefinedFunctions)
				{
					if (Pairs.second.first == PackageName)
					{
						for (auto& PredefFunc : Pairs.second.second)
						{
							if (PredefFunc.DeclarationCPP != "")
							{
								FunctionFile.Write(PredefFunc.DeclarationCPP);
								FunctionFile.Write(PredefFunc.Body);
								FunctionFile.Write("\n");
							}
						}
					}
				}

				FunctionFile.WriteFunction(Function);
				ParameterFile.WriteParamStruct(Function.GetParamStruct());
			}
		}
		else
		{
			ObjectPackages.erase(Pair.first);
			Package::PackageSorterClasses.RemoveDependant(Pair.first);
			Package::PackageSorterStructs.RemoveDependant(Pair.first);

			std::cout << "Removed package: " << Pack.DebugGetObject().GetName() << "\n";
		}
	}

	Package::CloseAssertionStream();

	GenerateSDKHeader(GenFolder, ObjectPackages);
	GenerateFixupFile(GenFolder);
	GenerateBasicFile(SDKFolder);

	std::cout << "\n\n[=] Done [=]\n\n";
}

void Generator::GenerateSDKHeader(fs::path& SdkPath, std::unordered_map<int32_t, std::vector<int32_t>>& Packages)
{
	std::ofstream HeaderStream(SdkPath / "SDK.hpp");

	HeaderStream << "#pragma once\n\n";
	HeaderStream << "// Made with <3 by Encryqed && me [Fischsalat]\n\n";

	HeaderStream << std::format("// {}\n", Settings::GameName);
	HeaderStream << std::format("// {}\n\n", Settings::GameVersion);

	HeaderStream << "#define WINDOWS_IGNORE_PACKING_MISMATCH\n\n";

	HeaderStream << "#include <string>\n";
	HeaderStream << "#include <Windows.h>\n";
	HeaderStream << "#include <iostream>\n\n";

	HeaderStream << "typedef __int8 int8;\n";
	HeaderStream << "typedef __int16 int16;\n";
	HeaderStream << "typedef __int32 int32;\n";
	HeaderStream << "typedef __int64 int64;\n\n";

	HeaderStream << "typedef unsigned __int8 uint8;\n";
	HeaderStream << "typedef unsigned __int16 uint16;\n";
	HeaderStream << "typedef unsigned __int32 uint32;\n";
	HeaderStream << "typedef unsigned __int64 uint64;\n";

	HeaderStream << std::format(
		R"(
namespace Offsets
{{
	inline int32 GObjects          = 0x{:08X};
	inline int32 AppendString      = 0x{:08X};
	inline int32 ProcessEvent      = 0x{:08X};
}}
)", Off::InSDK::GObjects, Off::InSDK::AppendNameToString, Off::InSDK::PEOffset);

	if (Settings::bShouldXorStrings)
		HeaderStream << "#define XORSTR(str) str\n";

	HeaderStream << "\n#include \"PropertyFixup.hpp\"\n";
	HeaderStream << "\n#include \"SDK/" << (Settings::FilePrefix ? Settings::FilePrefix : "") << "Basic.hpp\"\n";

	for (auto& Pack : Package::PackageSorterStructs.AllDependencies)
	{
		std::string IncludesString;
		Package::PackageSorterStructs.GetIncludesForPackage(Pack.first, false, IncludesString);
	
		HeaderStream << IncludesString;
	}

	for (auto& Pack : Package::PackageSorterClasses.AllDependencies)
	{
		std::string IncludesString;
		Package::PackageSorterClasses.GetIncludesForPackage(Pack.first, true, IncludesString);

		HeaderStream << IncludesString;
	}

	// Param files don't need dependency sorting
	for (auto& Pair : Packages)
	{
		UEObject PackageObj = ObjectArray::GetByIndex(Pair.first);

		if (!PackageObj)
			continue;

		HeaderStream << std::format("\n#include \"SDK/{}{}_parameters.hpp\"", (Settings::FilePrefix ? Settings::FilePrefix : ""), PackageObj.GetName());
	}

	HeaderStream.close();
}
void Generator::GenerateFixupFile(fs::path& SdkPath)
{
	std::ofstream FixupStream(SdkPath / "PropertyFixup.hpp");

	FixupStream << "#pragma once\n\n";

	FixupStream << "//Definitions for missing Properties\n\n";

	for (auto& Property : UEProperty::UnknownProperties)
	{
		FixupStream << std::format("class {}\n{{\n\tunsigned __int8 Pad[0x{:X}];\n}};\n\n", Property.first, Property.second);
	}
}


void Generator::InitPredefinedMembers()
{
	PredefinedMembers["UObject"] =
	{
		{ "static class TUObjectArray*", "GObjects", 0x00, 0x00},
		{ "void*", "Vft", Off::UObject::Vft, 0x08 },
		{ "int32 ", "Flags", Off::UObject::Flags, 0x04 },
		{ "int32", "Index", Off::UObject::Index, 0x04 },
		{ "class UClass*", "Class", Off::UObject::Class, 0x08 },
#ifndef WITH_CASE_PRESERVING_NAME
		{ "class FName", "Name", Off::UObject::Name, 0x08 },
#else
		{ "class FName", "Name", Off::UObject::Name, 0x10 },
#endif // WITH_CASE_PRESERVING_NAME
		{ "class UObject*", "Outer", Off::UObject::Outer, 0x08 }
	};

	PredefinedMembers["UField"] =
	{
		{ "class UField*", "Next", Off::UField::Next, 0x08 }
	};

	PredefinedMembers["UEnum"] =
	{
		{ "class TArray<class TPair<class FName, int64>>", "Names", Off::UEnum::Names, 0x10 }
	};

	PredefinedMembers["UStruct"] =
	{
		{ "class UStruct*", "Super", Off::UStruct::SuperStruct, 0x08 },
		{ "class UField*", "Children", Off::UStruct::Children, 0x08 },
		{ "int32", "Size", Off::UStruct::Size, 0x04 }
	};

	PredefinedMembers["UFunction"] =
	{
		{ "uint32", "FunctionFlags", Off::UFunction::FunctionFlags, 0x08 }
	};

	PredefinedMembers["UClass"] =
	{
		{ "enum class EClassCastFlags", "CastFlags", Off::UClass::ClassFlags, 0x08 },
		{ "class UObject*", "DefaultObject", Off::UClass::ClassDefaultObject, 0x08 }
	};

	PredefinedMembers["UProperty"] =
	{
		{ "int32", "ElementSize", Off::UProperty::ElementSize, 0x04 },
		{ "uint64", "PropertyFlags", Off::UProperty::PropertyFlags, 0x08 },
		{ "int32", "Offset", Off::UProperty::Offset_Internal, 0x04 }
	};

	PredefinedMembers["UByteProperty"] =
	{
		{ "class UEnum*", "Enum", Off::UByteProperty::Enum, 0x08 }
	};

	PredefinedMembers["UBoolProperty"] =
	{
		{ "uint8", "FieldSize", Off::UBoolProperty::Base, 0x01 },
		{ "uint8", "ByteOffset", Off::UBoolProperty::Base + 0x1, 0x01 },
		{ "uint8", "ByteMask", Off::UBoolProperty::Base + 0x2, 0x01 },
		{ "uint8", "FieldMask", Off::UBoolProperty::Base + 0x3, 0x01 }
	};

	PredefinedMembers["UObjectProperty"] =
	{
		{ "class UClass*", "PropertyClass", Off::UObjectProperty::PropertyClass, 0x08 }
	};

	PredefinedMembers["UClassProperty"] =
	{
		{ "class UClass*", "MetaClass", Off::UClassProperty::MetaClass, 0x08 }
	};

	PredefinedMembers["UStructProperty"] =
	{
		{ "class UStruct*", "Struct", Off::UStructProperty::Struct, 0x08 }
	};

	PredefinedMembers["UArrayProperty"] =
	{
		{ "class UProperty*", "InnerProperty", Off::UArrayProperty::Inner, 0x08 }
	};

	PredefinedMembers["UMapProperty"] =
	{
		{ "class UProperty*", "KeyProperty", Off::UMapProperty::Base, 0x08 },
		{ "class UProperty*", "ValueProperty", Off::UMapProperty::Base + 0x08, 0x08 }
	};

	PredefinedMembers["USetProperty"] =
	{
		{ "class UProperty*", "ElementProperty", Off::USetProperty::ElementProp, 0x08 }
	};

	PredefinedMembers["USetProperty"] =
	{
		{ "class UProperty*", "UnderlayingProperty", Off::UEnumProperty::Base, 0x08 },
		{ "class UEnum*", "Enum", Off::UEnumProperty::Base + 0x08, 0x08 }
	};
}

void Generator::InitPredefinedFunctions()
{
	PredefinedFunctions["UObject"] =
	{
		"CoreUObject",
		{
			{
				"\tbool HasTypeFlag(EClassCastFlags TypeFlag) const;",
				"\tbool UObject::HasTypeFlag(EClassCastFlags TypeFlag) const",
R"(
	{
		return TypeFlag != EClassCastFlags::None ? Class->CastFlags & TypeFlag : true;
	}
)"
			},
			{
				"\tbool IsDefaultObject() const", "",
R"(
	{
		return (Flags & 0x10) == 0x10;
	}
)"
			},
			{
				"\tstd::string GetName() const;",
				"\tstd::string UObject::GetName() const",
R"(
	{
		return this ? Name.ToString() : "None";
	}
)"
			},
			{
				"\tstd::string GetFullName() const;",
				"\tstd::string UObject::GetFullName() const",
R"(
	{
		if (Class)
		{
			std::string Temp;

			for (UObject* NextOuter = Outer; NextOuter; NextOuter = NextOuter->Outer)
			{
				Temp = NextOuter->GetName() + "." + Temp;
			}

			std::string Name = Class->GetName();
			Name += " ";
			Name += Temp;
			Name += GetName();

			return Name;
		}

		return "None";
	}
)"
			},
			{
				"\ttemplate<typename UEType = UObject>\n\tstatic UEType* FindObject(const std::string& FullName, EClassCastFlags RequiredType = EClassCastFlags::None)", "",
R"(
	{
		for (int i = 0; i < GObjects->Num(); ++i)
		{
			UObject* Object = GObjects->GetByIndex(i);
	
			if (!Object)
				continue;
			
			if (Object->HasTypeFlag(RequiredType) && Object->GetFullName() == FullName)
			{
				return static_cast<UEType*>(Object);
			}
		}

		return nullptr;
	}
)"
			},
			{
				"\ttemplate<typename UEType = UObject>\n\tstatic UEType* FindObjectFast(const std::string& Name, EClassCastFlags RequiredType = EClassCastFlags::None)", "",
R"(
	{
		for (int i = 0; i < GObjects->Num(); ++i)
		{
			UObject* Object = GObjects->GetByIndex(i);
	
			if (!Object)
				continue;
			
			if (Object->HasTypeFlag(RequiredType) && Object->GetName() == Name)
			{
				return static_cast<UEType*>(Object);
			}
		}

		return nullptr;
	}
)"
			},
			{
				"\tstatic class UClass* FindClass(const std::string& ClassFullName)", "",
R"(
	{
		return FindObject<class UClass>(ClassFullName, EClassCastFlags::UClass);
	}
)"
			},
			{
				"\tstatic class UClass* FindClassFast(const std::string& ClassName)", "",
R"(
	{
		return FindObjectFast<class UClass>(ClassName, EClassCastFlags::UClass);
	}
)"
			},
			{
				"\tbool IsA(class UClass* Clss) const;",
				"\tbool UObject::IsA(class UClass* Clss) const",
R"(
	{
		for (UStruct* Super = Class; Super; Super = Super->Super)
		{
			if (Super == Clss)
			{
				return true;
			}
		}

		return false;
	}
)"
			},
			{
				"\tinline void ProcessEvent(class UFunction* Function, void* Parms) const", "",
				std::format(
R"(
	{{
		return GetVFunction<void(*)(const UObject*, class UFunction*, void*)>(this, 0x{:X} /*0x{:X}*/)(this, Function, Parms);
	}}
)", Off::InSDK::PEIndex, Off::InSDK::PEOffset)
			}
		}
	};

	PredefinedFunctions["UClass"] =
	{
		"CoreUObject",
		{
			{
				"\tclass UFunction* GetFunction(const std::string& ClassName, const std::string& FuncName);",
				"\tclass UFunction* UClass::GetFunction(const std::string& ClassName, const std::string& FuncName)",
R"(
	{
		for(UStruct* Clss = this; Clss; Clss = Clss->Super)
		{
			if (Clss->GetName() == ClassName)
			{
				for (UField* Field = Clss->Children; Field; Field = Field->Next)
				{
					if(Field->HasTypeFlag(EClassCastFlags::UFunction) && Field->GetName() == FuncName)
					{
						return static_cast<class UFunction*>(Field);
					}	
				}
			}
		}
		return nullptr;
	}
)"
			}
		}
	};
}


void Generator::GenerateBasicFile(fs::path& SdkPath)
{
	FileWriter BasicHeader(SdkPath, Settings::FilePrefix ? Settings::FilePrefix + std::string("Basic") : "Basic", FileWriter::FileType::Header);
	FileWriter BasicSource(SdkPath, Settings::FilePrefix ? Settings::FilePrefix + std::string("Basic") : "Basic", FileWriter::FileType::Source);

	BasicHeader.Write(
		R"(
void InitGObjects();
)");

	BasicSource.Write(
		R"(
void InitGObjects()
{
	UObject::GObjects = reinterpret_cast<TUObjectArray*>(uintptr_t(GetModuleHandle(0)) + Offsets::GObjects);
}		
)");

	BasicHeader.Write(
		R"(
template<typename Fn>
inline Fn GetVFunction(const void* Instance, std::size_t Index)
{
	auto Vtable = *reinterpret_cast<const void***>(const_cast<void*>(Instance));
	return reinterpret_cast<Fn>(Vtable[Index]);
}
)");

	if (Off::InSDK::ChunkSize <= 0)
	{
		BasicHeader.Write(
			std::format(R"(
class TUObjectArray
{{
public:

	struct FUObjectItem
	{{
		class UObject* Object;
		uint8 Pad[0x{:02X}];
	}};

	FUObjectItem* Objects;
	int32 MaxElements;
	int32 NumElements;

	// Call InitGObjects() before using these functions

	inline int Num() const
	{{
		return NumElements;
	}}

	inline class UObject* GetByIndex(const int32 Index) const
	{{
		if (Index < 0 || Index > NumElements)
			return nullptr;

		return Objects[Index].Object;
	}}
}};
)", Off::InSDK::FUObjectItemSize - 0x8));
	}
	else
	{
		BasicHeader.Write(
			std::format(R"(
class TUObjectArray
{{
public:

	enum
	{{
		ElementsPerChunk = 0x{:X},
	}};

	struct FUObjectItem
	{{
		class UObject* Object;
		uint8 Pad[0x{:02X}];
	}};

	FUObjectItem** Objects;
	uint8 Pad_0[0x08];
	int32 MaxElements;
	int32 NumElements;
	int32 MaxChunks;
	int32 NumChunks;

	// Call InitGObjects() before using these functions

	inline int32 Num() const
	{{
		return NumElements;
	}}

	inline class UObject* GetByIndex(const int32 Index) const
	{{
		if (Index < 0 || Index > NumElements)
			return nullptr;

		const int32 ChunkIndex = Index / ElementsPerChunk;
		const int32 InChunkIdx = Index % ElementsPerChunk;

		return Objects[ChunkIndex][InChunkIdx].Object;
	}}
}};
)", Off::InSDK::ChunkSize, Off::InSDK::FUObjectItemSize - 0x8));
	}

	BasicHeader.Write(
		R"(
template<class T>
class TArray
{
protected:
	T* Data;
	int32 NumElements;
	int32 MaxElements;

public:

	TArray() = default;

	inline TArray(int32 Size)
		:NumElements(0), MaxElements(Size), Data(reinterpret_cast<T*>(malloc(sizeof(T) * Size)))
	{
	}

	inline T& operator[](uint32 Index)
	{
		return Data[Index];
	}
	inline const T& operator[](uint32 Index) const
	{
		return Data[Index];
	}

	inline int32 Num()
	{
		return NumElements;
	}

	inline int32 Max()
	{
		return MaxElements;
	}

	inline int32 GetSlack()
	{
		return MaxElements - NumElements;
	}

	inline bool IsValid()
	{
		return Data != nullptr;
	}

	inline bool IsValidIndex(int32 Index)
	{
		return Index >= 0 && Index < NumElements;
	}

	inline void ResetNum()
	{
		NumElements = 0;
	}
};
)");

	BasicHeader.Write(
		R"(
class FString : public TArray<wchar_t>
{
public:
	inline FString() = default;

	using TArray::TArray;

	inline FString(const wchar_t* WChar)
	{
		MaxElements = NumElements = *WChar ? std::wcslen(WChar) + 1 : 0;

		if (NumElements)
		{
			Data = const_cast<wchar_t*>(WChar);
		}
	}

	inline FString operator=(const wchar_t*&& Other)
	{
		return FString(Other);
	}

	inline std::wstring ToWString()
	{
		if (IsValid())
		{
			return Data;
		}

		return L"";
	}

	inline std::string ToString()
	{
		if (IsValid())
		{
			std::wstring WData(Data);
			return std::string(WData.begin(), WData.end());
		}

		return "";
	}
};
)");

	BasicHeader.Write(
		R"(
class FName
{
public:)");

#ifndef WITH_CASE_PRESERVING_NAME
	BasicHeader.Write(
		R"(
	int32 ComparisonIndex;
	int32 Number;
)");
#else
	BasicHeader.Write(
		R"(
int32 ComparisonIndex;
	int32 DisplayIndex;
	int32 Number;
	uint8 Pad[0x4];
)");
#endif // !WITH_CASE_PRESERVING_NAME


	BasicHeader.Write(
		std::format(R"(
	
	inline std::string ToString() const
	{{
		static FString TempString(1024);
		static auto AppendString = reinterpret_cast<void(*)(const FName*, FString&)>(uintptr_t(GetModuleHandle(0)) + Offsets::AppendString);

		AppendString(this, TempString);

		std::string OutputString = TempString.ToString();
		TempString.ResetNum();

		size_t pos = OutputString.rfind('/');

		if (pos == std::string::npos)
			return OutputString;

		return OutputString.substr(pos + 1);
	}}

	inline bool operator==(const FName& Other)
	{{
		return ComparisonIndex == Other.ComparisonIndex;
	}}

	inline bool operator!=(const FName& Other)
	{{
		return ComparisonIndex != Other.ComparisonIndex;
	}}
}};
)", Off::InSDK::AppendNameToString));

	BasicHeader.Write(
		R"(
template<typename ClassType>
class TSubclassOf
{
	class UClass* ClassPtr;

public:
	TSubclassOf() = default;

	inline TSubclassOf(UClass* Class)
		: ClassPtr(Class)
	{
	}

	inline UClass* Get()
	{
		return ClassPtr;
	}
	inline UClass* operator->()
	{
		return ClassPtr;
	}
	inline TSubclassOf& operator=(UClass* Class)
	{
		ClassPtr = Class;

		return *this;
	}

	inline bool operator==(const TSubclassOf& Other) const
	{
		return ClassPtr == Other.ClassPtr;
	}
	inline bool operator!=(const TSubclassOf& Other) const
	{
		return ClassPtr != Other.ClassPtr;
	}
	inline bool operator==(UClass* Other) const
	{
		return ClassPtr == Other;
	}
	inline bool operator!=(UClass* Other) const
	{
		return ClassPtr != Other;
	}
};
)");

	BasicHeader.Write(
		R"(
template<typename ValueType, typename KeyType>
class TPair
{
public:
	ValueType First;
	KeyType Second;
};
)");

	BasicHeader.Write(
		R"(
class FText
{
public:
	FString TextData;
	uint8 IdkTheRest[0x8];
};
)");

	BasicHeader.Write(
		R"(
template<typename ElementType>
class TSet
{
	uint8 WaitTillIImplementIt[0x50];
};
)");

	BasicHeader.Write(
		R"(
template<typename KeyType, typename ValueType>
class TMap
{
	uint8 WaitTillIImplementIt[0x50];
};
)");

	BasicHeader.Write(
		R"(
class FWeakObjectPtr
{
protected:
	int32		ObjectIndex;
	int32		ObjectSerialNumber;

public:
	class UObject* Get() const;

	class UObject* operator->() const;

	bool operator==(const FWeakObjectPtr& Other) const;
	bool operator!=(const FWeakObjectPtr& Other) const;

	bool operator==(const class UObject* Other) const;
	bool operator!=(const class UObject* Other) const;
};
)");

	BasicHeader.Write(
		R"(
template<typename UEType>
class TWeakObjectPtr : FWeakObjectPtr
{
public:

	UEType* Get() const
	{
		return static_cast<UEType*>(FWeakObjectPtr::Get());
	}

	UEType* operator->() const
	{
		return static_cast<UEType*>(FWeakObjectPtr::Get());
	}
};
)");

	BasicHeader.Write(
		R"(

struct FUniqueObjectGuid
{
	uint32 A;
	uint32 B;
	uint32 C;
	uint32 D;
};
)");

	BasicHeader.Write(
		R"(
template<typename TObjectID>
class TPersistentObjectPtr
{
public:
	FWeakObjectPtr WeakPtr;
	int32 TagAtLastTest;
	TObjectID ObjectID;

	class UObject* Get() const
	{
		return WeakPtr.Get();
	}
	class UObject* operator->() const
	{
		return WeakPtr.Get();
	}
};
)");

	BasicHeader.Write(
		R"(
template<typename UEType>
class TLazyObjectPtr : public TPersistentObjectPtr<FUniqueObjectGuid>
{
public:

	UEType* Get() const
	{
		return static_cast<UEType*>(TPersistentObjectPtr::Get());
	}

	UEType* operator->() const
	{
		return static_cast<UEType*>(TPersistentObjectPtr::Get());
	}
};
)");

	BasicHeader.Write(
		R"(
class FSoftObjectPath_
{
public:
	FName AssetPathName;
	FString SubPathString;
};
)");

	BasicHeader.Write(
		R"(
class alignas(8) FSoftObjectPtr : public TPersistentObjectPtr<FSoftObjectPath_>
{
public:

	FName GetAssetPathName();
	FString GetSubPathString();

	std::string GetAssetPathNameStr();
	std::string GetSubPathStringStr();
};
)");


	BasicHeader.Write(
		R"(
template<typename UEType>
class TSoftObjectPtr : public FSoftObjectPtr
{
public:

	UEType* Get() const
	{
		return static_cast<UEType*>(TPersistentObjectPtr::Get());
	}

	UEType* operator->() const
	{
		return static_cast<UEType*>(TPersistentObjectPtr::Get());
	}
};
)");

	BasicHeader.Write(
		R"(
template<typename UEType>
class TSoftClassPtr : public FSoftObjectPtr
{
public:

	UEType* Get() const
	{
		return static_cast<UEType*>(TPersistentObjectPtr::Get());
	}

	UEType* operator->() const
	{
		return static_cast<UEType*>(TPersistentObjectPtr::Get());
	}
};
)");

	BasicSource.Write(
		R"(
FName FSoftObjectPtr::GetAssetPathName()
{
	return ObjectID.AssetPathName;
}
FString FSoftObjectPtr::GetSubPathString()
{
	return ObjectID.SubPathString;
}

std::string FSoftObjectPtr::GetAssetPathNameStr()
{
	return ObjectID.AssetPathName.ToString();
}
std::string FSoftObjectPtr::GetSubPathStringStr()
{
	return ObjectID.SubPathString.ToString();
}
)");


	BasicSource.Write(
		R"(
class UObject* FWeakObjectPtr::Get() const
{
	return UObject::GObjects->GetByIndex(ObjectIndex);
}

class UObject* FWeakObjectPtr::operator->() const
{
	return UObject::GObjects->GetByIndex(ObjectIndex);
}

bool FWeakObjectPtr::operator==(const FWeakObjectPtr& Other) const
{
	return ObjectIndex == Other.ObjectIndex;
}
bool FWeakObjectPtr::operator!=(const FWeakObjectPtr& Other) const
{
	return ObjectIndex != Other.ObjectIndex;
}

bool FWeakObjectPtr::operator==(const class UObject* Other) const
{
	return ObjectIndex == Other->Index;
}
bool FWeakObjectPtr::operator!=(const class UObject* Other) const
{
	return ObjectIndex != Other->Index;
}
)");

	BasicHeader.Write(
		R"(

enum class EClassCastFlags : uint64_t
{
	None = 0x0000000000000000,

	UField							= 0x0000000000000001,
	UInt8Property					= 0x0000000000000002,
	UEnum							= 0x0000000000000004,
	UStruct							= 0x0000000000000008,
	UScriptStruct					= 0x0000000000000010,
	UClass							= 0x0000000000000020,
	UByteProperty					= 0x0000000000000040,
	UIntProperty					= 0x0000000000000080,
	UFloatProperty					= 0x0000000000000100,
	UUInt64Property					= 0x0000000000000200,
	UClassProperty					= 0x0000000000000400,
	UUInt32Property					= 0x0000000000000800,
	UInterfaceProperty				= 0x0000000000001000,
	UNameProperty					= 0x0000000000002000,
	UStrProperty					= 0x0000000000004000,
	UProperty						= 0x0000000000008000,
	UObjectProperty					= 0x0000000000010000,
	UBoolProperty					= 0x0000000000020000,
	UUInt16Property					= 0x0000000000040000,
	UFunction						= 0x0000000000080000,
	UStructProperty					= 0x0000000000100000,
	UArrayProperty					= 0x0000000000200000,
	UInt64Property					= 0x0000000000400000,
	UDelegateProperty				= 0x0000000000800000,
	UNumericProperty				= 0x0000000001000000,
	UMulticastDelegateProperty		= 0x0000000002000000,
	UObjectPropertyBase				= 0x0000000004000000,
	UWeakObjectProperty				= 0x0000000008000000,
	ULazyObjectProperty				= 0x0000000010000000,
	USoftObjectProperty				= 0x0000000020000000,
	UTextProperty					= 0x0000000040000000,
	UInt16Property					= 0x0000000080000000,
	UDoubleProperty					= 0x0000000100000000,
	USoftClassProperty				= 0x0000000200000000,
	UPackage						= 0x0000000400000000,
	ULevel							= 0x0000000800000000,
	AActor							= 0x0000001000000000,
	APlayerController				= 0x0000002000000000,
	APawn							= 0x0000004000000000,
	USceneComponent					= 0x0000008000000000,
	UPrimitiveComponent				= 0x0000010000000000,
	USkinnedMeshComponent			= 0x0000020000000000,
	USkeletalMeshComponent			= 0x0000040000000000,
	UBlueprint						= 0x0000080000000000,
	UDelegateFunction				= 0x0000100000000000,
	UStaticMeshComponent			= 0x0000200000000000,
	UMapProperty					= 0x0000400000000000,
	USetProperty					= 0x0000800000000000,
	UEnumProperty					= 0x0001000000000000,
};
)");

	BasicHeader.Write(
		R"(
inline constexpr EClassCastFlags operator|(EClassCastFlags Left, EClassCastFlags Right)
{																																										
	return (EClassCastFlags)((std::underlying_type<EClassCastFlags>::type)(Left) | (std::underlying_type<EClassCastFlags>::type)(Right));
}																																										

inline bool operator&(EClassCastFlags Left, EClassCastFlags Right)
{																																										
	return (((std::underlying_type<EClassCastFlags>::type)(Left) & (std::underlying_type<EClassCastFlags>::type)(Right)) == (std::underlying_type<EClassCastFlags>::type)(Right));
}
)");

	//-TUObjectArray
	//-TArray
	//-FString
	//-FName [AppendString()]
	//-TSubclassOf<Type>
	// TSet<Type>
	// TMap<Type>
	//-FText
}

//class FWeakObjectPtr
//{
//protected:
//	int32		ObjectIndex;
//	int32		ObjectSerialNumber;
//
//public:
//	class UObject* Get() const;
//
//	class UObject* operator->() const;
//
//	bool operator==(const FWeakObjectPtr& Other) const;
//	bool operator!=(const FWeakObjectPtr& Other) const;
//
//	bool operator==(const class UObject* Other) const;
//	bool operator!=(const class UObject* Other) const;
//};
//
//template<typename UEType>
//struct TWeakObjectPtr : FWeakObjectPtr
//{
//public:
//	class UEType* Get() const;
//
//	class UEType* operator->() const;
//};
//
//class UObject* FWeakObjectPtr::Get() const
//{
//	return UObject::GObjects->GetByIndex(ObjectIndex);
//}
//
//class UObject* FWeakObjectPtr::operator->() const
//{
//	return UObject::GObjects->GetByIndex(ObjectIndex);
//}
//
//bool FWeakObjectPtr::operator==(const FWeakObjectPtr& Other) const
//{
//	return ObjectIndex == Other.ObjectIndex;
//}
//bool FWeakObjectPtr::operator!=(const FWeakObjectPtr& Other) const
//{
//	return ObjectIndex != Other.ObjectIndex;
//}
//
//bool FWeakObjectPtr::operator==(const class UObject* Other) const
//{
//	return ObjectIndex == Obj->Index;
//}
//bool FWeakObjectPtr::operator!=(const class UObject* Other) const
//{
//	return ObjectIndex != Obj->Index;
//}
//
//
//class UEType* TWeakObjectPtr<UEType>::Get() const
//{
//	return UObject::GObjects->GetByIndex(ObjectIndex);
//}
//
//class UEType* TWeakObjectPtr<UEType>::operator->() const
//{
//	return UObject::GObjects->GetByIndex(ObjectIndex);
//}


//class TUObjectArray
//{
//	struct FUObjectItem
//	{
//		class UObject* Object;
//		uint8 Pad[0x10];
//	};
//
//	FUObjectItem* Objects;
//	int32_t MaxElements;
//	int32_t NumElements;
//
//public:
//	inline int Num() const
//	{
//		return NumElements;
//	}
//
//	inline class UObject* GetByIndex(const int32 Index) const
//	{
//		if (Index < 0 || Index > NumElements)
//			return nullptr;
//
//		return Objects[Index].Object;
//	}
//};
//
//class TUObjectArray
//{
//	enum
//	{
//		ElementsPerChunk = 0x{:X},
//	};
//
//	struct FUObjectItem
//	{
//		class UObject* Object;
//		uint8 Pad[0x10];
//	};
//
//	FUObjectItem** Objects;
//	uint8_t Pad_0[0x08];
//	int32_t MaxElements;
//	int32_t NumElements;
//	int32_t MaxChunks;
//	int32_t NumChunks;
//
//	inline int Num() const
//	{
//		return NumElements;
//	}
//
//	inline class UObject* GetByIndex(const int32 Index) const
//	{
//		if (Index < 0 || Index > NumElements)
//			return nullptr;
//
//		const int32 ChunkIndex = Index / ElementsPerChunk;
//		const int32 InChunkIdx = Index % ElementsPerChunk;
//
//		return Objects[ChunkIndex][InChunkIdx].Object;
//	}
//};

struct FText
{
	FString TextData;
	uint8 IdkTheRest[0x8];
};

template<typename ClassType>
class TSubclassOf
{
	class UClass* ClassPtr;

public:
	inline UClass* Get()
	{
		return ClassPtr;
	}
	inline UClass* operator->()
	{
		return ClassPtr;
	}
	inline TSubclassOf& operator=(UClass* Class)
	{
		ClassPtr = Class;

		return *this;
	}

	inline bool operator==(const TSubclassOf& Other) const
	{
		return ClassPtr == Other.ClassPtr;
	}
	inline bool operator!=(const TSubclassOf& Other) const
	{
		return ClassPtr != Other.ClassPtr;
	}
	inline bool operator==(UClass* Other) const
	{
		return ClassPtr == Other;
	}
	inline bool operator!=(UClass* Other) const
	{
		return ClassPtr != Other;
	}
};


//template<class T>
//class TArray
//{
//protected:
//	T* Data;
//	int32 NumElements;
//	int32 MaxElements;
//
//public:
//
//	TArray() = default;
//
//	inline TArray(int32 Size)
//		:NumElements(0), MaxElements(Size), Data(reinterpret_cast<T*>(malloc(sizeof(T) * Size)))
//	{
//	}
//
//	inline T& operator[](uint32 Index)
//	{
//		return Data[Index];
//	}
//	inline const T& operator[](uint32 Index) const
//	{
//		return Data[Index];
//	}
//
//	inline int32 Num()
//	{
//		return NumElements;
//	}
//
//	inline int32 Max()
//	{
//		return MaxElements;
//	}
//
//	inline int32 GetSlack()
//	{
//		return MaxElements - NumElements;
//	}
//
//	inline bool IsValid()
//	{
//		return Data != nullptr;
//	}
//
//	inline bool IsValidIndex(int32 Index)
//	{
//		return Index >= 0 && Index < NumElements;
//	}
//
//	inline bool IsValidIndex(uint32 Index)
//	{
//		return Index < NumElements;
//	}
//
//	inline void ResetNum()
//	{
//		NumElements = 0;
//	}
//};

//class FString : public TArray<wchar_t>
//{
//public:
//	inline FString() = default;
//
//	using TArray::TArray;
//
//	inline FString(const wchar_t* WChar)
//	{
//		MaxElements = NumElements = *WChar ? std::wcslen(WChar) + 1 : 0;
//
//		if (NumElements)
//		{
//			Data = const_cast<wchar_t*>(WChar);
//		}
//	}
//
//	inline FString operator=(const wchar_t*&& Other)
//	{
//		return FString(Other);
//	}
//
//	inline std::wstring ToWString()
//	{
//		if (IsValid())
//		{
//			return Data;
//		}
//
//		return L"";
//	}
//
//	inline std::string ToString()
//	{
//		if (IsValid())
//		{
//			std::wstring WData(Data);
//			return std::string(WData.begin(), WData.end());
//		}
//
//		return "";
//	}
//};

//class FName
//{
//public:
//	int32 ComparisonIndex;
//	int32 Number;
//
//	inline std::string ToString()
//	{
//		static FString TempString(1024);
//		static auto AppendString = reinterpret_cast<void(*)(FName*, FString&)>(uintptr_t(GetModuleHandle(0)) + OFFSET));
//
//		AppendString(this, TempString);
//
//		std::string OutputString = TempString.ToString();
//		TempString.ResetNum();
//
//		size_t pos = OutputString.rfind('/');
//
//		if (pos == std::string::npos)
//			return OutputString;
//
//		return OutputString.substr(pos + 1);
//	}
//
//	inline bool operator==(const FName& Other)
//	{
//		return ComparisonIndex == Other.ComparisonIndex;
//	}
//
//	inline bool operator!=(const FName& Other)
//	{
//		return ComparisonIndex != Other.ComparisonIndex;
//	}
//};