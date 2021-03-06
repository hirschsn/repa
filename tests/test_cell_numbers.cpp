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
 * Checks if the number of local cells on each process is meaningful.
 */

#define BOOST_TEST_NO_MAIN
#define BOOST_TEST_ALTERNATIVE_INIT_API
#define BOOST_TEST_DYN_LINK
#define BOOST_TEST_MODULE cell_numbers
#include <boost/test/unit_test.hpp>

#include "testenv.hpp"
#include <boost/mpi/collectives.hpp>
#include <boost/mpi/environment.hpp>
#include <repa/repa.hpp>

/**
 * Relative distance between a and b.
 * Only meaningfully defined for floating point types.
 */
template <typename T,
          typename
          = typename std::enable_if<std::is_floating_point<T>::value>::type>
T relative_distance(T a, T b)
{
    return std::fabs((a - b) / std::min(a, b));
}

/**
 * Returns true if the relative distance between a and b is smaller than eps.
 */
template <typename T,
          typename
          = typename std::enable_if<std::is_floating_point<T>::value>::type>
bool is_close(T a, T b, T eps = T{1e-14})
{
    return relative_distance(a, b) < eps;
}

static void test(const testenv::TEnv &t, repa::grids::ParallelLCGrid *grid)
{
    BOOST_TEST(grid->local_cells().size() >= 0);

    size_t nglobalcells = boost::mpi::all_reduce(
        t.comm(), grid->local_cells().size(), std::plus<size_t>{});
    BOOST_TEST(nglobalcells > 0);

    auto grid_size = grid->grid_size();
    auto cell_size = grid->cell_size();
    for (size_t i = 0; i < grid_size.size(); ++i) {
        BOOST_TEST((cell_size[i] > 0.));
        BOOST_TEST((grid_size[i] > 0));
        BOOST_TEST(grid_size[i] >= t.mings());
        BOOST_TEST(is_close(grid_size[i] * cell_size[i], t.box()[i]));
    }

    BOOST_TEST(
        (nglobalcells
         == static_cast<size_t>(grid_size[0] * grid_size[1] * grid_size[2])));
}

BOOST_AUTO_TEST_CASE(test_cell_numbers)
{
    testenv::TEnv::default_test_env().with_repart().all_grids().run(test);
}

int main(int argc, char **argv)
{
    boost::mpi::environment mpi_env{argc, argv};
    return boost::unit_test::unit_test_main(init_unit_test, argc, argv);
}
