/*
	DiscordCoreAPI, A bot library for Discord, written in C++, and featuring explicit multithreading through the usage of custom, asynchronous C++ CoRoutines.

	Copyright 2021, 2022 Chris M. (RealTimeChris)

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
/// VoiceConnection.cpp - Source file for the voice connection class.
/// Jul 15, 2021
/// https://discordcoreapi.com
/// \file VoiceConnection.cpp

#include <discordcoreapi/VoiceConnection.hpp>
#include <discordcoreapi/DiscordCoreClient.hpp>

namespace DiscordCoreAPI {

	constexpr uint16_t webSocketMaxPayloadLengthLarge{ 65535u };
	constexpr uint8_t webSocketPayloadLengthMagicLarge{ 126u };
	constexpr uint8_t webSocketPayloadLengthMagicHuge{ 127u };
	constexpr uint8_t maxHeaderSize{ sizeof(uint64_t) + 2u };
	constexpr uint8_t webSocketMaxPayloadLengthSmall{ 125u };
	constexpr uint8_t webSocketFinishBit{ (1u << 7u) };
	constexpr uint8_t webSocketMaskBit{ (1u << 7u) };

	VoiceSocketReadyData::VoiceSocketReadyData(simdjson::ondemand::value jsonObjectData) {
		this->ip = getString(jsonObjectData, "ip");
		this->ssrc = getUint32(jsonObjectData, "ssrc");
		simdjson::ondemand::array arrayValue{};
		if (jsonObjectData["modes"].get(arrayValue) == simdjson::error_code::SUCCESS) {
			this->mode.clear();
			for (simdjson::simdjson_result<simdjson::ondemand::value> value: arrayValue) {
				if (std::string{ value.get_string().take_value() } == "xsalsa20_poly1305") {
					this->mode = std::string{ value.get_string().take_value() };
				}
			}
		}
		this->port = getUint64(jsonObjectData, "port");
	}

	void OpusDecoderWrapper::OpusDecoderDeleter::operator()(OpusDecoder* other) noexcept {
		if (other) {
			opus_decoder_destroy(other);
			other = nullptr;
		}
	}

	OpusDecoderWrapper& OpusDecoderWrapper::operator=(OpusDecoderWrapper&& other) noexcept {
		if (this != &other) {
			this->ptr.reset(nullptr);
			this->ptr.reset(other.ptr.release());
		}
		return *this;
	}

	OpusDecoderWrapper::OpusDecoderWrapper(OpusDecoderWrapper&& other) noexcept {
		*this = std::move(other);
	}

	OpusDecoderWrapper::OpusDecoderWrapper() {
		int32_t error{};
		this->ptr.reset(nullptr);
		this->ptr.reset(opus_decoder_create(48000, 2, &error));
		if (error != OPUS_OK) {
			throw std::runtime_error{ "Failed to create the Opus Decoder" };
		}
	}

	void OpusDecoderWrapper::decodeData(std::string_view dataToDecode, DecodeData& data) {
		if (data.data.size() == 0) {
			data.data.resize(23040);
		}
		data.sampleCount = opus_decode(*this, reinterpret_cast<const unsigned char*>(dataToDecode.data()),
			static_cast<opus_int32>(dataToDecode.length() & 0x7FFFFFFF), data.data.data(), 5760, 0);
		return;
	}

	OpusDecoderWrapper::operator OpusDecoder*() noexcept {
		return this->ptr.get();
	}

	RTPPacketEncrypter::RTPPacketEncrypter(uint32_t ssrcNew, const std::vector<unsigned char>& keysNew) noexcept {
		this->keys = keysNew;
		this->ssrc = ssrcNew;
	}

	std::basic_string_view<unsigned char> RTPPacketEncrypter::encryptPacket(AudioFrameData& audioData) noexcept {
		if (this->keys.size() > 0) {
			this->sequence++;
			this->timeStamp += audioData.sampleCount;
			const uint8_t headerSize{ 12 };
			char header[headerSize]{};
			storeBits(header, this->version);
			storeBits(header + 1, this->flags);
			storeBits(header + 2, this->sequence);
			storeBits(header + 4, this->timeStamp);
			storeBits(header + 8, this->ssrc);
			uint8_t nonceForLibSodium[crypto_secretbox_NONCEBYTES]{};
			for (uint8_t x = 0; x < headerSize; ++x) {
				nonceForLibSodium[x] = header[x];
			}
			for (uint8_t x = headerSize; x < crypto_secretbox_NONCEBYTES; ++x) {
				nonceForLibSodium[x] = 0;
			}
			uint64_t numOfBytes{ headerSize + audioData.data.size() + crypto_secretbox_MACBYTES };
			if (this->data.size() < numOfBytes) {
				this->data.resize(numOfBytes);
			}
			for (uint8_t x = 0; x < headerSize; ++x) {
				this->data[x] = header[x];
			}
			if (crypto_secretbox_easy(reinterpret_cast<unsigned char*>(this->data.data()) + headerSize, audioData.data.data(), audioData.data.size(),
					nonceForLibSodium, this->keys.data()) != 0) {
				return {};
			}
			return std::basic_string_view<unsigned char>{ this->data.data(), numOfBytes };
		}
		return {};
	}

	VoiceConnection::VoiceConnection(DiscordCoreInternal::BaseSocketAgent* BaseSocketAgentNew, DiscordCoreInternal::WebSocketSSLShard* baseShardNew,
		DiscordCoreAPI::ConfigManager* configManagerNew, std::atomic_bool* doWeQuitNew) noexcept
		: WebSocketCore(configManagerNew, DiscordCoreInternal::WebSocketType::Voice), DatagramSocketClient(StreamType::None) {
		this->dataOpCode = DiscordCoreInternal::WebSocketOpCode::Op_Text;
		this->discordCoreClient = BaseSocketAgentNew->discordCoreClient;
		this->activeState.store(VoiceActiveState::Connecting);
		this->baseSocketAgent = BaseSocketAgentNew;
		this->configManager = configManagerNew;
		this->downSampledVector.resize(23040);
		this->upSampledVector.resize(23040);
		this->baseShard = baseShardNew;
		this->doWeQuit = doWeQuitNew;
	}

	Snowflake VoiceConnection::getChannelId() noexcept {
		return this->voiceConnectInitData.channelId;
	}

	UnboundedMessageBlock<AudioFrameData>& VoiceConnection::getAudioBuffer() noexcept {
		return this->discordCoreClient->getSongAPI(this->voiceConnectInitData.guildId)->audioDataBuffer;
	}

	void VoiceConnection::sendSingleFrame(AudioFrameData& frameData) noexcept {
		this->discordCoreClient->getSongAPI(this->voiceConnectInitData.guildId)->audioDataBuffer.send(std::move(frameData));
	}

	bool VoiceConnection::onMessageReceived(std::string_view data) noexcept {
		std::unique_lock lock00{ this->voiceUserMutex, std::defer_lock_t{} };
		try {
			std::string string{ data };
			string.reserve(string.size() + simdjson::SIMDJSON_PADDING);
			DiscordCoreInternal::WebSocketMessage message{};
			simdjson::ondemand::value value{};
			if (this->parser.iterate(string.data(), string.length(), string.capacity()).get(value) == simdjson::error_code::SUCCESS) {
				message = DiscordCoreInternal::WebSocketMessage{ value };
			}

			if (this->configManager->doWePrintWebSocketSuccessMessages()) {
				cout << shiftToBrightGreen() << "Message received from Voice WebSocket: " << data << reset() << endl << endl;
			}
			if (message.op != 0) {
				switch (message.op) {
					case 2: {
						VoiceSocketReadyData data{ value["d"] };
						this->audioSSRC = data.ssrc;
						this->voiceIp = data.ip;
						this->port = data.port;
						this->audioEncryptionMode = data.mode;
						this->connectionState.store(VoiceConnectionState::Initializing_DatagramSocket);
						break;
					}
					case 4: {
						auto arrayValue = getArray(value["d"], "secret_key");
						if (arrayValue.didItSucceed) {
							std::vector<unsigned char> secretKey{};
							for (auto iterator: arrayValue.arrayValue) {
								secretKey.push_back(static_cast<uint8_t>(iterator.get_uint64().take_value()));
							}
							this->encryptionKey = secretKey;
						}
						this->packetEncrypter = RTPPacketEncrypter{ this->audioSSRC, this->encryptionKey };
						this->connectionState.store(VoiceConnectionState::Collecting_Init_Data);
						break;
					}
					case 5: {
						uint32_t ssrc = getUint32(value["d"], "ssrc");
						VoiceUser user{};
						user.userId = stoull(getString(value["d"], "user_id"));
						lock00.lock();
						this->voiceUsers[ssrc] = std::move(user);
						break;
					}
					case 6: {
						this->haveWeReceivedHeartbeatAck = true;
						break;
					}
					case 8: {
						this->heartBeatStopWatch =
							StopWatch{ std::chrono::milliseconds{ static_cast<uint32_t>(getFloat(value["d"], "heartbeat_interval")) } };
						this->areWeHeartBeating = true;
						this->connectionState.store(VoiceConnectionState::Sending_Identify);
						this->currentState.store(DiscordCoreInternal::WebSocketState::Authenticated);
						this->haveWeReceivedHeartbeatAck = true;
						break;
					}
					case 9: {
						this->connectionState.store(VoiceConnectionState::Initializing_DatagramSocket);
						break;
					}
					case 13: {
						auto userId = stoull(getString(value["d"], "user_id"));
						for (auto& [key, value]: this->voiceUsers) {
							if (userId == value.userId) {
								lock00.lock();
								this->voiceUsers.erase(key);
								break;
							}
						}
						break;
					}
				}
			}
			return true;

		} catch (...) {
			reportException("VoiceConnection::onMessageReceived()");
			this->onClosed();
			this->currentMessage.clear();
			WebSocketCore::inputBuffer.clear();
			this->messageLength = 0;
			this->messageOffset = 0;
			return false;
		}
		this->currentMessage.clear();
		WebSocketCore::inputBuffer.clear();
		this->messageLength = 0;
		this->messageOffset = 0;
		return false;
	}

	void VoiceConnection::reconnectStream() noexcept {
		this->streamSocket->connect(this->streamInfo.address, this->streamInfo.port);
	}

	void VoiceConnection::sendVoiceData(std::basic_string_view<unsigned char> responseData) noexcept {
		try {
			if (responseData.size() == 0) {
				if (this->configManager->doWePrintWebSocketErrorMessages()) {
					cout << shiftToBrightRed() << "Please specify voice data to send" << reset() << endl << endl;
				}
				return;
			} else {
				if (DatagramSocketClient::areWeStillConnected()) {
					DatagramSocketClient::writeData(responseData);
				}
			}
		} catch (...) {
			if (this->configManager->doWePrintWebSocketErrorMessages()) {
				reportException("VoiceConnection::sendVoiceData()");
			}
			this->onClosed();
		}
	}

	void VoiceConnection::sendSpeakingMessage(const bool isSpeaking) noexcept {
		DiscordCoreInternal::SendSpeakingData data{};
		if (!isSpeaking) {
			data.type = static_cast<DiscordCoreInternal::SendSpeakingType>(0);
			this->sendSilence();
			DatagramSocketClient::processIO(DiscordCoreInternal::ProcessIOType::Write_Only);
		} else {
			data.type = DiscordCoreInternal::SendSpeakingType::Microphone;
			data.delay = 0;
			data.ssrc = this->audioSSRC;
			auto serializer = data.operator DiscordCoreAPI::Jsonifier();
			serializer.refreshString(JsonifierSerializeType::Json);
			std::string string = this->prepMessageData(serializer.operator std::string(), DiscordCoreInternal::WebSocketOpCode::Op_Text);
			if (!this->sendMessage(string, true)) {
				this->onClosed();
			}
		}
	}

	void VoiceConnection::checkForAndSendHeartBeat(bool isImmedate) noexcept {
		if (this->heartBeatStopWatch.hasTimePassed() || isImmedate) {
			DiscordCoreAPI::Jsonifier data{};
			data["d"] = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
			data["op"] = 3;
			data.refreshString(DiscordCoreAPI::JsonifierSerializeType::Json);
			std::string string = this->prepMessageData(data.operator std::string(), this->dataOpCode);
			if (!this->sendMessage(string, true)) {
				return;
			}
			this->haveWeReceivedHeartbeatAck = false;
			this->heartBeatStopWatch.resetTimer();
		}
	}

	void VoiceConnection::checkForConnections() {
		if (this->connections) {
			this->connections.reset(nullptr);
			VoiceActiveState currentState{ this->activeState.load() };
			StopWatch stopWatch{ 10000ms };
			this->connectionState.store(VoiceConnectionState::Collecting_Init_Data);
			while (this->baseShard->currentState.load() != DiscordCoreInternal::WebSocketState::Authenticated) {
				if (stopWatch.hasTimePassed() || this->activeState.load() == VoiceActiveState::Exiting) {
					return;
				}
				std::this_thread::sleep_for(1ms);
			}
			this->connectInternal();
			this->sendSpeakingMessage(true);
			this->activeState.store(currentState);
		}
	}

	void VoiceConnection::runBridge(std::stop_token token) noexcept {
		StopWatch stopWatch{ 20ms };
		StopWatch sleepStopWatch{ 20ms };
		int32_t timeToWaitInMs{ 20 };
		int32_t timeTakesToSleep{ 0 };
		int32_t iterationCount{ 0 };
		while (!token.stop_requested()) {
			iterationCount++;
			sleepStopWatch.resetTimer();
			if (timeToWaitInMs - (timeTakesToSleep / iterationCount) >= 0) {
				std::this_thread::sleep_for(std::chrono::milliseconds{ timeToWaitInMs - (timeTakesToSleep / iterationCount) });
			}
			timeTakesToSleep += sleepStopWatch.totalTimePassed();
			this->streamSocket->processIO(DiscordCoreInternal::ProcessIOType::Both);
			this->parseOutGoingVoiceData();
			while (!stopWatch.hasTimePassed()) {
				this->parseIncomingVoiceData();
			}
			stopWatch.resetTimer();
			this->mixAudio();
		}
	}

	void VoiceConnection::runVoice(std::stop_token stopToken) noexcept {
		StopWatch stopWatch{ 20000ms };
		StopWatch sendSilenceStopWatch{ 5000ms };
		while (!stopToken.stop_requested() && !this->doWeQuit->load() && this->activeState.load() != VoiceActiveState::Exiting) {
			switch (this->activeState.load()) {
				case VoiceActiveState::Connecting: {
					while (!stopToken.stop_requested() && this->activeState.load() == VoiceActiveState::Connecting) {
						std::this_thread::sleep_for(1ms);
						if (!stopToken.stop_requested() && VoiceConnection::areWeConnected()) {
							if (WebSocketCore::processIO(10) == DiscordCoreInternal::ProcessIOResult::Error) {
								this->onClosed();
							}
						}
						if (!stopToken.stop_requested() && VoiceConnection::areWeConnected()) {
							this->checkForAndSendHeartBeat(false);
						}
						this->checkForConnections();
					}
					break;
				}
				case VoiceActiveState::Stopped: {
					this->areWePlaying.store(false);
					this->discordCoreClient->getSongAPI(this->voiceConnectInitData.guildId)->audioDataBuffer.clearContents();
					this->clearAudioData();
					while (!stopToken.stop_requested() && this->activeState.load() == VoiceActiveState::Stopped) {
						DatagramSocketClient::processIO(DiscordCoreInternal::ProcessIOType::Both);
						if (sendSilenceStopWatch.hasTimePassed()) {
							sendSilenceStopWatch.resetTimer();
							this->sendSpeakingMessage(true);
							this->sendSpeakingMessage(false);
						}
						std::this_thread::sleep_for(1ms);
						if (!stopToken.stop_requested() && VoiceConnection::areWeConnected()) {
							if (WebSocketCore::processIO(10) == DiscordCoreInternal::ProcessIOResult::Error) {
								this->onClosed();
							}
						}
						if (!stopToken.stop_requested() && VoiceConnection::areWeConnected()) {
							this->checkForAndSendHeartBeat(false);
						}
						this->checkForConnections();
					}
					break;
				}
				case VoiceActiveState::Paused: {
					this->areWePlaying.store(false);
					while (!stopToken.stop_requested() && this->activeState.load() == VoiceActiveState::Paused) {
						DatagramSocketClient::processIO(DiscordCoreInternal::ProcessIOType::Both);
						if (sendSilenceStopWatch.hasTimePassed()) {
							sendSilenceStopWatch.resetTimer();
							this->sendSpeakingMessage(true);
							this->sendSpeakingMessage(false);
						}
						std::this_thread::sleep_for(1ms);
						if (!stopToken.stop_requested() && VoiceConnection::areWeConnected()) {
							if (WebSocketCore::processIO(10) == DiscordCoreInternal::ProcessIOResult::Error) {
								this->onClosed();
							}
						}
						if (!stopToken.stop_requested() && VoiceConnection::areWeConnected()) {
							this->checkForAndSendHeartBeat(false);
						}
						this->checkForConnections();
					}
					break;
				}
				case VoiceActiveState::Playing: {
					DoubleTimePointNs startingValue{ std::chrono::steady_clock::now().time_since_epoch() };
					DoubleTimePointNs intervalCount{ std::chrono::nanoseconds{ 20000000 } };
					DoubleTimePointNs targetTime{ startingValue.time_since_epoch() + intervalCount.time_since_epoch() };
					int64_t frameCounter{ 0 };
					DoubleTimePointNs totalTime{ std::chrono::nanoseconds{ 0 } };
					this->sendSpeakingMessage(false);
					this->sendSpeakingMessage(true);

					this->audioData.type = AudioFrameType::Encoded;
					this->audioData.data.clear();

					stopWatch.resetTimer();
					while (!stopToken.stop_requested() && !DatagramSocketClient::areWeStillConnected()) {
						if (stopWatch.hasTimePassed() || this->activeState.load() == VoiceActiveState::Exiting) {
							return;
						}
						std::this_thread::sleep_for(1ms);
						if (!stopToken.stop_requested() && VoiceConnection::areWeConnected()) {
							if (WebSocketCore::processIO(10) == DiscordCoreInternal::ProcessIOResult::Error) {
								this->onClosed();
							}
						}
						if (!stopToken.stop_requested() && VoiceConnection::areWeConnected()) {
							this->checkForAndSendHeartBeat(false);
						}
						this->checkForConnections();
					}

					while (!stopToken.stop_requested() && this->activeState.load() == VoiceActiveState::Playing) {
						this->areWePlaying.store(true);
						if (!stopToken.stop_requested() && VoiceConnection::areWeConnected()) {
							this->checkForAndSendHeartBeat(false);
						}
						this->checkForConnections();
						this->discordCoreClient->getSongAPI(this->voiceConnectInitData.guildId)->audioDataBuffer.tryReceive(this->audioData);
						if (this->audioData.data.size() == 0) {
							this->areWePlaying.store(false);
						} else {
							this->areWePlaying.store(true);
						}
						if (!this->streamSocket) {
							this->frameQueue.clear();
						}
						frameCounter++;
						if (this->audioData.guildMemberId != 0) {
							this->currentGuildMemberId = this->audioData.guildMemberId;
						}
						std::basic_string_view<unsigned char> newFrame{};
						bool doWeBreak{ false };
						switch (this->audioData.type) {
							case AudioFrameType::RawPCM: {
								auto encodedFrameData = this->encoder.encodeSingleAudioFrame(this->audioData);
								newFrame = this->packetEncrypter.encryptPacket(encodedFrameData);
								break;
							}
							case AudioFrameType::Encoded: {
								newFrame = this->packetEncrypter.encryptPacket(this->audioData);
								break;
							}
							case AudioFrameType::Skip: {
								SongCompletionEventData completionEventData{};
								completionEventData.guild = Guilds::getCachedGuild({ .guildId = this->voiceConnectInitData.guildId });
								if (this->currentGuildMemberId != 0) {
									completionEventData.guildMember = GuildMembers::getCachedGuildMember(
										{ .guildMemberId = this->currentGuildMemberId, .guildId = this->voiceConnectInitData.guildId });
								}
								completionEventData.wasItAFail = false;
								DiscordCoreClient::getSongAPI(this->voiceConnectInitData.guildId)->onSongCompletionEvent(completionEventData);
								this->areWePlaying.store(false);
								doWeBreak = true;
								break;
							}
						}
						if (doWeBreak) {
							break;
						}
						auto waitTime = targetTime - std::chrono::steady_clock::now();
						if (waitTime.count() >= 18000000) {
							if (!stopToken.stop_requested() && VoiceConnection::areWeConnected()) {
								if (WebSocketCore::processIO(1) == DiscordCoreInternal::ProcessIOResult::Error) {
									this->onClosed();
								}
							}
						}
						waitTime = targetTime - std::chrono::steady_clock::now();
						nanoSleep(static_cast<uint64_t>(static_cast<double>(waitTime.count()) * 0.95f));
						waitTime = targetTime - std::chrono::steady_clock::now();
						if (waitTime.count() > 0 && waitTime.count() < 20000000) {
							spinLock(waitTime.count());
						}
						startingValue = std::chrono::steady_clock::now();
						if (newFrame.size() > 0) {
							this->sendVoiceData(newFrame);
						}
						if (this->voiceUsers.size() > 0) {
							for (uint32_t x = 0; x < this->voiceUsers.size(); ++x) {
								DatagramSocketClient::processIO(DiscordCoreInternal::ProcessIOType::Both);
								std::string_view string = DatagramSocketClient::getInputBuffer();
								if (string.size() > 0) {
									this->frameQueue.push_back(static_cast<std::string>(string));
								}
							}
						} else {
							DatagramSocketClient::processIO(DiscordCoreInternal::ProcessIOType::Both);
						}

						this->audioData.data.clear();
						this->audioData.sampleCount = 0;
						this->audioData.type = AudioFrameType::Unset;
						totalTime += std::chrono::steady_clock::now() - startingValue;
						auto intervalCountNew =
							DoubleTimePointNs{ std::chrono::nanoseconds{ 20000000 } - (totalTime.time_since_epoch() / frameCounter) }
								.time_since_epoch()
								.count();
						intervalCount = DoubleTimePointNs{ std::chrono::nanoseconds{ static_cast<uint64_t>(intervalCountNew) } };
						targetTime = std::chrono::steady_clock::now().time_since_epoch() + intervalCount;
					}
					break;
				}
					this->areWePlaying.store(true);
				case VoiceActiveState::Exiting: {
					return;
				}
			}
			if (stopToken.stop_requested()) {
				return;
			}
			std::this_thread::sleep_for(1ms);
		}
	};

	void VoiceConnection::parseOutGoingVoiceData() noexcept {
		std::string_view buffer = this->streamSocket->getInputBuffer();
		if (buffer.size() > 0) {
			AudioFrameData frame{};
			frame.data.insert(frame.data.begin(), buffer.begin(), buffer.end());
			frame.sampleCount = 960;
			frame.type = AudioFrameType::Encoded;
			this->discordCoreClient->getSongAPI(this->voiceConnectInitData.guildId)->audioDataBuffer.send(std::move(frame));
		}
	}

	void VoiceConnection::parseIncomingVoiceData() noexcept {
		if (this->frameQueue.size() > 0) {
			this->rawDataBuffer = std::move(this->frameQueue.front());
			this->frameQueue.pop_front();
			if (this->rawDataBuffer.size() > 0 && this->encryptionKey.size() > 0) {
				auto size = this->rawDataBuffer.size();
				constexpr uint64_t headerSize{ 12 };
				if (this->rawDataBuffer.size() < headerSize) {
					return;
				}

				if (uint8_t payloadType = this->rawDataBuffer[1] & 0b0111'1111; 72 <= payloadType && payloadType <= 76) {
					return;
				}
				uint32_t speakerSsrc{ *reinterpret_cast<uint32_t*>(this->rawDataBuffer.data() + 8) };
				speakerSsrc = ntohl(speakerSsrc);

				uint8_t nonce[24]{};
				for (uint32_t x = 0; x < headerSize; ++x) {
					nonce[x] = this->rawDataBuffer[x];
				}
				const uint64_t csrcCount = this->rawDataBuffer[0] & 0b0000'1111;
				const ptrdiff_t offsetToData = headerSize + sizeof(uint32_t) * csrcCount;
				uint8_t* encryptedData = reinterpret_cast<uint8_t*>(this->rawDataBuffer.data()) + offsetToData;
				const uint64_t encryptedDataLength = size - offsetToData;
				if (this->decryptedString.size() < encryptedDataLength) {
					this->decryptedString.resize(encryptedDataLength);
				}
				if (crypto_secretbox_open_easy(this->decryptedString.data(), encryptedData, encryptedDataLength, nonce, this->encryptionKey.data())) {
					return;
				}
				std::string_view newString{ reinterpret_cast<char*>(this->decryptedString.data()), encryptedDataLength - crypto_secretbox_MACBYTES };

				if ((this->rawDataBuffer[0] >> 4) & 0b0001) {
					uint16_t extenstionLengthInWords{ ntohs(*reinterpret_cast<const uint16_t*>(&newString[2])) };
					size_t extensionLength{ sizeof(uint32_t) * extenstionLengthInWords };
					size_t extensionHeaderLength{ sizeof(uint16_t) * 2 };
					newString = newString.substr(extensionHeaderLength + extensionLength);
				}
				this->rawDataBuffer.clear();
				if (newString.size() > 0) {
					std::unique_lock lock00{ this->voiceUserMutex };
					if (this->voiceUsers.contains(speakerSsrc)) {
						this->voiceUsers[speakerSsrc].decoder.decodeData(newString, this->decodeData);
						if (this->decodeData.sampleCount <= 0) {
							if (this->configManager->doWePrintGeneralErrorMessages()) {
								cout << "Failed to decode user's voice payload." << std::endl;
							}
						} else {
							if (this->decodeData.data.size() != this->decodeData.sampleCount * 2) {
								this->decodeData.data.resize(static_cast<uint64_t>(this->decodeData.sampleCount) * 2);
							}
							this->voiceUsers[speakerSsrc].payloads.emplace_front(std::move(this->decodeData.data));
						}
					}
				}
			}
		}
	}

	bool VoiceConnection::areWeCurrentlyPlaying() noexcept {
		return (this->areWePlaying.load() && this->activeState.load() == VoiceActiveState::Playing) ||
			this->activeState.load() == VoiceActiveState::Paused;
	}

	void VoiceConnection::connectInternal() noexcept {
		StopWatch stopWatch{ 10000ms };
		if (this->currentReconnectTries >= this->maxReconnectTries) {
			this->doWeQuit->store(true);
			if (this->configManager->doWePrintWebSocketErrorMessages()) {
				cout << "VoiceConnection::connect() Error: Failed to connect to voice channel!" << endl << endl;
			}
			return;
		}
		switch (this->connectionState.load()) {
			case VoiceConnectionState::Collecting_Init_Data: {
				this->baseShard->voiceConnectionDataBuffersMap[this->voiceConnectInitData.guildId.operator size_t()] =
					&this->voiceConnectionDataBuffer;
				this->baseShard->voiceConnectionDataBuffersMap[this->voiceConnectInitData.guildId.operator size_t()]->clearContents();
				this->baseShard->getVoiceConnectionData(this->voiceConnectInitData);

				if (waitForTimeToPass(this->voiceConnectionDataBuffer, this->voiceConnectionData, 10000)) {
					this->currentReconnectTries++;
					this->onClosed();
					return;
				}
				this->baseUrl = this->voiceConnectionData.endPoint.substr(0, this->voiceConnectionData.endPoint.find(":"));
				this->connectionState.store(VoiceConnectionState::Initializing_WebSocket);
				this->connectInternal();
				break;
			}
			case VoiceConnectionState::Initializing_WebSocket: {
				if (!WebSocketCore::connect(this->baseUrl, "443", this->configManager->doWePrintWebSocketErrorMessages(), false)) {
					this->currentReconnectTries++;
					this->onClosed();
					return;
				}
				WebSocketCore::currentState.store(DiscordCoreInternal::WebSocketState::Upgrading);
				std::string sendVector = "GET /?v=4 HTTP/1.1\r\nHost: " + this->baseUrl +
					"\r\nPragma: no-cache\r\nUser-Agent: DiscordCoreAPI/1.0\r\nUpgrade: WebSocket\r\nConnection: "
					"Upgrade\r\nSec-WebSocket-Key: " +
					generateBase64EncodedKey() + "\r\nSec-WebSocket-Version: 13\r\n\r\n";
				this->shard[0] = 0;
				this->shard[1] = 1;
				if (!WebSocketCore::sendMessage(sendVector, true)) {
					this->currentReconnectTries++;
					this->onClosed();
					return;
				}
				while (this->currentState.load() != DiscordCoreInternal::WebSocketState::Collecting_Hello) {
					if (WebSocketCore::processIO(10) == DiscordCoreInternal::ProcessIOResult::Error) {
						this->currentReconnectTries++;
						this->onClosed();
						return;
					}
				}
				this->connectionState.store(VoiceConnectionState::Collecting_Hello);
				this->connectInternal();
				break;
			}
			case VoiceConnectionState::Collecting_Hello: {
				stopWatch.resetTimer();
				while (this->connectionState.load() != VoiceConnectionState::Sending_Identify) {
					if (stopWatch.hasTimePassed()) {
						this->currentReconnectTries++;
						this->onClosed();
						return;
					}
					if (WebSocketCore::processIO(100) == DiscordCoreInternal::ProcessIOResult::Error) {
						this->currentReconnectTries++;
						this->onClosed();
						return;
					}
					std::this_thread::sleep_for(1ms);
				}
				this->currentReconnectTries = 0;
				this->connectInternal();
				break;
			}
			case VoiceConnectionState::Sending_Identify: {
				this->haveWeReceivedHeartbeatAck = true;
				DiscordCoreInternal::VoiceIdentifyData data{};
				data.connectInitData = this->voiceConnectInitData;
				data.connectionData = this->voiceConnectionData;
				auto serializer = data.operator DiscordCoreAPI::Jsonifier();
				serializer.refreshString(JsonifierSerializeType::Json);
				std::string string = this->prepMessageData(serializer.operator std::string(), this->dataOpCode);
				if (!WebSocketCore::sendMessage(string, true)) {
					this->currentReconnectTries++;
					this->onClosed();
					return;
				}
				this->connectionState.store(VoiceConnectionState::Collecting_Ready);
				this->connectInternal();
				break;
			}
			case VoiceConnectionState::Collecting_Ready: {
				stopWatch.resetTimer();
				while (this->connectionState.load() != VoiceConnectionState::Initializing_DatagramSocket) {
					if (stopWatch.hasTimePassed()) {
						this->currentReconnectTries++;
						this->onClosed();
						return;
					}
					if (WebSocketCore::processIO(100) == DiscordCoreInternal::ProcessIOResult::Error) {
						this->currentReconnectTries++;
						this->onClosed();
						return;
					}
					std::this_thread::sleep_for(1ms);
				}
				this->connectInternal();
				break;
			}
			case VoiceConnectionState::Initializing_DatagramSocket: {
				this->voiceConnect();
				this->connectionState.store(VoiceConnectionState::Sending_Select_Protocol);
				this->connectInternal();
				break;
			}
			case VoiceConnectionState::Sending_Select_Protocol: {
				DiscordCoreInternal::VoiceSocketProtocolPayloadData data{};
				data.voiceEncryptionMode = this->audioEncryptionMode;
				data.externalIp = this->externalIp;
				data.voicePort = this->port;
				auto serializer = data.operator DiscordCoreAPI::Jsonifier();
				serializer.refreshString(JsonifierSerializeType::Json);
				std::string string = this->prepMessageData(serializer.operator std::string(), this->dataOpCode);
				if (!WebSocketCore::sendMessage(string, true)) {
					this->currentReconnectTries++;
					this->onClosed();
					return;
				}
				this->connectionState.store(VoiceConnectionState::Collecting_Session_Description);
				this->connectInternal();
				break;
			}
			case VoiceConnectionState::Collecting_Session_Description: {
				stopWatch.resetTimer();
				while (this->connectionState.load() != VoiceConnectionState::Collecting_Init_Data) {
					if (stopWatch.hasTimePassed()) {
						this->currentReconnectTries++;
						this->onClosed();
						return;
					}
					if (WebSocketCore::processIO(100) == DiscordCoreInternal::ProcessIOResult::Error) {
						this->currentReconnectTries++;
						this->onClosed();
						return;
					}
					std::this_thread::sleep_for(1ms);
				}
				this->baseShard->voiceConnectionDataBuffersMap[this->voiceConnectInitData.guildId.operator size_t()]->clearContents();
				this->connectionState.store(VoiceConnectionState::Collecting_Init_Data);
				if (this->streamType != StreamType::None) {
					this->streamSocket = std::make_unique<DatagramSocketClient>(this->streamType);
					if (this->taskThread02) {
						this->taskThread02.reset(nullptr);
					}
					this->taskThread02 = std::make_unique<std::jthread>([=, this](std::stop_token stopToken) {
						this->streamSocket->connect(this->streamInfo.address, this->streamInfo.port);
						this->runBridge(stopToken);
					});
				}
				this->play();
				return;
			}
		}
	}

	void VoiceConnection::clearAudioData() noexcept {
		if (this->audioData.data.size() != 0) {
			this->audioData.data.clear();
			this->audioData = AudioFrameData();
		}
		AudioFrameData frameData{};
		while (this->discordCoreClient->getSongAPI(this->voiceConnectInitData.guildId)->audioDataBuffer.tryReceive(frameData)) {
		};
	}

	bool VoiceConnection::areWeConnected() noexcept {
		return WebSocketCore::areWeStillConnected() && DatagramSocketClient::areWeStillConnected();
	}

	void VoiceConnection::onClosed() noexcept {
		this->connectionState.store(VoiceConnectionState::Collecting_Init_Data);
		if (this->activeState.load() != VoiceActiveState::Exiting && this->currentReconnectTries < this->maxReconnectTries) {
			this->reconnect();
		} else if (this->currentReconnectTries >= this->maxReconnectTries) {
			VoiceConnection::disconnect();
		}
	}

	bool VoiceConnection::voiceConnect() noexcept {
		try {
			if (!DatagramSocketClient::areWeStillConnected()) {
				if (!DatagramSocketClient::connect(this->voiceIp, std::to_string(this->port))) {
					return false;
				} else {
					unsigned char packet[74]{};
					uint16_t val1601{ 0x01 };
					uint16_t val1602{ 70 };
					packet[0] = static_cast<uint8_t>(val1601 >> 8);
					packet[1] = static_cast<uint8_t>(val1601 >> 0);
					packet[2] = static_cast<uint8_t>(val1602 >> 8);
					packet[3] = static_cast<uint8_t>(val1602 >> 0);
					packet[4] = static_cast<uint8_t>(this->audioSSRC >> 24);
					packet[5] = static_cast<uint8_t>(this->audioSSRC >> 16);
					packet[6] = static_cast<uint8_t>(this->audioSSRC >> 8);
					packet[7] = static_cast<uint8_t>(this->audioSSRC);
					DatagramSocketClient::getInputBuffer();
					DatagramSocketClient::writeData(std::basic_string_view<unsigned char>{ packet, std::size(packet) });
					std::string inputString{};
					StopWatch stopWatch{ 5500ms };
					while (inputString.size() < 74 && !this->doWeQuit->load() && this->activeState.load() != VoiceActiveState::Exiting) {
						DatagramSocketClient::processIO(DiscordCoreInternal::ProcessIOType::Both);
						inputString = DatagramSocketClient::getInputBuffer();
						std::this_thread::sleep_for(1ms);
						if (stopWatch.hasTimePassed()) {
							return false;
						}
					}
					std::string message{};
					if (inputString.size() > 64) {
						message.insert(message.begin(), inputString.begin() + 8, inputString.begin() + 64);
					}
					auto endLineFind = message.find('\u0000', 5);
					if (endLineFind != std::string::npos) {
						message = message.substr(0, endLineFind);
					}
					this->externalIp = message;
					this->voiceConnectionDataBuffer.clearContents();
					return true;
				}
			} else {
				return true;
			}
		} catch (...) {
			if (this->configManager->doWePrintWebSocketErrorMessages()) {
				reportException("VoiceConnection::voiceConnect()");
			}
			this->onClosed();
			return false;
		}
	}

	void VoiceConnection::sendSilence() noexcept {
		AudioFrameData newFrame{};
		newFrame.data.emplace_back(0xf8);
		newFrame.data.emplace_back(0xff);
		newFrame.data.emplace_back(0xfe);
		auto frame = this->packetEncrypter.encryptPacket(newFrame);
		this->sendVoiceData(frame);
	}

	void VoiceConnection::pauseToggle() noexcept {
		if (this) {
			if (this->activeState.load() == VoiceActiveState::Paused) {
				this->activeState.store(VoiceActiveState::Playing);
			} else {
				this->activeState.store(VoiceActiveState::Paused);
			}
		}
	}

	void VoiceConnection::disconnect() noexcept {
		this->activeState.store(VoiceActiveState::Exiting);
		if (this->taskThread01) {
			this->taskThread01->request_stop();
			if (this->taskThread01->joinable()) {
				this->taskThread01->detach();
			}
			this->taskThread01.reset(nullptr);
		}
		if (this->taskThread02) {
			this->taskThread02->request_stop();
			if (this->taskThread02->joinable()) {
				this->taskThread02->detach();
			}
			this->taskThread02.reset(nullptr);
		}
		DatagramSocketClient::disconnect();
		if (this->streamSocket && this->streamSocket->areWeStillConnected()) {
			this->streamSocket->disconnect();
		}
		if (DiscordCoreClient::getSongAPI(this->voiceConnectInitData.guildId)) {
			DiscordCoreClient::getSongAPI(this->voiceConnectInitData.guildId)
				->onSongCompletionEvent.remove(DiscordCoreClient::getSongAPI(this->voiceConnectInitData.guildId)->eventToken);
		}
		this->closeCode = 0;
		this->areWeHeartBeating = false;
		this->currentReconnectTries = 0;
		WebSocketCore::ssl = nullptr;
		WebSocketCore::outputBuffer.clear();
		WebSocketCore::inputBuffer.clear();
		WebSocketCore::socket = SOCKET_ERROR;
		DatagramSocketClient::outputBuffer.clear();
		DatagramSocketClient::inputBuffer.clear();
		this->activeState.store(VoiceActiveState::Connecting);
		this->connectionState.store(VoiceConnectionState::Collecting_Init_Data);
		this->currentState.store(DiscordCoreInternal::WebSocketState::Disconnected);
		this->discordCoreClient->getSongAPI(this->voiceConnectInitData.guildId)->audioDataBuffer.clearContents();
	}

	void VoiceConnection::reconnect() noexcept {
		this->currentState.store(DiscordCoreInternal::WebSocketState::Disconnected);
		WebSocketCore::ssl = nullptr;
		WebSocketCore::socket = SOCKET_ERROR;
		DatagramSocketClient::disconnect();
		WebSocketCore::outputBuffer.clear();
		WebSocketCore::inputBuffer.clear();
		DatagramSocketClient::outputBuffer.clear();
		DatagramSocketClient::inputBuffer.clear();
		this->closeCode = 0;
		this->areWeHeartBeating = false;
		this->currentReconnectTries++;
		this->connections = std::make_unique<ConnectionPackage>();
		this->connections->currentReconnectTries = this->currentReconnectTries;
		this->connections->currentShard = this->shard[0];
	}

	void VoiceConnection::mixAudio() noexcept {
		if (this->voiceUsers.size() > 0) {
			opus_int32 voiceUserCount{};
			size_t decodedSize{};
			std::memset(this->upSampledVector.data(), 0b00000000, this->upSampledVector.size() * sizeof(int32_t));
			std::unique_lock lock{ this->voiceUserMutex };
			for (auto& [key, value]: this->voiceUsers) {
				if (!lock.owns_lock()) {
					lock.lock();
				}
				if (value.payloads.size() > 0) {
					auto payload = std::move(value.payloads.back());
					value.payloads.pop_back();
					lock.unlock();
					if (payload.size() > 0) {
						decodedSize = std::max(decodedSize, payload.size());
						voiceUserCount++;
						for (uint32_t x = 0; x < payload.size(); ++x) {
							this->upSampledVector[x] += static_cast<opus_int32>(payload[x]);
						}
					}
				}
			}
			if (decodedSize > 0) {
				for (int32_t x = 0; x < decodedSize; ++x) {
					this->downSampledVector[x] = static_cast<opus_int16>(this->upSampledVector[x] / voiceUserCount);
				}
				auto encodedData =
					this->encoder.encodeSingleAudioFrame(std::basic_string_view<opus_int16>{ this->downSampledVector.data(), decodedSize });
				if (encodedData.data.size() <= 0) {
					if (this->configManager->doWePrintGeneralErrorMessages()) {
						cout << "Failed to encode user's voice payload." << endl;
					}
				} else {
					this->streamSocket->writeData(encodedData.data);
				}
			}
		}
	}

	void VoiceConnection::connect(DiscordCoreAPI::VoiceConnectInitData initData) noexcept {
		if (this->baseSocketAgent) {
			this->voiceConnectInitData = initData;
			this->connections = std::make_unique<ConnectionPackage>();
			this->connections->currentReconnectTries = this->currentReconnectTries;
			this->connections->currentShard = this->shard[0];
			this->streamInfo = initData.streamInfo;
			this->streamType = initData.streamType;
			this->activeState.store(VoiceActiveState::Connecting);
			if (!this->taskThread01) {
				this->taskThread01 = std::make_unique<std::jthread>([=, this](std::stop_token stopToken) {
					this->runVoice(stopToken);
				});
			}
		}
	}

	bool VoiceConnection::stop() noexcept {
		this->sendSpeakingMessage(false);
		this->activeState.store(VoiceActiveState::Stopped);
		return true;
	}

	bool VoiceConnection::play() noexcept {
		this->activeState.store(VoiceActiveState::Playing);
		return true;
	}

	VoiceConnection::~VoiceConnection() {
	}

}