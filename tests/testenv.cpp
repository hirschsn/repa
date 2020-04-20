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
#include "testenv.hpp"
#include <boost/test/unit_test.hpp>
#include <random>

static std::vector<double> get_random_vec(size_t n)
{
    std::random_device rd;
    std::mt19937 gen{rd()};
    std::uniform_real_distribution<> dis{1., 10.};

    std::vector<double> v;
    v.reserve(n);
    std::generate_n(std::back_inserter(v), n,
                    [&dis, &gen]() { return dis(gen); });

    return v;
}

static void repartition_helper(repa::grids::ParallelLCGrid *grid,
                               testenv::TEnv::MetricFunc f)
{
    auto noop = []() {};
    auto ccm = [](int, int) { return 1.0; };
    std::vector<double> metric_values
        = (f ? f : get_random_vec)(static_cast<size_t>(grid->n_local_cells()));
    auto metric = [&metric_values]() { return metric_values; };

    BOOST_CHECK_NO_THROW(grid->repartition(metric, ccm, noop));
}

namespace testenv {

struct TEnv::TEnv_impl {
    boost::mpi::communicator comm;
    repa::Vec3d box;
    double mings;
    repa::ExtraParams ep;
    std::set<repa::GridType> grids;
    bool repart = true, repart_twice = false;
    MetricFunc get_metric = nullptr;

    TEnv_impl(const boost::mpi::communicator &comm,
              repa::Vec3d box,
              double mings,
              repa::ExtraParams ep);
    TEnv_impl(repa::Vec3d box, double mings, repa::ExtraParams ep);
    TEnv_impl(repa::ExtraParams ep);

    void with_repart();
    void with_metric(MetricFunc f);
    void with_repart_twice();
    void without_repart();
    void all_grids();
    void only(std::set<repa::GridType> s);
    void exclude(std::set<repa::GridType> s);

    using TestFunc = std::function<void(repa::grids::ParallelLCGrid *grid,
                                        repa::GridType gt)>;
    using TestFuncReturner = std::function<TestFunc(void)>;

    void run(TestFuncReturner get_test_func);
};

TEnv::TEnv_impl::TEnv_impl(const boost::mpi::communicator &comm,
                           repa::Vec3d box,
                           double mings,
                           repa::ExtraParams ep)
    : comm(comm, boost::mpi::comm_duplicate),
      box(std::move(box)),
      mings(mings),
      ep(ep)
{
}

TEnv::TEnv_impl::TEnv_impl(repa::Vec3d box, double mings, repa::ExtraParams ep)
    : box(std::move(box)), mings(mings), ep(ep)
{
}

TEnv::TEnv_impl::TEnv_impl(repa::ExtraParams ep) : mings(1.0), ep(ep)
{
    // Devise some appropriately sized grid suitable for all methods.
    repa::Vec3i dims{0, 0, 0};
    MPI_Dims_create(comm.size(), 3, dims.data());
    for (size_t i = 0; i < box.size(); ++i)
        box[i] = mings * 5 * (dims[i] + 1);
}

void TEnv::TEnv_impl::with_repart()
{
    repart = true;
    repart_twice = false;
}

void TEnv::TEnv_impl::with_metric(MetricFunc f)
{
    get_metric = f;
}

void TEnv::TEnv_impl::with_repart_twice()
{
    repart = true;
    repart_twice = true;
}

void TEnv::TEnv_impl::without_repart()
{
    repart = false;
    repart_twice = false;
}

void TEnv::TEnv_impl::all_grids()
{
    grids = repa::supported_grid_types();
}

void TEnv::TEnv_impl::only(std::set<repa::GridType> s)
{
    grids.clear();
    auto supported = repa::supported_grid_types();
    for (const auto gt : s) {
        if (supported.find(gt) != std::end(supported))
            grids.insert(gt);
    }
}

void TEnv::TEnv_impl::exclude(std::set<repa::GridType> s)
{
    for (const auto gt : s)
        grids.erase(gt);
}

void TEnv::TEnv_impl::run(TestFuncReturner get_test_func)
{
    for (const auto gt : grids) {
        // Skip unavailable grids
        if (!repa::has_grid_type(gt))
            continue;

        // Initialize a fresh test_func
        auto test_func = get_test_func();

        // Test consistency of parsing and to_string.
        BOOST_TEST(
            (repa::parse_grid_type(repa::grid_type_to_string(gt)) == gt));

        if (comm.rank() == 0) {
            std::cout << "Checking grid '" << repa::grid_type_to_string(gt)
                      << "'" << std::endl;
        }

        std::unique_ptr<repa::grids::ParallelLCGrid> up = nullptr;
        BOOST_CHECK_NO_THROW(up = repa::make_pargrid(gt, comm, box, mings, ep));
        BOOST_TEST(up.get() != nullptr);

        test_func(up.get(), gt);

        if (!repart)
            continue;
        repartition_helper(up.get(), get_metric);

        test_func(up.get(), gt);

        if (!repart_twice)
            continue;
        repartition_helper(up.get(), get_metric);

        test_func(up.get(), gt);
    }
}

/// TEnv

TEnv &TEnv::with_repart()
{
    te_impl->with_repart();
    return *this;
}

TEnv &TEnv::with_metric(MetricFunc f)
{
    te_impl->with_metric(f);
    return *this;
}
TEnv &TEnv::with_repart_twice()
{
    te_impl->with_repart_twice();
    return *this;
}
TEnv &TEnv::without_repart()
{
    te_impl->without_repart();
    return *this;
}
TEnv &TEnv::all_grids()
{
    te_impl->all_grids();
    return *this;
}
TEnv &TEnv::only(std::set<repa::GridType> s)
{
    te_impl->only(s);
    return *this;
}
TEnv &TEnv::exclude(std::set<repa::GridType> s)
{
    te_impl->exclude(s);
    return *this;
}

void TEnv::run(Test_Func_No_GridType test_func)
{
    run([test_func](){ return test_func; });
}

void TEnv::run(Test_Func_With_GridType test_func)
{
    run([test_func](){ return test_func; });
}

void TEnv::run(Test_Func_No_GridType_Returner get_test_func)
{
    te_impl->run([this, get_test_func](){
        auto test_func = get_test_func();
        return [this, test_func](auto grid, auto) {
            test_func(*this, grid);
        };
    });
}

void TEnv::run(Test_Func_With_GridType_Returner get_test_func)
{
    te_impl->run([this, get_test_func](){
        auto test_func = get_test_func();
        return [this, test_func](auto grid, auto gt) {
            test_func(*this, grid, gt);
        };
    });
}

TEnv::TEnv(repa::ExtraParams ep) : te_impl(new TEnv_impl(ep))
{
}

TEnv::TEnv(repa::Vec3d box, double min_gs, repa::ExtraParams ep)
    : te_impl(new TEnv_impl(box, min_gs, ep))
{
}

TEnv TEnv::default_test_env(repa::ExtraParams ep)
{
    return testenv::TEnv{ep};
}

TEnv TEnv::custom_test_env(repa::Vec3d box, double min_gs, repa::ExtraParams ep)
{
    return testenv::TEnv{box, min_gs, ep};
}

TEnv::~TEnv() = default;
TEnv::TEnv(TEnv &&) = default;

const boost::mpi::communicator &TEnv::comm() const
{
    return te_impl->comm;
}

double TEnv::mings() const
{
    return te_impl->mings;
}

const repa::Vec3d &TEnv::box() const
{
    return te_impl->box;
}

} // namespace testenv
