# -*- coding: utf-8 -*-
# Copyright 2010-2020, Google Inc.
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are
# met:
#
#     * Redistributions of source code must retain the above copyright
# notice, this list of conditions and the following disclaimer.
#     * Redistributions in binary form must reproduce the above
# copyright notice, this list of conditions and the following disclaimer
# in the documentation and/or other materials provided with the
# distribution.
#     * Neither the name of Google Inc. nor the names of its
# contributors may be used to endorse or promote products derived from
# this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
# "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
# LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
# A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
# OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
# SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
# LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
# DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
# THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

"""A tool to generate POS matcher.

This script generates POS matcher data and the C++ class that provides functions
for POS ID matching.

* C++ class: POSMatcher

This class has two methods for each POS matching rule:
- GetXXXId(): returns the POS ID for XXX.
- IsXXX(uint16 id): checks if the given POS ID is XXX or not.
Here, XXX is replaced by rule names; see data/rules/pos_matcher_rule.def.

POSMathcer is created from the data generated by this script.
The binary format is as follows.

* Binary format

Support there are N matching rules.  Then, the first 2*N bytes is the array of
uint16 that contains the results for GetXXXId() methods.  The latter part
contains the ranges of POS IDs for each IsXXX(uint16 id) methods (IsXXX should
return true if id is in one of the ranges).  See the following figure:

+===========================================+=============================
| POS ID for rule 0 (2 bytes)               |   For GetXXXID() methods
+-------------------------------------------+
| POS ID for rule 1 (2 bytes)               |
+-------------------------------------------+
| ....                                      |
+-------------------------------------------+
| POS ID for rule N - 1 (2 bytes)           |
+===========================================+=============================
| POS range for rule 0: start 0 (2 bytes)   |   For IsXXX() for rule 0
+ - - - - - - - - - - - - - - - - - - - - - +
| POS range for rule 0: end 0 (2 bytes)     |
+-------------------------------------------+
| POS range for rule 0: start 1 (2 bytes)   |
+ - - - - - - - - - - - - - - - - - - - - - +
| POS range for rule 0: end 1 (2 bytes)     |
|-------------------------------------------+
| ....                                      |
|-------------------------------------------+
| POS range for rule 0: start M (2 bytes)   |
+ - - - - - - - - - - - - - - - - - - - - - +
| POS range for rule 0: end M (2 bytes)     |
|-------------------------------------------+
| Sentinel element 0xFFFF (2 bytes)         |
+===========================================+=============================
| POS range for rule 1: start 0 (2 bytes)   |   For IsXXX() for rule 1
+ - - - - - - - - - - - - - - - - - - - - - +
| POS range for rule 1: end 0 (2 bytes)     |
+-------------------------------------------+
| ....                                      |
+-------------------------------------------+
| Sentinel element 0xFFFF (2 bytes)         |
+===========================================+
| ....                                      |
|                                           |
"""

from __future__ import absolute_import

import codecs
import optparse
import struct

from dictionary import pos_util


def OutputPosMatcherData(pos_matcher, output):
  data = []
  for rule_name in pos_matcher.GetRuleNameList():
    data.append(pos_matcher.GetId(rule_name))

  offset = 2 * len(pos_matcher.GetRuleNameList())
  for rule_name in pos_matcher.GetRuleNameList():
    data.append(offset)
    offset += 2 * len(pos_matcher.GetRange(rule_name)) + 1

  for rule_name in pos_matcher.GetRuleNameList():
    for id_range in pos_matcher.GetRange(rule_name):
      data.append(id_range[0])
      data.append(id_range[1])
    data.append(0xFFFF)

  for u16 in data:
    output.write(struct.pack('<H', u16))


