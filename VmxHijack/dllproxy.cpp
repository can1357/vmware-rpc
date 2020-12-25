// Copyright (c) 2020, Can Boluk
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
// 1. Redistributions of source code must retain the above copyright notice, this
//    list of conditions and the following disclaimer.
//
// 2. Redistributions in binary form must reproduce the above copyright notice,
//    this list of conditions and the following disclaimer in the documentation
//    and/or other materials provided with the distribution.
//
// 3. Neither the name of the copyright holder nor the names of its
//    contributors may be used to endorse or promote products derived from
//    this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
// DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
// FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
// DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
// SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
// CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
// OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
//
#include <stdint.h>
#include <type_traits>
#include <string>
#include <Windows.h>
#include "logger.hpp"

// Quick and lazy wrapper around the real DSOUND.dll
//
extern "C" decltype( *std::declval<HMODULE>() ) __ImageBase;
extern "C" __declspec( dllexport ) uint64_t DirectSoundCreate( uint64_t a, uint64_t b, uint64_t c, uint64_t d )
{
	static void* real_func = [ ] () -> void*
	{
		std::wstring nt_path = ( const wchar_t* ) 0x7FFE0030;
		nt_path += L"system32\\dsound.dll";

		if ( HMODULE lib = LoadLibraryW( nt_path.c_str() ); lib && lib != &__ImageBase )
		{
			return GetProcAddress( lib, "DirectSoundCreate" );
		}
		else
		{
			logger::warning( "Failed to load the real DSOUND.dll" );
			return nullptr;
		}
	}( );

	if ( !real_func ) return -1;
	else return ( ( uint64_t( __stdcall* )( ... ) )real_func )( a, b, c, d );
}
