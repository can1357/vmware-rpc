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
#include <string>
#include <optional>
#include <string_view>

namespace vmx
{
	// Magic numbers.
	//
	enum class bdoor_cmd : uint16_t
	{
		message = 30
	};
	constexpr uint16_t bdoor_port =    0x5658;     // 'VX'
	constexpr uint32_t bdoor_magic =   0x564D5868; // 'VMXh'
	constexpr uint32_t rpc_magic =     0x49435052; // 'RPCI'

	// Interface details for bdoor_cmd::message.
	//
	enum class message_type : uint16_t
	{
		open,
		send_size,
		send_payload,
		recv_size,
		recv_payload,
		recv_status,
		close
	};
	union message_result
	{
		uint32_t raw;
		struct
		{
			uint32_t _pad           : 16;
			uint32_t success        : 1;
			uint32_t dorecv         : 1;
			uint32_t closed         : 1;
			uint32_t unsent         : 1;
			uint32_t checkpoint     : 1;
			uint32_t poweroff       : 1;
			uint32_t timeout        : 1;
			uint32_t high_bandwidth : 1;
		};
	};

	// Wrap simple I/O.
	//
	template<typename Tp = uint32_t, typename Ts = uint16_t>
	inline std::array<uint32_t, 4> send_command( bdoor_cmd command, Tp parameter = {}, Ts subcommand = {}, uint16_t channel = {} )
	{
		register uint32_t _a asm( "eax" ) = bdoor_magic;
		register uint32_t _b asm( "ebx" ) = uint32_t( parameter );
		register uint32_t _c asm( "ecx" ) = ( uint32_t( command ) ) | ( uint32_t( subcommand ) << 16 );
		register uint32_t _d asm( "edx" ) = ( uint32_t( bdoor_port ) ) | ( uint32_t( channel ) << 16 );
		asm volatile( "in %%dx, %%eax" : "+r" ( _a ), "+r" ( _b ), "+r" ( _c ), "+r" ( _d ) :: "memory" );
		return { _a, _b, _c, _d };
	}

	// Channel wrapper.
	//
	struct channel
	{
		uint16_t channel_number = 0xFFFF;

		// Default constructor creates invalid channel, no copy allowed, swapping move.
		//
		channel() {}
		channel( const channel& ) = delete;
		channel& operator=( const channel& ) = delete;
		channel( channel&& o ) : channel_number( std::exchange( o.channel_number, 0xFFFF ) ) {}
		channel& operator=( channel&& o )
		{
			std::swap( channel_number, o.channel_number );
			return *this;
		}

		// Attempts to open a valid channel.
		//
		static channel open()
		{
			channel res;
			auto [_, __, raw_status, channel] = send_command(
				bdoor_cmd::message,
				rpc_magic,
				message_type::open
			);
			message_result status{ .raw = raw_status };
			if ( status.success )
				res.channel_number = channel >> 16;
			return res;
		}

		// Validity check.
		//
		bool is_valid() const { return channel_number != 0xFFFF; }
		explicit operator bool() const { return is_valid(); }

		// Closes the channel.
		//
		void reset()
		{
			if ( is_valid() )
				send_command( bdoor_cmd::message, {}, message_type::close, channel_number );
		}

		// Restarts the channel.
		//
		channel& restart()
		{
			reset();
			return operator=( open() );
		}

		// Sends a message and gets a reply, empty on failure, any valid VMWare reply will be at least 2 bytes.
		//
		std::string send( std::string msg ) const
		{
			// Fail if invalid channel.
			//
			if ( !is_valid() )
				return {};

			// Align string length to 4-bytes.
			//
			size_t original_length = msg.size();
			msg.resize( ( msg.size() + 3 ) & ~3 );

			// Send the length, propagate failure.
			//
			message_result result;
			result.raw = send_command( 
				bdoor_cmd::message, 
				original_length, 
				message_type::send_size, 
				channel_number 
			)[ 2 ];
			if ( !result.success || result.checkpoint )
				return {};

			// Iterate the message in u32 boundaries:
			//
			for ( size_t it = 0; it != msg.size(); it += 4 )
			{
				// Send the partial command, propagate failure.
				//
				result.raw = send_command( 
					bdoor_cmd::message, 
					*( const uint32_t* )( msg.data() + it ), 
					message_type::send_payload, 
					channel_number 
				)[ 2 ];
				if ( !result.success || result.checkpoint )
					return {};
			}
			
			// Get the reply length.
			//
			auto [_, reply_length, reply_result, reply_id] = send_command(
				bdoor_cmd::message,
				{},
				message_type::recv_size,
				channel_number
			);
			result.raw = reply_result;
			reply_id >>= 16;
			if ( !result.success || result.checkpoint )
				return {};

			// If there is a reply:
			//
			if ( result.dorecv )
			{
				// Resize the buffer to 4-byte aligned reply length.
				//
				msg.resize( ( reply_length + 3 ) & ~3 );
				for ( size_t it = 0; it != msg.size(); it += 4 )
				{
					// Fetch the partial reply, propagate failure.
					//
					auto [_, data, recv_result, __] = send_command(
						bdoor_cmd::message,
						reply_id,
						message_type::recv_payload,
						channel_number
					);
					result.raw = recv_result;
					if ( !result.success || result.checkpoint )
					{
						// Instead of returning clear result and let it continue instead,
						// we want to call recv_status.
						//
						reply_length = 0;
						break;
					}
					*( uint32_t* ) ( msg.data() + it ) = data;
				}

				// Resize to the real reply size.
				//
				msg.resize( reply_length );
			}
			// Otherwise return fake success;
			//
			else
			{
				msg.resize( 2 );
				msg[ 0 ] = '1';
				msg[ 1 ] = ' ';
			}

			// Finish the reply, return the result.
			//
			result.raw = send_command(
				bdoor_cmd::message,
				reply_id,
				message_type::recv_status,
				channel_number
			)[ 2 ];
			if ( !result.success || result.checkpoint )
				return {};
			return msg;
		}

		// Destructor cleans up the host resources.
		//
		~channel() { reset(); }
	};
	
	// Global singleton channel and it's lazy getter.
	//
	inline channel g_channel = {};
	inline channel& get_channel()
	{
		if ( !g_channel )
			g_channel = channel::open();
		return g_channel;
	}

	// Sends a message and returns the reply, first two bytes usually signify state.
	//
	inline std::optional<std::string> send( const std::string_view& str )
	{
		// If there is a valid channel:
		//
		std::string result;
		if ( auto& channel = get_channel() )
		{
			// For up to three times:
			//
			for ( size_t n = 0; n != 3; n++ )
			{
				// Try logging the message, if we've got a result, return.
				//
				result = channel.send( "log " + std::string{ str } );
				if ( !result.empty() )
					break;
				channel.restart();
			}
		}

		if ( result.starts_with( "1 " ) )
		{
			result.erase( result.begin(), result.begin() + 2 );
			return result;
		}
		return std::nullopt;
	}
};