#pragma once

// OS accessibility preferences that Qt 6.8 does not expose (design system § 6).

namespace hio::gui {

// True when the OS asks for reduced motion: Windows "Show animations in Windows" off
// (SPI_GETCLIENTAREAANIMATION), macOS "Reduce motion" (com.apple.universalaccess reduceMotion), or
// the environment variable HIO_REDUCED_MOTION=1 on every OS (Linux desktops have no common API).
[[nodiscard]] bool reducedMotionRequested();

} // namespace hio::gui
