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
#pragma once
#include <stdint.h>
#include <Windows.h>
#include <iostream>
#include <string>
#include <mutex>

#define VMX_NO_CONSOLE 0

enum console_color
{
	CON_BRG = 15,
	CON_YLW = 14,
	CON_PRP = 13,
	CON_RED = 12,
	CON_CYN = 11,
	CON_GRN = 10,
	CON_BLU = 9,
	CON_DEF = 7,
};

namespace logger
{
	// Mutex guarding the console.
	//
	inline std::mutex mutex;

	// Simple interface to change the current text color.
	//
	inline void set_color( console_color col )
	{
#if !VMX_NO_CONSOLE
		static auto con_hnd = [ ] ()
		{
			// Create the console.
			//
			AllocConsole();
			freopen( "CONOUT$", "w", stdout );
			HANDLE hnd = GetStdHandle( STD_OUTPUT_HANDLE );

			// Print the banner.
			//
			static constexpr char banner[] =
				R"(                                                                                          )" "\n"
				R"(   /$$    /$$                         /$$   /$$ /$$                         /$$           )" "\n"
				R"(  | $$   | $$                        | $$  | $$|__/                        | $$           )" "\n"
				R"(  | $$   | $$ /$$$$$$/$$$$  /$$   /$$| $$  | $$ /$$ /$$  /$$$$$$   /$$$$$$$| $$   /$$     )" "\n"
				R"(  |  $$ / $$/| $$_  $$_  $$|  $$ /$$/| $$$$$$$$| $$|__/ |____  $$ /$$_____/| $$  /$$/     )" "\n"
				R"(   \  $$ $$/ | $$ \ $$ \ $$ \  $$$$/ | $$__  $$| $$ /$$  /$$$$$$$| $$      | $$$$$$/      )" "\n"
				R"(    \  $$$/  | $$ | $$ | $$  >$$  $$ | $$  | $$| $$| $$ /$$__  $$| $$      | $$_  $$      )" "\n"
				R"(     \  $/   | $$ | $$ | $$ /$$/\  $$| $$  | $$| $$| $$|  $$$$$$$|  $$$$$$$| $$ \  $$     )" "\n"
				R"(      \_/    |__/ |__/ |__/|__/  \__/|__/  |__/|__/| $$ \_______/ \_______/|__/  \__/     )" "\n"
				R"(                                              /$$  | $$                                   )" "\n"
				R"(                                             |  $$$$$$/                                   )" "\n"
				R"(                                              \______/                                    )" "\n"
				R"(  --------------------------------------------------------------------------------------  )" "\n\n";
			for ( char c : banner )
			{
				if ( c == '$' )
					SetConsoleTextAttribute( hnd, CON_BLU );
				else
					SetConsoleTextAttribute( hnd, CON_CYN );
				putchar( c );
			}

			// Return the file handle:
			//
			return hnd;
		}();
		SetConsoleTextAttribute( con_hnd, col );
#endif
	}

	// Wrappers acquiring the lock, setting the color and printing to the console.
	//
	template<console_color c = CON_BRG, typename... Tx>
	static void print( const char* fmt, Tx&&... args )
	{
		std::lock_guard _g{ mutex };
		set_color( c );
		printf( fmt, std::forward<Tx>( args )... );
	}
	template<typename... Tx>
	static void error( const char* fmt, Tx&&... args )
	{
#if !VMX_NO_CONSOLE
		print<CON_RED>( "[Error] " );
		print<CON_RED>( fmt, std::forward<Tx>( args )... );
		putchar( '\n' );
#else
		std::string buffer( 128, ' ' );
		buffer.resize( snprintf( buffer.data(), buffer.size() + 1, fmt, args... ) );
		if ( buffer.size() >= 128 )
			snprintf( buffer.data(), buffer.size() + 1, fmt, args... );
		MessageBoxA( 0, buffer.data(), "Failed to Initialize VmxHijack", MB_TOPMOST | MB_ICONERROR );
#endif
	}
	template<typename... Tx>
	static void warning( const char* fmt, Tx&&... args )
	{
		print<CON_YLW>( "[Warning] " );
		print<CON_YLW>( fmt, std::forward<Tx>( args )... );
		putchar( '\n' );
	}
};