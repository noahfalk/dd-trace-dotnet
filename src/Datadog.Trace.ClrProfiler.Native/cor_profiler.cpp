#include "cor_profiler.h"

#include <corprof.h>
#include <string>
#include "corhlpr.h"

#include "version.h"
#include "clr_helpers.h"
#include "dd_profiler_constants.h"
#include "dllmain.h"
#include "environment_variables.h"
#include "il_rewriter.h"
#include "il_rewriter_wrapper.h"
#include "integration_loader.h"
#include "logging.h"
#include "metadata_builder.h"
#include "module_metadata.h"
#include "pal.h"
#include "sig_helpers.h"
#include "resource.h"
#include "util.h"

namespace trace {

CorProfiler* profiler = nullptr;

class __declspec(uuid("AF1A5647-2F09-4F0F-8423-7612D1CAC640"))
    InstrumentationDataItem : public IUnknown
{
 private:
  std::atomic<int> ref_count_;

 public:
  mdMethodDef m_startupMethod;

  InstrumentationDataItem() : 
      ref_count_(0),
      m_startupMethod(0)
  {}

  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid,
                                           void** ppvObject) override {
    if (riid == IID_IUnknown) {
      *ppvObject = static_cast<IUnknown*>(this);
      AddRef();
      return S_OK;
    }

    *ppvObject = nullptr;
    return E_NOINTERFACE;
  }

  ULONG STDMETHODCALLTYPE AddRef(void) override {
    return std::atomic_fetch_add(&this->ref_count_, 1) + 1;
  }

  ULONG STDMETHODCALLTYPE Release(void) override {
    int count = std::atomic_fetch_sub(&this->ref_count_, 1) - 1;

    if (count <= 0) {
      delete this;
    }

    return count;
  }
};

HRESULT STDMETHODCALLTYPE
CorProfiler::Initialize(_In_ IProfilerManager* pProfilerManager) {

  ComPtr<IUnknown> pProfilerInfo;
  pProfilerManager->GetCorProfilerInfo((IUnknown**)&pProfilerInfo);
  IUnknown* cor_profiler_info_unknown = pProfilerInfo.Get();

  // check if debug mode is enabled
  const auto debug_enabled_value =
      GetEnvironmentValue(environment::debug_enabled);

  if (debug_enabled_value == "1"_W || debug_enabled_value == "true"_W) {
    debug_logging_enabled = true;
  }

  CorProfilerBase::Initialize(cor_profiler_info_unknown);

  // check if tracing is completely disabled
  const WSTRING tracing_enabled =
      GetEnvironmentValue(environment::tracing_enabled);

  if (tracing_enabled == "0"_W || tracing_enabled == "false"_W) {
    Info("Profiler disabled in ", environment::tracing_enabled);
    return E_FAIL;
  }

  const auto process_name = GetCurrentProcessName();
  const auto include_process_names =
      GetEnvironmentValues(environment::include_process_names);

  // if there is a process inclusion list, attach profiler only if this
  // process's name is on the list
  if (!include_process_names.empty() &&
      !Contains(include_process_names, process_name)) {
    Info("Profiler disabled: ", process_name, " not found in ",
         environment::include_process_names, ".");
    return E_FAIL;
  }

  const auto exclude_process_names =
      GetEnvironmentValues(environment::exclude_process_names);

  // attach profiler only if this process's name is NOT on the list
  if (Contains(exclude_process_names, process_name)) {
    Info("Profiler disabled: ", process_name, " found in ",
         environment::exclude_process_names, ".");
    return E_FAIL;
  }

  // get Profiler interface
  HRESULT hr = cor_profiler_info_unknown->QueryInterface<ICorProfilerInfo3>(
      &this->info_);
  if (FAILED(hr)) {
    Warn("Failed to attach profiler: interface ICorProfilerInfo3 not found.");
    return E_FAIL;
  }

  Info("Environment variables:");

  for (auto&& env_var : env_vars_to_display) {
    Info("  ", env_var, "=", GetEnvironmentValue(env_var));
  }

  const WSTRING azure_app_services_value =
      GetEnvironmentValue(environment::azure_app_services);

  if (azure_app_services_value == "1"_W) {
    Info("Profiler is operating within Azure App Services context.");
    in_azure_app_services = true;

    const auto app_pool_id_value =
        GetEnvironmentValue(environment::azure_app_services_app_pool_id);

    if (app_pool_id_value.size() > 1 && app_pool_id_value.at(0) == '~') {
      Info("Profiler disabled: ", environment::azure_app_services_app_pool_id,
           " ", app_pool_id_value,
           " is recognized as an Azure App Services infrastructure process.");
      return E_FAIL;
    }

    const auto cli_telemetry_profile_value = GetEnvironmentValue(
        environment::azure_app_services_cli_telemetry_profile_value);

    if (cli_telemetry_profile_value == "AzureKudu"_W) {
      Info("Profiler disabled: ", app_pool_id_value,
           " is recognized as Kudu, an Azure App Services reserved process.");
      return E_FAIL;
    }
  }

  // get path to integration definition JSON files
  const WSTRING integrations_paths =
      GetEnvironmentValue(environment::integrations_path);

  if (integrations_paths.empty()) {
    Warn("Profiler disabled: ", environment::integrations_path,
         " environment variable not set.");
    return E_FAIL;
  }

  // load all available integrations from JSON files
  const std::vector<Integration> all_integrations =
      LoadIntegrationsFromEnvironment();

  // get list of disabled integration names
  const std::vector<WSTRING> disabled_integration_names =
      GetEnvironmentValues(environment::disabled_integrations);

  // remove disabled integrations
  integrations_ =
      FilterIntegrationsByName(all_integrations, disabled_integration_names);

  // check if there are any enabled integrations left
  if (integrations_.empty()) {
    Warn("Profiler disabled: no enabled integrations found.");
    return E_FAIL;
  }

  DWORD event_mask = COR_PRF_MONITOR_JIT_COMPILATION |
                     COR_PRF_DISABLE_TRANSPARENCY_CHECKS_UNDER_FULL_TRUST |
                     COR_PRF_DISABLE_INLINING | COR_PRF_MONITOR_MODULE_LOADS |
                     COR_PRF_MONITOR_ASSEMBLY_LOADS |
                     COR_PRF_DISABLE_ALL_NGEN_IMAGES;

  if (DisableOptimizations()) {
    Info("Disabling all code optimizations.");
    event_mask |= COR_PRF_DISABLE_OPTIMIZATIONS;
  }

  const WSTRING domain_neutral_instrumentation =
      GetEnvironmentValue(environment::domain_neutral_instrumentation);

  if (domain_neutral_instrumentation == "1"_W || domain_neutral_instrumentation == "true"_W) {
    Info("Detected environment variable ", environment::domain_neutral_instrumentation,
         "=", domain_neutral_instrumentation);
    Info("Enabling automatic instrumentation of methods called from domain-neutral assemblies. ",
         "Please ensure that there is only one AppDomain or, if applications are being hosted in IIS, ",
         "ensure that all Application Pools have at most one application each. ",
         "Otherwise, a sharing violation (HRESULT 0x80131401) may occur.");
    instrument_domain_neutral_assemblies = true;
  }

  // set event mask to subscribe to events and disable NGEN images
  hr = this->info_->SetEventMask(event_mask);
  if (FAILED(hr)) {
    Warn("Failed to attach profiler: unable to set event mask.");
    return E_FAIL;
  }

  runtime_information_ = GetRuntimeInformation(this->info_);

  // we're in!
  Info("Profiler attached.");
  this->info_->AddRef();
  is_attached_ = true;
  profiler = this;
  return S_OK;
}

