# Each test is a standalone Qt Test executable linked against wakkaqt_core
# (the pure/testable slice of the app — see the top-level CMakeLists.txt
# comment on that target) plus Qt6::Test for QTEST_MAIN/QVERIFY/QCOMPARE.
function(wakkaqt_add_test name)
    add_executable(${name} ${ARGN})
    target_link_libraries(${name} PRIVATE wakkaqt_core Qt6::Test)
    add_test(NAME ${name} COMMAND ${name})
endfunction()

wakkaqt_add_test(test_atomicfilecommit test_atomicfilecommit.cpp)
wakkaqt_add_test(test_wavparsing       test_wavparsing.cpp)
wakkaqt_add_test(test_sessionrepository test_sessionrepository.cpp)

# VocalEnhancer lives in wakkaqt_dsp, not wakkaqt_core — separate from the
# helper above since it needs a different library (and the FFTW link that
# comes with it).
add_executable(test_vocalenhancer test_vocalenhancer.cpp)
target_link_libraries(test_vocalenhancer PRIVATE wakkaqt_dsp Qt6::Test)
add_test(NAME test_vocalenhancer COMMAND test_vocalenhancer)
