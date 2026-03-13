add_executable(bhplus-cli src/main.cpp ${BHPLUS_APP_RC})
target_link_libraries(bhplus-cli PRIVATE bhplus_core)
