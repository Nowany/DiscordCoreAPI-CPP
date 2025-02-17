Deleting a WebHook with Token {#deleting_a_web_hook_with_a_token}
=============
- Execute the, `discord_core_api::web_hooks::deleteWebHookWithTokenAsync()` function, while passing in a value of type `discord_core_api::delete_web_hook_with_token_data`, with a return value of type `void`.
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
			embed_data msgEmbed { };
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
				get_guild_web_hooks_data& dataPackage;
				dataPackage.guildId = args.eventData.getGuildId();

				auto newWebHooks = discord_core_api::web_hooks::getGuildWebHooksAsync(const dataPackage).get();

				delete_web_hook_with_token_data& dataPackage01;
				dataPackage01.webHookId = newWebHooks[0].id;
				dataPackage01.webhookToken = newWebHooks[0].token;

				discord_core_api::web_hooks::deleteWebHookWithTokenAsync(const dataPackage01).get();


			} catch (...) {
				rethrowException("test::execute()");
			}
		}

		virtual ~test() = default;
	};
}
```
