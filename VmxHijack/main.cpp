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
#include <Windows.h>
#include <string>
#include <unordered_set>
#include <algorithm>
#include <nt/image.hpp>
#include "logger.hpp"
#include "vmx.hpp"

// Finds the log GuestRPC handler and hooks it, the methods used to find it are very simplistic
// but it should not be an issue given that it's tailored to the way this image is linked and compiled.
//
static void try_hook( win::image_x64_t* vmx )
{
	static constexpr char target_string[] = "Guest: %s%s";
	static constexpr size_t target_string_align = 8;
	static constexpr uint8_t target_instruction[] = { 0x48, 0x8D, 0x0D }; // lea rcx, [rip + rel32]

	// Parse the image header.
	//
	if ( !vmx )
		return logger::error( "Failed hooking the vmx image: image not found." );
	win::nt_headers_x64_t* nt = vmx->get_nt_headers();
	win::section_header_t* scns = nt->get_sections();
	logger::print<CON_PRP>( "- Located 'vmware-vmx.exe' at [%p - %p]\n", vmx, vmx->raw_to_ptr( nt->optional_header.size_image ) );
	
	// Find "Guest: %s%s" in '.rdata'.
	//
	std::unordered_set<uint64_t> string_entries;
	for ( size_t n = 0; n != nt->file_header.num_sections; n++ )
	{
		win::section_header_t* scn = &scns[ n ];
		if ( !memcmp( &scn->name, ".rdata", sizeof( ".rdata" ) ) )
		{
			uint8_t* data = vmx->raw_to_ptr<uint8_t>( scn->virtual_address );
			for ( size_t n = 0; n < ( std::min( scn->virtual_size, scn->size_raw_data ) - ( sizeof( target_string ) - 1 ) ); n += target_string_align )
			{
				if ( !memcmp( data + n, target_string, sizeof( target_string ) - 1 ) )
					string_entries.emplace( uint64_t( data + n ) );
			}
		}
	}

	logger::print<CON_PRP>( " - Found %d match(es) of the string '%s'%c\n", string_entries.size(), target_string, string_entries.empty() ? '.' : ':' );
	for ( auto va : string_entries )
		logger::print<CON_BRG>( "  - .rdata:%p\n", va );
	if ( string_entries.empty() )
		return logger::error( "Failed string search." );

	// Find the referencing [lea rcx, [rip+x]] in '.text'.
	//
	std::unordered_set<uint64_t> code_references;
	for ( size_t n = 0; n != nt->file_header.num_sections; n++ )
	{
		win::section_header_t* scn = &scns[ n ];
		if ( !memcmp( &scn->name, ".text", sizeof( ".text" ) ) )
		{
			uint8_t* data = vmx->raw_to_ptr<uint8_t>( scn->virtual_address );
			for ( size_t n = 0; n < ( std::min( scn->virtual_size, scn->size_raw_data ) - ( sizeof( target_instruction ) + 4 ) ); n++ )
			{
				// lea rcx, [rip+N]:
				if ( !memcmp( data + n, target_instruction, sizeof( target_instruction ) ) )
				{
					uint64_t ref_ptr = ( uint64_t ) ( data + n + 3 + 4 + *( int32_t* ) ( data + n + 3 ) );
					if ( string_entries.contains( ref_ptr ) )
						code_references.insert( uint64_t( data + n ) );
				}
			}
		}
	}

	logger::print<CON_PRP>( " - Found %d instruction(s) referencing the string%c\n", code_references.size(), code_references.empty() ? '.' : ':' );
	for ( auto va : code_references )
		logger::print<CON_BRG>( "  - .text:%p\n", va );
	if ( code_references.size() != 1 )
		return logger::error( "Failed instruction search." );

	// Find the prologue.
	//
	uint8_t* instruction = (uint8_t*)*code_references.begin();
	while ( instruction[ -1 ] != 0xCC ) --instruction;
	if ( *instruction != 0x48 )
		return logger::error( "Failed prologue search." );

	// Form the hook bytes beforehand so that we can swap with a single MOVAPS.
	//
	alignas( M128A ) uint8_t hook[ 16 ] =
	{
		// jmp [rip]
		0xFF, 0x25, 0x00, 0x00, 0x00, 0x00
	};
	*( void** ) ( hook + 2 + 4 ) = &vmx_log_handler;

	// Unprotect the code region.
	//
	DWORD old;
	if ( !VirtualProtect( instruction, sizeof( hook ), PAGE_EXECUTE_READWRITE, &old ) )
		return logger::error( "Failed VirtualProtect." );

	// Write the hook and protect the code again.
	//
	memcpy( instruction, hook, sizeof( hook ) );
	VirtualProtect( instruction, sizeof( hook ), old, &old );
	logger::print<CON_GRN>( " - Successfully hooked the 'log' vmx handler, initialization complete!\n\n" );
}

// Hook the vmx RPC handler on DLL initialization.
//
BOOL WINAPI DllMain( HINSTANCE, DWORD fwd_reason, LPVOID )  
{
	if ( fwd_reason == DLL_PROCESS_ATTACH )
		try_hook( ( win::image_x64_t* ) GetModuleHandleA( nullptr ) );
	return true;
}