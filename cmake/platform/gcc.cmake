# gcc (and other Unix-like compilers, e.g. MinGW)

# Compiler flag modules.
INCLUDE(CheckCCompilerFlag)
INCLUDE(CheckCXXCompilerFlag)

# Check what flag is needed for C99 support.
INCLUDE(CheckC99CompilerFlag)
CHECK_C99_COMPILER_FLAG(RP_C99_CFLAG)

# Check what flag is needed for C++ 2011 support.
INCLUDE(CheckCXX11CompilerFlag)
CHECK_CXX11_COMPILER_FLAG(RP_CXX11_CXXFLAG)

# Check what flag is needed for stack smashing protection.
INCLUDE(CheckStackProtectorCompilerFlag)
CHECK_STACK_PROTECTOR_COMPILER_FLAG(RP_STACK_CFLAG)

SET(RP_C_FLAGS_COMMON "${RP_C99_CFLAG} ${RP_STACK_CFLAG}")
SET(RP_CXX_FLAGS_COMMON "${RP_CXX11_CXXFLAG} ${RP_STACK_CFLAG}")
SET(RP_EXE_LINKER_FLAGS_COMMON "")

UNSET(RP_C99_CFLAG)
UNSET(RP_CXX11_CXXFLAG)
UNSET(RP_CXX_NO_RTTI_CXXFLAG)
UNSET(RP_CXX_NO_EXCEPTIONS_CXXFLAG)
UNSET(RP_STACK_CFLAG)

# Test for common CFLAGS and CXXFLAGS.
FOREACH(FLAG_TEST "-Wall" "-Wextra" "-fstrict-aliasing" "-Wno-multichar")
	CHECK_C_COMPILER_FLAG("${FLAG_TEST}" CFLAG_${FLAG_TEST})
	IF(CFLAG_${FLAG_TEST})
		SET(RP_C_FLAGS_COMMON "${RP_C_FLAGS_COMMON} ${FLAG_TEST}")
	ENDIF(CFLAG_${FLAG_TEST})
	UNSET(CFLAG_${FLAG_TEST})
	
	CHECK_CXX_COMPILER_FLAG("${FLAG_TEST}" CXXFLAG_${FLAG_TEST})
	IF(CXXFLAG_${FLAG_TEST})
		SET(RP_CXX_FLAGS_COMMON "${RP_CXX_FLAGS_COMMON} ${FLAG_TEST}")
	ENDIF(CXXFLAG_${FLAG_TEST})
	UNSET(CXXFLAG_${FLAG_TEST})
ENDFOREACH()

# Code coverage checking.
IF(ENABLE_COVERAGE)
	# Partially based on:
	# https://github.com/bilke/cmake-modules/blob/master/CodeCoverage.cmake
	# (commit 59f8ab8dded56b490dec388ac6ad449318de8779)
	IF("${CMAKE_CXX_COMPILER_ID}" MATCHES "(Apple)?[Cc]lang")
		# CMake 3.0.0 added code coverage support.
		IF("${CMAKE_CXX_COMPILER_VERSION}" VERSION_LESS 3)
			MESSAGE(FATAL_ERROR "clang-3.0.0 or later is required for code coverage testing.")
		ENDIF()
	ELSEIF(NOT CMAKE_COMPILER_IS_GNUCXX)
		MESSAGE(FATAL_ERROR "Code coverage testing is currently only supported on gcc and clang.")
	ENDIF()

	# Don't bother checking for the coverage options.
	# We're assuming they're always supported.
	SET(RP_C_FLAGS_COVERAGE "--coverage -fprofile-arcs -ftest-coverage")
	SET(RP_C_FLAGS_COMMON "${RP_C_FLAGS_COMMON} ${RP_C_FLAGS_COVERAGE}")
	SET(RP_CXX_FLAGS_COMMON "${RP_CXX_FLAGS_COMMON} ${RP_C_FLAGS_COVERAGE}")

	# Link gcov to all targets.
	SET(GCOV_LIBRARY "-lgcov")
	FOREACH(VAR "" C_ CXX_)
		IF(CMAKE_${VAR}STANDARD_LIBRARIES)
			SET(CMAKE_${VAR}STANDARD_LIBRARIES "${CMAKE_${VAR}STANDARD_LIBRARIES} ${GCOV_LIBRARY}")
		ELSE(CMAKE_${VAR}STANDARD_LIBRARIES)
			SET(CMAKE_${VAR}STANDARD_LIBRARIES "${GCOV_LIBRARY}")
		ENDIF(CMAKE_${VAR}STANDARD_LIBRARIES)
	ENDFOREACH(VAR)

	# Create a code coverage target.
	FOREACH(_program gcov lcov genhtml)
		FIND_PROGRAM(${_program}_PATH gcov)
		IF(NOT ${_program}_PATH)
			MESSAGE(FATAL_ERROR "${_program} not found; cannot enable code coverage testing.")
		ENDIF(NOT ${_program}_PATH)
		UNSET(${_program}_PATH)
	ENDFOREACH(_program)

	ADD_CUSTOM_TARGET(coverage
		WORKING_DIRECTORY "${CMAKE_BUILD_DIR}"
		COMMAND ${POSIX_SH} "${CMAKE_SOURCE_DIR}/scripts/lcov.sh" "${CMAKE_BUILD_TYPE}" "rom-properties" "coverage"
		)
ENDIF(ENABLE_COVERAGE)

# Test for common LDFLAGS.
# TODO: Doesn't work on OS X. (which means it's not really testing it!)
IF(NOT APPLE)
	FOREACH(FLAG_TEST "-Wl,-O1" "-Wl,--sort-common" "-Wl,--as-needed")
		CHECK_C_COMPILER_FLAG("${FLAG_TEST}" LDFLAG_${FLAG_TEST})
		IF(LDFLAG_${FLAG_TEST})
			SET(RP_EXE_LINKER_FLAGS_COMMON "${RP_EXE_LINKER_FLAGS_COMMON} ${FLAG_TEST}")
		ENDIF(LDFLAG_${FLAG_TEST})
		UNSET(LDFLAG_${FLAG_TEST})
	ENDFOREACH()
ENDIF(NOT APPLE)
SET(RP_SHARED_LINKER_FLAGS_COMMON "${RP_EXE_LINKER_FLAGS_COMMON}")
SET(RP_MODULE_LINKER_FLAGS_COMMON "${RP_EXE_LINKER_FLAGS_COMMON}")

# Check for -Og.
# This flag was added in gcc-4.8, and enables optimizations that
# don't interfere with debugging.
CHECK_C_COMPILER_FLAG("-Og" CFLAG_OPTIMIZE_DEBUG)
IF (CFLAG_OPTIMIZE_DEBUG)
	SET(CFLAG_OPTIMIZE_DEBUG "-Og")
ELSE(CFLAG_OPTIMIZE_DEBUG)
	SET(CFLAG_OPTIMIZE_DEBUG "-O0")
ENDIF(CFLAG_OPTIMIZE_DEBUG)

# Debug/release flags.
SET(RP_C_FLAGS_DEBUG		"${CFLAG_OPTIMIZE_DEBUG} -ggdb -DDEBUG -D_DEBUG")
SET(RP_CXX_FLAGS_DEBUG		"${CFLAG_OPTIMIZE_DEBUG} -ggdb -DDEBUG -D_DEBUG")
SET(RP_C_FLAGS_RELEASE		"-O2 -ggdb -DNDEBUG")
SET(RP_CXX_FLAGS_RELEASE	"-O2 -ggdb -DNDEBUG")

# Unset temporary variables.
UNSET(CFLAG_OPTIMIZE_DEBUG)