HRESULT STDMETHODCALLTYPE CorProfiler::OnAssemblyLoaded(_In_ IAssemblyInfo* pAssemblyInfo) {

  AssemblyID assembly_id = 0;
  pAssemblyInfo->GetID(&assembly_id);

  if (!is_attached_) {
    return S_OK;
  }

  if (debug_logging_enabled) {
    Debug("AssemblyLoadFinished: ", assembly_id);
  }

  // keep this lock until we are done using the module,
  // to prevent it from unloading while in use
  std::lock_guard<std::mutex> guard(module_id_to_info_map_lock_);

  const auto assembly_info = GetAssemblyInfo(this->info_, assembly_id);
  if (!assembly_info.IsValid()) {
    return S_OK;
  }

  ComPtr<IUnknown> metadata_interfaces;
  auto hr = this->info_->GetModuleMetaData(assembly_info.manifest_module_id, ofRead | ofWrite,
                                           IID_IMetaDataImport2,
                                           metadata_interfaces.GetAddressOf());

  if (FAILED(hr)) {
    Warn("AssemblyLoadFinished failed to get metadata interface for module id ", assembly_info.manifest_module_id,
         " from assembly ", assembly_info.name);
    return S_OK;
  }

  // Get the IMetaDataAssemblyImport interface to get metadata from the managed assembly
  const auto assembly_import = metadata_interfaces.As<IMetaDataAssemblyImport>(
      IID_IMetaDataAssemblyImport);
  const auto assembly_metadata = GetAssemblyImportMetadata(assembly_import);

  // Configure a version string to compare with the profiler version
  WSTRINGSTREAM ws;
  ws << ToWSTRING(assembly_metadata.version.major)
      << '.'_W
      << ToWSTRING(assembly_metadata.version.minor)
      << '.'_W
      << ToWSTRING(assembly_metadata.version.build);

  if (debug_logging_enabled) {
    Debug("AssemblyLoadFinished: AssemblyName=", assembly_info.name, " AssemblyVersion=", ws.str(), ".", assembly_metadata.version.revision);
  }

  if (assembly_info.name == "Datadog.Trace.ClrProfiler.Managed"_W) {
    // Check that Major.Minor.Build match the profiler version
    if (ws.str() == ToWSTRING(PROFILER_VERSION)) {
      Info("AssemblyLoadFinished: Datadog.Trace.ClrProfiler.Managed v", ws.str(), " matched profiler version v", PROFILER_VERSION);
      managed_profiler_loaded_app_domains.insert(assembly_info.app_domain_id);

      if (runtime_information_.is_desktop() && corlib_module_loaded) {
        // Set the managed_profiler_loaded_domain_neutral flag whenever the managed profiler is loaded shared
        if (assembly_info.app_domain_id == corlib_app_domain_id) {
          Info("AssemblyLoadFinished: Datadog.Trace.ClrProfiler.Managed was loaded domain-neutral");
          managed_profiler_loaded_domain_neutral = true;
        }
        else {
          Info("AssemblyLoadFinished: Datadog.Trace.ClrProfiler.Managed was not loaded domain-neutral");
        }
      }
    }
    else {
      Warn("AssemblyLoadFinished: Datadog.Trace.ClrProfiler.Managed v", ws.str(), " did not match profiler version v", PROFILER_VERSION);
    }
  }

  return S_OK;
}

