#pragma once

//#ifdef HAVE_TETRA

#include <mpi.h>
#include <unordered_map>
#include <vector>

#include <tetra.hpp>

#include "globox.hpp"
#include "pargrid.hpp"

namespace repa {
namespace grids {

/** TODO: comment
 */
struct GridBasedGrid : public ParallelLCGrid {
    GridBasedGrid(const boost::mpi::communicator &comm,
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

    void command(std::string s) override;

private:
    // Indicator if the decomposition currently is a regular grid,
    // which is the case directly after instantiation.
    // This is important for position-to-rank queries.
    // They can be answered for the whole domain if the grid
    // is a regular grid. Otherwise, a process can only resolve
    // positions in neighboring subdomains.
    bool is_regular_grid;

    // Factor for grid point displacement.
    // Settable via command()
    double mu;

    // Number of local and ghost cells
    int nlocalcells, nghostcells;

    // Triangulation data structure for this subdomain
    tetra::Octagon my_dom;

    // Triangulation data structure for the neighboring subdomains
    std::vector<tetra::Octagon> neighbor_doms;
    // Ranks of the neigbors. Note that the number of neighbors
    // is constant and the neighbors themselves do *not*
    // change over time.
    // However, since we do not know how many neighbors a subdomain
    // will have (i.e. if nproc < 26 vs. nproc is prime vs. nproc = 10^3)
    // we use a dynamic std::vector here.
    std::vector<rank> neighbor_ranks;

    // Inverse mapping neighbor_rank to index [0, 26) in "neighbor_ranks".
    std::unordered_map<rank, int> neighbor_idx;

    // Associated grid point -- upper right back vertex of subdomain.
    Vec3d gridpoint;
    // The gathered version of "gridpoint", i.e. the gridpoint of every process.
    std::vector<Vec3d> gridpoints;

    // Indices of locally known cells. Local cells before ghost cells.
    std::vector<int> cells;

    globox::GlobalBox<int, int> gbox;
    // Global to local index mapping, defined for local and ghost cells
    std::unordered_map<int, int> global_to_local;

    std::vector<GhostExchangeDesc> exchange_vec;

    // Returns the 8 vertices bounding the subdomain of rank "r"
    std::array<Vec3d, 8> bounding_box(rank r);

    // Neighborhood communicator for load exchange during repart
    MPI_Comm neighcomm;

    // Global cell index to rank mapping
    rank gloidx_to_rank(int gloidx);

    // Initializes the partitioning to a regular Cartesian grid.
    void init_partitioning();
    // Reinitializes the internal data of this class
    void reinit();
    // Initializes "my_dom" and "neighbor_doms"
    void init_octagons();

    // Initializes the neighbor ranks data structures
    void init_neighbors();

    // Returns the center of this subdomain
    Vec3d center_of_load();

    rank cart_topology_position_to_rank(Vec3d pos);
};
} // namespace grids
} // namespace repa

//#endif //HAVE_TETRA