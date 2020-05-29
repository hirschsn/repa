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

#include <boost/mpi/communicator.hpp>
#include <boost/optional.hpp>
#include <vector>

#include "common_types.hpp"
#include "grids/util/simple_variant.hpp"
#include "grids/util/span.hpp"
#include "grids/util/strong_alias.hpp"

#ifndef NDEBUG
#define GLOBAL_HASH_NEEDED
#else
#ifdef GLOBAL_HASH_NEEDED
#undef GLOBAL_HASH_NEEDED
#endif
#endif

namespace repa {

/** Interface for implementations to query additional information
 * from the caller.
 * Used in the factory method make_pargrid in pargrid_factory.hpp
 */
struct ExtraParams {
    /**
     * For the gridbased method.
     * Not required, but strongly recommended.
     */
    std::function<Vec3d(void)> subdomain_midpoint = nullptr;

    /**
     * For unstructured methods.
     * Descriptor of the initial partitioning to use.
     * Currently, "Linear", "Cart1D" and "Cart3D" are available.
     */
    boost::optional<std::string> init_part;
};

namespace type_tags {

#define TYPE_TAG_DEFINE(tag_name)                                              \
    struct tag_name {                                                          \
    }

TYPE_TAG_DEFINE(RankIndex);
TYPE_TAG_DEFINE(LocalCellIndex);
TYPE_TAG_DEFINE(GhostCellIndex);
TYPE_TAG_DEFINE(GlobalCellIndex);

} // namespace type_tags

/** Some typedefs to document what an integer is supposed to mean
 */

/** Rank of a node
 */
typedef int rank_type;

/** Index of a local cell (0..n_local_cells-1) or the
 * total number of local cells (n_local_cells).
 */
using local_cell_index_type = util::StrongAlias<int, type_tags::LocalCellIndex>;

/** Index of a ghost cell (0..n_ghost_cells-1) or the
 * total number of ghost cells (n_ghost_cells).
 */
using ghost_cell_index_type = util::StrongAlias<int, type_tags::GhostCellIndex>;

/** Index of a local or ghost cell.
 */
using local_or_ghost_cell_index_type
    = util::simple_variant<local_cell_index_type, ghost_cell_index_type>;

/** Global cell index (unique across all
 * processes) or the number total number of
 * cells across all processes.
 */
using global_cell_index_type
    = util::StrongAlias<int, type_tags::GlobalCellIndex>;

typedef std::function<std::vector<double>(void)> CellMetric;
typedef std::function<double(local_cell_index_type,
                             local_or_ghost_cell_index_type)>
    CellCellMetric;
typedef std::function<void(void)> Thunk;

namespace grids {

/** Describes a ghost exchange process.
 * Corresponds to a GhostCommunication from ghosts.[ch]pp.
 * Associates a rank with a list of cell indices which are to be sent to and
 * received from a process, respectively.
 *
 * Note that the number of cells to be received must always be equal to the
 * number of cells to be send. When the same cell appears multiple times in the
 * ghostlayer of another process (which is possible in periodic domains), it
 * must exist multiple times in the send-datastructure.
 */
struct GhostExchangeDesc {
    rank_type dest; // Destination rank
    std::vector<ghost_cell_index_type>
        recv; // Ghost cell indices which are to be received
    std::vector<local_cell_index_type>
        send; // Local cell indices which are to be sent

    GhostExchangeDesc() : dest(-1)
    {
    }
    GhostExchangeDesc(rank_type dest,
                      std::vector<ghost_cell_index_type> &&recv,
                      std::vector<local_cell_index_type> &&send)
        : dest(dest), recv(std::move(recv)), send(std::move(send))
    {
    }
};

/** Interface for a parallel linked-cell grid implementation.
 */
struct ParallelLCGrid {
    ParallelLCGrid(const boost::mpi::communicator &comm,
                   Vec3d box_size,
                   double min_cell_size);

    // Must be called directly after construction
    // Make virtual calls here instead of the constructor.
    virtual void after_construction()
    {
    }

    virtual ~ParallelLCGrid() = default;