HRESULT STDMETHODCALLTYPE CorProfiler::OnModuleLoaded(_In_ IModuleInfo* pModuleInfo) {

  HRESULT hr = S_OK;
  ModuleID module_id = 0;
  pModuleInfo->GetModuleID(&module_id);

  if (!is_attached_) {
    return S_OK;
  }

  // keep this lock until we are done using the module,
  // to prevent it from unloading while in use
  std::lock_guard<std::mutex> guard(module_id_to_info_map_lock_);

  const auto module_info = GetModuleInfo(this->info_, module_id);
  if (!module_info.IsValid()) {
    return S_OK;
  }

  if (debug_logging_enabled) {
    Debug("ModuleLoadFinished: ", module_id, " ", module_info.assembly.name,
          " AppDomain ", module_info.assembly.app_domain_id, " ",
          module_info.assembly.app_domain_name);
  }

  AppDomainID app_domain_id = module_info.assembly.app_domain_id;

  // Identify the AppDomain ID of mscorlib which will be the Shared Domain
  // because mscorlib is always a domain-neutral assembly
  if (!corlib_module_loaded &&
      (module_info.assembly.name == "mscorlib"_W ||
       module_info.assembly.name == "System.Private.CoreLib"_W)) {
    corlib_module_loaded = true;
    corlib_app_domain_id = app_domain_id;
    return S_OK;
  }

  // In IIS, the startup hook will be inserted into a method in System.Web (which is domain-neutral)
  // but the Datadog.Trace.ClrProfiler.Managed.Loader assembly that the startup hook loads from a
  // byte array will be loaded into a non-shared AppDomain.
  // In this case, do not insert another startup hook into that non-shared AppDomain
  if (module_info.assembly.name == "Datadog.Trace.ClrProfiler.Managed.Loader"_W) {
    Info("ModuleLoadFinished: Datadog.Trace.ClrProfiler.Managed.Loader loaded into AppDomain ",
          app_domain_id, " ", module_info.assembly.app_domain_name);
    first_jit_compilation_app_domains.insert(app_domain_id);
    return S_OK;
  }

  if (module_info.IsWindowsRuntime()) {
    // We cannot obtain writable metadata interfaces on Windows Runtime modules
    // or instrument their IL.
    Debug("ModuleLoadFinished skipping Windows Metadata module: ", module_id,
          " ", module_info.assembly.name);
    return S_OK;
  }

  if (module_info.assembly.name == "Datadog.Trace.ClrProfiler.Managed"_W) {
    // we need this later to load integration bytecode
    IfFailRet(pModuleInfo->GetImageBase(&managed_profiler_image_base));
  }

  // this should probably be driven by the list of integrations, but hard-coding
  // an example for now
  if (module_info.assembly.name == "System.Net.Http"_W) {
    ComPtr<IModuleInfo4> pModuleInfo4;
    IfFailRet(pModuleInfo->QueryInterface(__uuidof(IModuleInfo4),
                                          (void**)&pModuleInfo4));
    IfFailRet(pModuleInfo4->ImportType(
        managed_profiler_image_base, 0, MappingKind_Image,
        L"Datadog.Trace.ClrProfiler.Integrations.Private.HttpMessageHandler"));
  }


  for (auto&& skip_assembly_pattern : skip_assembly_prefixes) {
    if (module_info.assembly.name.rfind(skip_assembly_pattern, 0) == 0) {
      Debug("ModuleLoadFinished skipping module by pattern: ", module_id, " ",
            module_info.assembly.name);
      return S_OK;
    }
  }

  for (auto&& skip_assembly : skip_assemblies) {
    if (module_info.assembly.name == skip_assembly) {
      Debug("ModuleLoadFinished skipping known module: ", module_id, " ",
            module_info.assembly.name);
      return S_OK;
    }
  }

  std::vector<IntegrationMethod> filtered_integrations =
      FlattenIntegrations(integrations_);

  filtered_integrations =
      FilterIntegrationsByCaller(filtered_integrations, module_info.assembly);
  if (filtered_integrations.empty()) {
    // we don't need to instrument anything in this module, skip it
    Debug("ModuleLoadFinished skipping module (filtered by caller): ",
          module_id, " ", module_info.assembly.name);
    return S_OK;
  }

  ComPtr<IUnknown> metadata_interfaces;
  hr = this->info_->GetModuleMetaData(module_id, ofRead | ofWrite,
                                           IID_IMetaDataImport2,
                                           metadata_interfaces.GetAddressOf());

  if (FAILED(hr)) {
    Warn("ModuleLoadFinished failed to get metadata interface for ", module_id,
         " ", module_info.assembly.name);
    return S_OK;
  }

  const auto metadata_import =
      metadata_interfaces.As<IMetaDataImport2>(IID_IMetaDataImport);
  const auto metadata_emit =
      metadata_interfaces.As<IMetaDataEmit2>(IID_IMetaDataEmit);
  const auto assembly_import = metadata_interfaces.As<IMetaDataAssemblyImport>(
      IID_IMetaDataAssemblyImport);
  const auto assembly_emit =
      metadata_interfaces.As<IMetaDataAssemblyEmit>(IID_IMetaDataAssemblyEmit);

  // don't skip Microsoft.AspNetCore.Hosting so we can run the startup hook and
  // subscribe to DiagnosticSource events
  if (module_info.assembly.name != "Microsoft.AspNetCore.Hosting"_W) {
    filtered_integrations =
        FilterIntegrationsByTarget(filtered_integrations, assembly_import);

    if (filtered_integrations.empty()) {
      // we don't need to instrument anything in this module, skip it
      Debug("ModuleLoadFinished skipping module (filtered by target): ",
            module_id, " ", module_info.assembly.name);
      return S_OK;
    }
  }

  mdModule module;
  hr = metadata_import->GetModuleFromScope(&module);
  if (FAILED(hr)) {
    Warn("ModuleLoadFinished failed to get module metadata token for ",
         module_id, " ", module_info.assembly.name);
    return S_OK;
  }

  GUID module_version_id;
  hr = metadata_import->GetScopeProps(nullptr, 0, nullptr, &module_version_id);
  if (FAILED(hr)) {
    Warn("ModuleLoadFinished failed to get module_version_id for ", module_id,
         " ", module_info.assembly.name);
    return S_OK;
  }

  ModuleMetadata* module_metadata = new ModuleMetadata(
      metadata_import, metadata_emit, assembly_import, assembly_emit,
      module_info.assembly.name, app_domain_id,
      module_version_id, filtered_integrations);

  // store module info for later lookup
  module_id_to_info_map_[module_id] = module_metadata;

  Debug("ModuleLoadFinished stored metadata for ", module_id, " ",
        module_info.assembly.name, " AppDomain ",
        module_info.assembly.app_domain_id, " ",
        module_info.assembly.app_domain_name);
  return S_OK;
}

HRESULT STDMETHODCALLTYPE CorProfiler::OnModuleUnloaded(_In_ IModuleInfo* pModuleInfo) {

  ModuleID module_id = 0;
  pModuleInfo->GetModuleID(&module_id);

  if (debug_logging_enabled) {
    const auto module_info = GetModuleInfo(this->info_, module_id);

    if (module_info.IsValid()) {
      Debug("ModuleUnloadStarted: ", module_id, " ", module_info.assembly.name,
            " AppDomain ", module_info.assembly.app_domain_id, " ",
            module_info.assembly.app_domain_name);
    } else {
      Debug("ModuleUnloadStarted: ", module_id);
    }
  }

  // take this lock so we block until the
  // module metadata is not longer being used
  std::lock_guard<std::mutex> guard(module_id_to_info_map_lock_);

  // remove module metadata from map
  if (module_id_to_info_map_.count(module_id) > 0) {
    ModuleMetadata* metadata = module_id_to_info_map_[module_id];

    // remove appdomain id from managed_profiler_loaded_app_domains set
    if (managed_profiler_loaded_app_domains.find(metadata->app_domain_id) !=
        managed_profiler_loaded_app_domains.end()) {
      managed_profiler_loaded_app_domains.erase(metadata->app_domain_id);
    }

    module_id_to_info_map_.erase(module_id);
    delete metadata;
  }

  return S_OK;
}

HRESULT STDMETHODCALLTYPE CorProfiler::Shutdown() {
  CorProfilerBase::Shutdown();

  // keep this lock until we are done using the module,
  // to prevent it from unloading while in use
  std::lock_guard<std::mutex> guard(module_id_to_info_map_lock_);

  is_attached_ = false;
  return S_OK;
}

