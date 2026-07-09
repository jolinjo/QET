# Copyright 2006 The QElectroTech Team
# This file is part of QElectroTech.
#
# QElectroTech is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 2 of the License, or
# (at your option) any later version.
#
# QElectroTech is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with QElectroTech. If not, see <http://www.gnu.org/licenses/>.

cmake_minimum_required(VERSION 3.5...4.2)

project(qelectrotech
  VERSION 2.0.49
  DESCRIPTION "QET is a CAD/CAE editor focusing on schematics drawing features."
  HOMEPAGE_URL "https://qelectrotech.org/"
  LANGUAGES CXX)

include(cmake/copyright_message.cmake)

set(QET_DIR ${PROJECT_SOURCE_DIR})
include(cmake/qet_compilation_vars.cmake)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 REQUIRED COMPONENTS ${QET_COMPONENTS})
qt_standard_project_setup()

# Add sub directories
option(PACKAGE_TESTS "Build the tests" NO)
if(PACKAGE_TESTS)
  message("Add sub directory tests")
  add_subdirectory(tests)
endif()

include(cmake/paths_compilation_installation.cmake)
include(cmake/start_options.cmake)
include(cmake/developer_options.cmake)
include(cmake/git_update_submodules.cmake)
include(cmake/git_last_commit_sha.cmake)
include(cmake/fetch_kdeaddons.cmake)
include(cmake/fetch_singleapplication.cmake)
include(cmake/fetch_pugixml.cmake)

set(CMAKE_INCLUDE_CURRENT_DIR ON)

set(CMAKE_AUTORCC ON)

set(CMAKE_AUTOUIC_SEARCH_PATHS ${QET_DIR}/sources/ui)

include(cmake/define_definitions.cmake)

qt_add_executable(
  ${PROJECT_NAME}
  ${QET_RES_FILES}
  ${QET_SRC_FILES}
  ${QM_FILES}
  ${QET_DIR}/qelectrotech.qrc
  )

# Scope the version define to qetversion.cpp only, so bumping the project version
# recompiles just that one file (+ relink) instead of the whole target.
# QET_RELEASE_BUILD=ON 時為 OTA 發行版:版號無 -dev 字尾。
option(QET_RELEASE_BUILD "OTA release build (clean version, no -dev suffix)" OFF)
set(QET_VERSION_DEFS QET_PROJECT_VERSION="${PROJECT_VERSION}")
if(QET_RELEASE_BUILD)
  list(APPEND QET_VERSION_DEFS QET_RELEASE_BUILD)
endif()
set_source_files_properties(${QET_DIR}/sources/qetversion.cpp PROPERTIES
  COMPILE_DEFINITIONS "${QET_VERSION_DEFS}")

if(APPLE)
  set_target_properties(${PROJECT_NAME} PROPERTIES
    MACOSX_BUNDLE TRUE
    MACOSX_BUNDLE_BUNDLE_NAME "QElectroTech"
    MACOSX_BUNDLE_GUI_IDENTIFIER "org.qelectrotech.qelectrotech"
    MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
    MACOSX_BUNDLE_ICON_FILE "qelectrotech.icns"
    # NSRequiresAquaSystemAppearance：固定淺色外觀，不跟隨系統深色模式
    MACOSX_BUNDLE_INFO_PLIST "${QET_DIR}/misc/MacOSXBundleInfo.plist.in")
  target_sources(${PROJECT_NAME} PRIVATE ${QET_DIR}/ico/mac_icon/qelectrotech.icns)
  set_source_files_properties(${QET_DIR}/ico/mac_icon/qelectrotech.icns
    PROPERTIES MACOSX_PACKAGE_LOCATION "Resources")
  # 元件庫與圖框複製進 bundle Resources（排除 submodule 的 .git）
  # 自用組建：.app 僅作外殼（圖示 + Resources），Qt 由系統 brew 提供
  # （@rpath 指向 /opt/homebrew）。不執行 macdeployqt，避免 bundle 內
  # 另塞一套 Qt 而與 brew Qt 雙重載入導致啟動即崩潰。cocoa/offscreen
  # 平台外掛都由 brew Qt 供給，毋須複製進 bundle。
  add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND rsync -a --delete --exclude=.git
            ${QET_DIR}/elements $<TARGET_BUNDLE_CONTENT_DIR:${PROJECT_NAME}>/Resources/
    COMMAND rsync -a --delete
            ${QET_DIR}/titleblocks $<TARGET_BUNDLE_CONTENT_DIR:${PROJECT_NAME}>/Resources/)
  # git-lfs 打包進 Resources/bin，供 PDM 出庫/入庫鎖定使用（免使用者安裝）。
  # PdmGitWorker 執行時把此目錄注入子行程 PATH。
  include(cmake/bundle_git_lfs.cmake)
  add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
            $<TARGET_BUNDLE_CONTENT_DIR:${PROJECT_NAME}>/Resources/bin
    COMMAND ${CMAKE_COMMAND} -E copy ${GITLFS_BINARY}
            $<TARGET_BUNDLE_CONTENT_DIR:${PROJECT_NAME}>/Resources/bin/git-lfs
    COMMAND chmod +x
            $<TARGET_BUNDLE_CONTENT_DIR:${PROJECT_NAME}>/Resources/bin/git-lfs)
  # 私有混合字型不進公開 repo/qrc（CI 編不了），改為執行時從
  # Resources/fonts 載入；本地有檔案才複製進 bundle。
  if(EXISTS ${QET_DIR}/fonts/YaHei.Consolas.1.11b.ttf)
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
      COMMAND mkdir -p $<TARGET_BUNDLE_CONTENT_DIR:${PROJECT_NAME}>/Resources/fonts
      COMMAND cp -f ${QET_DIR}/fonts/YaHei.Consolas.1.11b.ttf
              $<TARGET_BUNDLE_CONTENT_DIR:${PROJECT_NAME}>/Resources/fonts/)
  endif()
