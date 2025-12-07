// Test saving after opening an existing file
#include <cassert>
#include <cstdio>
#include <fstream>
#include <string>

#include "Buffer.h"

static void write_file(const std::string &path, const std::string &data)
{
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(data.data(), static_cast<std::streamsize>(data.size()));
}

static std::string read_file(const std::string &path)
{
    std::ifstream in(path, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

int main()
{
    const std::string path = "./.kte_test_buffer_save_existing.tmp";
    std::remove(path.c_str());
    const std::string initial = "abc\n123\n";
    write_file(path, initial);

    Buffer buf;
    std::string err;
    bool ok = buf.OpenFromFile(path, err);
    assert(ok && err.empty());

    // Insert at end
    buf.insert_text(2, 0, std::string("tail\n"));
    buf.SetDirty(true);

    // Save should overwrite the same file
    err.clear();
    ok = buf.Save(err);
    assert(ok && err.empty());

    const std::string expected = initial + "tail\n";
    const std::string got = read_file(path);
    assert(got == expected);

    std::remove(path.c_str());
    return 0;
}