HRESULT STDMETHODCALLTYPE CorProfiler::ShouldInstrumentMethod(
    _In_ IMethodInfo* pMethodInfo, _In_ BOOL isRejit,
    _Out_ BOOL* pbInstrument) {

  FunctionID function_id = 0;
  pMethodInfo->GetFunctionId(&function_id);

  // keep this lock until we are done using the module,
  // to prevent it from unloading while in use
  std::lock_guard<std::mutex> guard(module_id_to_info_map_lock_);

  ModuleID module_id;
  mdToken function_token = mdTokenNil;

  HRESULT hr = this->info_->GetFunctionInfo(function_id, nullptr, &module_id,
                                            &function_token);

  if (FAILED(hr)) {
    Warn(
        "JITCompilationStarted: Call to ICorProfilerInfo3.GetFunctionInfo() "
        "failed for ",
        function_id);
    return S_OK;
  }

  // Verify that we have the metadata for this module
  ModuleMetadata* module_metadata = nullptr;
  if (module_id_to_info_map_.count(module_id) > 0) {
    module_metadata = module_id_to_info_map_[module_id];
  }

  if (module_metadata == nullptr) {
    // we haven't stored a ModuleMetadata for this module,
    // so we can't modify its IL
    return S_OK;
  }

  // get function info
  const auto caller = GetFunctionInfo(module_metadata->metadata_import, function_token);
  if (!caller.IsValid()) {
    return S_OK;
  }

  if (debug_logging_enabled) {
    Debug("JITCompilationStarted: function_id=", function_id,
          " token=", function_token, " name=", caller.type.name, ".",
          caller.name, "()");
  }

  // IIS: Ensure that the startup hook is inserted into System.Web.Compilation.BuildManager.InvokePreStartInitMethods.
  // This will be the first call-site considered for the startup hook injection,
  // which correctly loads Datadog.Trace.ClrProfiler.Managed.Loader into the application's
  // own AppDomain because at this point in the code path, the ApplicationImpersonationContext
  // has been started.
  auto valid_startup_hook_callsite = true;
  if (module_metadata->assemblyName == "System"_W ||
     (module_metadata->assemblyName == "System.Web"_W && !(caller.type.name == "System.Web.Compilation.BuildManager"_W && caller.name == "InvokePreStartInitMethods"_W))) {
    valid_startup_hook_callsite = false;
  }

  // The first time a method is JIT compiled in an AppDomain, insert our startup
  // hook which, at a minimum, must add an AssemblyResolve event so we can find
  // Datadog.Trace.ClrProfiler.Managed.dll and its dependencies on-disk since it
  // is no longer provided in a NuGet package
  if (valid_startup_hook_callsite &&
      first_jit_compilation_app_domains.find(module_metadata->app_domain_id) ==
      first_jit_compilation_app_domains.end()) {
    bool domain_neutral_assembly = runtime_information_.is_desktop() && corlib_module_loaded && module_metadata->app_domain_id == corlib_app_domain_id;
    Info("JITCompilationStarted: Startup hook registered in function_id=", function_id,
          " token=", function_token, " name=", caller.type.name, ".",
          caller.name, "(), assembly_name=", module_metadata->assemblyName,
          " app_domain_id=", module_metadata->app_domain_id,
          " domain_neutral=", domain_neutral_assembly);

    first_jit_compilation_app_domains.insert(module_metadata->app_domain_id);
    ComPtr<IModuleInfo> pModuleInfo;
    if (FAILED(pMethodInfo->GetModuleInfo((IModuleInfo**)&pModuleInfo))) {
      Warn("JITCompilationStarted: MethodInfo::GetModuleInfo() failed for ", module_id, " ", function_token);
      return S_OK;
    }
    ComPtr<IModuleInfo4> pModuleInfo4 = pModuleInfo.As<IModuleInfo4>(__uuidof(IModuleInfo4));
    if(pModuleInfo4.IsNull()) {
      Warn("JITCompilationStarted: IModuleInfo QI for IModuleInfo4 failed for ", module_id, " ", function_token);
      return S_OK;
    }

    mdMethodDef startupMethodDef = 0;
    if (FAILED(GenerateVoidILStartupMethod(pModuleInfo4.Get(), &startupMethodDef))) {
      Warn("JITCompilationStarted: Call to GenerateVoidILStartupMethod failed");
      return S_OK;
    }
    
    ComPtr<IDataContainer> pDataContainer;
    if (FAILED(pMethodInfo->QueryInterface<IDataContainer>((IDataContainer**)&pDataContainer))) {
      Warn("JITCompilationStarted: IMethodInfo QI for IDataContainer failed");
      return S_OK;
    }

    InstrumentationDataItem* pDataItem = new InstrumentationDataItem();
    pDataItem->m_startupMethod = startupMethodDef;
    if (FAILED(pDataContainer->SetDataItem(&CLSID_CorProfiler, &__uuidof(pDataItem), (IUnknown*)pDataItem))) {
      Warn("JITCompilationStarted: IDataContainer SetDataItem failed");
      delete pDataItem;
      return S_OK;
    }

    *pbInstrument = TRUE;
    return S_OK;
  }

  // we don't actually need to instrument anything in
  // Microsoft.AspNetCore.Hosting, it was included only to ensure the startup
  // hook is called for AspNetCore applications
  if (module_metadata->assemblyName == "Microsoft.AspNetCore.Hosting"_W) {
    return S_OK;
  }

  // Get valid method replacements for this caller method
  const auto method_replacements =
      module_metadata->GetMethodReplacementsForCaller(caller);
  if (method_replacements.empty()) {
    return S_OK;
  }

  /* Pretty sure this is dead code, but if not it would need to be converted
  // Perform method insertion calls
  hr = ProcessInsertionCalls(module_metadata,
                             function_id,
                             module_id,
                             function_token,
                             caller,
                             method_replacements);

  if (FAILED(hr)) {
    Warn("JITCompilationStarted: Call to ProcessInsertionCalls() failed for ", function_id, " ", module_id, " ", function_token);
    return S_OK;
  }
  */

  // Perform method replacement calls
  hr = ProcessReplacementCalls(module_metadata,
                               function_id,
                               module_id,
                               function_token,
                               caller,
                               method_replacements,
                               pMethodInfo,
                               pbInstrument);
  if (FAILED(hr)) {
    Warn("JITCompilationStarted: Call to ProcessReplacementCalls() failed for ", function_id, " ", module_id, " ", function_token);
    return S_OK;
  }
  if (*pbInstrument) {
    ComPtr<IDataContainer> pDataContainer;
    IfFailRet(pMethodInfo->QueryInterface<IDataContainer>((IDataContainer**)&pDataContainer));
    InstrumentationDataItem* pDataItem = new InstrumentationDataItem();
    pDataItem->m_startupMethod = 0;
    IfFailRet(pDataContainer->SetDataItem(&CLSID_CorProfiler, &__uuidof(pDataItem), (IUnknown*)pDataItem));
  }

  return S_OK;
}

HRESULT CorProfiler::InstrumentMethod(
    IMethodInfo* pMethodInfo,
    BOOL isRejit) {

    HRESULT hr = S_OK;
  ComPtr<IDataContainer> pDataContainer;
    IfFailRet(pMethodInfo->QueryInterface<IDataContainer>(
        (IDataContainer**)&pDataContainer));
  ComPtr<IUnknown> pDataItemUnk;
  IfFailRet(pDataContainer->GetDataItem(&CLSID_CorProfiler,
                                        &__uuidof(InstrumentationDataItem),
                                        (IUnknown**)&pDataItemUnk));

  InstrumentationDataItem* pDataItem =
      static_cast<InstrumentationDataItem*>(pDataItemUnk.Get());
  if (pDataItem->m_startupMethod == 0) {
    FunctionID function_id = 0;
    pMethodInfo->GetFunctionId(&function_id);
    std::lock_guard<std::mutex> guard(module_id_to_info_map_lock_);
    ModuleID module_id;
    mdToken function_token = mdTokenNil;
    this->info_->GetFunctionInfo(function_id, nullptr, &module_id, &function_token);
    ModuleMetadata* module_metadata = module_id_to_info_map_[module_id];
    const auto caller = GetFunctionInfo(module_metadata->metadata_import, function_token);
    const auto method_replacements = module_metadata->GetMethodReplacementsForCaller(caller);
    IfFailRet(ProcessReplacementCalls(module_metadata, function_id, module_id,
                                         function_token, caller, method_replacements,
                                         pMethodInfo, nullptr));
  } else {
    // startup instrumentation
    ComPtr<IInstructionGraph> pInstructionGraph;
    pMethodInfo->GetInstructions((IInstructionGraph**)&pInstructionGraph);
    ComPtr<IInstructionFactory> pInstructionFactory;
    pMethodInfo->GetInstructionFactory(
        (IInstructionFactory**)&pInstructionFactory);

    ComPtr<IInstruction> pFirstInstruction;
    pInstructionGraph->GetFirstInstruction((IInstruction**)&pFirstInstruction);
    ComPtr<IInstruction> pCallStartupMethodInstruction;
    pInstructionFactory->CreateTokenOperandInstruction(
        ILOrdinalOpcode::Cee_Call, pDataItem->m_startupMethod,
        (IInstruction**)&pCallStartupMethodInstruction);
    pInstructionGraph->InsertBefore(pFirstInstruction.Get(),
                                    pCallStartupMethodInstruction.Get());
  }


  return S_OK;
}

