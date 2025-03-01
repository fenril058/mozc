// Copyright 2010-2021, Google Inc.
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//     * Redistributions of source code must retain the above copyright
// notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above
// copyright notice, this list of conditions and the following disclaimer
// in the documentation and/or other materials provided with the
// distribution.
//     * Neither the name of Google Inc. nor the names of its
// contributors may be used to endorse or promote products derived from
// this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

// Single Kanji dictionary generator:
// % gen_symbol_ewriter_dictionary_main
//    --sorting_table=sorting_table_file
//    --ordering_rule=ordering_rule_file
//    --input=input.tsv
//    --user_pos_manager_data=user_pos_manager.data
//    --output_token_array=output_token_file
//    --output_string_array=output_array_file

#include <climits>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "base/file_stream.h"
#include "base/file_util.h"
#include "base/init_mozc.h"
#include "base/japanese_util.h"
#include "base/logging.h"
#include "base/status.h"
#include "base/util.h"
#include "data_manager/data_manager.h"
#include "data_manager/serialized_dictionary.h"
#include "rewriter/dictionary_generator.h"
#include "absl/container/btree_map.h"
#include "absl/container/btree_set.h"
#include "absl/flags/flag.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"

ABSL_FLAG(std::string, sorting_table, "", "sorting table file");
ABSL_FLAG(std::string, ordering_rule, "", "sorting order file");
ABSL_FLAG(std::string, input, "", "symbol dictionary file");
ABSL_FLAG(std::string, user_pos_manager_data, "", "user pos manager data file");
ABSL_FLAG(std::string, output_token_array, "",
          "output token array binary file");
ABSL_FLAG(std::string, output_string_array, "",
          "output string array binary file");

