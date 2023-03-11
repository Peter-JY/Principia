#include "tools/generate_profiles.hpp"

#include <filesystem>
#include <fstream>

#include "glog/logging.h"
#include "google/protobuf/descriptor.h"
#include "serialization/journal.pb.h"
#include "tools/journal_proto_processor.hpp"

namespace principia {
namespace tools {
namespace _generate_profiles {
namespace internal {

namespace {
const char warning[] = "// Warning!  This file was generated by running a "
                       "program (see project |tools|).\n"
                       "// If you change it, the changes will be lost the next "
                       "time the generator is\n"
                       "// run.  You should change the generator instead.\n\n";
}  // namespace

void GenerateProfiles() {
  JournalProtoProcessor processor;
  processor.ProcessMessages();

  // Now write the output.
  std::filesystem::path const journal = SOLUTION_DIR / "journal";
  std::filesystem::path const ksp_plugin = SOLUTION_DIR / "ksp_plugin";
  std::filesystem::path const ksp_plugin_adapter =
      SOLUTION_DIR / "ksp_plugin_adapter";

  std::ofstream profiles_generated_h(journal / "profiles.generated.h");
  CHECK(profiles_generated_h.good());
  profiles_generated_h << warning;
  for (auto const& cxx_method_type : processor.GetCxxMethodTypes()) {
    profiles_generated_h << cxx_method_type;
  }

  std::ofstream profiles_generated_cc(journal / "profiles.generated.cc");
  CHECK(profiles_generated_cc.good());
  profiles_generated_cc << warning;
  for (auto const& cxx_interchange_implementation :
           processor.GetCxxInterchangeImplementations()) {
    profiles_generated_cc << cxx_interchange_implementation;
  }
  for (auto const& cxx_method_implementation :
           processor.GetCxxMethodImplementations()) {
    profiles_generated_cc << cxx_method_implementation;
  }

  std::ofstream player_generated_cc(journal / "player.generated.cc");
  CHECK(player_generated_cc.good());
  player_generated_cc << warning;
  for (auto const& cxx_play_statement :
           processor.GetCxxPlayStatements()) {
    player_generated_cc << cxx_play_statement;
  }

  std::ofstream interface_generated_h(ksp_plugin / "interface.generated.h");
  CHECK(interface_generated_h.good());
  interface_generated_h << warning;
  for (auto const& cxx_interface_type_declaration :
           processor.GetCxxInterchangeTypeDeclarations()) {
    interface_generated_h << cxx_interface_type_declaration;
  }
  for (auto const& cxx_interface_method_declaration :
           processor.GetCxxInterfaceMethodDeclarations()) {
    interface_generated_h << cxx_interface_method_declaration;
  }

  std::ofstream interface_generated_cs(ksp_plugin_adapter /
                                      "interface.generated.cs");
  CHECK(interface_generated_cs.good());
  interface_generated_cs << warning;
  interface_generated_cs << "using System;\n";
  interface_generated_cs << "using System.Runtime.InteropServices;\n\n";
  interface_generated_cs << "namespace principia {\n";
  interface_generated_cs << "namespace ksp_plugin_adapter {\n\n";
  for (auto const& cs_interface_type_declaration :
           processor.GetCsInterchangeTypeDeclarations()) {
    interface_generated_cs << cs_interface_type_declaration;
  }
  interface_generated_cs << "internal static partial class Interface {\n\n";
  for (auto const& cs_interface_method_declaration :
           processor.GetCsInterfaceMethodDeclarations()) {
    interface_generated_cs << cs_interface_method_declaration;
  }
  interface_generated_cs << "}\n\n";
  interface_generated_cs << "}  // namespace ksp_plugin_adapter\n";
  interface_generated_cs << "}  // namespace principia\n";

  std::ofstream marshalers_generated_cs(ksp_plugin_adapter /
                                      "marshalers.generated.cs");
  CHECK(marshalers_generated_cs.good());
  marshalers_generated_cs << warning;
  marshalers_generated_cs << "using System;\n";
  marshalers_generated_cs << "using System.Runtime.InteropServices;\n\n";
  marshalers_generated_cs << "namespace principia {\n";
  marshalers_generated_cs << "namespace ksp_plugin_adapter {\n\n";
  for (auto const& cs_custom_marshaler_class :
       processor.GetCsCustomMarshalerClasses()) {
    marshalers_generated_cs << cs_custom_marshaler_class;
  }
  marshalers_generated_cs << "}  // namespace ksp_plugin_adapter\n";
  marshalers_generated_cs << "}  // namespace principia\n";
}

}  // namespace internal
}  // namespace _generate_profiles
}  // namespace tools
}  // namespace principia
