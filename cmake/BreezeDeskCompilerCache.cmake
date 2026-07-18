function(breezedesk_enable_compiler_cache)
    find_program(BREEZEDESK_CCACHE_PROGRAM ccache)
    find_program(BREEZEDESK_SCCACHE_PROGRAM sccache)
    if(BREEZEDESK_CCACHE_PROGRAM)
        set(CMAKE_C_COMPILER_LAUNCHER "${BREEZEDESK_CCACHE_PROGRAM}" PARENT_SCOPE)
        set(CMAKE_CXX_COMPILER_LAUNCHER "${BREEZEDESK_CCACHE_PROGRAM}" PARENT_SCOPE)
        message(STATUS "${BREEZEDESK_PRODUCT_NAME} compiler cache: ccache")
    elseif(BREEZEDESK_SCCACHE_PROGRAM)
        if(MSVC)
            if(POLICY CMP0141)
                # /Zi makes every source in a target write to the same compiler
                # PDB. Parallel sccache invocations can still race on that file
                # despite /FS, so keep debug information in each object instead.
                set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT
                    "$<$<CONFIG:Debug,RelWithDebInfo>:Embedded>"
                    PARENT_SCOPE)
            else()
                message(STATUS
                    "${BREEZEDESK_PRODUCT_NAME} compiler cache: sccache disabled; "
                    "this CMake version cannot select embedded MSVC debug information")
                return()
            endif()
        endif()
        set(CMAKE_C_COMPILER_LAUNCHER "${BREEZEDESK_SCCACHE_PROGRAM}" PARENT_SCOPE)
        set(CMAKE_CXX_COMPILER_LAUNCHER "${BREEZEDESK_SCCACHE_PROGRAM}" PARENT_SCOPE)
        message(STATUS "${BREEZEDESK_PRODUCT_NAME} compiler cache: sccache")
    endif()
endfunction()
