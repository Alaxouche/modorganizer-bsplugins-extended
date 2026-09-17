cmake_minimum_required(VERSION 3.22)

if(DEFINED DEPENDENCIES_DIR AND EXISTS ${DEPENDENCIES_DIR}/modorganizer_super/cmake_common/mo2.cmake)
	include(${DEPENDENCIES_DIR}/modorganizer_super/cmake_common/mo2.cmake)
elseif(DEFINED DEPENDENCIES_DIR AND EXISTS ${DEPENDENCIES_DIR}/cmake_common/mo2.cmake)
	include(${DEPENDENCIES_DIR}/cmake_common/mo2.cmake)
else()
	include(${CMAKE_CURRENT_LIST_DIR}/cmake_common/mo2.cmake)
endif()

# An explicit -DMO2_UIBASE_INCLUDE wins over auto-detection; the MO2 2.5.3
# variant must build against that beta's uibase headers, not the shared
# 2.5.2 install (the plugin ABI differs between the two).
if(NOT DEFINED MO2_UIBASE_INCLUDE OR MO2_UIBASE_INCLUDE STREQUAL "")
	if(DEFINED DEPENDENCIES_DIR AND EXISTS ${DEPENDENCIES_DIR}/install/include/uibase)
		set(MO2_UIBASE_INCLUDE ${DEPENDENCIES_DIR}/install/include/uibase)
	elseif(DEFINED DEPENDENCIES_DIR AND EXISTS ${DEPENDENCIES_DIR}/modorganizer-uibase-v2.5.2/include/uibase)
		set(MO2_UIBASE_INCLUDE ${DEPENDENCIES_DIR}/modorganizer-uibase-v2.5.2/include/uibase)
	elseif(DEFINED DEPENDENCIES_DIR AND EXISTS ${DEPENDENCIES_DIR}/modorganizer-uibase/include/uibase)
		set(MO2_UIBASE_INCLUDE ${DEPENDENCIES_DIR}/modorganizer-uibase/include/uibase)
	endif()
endif()

project(bsplugins)
set(project_type plugin)
set(enable_warnings OFF)

add_subdirectory(src)
