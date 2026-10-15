cmake_minimum_required(VERSION 3.21)
project(LBP1120Win64 LANGUAGES CXX)

if(NOT WIN32)
  message(FATAL_ERROR "This project currently targets Windows.")
endif()

include(FetchContent)
set(LIBCAPT_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(LIBCAPT_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(LIBCAPT_INSTALL OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
  libcapt
  GIT_REPOSITORY https://github.com/darkvision77/libcapt.git
  GIT_TAG master
  GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(libcapt)

add_executable(lbp1120-usb-probe src/usb_probe.cpp)
target_compile_features(lbp1120-usb-probe PRIVATE cxx_std_17)
target_compile_definitions(lbp1120-usb-probe PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_libraries(lbp1120-usb-probe PRIVATE setupapi)

add_executable(lbp1120-submit-pbm src/submit_pbm.cpp)
target_compile_features(lbp1120-submit-pbm PRIVATE cxx_std_17)
target_compile_definitions(lbp1120-submit-pbm PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_libraries(lbp1120-submit-pbm PRIVATE winspool)

add_library(CAPTPBM SHARED src/capt_print_processor.cpp src/CAPTPBM.def)
target_compile_features(CAPTPBM PRIVATE cxx_std_17)
target_compile_definitions(CAPTPBM PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_libraries(CAPTPBM PRIVATE winspool)
set_target_properties(CAPTPBM PROPERTIES PREFIX "" OUTPUT_NAME "CAPTPBM")

add_executable(lbp1120-capt-print src/capt_print.cpp)
target_compile_features(lbp1120-capt-print PRIVATE cxx_std_23)
target_compile_definitions(lbp1120-capt-print PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_libraries(lbp1120-capt-print PRIVATE libcapt setupapi cfgmgr32)

if(MSVC)
  target_compile_options(lbp1120-usb-probe PRIVATE /W4 /permissive-)
  target_compile_options(lbp1120-capt-print PRIVATE /W4 /permissive-)
  target_compile_options(CAPTPBM PRIVATE /W4 /permissive-)
  target_compile_options(lbp1120-submit-pbm PRIVATE /W4 /permissive-)
endif()