bool CorProfiler::IsAttached() const { return is_attached_; }

//
// Helper methods
//
HRESULT CorProfiler::ProcessReplacementCalls(
    ModuleMetadata* module_metadata,
    const FunctionID function_id,
    const ModuleID module_id,
    const mdToken function_token,
    const trace::FunctionInfo& caller,
    const std::vector<MethodReplacement> method_replacements,
    IMethodInfo* pMethodInfo,
    BOOL* bShouldInstrument) {

  bool modified = false;
  ComPtr<IInstructionGraph> pInstructionGraph;
  HRESULT hr = pMethodInfo->GetInstructions((IInstructionGraph**)&pInstructionGraph);

  if (FAILED(hr)) {
    Warn("ProcessReplacementCalls: Call to ILRewriter.Import() failed for ", module_id, " ", function_token);
    return hr;
  }

  // Perform method call replacements
  for (auto& method_replacement : method_replacements) {
    // Exit early if the method replacement isn't actually doing a replacement
    if (method_replacement.wrapper_method.action != "ReplaceTargetMethod"_W) {
      continue;
    }

    const auto& wrapper_method_key =
        method_replacement.wrapper_method.get_method_cache_key();
    // Exit early if we previously failed to store the method ref for this wrapper_method
    if (module_metadata->IsFailedWrapperMemberKey(wrapper_method_key)) {
      continue;
    }

    // for each IL instruction
    ComPtr<IInstruction> pInstr;
    pInstructionGraph->GetFirstInstruction((IInstruction**)&pInstr);
    do {

      // only CALL or CALLVIRT
      ILOrdinalOpcode opcode;
      IfFailRet(pInstr->GetOpCode(&opcode));
      if (opcode != CEE_CALL && opcode != CEE_CALLVIRT) {
        continue;
      }

      // get the target function info, continue if its invalid
      ComPtr<IOperandInstruction> pOpInstr =
          pInstr.As<IOperandInstruction>(__uuidof(IOperandInstruction));
      mdToken callTargetTok = 0;
      IfFailRet(pOpInstr->GetOperandValue(4, (BYTE*)&callTargetTok));
      auto target =
          GetFunctionInfo(module_metadata->metadata_import, callTargetTok);
      if (!target.IsValid()) {
        continue;
      }

      // make sure the type and method names match
      if (method_replacement.target_method.type_name != target.type.name ||
          method_replacement.target_method.method_name != target.name) {
        continue;
      }

      // we add 3 parameters to every wrapper method: opcode, mdToken, and
      // module_version_id
      const short added_parameters_count = 3;

      auto wrapper_method_signature_size =
          method_replacement.wrapper_method.method_signature.data.size();

      if (wrapper_method_signature_size < (added_parameters_count + 3)) {
        // wrapper signature must have at least 6 bytes
        // 0:{CallingConvention}|1:{ParamCount}|2:{ReturnType}|3:{OpCode}|4:{mdToken}|5:{ModuleVersionId}
        if (debug_logging_enabled) {
          Debug(
              "JITCompilationStarted skipping function call: wrapper signature "
              "too short. function_id=",
              function_id, " token=", function_token,
              " wrapper_method=", method_replacement.wrapper_method.type_name,
              ".", method_replacement.wrapper_method.method_name,
              "() wrapper_method_signature_size=",
              wrapper_method_signature_size);
        }

        continue;
      }

      auto expected_number_args = method_replacement.wrapper_method
                                      .method_signature.NumberOfArguments();

      // subtract the last arguments we add to every wrapper
      expected_number_args = expected_number_args - added_parameters_count;

      if (target.signature.IsInstanceMethod()) {
        // We always pass the instance as the first argument
        expected_number_args--;
      }

      auto target_arg_count = target.signature.NumberOfArguments();

      if (expected_number_args != target_arg_count) {
        // Number of arguments does not match our wrapper method
        if (debug_logging_enabled) {
          Debug(
              "JITCompilationStarted skipping function call: argument counts "
              "don't match. function_id=",
              function_id, " token=", function_token,
              " target_name=", target.type.name, ".", target.name,
              "() expected_number_args=", expected_number_args,
              " target_arg_count=", target_arg_count);
        }

        continue;
      }

      // Resolve the MethodRef now. If the method is generic, we'll need to use it
      // to define a MethodSpec
      // Generate a method ref token for the wrapper method
      mdMemberRef wrapper_method_ref = mdMemberRefNil;
      auto generated_wrapper_method_ref = GetWrapperMethodRef(module_metadata,
                                                              module_id,
                                                              method_replacement,
                                                              wrapper_method_ref);
      if (!generated_wrapper_method_ref) {
        Warn(
          "JITCompilationStarted failed to obtain wrapper method ref for ",
          method_replacement.wrapper_method.type_name, ".", method_replacement.wrapper_method.method_name, "().",
             " function_id=", function_id, " function_token=", function_token,
             " name=", caller.type.name, ".", caller.name, "()");
        continue;
      }

      auto method_def_md_token = target.id;

      if (target.is_generic) {
        if (target.signature.NumberOfTypeArguments() !=
            method_replacement.wrapper_method.method_signature
                .NumberOfTypeArguments()) {
          // Number of generic arguments does not match our wrapper method
          continue;
        }

        // we need to emit a method spec to populate the generic arguments
        wrapper_method_ref =
            DefineMethodSpec(module_metadata->metadata_emit, wrapper_method_ref,
                             target.function_spec_signature);
        method_def_md_token = target.method_def_id;
      }

      std::vector<WSTRING> actual_sig;
      const auto successfully_parsed_signature = TryParseSignatureTypes(
          module_metadata->metadata_import, target, actual_sig);
      auto expected_sig =
          method_replacement.target_method.signature_types;

      if (!successfully_parsed_signature) {
        if (debug_logging_enabled) {
          Debug(
              "JITCompilationStarted skipping function call: failed to parse "
              "signature. function_id=",
              function_id, " token=", function_token,
              " target_name=", target.type.name, ".", target.name, "()",
              " successfully_parsed_signature=", successfully_parsed_signature,
              " sig_types.size()=", actual_sig.size(),
              " expected_sig_types.size()=", expected_sig.size());
        }

        continue;
      }

      if (actual_sig.size() != expected_sig.size()) {
        // we can't safely assume our wrapper methods handle the types
        if (debug_logging_enabled) {
          Debug(
              "JITCompilationStarted skipping function call: unexpected type "
              "count. function_id=",
              function_id, " token=", function_token,
              " target_name=", target.type.name, ".", target.name,
              "() successfully_parsed_signature=",
              successfully_parsed_signature,
              " sig_types.size()=", actual_sig.size(),
              " expected_sig_types.size()=", expected_sig.size());
        }

        continue;
      }

      auto is_match = true;
      for (size_t i = 0; i < expected_sig.size(); i++) {
        if (expected_sig[i] == "_"_W) {
          // We are supposed to ignore this index
          continue;
        }
        if (expected_sig[i] != actual_sig[i]) {
          // we have a type mismatch, drop out
          if (debug_logging_enabled) {
            Debug(
                "JITCompilationStarted skipping function call: types don't "
                "match. function_id=",
                function_id, " token=", function_token,
                " target_name=", target.type.name, ".", target.name,
                "() actual[", i, "]=", actual_sig[i], ", expected[",
                i, "]=", expected_sig[i]);
          }

          is_match = false;
          break;
        }
      }

      if (!is_match) {
        // signatures don't match
        continue;
      }

      // At this point we know we've hit a match. Error out if
      //   1) The target assembly is Datadog.Trace.ClrProfiler.Managed
      //   2) The managed profiler has not been loaded yet
      if (!ProfilerAssemblyIsLoadedIntoAppDomain(module_metadata->app_domain_id) &&
          method_replacement.wrapper_method.assembly.name == "Datadog.Trace.ClrProfiler.Managed"_W) {
        Warn(
            "JITCompilationStarted skipping method: Method replacement "
            "found but the managed profiler has not yet been loaded "
            "into AppDomain with id=", module_metadata->app_domain_id,
            " function_id=", function_id, " token=", function_token,
            " caller_name=", caller.type.name, ".", caller.name, "()",
            " target_name=", target.type.name, ".", target.name, "()");
        continue;
      }

      // At this point we know we've hit a match. Error out if
      //   1) The calling assembly is domain-neutral
      //   2) The profiler is not configured to instrument domain-neutral assemblies
      //   3) The target assembly is Datadog.Trace.ClrProfiler.Managed
      if (runtime_information_.is_desktop() && corlib_module_loaded &&
          module_metadata->app_domain_id == corlib_app_domain_id &&
          !instrument_domain_neutral_assemblies &&
          method_replacement.wrapper_method.assembly.name == "Datadog.Trace.ClrProfiler.Managed"_W) {
        Warn(
            "JITCompilationStarted skipping method: Method replacement",
             " found but the calling assembly ", module_metadata->assemblyName,
            " has been loaded domain-neutral so its code is being shared across AppDomains,"
             " making it unsafe for automatic instrumentation.",
             " function_id=", function_id, " token=", function_token,
             " caller_name=", caller.type.name, ".", caller.name, "()",
             " target_name=", target.type.name, ".", target.name, "()");
        continue;
      }

      if (bShouldInstrument != nullptr) {
        *bShouldInstrument = true;
        return S_OK;
      }

      const auto original_argument = callTargetTok;
      const void* module_version_id_ptr = &module_metadata->module_version_id;

      // Begin IL Modification
      ComPtr<IInstructionFactory> pInstructionFactory;
      IfFailRet(pMethodInfo->GetInstructionFactory((IInstructionFactory**)&pInstructionFactory));

      // No longer needed
      // CLRIE can update the offsets without needing to insert dummy NOP
      // IL Modification #1: Replace original method call with a NOP, so that all original
      //                     jump targets resolve correctly and we correctly populate the
      //                     stack with additional arguments
      //
      // IMPORTANT: Conditional branches may jump to the original call instruction which
      // resulted in the InvalidProgramException seen in
      // https://github.com/DataDog/dd-trace-dotnet/pull/542. To avoid this, we'll do
      // the rest of our IL modifications AFTER this instruction.
      auto original_methodcall_opcode = opcode;

      // IL Modification #2: Conditionally box System.Threading.CancellationToken
      //                     if it is the last argument in the target method.
      //
      // If the last argument in the method signature is of the type
      // System.Threading.CancellationToken (a struct) then box it before calling our
      // integration method. This resolves https://github.com/DataDog/dd-trace-dotnet/issues/662,
      // in which we did not box the System.Threading.CancellationToken object, even though the
      // wrapper method expects an object. In that issue we observed some strange CLR behavior
      // when the target method was in System.Data and the environment was 32-bit .NET Framework:
      // the CLR swapped the values of the CancellationToken argument and the opCode argument.
      // For example, the VIRTCALL opCode is '0x6F' and this value would be placed at the memory
      // location assigned to the CancellationToken variable. Since we treat the CancellationToken
      // variable as an object, this '0x6F' would be dereference to access the underlying object,
      // and an invalid memory read would occur and crash the application.
      //
      // Currently, all integrations that use System.Threading.CancellationToken (a struct)
      // have the argument as the last argument in the signature (lucky us!).
      // For now, we'll do the following:
      //   1) Get the method signature of the original target method
      //   2) Read the signature until the final argument type
      //   3) If the type begins with `ELEMENT_TYPE_VALUETYPE`, uncompress the compressed type token that follows
      //   4) If the type token represents System.Threading.CancellationToken, emit a 'box <type_token>' IL instruction before calling our wrapper method
      auto original_method_def = target.id;
      size_t argument_count = target.signature.NumberOfArguments();
      size_t return_type_index = target.signature.IndexOfReturnType();
      PCCOR_SIGNATURE pSigCurrent = PCCOR_SIGNATURE(&target.signature.data[return_type_index]); // index to the location of the return type
      bool signature_read_success = true;

      // iterate until the pointer is pointing at the last argument
      for (size_t signature_types_index = 0; signature_types_index < argument_count; signature_types_index++) {
        if (!ParseType(&pSigCurrent)) {
          signature_read_success = false;
          break;
        }
      }

      // read the last argument type
      if (signature_read_success && *pSigCurrent == ELEMENT_TYPE_VALUETYPE) {
        pSigCurrent++;
        mdToken valuetype_type_token = CorSigUncompressToken(pSigCurrent);

        // Currently, we only expect to see `System.Threading.CancellationToken` as a valuetype in this position
        // If we expand this to a general case, we would always perform the boxing regardless of type
        if (GetTypeInfo(module_metadata->metadata_import, valuetype_type_token).name == "System.Threading.CancellationToken"_W) {
          ComPtr<IInstruction> boxInstr;
          IfFailRet(pInstructionFactory->CreateTokenOperandInstruction(
              ILOrdinalOpcode::Cee_Box, valuetype_type_token, (IInstruction**)&boxInstr));
          IfFailRet(pInstructionGraph->InsertBeforeAndRetargetOffsets(pInstr.Get(), boxInstr.Get()));
        }
      }

      // IL Modification #3: Insert a non-virtual call (CALL) to the instrumentation wrapper.
      //                     Always use CALL because the wrapper methods are all static.
      ComPtr<IInstruction> callInstr;
      IfFailRet(pInstructionFactory->CreateTokenOperandInstruction(
          ILOrdinalOpcode::Cee_Call, wrapper_method_ref,
          (IInstruction**)&callInstr));
      IfFailRet(pInstructionGraph->Replace(pInstr.Get(), callInstr.Get()));
      pInstr = callInstr;

      // IL Modification #4: Push the following additional arguments on the evaluation stack in the
      //                     following order, which all integration wrapper methods expect:
      //                       1) [int32] original CALL/CALLVIRT opCode
      //                       2) [int32] mdToken for original method call target
      //                       3) [int64] pointer to MVID
      ComPtr<IInstruction> loadInstr;
      IfFailRet(pInstructionFactory->CreateIntOperandInstruction(
          ILOrdinalOpcode::Cee_Ldc_I4, original_methodcall_opcode,
          (IInstruction**)&loadInstr));
      IfFailRet(pInstructionGraph->InsertBeforeAndRetargetOffsets(pInstr.Get(), loadInstr.Get()));
      IfFailRet(pInstructionFactory->CreateIntOperandInstruction(
          ILOrdinalOpcode::Cee_Ldc_I4, method_def_md_token,
          (IInstruction**)&loadInstr));
      IfFailRet(pInstructionGraph->InsertBeforeAndRetargetOffsets(pInstr.Get(), loadInstr.Get()));
      IfFailRet(pInstructionFactory->CreateLongOperandInstruction(
          ILOrdinalOpcode::Cee_Ldc_I8, reinterpret_cast<INT64>(module_version_id_ptr),
          (IInstruction**)&loadInstr));
      IfFailRet(pInstructionGraph->InsertBeforeAndRetargetOffsets(pInstr.Get(), loadInstr.Get()));

      // IL Modification #5: Conditionally emit an unbox.any instruction on the return value
      //                     of the wrapper method if we return an object but the original
      //                     method call returned a valuetype or a generic type.
      //
      // This resolves https://github.com/DataDog/dd-trace-dotnet/pull/566, which raised a
      // System.EntryPointNotFoundException. This occurred because the return type of the
      // generic method was a generic type that evaluated to a value type at runtime. As a
      // result, this caller method expected an unboxed representation of the return value,
      // even though we can only return values of type object. So if we detect that the
      // expected return type is a valuetype or a generic type, issue an unbox.any
      // instruction that will unbox it.
      mdToken typeToken;
      if (method_replacement.wrapper_method.method_signature.ReturnTypeIsObject()
          && ReturnTypeIsValueTypeOrGeneric(module_metadata->metadata_import,
                              module_metadata->metadata_emit,
                              module_metadata->assembly_emit,
                              target.id,
                              target.signature,
                              &typeToken)) {
        if (debug_logging_enabled) {
          Debug(
              "JITCompilationStarted inserting 'unbox.any ", typeToken,
              "' instruction after calling target function."
              " function_id=", function_id,
              " token=", function_token,
              " target_name=", target.type.name, ".", target.name,"()");
        }
        ComPtr<IInstruction> pUnboxInstr;
        IfFailRet(pInstructionFactory->CreateTokenOperandInstruction(
            ILOrdinalOpcode::Cee_Ldc_I8, typeToken,
            (IInstruction**)&pUnboxInstr));
        IfFailRet(pInstructionGraph->InsertAfter(
            pInstr.Get(), pUnboxInstr.Get()));
        pInstr = pUnboxInstr;
        //rewriter_wrapper.UnboxAnyAfter(typeToken);
      }

      // End IL Modification
      modified = true;
      Info("*** JITCompilationStarted() replaced calls from ", caller.type.name,
           ".", caller.name, "() to ",
           method_replacement.target_method.type_name, ".",
           method_replacement.target_method.method_name, "() ",
           original_argument, " with calls to ",
           method_replacement.wrapper_method.type_name, ".",
           method_replacement.wrapper_method.method_name, "() ",
           wrapper_method_ref);
    } while (SUCCEEDED(pInstr->GetNextInstruction((IInstruction**)&pInstr)));
  }

  return S_OK;
}

