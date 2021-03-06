/** @file gsVisitorDg.h

    @brief A DG interface visitor for the Poisson problem .

    This file is part of the G+Smo library.

    This Source Code Form is subject to the terms of the Mozilla Public
    License, v. 2.0. If a copy of the MPL was not distributed with this
    file, You can obtain one at http://mozilla.org/MPL/2.0/.

    Author(s): C. Hofer, A. Mantzflaris, S. Moore, S. Takacs
*/

#pragma once

namespace gismo
{
/** @brief
    Implementation of a interface condition for the
    discontinuous Galerkin Assembler.

    It sets up an assembler and assembles the system patch wise and
    combines the patch-local stiffness matrices into a global system.
    Dirichlet boundary can also be imposed weakly (i.e. Nitsche).
*/

template <class T>
class gsVisitorDg
{
public:

   /** \brief Visitor for adding the interface conditions for the
     * interior penalty method of the Poisson problem.
     *
     * This visitor adds the following term to the left-hand side (bilinear form).
     * \f[ - \{\nabla u\} \cdot \mathbf{n} [ v ]
     *     - \{\nabla v\} \cdot \mathbf{n} [ u ]
     *     + \alpha [ u ][  v ] \f].
     * Where \f[v\f] is the test function and \f[ u \f] is trial function.
     */

    gsVisitorDg(const gsPde<T> & pde)
    {}

    void initialize(const gsBasis<T> & basis1,
                    const gsBasis<T> & basis2,
                    const boundaryInterface & bi,
                    const gsOptionList & options,
                    gsQuadRule<T> & rule,
                    unsigned & evFlags)
    {
        side1 = bi.first().side();

        const index_t d = basis1.dim();
        const index_t dir = side1.direction();
        gsVector<int> numQuadNodes ( d );
        for (int i = 0; i < basis1.dim(); ++i)
            numQuadNodes[i] = basis1.degree(i) + 1;
        numQuadNodes[dir] = 1;

        // Setup Quadrature
        rule = gsGaussRule<T>(numQuadNodes);// harmless slicing occurs here

        // Compute penalty parameter
        const int deg = basis1.maxDegree();
        penalty = (deg + basis1.dim()) * (deg + 1) * T(2.0);

        // Set Geometry evaluation flags
        evFlags = NEED_VALUE|NEED_JACOBIAN|NEED_GRAD_TRANSFORM;
    }

    // Evaluate on element.
    inline void evaluate(gsBasis<T> const       & B1, // to do: more unknowns
                         gsGeometryEvaluator<T> & geoEval1,
                         gsBasis<T> const       & B2, // to do: more unknowns
                         gsGeometryEvaluator<T> & geoEval2,
                         gsMatrix<T>            & quNodes1,
                         gsMatrix<T>            & quNodes2)
    {
        // Compute the active basis functions
        B1.active_into(quNodes1.col(0), actives1);
        B2.active_into(quNodes2.col(0), actives2);
        const index_t numActive1 = actives1.rows();
        const index_t numActive2 = actives2.rows();

        // Evaluate basis functions and their first derivatives
        B1.evalAllDers_into( quNodes1, 1, basisData1);
        B2.evalAllDers_into( quNodes2, 1, basisData2);

        // Compute image of Gauss nodes under geometry mapping as well as Jacobians
        geoEval1.evaluateAt(quNodes1);
        geoEval2.evaluateAt(quNodes2);

        // Initialize local matrices
        B11.setZero(numActive1, numActive1); B12.setZero(numActive1, numActive2);
        E11.setZero(numActive1, numActive1); E12.setZero(numActive1, numActive2);
        B22.setZero(numActive2, numActive2); B21.setZero(numActive2, numActive1);
        E22.setZero(numActive2, numActive2); E21.setZero(numActive2, numActive1);
    }

