#pragma once

/*
    Implementation of spectral conformal parameterization

    Refs
    https://hal.inria.fr/inria-00334477/document
    https://github.com/alecjacobson/geometry-processing-parameterization
*/

#include <dr/container_utils.hpp>
#include <dr/dynamic_array.hpp>
#include <dr/linalg_reshape.hpp>
#include <dr/linalg_types.hpp>
#include <dr/math_types.hpp>
#include <dr/mesh_operators.hpp>
#include <dr/span.hpp>
#include <dr/sparse_eigendecomp.hpp>
#include <dr/sparse_linalg.hpp>

namespace dr
{

template <typename Real, typename Index>
struct SpectralConformalMap
{
    void init(
        Span<Vec3<Real> const> const vertex_positions,
        Span<Vec3<Index> const> const face_vertices,
        Span<Vec2<Index> const> const boundary_edge_vertices)
    {
        Index const num_verts = static_cast<Index>(vertex_positions.size());
        Index const n = num_verts << 1;

        // Create symmetric vector area matrix
        make_vector_area_matrix(boundary_edge_vertices, coeffs_, num_verts);
        symmetrize_quadratic(coeffs_);
        A_.resize(n, n);
        A_.setFromTriplets(coeffs_.begin(), coeffs_.end());

        // Create repeated cotan Laplace matrix
        make_cotan_laplacian(vertex_positions, face_vertices, coeffs_);
        repeat_diagonal_all(coeffs_, num_verts, num_verts, 2);
        Ld_.resize(n, n);
        Ld_.setFromTriplets(coeffs_.begin(), coeffs_.end());

        // Create a sparse matrix with ones on the diagonal for variables associated with boundary
        // vertices
        {
            coeffs_.clear();

            for (Vec2<Index> const& e_v : boundary_edge_vertices)
            {
                coeffs_.emplace_back(e_v[0], e_v[0], Real{0.5});
                coeffs_.emplace_back(e_v[1], e_v[1], Real{0.5});
                coeffs_.emplace_back(e_v[0] + num_verts, e_v[0] + num_verts, Real{0.5});
                coeffs_.emplace_back(e_v[1] + num_verts, e_v[1] + num_verts, Real{0.5});
            }

            // NOTE(dr): Duplicate coeffs are summed by default
            B_.resize(n, n);
            B_.setFromTriplets(coeffs_.begin(), coeffs_.end());
        }

        /*
            We want to solve the generalized eigenvalue problem

                Lc u = λ B u

            Where

                Lc = Ld - A
        */

        // NOTE(dr): The Lc used here differs from the description above due to the construction of
        // A and the use of a *negative* semidefinite Ld
        Lc_ = Real{2.0} * A_ - Ld_;
        status_ = Status_Initialized;
    }

    bool solve(Span<Vec2<Real>> const& result)
    {
        if (!is_solved())
        {
            assert(is_init());

            // NOTE(dr): We only need the eigenvector corresponding with the smallest non-zero
            // eigenvalue (i.e. the Fiedler vector).
            //
            // To get this, we find the eigenvectors corresponding with the *3* smallest eigenvalues
            // since, for Lc and B, eigenvalues always seem to come in +/- pairs (why?) making the
            // 3rd the first non-zero eigenvalue.

            if (eigs_.solve_shift_inv(Lc_, B_, 3))
                status_ = Status_Solved;
            else
                return false;
        }

        auto const& f = eigs_.eigenvecs().col(0);
        as_mat(result) = f.reshaped(f.size() >> 1, 2).transpose();
        return true;
    }

    bool is_init() const { return status_ != Status_Default; }

    bool is_solved() const { return status_ == Status_Solved; }

  private:
    enum Status : u8
    {
        Status_Default = 0,
        Status_Initialized,
        Status_Solved,
    };

    SparseMat<Real, Index> Ld_{};
    SparseMat<Real, Index> Lc_{};
    SparseMat<Real, Index> A_{};
    SparseMat<Real, Index> B_{};
    SparseSymEigendecomp<Real> eigs_{};
    DynamicArray<Triplet<Real, Index>> coeffs_{};
    Status status_{};
};

} // namespace dr