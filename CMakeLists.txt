#**********************************************************************
# otpasswd -- One-time password manager and PAM module.
# Copyright (C) 2009-2013 by Tomasz bla Fortuna <bla@thera.be>
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# See LICENSE file for details.
##

##
# Intro
##

cmake_minimum_required(VERSION 2.4.7)

PROJECT(otpasswd)

SET(${PROJECT_NAME}_MAJOR_VERSION 0)
SET(${PROJECT_NAME}_MINOR_VERSION 8)

# Cmake says this is required for locating libotp.a
if(COMMAND cmake_policy)
  cmake_policy(SET CMP0003 NEW)
  cmake_policy(SET CMP0005 NEW) # Silences an error.
endif(COMMAND cmake_policy)

# Detect system
IF(${CMAKE_SYSTEM_NAME} MATCHES "Linux")
  ADD_DEFINITIONS("-DOS_LINUX")
ENDIF(${CMAKE_SYSTEM_NAME} MATCHES "Linux")

IF(${CMAKE_SYSTEM_NAME} MATCHES "FreeBSD")
  ADD_DEFINITIONS("-DOS_FREEBSD")
ENDIF(${CMAKE_SYSTEM_NAME} MATCHES "FreeBSD")

##
# Basic parameters
##

# FIXME: -fPIC is required for .a library on x64 only!
# How to add it only to this target?
ADD_DEFINITIONS("-Wall -fPIC")

ADD_DEFINITIONS("-DTESTCONFIG=\"$(PWD)/examples/otpasswd.conf\"")

##
# Configurable options
##

option(PROFILE "Enable coverage tests" OFF)
option(DEBUG "Enable additional debug information" OFF)
option(NLS "Enable National Language Support (NLS)" ON)
# option( MYSQL "Generate code for MySQL database" OFF )
# option( LDAP "Generate code for LDAP" OFF )


# If PROFILE option given - enable coverage tests
IF (PROFILE)
  ADD_DEFINITIONS("-static -DPROFILE=1 -fprofile-arcs -ftest-coverage")
  LINK_LIBRARIES(gcov)
ENDIF (PROFILE)

IF (DEBUG)
  ADD_DEFINITIONS("-ggdb")
  ADD_DEFINITIONS("-DDEBUG_POSITIONS=1")
  ADD_DEFINITIONS("-DDEBUG=1")
ELSE ()
  ADD_DEFINITIONS("-s -O2")
  ADD_DEFINITIONS("-DDEBUG=0")
ENDIF (DEBUG)

IF (NLS)
  ADD_DEFINITIONS("-DUSE_NLS=1")
  IF (${CMAKE_SYSTEM_NAME} MATCHES "FreeBSD")
    LINK_LIBRARIES(intl)
  ENDIF (${CMAKE_SYSTEM_NAME} MATCHES "FreeBSD")

ENDIF (NLS)

##
# Detect include dirs and lib dirs
##

# PAM
FIND_PATH(PAM_INCLUDE_DIR pam_modules.h /usr/include/security /usr/include/pam)
INCLUDE_DIRECTORIES(${PAM_INCLUDE_DIR})
#FIND_PATH(PAM_MODULE_DIR pam_unix.so /lib/security /usr/lib/security /lib /usr/lib)
SET(PAM_MODULE_DIR /lib/security)

# Intl: On FreeBSD gettext is in another library
# Also we install PAM modules in /usr/lib.
IF (${CMAKE_SYSTEM_NAME} MATCHES "FreeBSD")
  FIND_PATH(INTL_INCLUDE_DIR libintl.h /usr/include /usr/local/include)
  FIND_PATH(INTL_LIB_DIR libintl.so /usr/lib /usr/local/lib)
  INCLUDE_DIRECTORIES(${INTL_INCLUDE_DIR})
  LINK_DIRECTORIES(${INTL_LIB_DIR})
ENDIF (${CMAKE_SYSTEM_NAME} MATCHES "FreeBSD")



# Module and PAM uses libotp, so add it's include to path...
INCLUDE_DIRECTORIES(src/common/ src/libotp/ src/crypto/ src/agent/)