namespace mozc {
namespace {

void GetSortingMap(const std::string &auto_file, const std::string &rule_file,
                   absl::btree_map<std::string, uint16_t> *sorting_map) {
  CHECK(sorting_map);
  sorting_map->clear();
  std::string line;
  int sorting_key = 0;
  InputFileStream rule_ifs(rule_file);
  CHECK(rule_ifs.good());
  while (!std::getline(rule_ifs, line).fail()) {
    if (line.empty() ||
        // Support comment and single '#'
        (line[0] == '#' && line.size() > 1)) {
      continue;
    }
    sorting_map->insert(std::make_pair(line, sorting_key));
    ++sorting_key;
  }

  InputFileStream auto_ifs(auto_file);
  CHECK(auto_ifs.good());

  while (!std::getline(auto_ifs, line).fail()) {
    if (line.empty() || line[0] == '#') {
      continue;
    }
    const std::vector<std::string> fields =
        absl::StrSplit(line, absl::ByAnyChar("\t "), absl::SkipEmpty());
    CHECK_GE(fields.size(), 2);
    uint32_t ucs4 = 0;
    CHECK(absl::SimpleHexAtoi(fields[0], &ucs4));
    std::string utf8;
    Util::Ucs4ToUtf8(ucs4, &utf8);
    if (sorting_map->find(utf8) != sorting_map->end()) {
      // ordered by rule
      continue;
    }
    sorting_map->insert(std::make_pair(utf8, sorting_key));
    ++sorting_key;
  }
}

void AddSymbolToDictionary(
    const absl::string_view pos, const absl::string_view value,
    const std::vector<std::string> &keys, const absl::string_view description,
    const absl::string_view additional_description,
    const absl::btree_map<std::string, uint16_t> &sorting_map,
    rewriter::DictionaryGenerator *dictionary) {
  // use first char of value as sorting key.
  const auto first_value = std::string(Util::Utf8SubString(value, 0, 1));
  absl::btree_map<std::string, uint16_t>::const_iterator itr =
      sorting_map.find(first_value);
  uint16_t sorting_key = 0;
  if (itr == sorting_map.end()) {
    DLOG(WARNING) << first_value << " is not defined in sorting map.";
    // If the character is platform-dependent, put the character at the last.
    if (!Util::IsJisX0208(value)) {
      sorting_key = USHRT_MAX;
    }
  } else {
    sorting_key = itr->second;
  }

  for (size_t i = 0; i < keys.size(); ++i) {
    const std::string &key = keys[i];

    rewriter::Token token;
    token.set_sorting_key(sorting_key);
    token.set_key(key);
    token.set_value(value);
    token.set_pos(pos);
    token.set_description(description);
    token.set_additional_description(additional_description);
    dictionary->AddToken(token);

    std::string fw_key;
    japanese_util::HalfWidthAsciiToFullWidthAscii(key, &fw_key);
    if (fw_key != key) {
      token.set_key(fw_key);
      dictionary->AddToken(token);
    }
  }
}

// Read dic:
void MakeDictionary(const std::string &symbol_dictionary_file,
                    const std::string &sorting_map_file,
                    const std::string &ordering_rule_file,
                    rewriter::DictionaryGenerator *dictionary) {
  absl::btree_set<std::string> seen;
  absl::btree_map<std::string, uint16_t> sorting_map;
  GetSortingMap(sorting_map_file, ordering_rule_file, &sorting_map);

  InputFileStream ifs(symbol_dictionary_file);
  CHECK(ifs.good());

  std::string line;
  CHECK(!std::getline(ifs, line).fail());  // get first line

  while (!std::getline(ifs, line).fail()) {
    // Format:
    // POS <tab> value <tab> readings(space delimitered) <tab>
    // description <tab> memo
    std::vector<std::string> fields =
        absl::StrSplit(line, '\t', absl::AllowEmpty());
    if (fields.size() < 3 || (fields[1].empty() && fields[2].empty())) {
      VLOG(3) << "invalid format. skip line: " << line;
      continue;
    }
    std::string pos = fields[0];
    Util::UpperString(&pos);
    const std::string &value = fields[1];
    if (seen.find(value) != seen.end()) {
      LOG(WARNING) << "already inserted: " << value;
      continue;
    } else {
      seen.insert(value);
    }
    std::string keys_str;
    Util::StringReplace(fields[2], "　",  // Full-width space
                        " ", true, &keys_str);
    const std::vector<std::string> keys =
        absl::StrSplit(keys_str, ' ', absl::SkipEmpty());
    const std::string &description = (fields.size()) > 3 ? fields[3] : "";
    const std::string &additional_description =
        (fields.size()) > 4 ? fields[4] : "";
    AddSymbolToDictionary(pos, value, keys, description, additional_description,
                          sorting_map, dictionary);
  }
  // Add space as a symbol
  std::vector<std::string> keys_space;
  keys_space.push_back(" ");
  AddSymbolToDictionary("記号", " ", keys_space, "空白", "", sorting_map,
                        dictionary);
}
}  // namespace
}  // namespace mozc

int main(int argc, char **argv) {
  mozc::InitMozc(argv[0], &argc, &argv);

  if ((absl::GetFlag(FLAGS_input).empty() ||
       absl::GetFlag(FLAGS_sorting_table).empty() ||
       absl::GetFlag(FLAGS_ordering_rule).empty()) &&
      argc > 3) {
    absl::SetFlag(&FLAGS_input, argv[1]);
    absl::SetFlag(&FLAGS_sorting_table, argv[2]);
    absl::SetFlag(&FLAGS_ordering_rule, argv[3]);
  }

  const std::string tmp_text_file =
      absl::GetFlag(FLAGS_output_token_array) + ".txt";

  // User pos manager data for build tools has no magic number.
  const char *kMagciNumber = "";
  mozc::DataManager data_manager;
  const mozc::DataManager::Status status =
      data_manager.InitUserPosManagerDataFromFile(
          absl::GetFlag(FLAGS_user_pos_manager_data), kMagciNumber);
  CHECK_EQ(status, mozc::DataManager::Status::OK);

  mozc::rewriter::DictionaryGenerator dictionary(data_manager);
  mozc::MakeDictionary(absl::GetFlag(FLAGS_input),
                       absl::GetFlag(FLAGS_sorting_table),
                       absl::GetFlag(FLAGS_ordering_rule), &dictionary);
  dictionary.Output(tmp_text_file);
  mozc::SerializedDictionary::CompileToFiles(
      tmp_text_file, absl::GetFlag(FLAGS_output_token_array),
      absl::GetFlag(FLAGS_output_string_array));
  CHECK_OK(mozc::FileUtil::Unlink(tmp_text_file));

  return 0;
}
