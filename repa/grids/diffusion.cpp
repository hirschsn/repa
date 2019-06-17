
#include "diffusion.hpp"
#include <algorithm>
#include <boost/mpi/nonblocking.hpp>
#include <boost/serialization/array.hpp>
#include <boost/serialization/vector.hpp>
#include <numeric>

#include "util/ensure.hpp"
#include "util/mpi_graph.hpp"
#include "util/push_back_unique.hpp"

#ifndef NDEBUG
#define DIFFUSION_DEBUG
#define DIFFUSION_DEBUG_OUTPUT
#endif

namespace boost {
namespace serialization {
template <typename Archive>
void load(Archive &ar, repa::grids::Diffusion::NeighSend &n,
          const unsigned int /* file_version */)
{
    ar >> n.basecell;
    ar >> n.neighranks;
}

template <typename Archive>
void save(Archive &ar, const repa::grids::Diffusion::NeighSend &n,
          const unsigned int /* file_version */)
{
    ar << n.basecell;
    ar << n.neighranks;
}

template <class Archive>
void serialize(Archive &ar, repa::grids::Diffusion::NeighSend &n,
               const unsigned int file_version)
{
    split_free(ar, n, file_version);
}
} // namespace serialization
} // namespace boost

/** Sets "data[i]" to "val" for all i in the half-open interval
 * ["first_index", "last_index").
 */
template <typename T, typename It>
static void fill_index_range(std::vector<T> &data, It first_index,
                             It last_index, T val)
{
    for (; first_index != last_index; ++first_index) {
        data[*first_index] = val;
    }
}

/*
 * Determines the status of each process (underloaded, overloaded)
 * in the neighborhood given the local load and returns the volume of load to
 * send to each neighbor. On underloaded processes, returns a vector of zeros.
 *
 * This call is collective on neighcomm.
 *
 * See Willebeek Le Mair and Reeves, IEEE Tr. Par. Distr. Sys. 4(9), Sep 1993
 *
 * @param neighcomm Graph communicator which reflects the neighbor relationship
 *                  amongst processes (undirected edges), without edges to the
 *                  process itself.
 * @param load The load of the calling process.
 * @returns Vector of load values ordered according to the neighborhood
 *          ordering in neighcomm.
 */
static std::vector<double> compute_send_volume(MPI_Comm neighcomm, double load)
{
    int nneigh = repa::util::mpi_undirected_neighbor_count(neighcomm);
    // Exchange load in local neighborhood
    std::vector<double> neighloads(nneigh);
    MPI_Neighbor_allgather(&load, 1, MPI_DOUBLE, neighloads.data(), 1,
                           MPI_DOUBLE, neighcomm);

    double avgload
        = std::accumulate(std::begin(neighloads), std::end(neighloads), load)
          / (nneigh + 1);

    // Return empty send volume if this process is underloaded
    if (load < avgload)
        return std::vector<double>(neighloads.size(), 0.0);

    std::vector<double> deficiency(neighloads.size());

    // Calculate deficiency
    for (size_t i = 0; i < neighloads.size(); ++i) {
        deficiency[i] = std::max(avgload - neighloads[i], 0.0);
    }

    auto total_deficiency
        = std::accumulate(std::begin(deficiency), std::end(deficiency), 0.0);
    double overload = load - avgload;

    // Make "deficiency" relative and then scale it to be an
    // absolute part of this process's overload
    for (size_t i = 0; i < neighloads.size(); ++i) {
        deficiency[i] = overload * deficiency[i] / total_deficiency;
    }

    return deficiency;
}

