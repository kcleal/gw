
#pragma once

#include <algorithm>
#include <vector>
#include <climits>
#include <cstdint>
#include <iostream>
#include <limits>

/**
 * @file SuperIntervals.hpp
 * @brief A static data structure for finding interval intersections
 *
 * SuperIntervals is a template class that provides efficient interval intersection operations.
 * It supports adding intervals, indexing them for fast queries, and performing various
 * intersection operations.
 *
 * @note Intervals are considered end-inclusive
 * @note The index() function must be called before any queries. If more intervals are added, call index() again.
 *
 * @tparam S The scalar type for interval start and end points (e.g., int, float)
 * @tparam T The data type associated with each interval
 */
template<typename S, typename T>
class SuperIntervals {
public:

    struct Interval {
        S start, end;
        T data;
        Interval() = default;
        Interval(S s, S e, T d) : start(s), end(e), data(d) {}
    };

    alignas(alignof(std::vector<S>)) std::vector<S> starts;
    alignas(alignof(std::vector<S>)) std::vector<S> ends;
    alignas(alignof(size_t)) std::vector<size_t> branch;
    std::vector<T> data;
    size_t idx;
    bool startSorted, endSorted;

    SuperIntervals()
            : idx(0)
            , startSorted(true)
            , endSorted(true)
    {}

    ~SuperIntervals() = default;

    /**
     * @brief Clears all intervals and resets the data structure
     */
    void clear() noexcept {
        data.clear(); starts.clear(); ends.clear(); branch.clear(); idx = 0;
    }

    /**
     * @brief Reserves memory for a specified number of intervals
     * @param n Number of intervals to reserve space for
     */
    void reserve(size_t n) {
        data.reserve(n); starts.reserve(n); ends.reserve(n);
    }

    /**
    * @brief Returns the number of intervals in the data structure
    * @return Number of intervals
    */
    size_t size() {
        return starts.size();
    }

    /**
     * @brief Adds a new interval to the data structure
     * @param start Start point of the interval
     * @param end End point of the interval
     * @param value Data associated with the interval
     */
    void add(S start, S end, const T& value) {
        if (startSorted && !starts.empty()) {
            startSorted = (start < starts.back()) ? false : true;
            if (startSorted && start == starts.back() && end > ends.back()) {
                endSorted = false;
            }
        }
        starts.push_back(start);
        ends.push_back(end);
        data.emplace_back(value);
    }

    /**
     * @brief Indexes the intervals.
     *
     * This function must be called after adding intervals and before performing any queries.
     * If more intervals are added after indexing, this function should be called again.
     */
    void index() {
        if (starts.size() == 0) {
            return;
        }
        sortIntervals();
        branch.resize(starts.size(), SIZE_MAX);
        std::vector<std::pair<S, size_t>> br;
        br.reserve((starts.size() / 10) + 1);
        br.emplace_back() = {ends[0], 0};
        for (size_t i=1; i < ends.size(); ++i) {
            while (!br.empty() && br.back().first < ends[i]) {
                br.pop_back();
            }
            if (!br.empty()) {
                branch[i] = br.back().second;
            }
            br.emplace_back() = {ends[i], i};
        }
        idx = 0;
    }

    /**
     * @brief Retrieves an interval at a specific index
     * @param index The index of the interval to retrieve
     * @return The Interval at the specified index
     */
    const Interval& at(size_t index) const {
        return Interval{starts[index], ends[index], data[index]};
    }

    void at(size_t index, Interval& itv) {
        itv.start = starts[index];
        itv.end = ends[index];
        itv.data = data[index];
    }

    inline void upperBound(const S value) noexcept {  // https://github.com/mh-dm/sb_lower_bound/blob/master/sbpm_lower_bound.h
        size_t length = starts.size();
        idx = 0;
        while (length > 1) {
            size_t half = length / 2;
            idx += (starts[idx + half] <= value) * (length - half);
            length = half;
        }
        if (idx > 0 && starts[idx] > value) {
            --idx;
        }
    }

    bool anyOverlaps(const S start, const S end) noexcept {
        if (starts.empty()) {
            return false;
        }
        upperBound(end);
        size_t i = idx;
        while (i > 0) {
            if (start <= ends[i]) {
                return true;
            } else {
                if (branch[i] >= i) {
                    break;
                }
                i = branch[i];
            }
        }
        if (i==0 && start <= ends[0] && starts[0] <= end) {
            return true;
        }
        return false;
    }

    void findOverlaps(const S start, const S end, std::vector<T>& found) {
        if (starts.empty()) {
            return;
        }
        upperBound(end);
        size_t i = idx;
        while (i > 0) {
            if (start <= ends[i]) {
                std::cout << " si " << start << " <= " << starts[i] << "-" << ends[i] << " " << i << std::endl;
                found.push_back(data[i]);
                --i;
            } else {
                if (branch[i] >= i) {
                    break;
                }
                i = branch[i];
            }
        }
        if (i==0 && start <= ends[0] && starts[0] <= end) {
            found.push_back(data[0]);
        }
    }

    void findStabbed(const S point, std::vector<T>& found) {
        if (starts.empty()) {
            return;
        }
        upperBound(point);
        size_t i = idx;
        while (i > 0) {
            if (point <= ends[i]) {
                found.push_back(data[i]);
                --i;
            } else {
                if (branch[i] >= i) {
                    break;
                }
                i = branch[i];
            }
        }
        if (i==0 && point <= ends[0] && starts[0] <= point) {
            found.push_back(data[0]);
        }
    }

private:

    std::vector<Interval> tmp;

    template<typename CompareFunc>
    void sortBlock(size_t start_i, size_t end_i, CompareFunc compare) {
        size_t range_size = end_i - start_i;
        tmp.resize(range_size);
        for (size_t i = 0; i < range_size; ++i) {
            tmp[i].start = starts[start_i + i];
            tmp[i].end = ends[start_i + i];
            tmp[i].data = data[start_i + i];
        }
        std::sort(tmp.begin(), tmp.end(), compare);
        for (size_t i = 0; i < range_size; ++i) {
            starts[start_i + i] = tmp[i].start;
            ends[start_i + i] = tmp[i].end;
            data[start_i + i] = tmp[i].data;
        }
    }

    void sortIntervals() {
        if (!startSorted) {
            sortBlock(0, starts.size(),
                      [](const Interval& a, const Interval& b) { return (a.start < b.start || (a.start == b.start && a.end > b.end)); });
            startSorted = true;
            endSorted = true;
        } else if (!endSorted) {  // only sort parts that need sorting - ends in descending order
            size_t it_start = 0;
            while (it_start < starts.size()) {
                size_t block_end = it_start + 1;
                // Find the end of this block (all with same start value)
                while (block_end < starts.size() && starts[block_end] == starts[it_start]) {
                    ++block_end;
                }
                // Check if this block needs sorting, scan entire block to ensure descending order of ends
                bool needs_sort = false;
                for (size_t i = it_start + 1; i < block_end && !needs_sort; ++i) {
                    if (ends[i] > ends[i - 1]) {
                        needs_sort = true;
                    }
                }
                if (needs_sort) {
                    sortBlock(it_start, block_end,
                              [](const Interval& a, const Interval& b) { return a.end > b.end; });
                }
                it_start = block_end;
            }
            endSorted = true;
        }
    }

};
