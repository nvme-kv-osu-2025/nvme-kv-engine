# Example applications

add_executable(simple_store simple_store.c)
target_link_libraries(simple_store nvme_kv_engine)

add_executable(simple_cache simple_cache.c)
target_link_libraries(simple_cache nvme_kv_engine)

add_executable(async_example async_example.c)
target_link_libraries(async_example nvme_kv_engine)
