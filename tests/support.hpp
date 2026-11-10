#pragma once
#include "minigit/repository.hpp"
#include <atomic>
#include <chrono>
#include <gtest/gtest.h>

namespace minigit {
class Sandbox : public ::testing::Test {
  protected:
    fs::path dir;
    void SetUp() override {
        static std::atomic<unsigned> serial{0};
        dir = fs::temp_directory_path() /
              ("minigit-test-" +
               std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "-" +
               std::to_string(serial++));
        fs::create_directory(dir);
    }
    void TearDown() override {
        std::error_code ec;
        fs::remove_all(dir, ec);
    }
    void put(const std::string &name, std::string_view data) { atomicWrite(dir / name, data); }
    std::string get(const std::string &name) { return readFile(dir / name); }
};
} // namespace minigit