namespace repa {
namespace grids {

lidx Diffusion::n_local_cells()
{
    return localCells;
}

gidx Diffusion::n_ghost_cells()
{
    return ghostCells;
}

nidx Diffusion::n_neighbors()
{
    return neighbors.size();
}

rank Diffusion::neighbor_rank(nidx i)
{
    return neighbors[i];
}

Vec3d Diffusion::cell_size()
{
    return gbox.cell_size();
}

Vec3i Diffusion::grid_size()
{
    return gbox.grid_size();
}

lgidx Diffusion::cell_neighbor_index(lidx cellidx, int neigh)
{
    return global_to_local[gbox.neighbor(cells[cellidx], neigh)];
}

std::vector<GhostExchangeDesc> Diffusion::get_boundary_info()
{
    return exchangeVector;
}

lidx Diffusion::position_to_cell_index(double pos[3])
{
    if (position_to_rank(pos) != comm_cart.rank())
        throw std::domain_error("Particle not in local box");

    return global_to_local[gbox.cell_at_pos(pos)];
}

rank Diffusion::position_to_rank(double pos[3])
{
    auto r = partition[gbox.cell_at_pos(pos)];
    if (r == -1)
        throw std::runtime_error("Cell not in scope.");
    else
        return r;
}

nidx Diffusion::position_to_neighidx(double pos[3])
{
    rank rank = position_to_rank(pos);
    auto ni = std::find(std::begin(neighbors), std::end(neighbors), rank);

    if (ni != std::end(neighbors))
        return std::distance(std::begin(neighbors), ni);
    else
        throw std::runtime_error("Rank not a neighbor.");
}

void Diffusion::clear_unknown_cell_ownership()
{
    for (int i = 0; i < partition.size(); i++) {
        // Erase partition entry if neither cell itself nor any neighbors
        // are on this process
        auto neighborhood = gbox.full_shell_neigh(i);
        if (std::none_of(std::begin(neighborhood), std::end(neighborhood),
                         [this](int cell) {
                             return partition[cell] == comm_cart.rank();
                         }))
            partition[i] = -1;
    }
}

bool Diffusion::repartition(const repart::Metric &m,
                            std::function<void()> exchange_start_callback)
{
    auto cellweights = m();
#ifdef DIFFUSION_DEBUG
    static int nrepartcalls = 0;
    nrepartcalls++;
#endif
    auto nglocells = gbox.ncells();

    clear_unknown_cell_ownership();

    // compute local, estimated load
    double local_load
        = std::accumulate(std::begin(cellweights), std::end(cellweights), 0.0);

#ifdef DIFFUSION_DEBUG_OUTPUT
    std::vector<double> ls(comm_cart.size());
    MPI_Gather(&local_load, 1, MPI_DOUBLE, ls.data(), 1, MPI_DOUBLE, 0,
               comm_cart);
    if (comm_cart.rank() == 0) {
        std::cout << "Loads: ";
        std::copy(std::begin(ls), std::end(ls),
                  std::ostream_iterator<double>(std::cout, " "));

        auto average = std::accumulate(std::begin(ls), std::end(ls), 0.0)
                       / comm_cart.size();
        std::cout << "\tAverage: " << average;

        std::transform(std::begin(ls), std::end(ls), std::begin(ls),
                       [average](double l) { return std::fabs(l - average); });
        double mdev = *std::max_element(std::begin(ls), std::end(ls));
        std::cout << "\tMax dev.: " << mdev << std::endl;

        std::cout << std::endl;
    }
#endif

    std::vector<double> send_volume
        = compute_send_volume(neighcomm, local_load);
#ifdef DIFFUSION_DEBUG
    ENSURE(send_volume.size() == neighbors.size());
#endif

    std::vector<std::vector<int>> toSend(neighbors.size());

    if (std::any_of(std::begin(send_volume), std::end(send_volume),
                    [](double d) { return d > 0.0; })) {

#ifdef DIFFUSION_DEBUG_OUTPUT
        std::cout << "[" << comm_cart.rank() << "] Send volume: ";
        std::copy(std::begin(send_volume), std::end(send_volume),
                  std::ostream_iterator<double>(std::cout, " "));
        std::cout << std::endl;
#endif

        // Create list of border cells which can be send to neighbors.
        // First element of each vector is the rank to which the sells should
        // be send. The other elements in the vectors are the global cell IDs
        // of the cells.
        toSend = compute_send_list(std::move(send_volume), m);

        // Update partition array
        for (size_t i = 0; i < toSend.size(); ++i) {
            fill_index_range(partition, std::begin(toSend[i]),
                             std::end(toSend[i]), neighbors[i]);
        }
    }

    //
    // First communication step
    // Send *all* vectors in "toSend" to *all* neighbors.
    // (Not only their respective receive volumes.)
    // This is used to avoid inconsistencies, especially at newly created
    // neighborhood relationships
    //
    std::vector<boost::mpi::request> sreq_cells(neighbors.size());
    std::vector<boost::mpi::request> rreq_cells(neighbors.size());

    for (int i = 0; i < neighbors.size(); ++i) {
        //#ifdef DIFFUSION_DEBUG
        //    std::printf("Repart %i. [%i] Sending %zu cells to %i\n",
        //    nrepartcalls, comm_cart.rank(), toSend[i].size(), neighbors[i]);
        //#endif
        // Push back the rank, that is save an extra communication of
        // "neighbors" and interleave it into "toSend"
        toSend[i].push_back(neighbors[i]);
    }

    // Extra loop as all ranks need to be added before sending
    for (int i = 0; i < neighbors.size(); ++i) {
        sreq_cells[i] = comm_cart.isend(neighbors[i], 2, toSend);
    }

    // All send volumes from all processes
    std::vector<std::vector<std::vector<int>>> received_cells(neighbors.size());
    for (int i = 0; i < neighbors.size(); ++i) {
        rreq_cells[i] = comm_cart.irecv(neighbors[i], 2, received_cells[i]);
    }

    boost::mpi::wait_all(std::begin(rreq_cells), std::end(rreq_cells));

    //#ifdef DIFFUSION_DEBUG
    //  std::printf("Received_cells.size() = %zu Neighbors.size() = %zu\n",
    //  received_cells.size(), neighbors.size()); for (size_t from = 0; from <
    //  received_cells.size(); ++from) {
    //    for (size_t to = 0; to < received_cells[from].size(); ++to) {
    //      std::printf("received_cells[%zu][%zu].size() = %zu", from, to,
    //      received_cells[from][to].size()); std::fflush(stdout);
    //
    //      if (received_cells[from][to].size() > 0)
    //        std::printf("; last element = %i",
    //        received_cells[from][to].back());
    //
    //      std::printf("\n");
    //      std::fflush(stdout);
    //    }
    //  }
    //#endif

    // Update the partition entry for all received cells.
    for (size_t from = 0; from < received_cells.size(); ++from) {
        for (size_t to = 0; to < received_cells[from].size(); ++to) {
            // Extract target rank, again.
            int target_rank = received_cells[from][to].back();
            received_cells[from][to].pop_back();

            fill_index_range(partition, std::begin(received_cells[from][to]),
                             std::end(received_cells[from][to]), target_rank);
        }
    }

    boost::mpi::wait_all(std::begin(sreq_cells), std::end(sreq_cells));

    //
    // END of first communication step
    //
#ifdef DIFFUSION_DEBUG
    std::vector<int> p2 = partition;
    for (auto &el : p2)
        if (el != comm_cart.rank())
            el = -1;

    MPI_Allreduce(MPI_IN_PLACE, p2.data(), p2.size(), MPI_INT, MPI_MAX,
                  comm_cart);
    for (auto el : p2)
        ENSURE(el > -1);
#endif

    // Remove ranks from "toSend", again.
    for (int i = 0; i < neighbors.size(); ++i)
        toSend[i].pop_back();

    //
    // Second communication Step
    // Send neighbourhood of sent cells.
    //
    std::vector<boost::mpi::request> rreq_neigh(neighbors.size());
    std::vector<boost::mpi::request> sreq_neigh(neighbors.size());

    auto sendVectors = sendNeighbourhood(toSend);

    for (int i = 0; i < neighbors.size(); ++i) {
        sreq_neigh[i] = comm_cart.isend(neighbors[i], 2, sendVectors[i]);
    }

    // All send volumes from all processes
    std::vector<std::vector<NeighSend>> received_neighborhood(neighbors.size());
    for (int i = 0; i < neighbors.size(); ++i) {
        rreq_neigh[i]
            = comm_cart.irecv(neighbors[i], 2, received_neighborhood[i]);
    }

    boost::mpi::wait_all(std::begin(rreq_neigh), std::end(rreq_neigh));

    // Update neighbourhood
    updateReceivedNeighbourhood(received_neighborhood);

    boost::mpi::wait_all(std::begin(sreq_neigh), std::end(sreq_neigh));

#ifdef DIFFUSION_DEBUG
    for (int i = 0; i < partition.size(); ++i) {
        if (partition[i] != comm_cart.rank())
            continue;

        for (int j = 0; j < 27; ++j) {
            int n = gbox.neighbor(i, j);
            ENSURE(partition[n] > -1);
        }
    }
#endif

    exchange_start_callback();

    // Based on partition array the local structures will be rebuilded
    reinit();

    return true;
}
/*
 * Initialization
 */
Diffusion::Diffusion(const boost::mpi::communicator &comm, Vec3d box_size,
                     double min_cell_size)
    : ParallelLCGrid(comm, box_size, min_cell_size),
      gbox(box_size, min_cell_size), neighcomm(MPI_COMM_NULL)
{
    auto nglocells = gbox.ncells();
    partition.resize(nglocells);
    // Build and fill partitions-array initial
    for (int i = 0; i < nglocells; ++i) {
        partition[i] = i * comm_cart.size() / nglocells;
    }

    // Initialize local part of structure
    reinit(true);
}

/*
 * Computes a vector of vectors. The inner vectors contain a rank of the
 * process where the cells shall send and the cellids of this cells.
 */
std::vector<std::vector<int>>
Diffusion::compute_send_list(std::vector<double> &&send_loads,
                             const repart::Metric &m)
{
    auto weights = m();
    std::vector<std::tuple<int, double, int>> plist;
    for (size_t i = 0; i < borderCells.size(); i++) {
        // Profit when sending this cell away
        double profit = weights[borderCells[i]];

        // Additional cell communication induced if this cell is sent away
        int nadditional_comm = 0;
        for (int neighCell :
             gbox.full_shell_neigh_without_center(cells[borderCells[i]])) {
            if (partition[neighCell] == comm_cart.rank()
                && std::find(std::begin(borderCells), std::end(borderCells),
                             global_to_local[neighCell])
                       != std::end(borderCells)) {
                nadditional_comm++;
            }
        }
#ifdef DIFFUSION_DEBUG
        ENSURE(nadditional_comm < 27);
#endif

        if (profit > 0)
            plist.emplace_back(27 - nadditional_comm, profit, borderCells[i]);
    }

    std::vector<std::vector<int>> to_send(send_loads.size());

    // Use a maxheap: Always draw the maximum element
    // (1. least new border cells, 2. most profit)
    // and find a process that can take this cell.
    std::make_heap(std::begin(plist), std::end(plist));
    while (!plist.empty()) {
        std::pop_heap(std::begin(plist), std::end(plist));
        lidx cidx = std::get<2>(plist.back());
        plist.pop_back();

        for (auto neighrank : borderCellsNeighbors[cidx]) {
            auto neighidx
                = std::distance(std::begin(neighbors),
                                std::find(std::begin(neighbors),
                                          std::end(neighbors), neighrank));

            if (weights[cidx] <= send_loads[neighidx]) {
                to_send[neighidx].push_back(cells[cidx]);
                send_loads[neighidx] -= weights[cidx];
                // This cell is done. Continue with the next.
                break;
            }
        }
    }

    return to_send;
}

std::vector<std::vector<Diffusion::NeighSend>>
Diffusion::sendNeighbourhood(const std::vector<std::vector<int>> &toSend)
{
    std::vector<std::vector<NeighSend>> sendVectors(toSend.size());
    for (size_t i = 0; i < toSend.size(); ++i) {
        sendVectors[i].resize(toSend[i].size());
        for (size_t j = 0; j < toSend[i].size(); ++j) {
            sendVectors[i][j].basecell = toSend[i][j];
            int k = 0;
            for (int n : gbox.full_shell_neigh_without_center(
                     sendVectors[i][j].basecell)) {
                sendVectors[i][j].neighranks[k] = partition[n];
                k++;
            }
        }
    }

    return sendVectors;
}

/*
 * Based on neighbourhood, received in function "receiveNeighbourhood",
 * partition array is updated. (Only neighbourhood is changed)
 */
void Diffusion::updateReceivedNeighbourhood(
    const std::vector<std::vector<NeighSend>> &neighs)
{
    for (size_t i = 0; i < neighs.size(); ++i) {
        for (size_t j = 0; j < neighs[i].size(); ++j) {
            int basecell = neighs[i][j].basecell;
            int k = 0;
            for (int n : gbox.full_shell_neigh_without_center(basecell)) {
                partition[n] = neighs[i][j].neighranks[k++];
            }
        }
    }
}

/*
 * Rebuild the data structures describing subdomain and communication.
 */
void Diffusion::reinit(bool init)
{
    const int nglocells = partition.size();

    localCells = 0;
    ghostCells = 0;
    cells.clear();
    global_to_local.clear();
    neighbors.clear();
    borderCells.clear();
    borderCellsNeighbors.clear();

    // Extract the local cells from "partition".
    for (int i = 0; i < nglocells; i++) {
        if (partition[i] == comm_cart.rank()) {
            // Vector of own cells
            cells.push_back(i);
            // Index mapping from global to local
            global_to_local[i] = localCells;
            // Number of own cells
            localCells++;
        }
        else if (!init && partition[i] != -1) {
            // Erase partition entry if neither cell itself nor any neighbors
            // are on this process
            auto neighborhood = gbox.full_shell_neigh(i);
            if (std::none_of(std::begin(neighborhood), std::end(neighborhood),
                             [this](int cell) {
                                 return partition[cell] == comm_cart.rank();
                             }))
                partition[i] = -1;
        }
    }

    // Temporary storage for exchange descriptors.
    // Will be filled only for neighbors
    // and moved from later.
    std::vector<GhostExchangeDesc> tmp_ex_descs(comm_cart.size());

    // Determine ghost cells and communication volume
    for (int i = 0; i < localCells; i++) {
        for (int neighborIndex :
             gbox.full_shell_neigh_without_center(cells[i])) {
            rank owner = static_cast<rank>(partition[neighborIndex]);
            if (owner == comm_cart.rank())
                continue;

            // First cell identifying "i" as border cell?
            if (borderCells.empty() || borderCells.back() != i)
                borderCells.push_back(i);
            util::push_back_unique(borderCellsNeighbors[i], owner);

            // Find ghost cells. Add only once to "cells" vector.
            // Global_to_local has an entry, if this ghost cell has already
            // been visited
            if (global_to_local.find(neighborIndex)
                == std::end(global_to_local)) {
                // Add ghost cell to cells vector
                cells.push_back(neighborIndex);
                // Index mapping from global to ghost
                global_to_local[neighborIndex] = localCells + ghostCells;
                // Number of ghost cells
                ghostCells++;
            }

            // Initialize exdesc and add "rank" as neighbor if unknown.
            if (tmp_ex_descs[owner].dest == -1) {
                neighbors.push_back(owner);
                tmp_ex_descs[owner].dest = owner;
            }

            util::push_back_unique(tmp_ex_descs[owner].recv, neighborIndex);
            util::push_back_unique(tmp_ex_descs[owner].send, cells[i]);
        }
    }

    // Move all existent exchange descriptors from "tmp_ex_descs" to
    // "exchangeVector".
    exchangeVector.clear();
    for (int i = 0; i < comm_cart.size(); ++i) {
        if (tmp_ex_descs[i].dest != -1) {
            auto ed = std::move(tmp_ex_descs[i]);

            // Make sure, index ordering is the same on every process
            // and global to local index conversion
            std::sort(std::begin(ed.recv), std::end(ed.recv));
            std::transform(std::begin(ed.recv), std::end(ed.recv),
                           std::begin(ed.recv),
                           [this](int i) { return global_to_local[i]; });
            std::sort(std::begin(ed.send), std::end(ed.send));
            std::transform(std::begin(ed.send), std::end(ed.send),
                           std::begin(ed.send),
                           [this](int i) { return global_to_local[i]; });

            exchangeVector.push_back(std::move(ed));
        }
    }

#ifdef DIFFUSION_DEBUG
    for (int i = 0; i < comm_cart.size(); ++i) {
        if (tmp_ex_descs[i].dest != -1)
            ENSURE(tmp_ex_descs[i].recv.size() == 0
                   && tmp_ex_descs[i].send.size() == 0);
    }
#endif

    // Create graph comm with current process structure
    if (neighcomm != MPI_COMM_NULL)
        MPI_Comm_free(&neighcomm);
    // Edges to all processes in "neighbors"
    MPI_Dist_graph_create_adjacent(
        comm_cart, neighbors.size(), neighbors.data(),
        static_cast<const int *>(MPI_UNWEIGHTED), neighbors.size(),
        neighbors.data(), static_cast<const int *>(MPI_UNWEIGHTED),
        MPI_INFO_NULL, 0, &neighcomm);
#ifdef DIFFUSION_DEBUG
    int indegree = 0, outdegree = 0, weighted = 0;
    MPI_Dist_graph_neighbors_count(neighcomm, &indegree, &outdegree, &weighted);
    ENSURE(!weighted);
    ENSURE(static_cast<size_t>(indegree) == neighbors.size());
    ENSURE(static_cast<size_t>(outdegree) == neighbors.size());
    std::vector<int> __ineighs(indegree, -1), __iw(indegree, -1);
    std::vector<int> __oneighs(outdegree, -1), __ow(outdegree, -1);
    MPI_Dist_graph_neighbors(neighcomm, indegree, __ineighs.data(), __iw.data(),
                             outdegree, __oneighs.data(), __ow.data());
    for (size_t i = 0; i < neighbors.size(); ++i) {
        ENSURE(__ineighs[i] == neighbors[i]);
        ENSURE(__oneighs[i] == neighbors[i]);
    }
#endif
}

} // namespace grids
} // namespace repa