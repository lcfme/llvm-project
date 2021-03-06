//===- DirectiveEmitter.cpp - Directive Language Emitter ------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// DirectiveEmitter uses the descriptions of directives and clauses to construct
// common code declarations to be used in Frontends.
//
//===----------------------------------------------------------------------===//

#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/TableGen/Error.h"
#include "llvm/TableGen/Record.h"
#include "llvm/TableGen/TableGenBackend.h"

using namespace llvm;

namespace llvm {

// Generate enum class
void GenerateEnumClass(const std::vector<Record *> &Records, raw_ostream &OS,
                       StringRef Enum, StringRef Prefix, StringRef CppNamespace,
                       bool MakeEnumAvailableInNamespace) {
  OS << "\n";
  OS << "enum class " << Enum << " {\n";
  for (const auto &R : Records) {
    const auto Name = R->getValueAsString("name");
    std::string N = Name.str();
    std::replace(N.begin(), N.end(), ' ', '_');
    OS << "  " << Prefix << N << ",\n";
  }
  OS << "};\n";
  OS << "\n";
  OS << "static constexpr std::size_t " << Enum
     << "_enumSize = " << Records.size() << ";\n";

  // Make the enum values available in the defined namespace. This allows us to
  // write something like Enum_X if we have a `using namespace <CppNamespace>`.
  // At the same time we do not loose the strong type guarantees of the enum
  // class, that is we cannot pass an unsigned as Directive without an explicit
  // cast.
  if (MakeEnumAvailableInNamespace) {
    OS << "\n";
    for (const auto &R : Records) {
      const auto Name = R->getValueAsString("name");
      std::string N = Name.str();
      std::replace(N.begin(), N.end(), ' ', '_');
      OS << "constexpr auto " << Prefix << N << " = "
         << "llvm::" << CppNamespace << "::" << Enum << "::" << Prefix << N
         << ";\n";
    }
  }
}

// Generate the declaration section for the enumeration in the directive
// language
void EmitDirectivesDecl(RecordKeeper &Records, raw_ostream &OS) {

  const auto &DirectiveLanguages =
      Records.getAllDerivedDefinitions("DirectiveLanguage");

  if (DirectiveLanguages.size() != 1) {
    PrintError("A single definition of DirectiveLanguage is needed.");
    return;
  }

  const auto &DirectiveLanguage = DirectiveLanguages[0];
  StringRef LanguageName = DirectiveLanguage->getValueAsString("name");
  StringRef DirectivePrefix =
      DirectiveLanguage->getValueAsString("directivePrefix");
  StringRef ClausePrefix = DirectiveLanguage->getValueAsString("clausePrefix");
  StringRef CppNamespace = DirectiveLanguage->getValueAsString("cppNamespace");
  bool MakeEnumAvailableInNamespace =
      DirectiveLanguage->getValueAsBit("makeEnumAvailableInNamespace");
  bool EnableBitmaskEnumInNamespace =
      DirectiveLanguage->getValueAsBit("enableBitmaskEnumInNamespace");

  OS << "#ifndef LLVM_" << LanguageName << "_INC\n";
  OS << "#define LLVM_" << LanguageName << "_INC\n";

  if (EnableBitmaskEnumInNamespace)
    OS << "\n#include \"llvm/ADT/BitmaskEnum.h\"\n";

  OS << "\n";
  OS << "namespace llvm {\n";
  OS << "class StringRef;\n";

  // Open namespaces defined in the directive language
  llvm::SmallVector<StringRef, 2> Namespaces;
  llvm::SplitString(CppNamespace, Namespaces, "::");
  for (auto Ns : Namespaces)
    OS << "namespace " << Ns << " {\n";

  if (EnableBitmaskEnumInNamespace)
    OS << "\nLLVM_ENABLE_BITMASK_ENUMS_IN_NAMESPACE();\n";

  // Emit Directive enumeration
  const auto &Directives = Records.getAllDerivedDefinitions("Directive");
  GenerateEnumClass(Directives, OS, "Directive", DirectivePrefix, CppNamespace,
                    MakeEnumAvailableInNamespace);

  // Emit Clause enumeration
  const auto &Clauses = Records.getAllDerivedDefinitions("Clause");
  GenerateEnumClass(Clauses, OS, "Clause", ClausePrefix, CppNamespace,
                    MakeEnumAvailableInNamespace);

  // Generic function signatures
  OS << "\n";
  OS << "// Enumeration helper functions\n";
  OS << "Directive get" << LanguageName
     << "DirectiveKind(llvm::StringRef Str);\n";
  OS << "\n";
  OS << "llvm::StringRef get" << LanguageName
     << "DirectiveName(Directive D);\n";
  OS << "\n";
  OS << "Clause get" << LanguageName << "ClauseKind(llvm::StringRef Str);\n";
  OS << "\n";
  OS << "llvm::StringRef get" << LanguageName << "ClauseName(Clause C);\n";
  OS << "\n";

  // Closing namespaces
  for (auto Ns : llvm::reverse(Namespaces))
    OS << "} // namespace " << Ns << "\n";

  OS << "} // namespace llvm\n";

  OS << "#endif // LLVM_" << LanguageName << "_INC\n";
}

// Generate function implementation for get<Enum>Name(StringRef Str)
void GenerateGetName(const std::vector<Record *> &Records, raw_ostream &OS,
                     StringRef Enum, StringRef Prefix, StringRef LanguageName,
                     StringRef Namespace) {
  OS << "\n";
  OS << "llvm::StringRef llvm::" << Namespace << "::get" << LanguageName << Enum
     << "Name(" << Enum << " Kind) {\n";
  OS << "  switch (Kind) {\n";
  for (const auto &R : Records) {
    const auto Name = R->getValueAsString("name");
    const auto AlternativeName = R->getValueAsString("alternativeName");
    std::string N = Name.str();
    std::replace(N.begin(), N.end(), ' ', '_');
    OS << "    case " << Prefix << N << ":\n";
    OS << "      return \"";
    if (AlternativeName.empty())
      OS << Name;
    else
      OS << AlternativeName;
    OS << "\";\n";
  }
  OS << "  }\n"; // switch
  OS << "  llvm_unreachable(\"Invalid " << LanguageName << " " << Enum
     << " kind\");\n";
  OS << "}\n";
}

// Generate function implementation for get<Enum>Kind(StringRef Str)
void GenerateGetKind(const std::vector<Record *> &Records, raw_ostream &OS,
                     StringRef Enum, StringRef Prefix, StringRef LanguageName,
                     StringRef Namespace, bool ImplicitAsUnknown) {

  auto DefaultIt = std::find_if(Records.begin(), Records.end(), [](Record *R) {
    return R->getValueAsBit("isDefault") == true;
  });

  if (DefaultIt == Records.end()) {
    PrintError("A least one " + Enum + " must be defined as default.");
    return;
  }

  const auto DefaultName = (*DefaultIt)->getValueAsString("name");
  std::string DefaultEnum = DefaultName.str();
  std::replace(DefaultEnum.begin(), DefaultEnum.end(), ' ', '_');

  OS << "\n";
  OS << Enum << " llvm::" << Namespace << "::get" << LanguageName << Enum
     << "Kind(llvm::StringRef Str) {\n";
  OS << "  return llvm::StringSwitch<" << Enum << ">(Str)\n";

  for (const auto &R : Records) {
    const auto Name = R->getValueAsString("name");
    std::string N = Name.str();
    std::replace(N.begin(), N.end(), ' ', '_');
    if (ImplicitAsUnknown && R->getValueAsBit("isImplicit")) {
      OS << "    .Case(\"" << Name << "\"," << Prefix << DefaultEnum << ")\n";
    } else {
      OS << "    .Case(\"" << Name << "\"," << Prefix << N << ")\n";
    }
  }
  OS << "    .Default(" << Prefix << DefaultEnum << ");\n";
  OS << "}\n";
}

// Generate the implemenation section for the enumeration in the directive
// language
void EmitDirectivesImpl(RecordKeeper &Records, raw_ostream &OS) {

  const auto &DirectiveLanguages =
      Records.getAllDerivedDefinitions("DirectiveLanguage");

  if (DirectiveLanguages.size() != 1) {
    PrintError("A single definition of DirectiveLanguage is needed.");
    return;
  }

  const auto &DirectiveLanguage = DirectiveLanguages[0];
  StringRef DirectivePrefix =
      DirectiveLanguage->getValueAsString("directivePrefix");
  StringRef LanguageName = DirectiveLanguage->getValueAsString("name");
  StringRef ClausePrefix = DirectiveLanguage->getValueAsString("clausePrefix");
  StringRef CppNamespace = DirectiveLanguage->getValueAsString("cppNamespace");

  const auto &Directives = Records.getAllDerivedDefinitions("Directive");
  const auto &Clauses = Records.getAllDerivedDefinitions("Clause");

  // getDirectiveKind(StringRef Str)
  GenerateGetKind(Directives, OS, "Directive", DirectivePrefix, LanguageName,
                  CppNamespace, /*ImplicitAsUnknown=*/false);

  // getDirectiveName(Directive Kind)
  GenerateGetName(Directives, OS, "Directive", DirectivePrefix, LanguageName,
                  CppNamespace);

  // getClauseKind(StringRef Str)
  GenerateGetKind(Clauses, OS, "Clause", ClausePrefix, LanguageName,
                  CppNamespace, /*ImplicitAsUnknown=*/true);

  // getClauseName(Clause Kind)
  GenerateGetName(Clauses, OS, "Clause", ClausePrefix, LanguageName,
                  CppNamespace);
}

} // namespace llvm
