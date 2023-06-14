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
/// SongAPI.cpp - Source file for the song api related stuff.
/// Sep 17, 2021
/// https://discordcoreapi.com
/// \file SongAPI.cpp

#include <discordcoreapi/SongAPI.hpp>
#include <discordcoreapi/DiscordCoreClient.hpp>
#include <discordcoreapi/VoiceConnection.hpp>
#include <discordcoreapi/SoundCloudAPI.hpp>
#include <discordcoreapi/YouTubeAPI.hpp>

namespace DiscordCoreAPI {

	SongAPI::SongAPI(const Snowflake guildIdNew) {
		guildId = guildIdNew;
	}

	void SongAPI::onSongCompletion(std::function<CoRoutine<void>(SongCompletionEventData)> handler, const Snowflake guildId) {
		SongAPI* returnData = DiscordCoreClient::getSongAPI(guildId);
		returnData->onSongCompletionEvent.erase(returnData->eventToken);
		returnData->eventToken = returnData->onSongCompletionEvent.add(handler);
	}

	bool SongAPI::sendNextSong() {
		if (playlist.isLoopSongEnabled) {
			if (playlist.songQueue.size() > 1 && playlist.currentSong.songId == "") {
				playlist.currentSong = playlist.songQueue[0];
				for (int32_t x = 0; x < playlist.songQueue.size(); ++x) {
					if (x == playlist.songQueue.size() - 1) {
						break;
					}
					playlist.songQueue[x] = playlist.songQueue[static_cast<int64_t>(x + static_cast<int64_t>(1))];
				}
				playlist.songQueue.erase(playlist.songQueue.end() - 1, playlist.songQueue.end());
				return true;
			} else if (playlist.songQueue.size() > 0 && playlist.currentSong.songId == "") {
				return true;
			} else if (playlist.currentSong.songId != "" && playlist.songQueue.size() == 0) {
				return true;
			} else if (playlist.songQueue.size() == 1 && playlist.currentSong.songId == "") {
				playlist.currentSong = playlist.songQueue[0];
				playlist.songQueue.erase(playlist.songQueue.begin(), playlist.songQueue.begin() + 1);
				return true;
			} else if (playlist.currentSong.songId == "") {
				return false;
			}
		} else if (playlist.isLoopAllEnabled) {
			if (playlist.songQueue.size() > 1 && playlist.currentSong.songId == "") {
				playlist.currentSong = playlist.songQueue[0];
				for (int32_t x = 0; x < playlist.songQueue.size(); ++x) {
					if (x == playlist.songQueue.size() - 1) {
						break;
					}
					playlist.songQueue[x] = playlist.songQueue[static_cast<int64_t>(x + static_cast<int64_t>(1))];
				}
				playlist.songQueue.erase(playlist.songQueue.end() - 1, playlist.songQueue.end());
				return true;
			} else if (playlist.songQueue.size() > 0 && playlist.currentSong.songId != "") {
				Song tempSong02 = playlist.currentSong;
				playlist.currentSong = playlist.songQueue[0];
				for (int32_t x = 0; x < playlist.songQueue.size(); ++x) {
					if (x == playlist.songQueue.size() - 1) {
						break;
					}
					playlist.songQueue[x] = playlist.songQueue[static_cast<int64_t>(x + static_cast<int64_t>(1))];
				}
				playlist.songQueue[playlist.songQueue.size() - 1] = tempSong02;
				return true;
			} else if (playlist.currentSong.songId != "" && playlist.songQueue.size() == 0) {
				return true;
			} else if (playlist.songQueue.size() == 1 && playlist.currentSong.songId == "") {
				playlist.currentSong = playlist.songQueue[0];
				playlist.songQueue.erase(playlist.songQueue.begin(), playlist.songQueue.begin() + 1);
				return true;
			} else if (playlist.currentSong.songId == "") {
				return false;
			}
		} else {
			if (playlist.songQueue.size() > 0 && (playlist.currentSong.songId != "" || playlist.currentSong.songId == "")) {
				playlist.currentSong = playlist.songQueue[0];
				for (int32_t x = 0; x < playlist.songQueue.size() - 1; ++x) {
					playlist.songQueue[x] = playlist.songQueue[static_cast<int64_t>(x + static_cast<int64_t>(1))];
				}
				playlist.songQueue.erase(playlist.songQueue.end() - 1, playlist.songQueue.end());
				return true;
			} else if (playlist.currentSong.songId != "" && playlist.songQueue.size() == 0) {
				playlist.currentSong = Song();
				return true;
			} else if (playlist.currentSong.songId == "") {
				return false;
			}
		}
		return false;
	}

	bool SongAPI::play(const Snowflake guildId) {
		return DiscordCoreClient::getVoiceConnection(guildId)->play();
	}

