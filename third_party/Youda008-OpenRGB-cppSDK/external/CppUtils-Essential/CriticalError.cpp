#include "CriticalError.hpp"

#include <cstdio>
#include <cstdarg>
#include <stdexcept>

#ifdef NO_EXCEPTIONS
	#undef CRITICALS_CATCHABLE
#endif

[[noreturn]] void critical_error( const char * format, ... )
{
	va_list args;
	va_start( args, format );

#ifdef CRITICALS_CATCHABLE
	char message [1024];
	vsnprintf( message, sizeof(message), format, args );
	throw std::logic_error( message );
#else
	vfprintf( stderr, format, args );
	fputc( '\n', stderr );
	abort();
#endif

	va_end( args );
}
