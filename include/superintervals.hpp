
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
            , it_low(0)
            , it_high(0)
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

    class Iterator {
    public:
        size_t it_index;
        Iterator(const SuperIntervals* list, size_t index) : super(list) {
            _start = list->it_low;
            _end = list->it_high;
            it_index = index;
        }
        typename SuperIntervals::Interval operator*() const {
            return typename SuperIntervals<S, T>::Interval{super->starts[it_index], super->ends[it_index], super->data[it_index]};
        }
        Iterator& operator++() {
            if (it_index == 0) {
                it_index = SIZE_MAX;
                return *this;
            }
            if (it_index > 0) {
                if (_start <= super->ends[it_index]) {
                    --it_index;
                } else {
                    if (super->branch[it_index] >= it_index) {
                        it_index = SIZE_MAX;
                        return *this;
                    }
                    it_index = super->branch[it_index];
                    if (_start <= super->ends[it_index]) {
                        --it_index;
                    } else {
                        it_index = SIZE_MAX;
                        return *this;
                    }
                }
            }
            return *this;
        }
        bool operator!=(const Iterator& other) const {
            return it_index != other.it_index;
        }
        bool operator==(const Iterator& other) const {
            return it_index == other.it_index;
        }
        Iterator begin() const { return Iterator(super, super->idx); }
        Iterator end() const { return Iterator(super, SIZE_MAX); }
    private:
        S _start, _end;
        const SuperIntervals<S, T>* super;

    };

    Iterator begin() const { return Iterator(this, idx); }
    Iterator end() const { return Iterator(this, SIZE_MAX); }

    /**
     * @brief Sets the search interval. Must be called before using the iterator.
     * @param start Start point of the search range
     * @param end End point of the search range
     */
    void searchInterval(const S start, const S end) noexcept {
        if (starts.empty()) {
            return;
        }
        it_low = start; it_high = end;
        upperBound(end);
        if (start > ends[idx] || starts[0] > end) {
            idx = SIZE_MAX;
        }
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

    static constexpr auto findValues = &SuperIntervals::findOverlaps;

    void findIndexes(const S start, const S end, std::vector<size_t>& found) {
        if (starts.empty()) {
            return;
        }
        upperBound(end);
        size_t i = idx;
        while (i > 0) {
            if (start <= ends[i]) {
                found.push_back(i);
                --i;
            } else {
                if (branch[i] >= i) {
                    break;
                }
                i = branch[i];
            }
        }
        if (i==0 && start <= ends[0] && starts[0] <= end) {
            found.push_back(0);
        }
    }

    void findKeys(const S start, const S end, std::vector<std::pair<S, S>>& found) {
        if (starts.empty()) {
            return;
        }
        upperBound(end);
        size_t i = idx;
        while (i > 0) {
            if (start <= ends[i]) {
                found.push_back({starts[i], ends[i]});
                --i;
            } else {
                if (branch[i] >= i) {
                    break;
                }
                i = branch[i];
            }
        }
        if (i==0 && start <= ends[0] && starts[0] <= end) {
            found.push_back({starts[0], ends[0]});
        }
    }

    void findItems(const S start, const S end, std::vector<Interval>& found) {
        if (starts.empty()) {
            return;
        }
        upperBound(end);
        size_t i = idx;
        while (i > 0) {
            if (start <= ends[i]) {
                found.emplace_back() = {starts[i], ends[i], data[i]};
                --i;
            } else {
                if (branch[i] >= i) {
                    break;
                }
                i = branch[i];
            }
        }
        if (i==0 && start <= ends[0] && starts[0] <= end) {
            found.emplace_back() = {starts[0], ends[0], data[0]};
        }
    }

    void coverage(const S start, const S end, std::pair<size_t, S> &cov_result) {
        if (starts.empty()) {
            cov_result.first = 0;
            cov_result.second = 0;
            return;
        }
        upperBound(end);
        size_t i = idx;
        size_t cnt = 0;
        S cov = 0;
        while (i > 0) {
            if (start <= ends[i]) {
                ++cnt;
                cov += std::min(ends[i], end) - std::max(starts[i], start);
                --i;
            } else {
                if (branch[i] >= i) {
                    break;
                }
                i = branch[i];
            }
        }
        if (i==0 && start <= ends[0] && starts[0] <= end) {
            cov += std::min(ends[i], end) - std::max(starts[i], start);
            ++cnt;
        }
        cov_result.first = cnt;
        cov_result.second = cov;
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

protected:

    S it_low, it_high;
    std::vector<Interval> tmp;

    template<typename CompareFunc>
    void sortBlock(size_t start_i, size_t end_i, CompareFunc compare) {
        size_t range_size = end_i - start_i;
        tmp.resize(range_size);
        for (size_t i = 0; i < range_size; ++i) {
            tmp[i] = Interval(starts[start_i + i], ends[start_i + i], data[start_i + i]);
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
                bool needs_sort = false;
                while (block_end < starts.size() && starts[block_end] == starts[it_start]) {
                    if (block_end > it_start && ends[block_end] > ends[block_end - 1]) {
                        needs_sort = true;
                    }
                    ++block_end;
                }
                if (needs_sort) {
                    sortBlock(it_start, block_end, [](const Interval& a, const Interval& b) { return a.end > b.end; });
                }
                it_start = block_end;
            }
            endSorted = true;
        }
    }
};
