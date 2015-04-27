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

// GENERAL CONFIGURATION
#include <IBAMR_config.h>
#include <SAMRAI/SAMRAI_config.h>

// PETSC INCLUDES
#include <petsc.h>

// IBTK INCLUDES
#include <ibtk/CartExtrapPhysBdryOp.h>
#include <ibtk/HierarchyMathOps.h>
#include <ibtk/ibtk_utilities.h>

// LIBMESH INCLUDES
#include <libmesh/equation_systems.h>
#include <libmesh/exact_solution.h>
#include <libmesh/mesh.h>

// SAMRAI INCLUDES
#include <SAMRAI/appu/VisItDataWriter.h>
#include <SAMRAI/geom/CartesianGridGeometry.h>
#include <SAMRAI/hier/ComponentSelector.h>
#include <SAMRAI/hier/PatchHierarchy.h>
#include <SAMRAI/hier/VariableDatabase.h>
#include <SAMRAI/math/HierarchyCellDataOpsReal.h>
#include <SAMRAI/math/HierarchySideDataOpsReal.h>
#include <SAMRAI/pdat/CellVariable.h>
#include <SAMRAI/pdat/SideVariable.h>
#include <SAMRAI/tbox/Database.h>
#include <SAMRAI/tbox/HDFDatabase.h>
#include <SAMRAI/tbox/InputDatabase.h>
#include <SAMRAI/tbox/InputManager.h>
#include <SAMRAI/tbox/MathUtilities.h>
#include <SAMRAI/tbox/PIO.h>
#include <SAMRAI/tbox/SAMRAIManager.h>
#include <SAMRAI/tbox/SAMRAI_MPI.h>
#include <SAMRAI/tbox/Utilities.h>
#include <ibamr/app_namespaces.h>

