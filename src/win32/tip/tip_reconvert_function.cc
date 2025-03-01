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

#include "win32/tip/tip_reconvert_function.h"

#define _ATL_NO_AUTOMATIC_NAMESPACE
#define _WTL_NO_AUTOMATIC_NAMESPACE
#include <atlbase.h>
#include <atlcom.h>
#include <ctffunc.h>
#include <msctf.h>
#include <wrl/client.h>

#include <cstddef>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "base/win32/com.h"
#include "win32/tip/tip_candidate_list.h"
#include "win32/tip/tip_edit_session.h"
#include "win32/tip/tip_query_provider.h"
#include "win32/tip/tip_ref_count.h"
#include "win32/tip/tip_surrounding_text.h"
#include "win32/tip/tip_text_service.h"

namespace mozc {
namespace win32 {
namespace tsf {
namespace {

using ATL::CComBSTR;
using Microsoft::WRL::ComPtr;

#ifdef GOOGLE_JAPANESE_INPUT_BUILD
constexpr wchar_t kReconvertFunctionDisplayName[] =
    L"Google Japanese Input: Reconversion Function";
#else   // GOOGLE_JAPANESE_INPUT_BUILD
constexpr wchar_t kReconvertFunctionDisplayName[] =
    L"Mozc: Reconversion Function";
#endif  // GOOGLE_JAPANESE_INPUT_BUILD

class CandidateListCallbackImpl : public TipCandidateListCallback {
 public:
  CandidateListCallbackImpl(ComPtr<TipTextService> text_service,
                            ComPtr<ITfRange> range)
      : text_service_(std::move(text_service)), range_(std::move(range)) {}
  CandidateListCallbackImpl(const CandidateListCallbackImpl &) = delete;
  CandidateListCallbackImpl &operator=(const CandidateListCallbackImpl &) =
      delete;

 private:
  // TipCandidateListCallback overrides:
  virtual void OnFinalize(size_t index, const std::wstring &candidate) {
    TipEditSession::SetTextAsync(text_service_.Get(), candidate, range_.Get());
  }

  ComPtr<TipTextService> text_service_;
  ComPtr<ITfRange> range_;
};

class ReconvertFunctionImpl final : public ITfFnReconversion {
 public:
  explicit ReconvertFunctionImpl(ComPtr<TipTextService> text_service)
      : text_service_(std::move(text_service)) {}
  ReconvertFunctionImpl(const ReconvertFunctionImpl &) = delete;
  ReconvertFunctionImpl &operator=(const ReconvertFunctionImpl &) = delete;
  ~ReconvertFunctionImpl() = default;

  // The IUnknown interface methods.
  STDMETHODIMP QueryInterface(REFIID interface_id, void **object) {
    if (!object) {
      return E_INVALIDARG;
    }

    // Find a matching interface from the ones implemented by this object.
    // This object implements IUnknown and ITfEditSession.
    if (::IsEqualIID(interface_id, IID_IUnknown)) {
      *object = static_cast<IUnknown *>(this);
    } else if (IsEqualIID(interface_id, IID_ITfFunction)) {
      *object = static_cast<ITfFunction *>(this);
    } else if (IsEqualIID(interface_id, IID_ITfFnReconversion)) {
      *object = static_cast<ITfFnReconversion *>(this);
    } else {
      *object = nullptr;
      return E_NOINTERFACE;
    }

    AddRef();
    return S_OK;
  }

  STDMETHODIMP_(ULONG) AddRef() { return ref_count_.AddRefImpl(); }

  STDMETHODIMP_(ULONG) Release() {
    const ULONG count = ref_count_.ReleaseImpl();
    if (count == 0) {
      delete this;
    }
    return count;
  }

 private:
  // The ITfFunction interface method.
  virtual HRESULT STDMETHODCALLTYPE GetDisplayName(BSTR *name) {
    if (name == nullptr) {
      return E_INVALIDARG;
    }
    *name = CComBSTR(kReconvertFunctionDisplayName).Detach();
    return S_OK;
  }

  // The ITfFnReconversion interface method.
  virtual HRESULT STDMETHODCALLTYPE QueryRange(ITfRange *range,
                                               ITfRange **new_range,
                                               BOOL *convertible) {
    if (range == nullptr) {
      return E_INVALIDARG;
    }
    if (new_range == nullptr) {
      return E_INVALIDARG;
    }
    BOOL dummy_bool = FALSE;
    if (convertible == nullptr) {
      convertible = &dummy_bool;
    }
    *convertible = FALSE;
    *new_range = nullptr;

    ComPtr<ITfContext> context;
    if (FAILED(range->GetContext(&context))) {
      return E_FAIL;
    }

    TipSurroundingTextInfo info;
    if (!TipSurroundingText::Get(text_service_.Get(), context.Get(), &info)) {
      return E_FAIL;
    }

    if (info.in_composition) {
      // on-going composition is found.
      *convertible = FALSE;
      *new_range = nullptr;
      return S_OK;
    }

    if (info.selected_text.find(static_cast<wchar_t>(TS_CHAR_EMBEDDED)) !=
        std::wstring::npos) {
      // embedded object is found.
      *convertible = FALSE;
      *new_range = nullptr;
      return S_OK;
    }

    if (FAILED(range->Clone(new_range))) {
      return E_FAIL;
    }
    *convertible = TRUE;
    return S_OK;
  }

  virtual HRESULT STDMETHODCALLTYPE
  GetReconversion(ITfRange *range, ITfCandidateList **candidate_list) {
    if (range == nullptr) {
      return E_INVALIDARG;
    }
    if (candidate_list == nullptr) {
      return E_INVALIDARG;
    }
    std::unique_ptr<TipQueryProvider> provider(TipQueryProvider::Create());
    if (!provider) {
      return E_FAIL;
    }
    std::wstring query;
    if (!TipEditSession::GetTextSync(text_service_.Get(), range, &query)) {
      return E_FAIL;
    }
    std::vector<std::wstring> candidates;
    if (!provider->Query(query, TipQueryProvider::kReconversion, &candidates)) {
      return E_FAIL;
    }
    auto callback =
        std::make_unique<CandidateListCallbackImpl>(text_service_, range);
    *candidate_list =
        TipCandidateList::New(std::move(candidates), std::move(callback))
            .Detach();
    return S_OK;
  }

  virtual HRESULT STDMETHODCALLTYPE Reconvert(ITfRange *range) {
    if (range == nullptr) {
      return E_INVALIDARG;
    }

    if (!TipEditSession::ReconvertFromApplicationSync(text_service_.Get(),
                                                      range)) {
      return E_FAIL;
    }
    return S_OK;
  }

  TipRefCount ref_count_;
  ComPtr<TipTextService> text_service_;
};

}  // namespace

ComPtr<ITfFnReconversion> TipReconvertFunction::New(
    ComPtr<TipTextService> text_service) {
  return MakeComPtr<ReconvertFunctionImpl>(std::move(text_service));
}

}  // namespace tsf
}  // namespace win32
}  // namespace mozc