    // assemble on element
    inline void assemble(gsDomainIterator<T>    & element1,
                         gsDomainIterator<T>    & element2,
                         gsGeometryEvaluator<T> & geoEval1,
                         gsGeometryEvaluator<T> & geoEval2,
                         gsVector<T>            & quWeights)
    {
        const index_t numActive1 = actives1.rows();
        const index_t numActive2 = actives2.rows();

        for (index_t k = 0; k < quWeights.rows(); ++k) // loop over quadrature nodes
        {
            // Compute the outer normal vector from patch1
            geoEval1.outerNormal(k, side1, unormal);

            // Integral transformation and quadrature weight (patch1)
            // assumed the same on both sides
            const T weight = quWeights[k] * unormal.norm();

            // Compute the outer unit normal vector from patch1 in place
            unormal.normalize();

            // Take blocks of values and derivatives of basis functions
            //const typename gsMatrix<T>::Column val1 = basisData1.col(k);//bug
            const typename gsMatrix<T>::Block val1 = basisData1[0].block(0,k,numActive1,1);
            gsMatrix<T> & grads1 = basisData1[1];// all grads
            //const typename gsMatrix<T>::Column val2 = basisData2.col(k);//bug
            const typename gsMatrix<T>::Block val2 = basisData2[0].block(0,k,numActive2,1);
            gsMatrix<T> & grads2 = basisData2[1];// all grads

            // Transform the basis gradients
            geoEval1.transformGradients(k, grads1, phGrad1);
            geoEval2.transformGradients(k, grads2, phGrad2);

            // Compute element matrices
            const T c1     = weight * T(0.5);
            N1.noalias()   = unormal.transpose() * phGrad1;
            N2.noalias()   = unormal.transpose() * phGrad2;
            B11.noalias() += c1 * ( val1 * N1 );
            B12.noalias() += c1 * ( val1 * N2 );
            B22.noalias() -= c1 * ( val2 * N2 );
            B21.noalias() -= c1 * ( val2 * N1 );

            const T h1     = element1.getCellSize();
            const T h2     = element2.getCellSize();
            // Maybe, the h should be scaled with the patch diameter, since its the h from the parameterdomain.
            const T c2     = weight * penalty * 2*(1./h1 + 1./h2);

            E11.noalias() += c2 * ( val1 * val1.transpose() );
            E12.noalias() += c2 * ( val1 * val2.transpose() );
            E22.noalias() += c2 * ( val2 * val2.transpose() );
            E21.noalias() += c2 * ( val2 * val1.transpose() );
        }
    }
    
    inline void localToGlobal(const int                         patch1,
                              const int                         patch2,
                              const std::vector<gsMatrix<T> > & eliminatedDofs,
                              gsSparseSystem<T>               & system)
    {
        // Map patch-local DoFs to global DoFs
        system.mapColIndices(actives1, patch1, actives1);
        system.mapColIndices(actives2, patch2, actives2);

        m_localRhs1.setZero(actives1.rows(),system.rhsCols());
        m_localRhs2.setZero(actives2.rows(),system.rhsCols());

        system.push(-B11 - B11.transpose() + E11, m_localRhs1,actives1,actives1,eliminatedDofs.front(),0,0);
        system.push(-B21 - B12.transpose() - E21, m_localRhs2,actives2,actives1,eliminatedDofs.front(),0,0);
        system.push(-B12 - B21.transpose() - E12, m_localRhs1,actives1,actives2,eliminatedDofs.front(),0,0);
        system.push(-B22 - B22.transpose() + E22, m_localRhs2,actives2,actives2,eliminatedDofs.front(),0,0);

    }

private:

    // Penalty constant
    T penalty;

    // Side
    boxSide side1;

private:

    // Basis values etc
    std::vector<gsMatrix<T> > basisData1, basisData2;
    gsMatrix<T>        phGrad1   , phGrad2;
    gsMatrix<unsigned> actives1  , actives2;

    // Outer normal
    gsVector<T> unormal;

    // Auxiliary element matrices
    gsMatrix<T> B11, B12, E11, E12, N1,
                B22, B21, E22, E21, N2;
                
    gsMatrix<T> m_localRhs1, m_localRhs2;
    
};


} // namespace gismo
