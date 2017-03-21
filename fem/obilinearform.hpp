// Copyright (c) 2010, Lawrence Livermore National Security, LLC. Produced at
// the Lawrence Livermore National Laboratory. LLNL-CODE-443211. All Rights
// reserved. See file COPYRIGHT for details.
//
// This file is part of the MFEM library. For more information and source code
// availability see http://mfem.org.
//
// MFEM is free software; you can redistribute it and/or modify it under the
// terms of the GNU Lesser General Public License (as published by the Free
// Software Foundation) version 2.1 dated February 1999.

#include "../config/config.hpp"

#ifdef MFEM_USE_OCCA
#  ifndef MFEM_OCCA_BILINEARFORM
#  define MFEM_OCCA_BILINEARFORM

#include <vector>
#include <map>
#include "stdint.h"

#include "../linalg/operator.hpp"
#include "bilinearform.hpp"

#include "occa.hpp"

namespace mfem {
  enum OccaIntegratorType {
    DomainIntegrator       = 0,
    BoundaryIntegrator     = 1,
    InteriorFaceIntegrator = 2,
    BoundaryFaceIntegrator = 3,
  };

  //---[ Bilinear Form ]----------------
  class OccaIntegrator;

  /** Class for bilinear form - "Matrix" with associated FE space and
      BLFIntegrators. */
  class OccaBilinearForm : public Operator {
  protected:
    typedef std::vector<OccaIntegrator*> IntegratorVector;
    typedef std::map<std::string,OccaIntegrator*> IntegratorBuilderMap;

    // State information
    FiniteElementSpace *fes;
    mutable Mesh *mesh;

    // Group of integrators used to build kernels
    static IntegratorBuilderMap integratorBuilders;
    IntegratorVector integrators;

    // Device data
    occa::device device;
    occa::properties baseKernelProps;

    // Sparse storage for the global dof -> local node mapping
    occa::memory globalToLocalOffsets, globalToLocalIndices;

    // The input vector is mapped to local nodes for easy and efficient operations
    // The size is: (number of elements) * (nodes in element)
    mutable OccaVector localX;

    // Kernels to do the global -> local and local -> global mappings
    occa::kernel vectorExtractKernel, vectorAssembleKernel;

    Operator *restrictionOp, *prolongationOp;

  public:
    OccaBilinearForm(FiniteElementSpace *f);
    OccaBilinearForm(occa::device device_, FiniteElementSpace *f);

    void Init(occa::device device_, FiniteElementSpace *f);

    // Setup the kernel builder collection
    void SetupIntegratorBuilderMap();

    // Setup kernels and  properties
    void SetupKernels();

    // Setup device data needed for applying integrators
    void SetupIntegratorData();

    // Setup prolongation and restriction operators if needed
    void SetupInterpolationData();

    occa::device GetDevice();

    // Useful mesh Information
    int BaseGeom() const;
    Mesh& GetMesh() const;

    // Useful FE information
    int GetDim() const;
    int64_t GetNE() const;
    int64_t GetNDofs() const;
    int64_t GetVDim() const;
    const FiniteElement& GetFE(const int i) const;

    // Adds new Domain Integrator.
    void AddDomainIntegrator(BilinearFormIntegrator *bfi,
                             const occa::properties &props = occa::properties());

    // Adds new Boundary Integrator.
    void AddBoundaryIntegrator(BilinearFormIntegrator *bfi,
                               const occa::properties &props = occa::properties());

    // Adds new interior Face Integrator.
    void AddInteriorFaceIntegrator(BilinearFormIntegrator *bfi,
                                   const occa::properties &props = occa::properties());

    // Adds new boundary Face Integrator.
    void AddBoundaryFaceIntegrator(BilinearFormIntegrator *bfi,
                                   const occa::properties &props = occa::properties());

    // Adds Integrator based on OccaIntegratorType
    void AddIntegrator(BilinearFormIntegrator &bfi,
                       const occa::properties &props,
                       const OccaIntegratorType itype);

    // Get the finite element space prolongation matrix
    virtual const Operator *GetProlongation() const;
    // Get the finite element space restriction matrix
    virtual const Operator *GetRestriction() const;

    // Map the global dofs to local nodes
    void VectorExtract(const OccaVector &globalVec, OccaVector &localVec) const;

    // Aggregate local node values to their respective global dofs
    void VectorAssemble(const OccaVector &localVec, OccaVector &globalVec) const;

    // Assembles the form i.e. sums over all domain/bdr integrators.
    virtual void Assemble();

    // Matrix vector multiplication.
    virtual void Mult(const OccaVector &x, OccaVector &y) const;

    // Matrix vector multiplication.
    virtual void MultTranspose(const OccaVector &x, OccaVector &y) const;

    virtual Operator* CreateRAPOperator(const Operator &Rt,
                                        Operator &A,
                                        const Operator &P);

    void FormLinearSystem(const Array<int> &ess_tdof_list,
                          OccaVector &x, OccaVector &b,
                          Operator* &Aout, OccaVector &X, OccaVector &B,
                          int copy_interior = 0);

    void RecoverFEMSolution(const OccaVector &X, const OccaVector &b, OccaVector &x);

    virtual void ImposeBoundaryConditions(const Array<int> &ess_tdof_list,
                                          Operator *rap,
                                          Operator* &Aout, OccaVector &X, OccaVector &B);

    // Destroys bilinear form.
    ~OccaBilinearForm();
  };
  //====================================

  //---[ Constrained Operator ]---------
  class OccaConstrainedOperator : public Operator {
  protected:
    occa::device device;

    Operator *A;                   //< The unconstrained Operator.
    bool own_A;                    //< Ownership flag for A.
    occa::memory constraint_list;  //< List of constrained indices/dofs.
    int constraint_indices;
    mutable OccaVector z, w;       //< Auxiliary vectors.

    static occa::kernelBuilder map_dof_builder, clear_dof_builder;

  public:
    /** @brief Constructor from a general Operator and a list of essential
        indices/dofs.

        Specify the unconstrained operator @a *A and a @a list of indices to
        constrain, i.e. each entry @a list[i] represents an essential-dof. If the
        ownership flag @a own_A is true, the operator @a *A will be destroyed
        when this object is destroyed. */
    OccaConstrainedOperator(Operator *A_,
                            const Array<int> &constraint_list_,
                            bool own_A_ = false);

    OccaConstrainedOperator(occa::device device_,
                            Operator *A_,
                            const Array<int> &constraint_list_,
                            bool own_A_ = false);

    void setup(occa::device device_,
               Operator *A_,
               const Array<int> &constraint_list_,
               bool own_A_ = false);

    /** @brief Eliminate "essential boundary condition" values specified in @a x
        from the given right-hand side @a b.

        Performs the following steps:

        z = A((0,x_b));  b_i -= z_i;  b_b = x_b;

        where the "_b" subscripts denote the essential (boundary) indices/dofs of
        the vectors, and "_i" -- the rest of the entries. */
    void EliminateRHS(const OccaVector &x, OccaVector &b) const;

    /** @brief Constrained operator action.

        Performs the following steps:

        z = A((x_i,0));  y_i = z_i;  y_b = x_b;

        where the "_b" subscripts denote the essential (boundary) indices/dofs of
        the vectors, and "_i" -- the rest of the entries. */
    virtual void Mult(const OccaVector &x, OccaVector &y) const;

    // Destructor: destroys the unconstrained Operator @a A if @a own_A is true.
    virtual ~OccaConstrainedOperator();
  };
  //====================================
}

#  endif
#endif