# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0

# Resolve and validate the canonical release version for a CI tag.
#
# Returns an empty string outside tag pipelines. Accepted tags follow the
# canonical PEP 440 spellings used by the Python wheels:
#
#   vX.Y.Z
#   vX.Y.Z(a|b|rc)N
#   vX.Y.Z.postN
#
# The X.Y.Z portion must match the project version in the tagged source.
function(cae_release_tag_version OUT_VAR BASE_VERSION)
    set(_tag "$ENV{CI_COMMIT_TAG}")
    if(NOT _tag)
        set("${OUT_VAR}" "" PARENT_SCOPE)
        return()
    endif()

    if(NOT _tag MATCHES
            "^v(([0-9]+\\.[0-9]+\\.[0-9]+)((a|b|rc)[0-9]+|\\.post[0-9]+)?)$")
        message(FATAL_ERROR
            "Unsupported release tag '${_tag}'; expected vX.Y.Z, "
            "vX.Y.Z(a|b|rc)N, or vX.Y.Z.postN")
    endif()

    set(_version "${CMAKE_MATCH_1}")
    set(_tag_base "${CMAKE_MATCH_2}")
    if(NOT "${_tag_base}" STREQUAL "${BASE_VERSION}")
        message(FATAL_ERROR
            "Release tag '${_tag}' targets ${_tag_base}, but the project "
            "version is ${BASE_VERSION}")
    endif()

    set("${OUT_VAR}" "${_version}" PARENT_SCOPE)
endfunction()
