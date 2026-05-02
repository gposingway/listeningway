// We cannot use SystemErrorInfo.hpp directly, because that header is now in a submodule,
// and it isn't visible to the library users that add this to their include directories.
// So define this type again on the level of OpenRGB Client (outside of the 'own' namespace)
// and pretend they are two different types, even though they are the essentially the same.

#include <cstdint>

#ifdef _WIN32
	using system_error_t = uint32_t;  // should be DWORD but let's not include the whole windows.h just because of this
#else
	using system_error_t = int;
#endif
