// Test: Open a non-existent path (buffer becomes unnamed but with filename),
// insert text, then Save via SaveAs to the same path. Verify bytes on disk.
#include <cassert>
#include <cstdio>
#include <fstream>
#include <string>

#include "Buffer.h"

static std::string read_file(const std::string &path)
{
    std::ifstream in(path, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

int main()
{
    const std::string path = "./.kte_test_open_nonexistent_save.tmp";
    std::remove(path.c_str());

    // Sanity: path should not exist
    {
        std::ifstream probe(path);
        assert(!probe.good());
    }

    Buffer buf;
    std::string err;
    bool ok = buf.OpenFromFile(path, err);
    assert(ok && err.empty());
    assert(!buf.IsFileBacked());
    assert(buf.Filename().size() > 0);

    // Insert text like a user would type then press Return
    buf.insert_text(0, 0, std::string("hello, world"));
    // Simulate pressing Return (newline at end)
    buf.insert_text(0, 12, std::string("\n"));
    buf.SetDirty(true);

    // Save using SaveAs to the same filename the buffer carries (what cmd_save would do)
    ok = buf.SaveAs(buf.Filename(), err);
    assert(ok && err.empty());

    const std::string got = read_file(buf.Filename());
    const std::string expected = std::string("hello, world\n");
    assert(got == expected);

    std::remove(path.c_str());
    return 0;
}
