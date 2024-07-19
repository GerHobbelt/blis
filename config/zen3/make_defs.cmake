##Copyright (C) 2023 - 2024, Advanced Micro Devices, Inc. All rights reserved. ##

# FLAGS that are specific to the 'zen3' architecture are added here.
# FLAGS that are common for all the AMD architectures are present in
# config/zen/amd_config.mk.

# Include file containing common flags for all AMD architectures
include(${CMAKE_SOURCE_DIR}/config/zen/amd_config.cmake)

# --- Determine the C compiler and related flags ---
if(NOT WIN32)
    if(NOT (DEBUG_TYPE STREQUAL "off"))
        set(CDBGFLAGS -g)
    endif()

    if(DEBUG_TYPE STREQUAL "noopt")
        set(COPTFLAGS -O0)
    else() # off or opt
        set(COPTFLAGS -O3)
    endif()
endif()

# Flags specific to LPGEMM kernels.
set(CKLPOPTFLAGS "")

# Flags specific to optimized kernels.
# NOTE: The -fomit-frame-pointer option is needed for some kernels because
# they make explicit use of the rbp register.
if(MSVC)
    set(CKOPTFLAGS ${COPTFLAGS} /Oy)
else()
    set(CKOPTFLAGS ${COPTFLAGS} -fomit-frame-pointer)
endif()

if("${CMAKE_C_COMPILER_ID}" STREQUAL "GNU")
    if(CMAKE_C_COMPILER_VERSION VERSION_GREATER_EQUAL  11.0.0)
        # gcc 11.0 or later
        list(APPEND CKVECFLAGS -march=znver3)
        # Update CKLPOPTFLAGS for gcc to use O3 optimization without
        # -ftree-pre and -ftree-partial-pre flag. These flag results
        # in suboptimal code generation for instrinsic based kernels.
        # The -ftree-loop-vectorize results in inefficient code gen
        # for amd optimized l1 kernels based on instrinsics.
        list(APPEND CKLPOPTFLAGS -fno-tree-partial-pre -fno-tree-pre -fno-tree-loop-vectorize -fno-gcse)
    elseif(CMAKE_C_COMPILER_VERSION VERSION_GREATER_EQUAL  9.0.0)
       # gcc 9.0 or later
        list(APPEND CKVECFLAGS -march=znver2)
        list(APPEND CKLPOPTFLAGS -fno-tree-partial-pre -fno-tree-pre -fno-tree-loop-vectorize -fno-gcse)
    else()
        # If gcc is older than 9.1.0 but at least 6.1.0, then we can use -march=znver1
        # as the fallback option.
        list(APPEND CKVECFLAGS -march=znver1 -mno-avx256-split-unaligned-store)
        list(APPEND CRVECFLAGS -march=znver1 -mno-avx256-split-unaligned-store)
    endif()
endif()

if("${CMAKE_C_COMPILER_ID}" STREQUAL "Clang")
    # AOCC clang has various formats for the version line
    # AOCC.LLVM.2.0.0.B191.2019_07_19 clang version 8.0.0 (CLANG: Jenkins AOCC_2_0_0-Build#191) (based on LLVM AOCC.LLVM.2.0.0.B191.2019_07_19)
    # AOCC.LLVM.2.1.0.B1030.2019_11_12 clang version 9.0.0 (CLANG: Build#1030) (based on LLVM AOCC.LLVM.2.1.0.B1030.2019_11_12)
    # AMD clang version 10.0.0 (CLANG: AOCC_2.2.0-Build#93 2020_06_25) (based on LLVM Mirror.Version.10.0.0)
    # AMD clang version 11.0.0 (CLANG: AOCC_2.3.0-Build#85 2020_11_10) (based on LLVM Mirror.Version.11.0.0)
    # AMD clang version 12.0.0 (CLANG: AOCC_3.0.0-Build#2 2020_11_05) (based on LLVM Mirror.Version.12.0.0)
    # AMD clang version 14.0.0 (CLANG: AOCC_4.0.0-Build#98 2022_06_15) (based on LLVM Mirror.Version.14.0.0)

    # For our purpose we just want to know if it version 2x or 3x or 4x

    # But also set these in case we are using upstream LLVM clang
    execute_process(COMMAND ${CMAKE_C_COMPILER} --version OUTPUT_VARIABLE clang_full_version_string)
    string(REGEX MATCH "^[^\n]*" CLANG_VERSION_STRING "${clang_full_version_string}")
    string(REGEX MATCHALL "(AOCC_2|AOCC_3|AOCC_4|AOCC|LLVM|clang)" CLANG_STRING "${CLANG_VERSION_STRING}")
    string(REGEX REPLACE ".*clang version ([0-9]+\\.[0-9]+).*" "\\1" CLANG_VERSION "${CLANG_VERSION_STRING}")

    if("${CLANG_STRING}" MATCHES "AOCC_4")
        # AOCC version 4x we will enable znver3
        list(APPEND CKVECFLAGS -march=znver3)
    elseif("${CLANG_STRING}" MATCHES "AOCC_3")
        # AOCC version 3x we will enable znver3
        list(APPEND CKVECFLAGS -march=znver3)
    elseif("${CLANG_STRING}" MATCHES "(AOCC_2|LLVM)")
        # AOCC version 2x we will enable znver2
        list(APPEND CKVECFLAGS -march=znver2)
    elseif(CMAKE_C_COMPILER_VERSION VERSION_GREATER_EQUAL 9.0.0)
        # LLVM clang 9.0 or later
        list(APPEND CKVECFLAGS -march=znver2)
    else()
        list(APPEND CKVECFLAGS -march=znver1)
    endif()
endif()

# Flags specific to reference kernels.
set(CROPTFLAGS ${CKOPTFLAGS})
set(CRVECFLAGS ${CKVECFLAGS})
