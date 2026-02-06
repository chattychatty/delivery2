cmake_minimum_required(VERSION 3.15)

project(PARCO_Computing_2026_244944 LANGUAGES C CXX)

# ============================================================
# Base settings
# ============================================================
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Default to Release if single-config generators (Makefiles, Ninja)
if(NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# Build toggles for the current Deliverable 2 workflow
option(PARCO_BUILD_MPI "Build MPI targets (Deliverable 2)" ON)
option(PARCO_BUILD_OMP "Enable OpenMP for D2 targets that use it" ON)

# On clusters: -march=native is risky (login node != compute nodes)
option(PARCO_MARCH_NATIVE "Enable -march=native (NOT recommended on clusters)" OFF)

# ============================================================
# FORCE output directories to <repo>/bin (robust with IDEs)
# ============================================================
set(PARCO_BIN_DIR "${PROJECT_SOURCE_DIR}/bin")

# Global defaults (some IDEs may override -> we also set per-target below)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${PARCO_BIN_DIR}")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${PARCO_BIN_DIR}")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${PARCO_BIN_DIR}")

foreach(cfg DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
  string(TOUPPER ${cfg} CFG_UP)
  set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${CFG_UP} "${PARCO_BIN_DIR}")
  set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_${CFG_UP} "${PARCO_BIN_DIR}")
  set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_${CFG_UP} "${PARCO_BIN_DIR}")
endforeach()

