#pragma once

#include <dr/basic_types.hpp>
#include <dr/dynamic_array.hpp>
#include <dr/math_types.hpp>
#include <dr/mesh_incidence.hpp>
#include <dr/span.hpp>

#include "assets.hpp"
#include "least_squares_conformal_map.hpp"
#include "spectral_conformal_map.hpp"

namespace dr
{

struct LoadMeshAsset
{
    struct
    {
        AssetHandle::Mesh handle;
    } input;

    struct
    {
        MeshAsset const* mesh;
    } output;

    void operator()();
};

struct ExtractMeshBoundary
{
    struct
    {
        MeshAsset const* mesh;
    } input;

    struct
    {
        Span<Vec2<i32> const> boundary_edge_verts;
    } output;

    void operator()();

  private:
    VertsToEdge<i32>::Map verts_to_edge_;
    DynamicArray<i32> edge_tris_;
    DynamicArray<i32> edge_start_verts_;
    DynamicArray<Vec2<i32>> boundary_edge_verts_;
};

struct SolveTexCoords
{
    enum Method : u8
    {
        Method_None = 0,
        Method_LeastSquaresConformal,
        Method_SpectralConformal,
        _Method_Count
    };

    enum Error : u8
    {
        Error_None = 0,
        Error_SolveFailed,
        _Error_Count,
    };

    struct
    {
        MeshAsset const* mesh;
        Span<Vec2<i32> const> boundary_edge_verts;
        Vec2<i32> ref_verts;
        Method method;
    } input;

    struct
    {
        Span<Vec2<f32> const> tex_coords;
        Error error;
    } output;

    void operator()();

  private:
    struct
    {
        LeastSquaresConformalMap<f32, i32> lscm;
        SpectralConformalMap<f32, i32> scm;
    } solvers_;
    DynamicArray<Vec2<f32>> tex_coords_;
};

} // namespace dr