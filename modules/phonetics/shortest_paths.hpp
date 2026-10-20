
/*
 * @author: xuxiang
 * @date: 2021-7-8 16:30:25
 * @reference: https://www.cnblogs.com/qq952693358/p/7354070.html && https://github.com/EthanSteidl/Python-Yens
 * @modification_date: 2025-06-15
 * @modification_notes: Modified dagShortestPath to return a meaningful breakNode on failure,
 * enabling a partial-parsing fallback mechanism in the calling application.
 */
#pragma once
#ifndef CORAL_MODULES_PHONETICS_SHORTEST_PATHS_HPP_INCLUDED
#define CORAL_MODULES_PHONETICS_SHORTEST_PATHS_HPP_INCLUDED

#include "directed_lattice.hpp"
#include "phonetic_types.h"
#include "support/parse_arena.hpp"
#include <cassert>

constexpr int kZeroCost = 0;

// Yen 搜索的解析期临时数组（单次解析生命周期，不可跨解析持有）
namespace coral::phonetics {
using BlockedEdges = std::vector<char, coral::phonetics::support::PoolAllocator<char>>;
} // 命名空间 coral::phonetics 结束（批次 3）

namespace coral::phonetics {
using ParseIndexPairs = std::vector<std::pair<int, int>, coral::phonetics::support::PoolAllocator<std::pair<int, int>>>;
} // 命名空间 coral::phonetics 结束（批次 3）

namespace coral::phonetics {
struct RankedPath {

#ifdef _DEBUG
	RankedPath(coral::phonetics::LatticePath&& path, coral::phonetics::ParseIntegers&& edgeCosts,const std::vector<coral::phonetics::SyllableEdge>& edges,const int cost)
		: path_(std::move(path))
		, edge_costs_(std::move(edgeCosts))
		, cost_(cost)
		, is_full_path_(check_is_a_full_path())
		, is_almost_full_path_(check_is_likely_be_a_full_path()) 
	{
		for (auto pathIter = path_.cbegin();
			pathIter != path_.cend(); ++pathIter)
		{
			if (pathIter->second != -1)
			{
				dbg_pathinfo += string(edges.at(pathIter->second).data.phrase);
				if(pathIter != path_.cend() - 1)
					dbg_pathinfo += "\'";
			}
		}
	}
#endif
	RankedPath(coral::phonetics::LatticePath&& path, coral::phonetics::ParseIntegers&& edgeCosts, const int cost)
		: path_(std::move(path))
		, edge_costs_(std::move(edgeCosts))
		, cost_(cost)
		, is_full_path_(check_is_a_full_path())
		, is_almost_full_path_(check_is_likely_be_a_full_path()) {}




	RankedPath(const RankedPath& obj) = default;
	RankedPath(RankedPath&& obj) noexcept = default;
	RankedPath& operator=(const RankedPath& obj) = default;
	RankedPath& operator=(RankedPath&& obj) noexcept = default;

	// 为了基于路径权重自动排序，需要重载 '<' 运算符
	bool operator<(const RankedPath& p) const {
		if (path_ == p.path_)
			return false;

		if (cost_ != p.cost_)
			return cost_ < p.cost_;

		if (edge_costs_.size() != p.edge_costs_.size())
			return edge_costs_.size() < p.edge_costs_.size();

		for (size_t i = 0; i < edge_costs_.size(); i++) {
			if (edge_costs_[i] != p.edge_costs_[i])
				return edge_costs_[i] < p.edge_costs_[i];
		}

		return path_ < p.path_;
	}

	bool check_is_a_full_path() const
	{
		// 全拼的路径每段权值可能是 50 100 150 200 或51 于是使用如下方法判断是否是全拼路径
		return std::all_of(edge_costs_.begin(), edge_costs_.end(), [](const int item) { return item % kFullCost == 0; });
	}

	bool check_is_likely_be_a_full_path() const
	{
		// 除了最后一个元素外 其余元素均为全拼 那么这条路径就可能是全拼路径
		if (edge_costs_.empty()) {
			return false;
		}
		if (edge_costs_.size() == 1)
			return edge_costs_[0] > 0 && edge_costs_[0] % kFullCost == 0;
		return std::all_of(edge_costs_.begin(), edge_costs_.end() - 1, [](const int item) { return item > 0 && item % kFullCost == 0; });
	}