HRESULT CorProfiler::ProcessInsertionCalls(
    ModuleMetadata* module_metadata,
    const FunctionID function_id,
    const ModuleID module_id,
    const mdToken function_token,
    const FunctionInfo& caller,
    const std::vector<MethodReplacement> method_replacements) {

  ILRewriter rewriter(this->info_, nullptr, module_id, function_token);
  bool modified = false;

  auto hr = rewriter.Import();

  if (FAILED(hr)) {
    Warn("ProcessInsertionCalls: Call to ILRewriter.Import() failed for ", module_id, " ", function_token);
    return hr;
  }

  ILRewriterWrapper rewriter_wrapper(&rewriter);
  ILInstr* firstInstr = rewriter.GetILList()->m_pNext;
  ILInstr* lastInstr = rewriter.GetILList()->m_pPrev; // Should be a 'ret' instruction

  for (auto& method_replacement : method_replacements) {
    if (method_replacement.wrapper_method.action == "ReplaceTargetMethod"_W) {
      continue;
    }

    const auto& wrapper_method_key =
        method_replacement.wrapper_method.get_method_cache_key();

    // Exit early if we previously failed to store the method ref for this wrapper_method
    if (module_metadata->IsFailedWrapperMemberKey(wrapper_method_key)) {
      continue;
    }

    // Generate a method ref token for the wrapper method
    mdMemberRef wrapper_method_ref = mdMemberRefNil;
    auto generated_wrapper_method_ref = GetWrapperMethodRef(module_metadata,
                                                            module_id,
                                                            method_replacement,
                                                            wrapper_method_ref);
    if (!generated_wrapper_method_ref) {
      Warn(
        "JITCompilationStarted failed to obtain wrapper method ref for ",
        method_replacement.wrapper_method.type_name, ".", method_replacement.wrapper_method.method_name, "().",
        " function_id=", function_id, " function_token=", function_token,
        " name=", caller.type.name, ".", caller.name, "()");
      continue;
    }

    // After successfully getting the method reference, insert a call to it
    if (method_replacement.wrapper_method.action == "InsertFirst"_W) {
      // Get first instruction and set the rewriter to that location
      rewriter_wrapper.SetILPosition(firstInstr);
      rewriter_wrapper.CallMember(wrapper_method_ref, false);
      firstInstr = firstInstr->m_pPrev;
      modified = true;

      Info("*** JITCompilationStarted() : InsertFirst inserted call to ",
        method_replacement.wrapper_method.type_name, ".",
        method_replacement.wrapper_method.method_name, "() ", wrapper_method_ref,
        " to the beginning of method",
        caller.type.name,".", caller.name, "()");
    }
  }

  if (modified) {
    hr = rewriter.Export();

    if (FAILED(hr)) {
      Warn("ProcessInsertionCalls: Call to ILRewriter.Export() failed for ModuleID=", module_id, " ", function_token);
      return hr;
    }
  }

  return S_OK;
}

