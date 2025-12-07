// Test that Buffer::Save writes actual contents to disk
#include <cassert>
#include <cstdio>
#include <fstream>
#include <string>

#include "Buffer.h"

static std::string
read_file(const std::string &path)
{
    std::ifstream in(path, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

int
main()
{
    // Create a temporary path under current working directory
    const std::string path = "./.kte_test_buffer_save.tmp";

    // Ensure any previous file is removed
    std::remove(path.c_str());

    Buffer buf;

    // Simulate editing a new buffer: insert content
    const std::string payload = "Hello, world!\nThis is a save test.\n";
    buf.insert_text(0, 0, payload);

    // Make it file-backed with a filename so Save() path is exercised
    // We use SaveAs first to set filename/is_file_backed and then modify content
    std::string err;
    bool ok = buf.SaveAs(path, err);
    assert(ok && err.empty());

    // Modify buffer after SaveAs to ensure Save() writes new content
    const std::string more = "Appended line.\n";
    buf.insert_text(2, 0, more);

    // Mark as dirty so commands would attempt to save; Save() itself doesn’t require dirty
    buf.SetDirty(true);

    // Now call Save() which should use streaming write and persist all bytes
    err.clear();
    ok = buf.Save(err);
    assert(ok && err.empty());

    // Verify file contents exactly match expected string
    const std::string expected = payload + more;
    const std::string got      = read_file(path);
    assert(got == expected);

    // Cleanup
    std::remove(path.c_str());
    return 0;
}