    /** Returns the number of local cells.
     */
    virtual local_cell_index_type n_local_cells() const = 0;

    /** Returns the number of ghost cells
     */
    virtual ghost_cell_index_type n_ghost_cells() const = 0;

    /** Returns a span/range of ranks of all neighbor processes.
     */
    virtual util::const_span<rank_type> neighbor_ranks() const = 0;

    /** Returns the cell sizes of Linked Cell grid.
     */
    virtual Vec3d cell_size() const = 0;

    /** Returns the number of grid cells in total in each direction.
     */
    virtual Vec3i grid_size() const = 0;

    /** Returns the index of a cell neighboring a given cell (by index).
     *
     * A resulting index N can be interpreted as follows:
     * Case 1: 0 <= N < n_local_cells(): local cell N.
     * Case 2: n_local_cells() <= N < n_local_cells() + n_ghost_cells():
     *  ghost cell no. (N - n_local_cells()).
     * Other values for N cannot occur.
     *
     * Might throw a std::domain_error if 0 > cellidx or cellidx >=
     * get_n_local_cells().
     *
     * Neighbor 0 is the cells itself, i.e. "cell_neighbor_index(c, 0) == c"
     * Neighbors 1-13: Half shell neighborhood
     * Neighbors 14-26: Rest of full shell neighborhood
     *
     * @param cellidx Base cell
     * @param neigh Neighbor
     */
    virtual local_or_ghost_cell_index_type
    cell_neighbor_index(local_cell_index_type cellidx, fs_neighidx neigh)
        = 0;

    /** Returns the ghost exchange info.
     * @see GhostExchangeDesc
     */
    virtual util::const_span<GhostExchangeDesc> get_boundary_info() = 0;

    /** Returns the index of a local cell at position "pos".
     * @throws std::domain_error if position is not in the local subdomain.
     */
    virtual local_cell_index_type position_to_cell_index(Vec3d pos) = 0;

    /** Returns the rank of the process which is responsible for the cell at
     * position "pos". Works for the whole domain!
     */
    virtual rank_type position_to_rank(Vec3d pos) = 0;

    /** *Maybe* repartitions the grid. Returns true if grid has been changed
     * (repartitioned). This means all data of this class is invalidated.
     * If false is returned, *no* data returned since the last call to
     * repartition() or topology_init() has been invalidated.
     *
     * Be careful: If the call returns true also old cell indices are
     * invalidated and silently get a new meaning.
     *
     * @param exchange_start_callback is a function with no arguments which
     * starts the data migration. This function is only called if the return
     * value is "true". Also, it is called as soon as "position_to_rank" can
     * safely be called.
     */
    virtual bool
    repartition(CellMetric m, CellCellMetric ccm, Thunk exchange_start_callback)
        = 0;

    struct UnknwonCommandError : public std::exception {
        UnknwonCommandError(std::string s)
            : w(std::string("Could not interpret command `") + s
                + std::string("'"))
        {
        }
        const char *what() const noexcept override
        {
            return w.c_str();
        }

    private:
        std::string w;
    };
    /** Deliver implementation-defined commands to the partitioner.
     *
     * @throws UnknownCommandError if command cannot be interpreted.
     */
    virtual void command(std::string s)
    {
        throw UnknwonCommandError{s};
    };

    /** Returns a globally unique id for a local cell.
     * This id is uniquely assigned to the global cell
     * corresponding to a local one, i.e. two different
     * processes will return the same global_hash
     * if the (most likely different) local cellidxs correspond to the same
     * global cell.
     * If NDEBUG is set, additionally to the above stated semantics,
     * this function is allowed to return constant 0.
     *
     * This function is useful for testing purposes only.
     * Use *only* if NDEBUG is *not* set.
     *
     */
    virtual global_cell_index_type
    global_hash(local_or_ghost_cell_index_type cellidx)
        = 0;

protected:
    boost::mpi::communicator comm, comm_cart;
    Vec3d box_l;
    Vec3i node_grid, node_pos;
    double max_range;
};

} // namespace grids
} // namespace repa
