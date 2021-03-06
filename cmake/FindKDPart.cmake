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

set(KDPART_DIR "" CACHE PATH "kdpart directory")

find_path(KDPART_INCLUDE_DIR
          kdpart/kdpart.h
          HINTS ${KDPART_DIR}
          ENV C_INCLUDE_PATH
          PATH_SUFFIXES include)

find_library(KDPART_LIBRARIES
             kdpart
             HINTS ${KDPART_DIR}
             ENV LIBRARY_PATH
             PATH_SUFFIXES lib)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(KDPart
                                  DEFAULT_MSG
                                  KDPART_LIBRARIES
                                  KDPART_INCLUDE_DIR)
