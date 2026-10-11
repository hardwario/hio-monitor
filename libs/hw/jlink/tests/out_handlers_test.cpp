// Out-handler slots (review fix 6, 2026-10-09): handlers called by "vendor" threads while the slot
// is torn down never touch a destroyed queue. Run under TSan and ASan by the sanitizer presets.

#include "hio/core/error.h"
#include "jlink_abi.h"
#include "out_handlers.h"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {

using hio::jlink::LibraryMessage;
using hio::jlink::OutHandlerSlot;

TEST(OutHandlers, MessagesReachTheSlotQueueTruncated) {
    auto slot = OutHandlerSlot::acquire();
    ASSERT_TRUE(slot);
    (*slot)->errorHandler()("boom");
    const std::string longText(OutHandlerSlot::maxMessageBytes + 100, 'x');
    (*slot)->logHandler()(longText.c_str());
    (*slot)->warningHandler()(nullptr); // ignored
    const LibraryMessage first =
        (*slot)->queue().pop({}, std::chrono::milliseconds(0)).value_or(LibraryMessage{});
    EXPECT_EQ(first.kind, LibraryMessage::Kind::Error);
    EXPECT_EQ(first.text, "boom");
    const LibraryMessage second =
        (*slot)->queue().pop({}, std::chrono::milliseconds(0)).value_or(LibraryMessage{});
    EXPECT_EQ(second.text.size(), OutHandlerSlot::maxMessageBytes);
    EXPECT_FALSE((*slot)->queue().pop({}, std::chrono::milliseconds(0)));
}

TEST(OutHandlers, AtMostMaxSlots) {
    std::vector<std::unique_ptr<OutHandlerSlot>> slots;
    for (std::size_t i = 0; i < OutHandlerSlot::maxSlots; ++i) {
        auto slot = OutHandlerSlot::acquire();
        ASSERT_TRUE(slot);
        slots.push_back(std::move(*slot));
    }
    const auto extra = OutHandlerSlot::acquire();
    ASSERT_FALSE(extra);
    EXPECT_EQ(extra.error().code, hio::ErrorCode::InternalSessionLimit);
    slots.pop_back();
    EXPECT_TRUE(OutHandlerSlot::acquire());
}

// Several threads call the handlers of a slot without pause while the slot is destroyed and new
// slots are created on the same index: no use-after-free (ASan) and no data race (TSan).
TEST(OutHandlers, TeardownWhileVendorThreadsAreInsideHandlers) {
    for (int round = 0; round < 50; ++round) {
        auto slot = OutHandlerSlot::acquire();
        ASSERT_TRUE(slot);
        const hio::jlink::abi::LogHandler log = (*slot)->logHandler();
        const hio::jlink::abi::LogHandler error = (*slot)->errorHandler();
        std::atomic<bool> running{true};
        std::vector<std::jthread> vendor;
        vendor.reserve(4);
        for (int t = 0; t < 4; ++t) {
            vendor.emplace_back([&running, log, error] {
                while (running.load()) {
                    log("vendor log line");
                    error("vendor error");
                }
            });
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        slot->reset(); // the handlers keep running
        auto reused = OutHandlerSlot::acquire();
        ASSERT_TRUE(reused);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        running.store(false);
        vendor.clear();
    }
}

} // namespace