bool CorProfiler::GetWrapperMethodRef(
    ModuleMetadata* module_metadata,
    ModuleID module_id,
    const MethodReplacement& method_replacement,
    mdMemberRef& wrapper_method_ref) {
  const auto& wrapper_method_key =
      method_replacement.wrapper_method.get_method_cache_key();

  // Resolve the MethodRef now. If the method is generic, we'll need to use it
  // later to define a MethodSpec
  if (!module_metadata->TryGetWrapperMemberRef(wrapper_method_key,
                                                   wrapper_method_ref)) {
    const auto module_info = GetModuleInfo(this->info_, module_id);
    if (!module_info.IsValid()) {
      return false;
    }

    mdModule module;
    auto hr = module_metadata->metadata_import->GetModuleFromScope(&module);
    if (FAILED(hr)) {
      Warn(
          "JITCompilationStarted failed to get module metadata token for "
          "module_id=", module_id, " module_name=", module_info.assembly.name);
      return false;
    }

    const MetadataBuilder metadata_builder(
        *module_metadata, module, module_metadata->metadata_import,
        module_metadata->metadata_emit, module_metadata->assembly_import,
        module_metadata->assembly_emit);

    // for each wrapper assembly, emit an assembly reference
    hr = metadata_builder.EmitAssemblyRef(
        method_replacement.wrapper_method.assembly);
    if (FAILED(hr)) {
      Warn(
          "JITCompilationStarted failed to emit wrapper assembly ref for assembly=",
          method_replacement.wrapper_method.assembly.name,
          ", Version=", method_replacement.wrapper_method.assembly.version.str(),
          ", Culture=", method_replacement.wrapper_method.assembly.locale,
          " PublicKeyToken=", method_replacement.wrapper_method.assembly.public_key.str());
      return false;
    }

    // for each method replacement in each enabled integration,
    // emit a reference to the instrumentation wrapper methods
    hr = metadata_builder.StoreWrapperMethodRef(method_replacement);
    if (FAILED(hr)) {
      Warn(
        "JITCompilationStarted failed to obtain wrapper method ref for ",
        method_replacement.wrapper_method.type_name, ".", method_replacement.wrapper_method.method_name, "().");
      return false;
    } else {
      module_metadata->TryGetWrapperMemberRef(wrapper_method_key,
                                              wrapper_method_ref);
    }
  }

  return true;
}