## 
# Translations
##
INCLUDE(FindGettext)

SET(translations po/pl.po)
GETTEXT_CREATE_TRANSLATIONS(po/otpasswd.pot ALL po/pl.po)

##
# Targets
##

# Common functions library (64 bit numbers and logging)
ADD_LIBRARY(common STATIC src/common/print.c src/common/num.c 
  src/common/crypto.c src/crypto/polarssl_aes.c 
  src/crypto/coreutils_sha256.c)

# Library containing common functions
ADD_LIBRARY(otp STATIC src/libotp/ppp.c src/libotp/state.c 
  src/libotp/db_file.c src/libotp/db_mysql.c src/libotp/db_ldap.c
  src/libotp/config.c)

# Library containing agent functions (for both agent and its clients)
ADD_LIBRARY(agent STATIC src/agent/agent_interface.c src/agent/agent_private.c)

# Pam module target
ADD_LIBRARY(pam_otpasswd SHARED src/pam/pam_helpers.c src/pam/pam_otpasswd.c) 
SET_TARGET_PROPERTIES(pam_otpasswd PROPERTIES PREFIX "")

# Password management target
ADD_EXECUTABLE(otpasswd src/utility/otpasswd.c src/utility/actions.c 
  src/utility/actions_helpers.c src/utility/cards.c)

# Agent server
ADD_EXECUTABLE(agent_otp src/agent/agent.c src/agent/request.c 
  src/agent/testcases.c src/agent/security.c)

# Linking targets
TARGET_LINK_LIBRARIES(pam_otpasswd  otp common pam)
TARGET_LINK_LIBRARIES(otpasswd      agent common otp)
TARGET_LINK_LIBRARIES(agent_otp     agent common otp)

# Man page target
ADD_CUSTOM_TARGET(man ALL DEPENDS ${man_gz})

##
# Manuals
##
SET(man_src_1 docs/otpasswd.1)
SET(man_src_1a docs/agent_otp.1)
SET(man_src_5 docs/otpasswd.5)
SET(man_src_8 docs/pam_otpasswd.8)
SET(man_src "${man_src_1}" "${man_src_1a}" "${man_src_5}" "${man_src_8}")

##
# Install target
##
SET(CMAKE_INSTALL_PREFIX /usr)
#SET(CMAKE_INSTALL_PREFIX /home/bla/_projects/_otp/otpasswd/prefix/usr)
INSTALL(TARGETS pam_otpasswd otpasswd agent_otp
  RUNTIME DESTINATION bin
  LIBRARY DESTINATION ${PAM_MODULE_DIR})

INSTALL(FILES examples/otpasswd-login DESTINATION /etc/pam.d)
INSTALL(FILES examples/otpasswd.conf DESTINATION /etc/otpasswd)
INSTALL(FILES ${CMAKE_CURRENT_BINARY_DIR}/pl.gmo DESTINATION usr/share/otpasswd)

INSTALL(FILES ${man_src_1} DESTINATION "share/man/man1")
INSTALL(FILES ${man_src_1a} DESTINATION "share/man/man1")
INSTALL(FILES ${man_src_5} DESTINATION "share/man/man5")
INSTALL(FILES ${man_src_8} DESTINATION "share/man/man8")


##
# Tests / Coverage
# WARNING: THIS TESTS MODIFY USER STATE!
##
ENABLE_TESTING()

# This doesn't work yet:
#SET(MemoryCheckCommand /usr/bin/valgrind)
#SET(CTEST_MEMORYCHECK_COMMAND "usr/bin/valgrind")
#SET(CTEST_MEMORYCHECK_COMMAND_OPTIONS "--trace-children=yes --leak-check=full --show-reachable=yes --log-file=valgrindlog")


