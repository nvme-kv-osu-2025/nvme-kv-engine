# Example applications

add_executable(simple_store simple_store.c)
target_link_libraries(simple_store nvme_kv_engine)

add_executable(simple_cache simple_cache.c)
target_link_libraries(simple_cache nvme_kv_engine)

add_executable(async_example async_example.c)
target_link_libraries(async_example nvme_kv_engine)

add_executable(check_kv_support check_kv_support.c)
target_include_directories(check_kv_support PRIVATE
    ${CMAKE_SOURCE_DIR}/lib/KVSSD/PDK/core/include
)
target_link_libraries(check_kv_support kvapi)
