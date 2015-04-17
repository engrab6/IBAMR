// Copyright (c) 2002-2014, Boyce Griffith
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
//    * Redistributions of source code must retain the above copyright notice,
//      this list of conditions and the following disclaimer.
//
//    * Redistributions in binary form must reproduce the above copyright
//      notice, this list of conditions and the following disclaimer in the
//      documentation and/or other materials provided with the distribution.
//
//    * Neither the name of The University of North Carolina nor the names of
//      its contributors may be used to endorse or promote products derived from
//      this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.

// Config files
#include <IBTK_config.h>
#include <SAMRAI/SAMRAI_config.h>

// Headers for basic PETSc objects
#include <petscsys.h>

// Headers for major SAMRAI objects
#include <SAMRAI/geom/CartesianGridGeometry.h>
#include <SAMRAI/geom/CartesianPatchGeometry.h>
#include <SAMRAI/mesh/BergerRigoutsos.h>
#include <SAMRAI/mesh/ChopAndPackLoadBalancer.h>
#include <SAMRAI/mesh/GriddingAlgorithm.h>
#include <SAMRAI/mesh/StandardTagAndInitialize.h>

// Headers for application-specific algorithm/data structure objects
#include <SAMRAI/solv/LocationIndexRobinBcCoefs.h>
#include <ibtk/AppInitializer.h>
#include <ibtk/CartCellRobinPhysBdryOp.h>
#include <ibtk/CartExtrapPhysBdryOp.h>
#include <ibtk/app_namespaces.h>
#include <ibtk/ibtk_utilities.h>

/*******************************************************************************
 * For each run, the input filename must be given on the command line.  In all *
 * cases, the command line is:                                                 *
 *                                                                             *
 *    executable <input file name>                                             *
 *                                                                             *
 *******************************************************************************/
