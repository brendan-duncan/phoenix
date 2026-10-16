// Native extension libraries: a C library built with the tests, put in an
// External Libraries folder, called from scripts through mm_jsapi.h.
#include "test_util.h"

#include "jsfl_test_host.h"

#include "../src/jsfl/native_extensions.h"

#include <filesystem>
#include <string>
#include <system_error>

using jsfl_test::Fixture;

namespace fs = std::filesystem;

namespace {

/// A Configuration folder of its own for each test: a library stays loaded
/// once loaded, so its folder cannot be emptied while the tests run.
std::string configFolder(const char* name)
{
    const fs::path folder = fs::temp_directory_path() / "phoenix_jsfl_native_tests" / name;
    std::error_code ignored;
    fs::remove_all(folder, ignored);
    fs::create_directories(folder / "External Libraries", ignored);
    return folder.u8string();
}

/// Copies the library at \a from into \a config's External Libraries as
/// \a name, keeping its extension.
void install(const std::string& config, const char* from, const std::string& name)
{
    const fs::path source = fs::u8path(from);
    const fs::path target = fs::u8path(config) / "External Libraries" / (name + source.extension().u8string());
    std::error_code error;
    fs::copy_file(source, target, fs::copy_options::overwrite_existing, error);
    CHECK(!error);
}

void expectOutput(Fixture& f, const std::string& wanted)
{
    const std::string got = f.take();
    CHECK(got == wanted);
    if (got != wanted)
        std::printf("    got:\n%s    wanted:\n%s", got.c_str(), wanted.c_str());
}

} // namespace

TEST(jsfl_native_extension_library_files)
{
#if defined(_WIN32)
    CHECK(jsfl::native::isLibraryFile("Sample.dll"));
    CHECK(jsfl::native::isLibraryFile("Sample.DLL"));
    CHECK(!jsfl::native::isLibraryFile("Sample.dylib"));
#elif defined(__APPLE__)
    CHECK(jsfl::native::isLibraryFile("Sample.dylib"));
    CHECK(jsfl::native::isLibraryFile("Sample.bundle"));
    CHECK(!jsfl::native::isLibraryFile("Sample.dll"));
#else
    CHECK(jsfl::native::isLibraryFile("libsample.so"));
    CHECK(!jsfl::native::isLibraryFile("Sample.dll"));
#endif
    CHECK(!jsfl::native::isLibraryFile("readme.txt"));
}

TEST(jsfl_native_extension_functions)
{
    jsfl::native::setEnabled(true);
    Fixture f;
    f.host.config = configFolder("functions");
    install(f.host.config, PHOENIX_TEST_EXTENSION, "testext");
    CHECK(f.run("fl.trace(testext.add(2, 3));\n"
                "fl.trace(testext.add(-7, 2));\n"
                "fl.trace(testext.add.length);\n"
                "fl.trace(testext.greet('Phoenix'));\n"
                "fl.trace(testext.greet('\\u65e5\\u672c \\ud83d\\ude00') == 'Hello, \\u65e5\\u672c \\ud83d\\ude00!');\n"
                "fl.trace(testext.greet(12));\n"
                "fl.trace(testext.sum([1, 2.5, 3]));\n"
                "fl.trace(testext.sum([1e20, -2000000000]) == 1e20 - 2000000000);\n"
                "fl.trace(testext.evaluate('1 + 2 * 3'));\n"
                "fl.trace(testext.evaluate('var fromNative = 5; fromNative * 2') + ' ' + fromNative);\n"
                "fl.trace(testext.range(4).join(','));\n"
                "var p = testext.pair('a', {x: 1});\n"
                "fl.trace(p.length + ' ' + p[0] + ' ' + p[1].x);\n"
                "fl.trace(testext.typeOf([]) + ' ' + testext.typeOf(new Date()) + ' ' + testext.typeOf({}));\n"
                "fl.trace(testext.shout('hello, world'));\n"
                "fl.trace(testext.negate(0) + ' ' + testext.negate('x'));\n"
                "fl.trace(typeof testext.nothing());\n"
                "fl.trace(testext.evaluate('null') === null);\n")
              .ok);
    expectOutput(f, "5\n-5\n2\nHello, Phoenix!\ntrue\nHello, 12!\n6.5\ntrue\n7\n10 5\n0,1,2,3\n"
                    "2 a 1\nArray Date Object\nHELLO, WORLD\ntrue false\nundefined\ntrue\n");

    // Errors the library reports, and values it makes up.
    CHECK(f.run("try { testext.fail(); } catch (e) { fl.trace(e.message); }\n"
                "try { testext.add('x'); } catch (e) { fl.trace(e.message); }\n"
                "try { testext.forged(); } catch (e) { fl.trace(e.name); }\n"
                "try { testext.evaluate('throw new RangeError(\"boom\")'); } catch (e) { fl.trace(e.name + ' ' + e.message); }\n")
              .ok);
    expectOutput(f, "testext.fail: it went wrong\ntestext.add failed.\nTypeError\nRangeError boom\n");

    // Loaded once, there for every run after.
    CHECK(f.run("fl.trace(testext.add(40, 2));").ok);
    expectOutput(f, "42\n");
}

TEST(jsfl_native_extension_without_entry_point_is_skipped)
{
    jsfl::native::setEnabled(true);
    Fixture f;
    f.host.config = configFolder("no_init");
    install(f.host.config, PHOENIX_TEST_NO_INIT, "broken");
    install(f.host.config, PHOENIX_TEST_EXTENSION, "working");
    CHECK(f.run("fl.trace(typeof broken + ' ' + working.add(1, 1));").ok);
    const std::string output = f.take();
    const std::string file = "broken" + fs::u8path(PHOENIX_TEST_NO_INIT).extension().u8string();
    CHECK(output.find("Native extension " + file + " was not loaded: it has no MM_InitWrapper") != std::string::npos);
    CHECK(output.find("undefined 2\n") != std::string::npos);

    // Reported once, not on every run.
    CHECK(f.run("fl.trace(typeof broken);").ok);
    expectOutput(f, "undefined\n");
}

TEST(jsfl_native_extensions_can_be_turned_off)
{
    Fixture f;
    f.host.config = configFolder("off");
    install(f.host.config, PHOENIX_TEST_EXTENSION, "testext");
    jsfl::native::setEnabled(false);
    CHECK(f.run("fl.trace(typeof testext);").ok);
    expectOutput(f, "undefined\n");
    jsfl::native::setEnabled(true);
    CHECK(f.run("fl.trace(testext.add(1, 2));").ok);
    expectOutput(f, "3\n");
}

TEST(jsfl_native_extension_leaves_script_globals_alone)
{
    jsfl::native::setEnabled(true);
    Fixture f;
    f.host.config = configFolder("globals");
    install(f.host.config, PHOENIX_TEST_EXTENSION, "FLfile");
    CHECK(f.run("fl.trace(typeof FLfile.exists + ' ' + typeof FLfile.add);").ok);
    const std::string output = f.take();
    CHECK(output.find("Native extension FLfile was left out") != std::string::npos);
    CHECK(output.find("function undefined\n") != std::string::npos);
}
