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

#include "rewriter/calculator_rewriter.h"

#include <algorithm>
#include <string>

#include "base/japanese_util.h"
#include "base/logging.h"
#include "base/util.h"
#include "config/config_handler.h"
#include "converter/converter_interface.h"
#include "converter/segments.h"
#include "protocol/commands.pb.h"
#include "protocol/config.pb.h"
#include "request/conversion_request.h"
#include "rewriter/calculator/calculator_interface.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"

namespace mozc {

CalculatorRewriter::CalculatorRewriter(
    const ConverterInterface *parent_converter)
    : parent_converter_(parent_converter) {
  DCHECK(parent_converter_);
}

int CalculatorRewriter::capability(const ConversionRequest &request) const {
  if (request.request().mixed_conversion()) {
    return RewriterInterface::ALL;
  }
  return RewriterInterface::CONVERSION;
}

// Rewrites candidates when conversion segments of |segments| represents an
// expression that can be calculated. In such case, if |segments| consists
// of multiple segments, it merges them by calling ConverterInterface::
// ResizeSegment(), otherwise do calculation and insertion.
// TODO(tok): It currently calculates same expression twice, if |segments| is
//            a valid expression.
bool CalculatorRewriter::Rewrite(const ConversionRequest &request,
                                 Segments *segments) const {
  if (!request.config().use_calculator()) {
    return false;
  }

  CalculatorInterface *calculator = CalculatorFactory::GetCalculator();

  const size_t segments_size = segments->conversion_segments_size();
  if (segments_size == 0) {
    return false;
  }

  // If |segments| has only one conversion segment, try calculation and insert
  // the result on success.
  if (segments_size == 1) {
    const std::string &key = segments->conversion_segment(0).key();
    std::string result;
    if (key.empty()) {
      return false;
    }
    if (!calculator->CalculateString(key, &result)) {
      return false;
    }
    // Insert the result.
    if (!InsertCandidate(result, 0, segments->mutable_conversion_segment(0))) {
      return false;
    }
    return true;
  }

  // Merge keys of all conversion segments and try calculation.
  std::string merged_key;
  for (size_t i = 0; i < segments->conversion_segments_size(); ++i) {
    merged_key += segments->conversion_segment(i).key();
  }
  // The decision to calculate and calculation itself are both done by the
  // calculator.
  std::string result;
  if (!calculator->CalculateString(merged_key, &result)) {
    return false;
  }

  // Merge all conversion segments.
  int offset = Util::CharsLen(merged_key) -
               Util::CharsLen(segments->conversion_segment(0).key());
  // ConverterInterface::ResizeSegment() calls Rewriter::Rewrite(), so
  // CalculatorRewriter::Rewrite() is recursively called with merged
  // conversion segment.
  if (!parent_converter_->ResizeSegment(segments, request, 0, offset)) {
    LOG(ERROR) << "Failed to merge conversion segments";
    return false;
  }
  return true;
}

bool CalculatorRewriter::InsertCandidate(const absl::string_view value,
                                         const size_t insert_pos,
                                         Segment *segment) const {
  if (segment->candidates_size() == 0) {
    LOG(WARNING) << "candidates_size is 0";
    return false;
  }

  const Segment::Candidate &base_candidate = segment->candidate(0);

  // Normalize the expression, used in description.
  std::string temp, temp2, expression;
  japanese_util::FullWidthAsciiToHalfWidthAscii(base_candidate.content_key,
                                                &temp);
  Util::StringReplace(temp, "・", "/", true, &temp2);
  // "ー", onbiki
  Util::StringReplace(temp2, "ー", "-", true, &expression);

  size_t offset = std::min(insert_pos, segment->candidates_size());

  for (int n = 0; n < 2; ++n) {
    int current_offset = offset + n;
    Segment::Candidate *candidate = segment->insert_candidate(current_offset);
    if (candidate == nullptr) {
      LOG(ERROR) << "cannot insert candidate at " << current_offset;
      return false;
    }

    // Simply sets some member variables of the new candidate to ones of the
    // existing candidate next to it.
    size_t reference_index = current_offset + 1;
    if (reference_index >= segment->candidates_size()) {
      reference_index = current_offset - 1;
    }
    const Segment::Candidate &reference_candidate =
        segment->candidate(reference_index);

    candidate->Init();
    candidate->lid = reference_candidate.lid;
    candidate->rid = reference_candidate.rid;
    candidate->cost = reference_candidate.cost;
    candidate->key = base_candidate.key;
    candidate->content_key = base_candidate.content_key;
    candidate->attributes |= Segment::Candidate::NO_VARIANTS_EXPANSION;
    candidate->attributes |= Segment::Candidate::NO_LEARNING;
    candidate->description = "計算結果";

    if (n == 0) {  // without expression
      candidate->value = std::string(value);
      candidate->content_value = std::string(value);
    } else {  // with expression
      DCHECK(!expression.empty());
      if (expression.front() == '=') {
        // Expression starts with '='.
        // Appends value to the left of expression.
        candidate->value = absl::StrCat(value, expression);
        candidate->content_value = absl::StrCat(value, expression);
      } else {
        // Expression ends with '='.
        // Appends value to the right of expression.
        candidate->value = absl::StrCat(expression, value);
        candidate->content_value = absl::StrCat(expression, value);
      }
    }
  }

  return true;
}
}  // namespace mozc
