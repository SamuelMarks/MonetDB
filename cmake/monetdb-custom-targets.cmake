#[[
# SPDX-License-Identifier: MPL-2.0
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0.  If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.
#
# Copyright 2024 MonetDB Foundation;
# Copyright August 2008 - 2023 MonetDB B.V.;
# Copyright 1997 - July 2008 CWI.
#]]

if(WIN32)
  set (MONETDB_MTEST_SCRIPT "monetdb_mtest.bat")
else()
  set (MONETDB_MTEST_SCRIPT "monetdb_mtest.sh")
endif()

if (TESTING)
  add_custom_target(mtest
    COMMAND
    ${CMAKE_INSTALL_FULL_BINDIR}/${MONETDB_MTEST_SCRIPT}
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
endif()

if (CREATEREPO_FOUND)
  add_custom_target(create-rpm-repo
    COMMAND
    ${CREATEREPO_EXECUTABLE} ${CMAKE_BINARY_DIR}
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
else()
  add_custom_target(create-rpm-repo
    COMMAND
    ${CMAKE_COMMAND} -E echo 'Target not available because \"createrepo\" was not found.')
endif()

if(RPMBUILD_FOUND)
  add_custom_target(create-rpm-packages
    COMMAND
    ${CMAKE_CPACK_COMMAND} -G RPM
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

  add_custom_target(create-source-rpm-packages
    COMMAND
    ${CMAKE_CPACK_COMMAND} --config CPackSourceConfig.cmake -G RPM
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
else()
  add_custom_target(create-rpm-packages
    COMMAND
    ${CMAKE_COMMAND} -E echo 'Target not available because \"rpmbuild\" was not found.')

  add_custom_target(create-source-rpm-packages
    COMMAND
    ${CMAKE_COMMAND} -E echo 'Target not available because \"rpmbuild\" was not found.')
endif()

add_custom_target(create-rpm-distro
  COMMAND
  ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/distro"
  COMMAND
  ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/distro/repodata"
  COMMAND
  ${CMAKE_COMMAND} -E copy "${CMAKE_BINARY_DIR}/*rpm" "${CMAKE_BINARY_DIR}/distro/"
  COMMAND
  ${CMAKE_COMMAND} -E copy "${CMAKE_BINARY_DIR}/_CPack_Packages/Linux-Source/RPM/SPECS/monetdb.spec" "${CMAKE_BINARY_DIR}/distro/"
  COMMAND
  ${CMAKE_COMMAND} -E copy_directory "${CMAKE_BINARY_DIR}/repodata" "${CMAKE_BINARY_DIR}/distro/repodata/"
  WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

if(DPKGBUILDPACKAGE_FOUND)
  add_custom_target(create-deb-packages
    COMMAND
    ${CMAKE_CPACK_COMMAND} -G DEB
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

  add_custom_target(create-source-deb-packages
    COMMAND
    ${CMAKE_CPACK_COMMAND} --config CPackSourceConfig.cmake -G DEB
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
else()
  add_custom_target(create-deb-packages
    COMMAND
    ${CMAKE_COMMAND} -E echo 'Target not available because \"dpkg-buildpackage\" was not found.')

  add_custom_target(create-source-deb-packages
    COMMAND
    ${CMAKE_COMMAND} -E echo 'Target not available because \"dpkg-buildpackage\" was not found.')
endif()

if(CANDLE_FOUND)
  add_custom_target(create-wix-packages
    COMMAND
    ${CMAKE_CPACK_COMMAND} -G WIX -C Release
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
else()
  add_custom_target(create-wix-packages
    COMMAND
    ${CMAKE_COMMAND} -E echo 'Target not available because \"candle\" was not found.')
endif()

if(CTAGS_PATH)
add_custom_target(tags
  COMMAND ${CTAGS_PATH} -R --kinds-C=+pLl --fields=+iaS --exclude=*.js --exclude=build --exclude=install
        ${CMAKE_CURRENT_BINARY_DIR} ${CMAKE_CURRENT_SOURCE_DIR}
        COMMAND ln -sf ${CMAKE_CURRENT_BINARY_DIR}/tags ${CMAKE_CURRENT_SOURCE_DIR}
  WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
endif()

if(CSCOPE_PATH)
add_custom_target(cscope
  COMMAND ${CSCOPE_PATH} -bcqR -s${CMAKE_CURRENT_SOURCE_DIR}
        COMMAND ln -sf ${CMAKE_CURRENT_BINARY_DIR}/cscope.out ${CMAKE_CURRENT_SOURCE_DIR}/
        COMMAND ln -sf ${CMAKE_CURRENT_BINARY_DIR}/cscope.in.out ${CMAKE_CURRENT_SOURCE_DIR}/
        COMMAND ln -sf ${CMAKE_CURRENT_BINARY_DIR}/cscope.po.out ${CMAKE_CURRENT_SOURCE_DIR}/
  WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
endif()