	void SongAPI::pauseToggle(const Snowflake guildId) {
		DiscordCoreClient::getVoiceConnection(guildId)->pauseToggle();
	}

	bool SongAPI::areWeCurrentlyPlaying(const Snowflake guildId) {
		return DiscordCoreClient::getVoiceConnection(guildId)->areWeCurrentlyPlaying();
	}

	void SongAPI::skip(const GuildMember& guildMember) {
		DiscordCoreClient::getSongAPI(guildMember.guildId)->cancelCurrentSong();
		DiscordCoreAPI::Song newSong{};
		DiscordCoreClient::getSongAPI(guildMember.guildId)->audioDataBuffer.clearContents();
		if (SongAPI::isLoopAllEnabled(guildMember.guildId) || SongAPI::isLoopSongEnabled(guildMember.guildId)) {
			DiscordCoreClient::getSongAPI(guildMember.guildId)
				->playlist.songQueue.emplace_back(DiscordCoreClient::getSongAPI(guildMember.guildId)->playlist.currentSong);
			SongAPI::setCurrentSong(newSong, guildMember.guildId);
		} else {
			SongAPI::setCurrentSong(newSong, guildMember.guildId);
		}
		DiscordCoreClient::getVoiceConnection(guildMember.guildId)->doWeSkipNow.store(true);
	}

	void SongAPI::stop(const Snowflake guildId) {
		DiscordCoreClient::getVoiceConnection(guildId)->stop();
		DiscordCoreClient::getSongAPI(guildId)->cancelCurrentSong();
		std::vector<Song> newVector02;
		newVector02.emplace_back(DiscordCoreClient::getSongAPI(guildId)->playlist.currentSong);
		for (auto& value: DiscordCoreClient::getSongAPI(guildId)->playlist.songQueue) {
			newVector02.emplace_back(value);
		}
		DiscordCoreClient::getSongAPI(guildId)->playlist.songQueue = newVector02;
		auto returnData = DiscordCoreClient::getSongAPI(guildId);
		if (returnData) {
			DiscordCoreClient::getSongAPI(guildId)->onSongCompletionEvent.erase(DiscordCoreClient::getSongAPI(guildId)->eventToken);
			DiscordCoreClient::getSongAPI(guildId)->eventToken = DiscordCoreInternal::EventDelegateToken{};
		}
	}

	std::vector<Song> SongAPI::searchForSong(const std::string& searchQuery, const Snowflake guildId) {
		auto vector01 = DiscordCoreClient::getSoundCloudAPI(guildId)->searchForSong(searchQuery);
		auto vector02 = DiscordCoreClient::getYouTubeAPI(guildId)->searchForSong(searchQuery);
		int32_t totalLength = static_cast<int32_t>(vector01.size() + vector02.size());
		std::vector<Song> newVector{};
		int32_t vector01Used{};
		int32_t vector02Used{};
		for (int32_t x = 0; x < totalLength; ++x) {
			if ((vector01Used < vector01.size()) && (x % 2 == 0) && vector01.size() > 0) {
				newVector.emplace_back(vector01[vector01Used]);
				newVector[newVector.size() - 1].type = SongType::SoundCloud;
				++vector01Used;
			} else if (vector02Used < vector02.size() && vector02.size() > 0) {
				newVector.emplace_back(vector02[vector02Used]);
				newVector[newVector.size() - 1].type = SongType::YouTube;
				++vector02Used;
			}
		}
		return newVector;
	}

	void SongAPI::setLoopAllStatus(bool enabled, const Snowflake guildId) {
		DiscordCoreClient::getSongAPI(guildId)->playlist.isLoopAllEnabled = enabled;
	}

	bool SongAPI::isLoopAllEnabled(const Snowflake guildId) {
		return DiscordCoreClient::getSongAPI(guildId)->playlist.isLoopAllEnabled;
	}

	void SongAPI::setLoopSongStatus(bool enabled, const Snowflake guildId) {
		DiscordCoreClient::getSongAPI(guildId)->playlist.isLoopSongEnabled = enabled;
	}

	bool SongAPI::isLoopSongEnabled(const Snowflake guildId) {
		return DiscordCoreClient::getSongAPI(guildId)->playlist.isLoopSongEnabled;
	}

	bool SongAPI::isThereAnySongs(const Snowflake guildId) {
		if (DiscordCoreClient::getSongAPI(guildId)->playlist.isLoopAllEnabled || DiscordCoreClient::getSongAPI(guildId)->playlist.isLoopSongEnabled) {
			if (DiscordCoreClient::getSongAPI(guildId)->playlist.songQueue.size() == 0 &&
				DiscordCoreClient::getSongAPI(guildId)->playlist.currentSong.songId == "") {
				return false;
			} else {
				return true;
			}
		} else {
			if (DiscordCoreClient::getSongAPI(guildId)->playlist.songQueue.size() == 0) {
				return false;
			} else {
				return true;
			}
		}
	}

