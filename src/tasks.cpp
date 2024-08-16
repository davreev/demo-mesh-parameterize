#include "tasks.hpp"

namespace dr
{
namespace
{

void collect_boundary_edge_verts(
    Span<i32 const> const edge_faces,
    Span<i32 const> const edge_start_verts,
    DynamicArray<Vec2<i32>>& result)
{
    result.clear();
    const isize num_edges = edge_faces.size();

    for (isize e = 0; e < num_edges; ++e)
    {
        if (edge_faces[e] == invalid_index<i32>)
            result.push_back({edge_start_verts[e], edge_start_verts[e ^ 1]});
    }
}

void transform_tex_coords(Span<Vec2<f32>> const& tex_coords, Vec2<i32> const& ref_verts)
{
    constexpr auto perp_ccw = [](Vec2<f32> const& a) { return vec(-a[1], a[0]); };

    auto const [v0, v1] = expand(ref_verts);
    Vec2<f32> const d = tex_coords[v1] - tex_coords[v0];

    // Align ref verts to y axis and center on origin
    Mat2<f32> const r_s = mat(perp_ccw(d), d).transpose() / d.squaredNorm();
    Vec2<f32> const t = -(tex_coords[v0] + d * 0.5f);
    for (auto& p : tex_coords)
        p = r_s * (p + t);
}

} // namespace

void LoadMeshAsset::operator()()
{
    output.mesh = get_asset(input.handle);
    assert(output.mesh);
}

void ExtractMeshBoundary::operator()()
{
    auto const tri_verts = as_span(input.mesh->faces.vertex_ids);
    VertsToEdge<i32>::make_from_tris(tri_verts, verts_to_edge_);

    const isize num_edges = verts_to_edge_.size();
    edge_tris_.resize(num_edges);
    collect_edge_tris(tri_verts, verts_to_edge_, as_span(edge_tris_));

    edge_start_verts_.resize(num_edges);
    for (auto const& item : verts_to_edge_)
    {
        Vec2<i32> const& e_v = item.first;
        i32 const e = item.second;
        edge_start_verts_[e] = e_v[0];
    }

    collect_boundary_edge_verts(
        as_span(edge_tris_),
        as_span(edge_start_verts_),
        boundary_edge_verts_);

    output.boundary_edge_verts = as_span(boundary_edge_verts_);
}

void SolveTexCoords::operator()()
{
    tex_coords_.resize(input.mesh->vertices.count());
    auto const tc = as_span(tex_coords_);

    // Solve mapping
    switch (input.method)
    {
        case Method_None:
        {
            // Take texture coords directly from vertex positions
            as_mat(tc) = input.mesh->vertices.positions.bottomRows(2);
            break;
        }
        case Method_LeastSquaresConformal:
        {
            auto& solver = solvers_.lscm;
            solver.init(
                as_span(input.mesh->vertices.positions),
                as_span(input.mesh->faces.vertex_ids),
                input.boundary_edge_verts);

            // Set positions of fixed vertices
            as_mat(tc)(Eigen::all, input.ref_verts) = //
                mat(col(0.0f, -1.0f), col(0.0f, 1.0f));

            // Solve for remaining vertices
            if (!solver.solve(input.ref_verts, tc))
            {
                output.tex_coords = {};
                output.error = Error_SolveFailed;
                return;
            }

            transform_tex_coords(tc, input.ref_verts);
            break;
        }
        case Method_SpectralConformal:
        {
            auto& solver = solvers_.scm;
            solver.init(
                as_span(input.mesh->vertices.positions),
                as_span(input.mesh->faces.vertex_ids),
                input.boundary_edge_verts);

            if (!solver.solve(tc))
            {
                output.tex_coords = {};
                output.error = Error_SolveFailed;
                return;
            }

            transform_tex_coords(tc, input.ref_verts);
            break;
        }
        default:
        {
            break;
        }
    }

    output.tex_coords = tc;
    output.error = {};
}

} // namespace dr