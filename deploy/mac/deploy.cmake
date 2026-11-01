# SPDX-FileCopyrightText: (C) 2024 Chris Rizzitello <sithlord48@gmail.com>
# SPDX-License-Identifier: MIT

# HACK This is set when the files is included so its the real path
# calling CMAKE_CURRENT_LIST_DIR after include would return the wrong scope var
set(MY_DIR ${CMAKE_CURRENT_LIST_DIR})
set(OSX_BUNDLE ${BUILD_OSX_BUNDLE})

set(OS_STRING "macos-${BUILD_ARCHITECTURE}")

if (OSX_BUNDLE)
  # Ad-hoc signatures carry no Team ID, so the hardened runtime's library validation would refuse the bundled Qt
  set(_codesign_args "-codesign=-")
  if (APPLE_CODESIGN_DEV)
    set(_codesign_args "-hardened-runtime -timestamp \"-codesign=${APPLE_CODESIGN_DEV}\"")
  endif()
  # Homebrew ships each Qt module in its own keg, so macdeployqt cannot resolve the svg plugins' QtSvg;
  # it does search the bundle's Frameworks, so put QtSvg there first, pointed at the bundled Qt it will not rewrite
  find_path(_qtsvg_libdir QtSvg.framework PATHS /opt/homebrew/opt/qtsvg/lib /usr/local/opt/qtsvg/lib NO_DEFAULT_PATH)
  find_path(_qtbase_libdir QtCore.framework PATHS /opt/homebrew/opt/qtbase/lib /usr/local/opt/qtbase/lib NO_DEFAULT_PATH)
  if (_qtsvg_libdir AND _qtbase_libdir)
    set(_frameworks "\${CMAKE_INSTALL_PREFIX}/${CMAKE_PROJECT_PROPER_NAME}.app/Contents/Frameworks")
    set(_bundled "@executable_path/../Frameworks")
    install(CODE "file(COPY \"${_qtsvg_libdir}/QtSvg.framework\" DESTINATION \"${_frameworks}\" PATTERN Headers EXCLUDE)
      execute_process(COMMAND install_name_tool
        -id ${_bundled}/QtSvg.framework/Versions/A/QtSvg
        -change ${_qtbase_libdir}/QtGui.framework/Versions/A/QtGui ${_bundled}/QtGui.framework/Versions/A/QtGui
        -change ${_qtbase_libdir}/QtCore.framework/Versions/A/QtCore ${_bundled}/QtCore.framework/Versions/A/QtCore
        \"${_frameworks}/QtSvg.framework/Versions/A/QtSvg\"
        COMMAND_ERROR_IS_FATAL ANY)")
  endif()
  install(CODE "execute_process(COMMAND
    ${DEPLOYQT}
    \"\${CMAKE_INSTALL_PREFIX}/${CMAKE_PROJECT_PROPER_NAME}.app\"
    \"-executable=\${CMAKE_INSTALL_PREFIX}/${CMAKE_PROJECT_PROPER_NAME}.app/Contents/MacOS/${CMAKE_PROJECT_NAME}-core\"
    ${_codesign_args}
  )")
  set(CPACK_PACKAGE_ICON "${MY_DIR}/dmg-volume.icns")
  set(CPACK_DMG_BACKGROUND_IMAGE "${MY_DIR}/dmg-background.tiff")
  set(CPACK_DMG_DS_STORE_SETUP_SCRIPT "${MY_DIR}/generate_ds_store.applescript")
  set(CPACK_DMG_VOLUME_NAME "${CMAKE_PROJECT_PROPER_NAME}")
  set(CPACK_DMG_SLA_USE_RESOURCE_FILE_LICENSE ON)
  set(CPACK_GENERATOR "DragNDrop")
endif()
