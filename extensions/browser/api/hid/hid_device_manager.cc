// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "extensions/browser/api/hid/hid_device_manager.h"

#include <limits>
#include <vector>

#include "base/lazy_instance.h"
#include "device/core/device_client.h"
#include "device/hid/hid_device_filter.h"
#include "device/hid/hid_service.h"
#include "extensions/common/permissions/permissions_data.h"
#include "extensions/common/permissions/usb_device_permission.h"

namespace hid = extensions::core_api::hid;

using device::HidDeviceFilter;
using device::HidDeviceId;
using device::HidDeviceInfo;
using device::HidService;

namespace extensions {

namespace {

void PopulateHidDeviceInfo(hid::HidDeviceInfo* output,
                           const HidDeviceInfo& input) {
  output->vendor_id = input.vendor_id;
  output->product_id = input.product_id;
  output->max_input_report_size = input.max_input_report_size;
  output->max_output_report_size = input.max_output_report_size;
  output->max_feature_report_size = input.max_feature_report_size;

  for (const device::HidCollectionInfo& collection : input.collections) {
    // Don't expose sensitive data.
    if (collection.usage.IsProtected()) {
      continue;
    }

    hid::HidCollectionInfo* api_collection = new hid::HidCollectionInfo();
    api_collection->usage_page = collection.usage.usage_page;
    api_collection->usage = collection.usage.usage;

    api_collection->report_ids.resize(collection.report_ids.size());
    std::copy(collection.report_ids.begin(), collection.report_ids.end(),
              api_collection->report_ids.begin());

    output->collections.push_back(make_linked_ptr(api_collection));
  }
}

}  // namespace

struct HidDeviceManager::GetApiDevicesParams {
 public:
  GetApiDevicesParams(const Extension* extension,
                      const std::vector<HidDeviceFilter>& filters,
                      const GetApiDevicesCallback& callback)
      : extension(extension), filters(filters), callback(callback) {}
  ~GetApiDevicesParams() {}

