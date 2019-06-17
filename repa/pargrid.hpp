
#pragma once

#include <array>
#include <boost/mpi/communicator.hpp>
#include <map>
#include <memory>
#include <stdexcept>
#include <vector>

#include "common_types.hpp"
#include "repart/metric.hpp"

namespace repa {
namespace grids {

/** Some typedefs to document what an integer is supposed to mean
 */
typedef int rank;  // Rank of a node
typedef int nidx;  // Index of a neighboring process (rank) (0..n_neighbors)
typedef int lidx;  // Index of a local cell (0..n_local_cells)
typedef int gidx;  // Index of a ghost cell (0..n_ghost_cells)
typedef int lgidx; // Index of a local (0..n_local_cells) or ghost cell
                   // (n_local_cells..n_local_cells+n_ghost_cells)

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
    rank dest;               // Destination rank
    std::vector<lgidx> recv; // Ghost cell indices which are to be received
    std::vector<lidx> send;  // Local cell indices which are to be sent

    GhostExchangeDesc() : dest(-1)
    {
    }
    GhostExchangeDesc(rank dest,
                      std::vector<gidx> &&recv,
                      std::vector<lidx> &&send)
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

    virtual ~ParallelLCGrid(){};

    /** Returns the number of local cells.
     */
    virtual lidx n_local_cells() = 0;

    /** Returns the number of ghost cells
     */
    virtual gidx n_ghost_cells() = 0;

    /** Returns the number of neighboring processes over faces, edges and
     * corners
     */
    virtual nidx n_neighbors() = 0;

    /** Returns the rank of a neighbor process.
     * @param i index of neighbor process. 0 <= i < n_neighbors()
     * @throws std::domain_error if 0 > i or i >= n_neighbors().
     */
    virtual rank neighbor_rank(nidx i) = 0;

    /** Returns the cell sizes of Linked Cell grid.
     */
    virtual Vec3d cell_size() = 0;

    /** Returns the number of grid cells of the local process's subdomain in
     * each direction.
     */
    virtual Vec3i grid_size() = 0;

    /** Returns the index of a cell neighboring a given cell (by index).
     *
     * Interpret a neighbor index N the following way:
     * Case 1: 0 <= N < n_local_cells(): local cell N.
     * Case 2: n_local_cells() <= N < n_local_cells() + n_ghost_cells():
     *  ghost cell no. (N - n_local_cells()).
     * Other values for N cannot occur.
     *
     * Neighbor 0 is the cells itself
     * Neighbors 1-13: Half shell neighborhood
     * Neighbors 14-26: Rest of full shell neighborhood
     *
     * @param cellidx Base cell
     * @param neigh Neighbor
     *
     * @throws std::domain_error if 0 > cellidx or cellidx >=
     * get_n_local_cells() or neigh < 0 or neigh >= 26.
     */
    virtual lgidx cell_neighbor_index(lidx cellidx, int neigh) = 0;

    /** Returns the ghost exchange info.
     * @see GhostExchangeDesc
     */
    virtual std::vector<GhostExchangeDesc> get_boundary_info() = 0;

    /** Returns the index of a local cell at position "pos".
     * @throws std::domain_error if position is not in the local subdomain.
     */
    virtual lidx position_to_cell_index(double pos[3]) = 0;

    /** Returns the rank of the process which is responsible for the cell at
     * position "pos". Works for the whole domain!
     */
    virtual rank position_to_rank(double pos[3]) = 0;

    /** Returns the index of a neighboring process which is responsible for the
     * cell at position "pos". "Pos" must therefore be *in the ghost layer*.
     *
     * @throws std::domain_error if position is not in the ghost layer.
     */
    virtual nidx position_to_neighidx(double pos[3]) = 0;

    /** *Maybe* repartitions the grid. Returns true if grid has been changed
     * (repartitioned). This means all data of this class is invalidated.
     * If false is returned, *no* data returned since the last call to
     * repartition() or topology_init() has been invalidated.
     *
     * Be careful: If the call returns true also old cell indices are
     * invalidated and silently get a new meaning.
     *
     * @param exchange_start_callback is a function which starts the data
     * migration. This function is only called if the return value is "true".
     * Also, it is called as soon as "position_to_rank" can safely be called.
     */
    virtual bool repartition(const repart::Metric &m,
                             std::function<void()> exchange_start_callback)
        = 0;

    struct UnknwonCommandError : public std::exception {
        UnknwonCommandError(std::string s)
            : w(std::string("Could not interpret command `") + s
                + std::string("'"))
        {
        }
        virtual const char *what() const noexcept
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

protected:
    boost::mpi::communicator comm, comm_cart;
    Vec3d box_l;
    Vec3i node_grid, node_pos;
    double max_range;
};

} // namespace grids
} // namespace repa