# Copyright 2017-2019 Steffen Hirschmann
#
# This file is part of Repa.
#
# Repa is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# Repa is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with Repa.  If not, see <https://www.gnu.org/licenses/>.
#

include(define_test)

add_library(testenv STATIC "testenv.cpp")
target_include_directories(testenv PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(testenv PRIVATE repa)

# Unit tests
define_test(NAME test_vec SRC test_vec.cpp SINGLEPROC)
define_test(NAME test_integral_range SRC test_integral_range.cpp SINGLEPROC)
define_test(NAME test_unknown_exceptions SRC test_unknown_exceptions.cpp SINGLEPROC)
define_test(NAME test_vec_arith SRC test_vec_arith.cpp SINGLEPROC)
define_test(NAME test_tetra SRC test_tetra.cpp SINGLEPROC)


# Integration tests
define_test(NAME test_small_grid SRC test_small_grid.cpp SINGLEPROC LIBRARIES testenv)
define_test(NAME test_grid_types SRC test_grid_types.cpp LIBRARIES testenv)
define_test(NAME test_repart SRC test_repart.cpp LIBRARIES testenv)
define_test(NAME test_extra_params SRC test_extra_params.cpp LIBRARIES testenv)
define_test(NAME test_hybrid_method_switch SRC test_hybrid_method_switch.cpp LIBRARIES testenv)
define_test(NAME test_cell_numbers SRC test_cell_numbers.cpp LIBRARIES testenv)
define_test(NAME test_neighborhood_symmetry SRC test_neighborhood_symmetry.cpp LIBRARIES testenv)
define_test(NAME test_process_neighborhood_symmetry SRC test_process_neighborhood_symmetry.cpp LIBRARIES testenv)
define_test(NAME test_all_cells_assigned SRC test_all_cells_assigned.cpp LIBRARIES testenv)
define_test(NAME test_all_ghost_cells_valid SRC test_all_ghost_cells_valid.cpp LIBRARIES testenv)
define_test(NAME test_ghost_exchange_volume SRC test_ghost_exchange_volume.cpp LIBRARIES testenv)
define_test(NAME test_pos_rank_agreement SRC test_pos_rank_agreement.cpp LIBRARIES testenv)
define_test(NAME test_load_reduction SRC test_load_reduction.cpp LIBRARIES testenv)

# ghost_exchange_volume_ext requires debug build (NDEBUG *not* set).
if(${CMAKE_BUILD_TYPE} STREQUAL "Debug")
  define_test(NAME test_ghost_exchange_volume_ext SRC test_ghost_exchange_volume_ext.cpp LIBRARIES testenv)
else()
  message(STATUS "WITH_TESTS: Skipping tests that need a debug build (build type: ${CMAKE_BUILD_TYPE}).")
endif()
