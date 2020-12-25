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
#include "logger.hpp"

//                                        //
// <<< Edit this function as you wish >>> //
//                                        //
bool vmx_log_handler(
	uint64_t vcpuid, void* vcpuctx,
	const char* data, uint32_t length,
	const void** out, uint32_t* out_length )
{
	// Normalize the string.
	//
	std::string_view str = { data, length };
	if ( auto pos = str.find( '\x0' ); pos != std::string::npos )
		str = str.substr( 0, pos );
	if ( str.ends_with( '\n' ) )
		str.remove_suffix( 1 );

	// Insert a prefix.
	//
	std::string msg = "[vmx] vcpu-" + std::to_string( vcpuid ) + ": ";
	msg.insert( msg.end(), str.begin(), str.end() );

	// Print onto the console and DebugView.
	//
	OutputDebugStringA( msg.c_str() );
	logger::print<CON_BRG>( "%s\n", msg.c_str() );

	// Write dummy output.
	//
	*out = "OK";
	*out_length = 2;
	return true;
}
