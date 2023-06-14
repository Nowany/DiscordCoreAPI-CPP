/*
	DiscordCoreAPI, A bot library for Discord, written in C++, and featuring explicit multithreading through the usage of custom, asynchronous C++ CoRoutines.

	Copyright 2021, 2022, 2023 Chris M. (RealTimeChris)

	This library is free software; you can redistribute it and/or
	modify it under the terms of the GNU Lesser General Public
	License as published by the Free Software Foundation; either
	version 2.1 of the License, or (at your option) any later version.

	This library is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
	Lesser General Public License for more details.

	You should have received a copy of the GNU Lesser General Public
	License along with this library; if not, write to the Free Software
	Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301
	USA
*/
/// UDPConnection.hpp - Header file for the "UDP Connection" stuff.
/// Dec 12, 2021
/// https://discordcoreapi.com
/// \file UDPConnection.hpp

#pragma once

#include <discordcoreapi/FoundationEntities.hpp>
#include <discordcoreapi/TCPConnection.hpp>

namespace DiscordCoreInternal {

	class DiscordCoreAPI_Dll UDPConnection {
	  public:
		friend class DiscordCoreAPI::VoiceConnection;

		UDPConnection(const std::string& baseUrlNew, uint16_t portNew, DiscordCoreAPI::StreamType streamType, bool doWePrintErrors,
			std::stop_token token = std::stop_token{});

		void writeData(std::basic_string_view<uint8_t> dataToWrite);

		std::basic_string_view<uint8_t> getInputBuffer() noexcept;

		virtual void handleAudioBuffer() noexcept = 0;

		bool areWeStillConnected() noexcept;

		void disconnect() noexcept;

		bool processWriteData();

		bool processReadData();

		void processIO();

		~UDPConnection() noexcept;

	  protected:
		const uint64_t maxBufferSize{ (1024 * 16) };
		DiscordCoreAPI::StreamType streamType{};
		RingBuffer<uint8_t, 16> outputBuffer{};
		RingBuffer<uint8_t, 16> inputBuffer{};
		addrinfoWrapper address{};
		bool doWePrintErrors{};
		SOCKETWrapper socket{};
		std::string baseUrl{};
		int64_t bytesRead{};
		uint16_t port{};
	};
}
