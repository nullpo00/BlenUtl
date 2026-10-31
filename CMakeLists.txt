cmake_minimum_required(VERSION 4.3.2)
project(BlenUtl CXX)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# MSVC以外を弾く
if(NOT MSVC)
    message(FATAL_ERROR "Please build this project using MSVC.")
endif()

add_library(BlenUtl MODULE 
	./src/main.cpp
	./src/resource.h
	./src/BlenUtl.rc
)

target_compile_features(${PROJECT_NAME} PRIVATE cxx_std_23)
set_target_properties(${PROJECT_NAME} PROPERTIES SUFFIX ".aux2")

target_include_directories(${PROJECT_NAME} PRIVATE 
	./include
	./src
)

if(NOT LIB_DOWNLOAD)
	find_package(nlohmann_json CONFIG REQUIRED)
	find_package(eigen3 CONFIG REQUIRED)
	
	target_link_libraries(${PROJECT_NAME} PRIVATE 
		nlohmann_json::nlohmann_json
		Eigen3::Eigen
	)
endif()

# 依存ライブラリをダウンロード
if(LIB_DOWNLOAD)
	include(FetchContent)
	FetchContent_Declare(
		nlohmann_json
		GIT_REPOSITORY https://github.com/nlohmann/json.git
		GIT_TAG v3.11.3
	)
	FetchContent_MakeAvailable(nlohmann_json)

	FetchContent_Declare(
		eigen
		GIT_REPOSITORY https://gitlab.com/libeigen/eigen.git
		GIT_TAG 5.0.0
	)
	FetchContent_MakeAvailable(eigen)

	target_include_directories(${PROJECT_NAME} PRIVATE 
		${nlohmann_json_SOURCE_DIR}/single_include
		${eigen_SOURCE_DIR}
	)
endif()

if(MSVC)
	target_compile_options(${PROJECT_NAME} PRIVATE 
		/utf-8
		/W4
	)
	set_property(TARGET ${PROJECT_NAME} PROPERTY MSVC_DEBUG_INFORMATION_FORMAT "")
	set_property(TARGET ${PROJECT_NAME} PROPERTY LINK_FLAGS /DEBUG:NONE)
endif()
