# Copyright (C) 2023 - 2024 Advanced Micro Devices, Inc.  All rights reserved.
# SPDX-License-Identifier: MIT

option(YOCTO "Yocto based embeddedsw FLOW" OFF)
set(CMAKE_POLICY_DEFAULT_CMP0140 OLD)

if (YOCTO)
find_package(commonmeta QUIET)
endif()

set (CMAKE_INSTALL_LIBDIR "lib")
function (collector_create name base)
  set_property (GLOBAL PROPERTY "COLLECT_${name}_LIST")
  set_property (GLOBAL PROPERTY "COLLECT_${name}_BASE" "${base}")
endfunction (collector_create)

function (collector_list var name)
  get_property (_list GLOBAL PROPERTY "COLLECT_${name}_LIST")
  set (${var} "${_list}" PARENT_SCOPE)
endfunction (collector_list)

function (collector_base var name)
  get_property (_base GLOBAL PROPERTY "COLLECT_${name}_BASE")
  set (${var} "${_base}" PARENT_SCOPE)
endfunction (collector_base)

function (collect name)
  collector_base (_base ${name})
  string(COMPARE NOTEQUAL "${_base}" "" _is_rel)
  set (_list)
  foreach (s IN LISTS ARGN)
    if (_is_rel)
      get_filename_component (s "${s}" ABSOLUTE)
      file (RELATIVE_PATH s "${_base}" "${s}")
    endif (_is_rel)
    list (APPEND _list "${s}")
  endforeach ()
  set_property (GLOBAL APPEND PROPERTY "COLLECT_${name}_LIST" "${_list}")
endfunction (collect)

string(ASCII 27 Esc)
set(ColourReset "${Esc}[m")
set(BoldRed     "${Esc}[1;31m")
set(BoldYellow  "${Esc}[1;33m")
