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
#include <cstring>
#include <string_view>
#include <optional>
#include <tuple>
#include <initializer_list>

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
			channel_number = 0xFFFF;
		}

		// Restarts the channel.
		//
		channel& restart()
		{
			reset();
			return operator=( open() );
		}

		// Internal send/recv functions.
		//
		bool send_msg( const std::initializer_list<std::string_view>& segments )
		{
			size_t total_msg_length = 0;
			for ( auto& n : segments )
				total_msg_length += n.size();
			auto read4 = [ & ] ( size_t it ) -> uint32_t
			{
				// Enumerate every segment:
				//
				uint32_t dword = 0;
				size_t pos = 0, epos = 0;
				for ( auto& msg : segments )
				{
					pos = epos;

					// If beyond limit, break, we're done.
					//
					if ( pos >= ( it + 4 ) )
						return dword;

					// If before limit, skip.
					//
					epos = pos + msg.size();
					if ( epos <= it )
						continue;

					// Copy the relevant part.
					//
					char* dbegin = ( char* ) &dword;
					const char* sbegin = msg.data();
					if ( pos < it ) sbegin += it - pos;
					else            dbegin += pos - it;
					memcpy( dbegin, sbegin, std::min<size_t>( epos - it, 4 ) );
				}
				return dword;
			};

			while( true )
			{
				message_result result = { .raw = 0 };

				// Send the length.
				//
				result.raw = send_command( 
					bdoor_cmd::message, 
					total_msg_length,
					message_type::send_size, 
					channel_number 
				)[ 2 ];
				if ( !result.success )
					return false;

				// Iterate the message in u32 boundaries:
				//
				for ( size_t it = 0; it < total_msg_length; it += 4 )
				{
					// Send the partial command, propagate failure.
					//
					result.raw = send_command( 
						bdoor_cmd::message,
						read4( it ) , 
						message_type::send_payload, 
						channel_number 
					)[ 2 ];

					// If server reported a checkpoint, retry the entire operation.
					//
					if ( result.checkpoint )
						break;

					// If server reported an error, fail.
					//
					if ( !result.success )
						return false;
				}

				// If no retry required, quit.
				//
				if ( !result.checkpoint )
					return true;
			}
			return false;
		}
		std::optional<std::string> recv_reply()
		{
			std::string buffer;
			while ( true )
			{
				message_result result = { .raw = 0 };

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
				if ( !result.success )
					return std::nullopt;

				// If there is no reply, return empty string.
				//
				if( !result.dorecv )
					return std::string{};

				// Resize the buffer to 4-byte aligned reply length.
				//
				buffer.resize( ( reply_length + 3 ) & ~3 );
				for ( size_t it = 0; it != buffer.size(); it += 4 )
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

					// If server reported a checkpoint, retry the entire operation.
					//
					if ( result.checkpoint )
						break;

					// If server reported an error, fail.
					//
					if ( !result.success )
						return std::nullopt;

					// Otherwise write the partial reply.
					//
					*( uint32_t* ) ( buffer.data() + it ) = data;
				}

				// Handle checkpoints.
				//
				if ( result.checkpoint )
					continue;

				// Resize to the real reply size.
				//
				buffer.resize( reply_length );

				// Finish the reply and return the final message.
				//
				result.raw = send_command(
					bdoor_cmd::message,
					reply_id,
					message_type::recv_status,
					channel_number
				)[ 2 ];
				if ( !result.success )
					return std::nullopt;
				if ( result.checkpoint )
					continue;
				return buffer;
			}
			return std::nullopt;
		}

		// Combined send.
		//
		std::optional<std::string> send( const std::initializer_list<std::string_view>& segments )
		{
			if ( !send_msg( segments ) )
				return std::nullopt;
			return recv_reply();
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

	// Sends a message and ignores the reply.
	//
	template<typename... Tx>
	inline void send_n( std::string_view msg_0, Tx&&... msg_n )
	{
		// If there is a valid channel:
		//
		if ( auto& channel = get_channel() )
		{
			// Try sending the message, retry up to three times.
			//
			for ( size_t n = 0; n != 3; n++ )
			{
				if ( channel.send_msg( { "log ", msg_0, ( std::string_view ) msg_n... } ) )
				{
					channel.reset();
					break;
				}
				channel.restart();
			}
		}
	}

	// Sends a message and returns the reply.
	//
	template<typename... Tx>
	inline std::pair<bool, std::string> send( std::string_view msg_0, Tx&&... msg_n )
	{
		bool success = false;
		std::string result = {};

		// If there is a valid channel:
		//
		if ( auto& channel = get_channel() )
		{
			// Try sending the message, retry up to three times.
			//
			for ( size_t n = 0; n != 3; n++ )
			{
				if ( auto res = channel.send( { "log ", msg_0, ( std::string_view ) msg_n... } ) )
				{
					success = true;
					result = std::move( res ).value();
					break;
				}
				channel.restart();
			}

			// If reply starts with standard VMWare header, parse it.
			//
			if ( result.starts_with( "0 " ) )
				success = false, result.erase( result.begin(), result.begin() + 2 );
			else if ( result.starts_with( "1 " ) )
				success = true, result.erase( result.begin(), result.begin() + 2 );
		}

		return std::pair{ success, std::move( result ) };
	}
};