/**
 * Copyright 2017-2019 Steffen Hirschmann
 * Copyright 2017-2018 Maximilian Wildbrett
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

#include "globox.hpp"
#include "pargrid.hpp"
#include <array>
#include <parmetis.h>
#include <unordered_map>

namespace repa {
namespace grids {

struct GloMethod : public ParallelLCGrid {
    GloMethod(const boost::mpi::communicator &comm,
              Vec3d box_size,
              double min_cell_size);
    void after_construction() override;
    lidx n_local_cells() override;
    gidx n_ghost_cells() override;
    nidx n_neighbors() override;
    rank neighbor_rank(nidx i) override;
    Vec3d cell_size() override;
    Vec3i grid_size() override;
    lgidx cell_neighbor_index(lidx cellidx, int neigh) override;
    std::vector<GhostExchangeDesc> get_boundary_info() override;
    lidx position_to_cell_index(Vec3d pos) override;
    rank position_to_rank(Vec3d pos) override;
    nidx position_to_neighidx(Vec3d pos) override;
    bool repartition(CellMetric m,
                     CellCellMetric ccm,
                     Thunk exchange_start_callback) override;
    ~GloMethod();

    int global_hash(lgidx cellidx) override;

protected:
    virtual bool sub_repartition(CellMetric m, CellCellMetric ccm) = 0;

    // Number of local cells
    int localCells;
    // Number of ghost cells
    int ghostCells;
    // All neighbor ranks (ranks of subdomains neighboring this subdomain)
    std::vector<rank> neighbors;
    // Communication descriptors
    std::vector<GhostExchangeDesc> exchangeVector;

    // Defines the linearization and global cell neighborhoods.
    globox::GlobalBox<int, int> gbox;

    // Stores the global index of local cells and the ghost cells of the
    // subdomain associated to this process. This ordering is imposed via
    // pargrid.hpp and ESPResSo.
    std::vector<int> cells;
    // Stores the global partitioning. One rank per cell. Index via global
    // index.
    std::vector<rank> partition;

    // Stores the mapping of a global linearized index to a
    // local linearized index or an ghost linearized index.
    std::unordered_map<int, int> global_to_local;

    // Called before init()
    // To override by subclasses if necessary
    // Partitioning is done, however all datastructures (but "partition")
    // are still in the old state (inconsistent to the new partitioning).
    virtual void pre_init(bool firstcall)
    {
    }

    // Called after init()
    // To override by subclasses if necessary
    // Partitioning and init fully done.
    virtual void post_init(bool firstcall)
    {
    }

    // Callback for a new border cell
    // Called during init()
    // @param localcell local cell index of the border cell
    // @apram foreigncell global index of the new ghost cell
    // @param owner owner rank of the ghost cell
    virtual void
    init_new_foreign_cell(lidx localcell, int foreigncell, rank owner)
    {
    }

    // Reinitializes the subdomain and communication data structures
    // after repartitioning.
    void init(bool firstcall = false);
};
} // namespace grids
} // namespace repa