ADD_TEST(internal_check ./agent_otp --testcase)
# This may fail if user does not have state file!
# This tests are used mostly to have some rationale
# coverage test results
ADD_TEST(state_key0_fail tools/test_generate_no.sh)
ADD_TEST(state_key1 tools/test_generate_yes.sh)
ADD_TEST(state_flag0 ./otpasswd -v -c codelength=4)
ADD_TEST(state_print1 ./otpasswd -v -t "D10[123]")
ADD_TEST(state_print2 ./otpasswd -v -t "[123]")
ADD_TEST(state_print3 ./otpasswd -v -t "123")
ADD_TEST(state_print4 ./otpasswd -v -l "[124]")
ADD_TEST(state_print5 ./otpasswd -v -t "current")
ADD_TEST(state_print6 ./otpasswd -v -t "next")
ADD_TEST(state_print7 ./otpasswd -v -t "[next]")
ADD_TEST(state_print8 ./otpasswd -v -t "10D[123]")

ADD_TEST(state_skip1 ./otpasswd -v -s "[300]")
ADD_TEST(state_flag1a ./otpasswd -v -c show=off)
ADD_TEST(state_flag1b ./otpasswd -v -c show=on)
ADD_TEST(state_flag2 ./otpasswd -v -c codelength=12)
ADD_TEST(state_flag3 ./otpasswd -v -c alphabet=5)
ADD_TEST(state_flag4 ./otpasswd -v -c alphabet=2)
ADD_TEST(state_flag5 ./otpasswd -v -c alphabet=list)
ADD_TEST(state_flag6 ./otpasswd -v -i)
ADD_TEST(state_flag7 ./otpasswd -v --info-key)
ADD_TEST(state_spass ./otpasswd -v --password=asdfghjklASDDFGHJKL@@@%23)
ADD_TEST(state_spass_fail ./otpasswd -v --password=a)
ADD_TEST(state_label ./otpasswd -v -c "label=Set label")
ADD_TEST(state_contact ./otpasswd -v -c "contact=123456")
ADD_TEST(state_unset ./otpasswd -v -c contact= -c label=)
ADD_TEST(state_multi ./otpasswd -v -c contact=aAaA -c label=label)
ADD_TEST(state_warnings ./otpasswd -v -w)

ADD_TEST(agent_0 ./agent_otp --check-config)

# Tests which should fail
ADD_TEST(fail_ok1 ./otpasswd -v -l "[0]")
ADD_TEST(fail_ok2 ./otpasswd -v -l "0")
ADD_TEST(fail_ok3 ./otpasswd -v -t "340282366920938463463374607431768211457")
ADD_TEST(fail_ok4 ./otpasswd -v -s "-5")
ADD_TEST(fail_ok5 ./otpasswd -v -c contact="f`g")
ADD_TEST(fail_ok6 ./otpasswd -v -c label="f`g")
ADD_TEST(fail_ok7 ./otpasswd -v -c label="012345678901234567890123456789012345678901234567890")
ADD_TEST(fail_ok8 ./otpasswd -v -c contact="0123456789012345678901234567890123456789012345678900123456789")
ADD_TEST(fail_ok9 ./otpasswd -v -c codelength=17)
ADD_TEST(fail_ok10 ./otpasswd -v -c codelength=1)
ADD_TEST(fail_ok11 ./otpasswd -v -c alphabet=-1)
ADD_TEST(fail_ok12 ./otpasswd -v -c alphabet=33)
ADD_TEST(fail_ok13 ./otpasswd -v -c alphabet)
ADD_TEST(fail_ok14 ./otpasswd -v -c alphabet=2 illegal_arg)
ADD_TEST(fail_ok15 ./otpasswd -a '___')
ADD_TEST(fail_ok16 ./otpasswd --nonexisting-command a)
ADD_TEST(fail_ok17 ./otpasswd -u 32189371290 -t current) # This will fail if such user doesn't exists

# Remove state - everything should fail then!
ADD_TEST(remove_key tools/test_remove_yes.sh)



SET_TESTS_PROPERTIES(state_key0_fail state_spass_fail fail_ok1 fail_ok2 fail_ok3 
	fail_ok4 fail_ok5 fail_ok6 fail_ok7 fail_ok8 fail_ok9 fail_ok10 fail_ok11
	fail_ok12 fail_ok13 fail_ok14 fail_ok15 fail_ok16 fail_ok17
	PROPERTIES WILL_FAIL TRUE)

