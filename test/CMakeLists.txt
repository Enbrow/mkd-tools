set(test_dictionaries "${CMAKE_CURRENT_BINARY_DIR}/dictionaries/$<CONFIG>")
set(test_python "${CMAKE_CURRENT_BINARY_DIR}/python/$<CONFIG>")

file(GLOB_RECURSE dictionary_sources CONFIGURE_DEPENDS "dictionary/*.py" "dictionary/articles/*" "dictionary/assets/*")
add_custom_command(
        OUTPUT "${test_dictionaries}/.built"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${test_python}/mkd"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${PROJECT_SOURCE_DIR}/python/mkd/__init__.py" "${test_python}/mkd/__init__.py"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:_mkd>" "${test_python}/mkd/$<TARGET_FILE_NAME:_mkd>"
        COMMAND ${CMAKE_COMMAND} -E env "PYTHONPATH=${test_python}"
        ${Python_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/dictionary/build.py" "${test_dictionaries}"
        COMMAND ${CMAKE_COMMAND} -E touch "${test_dictionaries}/.built"
        DEPENDS _mkd ${dictionary_sources} "${PROJECT_SOURCE_DIR}/python/mkd/__init__.py"
        VERBATIM
)
add_custom_target(mkd-test-dictionaries ALL DEPENDS "${test_dictionaries}/.built")

add_executable(mkd-tests
        support/test_dictionary.cpp
        navigation.cpp
        dictionary.cpp
        search.cpp
        resources.cpp
        creation.cpp
        content.cpp
        export.cpp
        tar_output.cpp
)

if (APPLE)
    target_sources(mkd-tests PRIVATE receipt.cpp)
endif ()

target_include_directories(mkd-tests PRIVATE "${PROJECT_SOURCE_DIR}/src" "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_definitions(mkd-tests PRIVATE
        MKD_TEST_DICTIONARIES="${test_dictionaries}"
        MKD_TEST_ASSETS="${CMAKE_CURRENT_SOURCE_DIR}/dictionary/assets"
)
target_compile_options(mkd-tests PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/utf-8>)
target_link_libraries(mkd-tests PRIVATE MKD::mkd GTest::gtest_main)
add_dependencies(mkd-tests mkd-test-dictionaries)

include(GoogleTest)
gtest_discover_tests(mkd-tests)

foreach (suite IN ITEMS reading creation)
    add_test(NAME "python.${suite}"
            COMMAND ${Python_EXECUTABLE} -m unittest discover -s "${CMAKE_CURRENT_SOURCE_DIR}/python"
            -p "test_${suite}.py" -v)
endforeach ()

set(python_tests python.reading python.creation)
if (TARGET mkd-cli)
    add_dependencies(mkd-tests mkd-cli)
    add_test(NAME python.cli
            COMMAND ${Python_EXECUTABLE} -m unittest discover -s "${CMAKE_CURRENT_SOURCE_DIR}/python"
            -p test_cli.py -v)
    list(APPEND python_tests python.cli)
    set_tests_properties(python.cli PROPERTIES ENVIRONMENT "MKD_TEST_CLI=$<TARGET_FILE:mkd-cli>")
endif ()

set_property(TEST ${python_tests} APPEND PROPERTY ENVIRONMENT
        "PYTHONPATH=${test_python}"
        "MKD_TEST_DICTIONARIES=${test_dictionaries}"
        "MKD_TEST_ASSETS=${CMAKE_CURRENT_SOURCE_DIR}/dictionary/assets"
)
