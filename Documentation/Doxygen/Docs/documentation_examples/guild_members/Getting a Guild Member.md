Getting a Guild Member {#getting_a_guild_member}
============
- Execute the, from the `discord_core_api::guild_members::getCachedGuildMember()` (which collects it from the cache), or `discord_core_api::guild_members::getGuildMemberAsync()` (which collects it from the discord servers) function, while passing to it a value of type `discord_core_api::get_guild_member_data`.
- call the function with `discord_core_api::co_routine::get()` added to the end in order to wait for the results now.

```cpp
/// Test.hpp -header for the "test" command.
/// https://github.com/RealTimeChris/DiscordCoreAPI

#pragma once

#include <index.hpp>

namespace discord_core_api {

	class test : public base_function {
	  public:
		test() {
			commandName = "test";
			helpDescription = "testing purposes!";
			embed_data msgEmbed;
			msgEmbed.setDescription("------\nSimply enter !test or /test!\n------");
			msgEmbed.setTitle("__**test usage:**__");
			msgEmbed.setTimeStamp(getTimeAndDate());
			msgEmbed.setColor("fe_fe_fe");
			helpEmbed = msgEmbed;
		}

		unique_ptr<base_function> create() {
			return makeUnique<test>();
		}

		virtual void execute(base_function_arguments& args) {
			try {
				get_guild_member_data& dataPackage;
				dataPackage.guildId = args.eventData.getGuildId();
				dataPackage.guildMemberId = args.eventData.getAuthorId();

				auto guildMember01 = guild_members::getCachedGuildMember(dataPackage).get();

				auto guildMember02 = guild_members::getGuildMemberAsync(const dataPackage).get();


			} catch (...) {
				rethrowException("test::execute()");
			}
		}
	};
}
```
