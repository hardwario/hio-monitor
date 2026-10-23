# libFuzzer target of libs/firmware (plan 0021; ADR-0152: linux-fuzz preset only,
# HIO_BUILD_FUZZERS=ON). Seed corpus in corpus/catalog_page_fuzz/ (small, text). The CI fuzz job runs
# it for 10 minutes (plan 0021 § Tests and verification):
#   build/linux-fuzz/libs/firmware/fuzz/catalog_page_fuzz -max_total_time=600 \
#       libs/firmware/fuzz/corpus/catalog_page_fuzz

hio_add_fuzzer(catalog_page_fuzz catalog_page_fuzz.cpp)
hio_apply_target_defaults(catalog_page_fuzz)
target_link_libraries(catalog_page_fuzz PRIVATE hio::firmware)
