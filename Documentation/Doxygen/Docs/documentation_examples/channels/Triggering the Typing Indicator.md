Triggering the Typing Indicator {#triggering_the_typing_indicator}
============
- Execute the, `discord_core_api::channels::triggerTypingIndicatorAsync()` function, while passing in a data structure of type `discord_core_api::trigger_typing_indicator_data` with a return value of `void`.
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
				trigger_typing_indicator_data dataPackage;
				dataPackage.channelId = args.eventData.getChannelId();

				discord_core_api::channels::triggerTypingIndicatorAsync(const dataPackage).get();

			} catch (...) {
				rethrowException("test::execute()");
			}
		}
	};
}
```
