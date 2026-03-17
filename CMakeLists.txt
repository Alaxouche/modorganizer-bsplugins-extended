cmake_minimum_required(VERSION 3.22)

if(DEFINED DEPENDENCIES_DIR AND EXISTS ${DEPENDENCIES_DIR}/modorganizer_super/cmake_common/mo2.cmake)
	include(${DEPENDENCIES_DIR}/modorganizer_super/cmake_common/mo2.cmake)
elseif(DEFINED DEPENDENCIES_DIR AND EXISTS ${DEPENDENCIES_DIR}/cmake_common/mo2.cmake)
	include(${DEPENDENCIES_DIR}/cmake_common/mo2.cmake)
else()
	include(${CMAKE_CURRENT_LIST_DIR}/cmake_common/mo2.cmake)
endif()

if(DEFINED DEPENDENCIES_DIR AND EXISTS ${DEPENDENCIES_DIR}/install/include/uibase)
	set(MO2_UIBASE_INCLUDE ${DEPENDENCIES_DIR}/install/include/uibase)
elseif(DEFINED DEPENDENCIES_DIR AND EXISTS ${DEPENDENCIES_DIR}/modorganizer-uibase-v2.5.2/include/uibase)
	set(MO2_UIBASE_INCLUDE ${DEPENDENCIES_DIR}/modorganizer-uibase-v2.5.2/include/uibase)
elseif(DEFINED DEPENDENCIES_DIR AND EXISTS ${DEPENDENCIES_DIR}/modorganizer-uibase/include/uibase)
	set(MO2_UIBASE_INCLUDE ${DEPENDENCIES_DIR}/modorganizer-uibase/include/uibase)
endif()

project(bsplugins)
set(project_type plugin)
set(enable_warnings OFF)

add_subdirectory(src)
