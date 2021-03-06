// Filename: IBLagrangianForceStrategySet.h
// Created on 04 April 2007 by Boyce Griffith
//
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

#ifndef included_IBAMR_IBLagrangianForceStrategySet
#define included_IBAMR_IBLagrangianForceStrategySet

/////////////////////////////// INCLUDES /////////////////////////////////////

#include <vector>

#include "ibamr/IBLagrangianForceStrategy.h"
#include "petscmat.h"
#include "tbox/Pointer.h"

namespace IBTK
{
class LData;
class LDataManager;
} // namespace IBTK
namespace SAMRAI
{
namespace hier
{
template <int DIM>
class PatchHierarchy;
} // namespace hier
} // namespace SAMRAI

/////////////////////////////// CLASS DEFINITION /////////////////////////////

namespace IBAMR
{
/*!
 * \brief Class IBLagrangianForceStrategySet is a utility class that allows
 * multiple IBLagrangianForceStrategy objects to be employed by a single
 * IBHierarchyIntegrator.
 */
class IBLagrangianForceStrategySet : public IBLagrangianForceStrategy
{
public:
    /*!
     * \brief Constructor.
     */
    template <typename InputIterator>
    IBLagrangianForceStrategySet(InputIterator first, InputIterator last)
        : d_strategy_set(first, last)
    {
        // intentionally blank
        return;
    } // IBLagrangianForceStrategySet

    /*!
     * \brief Destructor.
     */
    ~IBLagrangianForceStrategySet();

    /*!
     * \brief Set the current and new times for the present timestep.
     */
    void setTimeInterval(double current_time, double new_time);

    /*!
     * \brief Setup the data needed to compute the forces on the specified level
     * of the patch hierarchy.
     */
    void initializeLevelData(SAMRAI::tbox::Pointer<SAMRAI::hier::PatchHierarchy<NDIM> > hierarchy,
                             int level_number,
                             double init_data_time,
                             bool initial_time,
                             IBTK::LDataManager* l_data_manager);

    /*!
     * \brief Compute the force generated by the Lagrangian structure on the
     * specified level of the patch hierarchy.
     *
     * \note Nodal forces computed by this method are \em added to the force
     * vector.
     */
    void computeLagrangianForce(SAMRAI::tbox::Pointer<IBTK::LData> F_data,
                                SAMRAI::tbox::Pointer<IBTK::LData> X_data,
                                SAMRAI::tbox::Pointer<IBTK::LData> U_data,
                                SAMRAI::tbox::Pointer<SAMRAI::hier::PatchHierarchy<NDIM> > hierarchy,
                                int level_number,
                                double data_time,
                                IBTK::LDataManager* l_data_manager);

    /*!
     * \brief Compute the non-zero structure of the force Jacobian matrix.
     *
     * \note Elements indices must be global PETSc indices.
     */
    void
    computeLagrangianForceJacobianNonzeroStructure(std::vector<int>& d_nnz,
                                                   std::vector<int>& o_nnz,
                                                   SAMRAI::tbox::Pointer<SAMRAI::hier::PatchHierarchy<NDIM> > hierarchy,
                                                   int level_number,
                                                   IBTK::LDataManager* l_data_manager);

    /*!
     * \brief Compute the Jacobian of the force with respect to the present
     * structure configuration and velocity.
     *
     * \note The elements of the Jacobian should be "accumulated" in the
     * provided matrix J.
     */
    void computeLagrangianForceJacobian(Mat& J_mat,
                                        MatAssemblyType assembly_type,
                                        double X_coef,
                                        SAMRAI::tbox::Pointer<IBTK::LData> X_data,
                                        double U_coef,
                                        SAMRAI::tbox::Pointer<IBTK::LData> U_data,
                                        SAMRAI::tbox::Pointer<SAMRAI::hier::PatchHierarchy<NDIM> > hierarchy,
                                        int level_number,
                                        double data_time,
                                        IBTK::LDataManager* l_data_manager);

    /*!
     * \brief Compute the potential energy with respect to the present structure
     * configuration and velocity.
     */
    double computeLagrangianEnergy(SAMRAI::tbox::Pointer<IBTK::LData> X_data,
                                   SAMRAI::tbox::Pointer<IBTK::LData> U_data,
                                   SAMRAI::tbox::Pointer<SAMRAI::hier::PatchHierarchy<NDIM> > hierarchy,
                                   int level_number,
                                   double data_time,
                                   IBTK::LDataManager* l_data_manager);

private:
    /*!
     * \brief Default constructor.
     *
     * \note This constructor is not implemented and should not be used.
     */
    IBLagrangianForceStrategySet();

    /*!
     * \brief Copy constructor.
     *
     * \note This constructor is not implemented and should not be used.
     *
     * \param from The value to copy to this object.
     */
    IBLagrangianForceStrategySet(const IBLagrangianForceStrategySet& from);

    /*!
     * \brief Assignment operator.
     *
     * \note This operator is not implemented and should not be used.
     *
     * \param that The value to assign to this object.
     *
     * \return A reference to this object.
     */
    IBLagrangianForceStrategySet& operator=(const IBLagrangianForceStrategySet& that);

    /*!
     * \brief The set of IBLagrangianForceStrategy objects.
     */
    std::vector<SAMRAI::tbox::Pointer<IBLagrangianForceStrategy> > d_strategy_set;
};
} // namespace IBAMR

//////////////////////////////////////////////////////////////////////////////

#endif //#ifndef included_IBAMR_IBLagrangianForceStrategySet