def OutputPosMatcherHeader(pos_matcher, output):
  """Generates the definition of POSMatcher class.

  POSMatcher is independent of the actual input data but just provides logic for
  POS matching. To use a generated class, it's required to pass the data
  generated by OutputPosMatcherData() above.
  """

  lid_table_size = len(pos_matcher.GetRuleNameList())

  output.write(
      '#ifndef MOZC_DICTIONARY_POS_MATCHER_H_\n'
      '#define MOZC_DICTIONARY_POS_MATCHER_H_\n'
      '#include "./base/port.h"\n'
      'namespace mozc {\n'
      'namespace dictionary {\n'
      'class POSMatcher {\n'
      ' public:\n')

  # Helper function to generate Get<RuleName>Id() method from rule name and its
  # corresponding index.
  def _GenerateGetMethod(rule_name, index):
    return ('  inline uint16 Get%(rule_name)sId() const {\n'
            '    return data_[%(index)d];\n'
            '  }' % {
                'rule_name': rule_name,
                'index': index,
            })

  # Helper function to generate Is<RuleName>(uint16 id) method from rule name
  # and its corresponding index. The generated function checks if the given id
  # belongs to some range in kRangeTable[index] = kRangeTable_RuleName[].
  def _GenerateIsMethod(rule_name, index):
    return ('  inline bool Is%(rule_name)s(uint16 id) const {\n'
            '    const uint16 offset = data_[%(lid_table_size)d + %(index)d];\n'
            '    for (const uint16 *ptr = data_ + offset;\n'
            '         *ptr != static_cast<uint16>(0xFFFF); ptr += 2) {\n'
            '      if (id >= *ptr && id <= *(ptr + 1)) {\n'
            '        return true;\n'
            '      }\n'
            '    }\n'
            '    return false;\n'
            '  }' % {
                'rule_name': rule_name,
                'index': index,
                'lid_table_size': lid_table_size,
            })

  # Generate Get<RuleName>Id() and Is<RuleName>(uint16 id) for each rule.
  for i, rule_name in enumerate(pos_matcher.GetRuleNameList()):
    output.write(
        '  // %(rule_name)s "%(original_pattern)s"\n'
        '%(get_method)s\n'
        '%(is_method)s\n' % {
            'rule_name': rule_name,
            'original_pattern': pos_matcher.GetOriginalPattern(rule_name),
            'get_method': _GenerateGetMethod(rule_name, i),
            'is_method': _GenerateIsMethod(rule_name, i)})

  # Constructor takes two pointers to arrays generated by OutputPosMatcherData()
  # function.
  output.write(
      ' public:\n'
      '  POSMatcher() : data_(nullptr) {}\n'
      '  explicit POSMatcher(const uint16 *data) : data_(data) {}\n'
      '  void Set(const uint16 *data) { data_ = data; }\n'
      ' private:\n'
      '  const uint16 *data_;\n'
      '};\n'
      '}  // namespace dictionary\n'
      '}  // namespace mozc\n'
      '#endif  // MOZC_DICTIONARY_POS_MATCHER_H_\n')


def ParseOptions():
  parser = optparse.OptionParser()
  parser.add_option('--id_file', dest='id_file', help='Path to id.def')
  parser.add_option('--special_pos_file', dest='special_pos_file',
                    help='Path to special_pos.def')
  parser.add_option('--pos_matcher_rule_file', dest='pos_matcher_rule_file',
                    help='Path to pos_matcher_rule.def')
  parser.add_option('--output_pos_matcher_data',
                    dest='output_pos_matcher_data',
                    default='',
                    help='Path to the output file of pos matcher data.')
  parser.add_option('--output_pos_matcher_h',
                    dest='output_pos_matcher_h',
                    default='',
                    help='Path to the output header file of POSMatcher.')
  return parser.parse_args()[0]


def main():
  options = ParseOptions()

  if options.output_pos_matcher_h:
    # To generate a header file of POSMatcher, you don't need to specify
    # --id_file and --special_pos_file because empty POS database sufficies.
    pos_database = pos_util.PosDataBase()
    pos_matcher = pos_util.PosMatcher(pos_database)
    pos_matcher.Parse(options.pos_matcher_rule_file)
    with codecs.open(options.output_pos_matcher_h, 'w',
                     encoding='utf-8') as stream:
      OutputPosMatcherHeader(pos_matcher, stream)

  if options.output_pos_matcher_data:
    pos_database = pos_util.PosDataBase()
    pos_database.Parse(options.id_file, options.special_pos_file)
    pos_matcher = pos_util.PosMatcher(pos_database)
    pos_matcher.Parse(options.pos_matcher_rule_file)
    with codecs.open(options.output_pos_matcher_data, 'wb') as stream:
      OutputPosMatcherData(pos_matcher, stream)


if __name__ == '__main__':
  main()
