#include "netlist/ExternalManager.hpp"

#include "Test.hpp"

TEST_CASE("Allocate and get", "[ExternalManager]") {
  ExternalManager<std::vector<int>> manager;

  auto handle1 = manager.allocate(3, 42); // vector of size 3, filled with 42
  auto handle2 = manager.allocate(2, 7);  // vector of size 2, filled with 7

  const auto &vec1 = manager.get(handle1);
  const auto &vec2 = manager.get(handle2);

  CHECK(vec1.size() == 3);
  CHECK(vec1[0] == 42);
  CHECK(vec1[1] == 42);
  CHECK(vec1[2] == 42);

  CHECK(vec2.size() == 2);
  CHECK(vec2[0] == 7);
  CHECK(vec2[1] == 7);
}

TEST_CASE("Erase and valid", "[ExternalManager]") {
  ExternalManager<std::vector<int>> manager;

  auto handle1 = manager.allocate(3, 42);
  auto handle2 = manager.allocate(2, 7);

  CHECK(manager.valid(handle1));
  CHECK(manager.valid(handle2));

  manager.erase(handle1);

  CHECK(!manager.valid(handle1));
  CHECK(manager.valid(handle2));

  // Attempting to get a freed handle should assert (in debug mode)
#ifndef NDEBUG
  CHECK_THROWS_AS(manager.get(handle1), std::exception);
#endif
}

TEST_CASE("Reuse freed handle", "[ExternalManager]") {
  ExternalManager<std::vector<int>> manager;

  auto handle1 = manager.allocate(3, 42);
  manager.erase(handle1);

  auto handle2 = manager.allocate(2, 7);

  // In this simple implementation, the freed handle should be reused.
  CHECK(handle1 == handle2);

  const auto &vec2 = manager.get(handle2);
  CHECK(vec2.size() == 2);
  CHECK(vec2[0] == 7);
  CHECK(vec2[1] == 7);
}

TEST_CASE("Clone", "[ExternalManager]") {
  ExternalManager<std::vector<int>> manager;

  auto handle1 = manager.allocate(3, 42);
  auto handle2 = manager.allocate(2, 7);

  auto clone = manager.clone();

  CHECK(clone.valid(handle1));
  CHECK(clone.valid(handle2));

  const auto &vec1 = clone.get(handle1);
  const auto &vec2 = clone.get(handle2);

  CHECK(vec1.size() == 3);
  CHECK(vec1[0] == 42);
  CHECK(vec1[1] == 42);
  CHECK(vec1[2] == 42);

  CHECK(vec2.size() == 2);
  CHECK(vec2[0] == 7);
  CHECK(vec2[1] == 7);
}

TEST_CASE("Deep copy assignment", "[ExternalManager]") {
  ExternalManager<std::vector<int>> manager1;

  auto handle1 = manager1.allocate(3, 42);
  auto handle2 = manager1.allocate(2, 7);

  ExternalManager<std::vector<int>> manager2;
  manager2 = manager1; // Deep copy assignment

  CHECK(manager2.valid(handle1));
  CHECK(manager2.valid(handle2));

  const auto &vec1 = manager2.get(handle1);
  const auto &vec2 = manager2.get(handle2);

  CHECK(vec1.size() == 3);
  CHECK(vec1[0] == 42);
  CHECK(vec1[1] == 42);
  CHECK(vec1[2] == 42);

  CHECK(vec2.size() == 2);
  CHECK(vec2[0] == 7);
  CHECK(vec2[1] == 7);
}