	Song SongAPI::addSongToQueue(const GuildMember& guildMember, Song& song) {
		song.addedByUserId = guildMember.user.id;
		song.addedByUserName = (( GuildMemberData& )guildMember).getUserData().userName;
		DiscordCoreClient::getSongAPI(guildMember.guildId)->playlist.songQueue.emplace_back(song);
		return song;
	}

	void SongAPI::setPlaylist(const Playlist& playlistNew, const Snowflake guildId) {
		DiscordCoreClient::getSongAPI(guildId)->playlist = playlistNew;
	}

	Playlist SongAPI::getPlaylist(const Snowflake guildId) {
		return DiscordCoreClient::getSongAPI(guildId)->playlist;
	}

	void SongAPI::modifyQueue(int32_t firstSongPosition, int32_t secondSongPosition, const Snowflake guildId) {
		Song tempSong = DiscordCoreClient::getSongAPI(guildId)->playlist.songQueue[firstSongPosition];
		DiscordCoreClient::getSongAPI(guildId)->playlist.songQueue[firstSongPosition] =
			DiscordCoreClient::getSongAPI(guildId)->playlist.songQueue[secondSongPosition];
		DiscordCoreClient::getSongAPI(guildId)->playlist.songQueue[secondSongPosition] = tempSong;
	}

	Song SongAPI::getCurrentSong(const Snowflake guildId) {
		if (DiscordCoreClient::getSongAPI(guildId)->playlist.currentSong.songId != "") {
			return DiscordCoreClient::getSongAPI(guildId)->playlist.currentSong;
		} else if (DiscordCoreClient::getSongAPI(guildId)->playlist.songQueue.size() > 0) {
			return DiscordCoreClient::getSongAPI(guildId)->playlist.songQueue[0];
		} else {
			return Song();
		};
	}

	void SongAPI::setCurrentSong(const Song& song, const Snowflake guildId) {
		DiscordCoreClient::getSongAPI(guildId)->playlist.currentSong = song;
	}

	void SongAPI::sendNextSongFinal(const GuildMember& guildMember) {
		cancelCurrentSong();
		if (playlist.currentSong.type == SongType::SoundCloud) {
			Song newerSong{};
			newerSong = DiscordCoreClient::getSoundCloudAPI(guildMember.guildId)->collectFinalSong(playlist.currentSong);
			taskThread = std::make_unique<std::jthread>([=, this](std::stop_token eventToken) {
				DiscordCoreClient::getSoundCloudAPI(guildId)->downloadAndStreamAudio(newerSong, eventToken, 0);
			});

		} else if (playlist.currentSong.type == SongType::YouTube) {
			Song newerSong{};
			newerSong = DiscordCoreClient::getYouTubeAPI(guildMember.guildId)->collectFinalSong(playlist.currentSong);
			taskThread = std::make_unique<std::jthread>([=, this](std::stop_token eventToken) {
				DiscordCoreClient::getYouTubeAPI(guildId)->downloadAndStreamAudio(newerSong, eventToken, 0);
			});
		};
	}

	bool SongAPI::sendNextSong(const GuildMember& guildMember) {
		std::unique_lock accessLock{ SongAPI::accessMutex };
		DiscordCoreClient::getSongAPI(guildMember.guildId)->sendNextSong();
		if (DiscordCoreClient::getSongAPI(guildMember.guildId)->playlist.currentSong.songId == "") {
			DiscordCoreClient::getSongAPI(guildMember.guildId)->sendNextSong();
		}
		DiscordCoreClient::getSongAPI(guildMember.guildId)->sendNextSongFinal(guildMember);
		return true;
	}

	void SongAPI::cancelCurrentSong() {
		if (taskThread) {
			taskThread->request_stop();
			if (taskThread->joinable()) {
				taskThread->detach();
			}
		}
		audioDataBuffer.clearContents();
		StopWatch stopWatch{ 10000ms };
		while (DiscordCoreClient::getSoundCloudAPI(guildId)->areWeWorking() || DiscordCoreClient::getYouTubeAPI(guildId)->areWeWorking()) {
			if (stopWatch.hasTimePassed()) {
				break;
			}
			std::this_thread::sleep_for(1ms);
		}
	}

	SongAPI::~SongAPI() noexcept {
		if (taskThread) {
			taskThread->request_stop();
			if (taskThread->joinable()) {
				taskThread->detach();
			}
		}
	}

	std::mutex SongAPI::accessMutex{};
};