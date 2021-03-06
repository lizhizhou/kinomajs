#
#     Copyright (C) 2010-2015 Marvell International Ltd.
#     Copyright (C) 2002-2010 Kinoma, Inc.
#
#     Licensed under the Apache License, Version 2.0 (the "License");
#     you may not use this file except in compliance with the License.
#     You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
#     Unless required by applicable law or agreed to in writing, software
#     distributed under the License is distributed on an "AS IS" BASIS,
#     WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
#     See the License for the specific language governing permissions and
#     limitations under the License.
#
include(CMakeParseArguments)

set(BIN_DIR ${BUILD_BIN})

macro(local_find_library libname)
	set(options OPTIONAL EXIT)
	cmake_parse_arguments(LOCAL_LIBRARY "${options}" "${onevalueArgs}" "${multiValueArgs}" ${ARGN})

	string(TOUPPER ${libname} LIB_NAME)
	find_library(${LIB_NAME} ${libname} ${ARGN})
	if (${${LIB_NAME}} STREQUAL ${LIB_NAME}-NOTFOUND)
		if (NOT ${LOCAL_LIBRARY_EXIT})
			local_find_library(${libname} EXIT true NO_CMAKE_FIND_ROOT_PATH)
		else ()
			message(FATAL_ERROR ": ${libname} not found")
		endif ()
	else ()
		if (APPLE)
			get_filename_component(EXTENSION ${${LIB_NAME}} EXT)
			if (${EXTENSION} STREQUAL ".framework")
				mark_as_advanced(${LIB_NAME})
			endif ()
		endif ()
		list(APPEND EXTRA_LIBS ${${LIB_NAME}})
	endif()

endmacro()

macro(xscc sourcename parent)
	get_filename_component(NAME ${sourcename} NAME_WE)
	set(OUTDIR ${BUILD_TMP}/${parent})
	set(OUTDIR_TMP ${OUTDIR}/tmp)
	set(OUTPUT ${OUTDIR}/${NAME}.xsb ${OUTDIR}/${NAME}.xs.c ${OUTDIR}/${NAME}.xs.h)
	set_source_files_properties(${OUTPUT} PROPERTIES GENERATED TRUE)

	add_custom_target(
		${NAME}
		COMMAND ${CMAKE_COMMAND} -E make_directory ${OUTDIR_TMP}
		COMMAND ${XSC} ${sourcename} ${XSC_OPTIONS} -o ${OUTDIR_TMP}
		COMMAND ${CMAKE_COMMAND} -E copy_if_different ${OUTDIR_TMP}/${NAME}.xsb ${OUTDIR}
		COMMAND ${CMAKE_COMMAND} -E copy_if_different ${OUTDIR_TMP}/${NAME}.xs.c ${OUTDIR}
		COMMAND ${CMAKE_COMMAND} -E copy_if_different ${OUTDIR_TMP}/${NAME}.xs.h ${OUTDIR}
		COMMAND ${CMAKE_COMMAND} -E remove_directory ${OUTDIR_TMP}
		DEPENDS ${sourcename}
		)
	list(APPEND sources ${OUTDIR}/${NAME}.xs.c)
endmacro()