int main(int argc, char* argv[])
{
    // Initialize PETSc, MPI, and SAMRAI.
    PetscInitialize(&argc, &argv, NULL, NULL);
    SAMRAI_MPI::init(PETSC_COMM_WORLD);
    SAMRAI_MPI::setCallAbortInSerialInsteadOfExit();
    SAMRAIManager::startup();

    { // cleanup dynamically allocated objects prior to shutdown

        // Parse command line options, set some standard options from the input
        // file, and enable file logging.
        auto app_initializer = boost::make_shared<AppInitializer>(argc, argv, "cc_poisson.log");
        auto input_db = app_initializer->getInputDatabase();

        // Create major algorithm and data objects that comprise the
        // application.  These objects are configured from the input database.
        auto grid_geometry = boost::make_shared<CartesianGridGeometry>(
            DIM, "CartesianGeometry", app_initializer->getComponentDatabase("CartesianGeometry"));
        auto patch_hierarchy = boost::make_shared<PatchHierarchy>("PatchHierarchy", grid_geometry);
        auto error_detector = boost::make_shared<StandardTagAndInitialize>(
            "StandardTagAndInitialize", static_cast<StandardTagAndInitStrategy*>(NULL),
            app_initializer->getComponentDatabase("StandardTagAndInitialize"));
        auto box_generator = boost::make_shared<BergerRigoutsos>(DIM);
        auto load_balancer = boost::make_shared<ChopAndPackLoadBalancer>(
            DIM, "ChopAndPackLoadBalancer", app_initializer->getComponentDatabase("ChopAndPackLoadBalancer"));
        auto gridding_algorithm = boost::make_shared<GriddingAlgorithm>(
            patch_hierarchy, "GriddingAlgorithm", app_initializer->getComponentDatabase("GriddingAlgorithm"),
            error_detector, box_generator, load_balancer);

        // Initialize the AMR patch hierarchy.
        gridding_algorithm->makeCoarsestLevel(0.0);
        int tag_buffer = 1;
        int level_number = 0;
        bool done = false;
        while (!done && (patch_hierarchy->levelCanBeRefined(level_number)))
        {
            gridding_algorithm->makeFinerLevel(tag_buffer, true, 0, 0.0);
            done = !patch_hierarchy->finerLevelExists(level_number);
            ++level_number;
        }

        // Create cell-centered data and extrapolate that data at physical
        // boundaries to obtain ghost cell values.
        auto var_db = VariableDatabase::getDatabase();
        auto context = var_db->getContext("CONTEXT");
        auto var = boost::make_shared<CellVariable<double> >(DIM, "v");
        const IntVector gcw(DIM, 4);
        const int idx = var_db->registerVariableAndContext(var, context, gcw);
        for (int ln = 0; ln <= patch_hierarchy->getFinestLevelNumber(); ++ln)
        {
            auto level = patch_hierarchy->getPatchLevel(ln);
            level->allocatePatchData(idx);
            for (auto p = level->begin(); p != level->end(); ++p)
            {
                auto patch = *p;
                const Box& patch_box = patch->getBox();
                const Index& patch_lower = patch_box.lower();
                auto data = BOOST_CAST<CellData<double> >(patch->getPatchData(idx));
                const Box& ghost_box = data->getGhostBox();
                for (auto b = CellGeometry::begin(patch_box), e = CellGeometry::end(patch_box); b != e; ++b)
                {
                    const CellIndex& i = *b;
                    (*data)(i) = 0;
                    for (unsigned int d = 0; d < NDIM; ++d)
                    {
                        (*data)(i) += 4 * (d + 1) * (d + 1) * i(d);
                    }
                }

                pout << "level number = " << ln << "\n";
                pout << "patch_box = " << patch_box << "\n";
                pout << "\n";

                plog << "interior data:\n";
                data->print(patch_box);
                plog << "\n";

                CartExtrapPhysBdryOp constant_fill_op(idx, "CONSTANT");
                constant_fill_op.setPhysicalBoundaryConditions(*patch, 0.0, data->getGhostCellWidth());
                plog << "constant extrapolated ghost data:\n";
                data->print(ghost_box);
                plog << "\n";

                CartExtrapPhysBdryOp linear_fill_op(idx, "LINEAR");
                linear_fill_op.setPhysicalBoundaryConditions(*patch, 0.0, data->getGhostCellWidth());
                plog << "linear extrapolated ghost data:\n";
                data->print(ghost_box);
                plog << "\n";

                bool warning = false;
                for (auto b = CellGeometry::begin(ghost_box), e = CellGeometry::end(ghost_box); b != e; ++b)
                {
                    const CellIndex& i = *b;
                    double val = 0;
                    for (int d = 0; d < NDIM; ++d)
                    {
                        val += 4 * (d + 1) * (d + 1) * i(d);
                    }

                    if (!MathUtilities<double>::equalEps(val, (*data)(i)))
                    {
                        warning = true;
                        pout << "warning: value at location " << i << " is not correct\n";
                        pout << "  expected value = " << val << "   computed value = " << (*data)(i) << "\n";
                    }
                }

                if (!warning)
                {
                    pout << "linearly extrapolated boundary data appears to be correct.\n";
                }
                else
                {
                    pout << "possible errors encountered in linearly extrapolated boundary data.\n";
                }

                pout << "checking robin bc handling . . .\n";

                auto pgeom = BOOST_CAST<CartesianPatchGeometry>(patch->getPatchGeometry());
                const double* const x_lower = pgeom->getXLower();
                const double* const x_upper = grid_geometry->getXUpper();
                const double* const dx = pgeom->getDx();
                const double shift = 3.14159;
                for (auto b = CellGeometry::begin(patch_box), e = CellGeometry::end(patch_box); b != e; ++b)
                {
                    const CellIndex& i = *b;
                    double X[NDIM];
                    for (unsigned int d = 0; d < NDIM; ++d)
                    {
                        X[d] = x_lower[d] + dx[d] * (static_cast<double>(i(d) - patch_lower(d)) + 0.5);
                    }
                    (*data)(i) = 2.0 * X[NDIM - 1] + shift;
                }

                plog << "interior data:\n";
                data->print(data->getBox());
                plog << "\n";

                auto dirichlet_bc_coef = boost::make_shared<LocationIndexRobinBcCoefs>(DIM, "dirichlet_bc_coef");
                for (unsigned int d = 0; d < NDIM - 1; ++d)
                {
                    dirichlet_bc_coef->setBoundarySlope(2 * d, 0.0);
                    dirichlet_bc_coef->setBoundarySlope(2 * d + 1, 0.0);
                }
                dirichlet_bc_coef->setBoundaryValue(2 * (NDIM - 1), shift);
                dirichlet_bc_coef->setBoundaryValue(2 * (NDIM - 1) + 1, 2.0 * x_upper[NDIM - 1] + shift);

                CartCellRobinPhysBdryOp dirichlet_bc_fill_op(idx, dirichlet_bc_coef);
                dirichlet_bc_fill_op.setPhysicalBoundaryConditions(*patch, 0.0, data->getGhostCellWidth());
                plog << "extrapolated ghost data:\n";
                data->print(data->getGhostBox());
                plog << "\n";

                warning = false;
                for (auto b = CellGeometry::begin(ghost_box), e = CellGeometry::end(ghost_box); b != e; ++b)
                {
                    const CellIndex& i = *b;
                    double X[NDIM];
                    for (unsigned int d = 0; d < NDIM; ++d)
                    {
                        X[d] = x_lower[d] + dx[d] * (static_cast<double>(i(d) - patch_lower(d)) + 0.5);
                    }
                    double val = 2.0 * X[NDIM - 1] + shift;

                    if (!MathUtilities<double>::equalEps(val, (*data)(i)))
                    {
                        warning = true;
                        pout << "warning: value at location " << i << " is not correct\n";
                        pout << "  expected value = " << val << "   computed value = " << (*data)(i) << "\n";
                    }
                }

                if (!warning)
                {
                    pout << "dirichlet boundary data appears to be correct.\n";
                }
                else
                {
                    pout << "possible errors encountered in extrapolated dirichlet boundary data.\n";
                }

                auto neumann_bc_coef = boost::make_shared<LocationIndexRobinBcCoefs>(DIM, "neumann_bc_coef");
                for (unsigned int d = 0; d < NDIM - 1; ++d)
                {
                    neumann_bc_coef->setBoundarySlope(2 * d, 0.0);
                    neumann_bc_coef->setBoundarySlope(2 * d + 1, 0.0);
                }
                neumann_bc_coef->setBoundarySlope(2 * (NDIM - 1), -2.0);
                neumann_bc_coef->setBoundarySlope(2 * (NDIM - 1) + 1, +2.0);

                CartCellRobinPhysBdryOp neumann_bc_fill_op(idx, neumann_bc_coef);
                neumann_bc_fill_op.setPhysicalBoundaryConditions(*patch, 0.0, data->getGhostCellWidth());
                plog << "extrapolated ghost data:\n";
                data->print(data->getGhostBox());
                plog << "\n";

                warning = false;
                for (auto b = CellGeometry::begin(ghost_box), e = CellGeometry::end(ghost_box); b != e; ++b)
                {
                    const CellIndex& i = *b;
                    double X[NDIM];
                    for (unsigned int d = 0; d < NDIM; ++d)
                    {
                        X[d] = x_lower[d] + dx[d] * (static_cast<double>(i(d) - patch_lower(d)) + 0.5);
                    }
                    double val = 2.0 * X[NDIM - 1] + shift;

                    if (!MathUtilities<double>::equalEps(val, (*data)(i)))
                    {
                        warning = true;
                        pout << "warning: value at location " << i << " is not correct\n";
                        pout << "  expected value = " << val << "   computed value = " << (*data)(i) << "\n";
                    }
                }

                if (!warning)
                {
                    pout << "neumann boundary data appears to be correct.\n";
                }
                else
                {
                    pout << "possible errors encountered in extrapolated neumann boundary data.\n";
                }
            }
        }
    }

    SAMRAIManager::shutdown();
    PetscFinalize();
    return 0;
}