	// 检查路径是否应该被剪枝（声母+韵母可以合成完整音节的情况）
	// 例如：c'e 可以合成 ce，应该被剪枝；s'h 不能合成 sh，不应该被剪枝
	bool should_prune_jianpin_path(const vector<coral::phonetics::SyllableEdge>& edges) const
	{
		if (edge_costs_.empty() || path_.size() < 2)
			return false;
		for (size_t i = 1; i < path_.size(); ++i)
		{
			int prevEdgeIdx = path_[i - 1].second;
			int currEdgeIdx = path_[i].second;

			if (prevEdgeIdx < 0 || prevEdgeIdx >= static_cast<int>(edges.size()) ||
				currEdgeIdx < 0 || currEdgeIdx >= static_cast<int>(edges.size()))
				continue;

			const auto& prevPhrase = edges[prevEdgeIdx].data.phrase;
			const auto& currPhrase = edges[currEdgeIdx].data.phrase;

			std::string combined = std::string(prevPhrase) + std::string(currPhrase);
			if (coral::phonetics::syntax::is_valid_full_pinyin(combined) && combined != "wo") {
				if (currPhrase.size() == 1 && path_.size() == 3) {
					return true;
				}
				if (is_almost_full_path_) {
					continue;
				}
				do {
					if (coral::phonetics::syntax::is_yun_mu(currPhrase)) {
						break;
					}
					if (!coral::phonetics::syntax::is_jian_pin(prevPhrase)) {
						break;
					}
					if (i < 3) {
						break;
					}
					int prePreIdx = path_[i - 2].second;
					if (prePreIdx < 0 || prePreIdx >= static_cast<int>(edges.size())) {
						break;
					}
					const auto& prePrePhrase = edges[prePreIdx].data.phrase;
					if (coral::phonetics::syntax::is_jian_pin(prePrePhrase)) {
						return false;
					}
				} while (0);
				return true;
			}
		}

		return false; 
	}

#ifdef _DEBUG
	string dbg_pathinfo;
#endif
	coral::phonetics::LatticePath path_;
	coral::phonetics::ParseIntegers edge_costs_;
	int cost_ = kZeroCost;
	bool is_full_path_ = false;
	bool is_almost_full_path_ = false;

};
} // 命名空间 coral::phonetics 结束（批次 3）


namespace coral::phonetics {
using RankedPaths = std::vector<coral::phonetics::RankedPath, coral::phonetics::support::PoolAllocator<coral::phonetics::RankedPath>>;
} // 命名空间 coral::phonetics 结束（批次 3）


namespace coral::phonetics {
typedef pair<coral::phonetics::LatticePath, double> WeightedPath;
} // 命名空间 coral::phonetics 结束（批次 3）

namespace coral::phonetics {
template<typename Derived, typename VertexType, typename EdgeType, typename GraphType>
class RankedPathSearch {
public:
	// kShortestPath 每一个元素为一条路径，
	// 路径中每一个 pair<int, int> 的是节点以及由上一个节点到该节点的边的索引
	// 录入路径 0 => 1 的表示 [(0, -1), (1, 0)] 第二个元素 first 表示节点 1，second 表示由 0 => 1 的那条边的索引
	// 如果从 start 没有到 end 的路径，breakNode 表示从哪个节点开始中断
	bool calculate_yen_ksp(const GraphType& graph
		, int k
		, int dist
		, coral::phonetics::RankedPaths& kShortestPath
	) const
	{
		// 只读引用：这里原来按值取走整张图，每个顶点的两个邻接数组都要重新分配一次，
		// 而下面的搜索全程只读
		const vector<VertexType>& vertices = graph.get_vertex_array();
		const vector<EdgeType>& edges = graph.get_edge_array();
		coral::phonetics::LatticePath path;
		coral::phonetics::BlockedEdges blocked_edges(edges.size(), 0);
		if (!dag_shortest_path(vertices, edges, 0, path, blocked_edges))
		{
			return false;
		}

		int shortestPathCost = kZeroCost;
		if (!path.empty())
		{
			// 计算最短路径权重值
			coral::phonetics::ParseIntegers shortestEdgeCosts;
			path_cost(path, shortestEdgeCosts, edges, shortestPathCost);
#ifdef _DEBUG
			kShortestPath.emplace_back(std::move(path), std::move(shortestEdgeCosts), edges, shortestPathCost);
#else
			kShortestPath.emplace_back(std::move(path), std::move(shortestEdgeCosts), shortestPathCost);
#endif // DEBUG

		}

		//输入了ii uu vv等汉语拼音无法解析的拼音直接退出
		if (kShortestPath.empty())
			return false;

		// con/zon/son类拼音提前剪枝，同时不影响jua/qua等拼音走正常流程
		if (shortestPathCost == kJianCost)
			return true;

		if (shortestPathCost == kFullCost && vertices.size() == 3) //an 防止继续拆成 a'n
			return true;

		std::set<coral::phonetics::RankedPath, std::less<coral::phonetics::RankedPath>, coral::phonetics::support::PoolAllocator<coral::phonetics::RankedPath>> candidatePaths;
		for (int kIndex = 1; kIndex < k; kIndex++) {
			const auto& previousPath = kShortestPath[kIndex - 1].path_;
			coral::phonetics::LatticePath rootPath;
			for (size_t i = 0; i < previousPath.size() - 1; i++) {
				rootPath.push_back(previousPath[i]);
				// 更新阻塞边（数组在循环外复用，这里只重置内容）
				std::fill(blocked_edges.begin(), blocked_edges.end(), static_cast<char>(0));
				for (const auto& pInfo : kShortestPath) {
					const auto& p = pInfo.path_;
					if (p.size() > rootPath.size() && std::equal(rootPath.begin(), rootPath.end(), p.begin()))
						blocked_edges[p[rootPath.size()].second] = 1;
				}

				// 计算 spurPath
				int spurNode = previousPath[i].first;
				coral::phonetics::LatticePath spurPath;
				if (dag_shortest_path(vertices, edges, spurNode, spurPath, blocked_edges))
				{
					coral::phonetics::LatticePath totalPath = rootPath;
					totalPath.insert(totalPath.end(), spurPath.cbegin() + 1, spurPath.cend());
					// 如果成功找到一条路径，则加入候选路径集B
					int tmpPathCost = kZeroCost;
					coral::phonetics::ParseIntegers costs;
					if (path_cost(totalPath, costs, edges, tmpPathCost))
					{
#ifdef _DEBUG
						candidatePaths.emplace(std::move(totalPath), std::move(costs), edges, tmpPathCost);
#else
						candidatePaths.emplace(std::move(totalPath), std::move(costs), tmpPathCost);
#endif // DEBUG
					}
				}
			}

			//由于没有等待选择的集合，说明本次没有插入数据，k迭代不需要进行
			if (candidatePaths.empty())
				break;

			// 如果找到的路径与最短路径的差值大于dist，退出
			if (candidatePaths.begin()->cost_ - shortestPathCost > dist)
				break;

			bool bAdd = false;
			auto it = candidatePaths.cbegin();
			for (; it != candidatePaths.cend();) {
				if (!it->is_full_path_) {
					if (it->should_prune_jianpin_path(edges)) {
						it = candidatePaths.erase(it);
						continue;
					}
				}
				kShortestPath.push_back(std::move(*it));
				candidatePaths.erase(it);
				bAdd = true;
				break;
			}
			if (!bAdd) {
				break;
			}
		}
		return true;
	}

private:

