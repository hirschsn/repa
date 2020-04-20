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

#include "repa/repa.hpp"
#include <boost/mpi/communicator.hpp>
#include <functional>
#include <memory>
#include <set>

namespace testenv {

struct TEnv {
    using MetricFunc = std::function<std::vector<double>(size_t)>;
    using Test_Func_No_GridType
        = std::function<void(const TEnv &, repa::grids::ParallelLCGrid *)>;
    using Test_Func_With_GridType = std::function<void(
        const TEnv &, repa::grids::ParallelLCGrid *, repa::GridType)>;
    
    // Function that returns a Test_Func. Useful for starting with a
    // new state for each grid type.
    using Test_Func_No_GridType_Returner
        = std::function<Test_Func_No_GridType(void)>;
    using Test_Func_With_GridType_Returner
        = std::function<Test_Func_With_GridType(void)>;

    TEnv &with_repart();
    TEnv &with_metric(MetricFunc f);
    TEnv &with_repart_twice();
    TEnv &without_repart();
    TEnv &all_grids();
    TEnv &only(std::set<repa::GridType> s);
    TEnv &exclude(std::set<repa::GridType> s);

    void run(Test_Func_No_GridType test_func);
    void run(Test_Func_With_GridType test_func);
    // Calls get_test_func() to get a fresh test_func for each grid type.
    // Use these overloads if your test_func needs a separate state for
    // each grid type. See, e.g., test_load_reduction.
    void run(Test_Func_No_GridType_Returner get_test_func);
    void run(Test_Func_With_GridType_Returner get_test_func);

    static testenv::TEnv default_test_env(repa::ExtraParams ep
                                          = repa::ExtraParams{});
    static testenv::TEnv custom_test_env(repa::Vec3d box,
                                         double min_gs,
                                         repa::ExtraParams ep
                                         = repa::ExtraParams{});
    ~TEnv();
    TEnv(TEnv &&);

    const boost::mpi::communicator &comm() const;
    double mings() const;
    const repa::Vec3d &box() const;

private:
    TEnv(repa::ExtraParams ep);
    TEnv(repa::Vec3d box, double min_gs, repa::ExtraParams ep);

    struct TEnv_impl;
    std::unique_ptr<TEnv_impl> te_impl;
};

} // namespace testenv