endif()

if(QMFILES_AS_RESOURCE)
  qt_add_translations(${PROJECT_NAME} TS_FILES ${TS_FILES} RESOURCE_PREFIX "/lang")
else()
  qt_create_translation(QM_FILES ${CMAKE_SOURCE_DIR} ${TS_FILES})
  set_source_files_properties(${TS_FILES} PROPERTIES OUTPUT_LOCATION "${QET_DIR}/lang")
  qt_add_translation(QM_FILES ${TS_FILES})
endif()

find_package(SQLite3 REQUIRED)

target_link_libraries(
  ${PROJECT_NAME}
  PUBLIC
  PRIVATE
  pugixml::pugixml
  SingleApplication::SingleApplication
  SQLite::SQLite3
  ${KF6_PRIVATE_LIBRARIES}
  ${QET_PRIVATE_LIBRARIES}
  )

target_include_directories(
  ${PROJECT_NAME}
  PRIVATE
  ${QET_DIR}/sources/titleblock
  ${QET_DIR}/sources/ui
  ${QET_DIR}/sources/qetgraphicsitem
  ${QET_DIR}/sources/qetgraphicsitem/ViewItem
  ${QET_DIR}/sources/qetgraphicsitem/ViewItem/ui
  ${QET_DIR}/sources/richtext
  ${QET_DIR}/sources/factory
  ${QET_DIR}/sources/properties
  ${QET_DIR}/sources/dvevent
  ${QET_DIR}/sources/editor
  ${QET_DIR}/sources/editor/esevent
  ${QET_DIR}/sources/editor/graphicspart
  ${QET_DIR}/sources/editor/ui
  ${QET_DIR}/sources/editor/UndoCommand
  ${QET_DIR}/sources/undocommand
  ${QET_DIR}/sources/diagramevent
  ${QET_DIR}/sources/ElementsCollection
  ${QET_DIR}/sources/ElementsCollection/ui
  ${QET_DIR}/sources/Pdm
  ${QET_DIR}/sources/autoNum
  ${QET_DIR}/sources/autoNum/ui
  ${QET_DIR}/sources/ui/configpage
  ${QET_DIR}/sources/SearchAndReplace
  ${QET_DIR}/sources/SearchAndReplace/ui
  ${QET_DIR}/sources/NameList
  ${QET_DIR}/sources/NameList/ui
  ${QET_DIR}/sources/utils
  ${QET_DIR}/sources/dataBase
  ${QET_DIR}/sources/dataBase/ui
  ${QET_DIR}/sources/factory/ui
  ${QET_DIR}/sources/print
  ${QET_DIR}/sources/svg
  )

install(TARGETS ${PROJECT_NAME} BUNDLE DESTINATION "." RUNTIME DESTINATION bin)
if (NOT MINGW)

install(DIRECTORY ico/breeze-icons/16x16 DESTINATION ${QET_ICONS_PATH})
install(DIRECTORY ico/breeze-icons/22x22 DESTINATION ${QET_ICONS_PATH})
install(DIRECTORY ico/breeze-icons/32x32 DESTINATION ${QET_ICONS_PATH})
install(DIRECTORY ico/breeze-icons/48x48 DESTINATION ${QET_ICONS_PATH})
install(DIRECTORY ico/breeze-icons/64x64 DESTINATION ${QET_ICONS_PATH})
install(DIRECTORY ico/breeze-icons/128x128 DESTINATION ${QET_ICONS_PATH})
install(DIRECTORY ico/breeze-icons/256x256 DESTINATION ${QET_ICONS_PATH})
install(DIRECTORY elements DESTINATION share/qelectrotech)
install(DIRECTORY examples DESTINATION share/qelectrotech)
install(DIRECTORY titleblocks DESTINATION share/qelectrotech)
install(FILES LICENSE ELEMENTS.LICENSE CREDIT README ChangeLog DESTINATION share/doc/qelectrotech)
install(FILES misc/org.qelectrotech.qelectrotech.desktop DESTINATION share/applications)
install(FILES misc/qelectrotech.xml DESTINATION share/mime/packages)
install(FILES misc/qelectrotech.appdata.xml DESTINATION ${QET_APPDATA_PATH})
if(NOT QMFILES_AS_RESOURCE)
  install(FILES ${QM_FILES} DESTINATION ${QET_LANG_PATH})
endif()
endif()
