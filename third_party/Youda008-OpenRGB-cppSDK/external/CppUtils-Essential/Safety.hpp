//======================================================================================================================
// Project: CppUtils
//----------------------------------------------------------------------------------------------------------------------
// Author:      Jan Broz (Youda008)
// Description: declarations and utils for memory safety
//======================================================================================================================

#ifndef CPPUTILS_SAFETY_INCLUDED
#define CPPUTILS_SAFETY_INCLUDED


#ifdef DEBUG
	#define SAFETY_CHECKS
#endif

enum class SafetyChecks
{
	Disabled,
	Enabled
};

#ifdef SAFETY_CHECKS
	constexpr SafetyChecks defaultSafetyChecks = SafetyChecks::Enabled;
#else
	constexpr SafetyChecks defaultSafetyChecks = SafetyChecks::Disabled;
#endif


#endif // CPPUTILS_SAFETY_INCLUDED
