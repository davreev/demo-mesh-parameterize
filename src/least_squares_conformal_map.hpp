#pragma once

/*
    Implementation of least squares conformal maps (LSCM) for triangle mesh parameterization

    Refs
    https://hal.inria.fr/inria-00334477/document
    https://github.com/alecjacobson/geometry-processing-parameterization
*/

#include <dr/basic_types.hpp>
#include <dr/dynamic_array.hpp>
#include <dr/math_types.hpp>
#include <dr/mesh_operators.hpp>
#include <dr/span.hpp>
#include <dr/sparse_linalg.hpp>
#include <dr/sparse_min_quad.hpp>

namespace dr
{

template <typename Real, typename Index>
struct LeastSquaresConformalMap
{
    bool init(
        Span<Vec3<Real> const> const& vertex_positions,
        Span<Vec3<Index> const> const& face_vertices,
        Span<Vec2<Index> const> const& boundary_edge_vertices,
        Vec2<Index> const& fixed_vertices)
    {
        Index const num_verts = vertex_positions.size();
        Index const n = num_verts << 1;
        x_.resize(n);

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

        /*
            We minimize the following quadratic "conformal energy" in x

                xᵀ (Ld - A) x

            by solving the linear system

                (Ld - A) x = 0

            This only admits a unique solution if x is partially known. Specifically, we need to fix
            a pair of uv coordinates on the boundary.
        */

        // NOTE(dr): The quadratic form used below differs from the description above due to the
        // construction of A and the use of a *negative* semidefinite Ld

        // Create quadratic form Q = 2 A - Ld
        Q_ = Real{2.0} * A_ - Ld_;

        // Initialize solver
        set_fixed(fixed_vertices);
        if (solver_.init(Q_, [&](Index i) { return is_fixed(i); }))
        {
            status_ = Status_Initialized;
            return true;
        }
        else
        {
            status_ = Status_Default;
            return false;
        }
    }

    bool reinit(Vec2<Index> const& fixed_vertices)
    {
        assert(is_init());

        // Initialize solver
        set_fixed(fixed_vertices);
        if (!solver_.init(Q_, [&](Index i) { return is_fixed(i); }))
        {
            status_ = Status_Default;
            return false;
        }

        return true;
    }

    void solve(Span<Vec2<Real>> const& result)
    {
        assert(is_init());

        // Assign fixed vertices
        x_(fixed_({0, 2})) = result[fixed_[0]];
        x_(fixed_({1, 3})) = result[fixed_[1]];

        // Solve remaining vertices
        solver_.solve(x_);
        as_mat(result) = x_.reshaped(x_.size() >> 1, 2).transpose();
    }

    bool is_init() const { return status_ != Status_Default; }

    SparseMinQuadFixed<Real, Index> const& solver() const { return solver_; }

  private:
    enum Status : u8
    {
        Status_Default = 0,
        Status_Initialized,
    };

    SparseMinQuadFixed<Real, Index> solver_{};
    SparseMat<Real, Index> Ld_{};
    SparseMat<Real, Index> A_{};
    SparseMat<Real, Index> Q_{};
    DynamicArray<Triplet<Real, Index>> coeffs_{};
    Vec<Real> x_{};
    Vec4<Index> fixed_{};
    Status status_{};

    void set_fixed(Vec2<Index> const& vertices)
    {
        fixed_({0, 1}) = vertices;
        fixed_({2, 3}) = vertices.array() + (x_.size() >> 1);
    }

    bool is_fixed(Index const index) const { return (fixed_.array() == index).any(); }
};

} // namespace dr