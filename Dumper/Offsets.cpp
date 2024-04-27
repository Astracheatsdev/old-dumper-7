#include "Offsets.h"
#include "ObjectArray.h"
#include "OffsetFinder.h"


void Off::InSDK::InitPE()
{
	void* PeAddr = (void*)FindByWString(L"Accessed None").FindNextFunctionStart();
	void** Vft = *(void***)ObjectArray::GetByIndex(0).GetAddress();

	Off::InSDK::PEOffset = uintptr_t(PeAddr) - uintptr_t(GetModuleHandle(0));

	for (int i = 0; i < 0x150; i++)
	{
		if (Vft[i] == PeAddr)
		{
			Off::InSDK::PEIndex = i;
			std::cout << "PE-Offset: 0x" << std::hex << Off::InSDK::PEOffset << "\n";
			std::cout << "PE-Index: 0x" << std::hex << i << "\n\n";
			break;
		}
	}
}
void Off::InSDK::InitPE(int32 Index)
{
	Off::InSDK::PEIndex = Index;

	void** VFT = *reinterpret_cast<void***>(ObjectArray::GetByIndex(0).GetAddress());

	Off::InSDK::PEOffset = uintptr_t(VFT[Off::InSDK::PEIndex]) - uintptr_t(GetModuleHandle(0));
}

void Off::Init()
{
	Off::UClass::ClassFlags = OffsetFinder::FindCastFlagsOffset();	
	//std::cout << "Off::UClass::ClassFlags: " << Off::UClass::ClassFlags << "\n";
	
	Off::UClass::ClassDefaultObject = OffsetFinder::FindDefaultObjectOffset();
	//std::cout << "Off::UClass::ClassDefaultObject: " << Off::UClass::ClassDefaultObject << "\n";
	

	Off::UEnum::Names = OffsetFinder::FindEnumNamesOffset();
	//std::cout << "Off::UEnum::Names: " << Off::UEnum::Names << "\n";
	

	Off::UStruct::SuperStruct = OffsetFinder::FindSuperOffset();
	//std::cout << "Off::UStruct::SuperStruct: " << Off::UStruct::SuperStruct << "\n";
	
	Off::UStruct::Children = OffsetFinder::FindChildOffset();
	//std::cout << "Off::UStruct::Children: " << Off::UStruct::Children << "\n";
	
	Off::UStruct::Size = OffsetFinder::FindStructSizeOffset();
	//std::cout << "Off::UStruct::Size: " << Off::UStruct::Size << "\n";
	

	Off::UFunction::FunctionFlags = OffsetFinder::FindFunctionFlagsOffset();
	//std::cout << "Off::UFunction::FunctionFlags: " << Off::UFunction::FunctionFlags << "\n";
	
	
	Off::UProperty::ElementSize = OffsetFinder::FindElementSizeOffset();
	//std::cout << "Off::UProperty::ElementSize: " << Off::UProperty::ElementSize << "\n";
	
	Off::UProperty::PropertyFlags = OffsetFinder::FindPropertyFlagsOffset();
	//std::cout << "Off::UProperty::PropertyFlags: " << Off::UProperty::PropertyFlags << "\n";
	
	Off::UProperty::Offset_Internal = OffsetFinder::FindOffsetInternalOffset();
	//std::cout << "Off::UProperty::Offset_Internal: " << Off::UProperty::Offset_Internal << "\n";
	


	const int32 UPropertySize = OffsetFinder::FindInnerTypeOffset();
	//std::cout << "UPropertySize: " << UPropertySize << "\n";
	

	Off::UByteProperty::Enum = UPropertySize;
	Off::UBoolProperty::Base = UPropertySize;
	Off::UObjectProperty::PropertyClass = UPropertySize;
	Off::UStructProperty::Struct = UPropertySize;
	Off::UArrayProperty::Inner = UPropertySize;
	Off::UMapProperty::Base = UPropertySize;
	Off::USetProperty::ElementProp = UPropertySize;
	Off::UEnumProperty::Base = UPropertySize;

	Off::UClassProperty::MetaClass = UPropertySize + 0x8; //0x8 inheritance from UObjectProperty
}