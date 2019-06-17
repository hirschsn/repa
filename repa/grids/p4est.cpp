
//#ifdef HAVE_P4EST

#include <numeric>

#include <p8est_algorithms.h>
#include <p8est_bits.h>
#include <p8est_extended.h>

#include "p4est.hpp"

#include "_compat.hpp"

#ifdef __BMI2__
#include <x86intrin.h>
#endif

namespace repa {
namespace grids {

namespace impl {

RepartState::RepartState(const boost::mpi::communicator &comm_cart)
    : comm_cart(comm_cart),
      after_repart(false),
      nquads_per_proc(comm_cart.size())
{
}

void RepartState::reset()
{
    after_repart = false;
    std::fill(std::begin(nquads_per_proc), std::end(nquads_per_proc),
              static_cast<p4est_locidx_t>(0));
}

void RepartState::inc_nquads(rank proc)
{
    nquads_per_proc[proc]++;
}

void RepartState::allreduce()
{
    MPI_Allreduce(MPI_IN_PLACE, nquads_per_proc.data(), comm_cart.size(),
                  P4EST_MPI_LOCIDX, MPI_SUM, comm_cart);
}

// Returns the number of trailing zeros in an integer x.
static inline int count_trailing_zeros(int x)
{
    int z = 0;
    for (; (x & 1) == 0; x >>= 1)
        z++;
    return z;
}

// Creates the bitmask for the integer LocalShell::boundary from a given
// x, y, z coordinate.
static int local_boundary_bitset(int x, int y, int z, int grid_size[3])
{
    int ret = 0;
    if (PERIODIC(0) && x == 0)
        ret |= 1;
    if (PERIODIC(0) && x == grid_size[0] - 1)
        ret |= 2;
    if (PERIODIC(1) && y == 0)
        ret |= 4;
    if (PERIODIC(1) && y == grid_size[1] - 1)
        ret |= 8;
    if (PERIODIC(2) && z == 0)
        ret |= 16;
    if (PERIODIC(2) && z == grid_size[2] - 1)
        ret |= 32;
    return ret;
}

// Returns a global SFC-curve index for a given cell.
// Note: This is a global index on the Z-curve and not a local cell index to
// cells.
gidx cell_morton_idx(int x, int y, int z)
{
#ifdef __BMI2__
    static constexpr unsigned mask_x = 0x49249249;
    static constexpr unsigned mask_y = 0x92492492;
    static constexpr unsigned mask_z = 0x24924924;
    return _pdep_u32(x, mask_x) | _pdep_u32(y, mask_y) | _pdep_u32(z, mask_z);
#else
    gidx idx = 0;
    int pos = 1;

    for (int i = 0; i < 21; ++i) {
        if ((x & 1))
            idx += pos;
        x >>= 1;
        pos <<= 1;
        if ((y & 1))
            idx += pos;
        y >>= 1;
        pos <<= 1;
        if ((z & 1))
            idx += pos;
        z >>= 1;
        pos <<= 1;
    }

    return idx;
#endif
}

// Maps a position to the cartesian grid and returns the morton index of this
// coordinates
// Note: This is a global index on the Z-curve and not a local cell index to
// cells.
gidx pos_morton_idx(double box_l[3],
                    double pos[3],
                    double cell_size[3],
                    double inv_cell_size[3])
{
    for (int d = 0; d < 3; ++d) {
        double errmar = 0.5 * ROUND_ERROR_PREC * box_l[d];
        if (pos[d] < 0 && pos[d] > -errmar)
            pos[d] = 0;
        else if (pos[d] >= box_l[d] && pos[d] < box_l[d] + errmar)
            pos[d] = pos[d] - 0.5 * cell_size[d];
        // In the other two cases ("pos[d] <= -errmar" and
        // "pos[d] >= box_l[d] + errmar") pos is correct.
    }

    return impl::cell_morton_idx(pos[0] * inv_cell_size[0],
                                 pos[1] * inv_cell_size[1],
                                 pos[2] * inv_cell_size[2]);
}

} // namespace impl

// Compute the grid- and bricksize according to box_l and maxrange
void P4estGrid::set_optimal_cellsize()
{
    Vec3i ncells = {{1, 1, 1}};

    // Compute number of cells
    if (max_range > ROUND_ERROR_PREC * box_l[0]) {
        ncells[0] = std::max<int>(box_l[0] / max_range, 1);
        ncells[1] = std::max<int>(box_l[1] / max_range, 1);
        ncells[2] = std::max<int>(box_l[2] / max_range, 1);
    }

    m_grid_size[0] = ncells[0];
    m_grid_size[1] = ncells[1];
    m_grid_size[2] = ncells[2];

    // Divide all dimensions by biggest common power of 2
    m_grid_level
        = impl::count_trailing_zeros(ncells[0] | ncells[1] | ncells[2]);

    m_brick_size[0] = ncells[0] >> m_grid_level;
    m_brick_size[1] = ncells[1] >> m_grid_level;
    m_brick_size[2] = ncells[2] >> m_grid_level;
}

void P4estGrid::create_grid()
{
    set_optimal_cellsize();

    m_cell_size[0] = box_l[0] / m_grid_size[0];
    m_cell_size[1] = box_l[1] / m_grid_size[1];
    m_cell_size[2] = box_l[2] / m_grid_size[2];

    m_inv_cell_size[0] = 1.0 / m_cell_size[0];
    m_inv_cell_size[1] = 1.0 / m_cell_size[1];
    m_inv_cell_size[2] = 1.0 / m_cell_size[2];

    if (!m_repartstate.after_repart) {
        // Keep old connectivity as the p4est destructor needs it
        auto oldconn = std::move(m_p8est_conn);
        // Create p8est structures
        m_p8est_conn = std::unique_ptr<p8est_connectivity_t>(
            p8est_connectivity_new_brick(m_brick_size[0], m_brick_size[1],
                                         m_brick_size[2], PERIODIC(0),
                                         PERIODIC(1), PERIODIC(2)));
        m_p8est = std::unique_ptr<p8est_t>(
            p8est_new_ext(comm_cart, m_p8est_conn.get(), 0, m_grid_level, true,
                          0, NULL, NULL));
    }

    // Information about first quads of each node
    // Assemble this as early as possible as it is necessary for
    // position_to_rank. As soon as this information is ready, we can start
    // migrating particles.
    m_node_first_cell_idx.resize(comm_cart.size() + 1);
    for (int i = 0; i < comm_cart.size(); ++i) {
        p8est_quadrant_t *q = &m_p8est->global_first_position[i];
        double xyz[3];
        p8est_qcoord_to_vertex(m_p8est_conn.get(), q->p.which_tree, q->x, q->y,
                               q->z, xyz);
        m_node_first_cell_idx[i] = impl::cell_morton_idx(
            xyz[0] * (1 << m_grid_level), xyz[1] * (1 << m_grid_level),
            xyz[2] * (1 << m_grid_level));
    }

    // Total number of quads
    int tmp = 1 << m_grid_level;
    while (tmp < m_grid_size[0] || tmp < m_grid_size[1] || tmp < m_grid_size[2])
        tmp <<= 1;
    m_node_first_cell_idx[comm_cart.size()] = tmp * tmp * tmp;

    if (m_repartstate.after_repart)
        m_repartstate.exchange_start_callback();

    auto p8est_ghost = std::unique_ptr<p8est_ghost_t>(
        p8est_ghost_new(m_p8est.get(), P8EST_CONNECT_CORNER));
    auto p8est_mesh = std::unique_ptr<p8est_mesh_t>(p8est_mesh_new_ext(
        m_p8est.get(), p8est_ghost.get(), 1, 1, 0, P8EST_CONNECT_CORNER));

    m_num_local_cells = m_p8est->local_num_quadrants;
    m_num_ghost_cells = p8est_ghost->ghosts.elem_count;
    int num_cells = m_num_local_cells + m_num_ghost_cells;

    std::unique_ptr<sc_array_t> ni
        = std::unique_ptr<sc_array_t>(sc_array_new(sizeof(int)));
    // Collect info about local cells
    m_p8est_shell.clear(); // Need to clear because we push_back
    m_p8est_shell.reserve(num_cells);
    for (int i = 0; i < m_num_local_cells; ++i) {
        p8est_quadrant_t *q
            = p8est_mesh_get_quadrant(m_p8est.get(), p8est_mesh.get(), i);
        double xyz[3];
        p8est_qcoord_to_vertex(m_p8est_conn.get(), p8est_mesh->quad_to_tree[i],
                               q->x, q->y, q->z, xyz);

        int ql = 1 << p8est_tree_array_index(m_p8est->trees,
                                             p8est_mesh->quad_to_tree[i])
                          ->maxlevel;
        int x = xyz[0] * ql;
        int y = xyz[1] * ql;
        int z = xyz[2] * ql;

        // Cell on domain boundaries?
        int bndry = impl::local_boundary_bitset(x, y, z, m_grid_size.data());
        m_p8est_shell.emplace_back(i, comm_cart.rank(),
                                   bndry ? impl::CellType::boundary
                                         : impl::CellType::inner,
                                   bndry, x, y, z);

        // Neighborhood
        for (int n = 0; n < 26; ++n) {
            m_p8est_shell[i].neighbor[n] = -1;
            p8est_mesh_get_neighbors(m_p8est.get(), p8est_ghost.get(),
                                     p8est_mesh.get(), i, n, NULL, NULL,
                                     ni.get());
            // Fully periodic, regular grid.
            if (ni->elem_count != 1)
                throw std::runtime_error("Error in neighborhood search.");

            int neighrank = *(int *)sc_array_index_int(ni.get(), 0);
            m_p8est_shell[i].neighbor[n] = neighrank;

            if (neighrank >= m_p8est->local_num_quadrants) {
                // Ghost cell on inner subdomain boundaries
                m_p8est_shell[i].shell = impl::CellType::boundary;
            }
            sc_array_truncate(ni.get());
        }
    }

    // Collect info about ghost cells
    for (int g = 0; g < m_num_ghost_cells; ++g) {
        p8est_quadrant_t *q
            = p8est_quadrant_array_index(&p8est_ghost->ghosts, g);
        double xyz[3];
        p8est_qcoord_to_vertex(m_p8est_conn.get(), q->p.piggy3.which_tree, q->x,
                               q->y, q->z, xyz);

        int ql = 1 << p8est_tree_array_index(m_p8est->trees,
                                             q->p.piggy3.which_tree)
                          ->maxlevel;
        int x = xyz[0] * ql;
        int y = xyz[1] * ql;
        int z = xyz[2] * ql;

        m_p8est_shell.emplace_back(g, p8est_mesh->ghost_to_proc[g],
                                   impl::CellType::ghost, 0, x, y, z);
    }
}

void P4estGrid::prepare_communication()
{
    int num_cells = n_local_cells() + n_ghost_cells();
    // List of cell indices for each process for send/recv
    std::vector<std::vector<lidx>> send_idx(comm_cart.size());
    std::vector<std::vector<gidx>> recv_idx(comm_cart.size());

    // Find all cells to be sent or received
    for (int i = 0; i < num_cells; ++i) {
        // Ghost cell? -> add to recv lists
        if (m_p8est_shell[i].shell == impl::CellType::ghost) {
            int nrank = m_p8est_shell[i].rank;
            if (nrank >= 0)
                recv_idx[nrank].push_back(i);
        }
        // Boundary cell? -> add to send lists
        if (m_p8est_shell[i].shell == impl::CellType::boundary) {
            // Add to all possible neighbors
            for (int n = 0; n < 26; ++n) {
                int nidx = m_p8est_shell[i].neighbor[n];
                // Invalid neighbor?
                if (nidx < 0
                    || m_p8est_shell[nidx].shell != impl::CellType::ghost)
                    continue;

                int nrank = m_p8est_shell[nidx].rank;
                if (nrank < 0)
                    continue;

                // Several neighbors n can be on the same process, therefore we
                // have to pay attention to add it only once.
                if (send_idx[nrank].empty() || send_idx[nrank].back() != i)
                    send_idx[nrank].push_back(i);
            }
        }
    }

    // Count the number of communications and assign indices for the
    // communication
    int num_comm_proc = 0;
    std::vector<int> comm_proc(comm_cart.size(), -1);
    for (int i = 0; i < comm_cart.size(); ++i) {
        if (send_idx[i].size() != 0 && recv_idx[i].size() != 0)
            comm_proc[i] = num_comm_proc++;
        else if (!(send_idx[i].size() == 0 && recv_idx[i].size() == 0))
            throw std::runtime_error(
                "Unexpected mismatch in send and receive lists.\n");
    }

    // Assemble ghost exchange descriptors
    m_exdescs.resize(num_comm_proc);
    m_neighranks.resize(num_comm_proc);
    for (int n = 0; n < comm_cart.size(); ++n) {
        if (comm_proc[n] == -1)
            continue;
        int index = comm_proc[n];
        m_neighranks[index] = n;
        m_exdescs[index].dest = n;
        m_exdescs[index].recv = std::move(recv_idx[n]);
        m_exdescs[index].send = std::move(send_idx[n]);
    }
}

void P4estGrid::reinitialize()
{
    create_grid();
    prepare_communication();
}

P4estGrid::P4estGrid(const boost::mpi::communicator &comm,
                     Vec3d box_size,
                     double min_cell_size)
    : ParallelLCGrid(comm, box_size, min_cell_size), m_repartstate(comm_cart)
{
    reinitialize();
}

lidx P4estGrid::n_local_cells()
{
    return m_num_local_cells;
}

gidx P4estGrid::n_ghost_cells()
{
    return m_num_ghost_cells;
}

nidx P4estGrid::n_neighbors()
{
    return m_neighranks.size();
}

rank P4estGrid::neighbor_rank(nidx i)
{
    if (i < 0 || i > m_neighranks.size())
        throw std::domain_error("Neighbor rank out of bounds.");
    return m_neighranks[i];
}

lgidx P4estGrid::cell_neighbor_index(lidx cellidx, int neigh)
{
    // Indices of the half shell neighbors in m_p8est_shell
    static const std::array<int, 14> hs_idxs
        = {{-1, 1, 16, 3, 17, 22, 8, 23, 12, 5, 13, 24, 9, 25}};
    // [0, 25] \ hs_idxs, No. 26 would be the cell itself which is not stored in
    // m_p8est_shell[i].neighbor
    static const std::array<int, 13> fs_idxs
        = {{0, 2, 4, 6, 7, 10, 11, 14, 15, 18, 19, 20, 21}};

    if (cellidx < 0 || cellidx > n_local_cells())
        throw std::domain_error("Cell index outside of local subdomain");

    if (neigh == 0)
        return cellidx;
    else if (neigh > 0 && neigh < 14)
        return m_p8est_shell[cellidx].neighbor[hs_idxs[neigh]];
    else if (neigh >= 14 && neigh < 27)
        return m_p8est_shell[cellidx].neighbor[fs_idxs[neigh - hs_idxs.size()]];
    else
        throw std::domain_error("Neighbor index outside of [0, 26]");
}

std::vector<GhostExchangeDesc> P4estGrid::get_boundary_info()
{
    return m_exdescs;
}

lidx P4estGrid::position_to_cell_index(double pos[3])
{
    auto shellidxcomp = [](const impl::LocalShell &s, int idx) {
        int64_t sidx
            = impl::cell_morton_idx(s.coord[0], s.coord[1], s.coord[2]);
        return sidx < idx;
    };

    auto needle = impl::pos_morton_idx(box_l.data(), pos, m_cell_size.data(),
                                       m_inv_cell_size.data());

    auto shell_local_end = std::begin(m_p8est_shell) + n_local_cells();
    auto it
        = std::lower_bound(std::begin(m_p8est_shell),
                           // Only take into account local cells!
                           // This cannot be extended to ghost cells as these
                           // are not stored in SFC order in m_p8est_shell.
                           shell_local_end, needle, shellidxcomp);

    if (it != shell_local_end &&
        // Exclude finding cell 0 (lower_bound) if 0 is not the wanted result
        impl::cell_morton_idx(it->coord[0], it->coord[1], it->coord[2])
            == needle)
        return std::distance(std::begin(m_p8est_shell), it);
    else
        throw std::domain_error("Pos not in local domain.");
}

rank P4estGrid::position_to_rank(double pos[3])
{
    // Cell of pos might not be known on this process (not in m_p8est_shell).
    // Therefore, use the global first cell indices.
    auto it = std::upper_bound(
        std::begin(m_node_first_cell_idx), std::end(m_node_first_cell_idx),
        impl::pos_morton_idx(box_l.data(), pos, m_cell_size.data(),
                             m_inv_cell_size.data()),
        [](int i, int64_t idx) { return i < idx; });

    return std::distance(std::begin(m_node_first_cell_idx), it) - 1;
}

nidx P4estGrid::position_to_neighidx(double pos[3])
{
    // Determine the neighbor rank for locally known cell
    // Using position_to_rank here as it is the simpler code. Could also
    // search the neighboring cells of the cell where pos lies in.
    auto rank = position_to_rank(pos);

    // Search this rank in the local neighbor list and return its index
    auto it = std::lower_bound(std::begin(m_neighranks), std::end(m_neighranks),
                               rank);
    if (*it != rank)
        throw std::runtime_error("Position not in a ghost cell.");
    return std::distance(std::begin(m_neighranks), it);
}

std::array<double, 3> P4estGrid::cell_size()
{
    return m_cell_size;
}

Vec3i P4estGrid::grid_size()
{
    return m_grid_size;
}

bool P4estGrid::repartition(const repart::Metric &m,
                            std::function<void()> exchange_start_callback)
{
    // If this method exits early, successive calls to reinitialize() will
    // partition the grid uniformly.
    m_repartstate.reset();

    std::vector<double> weights = m();

    // Determine prefix and target load
    double localsum
        = std::accumulate(std::begin(weights), std::end(weights), 0.0);
    double sum, prefix = 0; // Initialization is necessary on rank 0!
    MPI_Allreduce(&localsum, &sum, 1, MPI_DOUBLE, MPI_SUM, comm_cart);
    MPI_Exscan(&localsum, &prefix, 1, MPI_DOUBLE, MPI_SUM, comm_cart);
    double target = sum / comm_cart.size();

    // Determine new process boundaries in local subdomain
    // Evaluated for its side effect of setting part_nquads.
    std::accumulate(std::begin(weights), std::end(weights), prefix,
                    [this, target](double cellpref, double weight) {
                        int proc = std::min<int>(cellpref / target,
                                                 comm_cart.size() - 1);
                        m_repartstate.inc_nquads(proc);
                        return cellpref + weight;
                    });

    m_repartstate.allreduce();

    // TODO: Could try to steal quads from neighbors.
    //       Global reshifting (i.e. stealing from someone else than the direct
    //       neighbors) is not a good idea since it globally changes the metric.
    //       Anyways, this is most likely due to a bad quad/proc quotient.
    if (m_repartstate.nquads_per_proc[comm_cart.rank()] == 0) {
        fprintf(
            stderr,
            "[%i] No quads assigned to me. Cannot guarantee to work. Exiting\n",
            comm_cart.rank());
        fprintf(stderr,
                "[%i] Try changing the metric or reducing the number of "
                "processes\n",
                comm_cart.rank());
        errexit();
    }

    // Reinitialize the grid and prepare its internal datastructures for
    // querying by generic_dd.
    m_repartstate.after_repart = true;
    m_repartstate.exchange_start_callback = exchange_start_callback;

    p8est_partition_given(m_p8est.get(), m_repartstate.nquads_per_proc.data());
    reinitialize();

    return true;
}
} // namespace grids
} // namespace repa

//#endif // HAVE_P4EST