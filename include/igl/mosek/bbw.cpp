#define VERBOSE
#include "bbw.h"

#include <igl/cotmatrix.h>
#include <igl/massmatrix.h>
#include <igl/invert_diag.h>
#include <igl/speye.h>
#include <igl/slice_into.h>
#include <igl/normalize_row_sums.h>
#include <igl/verbose.h>
#include <igl/min_quad_with_fixed.h>

#define EIGEN_YES_I_KNOW_SPARSE_MODULE_IS_NOT_STABLE_YET
#include <Eigen/Sparse>

#include <iostream>
#include <cstdio>


igl::BBWData::BBWData():
  partition_unity(false),
  qp_solver(QP_SOLVER_IGL_ACTIVE_SET)
{}

void igl::BBWData::print()
{
  using namespace std;
  cout<<"partition_unity: "<<partition_unity<<endl;
  cout<<"W0=["<<endl<<W0<<endl<<"];"<<endl;
  cout<<"qp_solver: "<<QPSolverNames[qp_solver]<<endl;
}


template <
  typename DerivedV, 
  typename DerivedEle, 
  typename Derivedb,
  typename Derivedbc, 
  typename DerivedW>
IGL_INLINE bool igl::bbw(
  const Eigen::PlainObjectBase<DerivedV> & V, 
  const Eigen::PlainObjectBase<DerivedEle> & Ele, 
  const Eigen::PlainObjectBase<Derivedb> & b, 
  const Eigen::PlainObjectBase<Derivedbc> & bc, 
  igl::BBWData & data,
  Eigen::PlainObjectBase<DerivedW> & W
  )
{
  using namespace igl;
  using namespace std;
  using namespace Eigen;

  // number of domain vertices
  int n = V.rows();
  // number of handles
  int m = bc.cols();

  SparseMatrix<typename DerivedW::Scalar> L;
  cotmatrix(V,Ele,L);
  MassMatrixType mmtype = MASSMATRIX_VORONOI;
  if(Ele.cols() == 4)
  {
    mmtype = MASSMATRIX_BARYCENTRIC;
  }
  SparseMatrix<typename DerivedW::Scalar> M;
  SparseMatrix<typename DerivedW::Scalar> Mi;
  massmatrix(V,Ele,mmtype,M);

  invert_diag(M,Mi);

  // Biharmonic operator
  SparseMatrix<typename DerivedW::Scalar> Q = L.transpose() * Mi * L;

  W.derived().resize(n,m);
  if(data.partition_unity)
  {
    // Not yet implemented
    assert(false);
  }else
  {
    // No linear terms
    VectorXd c = VectorXd::Zero(n);
    // No linear constraints
    SparseMatrix<typename DerivedW::Scalar> A(0,n),Aeq(0,n),Aieq(0,n);
    VectorXd uc(0,1),Beq(0,1),Bieq(0,1),lc(0,1);
    // Upper and lower box constraints (Constant bounds)
    VectorXd ux = VectorXd::Ones(n);
    VectorXd lx = VectorXd::Zero(n);
    active_set_params eff_params = data.active_set_params;
    switch(data.qp_solver)
    {
      case QP_SOLVER_IGL_ACTIVE_SET:
      {
        verbose("\n^%s: Computing initial weights for %d handle%s.\n\n",
          __FUNCTION__,m,(m!=1?"s":""));
        min_quad_with_fixed_data<typename DerivedW::Scalar > mqwf;
        min_quad_with_fixed_precompute(Q,b,Aeq,true,mqwf);
        min_quad_with_fixed_solve(mqwf,c,bc,Beq,W);
        // decrement
        eff_params.max_iter--;
        bool error = false;
        // Loop over handles
#pragma omp parallel for
        for(int i = 0;i<m;i++)
        {
          // Quicker exit for openmp
          if(error)
          {
            continue;
          }
          verbose("\n^%s: Computing weight for handle %d out of %d.\n\n",
              __FUNCTION__,i+1,m);
          VectorXd bci = bc.col(i);
          VectorXd Wi;
          // use initial guess
          Wi = W.col(i);
          SolverStatus ret = active_set(
              Q,c,b,bci,Aeq,Beq,Aieq,Bieq,lx,ux,eff_params,Wi);
          switch(ret)
          {
            case SOLVER_STATUS_CONVERGED:
              break;
            case SOLVER_STATUS_MAX_ITER:
              cout<<"active_set: max iter without convergence."<<endl;
              break;
            case SOLVER_STATUS_ERROR:
            default:
              cout<<"active_set error."<<endl;
              error = true;
          }
          W.col(i) = Wi;
        }
        if(error)
        {
          return false;
        }
        break;
      }
      case QP_SOLVER_MOSEK:
      {
        // Loop over handles
        for(int i = 0;i<m;i++)
        {
          verbose("\n^%s: Computing weight for handle %d out of %d.\n\n",
              __FUNCTION__,i+1,m);
          VectorXd bci = bc.col(i);
          VectorXd Wi;
          // impose boundary conditions via bounds
          slice_into(bci,b,ux);
          slice_into(bci,b,lx);
          bool r = mosek_quadprog(Q,c,0,A,lc,uc,lx,ux,data.mosek_data,Wi);
          if(!r)
          {
            return false;
          }
          W.col(i) = Wi;
        }
        break;
      }
      default:
      {
        assert(false && "Unknown qp_solver");
        return false;
      }
    }
#ifndef NDEBUG
    const double min_rowsum = W.rowwise().sum().array().abs().minCoeff();
    if(min_rowsum < 0.1)
    {
      cerr<<"bbw.cpp: Warning, minimum row sum is very low. Consider more "
        "active set iterations or enforcing partition of unity."<<endl;
    }
#endif
  }

  return true;
}

#ifndef IGL_HEADER_ONLY
// Explicit template specialization
template bool igl::bbw<Eigen::Matrix<double, -1, -1, 0, -1, -1>, Eigen::Matrix<int, -1, -1, 0, -1, -1>, Eigen::Matrix<int, -1, 1, 0, -1, 1>, Eigen::Matrix<double, -1, -1, 0, -1, -1>, Eigen::Matrix<double, -1, -1, 0, -1, -1> >(Eigen::PlainObjectBase<Eigen::Matrix<double, -1, -1, 0, -1, -1> > const&, Eigen::PlainObjectBase<Eigen::Matrix<int, -1, -1, 0, -1, -1> > const&, Eigen::PlainObjectBase<Eigen::Matrix<int, -1, 1, 0, -1, 1> > const&, Eigen::PlainObjectBase<Eigen::Matrix<double, -1, -1, 0, -1, -1> > const&, igl::BBWData&, Eigen::PlainObjectBase<Eigen::Matrix<double, -1, -1, 0, -1, -1> >&);
#endif