int main(int argc, char* argv[])
{
    // Initialize libMesh, PETSc, MPI, and SAMRAI.
    LibMeshInit init(argc, argv);
    {
        SAMRAI_MPI::setCallAbortInSerialInsteadOfExit();
        SAMRAI_MPI::init(PETSC_COMM_WORLD);
        SAMRAIManager::startup();

        if (argc != 2)
        {
            pout << "USAGE:  " << argv[0] << " <input filename>\n"
                 << "  options:\n"
                 << "  none at this time" << endl;
            SAMRAI_MPI::abort();
            return (-1);
        }

        string input_filename = argv[1];
        plog << "input_filename = " << input_filename << endl;

        // Create input database and parse all data in input file.
        auto input_db = boost::make_shared<InputDatabase>("input_db");
        InputManager::getManager()->parseInputFile(input_filename, input_db);

        // Retrieve "Main" section of the input database.
        auto main_db = input_db->getDatabase("Main");

        int coarse_hier_dump_interval = 0;
        int fine_hier_dump_interval = 0;
        if (main_db->keyExists("hier_dump_interval"))
        {
            coarse_hier_dump_interval = main_db->getInteger("hier_dump_interval");
            fine_hier_dump_interval = main_db->getInteger("hier_dump_interval");
        }
        else if (main_db->keyExists("coarse_hier_dump_interval") && main_db->keyExists("fine_hier_dump_interval"))
        {
            coarse_hier_dump_interval = main_db->getInteger("coarse_hier_dump_interval");
            fine_hier_dump_interval = main_db->getInteger("fine_hier_dump_interval");
        }
        else
        {
            TBOX_ERROR("hierarchy dump intervals not specified in input file. . .\n");
        }

        string coarse_hier_dump_dirname;
        if (main_db->keyExists("coarse_hier_dump_dirname"))
        {
            coarse_hier_dump_dirname = main_db->getString("coarse_hier_dump_dirname");
        }
        else
        {
            TBOX_ERROR("key `coarse_hier_dump_dirname' not specified in input file");
        }

        string fine_hier_dump_dirname;
        if (main_db->keyExists("fine_hier_dump_dirname"))
        {
            fine_hier_dump_dirname = main_db->getString("fine_hier_dump_dirname");
        }
        else
        {
            TBOX_ERROR("key `fine_hier_dump_dirname' not specified in input file");
        }

        // Create major algorithm and data objects which comprise application.
        auto grid_geom = boost::make_shared<geom::CartesianGridGeometry>(DIM, "CartesianGeometry",
                                                                         input_db->getDatabase("CartesianGeometry"));

        // Initialize variables.
        auto var_db = VariableDatabase::getDatabase();

        auto current_ctx = var_db->getContext("INSStaggeredHierarchyIntegrator::CURRENT");
        auto scratch_ctx = var_db->getContext("INSStaggeredHierarchyIntegrator::SCRATCH");

        auto U_var = boost::make_shared<SideVariable<double>>(DIM, "INSStaggeredHierarchyIntegrator::U");
        const int U_idx = var_db->registerVariableAndContext(U_var, current_ctx, IntVector(DIM, 0));
        const int U_interp_idx = var_db->registerClonedPatchDataIndex(U_var, U_idx);
        const int U_scratch_idx = var_db->registerVariableAndContext(U_var, scratch_ctx, IntVector(DIM, 2));

        auto P_var = boost::make_shared<CellVariable<double>>(DIM, "INSStaggeredHierarchyIntegrator::P");
        //     auto P_var = new
        //     CellVariable<double>("INSStaggeredHierarchyIntegrator::P_extrap");
        const int P_idx = var_db->registerVariableAndContext(P_var, current_ctx, IntVector(DIM, 0));
        const int P_interp_idx = var_db->registerClonedPatchDataIndex(P_var, P_idx);
        const int P_scratch_idx = var_db->registerVariableAndContext(P_var, scratch_ctx, IntVector(DIM, 2));

        // Set up visualization plot file writer.
        auto visit_data_writer =
            boost::make_shared<VisItDataWriter>(DIM, "VisIt Writer", main_db->getString("viz_dump_dirname"), 1);
        visit_data_writer->registerPlotQuantity("P", "SCALAR", P_idx);
        visit_data_writer->registerPlotQuantity("P interp", "SCALAR", P_interp_idx);

        // Time step loop.
        double loop_time = 0.0;
        int coarse_iteration_num = coarse_hier_dump_interval;
        int fine_iteration_num = fine_hier_dump_interval;

        bool files_exist = true;
        for (; files_exist;
             coarse_iteration_num += coarse_hier_dump_interval, fine_iteration_num += fine_hier_dump_interval)
        {
            SAMRAI_MPI comm(MPI_COMM_WORLD);
            char temp_buf[128];

            sprintf(temp_buf, "%05d.samrai.%05d", coarse_iteration_num, comm.getRank());
            string coarse_file_name = coarse_hier_dump_dirname + "/" + "hier_data.";
            coarse_file_name += temp_buf;

            sprintf(temp_buf, "%05d.samrai.%05d", fine_iteration_num, comm.getRank());
            string fine_file_name = fine_hier_dump_dirname + "/" + "hier_data.";
            fine_file_name += temp_buf;

            for (int rank = 0; rank < comm.getSize(); ++rank)
            {
                if (rank == comm.getRank())
                {
                    fstream coarse_fin, fine_fin;
                    coarse_fin.open(coarse_file_name.c_str(), ios::in);
                    fine_fin.open(fine_file_name.c_str(), ios::in);
                    if (!coarse_fin.is_open() || !fine_fin.is_open())
                    {
                        files_exist = false;
                    }
                    coarse_fin.close();
                    fine_fin.close();
                }
                SAMRAI_MPI comm(MPI_COMM_WORLD);
                comm.Barrier();
            }

            if (!files_exist) break;

            pout << endl;
            pout << "+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++" << endl;
            pout << "processing data" << endl;
            pout << "     coarse iteration number = " << coarse_iteration_num << endl;
            pout << "     fine iteration number = " << fine_iteration_num << endl;
            pout << "     coarse file name = " << coarse_file_name << endl;
            pout << "     fine file name = " << fine_file_name << endl;

            // Read in data to post-process.
            ComponentSelector hier_data;
            hier_data.setFlag(U_idx);
            hier_data.setFlag(P_idx);

            auto coarse_hier_db = boost::make_shared<HDFDatabase>("coarse_hier_db");
            coarse_hier_db->open(coarse_file_name);

            auto coarse_patch_hierarchy = boost::make_shared<PatchHierarchy>("CoarsePatchHierarchy", grid_geom);
            // coarse_patch_hierarchy->getFromDatabase(coarse_hier_db->getDatabase("PatchHierarchy"));

            const double coarse_loop_time = coarse_hier_db->getDouble("loop_time");

            coarse_hier_db->close();

            auto fine_hier_db = boost::make_shared<HDFDatabase>("fine_hier_db");
            fine_hier_db->open(fine_file_name);

            auto fine_patch_hierarchy = boost::make_shared<PatchHierarchy>(
                "FinePatchHierarchy", grid_geom->makeRefinedGridGeometry("FineGridGeometry", IntVector(DIM, 2)));
            // fine_patch_hierarchy->getFromDatabase(fine_hier_db->getDatabase("PatchHierarchy"), hier_data);

            const double fine_loop_time = fine_hier_db->getDouble("loop_time");

            fine_hier_db->close();

            TBOX_ASSERT(MathUtilities<double>::equalEps(coarse_loop_time, fine_loop_time));
            loop_time = fine_loop_time;
            pout << "     loop time = " << loop_time << endl;

            auto coarsened_fine_patch_hierarchy =
                fine_patch_hierarchy->makeCoarsenedPatchHierarchy("CoarsenedFinePatchHierarchy", IntVector(DIM, 2));

            // Setup hierarchy operations objects.
            HierarchyCellDataOpsReal<double> coarse_hier_cc_data_ops(coarse_patch_hierarchy, 0,
                                                                     coarse_patch_hierarchy->getFinestLevelNumber());
            HierarchySideDataOpsReal<double> coarse_hier_sc_data_ops(coarse_patch_hierarchy, 0,
                                                                     coarse_patch_hierarchy->getFinestLevelNumber());
            HierarchyMathOps hier_math_ops("hier_math_ops", coarse_patch_hierarchy);
            hier_math_ops.setPatchHierarchy(coarse_patch_hierarchy);
            hier_math_ops.resetLevels(0, coarse_patch_hierarchy->getFinestLevelNumber());
            const int wgt_cc_idx = hier_math_ops.getCellWeightPatchDescriptorIndex();
            const int wgt_sc_idx = hier_math_ops.getSideWeightPatchDescriptorIndex();

            // Allocate patch data.
            for (int ln = 0; ln <= coarse_patch_hierarchy->getFinestLevelNumber(); ++ln)
            {
                auto level = coarse_patch_hierarchy->getPatchLevel(ln);
                level->allocatePatchData(U_interp_idx, loop_time);
                level->allocatePatchData(P_interp_idx, loop_time);
                level->allocatePatchData(U_scratch_idx, loop_time);
                level->allocatePatchData(P_scratch_idx, loop_time);
            }

            for (int ln = 0; ln <= fine_patch_hierarchy->getFinestLevelNumber(); ++ln)
            {
                auto level = fine_patch_hierarchy->getPatchLevel(ln);
                level->allocatePatchData(U_interp_idx, loop_time);
                level->allocatePatchData(P_interp_idx, loop_time);
                level->allocatePatchData(U_scratch_idx, loop_time);
                level->allocatePatchData(P_scratch_idx, loop_time);
            }

            for (int ln = 0; ln <= coarsened_fine_patch_hierarchy->getFinestLevelNumber(); ++ln)
            {
                auto level = coarsened_fine_patch_hierarchy->getPatchLevel(ln);
                level->allocatePatchData(U_idx, loop_time);
                level->allocatePatchData(P_idx, loop_time);
                level->allocatePatchData(U_interp_idx, loop_time);
                level->allocatePatchData(P_interp_idx, loop_time);
                level->allocatePatchData(U_scratch_idx, loop_time);
                level->allocatePatchData(P_scratch_idx, loop_time);
            }

            // Synchronize the coarse hierarchy data.
            for (int ln = coarse_patch_hierarchy->getFinestLevelNumber(); ln > 0; --ln)
            {
                auto coarser_level = coarse_patch_hierarchy->getPatchLevel(ln - 1);
                auto finer_level = coarse_patch_hierarchy->getPatchLevel(ln);

                xfer::CoarsenAlgorithm coarsen_alg(DIM);
                boost::shared_ptr<CoarsenOperator> coarsen_op;

                coarsen_op = grid_geom->lookupCoarsenOperator(U_var, "CONSERVATIVE_COARSEN");
                coarsen_alg.registerCoarsen(U_idx, U_idx, coarsen_op);

                coarsen_op = grid_geom->lookupCoarsenOperator(P_var, "CONSERVATIVE_COARSEN");
                coarsen_alg.registerCoarsen(P_idx, P_idx, coarsen_op);

                coarsen_alg.createSchedule(coarser_level, finer_level)->coarsenData();
            }

            // Synchronize the fine hierarchy data.
            for (int ln = fine_patch_hierarchy->getFinestLevelNumber(); ln > 0; --ln)
            {
                auto coarser_level = fine_patch_hierarchy->getPatchLevel(ln - 1);
                auto finer_level = fine_patch_hierarchy->getPatchLevel(ln);

                xfer::CoarsenAlgorithm coarsen_alg(DIM);
                boost::shared_ptr<CoarsenOperator> coarsen_op;

                coarsen_op = grid_geom->lookupCoarsenOperator(U_var, "CONSERVATIVE_COARSEN");
                coarsen_alg.registerCoarsen(U_idx, U_idx, coarsen_op);

                coarsen_op = grid_geom->lookupCoarsenOperator(P_var, "CONSERVATIVE_COARSEN");
                coarsen_alg.registerCoarsen(P_idx, P_idx, coarsen_op);

                coarsen_alg.createSchedule(coarser_level, finer_level)->coarsenData();
            }

            // Coarsen data from the fine hierarchy to the coarsened fine hierarchy.
            for (int ln = 0; ln <= fine_patch_hierarchy->getFinestLevelNumber(); ++ln)
            {
                auto dst_level = coarsened_fine_patch_hierarchy->getPatchLevel(ln);
                auto src_level = fine_patch_hierarchy->getPatchLevel(ln);

                boost::shared_ptr<CoarsenOperator> coarsen_op;
                for (auto p = dst_level->begin(), e = dst_level->end(); p != e; ++p)
                {
                    auto dst_patch = *p;
                    auto src_patch = src_level->getPatch(dst_patch->getGlobalId());
                    const Box& coarse_box = dst_patch->getBox();
                    TBOX_ASSERT(Box::coarsen(src_patch->getBox(), IntVector(DIM, 2)).isSpatiallyEqual(coarse_box));

                    coarsen_op = grid_geom->lookupCoarsenOperator(U_var, "CONSERVATIVE_COARSEN");
                    coarsen_op->coarsen(*dst_patch, *src_patch, U_interp_idx, U_idx, coarse_box, IntVector(DIM, 2));

                    coarsen_op = grid_geom->lookupCoarsenOperator(P_var, "CONSERVATIVE_COARSEN");
                    coarsen_op->coarsen(*dst_patch, *src_patch, P_interp_idx, P_idx, coarse_box, IntVector(DIM, 2));
                }
            }

            // Interpolate and copy data from the coarsened fine patch hierarchy to
            // the coarse patch hierarchy.
            for (int ln = 0; ln <= coarse_patch_hierarchy->getFinestLevelNumber(); ++ln)
            {
                auto dst_level = coarse_patch_hierarchy->getPatchLevel(ln);
                auto src_level = coarsened_fine_patch_hierarchy->getPatchLevel(ln);

                xfer::RefineAlgorithm refine_alg;
                boost::shared_ptr<RefineOperator> refine_op;

                refine_op = grid_geom->lookupRefineOperator(U_var, "CONSERVATIVE_LINEAR_REFINE");
                refine_alg.registerRefine(U_interp_idx, U_interp_idx, U_scratch_idx, refine_op);

                refine_op = grid_geom->lookupRefineOperator(P_var, "LINEAR_REFINE");
                refine_alg.registerRefine(P_interp_idx, P_interp_idx, P_scratch_idx, refine_op);

                ComponentSelector data_indices;
                data_indices.setFlag(U_scratch_idx);
                data_indices.setFlag(P_scratch_idx);
                CartExtrapPhysBdryOp bc_helper(data_indices, "LINEAR");

                refine_alg.createSchedule(dst_level, src_level, ln - 1, coarse_patch_hierarchy, &bc_helper)
                    ->fillData(loop_time);
            }

            // Output plot data before taking norms of differences.
            visit_data_writer->writePlotData(coarse_patch_hierarchy, coarse_iteration_num, loop_time);

            // Compute norms of differences.
            coarse_hier_sc_data_ops.subtract(U_interp_idx, U_idx, U_interp_idx);
            coarse_hier_cc_data_ops.subtract(P_interp_idx, P_idx, P_interp_idx);

            pout << "\n"
                 << "Error in " << U_var->getName() << " at time " << loop_time << ":\n"
                 << "  L1-norm:  " << coarse_hier_sc_data_ops.L1Norm(U_interp_idx, wgt_sc_idx) << "\n"
                 << "  L2-norm:  " << coarse_hier_sc_data_ops.L2Norm(U_interp_idx, wgt_sc_idx) << "\n"
                 << "  max-norm: " << coarse_hier_sc_data_ops.maxNorm(U_interp_idx, wgt_sc_idx) << "\n";

            pout << "\n"
                 << "Error in " << P_var->getName() << " at time " << loop_time << ":\n"
                 << "  L1-norm:  " << coarse_hier_cc_data_ops.L1Norm(P_interp_idx, wgt_cc_idx) << "\n"
                 << "  L2-norm:  " << coarse_hier_cc_data_ops.L2Norm(P_interp_idx, wgt_cc_idx) << "\n"
                 << "  max-norm: " << coarse_hier_cc_data_ops.maxNorm(P_interp_idx, wgt_cc_idx) << "\n";

            // Output plot data after taking norms of differences.
            visit_data_writer->writePlotData(coarse_patch_hierarchy, coarse_iteration_num + 1, loop_time);

            // Do the same thing for the FE data.
            string file_name;

            Mesh mesh_coarse(NDIM);
            file_name = coarse_hier_dump_dirname + "/" + "fe_mesh.";
            sprintf(temp_buf, "%05d", coarse_iteration_num);
            file_name += temp_buf;
            file_name += ".xda";
            mesh_coarse.read(file_name);

            Mesh mesh_fine(NDIM);
            file_name = fine_hier_dump_dirname + "/" + "fe_mesh.";
            sprintf(temp_buf, "%05d", fine_iteration_num);
            file_name += temp_buf;
            file_name += ".xda";
            mesh_fine.read(file_name);

            EquationSystems equation_systems_coarse(mesh_coarse);
            file_name = coarse_hier_dump_dirname + "/" + "fe_equation_systems.";
            sprintf(temp_buf, "%05d", coarse_iteration_num);
            file_name += temp_buf;
            equation_systems_coarse.read(file_name, (EquationSystems::READ_HEADER | EquationSystems::READ_DATA |
                                                     EquationSystems::READ_ADDITIONAL_DATA));

            EquationSystems equation_systems_fine(mesh_fine);
            file_name = fine_hier_dump_dirname + "/" + "fe_equation_systems.";
            sprintf(temp_buf, "%05d", fine_iteration_num);
            file_name += temp_buf;
            equation_systems_fine.read(file_name, (EquationSystems::READ_HEADER | EquationSystems::READ_DATA |
                                                   EquationSystems::READ_ADDITIONAL_DATA));

            ExactSolution error_estimator(equation_systems_coarse);
            error_estimator.attach_reference_solution(&equation_systems_fine);

            error_estimator.compute_error("IB coordinates system", "X_0");
            double X0_error[3];
            X0_error[0] = error_estimator.l1_error("IB coordinates system", "X_0");
            X0_error[1] = error_estimator.l2_error("IB coordinates system", "X_0");
            X0_error[2] = error_estimator.l_inf_error("IB coordinates system", "X_0");

            error_estimator.compute_error("IB coordinates system", "X_1");
            double X1_error[3];
            X1_error[0] = error_estimator.l1_error("IB coordinates system", "X_1");
            X1_error[1] = error_estimator.l2_error("IB coordinates system", "X_1");
            X1_error[2] = error_estimator.l_inf_error("IB coordinates system", "X_1");

            double X_error[3];
            X_error[0] = X0_error[0] + X1_error[0];
            X_error[1] = sqrt(X0_error[1] * X0_error[1] + X1_error[1] * X1_error[1]);
            X_error[2] = max(X0_error[2], X1_error[2]);

            pout << "\n"
                 << "Error in X at time " << loop_time << ":\n"
                 << "  L1-norm:  " << X_error[0] << "\n"
                 << "  L2-norm:  " << X_error[1] << "\n"
                 << "  max-norm: " << X_error[2] << "\n";

            pout << endl;
            pout << "+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++" << endl;
            pout << endl;
        }

        SAMRAIManager::shutdown();
    }
    return 0;
}
