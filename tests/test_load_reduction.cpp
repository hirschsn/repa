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

/**
 * Checks for the correct creation of supported pargrids.
 */

#define BOOST_TEST_MODULE load_reduction
#include <boost/test/included/unit_test.hpp>

#include "testenv.hpp"
#include <boost/mpi/collectives.hpp>
#include <boost/mpi/communicator.hpp>
#include <boost/mpi/environment.hpp>
#include <repa/repa.hpp>

bool if_then(bool a, bool b)
{
    return !a || b;
}

bool is_overloaded_process(const boost::mpi::communicator &comm)
{
    return comm.rank() < comm.size() / 2;
}

struct StrictImbalanceReductionTest {
    void operator()(const testenv::TEnv &t,
                    repa::grids::ParallelLCGrid *grid,
                    repa::GridType gt)
    {
        ncalls++;
        if (ncalls == 1) {
            initial_nlocalcells = grid->n_local_cells();
        }
        else if (ncalls >= 2) {
            BOOST_CHECK(if_then(is_overloaded_process(t.comm()),
                                grid->n_local_cells() < initial_nlocalcells));
        }
    }

private:
    int ncalls = 0;
    repa::grids::local_cell_index_type initial_nlocalcells;
};

struct WeakImbalanceReductionTest {
    void operator()(const testenv::TEnv &t,
                    repa::grids::ParallelLCGrid *grid,
                    repa::GridType gt)
    {
        ncalls++;
        if (ncalls == 1) {
            initial_nlocalcells = grid->n_local_cells();
        }
        else if (ncalls >= 2) {
            if (t.comm().size() == 1)
                return;

            // Must not increase load on overloaded processes
            BOOST_CHECK(if_then(is_overloaded_process(t.comm()),
                                grid->n_local_cells() <= initial_nlocalcells));

            // There must be one process which was overloaded and its load
            // got reduced.
            int cell_difference = 0;
            if (is_overloaded_process(t.comm()))
                cell_difference = initial_nlocalcells - grid->n_local_cells();

            int total_cell_difference = boost::mpi::all_reduce(
                t.comm(), cell_difference, std::plus<int>{});

            BOOST_CHECK(total_cell_difference > 0);
        }
#if 0
        std::cout << "[" << t.comm().rank() << "] "
                  << repa::grid_type_to_string(gt) << "/" << ncalls
                  << ": intial_nlocalcells: " << initial_nlocalcells
                  << " current nlocalcells: " << grid->n_local_cells()
                  << std::endl;
#endif
    }

private:
    int ncalls = 0;
    repa::grids::local_cell_index_type initial_nlocalcells;
};

BOOST_AUTO_TEST_CASE(test_load_reduction)
{
    boost::mpi::environment env;
    boost::mpi::communicator comm;
    auto m = [&comm](size_t n) {
        if (is_overloaded_process(comm))
            return std::vector<double>(n, 10.0);
        else
            return std::vector<double>(n, 1.0);
    };

    // Global methods: Check for for imbalance reduction after the first
    // repartition call.
    testenv::TEnv::default_test_env()
        .with_metric(m)
        .with_repart()
        .all_grids()
        .only({repa::GridType::GRAPH, repa::GridType::P4EST,
               repa::GridType::KD_TREE,
               repa::GridType::HYB_GP_DIFF}) // Cart will not reduce imbalance
        .run([]() { return StrictImbalanceReductionTest{}; });

    // Local/iterative methods: Check for imbalance reductiona after second
    // repartitioning.
    testenv::TEnv::default_test_env()
        .with_metric(m)
        .with_repart_twice()
        .all_grids()
        .only({repa::GridType::DIFF, repa::GridType::GRIDBASED})
        // FIXME: Points in Gridbased might collide because of strong imbalance
        // and we currently don't handle collisions in gridbased, so excluding
        // it for now
        .exclude({repa::GridType::GRIDBASED})
        .run([]() { return WeakImbalanceReductionTest{}; });
}
