
#pragma once

#include "../pargrid.hpp"
#include <array>
#include <vector>

namespace repa {
namespace grids {

/** TODO: comment
 *
 * Cells are ordered on the ghost grid according to a simple row-wise ordering
 * (see impl::linearize). All 3d indices live on the ghost grid, i.e.
 * their values range from {0, 0, 0} to m_ghost_grid.
 * Where {0, 0, 0} is the first ghost cell, {1, 1, 1} the first inner (i.e.
 * boundary) cell and so on.
 */
struct CartGrid : public ParallelLCGrid {
    CartGrid(const boost::mpi::communicator &comm,
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

private:
    // Cell size (box_l / m_grid_size)
    std::array<double, 3> m_cell_size, m_inv_cell_size;
    // No of (ghost) cells on this node
    Vec3i m_grid_size, m_ghost_grid_size;

    // Number of processes in direction i
    Vec3i m_procgrid, m_procgrid_pos;

    // Lower left corner of this process
    Vec3d m_lowerleft, m_localbox;

    // comm data structures
    std::vector<GhostExchangeDesc> m_exdescs;
    std::vector<rank> m_neighranks; // Unique ranks in m_rank_in_dir

    // Permutation of linearized indices to ensure cell ordering.
    // m_to_pargrid_order orders local cells before ghost cells, i.e.
    // m_to_pargrid_order[i] < n_local_cells() if i is a local cell.
    // m_to_pargrid_order[i] >= n_local_cells() if i is ghost cell.
    // m_from_pargrid_order is just the inverse permutation.
    std::vector<lgidx> m_to_pargrid_order, m_from_pargrid_order;

    lgidx linearize(Vec3i c);
    Vec3i unlinearize(lgidx cidx);

    // rank cell_to_rank(const Vec3i& c);
    nidx neighbor_idx(rank r);
    // rank cell_to_neighidx(const Vec3i& c);
    rank proc_offset_to_rank(const Vec3i &offset);

    void
    fill_comm_cell_lists(std::vector<int> &v, const Vec3i &lc, const Vec3i &hc);
    void create_index_permutations();
    void create_grid();
    void prepare_communication();
    void fill_neighranks();

    bool is_ghost_cell(const Vec3i &c);
};
} // namespace grids
} // namespace repa