macro(copy_file)
	set(oneValueArgs SOURCE DESTINATION)
	set(multiValueArgs DEPENDS)
	cmake_parse_arguments(LOCAL_VAR "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})
	get_filename_component(LOCAL_NAME ${LOCAL_VAR_DESTINATION} NAME)
	get_filename_component(LOCAL_DIRECTORY ${BIN_DIR}/${LOCAL_VAR_DESTINATION} PATH)
	string(REGEX REPLACE "[-./]" "_" TARGET ${LOCAL_VAR_DESTINATION})
	add_custom_command(
		OUTPUT ${BIN_DIR}/${LOCAL_VAR_DESTINATION}
		COMMAND ${CMAKE_COMMAND} -E make_directory ${LOCAL_DIRECTORY}
		COMMAND ${CMAKE_COMMAND} -E copy ${LOCAL_VAR_SOURCE} ${LOCAL_DIRECTORY}/${LOCAL_NAME}
		)
	add_custom_target("${TARGET}" DEPENDS ${BIN_DIR}/${LOCAL_VAR_DESTINATION} ${LOCAL_VAR_DEPENDS})
	list(APPEND PROJECT_DEPENDS ${TARGET})
endmacro()

macro(js2jsb)
	set(oneValueArgs SOURCE DESTINATION)
	cmake_parse_arguments(LOCAL_VAR "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})
	get_filename_component(LOCAL_NAME ${LOCAL_VAR_DESTINATION} NAME_WE)
	get_filename_component(LOCAL_DIRECTORY ${BIN_DIR}/${LOCAL_VAR_DESTINATION} PATH)
	string(REGEX REPLACE "[-./]" "_" TARGET ${LOCAL_VAR_DESTINATION})
	add_custom_command(
		OUTPUT ${BIN_DIR}/${LOCAL_VAR_DESTINATION}
		COMMAND ${CMAKE_COMMAND} -E make_directory ${LOCAL_DIRECTORY}
		COMMAND ${XSC} ${XS_DEBUG} -b ${LOCAL_VAR_SOURCE} -o ${LOCAL_DIRECTORY}
		DEPENDS ${LOCAL_VAR_SOURCE}
		)
	add_custom_target(${TARGET} DEPENDS ${BIN_DIR}/${LOCAL_VAR_DESTINATION})
	list(APPEND PROJECT_DEPENDS ${TARGET})
endmacro()

macro(xml2jsb)
	set(oneValueArgs SOURCE DESTINATION)
	cmake_parse_arguments(LOCAL_VAR "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})
	get_filename_component(LOCAL_NAME ${LOCAL_VAR_DESTINATION} NAME_WE)
	get_filename_component(LOCAL_DIRECTORY ${BUILD_TMP}/${LOCAL_VAR_DESTINATION} PATH)
	set(LOCAL_TEMP_NAME ${LOCAL_DIRECTORY}/${LOCAL_NAME}.js)
	add_custom_command(
		OUTPUT ${LOCAL_TEMP_NAME}
		COMMAND ${CMAKE_COMMAND} -E make_directory ${LOCAL_DIRECTORY}
		COMMAND ${KPR2JS} ${LOCAL_VAR_SOURCE} -o ${LOCAL_DIRECTORY}
		DEPENDS ${LOCAL_VAR_SOURCE}
		)
	js2jsb(SOURCE ${LOCAL_TEMP_NAME} DESTINATION ${LOCAL_VAR_DESTINATION})
endmacro()

separate_arguments(KPRCONFIG_OPTIONS)
# remove quotes around kprconfig options as they're not needed for cmake
foreach(OPTION ${KPRCONFIG_OPTIONS})
	string(REGEX REPLACE "[\"']" "" NEW_OPTION ${OPTION})
	list(REMOVE_ITEM KPRCONFIG_OPTIONS ${OPTION})
	list(APPEND KPRCONFIG_OPTIONS ${NEW_OPTION})
	set(NEW_OPTION "")
endforeach()
add_custom_command(
	OUTPUT ${BUILD_TMP}/FskManifest.xs
	COMMAND ${KPRCONFIG} ${KPRCONFIG_OPTIONS}
	COMMAND ${CMAKE_COMMAND} ${BUILD_TMP}/obj
	DEPENDS ${MANIFEST}
	VERBATIM
	)
add_custom_target(kprconfig DEPENDS ${BUILD_TMP}/FskManifest.xs)

include_directories(${F_HOME}/kinoma/kpr/patches)
include_directories(${XS_HOME}/includes)
include_directories(${F_HOME}/libraries/QTReader)
include_directories(${F_HOME}/libraries/expat)
include_directories(${F_HOME}/libraries/zlib)
include_directories(${F_HOME}/core/base)
include_directories(${F_HOME}/core/graphics)
include_directories(${F_HOME}/core/managers)
include_directories(${F_HOME}/core/misc)
include_directories(${F_HOME}/core/network)
include_directories(${F_HOME}/core/scripting)
include_directories(${F_HOME}/core/ui)
include_directories(${BUILD_TMP})

include(${CONFIG_BASEDIR}/${TARGET_PLATFORM}/CMakeLists.txt)
