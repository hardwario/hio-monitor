# libFuzzer targets of libs/mcp (ADR-0152; linux-fuzz preset only, HWIO_BUILD_FUZZERS=ON).
# Seed corpora live in corpus/<target>/ (small, text). CI runs each target for 60 s:
#   build/linux-fuzz/libs/mcp/fuzz/http_parser_fuzz -max_total_time=60 libs/mcp/fuzz/corpus/http_parser_fuzz

hwio_add_fuzzer(http_parser_fuzz http_parser_fuzz.cpp)
hwio_apply_target_defaults(http_parser_fuzz)
target_link_libraries(http_parser_fuzz PRIVATE hwio::mcp)

hwio_add_fuzzer(json_rpc_fuzz json_rpc_fuzz.cpp)
hwio_apply_target_defaults(json_rpc_fuzz)
target_link_libraries(json_rpc_fuzz PRIVATE hwio::mcp)
