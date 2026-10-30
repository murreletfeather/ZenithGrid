/** @file TestSelection.cpp
 * @brief 选择类型、载荷映射与值快照的回归测试。
 */
#include "Selection.h"
#include <catch2/catch_test_macros.hpp>

TEST_CASE("Selection rejects topology access to precise points and keeps independent snapshots")
{
    auto precise = Selection::create<ElementEnum::GeometryPoint>({ GeometryPoint { } });
    CHECK_THROWS_AS(precise.ids(), std::logic_error);
    CHECK_THROWS_AS(Selection({ 12 }, ElementEnum::GeometryPoint), std::invalid_argument);
    auto snapshot = precise;
    precise.get<ElementEnum::GeometryPoint>()->clear();
    CHECK(snapshot.size() == 1);
    snapshot.setType(ElementEnum::GeometryEdge);
    CHECK(snapshot.size() == 0);
    CHECK(snapshot.get<ElementEnum::GeometryEdge>());
    CHECK_FALSE(snapshot.get<ElementEnum::GeometryPoint>());
}
