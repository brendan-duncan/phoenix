#include "test_util.h"

#include "../src/util/exr_writer.h"

#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>

TEST(the_exr_writer_writes_halves_and_a_valid_header)
{
    CHECK(exr::toHalf(0.0f) == 0x0000);
    CHECK(exr::toHalf(1.0f) == 0x3c00);
    CHECK(exr::toHalf(-2.0f) == 0xc000);
    CHECK(exr::toHalf(0.5f) == 0x3800);
    CHECK(exr::toHalf(65504.0f) == 0x7bff);
    CHECK(exr::toHalf(1.0e6f) == 0x7c00);

    std::vector<exr::Channel> channels;
    channels.push_back({"R", {1.0f, 0.0f, 0.5f, 0.25f}});
    channels.push_back({"A", {1.0f, 1.0f, 1.0f, 1.0f}});
    channels.push_back({"matte.Y", {0.0f, 1.0f, 0.0f, 1.0f}});
    const std::string path = "exr_writer_test.exr";
    std::string error;
    CHECK(exr::write(path, 2, 2, channels, error));
    std::ifstream in(path, std::ios::binary);
    const std::string bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    in.close();
    std::remove(path.c_str());
    CHECK(bytes.size() > 100);
    CHECK(static_cast<unsigned char>(bytes[0]) == 0x76 && bytes[1] == 0x2f && bytes[2] == 0x31 && bytes[3] == 0x01);
    // Channels listed in name order: A, R, matte.Y.
    const size_t a = bytes.find(std::string("A\0", 2));
    const size_t r = bytes.find(std::string("R\0", 2));
    const size_t m = bytes.find("matte.Y");
    CHECK(a != std::string::npos && r != std::string::npos && m != std::string::npos && a < r && r < m);
    CHECK(!exr::write(path, 3, 2, channels, error));
}
