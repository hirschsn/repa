#pragma once

//#ifdef HAVE_METIS

#include "globox.hpp"
#include "pargrid.hpp"
#include <array>
#include <parmetis.h>
#include <unordered_map>

namespace repa {
namespace grids {

struct Graph : public ParallelLCGrid {
    Graph(const boost::mpi::communicator &comm,
          Vec3d box_size,
          double min_cell_size);
    lidx n_local_cells() override;
    gidx n_ghost_cells() override;
    nidx n_neighbors() override;
    rank neighbor_rank(nidx i) override;
    Vec3d cell_size() override;
    Vec3i grid_size() override;
    lgidx cell_neighbor_index(lidx cellidx, int neigh) override;
    std::vector<GhostExchangeDesc> get_boundary_info() override;
    lidx position_to_cell_index(double pos[3]) override;
    rank position_to_rank(double pos[3]) override;
    nidx position_to_neighidx(double pos[3]) override;
    bool repartition(const repart::Metric &m,
                     std::function<void()> exchange_start_callback) override;
    ~Graph();

private:
    friend struct HybridGPDiff; // Needs access to "partition" vector
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
    std::vector<idx_t> partition;

    // Stores the mapping of a global linearized index to a
    // local linearized index or an ghost linearized index.
    std::unordered_map<int, int> global_to_local;

    // Reinitializes the subdomain and communication data structures
    // after repartitioning.
    void init();
};
} // namespace grids
} // namespace repa

//#endif // HAVE_METIS