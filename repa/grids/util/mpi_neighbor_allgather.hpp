/**
 * Copyright 2017-2019 Steffen Hirschmann
 *
 * This file is part of Repa.
 *
 * Repa is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Repa is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Repa.  If not, see <https://www.gnu.org/licenses/>.
 */

#pragma once

#include "pargrid.hpp"
#include <boost/mpi.hpp>
#include <boost/range/irange.hpp>

namespace repa {
namespace util {

namespace __impl {

/** MPI_Allgather to and from a subset of processes
 *
 */
template <typename T, typename U = T>
std::vector<T> mpi_subset_allgather(const boost::mpi::communicator &comm,
                                    const std::vector<rank_type> &neighbors,
                                    const U &data)
{
    std::vector<boost::mpi::request> sreq_cells(neighbors.size());
    std::vector<boost::mpi::request> rreq_cells(neighbors.size());

    for (const auto i : boost::irange(neighbors.size())) {
        sreq_cells[i] = comm.isend(neighbors[i], 2, data);
    }

    std::vector<T> all_data(neighbors.size());
    for (const auto i : boost::irange(neighbors.size())) {
        rreq_cells[i] = comm.irecv(neighbors[i], 2, all_data[i]);
    }

    boost::mpi::wait_all(std::begin(rreq_cells), std::end(rreq_cells));
    boost::mpi::wait_all(std::begin(sreq_cells), std::end(sreq_cells));

    return all_data;
}

/**
 * Type trait that has a boolean member "value" equal to true if the
 * template parameter is a std::pair. Otherwise, the member is false.
 */
template <typename T>
struct is_pair {
    static const bool value = false;
};

template <typename T, typename U>
struct is_pair<std::pair<T, U>> {
    static const bool value = true;
};

template <typename T>
const bool is_pair_v = is_pair<T>::value;

} // namespace __impl

/** MPI_Neighbor_allgather.
 *
 */
template <typename T, typename U = T>
std::vector<T> mpi_neighbor_allgather(const boost::mpi::communicator &neighcomm,
                                      const U &data)
{
    assert(has_dist_graph_topology(neighcomm));
    return __impl::mpi_subset_allgather<T, U>(
        neighcomm, mpi_undirected_neighbors(neighcomm), data);
}

/** MPI_Neighbor_allgather.
 *
 * Overload specifically to send std::pairs that hold references.
 * Pass references and get out non-references.
 */
template <typename T1,
          typename T2,
          // Only enabled if "T1" is not a std::pair
          typename = typename std::enable_if<!__impl::is_pair_v<T1>>::type>
std::vector<std::pair<T1, T2>>
mpi_neighbor_allgather(const boost::mpi::communicator &neighcomm,
                       const std::pair<const T1 &, const T2 &> &data)
{
    return mpi_neighbor_allgather<std::pair<T1, T2>,
                                  std::pair<const T1 &, const T2 &>>(neighcomm,
                                                                     data);
}

} // namespace util
} // namespace repa