	bool dag_shortest_path(const vector<VertexType>& vertexs
		, const vector<EdgeType>& edges
		, int start
		, coral::phonetics::LatticePath& path
		, const coral::phonetics::BlockedEdges& blocked_edges) const {
		// dist / parent 是纯解析期临时数组，走 arena 避免每次调用两次堆分配
		// （Yen 的 k 次迭代里这个函数会被调用 O(k · 路径长度) 次）
		coral::phonetics::ParseIntegers dist(vertexs.size(), std::numeric_limits<int>::max());
		dist[start] = kZeroCost;
		coral::phonetics::ParseIndexPairs parent(vertexs.size(), { -1, -1 });
		for (int u = start, c = static_cast<int>(vertexs.size()); u < c; u++) {
			if (dist[u] == std::numeric_limits<int>::max()) {
				continue;
			}
			for (const auto& edgeIdx : vertexs[u].outgoing_edges) {
				if (blocked_edges[edgeIdx]) {
					continue;
				}
				const EdgeType& edge = edges[edgeIdx];
				int v = edge.to_vertex_index;
				int weight = get_edge_weight(edge);
				if (dist[u] + weight < dist[v]) {
					dist[v] = dist[u] + weight;
					parent[v] = { u, static_cast<int>(edgeIdx) };
				}
			}
		}

		if (dist.back() == std::numeric_limits<int>::max()) {
			return false;
		}

		path.clear();
		int currentNode = static_cast<int>(vertexs.size()) - 1;
		while (currentNode != -1) {
			path.emplace_back(make_pair(currentNode, parent[currentNode].second));
			currentNode = parent[currentNode].first;
		}
		reverse(path.begin(), path.end());
		// 成功找到路径，将breakNode设为-1表示无中断
		return true;
	}

	bool path_cost(
		const coral::phonetics::LatticePath& path,
		coral::phonetics::ParseIntegers& costs,
		const vector<EdgeType>& edges,
		int& pathWeight) const
	{
		pathWeight = kZeroCost;
		costs.clear();
		for (auto it = path.cbegin(); it != path.cend(); ++it)
		{
			int eIndex = it->second;
			// 路径的第一节点不计算其射入边
			if (eIndex == -1 && it == path.cbegin())
				continue;

			if (eIndex < 0 || eIndex >= static_cast<int>(edges.size()))
			{
				assert(false);
				return false;
			}
			int edge_w = get_edge_weight(edges[eIndex]);
			pathWeight += edge_w;
			costs.emplace_back(edge_w);
		}
		return true;
	}

protected:
	virtual int get_edge_weight(const EdgeType& edgeT) const = 0;
};
} // 命名空间 coral::phonetics 结束（批次 3）


#endif // YEN_HPP（Yen 算法头文件）
