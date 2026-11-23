cmake_minimum_required(VERSION 3.0.0)
project(shogi C CXX)

# Automatically create moc files
set(CMAKE_AUTOMOC ON)

find_package(Qt5Core REQUIRED)
find_package(Qt5Qml REQUIRED)
find_package(Qt5Quick REQUIRED)
find_package(Qt5QuickControls2 REQUIRED)

execute_process(
    COMMAND dpkg-architecture -qDEB_HOST_MULTIARCH
    OUTPUT_VARIABLE ARCH_TRIPLET
    OUTPUT_STRIP_TRAILING_WHITESPACE
)
set(QT_IMPORTS_DIR "lib/${ARCH_TRIPLET}")

set(PROJECT_NAME "shogi")
set(FULL_PROJECT_NAME "shogi.ow")
set(DATA_DIR /)
set(DESKTOP_FILE_NAME ${PROJECT_NAME}.desktop)



configure_file(manifest.json.in ${CMAKE_CURRENT_BINARY_DIR}/manifest.json)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/manifest.json DESTINATION ${CMAKE_INSTALL_PREFIX})
install(FILES ${PROJECT_NAME}.apparmor DESTINATION ${DATA_DIR})
install(FILES assets/logo.svg DESTINATION assets)
install(FILES assets/sounds/clack.wav assets/sounds/check.wav assets/sounds/end.wav
        DESTINATION assets/sounds)

# USIエンジン (engines/ に <name>_<arch> バイナリと suisho5_eval/nn.bin を同梱)
file(GLOB ENGINE_BINS RELATIVE ${CMAKE_CURRENT_SOURCE_DIR}/engines
     ${CMAKE_CURRENT_SOURCE_DIR}/engines/yaneuraou*
     ${CMAKE_CURRENT_SOURCE_DIR}/engines/suisho5*
     ${CMAKE_CURRENT_SOURCE_DIR}/engines/tsuyou11_*)
list(FILTER ENGINE_BINS EXCLUDE REGEX "^[^/]+/$") # ディレクトリを除外
foreach(eng ${ENGINE_BINS})
    if(NOT IS_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/engines/${eng})
        install(PROGRAMS engines/${eng} DESTINATION engines)
    endif()
endforeach()
install(FILES engines/suisho5_eval/nn.bin DESTINATION engines/suisho5_eval)
install(FILES engines/tsuyou11_eval/nn.bin engines/tsuyou11_eval/progress.bin
        engines/tsuyou11_eval/eval_options.txt engines/tsuyou11_eval/LICENSE.tsuyou11.txt
        DESTINATION engines/tsuyou11_eval)

qt5_add_resources(QT_RESOURCES qml/qml.qrc)
qt5_add_resources(QT_RESOURCES assets/assets.qrc)
add_executable(${PROJECT_NAME}
    main.cpp
    src/game.cpp
    src/soundfx.cpp
    src/kifu/kifu.cpp
    src/engine/usiengine.cpp
    src/shogi/board.cpp
    ${QT_RESOURCES})
set_target_properties(${PROJECT_NAME} PROPERTIES LINK_FLAGS_RELEASE -s)
target_link_libraries(${PROJECT_NAME} Qt5::Gui Qt5::Core Qt5::Qml Qt5::Quick Qt5::QuickControls2)
install(TARGETS ${PROJECT_NAME} RUNTIME DESTINATION ${CMAKE_INSTALL_PREFIX})

# Translations
file(GLOB_RECURSE I18N_SRC_FILES RELATIVE ${CMAKE_CURRENT_SOURCE_DIR}/po qml/*.qml qml/*.js)
list(APPEND I18N_SRC_FILES ${DESKTOP_FILE_NAME}.in.h)

find_program(INTLTOOL_MERGE intltool-merge)
if(NOT INTLTOOL_MERGE)
    message(FATAL_ERROR "Could not find intltool-merge, please install the intltool package")
endif()
find_program(INTLTOOL_EXTRACT intltool-extract)
if(NOT INTLTOOL_EXTRACT)
    message(FATAL_ERROR "Could not find intltool-extract, please install the intltool package")
endif()

add_custom_target(${DESKTOP_FILE_NAME} ALL
    COMMENT "Merging translations into ${DESKTOP_FILE_NAME}..."
    COMMAND LC_ALL=C ${INTLTOOL_MERGE} -d -u ${CMAKE_SOURCE_DIR}/po ${CMAKE_SOURCE_DIR}/${DESKTOP_FILE_NAME}.in ${DESKTOP_FILE_NAME}
    COMMAND sed -i 's/${PROJECT_NAME}-//g' ${CMAKE_CURRENT_BINARY_DIR}/${DESKTOP_FILE_NAME}
)

install(FILES ${CMAKE_CURRENT_BINARY_DIR}/${DESKTOP_FILE_NAME} DESTINATION ${DATA_DIR})

add_subdirectory(po)

# Make source files visible in qtcreator
file(GLOB_RECURSE PROJECT_SRC_FILES
    RELATIVE ${CMAKE_CURRENT_SOURCE_DIR}
    qml/*.qml
    qml/*.js
    src/*
    *.json
    *.json.in
    *.apparmor
    *.desktop.in
)

add_custom_target(${PROJECT_NAME}_FILES ALL SOURCES ${PROJECT_SRC_FILES})

