// Checks the fl.* component library (resources/as3) with libas3, away from
// Phoenix: every file parses and compiles, every class loads through the
// class resolver, and each test case in cases/ runs without a failed check.
// The flash.* classes are stand-ins written in ActionScript (stubs/), so
// what is tested is the components' own logic, not drawing.
//
//   as3_components <library folder> <this folder> [script name filter]

#include "parser/parser.h"
#include "bytecode/compiler.h"
#include "bytecode/vm.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace as3;

namespace {

std::string readFile(const fs::path& path)
{
    std::ifstream in(path, std::ios::binary);
    std::stringstream text;
    text << in.rdbuf();
    return text.str();
}

std::vector<fs::path> scriptFiles(const fs::path& root)
{
    std::vector<fs::path> files;
    if (!fs::exists(root))
        return files;
    for (const auto& entry : fs::recursive_directory_iterator(root))
        if (entry.is_regular_file() && entry.path().extension() == ".as")
            files.push_back(entry.path());
    std::sort(files.begin(), files.end());
    return files;
}

// "fl/controls/Button.as" under root -> "fl.controls.Button".
std::string className(const fs::path& root, const fs::path& file)
{
    std::string name = fs::relative(file, root).replace_extension().generic_string();
    std::replace(name.begin(), name.end(), '/', '.');
    return name;
}

void setResolver(VM& vm, const std::vector<fs::path>& roots)
{
    vm.setClassResolver([roots](const std::string& name) -> std::optional<ClassSource> {
        std::string relative = name;
        std::replace(relative.begin(), relative.end(), '.', '/');
        for (const fs::path& root : roots)
        {
            fs::path file = root / (relative + ".as");
            if (fs::exists(file))
                return ClassSource{readFile(file), file.generic_string()};
        }
        return std::nullopt;
    });
}

int failures = 0;

// The program the components are first checked against.
const char* kBunnyPaint = "D:/Backup/hulabunny/bunnyPaint";

void defineHelpers(VM& vm, const std::string& script)
{
    vm.defineFunction("trace", [](VM& vm, const Val&, Args args) {
        std::string line;
        for (size_t i = 0; i < args.size(); ++i)
            line += (i ? " " : "") + vm.toString(args[i]);
        std::cout << "    " << line << "\n";
        return Val();
    });
    // check(condition, what): counts a failure when the condition is false.
    vm.defineFunction("check", [script](VM& vm, const Val&, Args args) {
        if (!args[0].toBoolean())
        {
            ++failures;
            std::cout << "  FAIL " << script << ": " << vm.toString(args[1]) << "\n";
        }
        return Val();
    });
}

} // namespace

int main(int argc, char** argv)
{
    if (argc < 3)
    {
        std::cerr << "as3_components <library folder> <test folder> [filter]\n";
        return 2;
    }
    const fs::path library = argv[1];
    const fs::path tests = argv[2];
    const std::string filter = argc > 3 ? argv[3] : "";

    // 1. Every file parses and compiles on its own.
    int compileErrors = 0;
    const std::vector<fs::path> files = scriptFiles(library);
    for (const fs::path& file : files)
    {
        Parser parser(readFile(file), file.filename().string());
        auto program = parser.parse();
        if (parser.hasErrors())
        {
            std::cout << parser.getErrors();
            ++compileErrors;
            continue;
        }
        if (!parser.getWarnings().empty())
        {
            std::cout << parser.getWarnings();
            ++compileErrors;
        }
        try
        {
            Compiler compiler;
            compiler.compile(program.get());
        }
        catch (const std::exception& failed)
        {
            std::cout << file.filename().string() << ": " << failed.what() << "\n";
            ++compileErrors;
        }
    }
    std::cout << files.size() << " files, " << compileErrors << " compile errors\n";

    // 2. Every class loads through the class resolver against the stubs.
    int loadErrors = 0;
    {
        VM vm;
        defineHelpers(vm, "load");
        setResolver(vm, {tests / "stubs", library});
        for (const fs::path& file : files)
        {
            const std::string name = className(library, file);
            try
            {
                Val found = vm.findDefinition(name);
                if (!found.isClass())
                {
                    std::cout << name << ": not defined\n";
                    ++loadErrors;
                }
            }
            catch (ScriptException& failed)
            {
                std::cout << name << ": " << vm.toString(failed.value) << "\n";
                ++loadErrors;
            }
        }
    }
    std::cout << loadErrors << " load errors\n";

    // 3. The test cases: each class in cases/ made in a VM of its own and
    // its run() called.
    int scriptErrors = 0;
    int run = 0;
    for (const fs::path& script : scriptFiles(tests / "cases"))
    {
        const std::string name = script.stem().string();
        if (!filter.empty() && name.find(filter) == std::string::npos)
            continue;
        ++run;
        std::cout << name << "\n";
        VM vm;
        defineHelpers(vm, name);
        std::vector<fs::path> roots = {tests, tests / "stubs", library};
        // A case about skins finds the stand-in skin classes too.
        if (name.find("Skin") != std::string::npos)
            roots.insert(roots.begin(), tests / "skins");
        // A case about bunnyPaint finds its sources (where they are), and,
        // running its classes, stand-ins for the canvas they need.
        if (name.find("BunnyPaint") != std::string::npos)
        {
            if (!fs::exists(fs::path(kBunnyPaint) / "com/hulabunny/bunnyPaint"))
            {
                std::cout << "  (bunnyPaint's sources not found; skipped)\n";
                continue;
            }
            roots.push_back(kBunnyPaint);
            roots.push_back(fs::path(kBunnyPaint) / "as3corelib-.93/src");
            if (name.find("Run") != std::string::npos)
                roots.insert(roots.begin(), tests / "bunnypaint");
        }
        setResolver(vm, roots);
        const int before = failures;
        try
        {
            Val test = vm.construct(vm.findDefinition("cases." + name), std::vector<Val>{});
            vm.callMethod(test, "run");
            if (failures == before)
                std::cout << "  ok\n";
        }
        catch (ScriptException& failed)
        {
            std::cout << "  ERROR " << vm.toString(failed.value) << " at line " << failed.line << "\n";
            try
            {
                if (failed.value.isObject())
                    std::cout << vm.toString(vm.callMethod(failed.value, "getStackTrace")) << "\n";
            }
            catch (ScriptException&)
            {
            }
            ++scriptErrors;
        }
    }
    std::cout << run << " cases, " << scriptErrors << " errors, " << failures << " failed checks\n";
    return (compileErrors || loadErrors || scriptErrors || failures) ? 1 : 0;
}
