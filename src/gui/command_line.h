#pragma once

#include <QStringList>

#include <memory>
#include <string>

namespace fla {
class FLADocument;
}

/// Phoenix's commands for render nodes and pipelines, run with no window:
///
///   Phoenix render <file> [--range 1-240] [--out frames/] [--size WxH] ...
///   Phoenix doctor <file> [--repair <out>] [--json]
///   Phoenix diff <before> <after> [--out <folder>] [--samples N]
///   Phoenix batch <folder> --doctor | --convert fla|phx|xfl | --render
///                 | --python <script.py> [--save] [--recursive] [--out <folder>]
///   Phoenix run-script <script.jsfl> [document ...] [--timeout SECONDS]
///   Phoenix help
namespace commands {

/// Whether \a name (the first argument) is one of the commands.
bool isCommand(const QString& name);

/// Runs the command in \a arguments (the program's, from argv[0]); the
/// exit code.
int run(const QStringList& arguments);

/// Reads \a path (.phx, .fla or an XFL folder); null with \a error when it
/// cannot.
std::unique_ptr<fla::FLADocument> load(const std::string& path, std::string& error);

/// Reads \a in and writes it to \a out in the format its extension names:
/// .phx, .fla, or anything else an XFL folder.
bool convert(const std::string& in, const std::string& out, std::string& error);

} // namespace commands