function(parco_force_bin_output tgt)
  set_target_properties(${tgt} PROPERTIES
          RUNTIME_OUTPUT_DIRECTORY "${PARCO_BIN_DIR}"
          ARCHIVE_OUTPUT_DIRECTORY "${PARCO_BIN_DIR}"
          LIBRARY_OUTPUT_DIRECTORY "${PARCO_BIN_DIR}"
  )
  foreach(cfg DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
    string(TOUPPER ${cfg} CFG_UP)
    set_target_properties(${tgt} PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY_${CFG_UP} "${PARCO_BIN_DIR}"
            ARCHIVE_OUTPUT_DIRECTORY_${CFG_UP} "${PARCO_BIN_DIR}"
            LIBRARY_OUTPUT_DIRECTORY_${CFG_UP} "${PARCO_BIN_DIR}"
    )
  endforeach()
endfunction()

# ============================================================
# Locate D2 sources
# ============================================================
set(D2_DIR "${PROJECT_SOURCE_DIR}/D2-code")
set(D2_CUSTOM_MATRIX_SRC "${D2_DIR}/custom_matrix.cpp")
set(D2_SPMV_MPI_SRC      "${D2_DIR}/spmv_mpi.cpp")
set(D2_STRONG_MATRIX_SRC "${D2_DIR}/strong_matrix.cpp")

if (NOT EXISTS "${D2_CUSTOM_MATRIX_SRC}")
  message(FATAL_ERROR "D2 custom_matrix source not found: ${D2_CUSTOM_MATRIX_SRC}")
endif()
if (NOT EXISTS "${D2_SPMV_MPI_SRC}")
  message(FATAL_ERROR "D2 spmv_mpi source not found: ${D2_SPMV_MPI_SRC}")
endif()
if (NOT EXISTS "${D2_STRONG_MATRIX_SRC}")
  message(FATAL_ERROR "D2 strong_matrix source not found: ${D2_STRONG_MATRIX_SRC}")
endif()

# ============================================================
# Locate mmio dir (must be ./mmio)
# ============================================================
set(MMIO_DIR "${PROJECT_SOURCE_DIR}/mmio")
if(NOT EXISTS "${MMIO_DIR}/mmio.c" OR NOT EXISTS "${MMIO_DIR}/mmio.h")
  message(FATAL_ERROR
          "mmio not found.\n"
          "Expected:\n"
          "  - ${MMIO_DIR}/mmio.c\n"
          "  - ${MMIO_DIR}/mmio.h\n"
          "Fix: ensure mmio.c and mmio.h are in the 'mmio' folder at repo root."
  )
endif()

# ============================================================
# Common C library (mmio)
# ============================================================
add_library(mmio STATIC "${MMIO_DIR}/mmio.c")
target_include_directories(mmio PUBLIC "${MMIO_DIR}")
parco_force_bin_output(mmio)

# ============================================================
# Packages (MPI/OpenMP)
# ============================================================
if(NOT PARCO_BUILD_MPI)
  message(FATAL_ERROR "PARCO_BUILD_MPI must be ON for the Deliverable 2 workflow.")
endif()

find_package(MPI REQUIRED)

if(PARCO_BUILD_OMP)
  find_package(OpenMP REQUIRED)
else()
  message(FATAL_ERROR "PARCO_BUILD_OMP must be ON because spmv_mpi.cpp uses OpenMP APIs.")
endif()

# ============================================================
# Helper: common release flags
# ============================================================
function(parco_release_flags tgt)
  if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
    target_compile_options(${tgt} PRIVATE
            $<$<CONFIG:Release>:-O3>
            $<$<CONFIG:RelWithDebInfo>:-O3>
    )
    if (PARCO_MARCH_NATIVE)
      target_compile_options(${tgt} PRIVATE -march=native)
    endif()
  elseif (MSVC)
    target_compile_options(${tgt} PRIVATE $<$<CONFIG:Release>:/O2>)
  endif()
endfunction()

# ============================================================
# Helper: enable OpenMP via imported target if available
# ============================================================
function(parco_link_openmp tgt)
  if (OpenMP_CXX_FOUND)
    target_link_libraries(${tgt} PRIVATE OpenMP::OpenMP_CXX)
  else()
    message(FATAL_ERROR "OpenMP requested but not found by CMake.")
  endif()
endfunction()

# ============================================================
# Helper: filesystem workaround (GCC < 9 may need stdc++fs)
# ============================================================
include(CheckCXXSourceCompiles)
check_cxx_source_compiles("
  #include <filesystem>
  int main(){ std::filesystem::path p{\".\"}; return (int)p.string().size(); }
" PARCO_HAS_STD_FILESYSTEM)

function(parco_fix_filesystem tgt)
  if(NOT PARCO_HAS_STD_FILESYSTEM)
    target_link_libraries(${tgt} PRIVATE stdc++fs)
  endif()
endfunction()

# ============================================================
# D2 utility target: custom_matrix
# ============================================================
add_executable(custom_matrix "${D2_CUSTOM_MATRIX_SRC}")
parco_force_bin_output(custom_matrix)
target_link_libraries(custom_matrix PRIVATE mmio)
parco_release_flags(custom_matrix)
parco_fix_filesystem(custom_matrix)

if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
  target_compile_options(custom_matrix PRIVATE -Wno-write-strings)
endif()

# ============================================================
# D2 benchmark targets (MPI)
# ============================================================
add_executable(spmv_mpi "${D2_SPMV_MPI_SRC}")
parco_force_bin_output(spmv_mpi)
target_link_libraries(spmv_mpi PRIVATE MPI::MPI_CXX mmio)
parco_link_openmp(spmv_mpi)
parco_release_flags(spmv_mpi)
parco_fix_filesystem(spmv_mpi)

add_executable(strong_matrix "${D2_STRONG_MATRIX_SRC}")
parco_force_bin_output(strong_matrix)
target_link_libraries(strong_matrix PRIVATE MPI::MPI_CXX mmio)
parco_release_flags(strong_matrix)
parco_fix_filesystem(strong_matrix)

# ============================================================
# Install (optional)
# ============================================================
install(TARGETS custom_matrix spmv_mpi strong_matrix RUNTIME DESTINATION bin)

# ============================================================
# Packaging (optional)
# ============================================================
set(CPACK_GENERATOR "TGZ;ZIP")
set(CPACK_PACKAGE_NAME "parco")
set(CPACK_PACKAGE_VERSION "1.0.0")
include(CPack)