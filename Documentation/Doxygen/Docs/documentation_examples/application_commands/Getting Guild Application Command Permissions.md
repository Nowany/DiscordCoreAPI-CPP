Getting Guild Application Command Permissions {#get_guild_command_permissions}
============
- Execute the `discord_core_api::application_commands::getGuildApplicationCommandPermissionsAsync()` function, while passing in an argument of type `discord_core_api::get_guild_application_command_permissions_data`, with a return value of type `auto` or `jsonifier::vector<guild_application_command_permission_data>`.
- call the function with `discord_core_api::co_routine::get()` added to the end in order to wait for the results now.

```cpp
/// Test.hpp -header for the "test" command.
/// https://github.com/RealTimeChris/DiscordCoreAPI

#pragma once

#include "index.hpp"

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
			input_events::deleteInputEventResponseAsync(const args.eventData).get();

			get_guild_application_command_permissions_data& dataPackage;
			dataPackage.guildId = args.eventData.getGuildId();

			auto returnVector = application_commands::getGuildApplicationCommandPermissionsAsync(const dataPackage).get();

			for (const auto& value: returnVector) {
				std::cout << value.applicationId << std::endl;
			}
		}
	};
}
```