bool CorProfiler::ProfilerAssemblyIsLoadedIntoAppDomain(AppDomainID app_domain_id) {
  return managed_profiler_loaded_domain_neutral ||
         managed_profiler_loaded_app_domains.find(app_domain_id) !=
             managed_profiler_loaded_app_domains.end();
}

//
// Startup methods
//
HRESULT CorProfiler::GenerateVoidILStartupMethod(
    IModuleInfo4* pModuleInfo, mdMethodDef* ret_method_token) {
  HRESULT hr = S_OK;
  BYTE* pAssemblyArray = nullptr;
  int assemblySize = 0;
  BYTE* pSymbolsArray = nullptr;
  int symbolsSize = 0;
  GetAssemblyAndSymbolsBytes(&pAssemblyArray, &assemblySize, &pSymbolsArray,
                             &symbolsSize);
  
  const WCHAR* typeName = L"Datadog.Trace.ClrProfiler.Managed.Loader.DDVoidMethodType";
  if (FAILED(hr = pModuleInfo->ImportType((LPCBYTE)pAssemblyArray, assemblySize, MappingKind_Flat, typeName))) {
    Warn("GenerateVoidILStartupMethod: Call to IModuleInfo4::ImportType failed");
    return hr;
  }

  ComPtr<IMetaDataImport> pImport;
  if (FAILED(hr = pModuleInfo->GetMetaDataImport((IUnknown**)&pImport))) {
    Warn("GenerateVoidILStartupMethod: GetMetaDataImport failed");
    return hr;
  }

  mdTypeDef importedTypeDefTok = 0;
  if (FAILED(hr = pImport->FindTypeDefByName(typeName, mdTypeDefNil, &importedTypeDefTok))) {
    Warn("GenerateVoidILStartupMethod: IMetadataImport::FindTypeDefByName() failed to find DDVoidMethodType");
    return hr;
  }

  const WCHAR* methodName = L"DDVoidMethodCall";
  BYTE methodSig[] = {
      IMAGE_CEE_CS_CALLCONV_DEFAULT,  // Calling convention
      0,                              // Number of parameters
      ELEMENT_TYPE_VOID               // Return type
  };
  if (FAILED(hr = pImport->FindMethod(importedTypeDefTok, methodName,
                                      methodSig, sizeof(methodSig), ret_method_token))) {
    Warn("GenerateVoidILStartupMethod: IMetadataImport::FindMethod() failed to find DDVoidMethodCall");
    return hr;
  }

  return S_OK;
}

#ifndef _WIN32
extern uint8_t dll_start[] asm("_binary_Datadog_Trace_ClrProfiler_Managed_Loader_dll_start");
extern uint8_t dll_end[] asm("_binary_Datadog_Trace_ClrProfiler_Managed_Loader_dll_end");

extern uint8_t pdb_start[] asm("_binary_Datadog_Trace_ClrProfiler_Managed_Loader_pdb_start");
extern uint8_t pdb_end[] asm("_binary_Datadog_Trace_ClrProfiler_Managed_Loader_pdb_end");
#endif

void CorProfiler::GetAssemblyAndSymbolsBytes(BYTE** pAssemblyArray, int* assemblySize, BYTE** pSymbolsArray, int* symbolsSize) const {
#ifdef _WIN32
  HINSTANCE hInstance = DllHandle;
  LPCWSTR dllLpName;
  LPCWSTR symbolsLpName;

  if (runtime_information_.is_desktop()) {
    dllLpName = MAKEINTRESOURCE(NET45_MANAGED_ENTRYPOINT_DLL);
    symbolsLpName = MAKEINTRESOURCE(NET45_MANAGED_ENTRYPOINT_SYMBOLS);
  } else {
    dllLpName = MAKEINTRESOURCE(NETCOREAPP20_MANAGED_ENTRYPOINT_DLL);
    symbolsLpName = MAKEINTRESOURCE(NETCOREAPP20_MANAGED_ENTRYPOINT_SYMBOLS);
  }

  HRSRC hResAssemblyInfo = FindResource(hInstance, dllLpName, L"ASSEMBLY");
  HGLOBAL hResAssembly = LoadResource(hInstance, hResAssemblyInfo);
  *assemblySize = SizeofResource(hInstance, hResAssemblyInfo);
  *pAssemblyArray = (LPBYTE)LockResource(hResAssembly);

  HRSRC hResSymbolsInfo = FindResource(hInstance, symbolsLpName, L"SYMBOLS");
  HGLOBAL hResSymbols = LoadResource(hInstance, hResSymbolsInfo);
  *symbolsSize = SizeofResource(hInstance, hResSymbolsInfo);
  *pSymbolsArray = (LPBYTE)LockResource(hResSymbols);
#else
  *assemblySize = dll_end - dll_start;
  *pAssemblyArray = (BYTE*)dll_start;

  *symbolsSize = pdb_end - pdb_start;
  *pSymbolsArray = (BYTE*)pdb_start;
#endif
  return;
}
}  // namespace trace
