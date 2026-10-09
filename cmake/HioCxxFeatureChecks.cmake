# C++23 standard library feature checks (ADR-0001, plan/0005-build-ci-release.md).
#
# Only features listed in the "Allowed C++23 library features" table of
# plan/0005-build-ci-release.md may be used. Each one is verified here so that a missing
# feature on one of the three standard libraries (libstdc++, libc++, MSVC STL) fails at
# configure time with a clear message instead of deep inside a compile.
include_guard(GLOBAL)

include(CheckCXXSourceCompiles)

function(_hio_detect_std_library out_var)
    set(CMAKE_REQUIRED_QUIET ON)
    check_cxx_source_compiles("
        #include <version>
        #if !defined(__GLIBCXX__)
        #error not libstdc++
        #endif
        int main() { return 0; }" HIO_STDLIB_IS_LIBSTDCXX)
    check_cxx_source_compiles("
        #include <version>
        #if !defined(_LIBCPP_VERSION)
        #error not libc++
        #endif
        int main() { return 0; }" HIO_STDLIB_IS_LIBCXX)
    check_cxx_source_compiles("
        #include <version>
        #if !defined(_MSVC_STL_VERSION)
        #error not MSVC STL
        #endif
        int main() { return 0; }" HIO_STDLIB_IS_MSVC_STL)
    if(HIO_STDLIB_IS_LIBSTDCXX)
        set(${out_var} "libstdc++" PARENT_SCOPE)
    elseif(HIO_STDLIB_IS_LIBCXX)
        set(${out_var} "libc++" PARENT_SCOPE)
    elseif(HIO_STDLIB_IS_MSVC_STL)
        set(${out_var} "MSVC STL" PARENT_SCOPE)
    else()
        set(${out_var} "unknown standard library" PARENT_SCOPE)
    endif()
endfunction()

# hio_require_cxx_feature(<result var> <human name> <source>)
function(hio_require_cxx_feature var name source)
    set(CMAKE_REQUIRED_QUIET ON)
    check_cxx_source_compiles("${source}" ${var})
    if(NOT ${var})
        _hio_detect_std_library(_stdlib)
        message(FATAL_ERROR
            "Required C++23 library feature '${name}' is not available with ${_stdlib} "
            "(${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}). See the allowed-features "
            "table in plan/0005-build-ci-release.md; a newer toolchain is needed.")
    endif()
endfunction()

hio_require_cxx_feature(HIO_HAS_EXPECTED "std::expected (__cpp_lib_expected >= 202211L)" "
    #include <expected>
    #include <version>
    #if !defined(__cpp_lib_expected) || __cpp_lib_expected < 202211L
    #error missing
    #endif
    int main() { std::expected<int, int> e{1}; return e.value() - 1; }")

hio_require_cxx_feature(HIO_HAS_FORMAT "std::format (__cpp_lib_format)" "
    #include <format>
    #include <version>
    #if !defined(__cpp_lib_format)
    #error missing
    #endif
    int main() { return std::format(\"{}\", 1).size() == 1 ? 0 : 1; }")

hio_require_cxx_feature(HIO_HAS_JTHREAD "std::jthread / std::stop_token (__cpp_lib_jthread)" "
    #include <stop_token>
    #include <thread>
    #include <version>
    #if !defined(__cpp_lib_jthread)
    #error missing
    #endif
    int main() { std::jthread t([](std::stop_token st) { (void)st.stop_requested(); }); return 0; }")

hio_require_cxx_feature(HIO_HAS_SPAN "std::span (__cpp_lib_span)" "
    #include <array>
    #include <span>
    #include <version>
    #if !defined(__cpp_lib_span)
    #error missing
    #endif
    int main() { std::array<int, 2> a{1, 2}; std::span<int> s{a}; return static_cast<int>(s.size()) - 2; }")

hio_require_cxx_feature(HIO_HAS_RANGES "ranges (__cpp_lib_ranges)" "
    #include <algorithm>
    #include <array>
    #include <ranges>
    #include <version>
    #if !defined(__cpp_lib_ranges)
    #error missing
    #endif
    int main() { std::array<int, 3> a{3, 1, 2}; std::ranges::sort(a); return a[0] - 1; }")

hio_require_cxx_feature(HIO_HAS_CV_ANY_STOP_TOKEN "std::condition_variable_any::wait with std::stop_token" "
    #include <condition_variable>
    #include <mutex>
    #include <stop_token>
    int main() {
        std::mutex m;
        std::condition_variable_any cv;
        std::stop_source src;
        src.request_stop();
        std::unique_lock lock(m);
        return cv.wait(lock, src.get_token(), [] { return false; }) ? 1 : 0;
    }")
