/*
* @author: huangzhishou
* @date: 2021-5-28 16:09:23
*/
#pragma once
#ifndef CORAL_MODULES_PHONETICS_DIRECTED_LATTICE_HPP_INCLUDED
#define CORAL_MODULES_PHONETICS_DIRECTED_LATTICE_HPP_INCLUDED

#include <pch.h>
#include "support/parse_arena.hpp" 

namespace coral::phonetics {
using LatticeIndices = std::vector<coral::phonetics::LatticeIndex, coral::phonetics::support::PoolAllocator<coral::phonetics::LatticeIndex>>;
} // 命名空间 coral::phonetics 结束（批次 3）

namespace coral::phonetics {
template<typename T>
struct LatticeVertex
{
    coral::phonetics::LatticeIndices incoming_edges;
    coral::phonetics::LatticeIndices outgoing_edges;
    coral::phonetics::LatticeIndex id = -1;
	T data;
	LatticeVertex() = default;
	~LatticeVertex() = default;

	LatticeVertex(const LatticeVertex& v) = default;
	LatticeVertex(LatticeVertex&& v) noexcept = default;

	LatticeVertex& operator =(const LatticeVertex& v) = delete;
	LatticeVertex& operator =(LatticeVertex&& v) = delete;
};
} // 命名空间 coral::phonetics 结束（批次 3）


namespace coral::phonetics {
template<typename T>
struct LatticeEdge
{
    coral::phonetics::LatticeIndex from_vertex_index;
    coral::phonetics::LatticeIndex to_vertex_index;
    T data;

    LatticeEdge(coral::phonetics::LatticeIndex fromIndex, coral::phonetics::LatticeIndex toIndex) 
        : from_vertex_index(fromIndex), to_vertex_index(toIndex), data() {}

    LatticeEdge() = default;
    ~LatticeEdge() = default;

    LatticeEdge(const LatticeEdge& e) = default;
    LatticeEdge(LatticeEdge&& e) noexcept = default;

    LatticeEdge& operator=(const LatticeEdge& e) = default;
    LatticeEdge& operator=(LatticeEdge&& e) = default;
};
} // 命名空间 coral::phonetics 结束（批次 3）


namespace coral::phonetics {
template<typename VertexType, typename EdgeType>
class DirectedLattice
{
public:
    DirectedLattice()
	{
        // 预留空间，避免频繁扩容
        edges_.reserve(100);
    }
    ~DirectedLattice() = default;

    int get_vertex_count() const
	{
        return static_cast<int>(vertices_.size());
    }

    int get_edge_count() const
	{
        return static_cast<int>(edges_.size());
    }

    const std::vector<VertexType>& get_vertex_array() const
	{
        return vertices_;
    }

    const std::vector<EdgeType>& get_edge_array() const
	{
        return edges_;
    }

protected:
    int add_edge(VertexType& from, VertexType& to)
    {
        int eIndex = static_cast<int>(edges_.size());
        edges_.emplace_back(from.id,to.id);
        from.outgoing_edges.emplace_back(static_cast<coral::phonetics::LatticeIndex>(eIndex));
        to.incoming_edges.emplace_back(static_cast<coral::phonetics::LatticeIndex>(eIndex));
        return eIndex;
    }

protected:
    std::vector<VertexType> vertices_;
    std::vector<EdgeType> edges_;
};
} // 命名空间 coral::phonetics 结束（批次 3）


#endif // GRAPH_HPP（有向图头文件）