  const Extension* extension;
  std::vector<HidDeviceFilter> filters;
  GetApiDevicesCallback callback;
};

HidDeviceManager::HidDeviceManager(content::BrowserContext* context)
    : weak_factory_(this),
      initialized_(false),
      hid_service_observer_(this),
      enumeration_ready_(false),
      next_resource_id_(0) {
}

HidDeviceManager::~HidDeviceManager() {
  DCHECK(thread_checker_.CalledOnValidThread());
}

// static
BrowserContextKeyedAPIFactory<HidDeviceManager>*
HidDeviceManager::GetFactoryInstance() {
  static base::LazyInstance<BrowserContextKeyedAPIFactory<HidDeviceManager> >
      factory = LAZY_INSTANCE_INITIALIZER;
  return &factory.Get();
}

void HidDeviceManager::GetApiDevices(
    const Extension* extension,
    const std::vector<HidDeviceFilter>& filters,
    const GetApiDevicesCallback& callback) {
  DCHECK(thread_checker_.CalledOnValidThread());
  LazyInitialize();

  if (enumeration_ready_) {
    scoped_ptr<base::ListValue> devices =
        CreateApiDeviceList(extension, filters);
    base::MessageLoop::current()->PostTask(
        FROM_HERE, base::Bind(callback, base::Passed(&devices)));
  } else {
    pending_enumerations_.push_back(
        new GetApiDevicesParams(extension, filters, callback));
  }
}

bool HidDeviceManager::GetDeviceInfo(int resource_id,
                                     HidDeviceInfo* device_info) {
  DCHECK(thread_checker_.CalledOnValidThread());
  HidService* hid_service = device::DeviceClient::Get()->GetHidService();
  DCHECK(hid_service);

  ResourceIdToDeviceIdMap::const_iterator device_iter =
      device_ids_.find(resource_id);
  if (device_iter == device_ids_.end()) {
    return false;
  }

  return hid_service->GetDeviceInfo(device_iter->second, device_info);
}

bool HidDeviceManager::HasPermission(const Extension* extension,
                                     const device::HidDeviceInfo& device_info) {
  DCHECK(thread_checker_.CalledOnValidThread());
  UsbDevicePermission::CheckParam usbParam(
      device_info.vendor_id,
      device_info.product_id,
      UsbDevicePermissionData::UNSPECIFIED_INTERFACE);
  if (extension->permissions_data()->CheckAPIPermissionWithParam(
          APIPermission::kUsbDevice, &usbParam)) {
    return true;
  }

  if (extension->permissions_data()->HasAPIPermission(
          APIPermission::kU2fDevices)) {
    HidDeviceFilter u2f_filter;
    u2f_filter.SetUsagePage(0xF1D0);
    if (u2f_filter.Matches(device_info)) {
      return true;
    }
  }

  return false;
}

void HidDeviceManager::OnDeviceAdded(const HidDeviceInfo& device_info) {
  DCHECK(thread_checker_.CalledOnValidThread());
  DCHECK_LT(next_resource_id_, std::numeric_limits<int>::max());
  int new_id = next_resource_id_++;
  DCHECK(!ContainsKey(resource_ids_, device_info.device_id));
  resource_ids_[device_info.device_id] = new_id;
  device_ids_[new_id] = device_info.device_id;
}

void HidDeviceManager::OnDeviceRemoved(const HidDeviceInfo& device_info) {
  DCHECK(thread_checker_.CalledOnValidThread());
  const auto& resource_entry = resource_ids_.find(device_info.device_id);
  DCHECK(resource_entry != resource_ids_.end());
  int resource_id = resource_entry->second;
  const auto& device_entry = device_ids_.find(resource_id);
  DCHECK(device_entry != device_ids_.end());
  resource_ids_.erase(resource_entry);
  device_ids_.erase(device_entry);
}

void HidDeviceManager::LazyInitialize() {
  DCHECK(thread_checker_.CalledOnValidThread());

  if (initialized_) {
    return;
  }

  HidService* hid_service = device::DeviceClient::Get()->GetHidService();
  DCHECK(hid_service);
  hid_service->GetDevices(base::Bind(&HidDeviceManager::OnEnumerationComplete,
                                     weak_factory_.GetWeakPtr()));

  hid_service_observer_.Add(hid_service);
  initialized_ = true;
}

scoped_ptr<base::ListValue> HidDeviceManager::CreateApiDeviceList(
    const Extension* extension,
    const std::vector<HidDeviceFilter>& filters) {
  HidService* hid_service = device::DeviceClient::Get()->GetHidService();
  DCHECK(hid_service);

  scoped_ptr<base::ListValue> api_devices(new base::ListValue());
  for (const std::pair<int, HidDeviceId>& map_entry : device_ids_) {
    int resource_id = map_entry.first;
    const HidDeviceId& device_id = map_entry.second;

    HidDeviceInfo device_info;
    if (!hid_service->GetDeviceInfo(device_id, &device_info)) {
      continue;
    }

    if (!filters.empty() &&
        !HidDeviceFilter::MatchesAny(device_info, filters)) {
      continue;
    }

    if (!HasPermission(extension, device_info)) {
      continue;
    }

    hid::HidDeviceInfo api_device_info;
    api_device_info.device_id = resource_id;
    PopulateHidDeviceInfo(&api_device_info, device_info);

    // Expose devices with which user can communicate.
    if (api_device_info.collections.size() > 0) {
      api_devices->Append(api_device_info.ToValue().release());
    }
  }

  return api_devices.Pass();
}

void HidDeviceManager::OnEnumerationComplete(
    const std::vector<HidDeviceInfo>& devices) {
  DCHECK(resource_ids_.empty());
  DCHECK(device_ids_.empty());
  for (const device::HidDeviceInfo& device_info : devices) {
    OnDeviceAdded(device_info);
  }
  enumeration_ready_ = true;

  for (const GetApiDevicesParams* params : pending_enumerations_) {
    scoped_ptr<base::ListValue> devices =
        CreateApiDeviceList(params->extension, params->filters);
    params->callback.Run(devices.Pass());
  }
  pending_enumerations_.clear();
}

}  // namespace extensions
