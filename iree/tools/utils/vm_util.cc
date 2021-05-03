// Copyright 2020 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "iree/tools/utils/vm_util.h"

#include <ostream>

#include "absl/strings/numbers.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "iree/base/internal/file_io.h"
#include "iree/base/signature_parser.h"
#include "iree/base/status.h"
#include "iree/hal/api.h"
#include "iree/modules/hal/hal_module.h"
#include "iree/vm/bytecode_module.h"

namespace iree {

Status GetFileContents(const char* path, std::string* out_contents) {
  IREE_TRACE_ZONE_BEGIN(z0);
  *out_contents = std::string();
  FILE* file = fopen(path, "rb");
  if (file == NULL) {
    IREE_TRACE_ZONE_END(z0);
    return iree_make_status(iree_status_code_from_errno(errno),
                            "failed to open file '%s'", path);
  }
  iree_status_t status = iree_ok_status();
  if (fseek(file, 0, SEEK_END) == -1) {
    status = iree_make_status(iree_status_code_from_errno(errno), "seek (end)");
  }
  size_t file_size = 0;
  if (iree_status_is_ok(status)) {
    file_size = ftell(file);
    if (file_size == -1L) {
      status =
          iree_make_status(iree_status_code_from_errno(errno), "size query");
    }
  }
  if (iree_status_is_ok(status)) {
    if (fseek(file, 0, SEEK_SET) == -1) {
      status =
          iree_make_status(iree_status_code_from_errno(errno), "seek (beg)");
    }
  }
  std::string contents;
  if (iree_status_is_ok(status)) {
    contents.resize(file_size);
    if (fread((char*)contents.data(), file_size, 1, file) != 1) {
      status =
          iree_make_status(iree_status_code_from_errno(errno),
                           "unable to read entire file contents of '%s'", path);
    }
  }
  if (iree_status_is_ok(status)) {
    *out_contents = std::move(contents);
  }
  fclose(file);
  IREE_TRACE_ZONE_END(z0);
  return status;
}

Status ValidateFunctionAbi(const iree_vm_function_t& function) {
  // Benchmark functions are always allowed through as they are () -> ().
  // That we are requiring SIP for everything in this util file is bad, and this
  // workaround at least allows us to benchmark non-SIP functions.
  if (iree_vm_function_reflection_attr(&function, IREE_SV("benchmark")).size !=
      0) {
    return OkStatus();
  }

  iree_string_view_t sig_fv =
      iree_vm_function_reflection_attr(&function, IREE_SV("fv"));
  if (absl::string_view{sig_fv.data, sig_fv.size} != "1") {
    auto function_name = iree_vm_function_name(&function);
    return iree_make_status(IREE_STATUS_UNIMPLEMENTED,
                            "unsupported function ABI for: '%.*s'(%.*s)",
                            (int)function_name.size, function_name.data,
                            (int)sig_fv.size, sig_fv.data);
  }
  return OkStatus();
}

Status ParseInputSignature(
    iree_vm_function_t& function,
    std::vector<RawSignatureParser::Description>* out_input_descs) {
  out_input_descs->clear();
  iree_string_view_t sig_f =
      iree_vm_function_reflection_attr(&function, IREE_SV("f"));
  if (sig_f.size == 0) return OkStatus();
  RawSignatureParser sig_parser;
  sig_parser.VisitInputs(absl::string_view{sig_f.data, sig_f.size},
                         [&](const RawSignatureParser::Description& desc) {
                           out_input_descs->push_back(desc);
                         });
  if (sig_parser.GetError()) {
    return iree_make_status(
        IREE_STATUS_INVALID_ARGUMENT,
        "parsing function signature '%.*s' failed getting input",
        (int)sig_f.size, sig_f.data);
  }
  return OkStatus();
}

Status ParseOutputSignature(
    const iree_vm_function_t& function,
    std::vector<RawSignatureParser::Description>* out_output_descs) {
  out_output_descs->clear();
  iree_string_view_t sig_f =
      iree_vm_function_reflection_attr(&function, IREE_SV("f"));
  if (sig_f.size == 0) return OkStatus();
  RawSignatureParser sig_parser;
  sig_parser.VisitResults(absl::string_view{sig_f.data, sig_f.size},
                          [&](const RawSignatureParser::Description& desc) {
                            out_output_descs->push_back(desc);
                          });
  if (sig_parser.GetError()) {
    return iree_make_status(
        IREE_STATUS_INVALID_ARGUMENT,
        "parsing function signature '%.*s' failed getting input",
        (int)sig_f.size, sig_f.data);
  }
  return OkStatus();
}

Status ParseToVariantList(
    absl::Span<const RawSignatureParser::Description> descs,
    iree_hal_allocator_t* allocator,
    absl::Span<const absl::string_view> input_strings,
    iree_vm_list_t** out_list) {
  *out_list = NULL;
  if (input_strings.size() != descs.size()) {
    return iree_make_status(
        IREE_STATUS_INVALID_ARGUMENT,
        "signature mismatch; expected %zu buffer strings but received %zu",
        descs.size(), input_strings.size());
  }
  vm::ref<iree_vm_list_t> variant_list;
  IREE_RETURN_IF_ERROR(
      iree_vm_list_create(/*element_type=*/nullptr, input_strings.size(),
                          iree_allocator_system(), &variant_list));
  for (size_t i = 0; i < input_strings.size(); ++i) {
    auto input_string = input_strings[i];
    auto desc = descs[i];
    std::string desc_str;
    desc.ToString(desc_str);
    switch (desc.type) {
      case RawSignatureParser::Type::kScalar: {
        if (desc.scalar.type != AbiConstants::ScalarType::kSint32) {
          return iree_make_status(IREE_STATUS_UNIMPLEMENTED,
                                  "unsupported signature scalar type: %s",
                                  desc_str.c_str());
        }
        iree_string_view_t input_view = iree_string_view_trim(
            iree_make_string_view(input_string.data(), input_string.size()));
        input_view = iree_string_view_strip_prefix(input_view, IREE_SV("\""));
        input_view = iree_string_view_strip_suffix(input_view, IREE_SV("\""));
        if (!iree_string_view_consume_prefix(&input_view, IREE_SV("i32="))) {
          return iree_make_status(IREE_STATUS_INVALID_ARGUMENT,
                                  "parsing '%.*s'; has i32 descriptor but does "
                                  "not start with 'i32='",
                                  (int)input_string.size(),
                                  input_string.data());
        }
        iree_vm_value_t val = iree_vm_value_make_i32(0);
        if (!absl::SimpleAtoi(
                absl::string_view(input_view.data, input_view.size),
                &val.i32)) {
          return iree_make_status(
              IREE_STATUS_INVALID_ARGUMENT,
              "converting '%.*s' to i32 when parsing '%.*s'",
              (int)input_view.size, input_view.data, (int)input_string.size(),
              input_string.data());
        }
        IREE_RETURN_IF_ERROR(iree_vm_list_push_value(variant_list.get(), &val));
        break;
      }
      case RawSignatureParser::Type::kBuffer: {
        iree_hal_buffer_view_t* buffer_view = nullptr;
        IREE_RETURN_IF_ERROR(
            iree_hal_buffer_view_parse(
                iree_string_view_t{input_string.data(), input_string.size()},
                allocator, iree_allocator_system(), &buffer_view),
            "parsing value '%.*s'", (int)input_string.size(),
            input_string.data());
        auto buffer_view_ref = iree_hal_buffer_view_move_ref(buffer_view);
        IREE_RETURN_IF_ERROR(
            iree_vm_list_push_ref_move(variant_list.get(), &buffer_view_ref));
        break;
      }
      default:
        return iree_make_status(IREE_STATUS_UNIMPLEMENTED,
                                "unsupported signature type: %s",
                                desc_str.c_str());
    }
  }
  *out_list = variant_list.release();
  return OkStatus();
}

Status ParseToVariantList(
    absl::Span<const RawSignatureParser::Description> descs,
    iree_hal_allocator_t* allocator,
    absl::Span<const std::string> input_strings, iree_vm_list_t** out_list) {
  std::vector<absl::string_view> input_views(input_strings.size());
  for (int i = 0; i < input_strings.size(); ++i) {
    input_views[i] = input_strings[i];
  }
  return ParseToVariantList(descs, allocator, input_views, out_list);
}

Status PrintVariantList(absl::Span<const RawSignatureParser::Description> descs,
                        iree_vm_list_t* variant_list, std::ostream* os) {
  for (int i = 0; i < iree_vm_list_size(variant_list); ++i) {
    iree_vm_variant_t variant = iree_vm_variant_empty();
    IREE_RETURN_IF_ERROR(iree_vm_list_get_variant(variant_list, i, &variant),
                         "variant %d not present", i);

    const auto& desc = descs[i];
    std::string desc_str;
    desc.ToString(desc_str);
    IREE_LOG(INFO) << "result[" << i << "]: " << desc_str;

    switch (desc.type) {
      case RawSignatureParser::Type::kScalar: {
        if (variant.type.value_type != IREE_VM_VALUE_TYPE_I32) {
          return iree_make_status(
              IREE_STATUS_INVALID_ARGUMENT,
              "variant %d has value type %d but descriptor information %s", i,
              (int)variant.type.value_type, desc_str.c_str());
        }
        if (desc.scalar.type != AbiConstants::ScalarType::kSint32) {
          return iree_make_status(IREE_STATUS_UNIMPLEMENTED,
                                  "unsupported signature scalar type: %s",
                                  desc_str.c_str());
        }
        *os << "i32=" << variant.i32 << "\n";
        break;
      }
      case RawSignatureParser::Type::kBuffer: {
        if (!iree_vm_type_def_is_ref(&variant.type)) {
          return iree_make_status(
              IREE_STATUS_INVALID_ARGUMENT,
              "variant %d has value type %d but descriptor information %s", i,
              (int)variant.type.value_type, desc_str.c_str());
        }
        auto* buffer_view = iree_hal_buffer_view_deref(variant.ref);
        if (!buffer_view) {
          return iree_make_status(IREE_STATUS_INVALID_ARGUMENT,
                                  "failed dereferencing variant %d", i);
        }

        std::string result_str(4096, '\0');
        iree_status_t status;
        do {
          iree_host_size_t actual_length = 0;
          status = iree_hal_buffer_view_format(
              buffer_view, /*max_element_count=*/1024, result_str.size() + 1,
              &result_str[0], &actual_length);
          result_str.resize(actual_length);
        } while (iree_status_is_out_of_range(status));
        IREE_RETURN_IF_ERROR(status);

        *os << result_str << "\n";
        break;
      }
      default:
        return iree_make_status(IREE_STATUS_UNIMPLEMENTED,
                                "unsupported signature type: %s",
                                desc_str.c_str());
    }
  }

  return OkStatus();
}

Status CreateDevice(absl::string_view driver_name,
                    iree_hal_device_t** out_device) {
  IREE_LOG(INFO) << "Creating driver and device for '" << driver_name << "'...";
  iree_hal_driver_t* driver = nullptr;
  IREE_RETURN_IF_ERROR(
      iree_hal_driver_registry_try_create_by_name(
          iree_hal_driver_registry_default(),
          iree_string_view_t{driver_name.data(), driver_name.size()},
          iree_allocator_system(), &driver),
      "creating driver '%.*s'", (int)driver_name.size(), driver_name.data());
  IREE_RETURN_IF_ERROR(iree_hal_driver_create_default_device(
                           driver, iree_allocator_system(), out_device),
                       "creating default device for driver '%.*s'",
                       (int)driver_name.size(), driver_name.data());
  iree_hal_driver_release(driver);
  return OkStatus();
}

Status CreateHalModule(iree_hal_device_t* device,
                       iree_vm_module_t** out_module) {
  IREE_RETURN_IF_ERROR(
      iree_hal_module_create(device, iree_allocator_system(), out_module),
      "creating HAL module");
  return OkStatus();
}

Status LoadBytecodeModule(absl::string_view module_data,
                          iree_vm_module_t** out_module) {
  IREE_RETURN_IF_ERROR(
      iree_vm_bytecode_module_create(
          iree_const_byte_span_t{
              reinterpret_cast<const uint8_t*>(module_data.data()),
              module_data.size()},
          iree_allocator_null(), iree_allocator_system(), out_module),
      "deserializing module");
  return OkStatus();
}
}  // namespace iree
