cmake_minimum_required(VERSION 3.27)

include(FetchContent)

FetchContent_Declare(
	ryml
	EXCLUDE_FROM_ALL
	GIT_REPOSITORY https://github.com/biojppm/rapidyaml.git
	GIT_TAG master
)

FetchContent_MakeAvailable(ryml)

include(${CMAKE_CURRENT_SOURCE_DIR}/ParseTES.cmake)

add_library(bsplugins SHARED)

set_property(TARGET bsplugins PROPERTY CXX_STANDARD 20)
target_sources(bsplugins PRIVATE ${TES_INCLUDE_FILES})
target_include_directories(
	bsplugins
	PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
	PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/include
)
target_link_libraries(bsplugins PRIVATE ryml)

if(MSVC)
	target_compile_options(
		bsplugins
		PRIVATE
			/bigobj # increase number of sections in .obj file
			/W4     # level 4 warnings
			/WX     # treat all compiler warnings as errors
			/wd4201 # nonstandard extension used: nameless struct/union
			/wd4458 # declaration of 'identifier' hides class member
			/Zi     # debug info; the install step ships the PDB for crash dumps
	)
	target_link_options(bsplugins PRIVATE /DEBUG)
endif()

mo2_configure_plugin(
	bsplugins
	WARNINGS OFF
	PRIVATE_DEPENDS
		bsatk lootcli uibase
		boost fmt zlib
		Qt::WebEngineWidgets
)
mo2_install_target(bsplugins)

# Community translations: every bsplugins_<lang>.ts next to the sources is
# compiled and shipped; the _en one is already handled by mo2_add_translations.
file(GLOB extra_ts_files ${CMAKE_CURRENT_SOURCE_DIR}/bsplugins_*.ts)
list(FILTER extra_ts_files EXCLUDE REGEX "_en\\.ts$")
set(extra_qm_files "")
foreach(ts_file ${extra_ts_files})
	get_filename_component(ts_name ${ts_file} NAME_WE)
	set(qm_file ${CMAKE_CURRENT_BINARY_DIR}/${ts_name}.qm)
	add_custom_command(OUTPUT ${qm_file}
		COMMAND ${QT_ROOT}/bin/lrelease ${ts_file} -qm ${qm_file}
		DEPENDS ${ts_file}
		VERBATIM)
	list(APPEND extra_qm_files ${qm_file})
	install(FILES ${qm_file} DESTINATION bin/translations)
endforeach()
if(extra_qm_files)
	add_custom_target(bsplugins_extra_translations DEPENDS ${extra_qm_files})
	set_target_properties(bsplugins_extra_translations PROPERTIES FOLDER autogen)
	add_dependencies(bsplugins bsplugins_extra_translations)
endif()
