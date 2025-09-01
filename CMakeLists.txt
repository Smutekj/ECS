cmake_minimum_required(VERSION 3.15)

project(Fun)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(BUILD_TESTS)

    include(FetchContent)
    FetchContent_Declare(
    googletest
    URL https://github.com/google/googletest/archive/03597a01ee50ed33e9dfd640b249b4be3799d395.zip
    )
    # For Windows: Prevent overriding the parent project's compiler/linker settings
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)
endif()

add_library(comps STATIC src/EntityWorld.cpp src/Archetype.cpp src/Component.cpp)
target_include_directories(comps
    PUBLIC 
    src
)

if(BUILD_TESTS)
    enable_testing()

    add_executable(ecs_test src/tests/main.cpp src/tests/test.cc)
    target_link_libraries(ecs_test PRIVATE comps GTest::gtest_main)

    
    target_compile_options(comps PRIVATE 
    # -fsanitize=address,undefined
    # -fno-omit-frame-pointer
    # -static-libasan
    -g
    -O1
    )
    add_test(TEST_STUFF ecs_test COMMAND ecs_test)
endif()

# add_executable(ecs_test src/speedTest.cpp)

# target_link_options(comps PRIVATE -fsanitize=address,undefined -static-libasan)

# target_compile_definitions(foobar PRIVATE
#     $<$<CONFIG:Debug>:
#         -g
#         -O1
#     >

# )

