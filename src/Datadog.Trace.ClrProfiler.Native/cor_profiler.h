#ifndef DD_CLR_PROFILER_COR_PROFILER_H_
#define DD_CLR_PROFILER_COR_PROFILER_H_

#include <atomic>
#include <mutex>
#include <string>
#include <unordered_map>
#include "cor.h"
#include "corprof.h"

#include "cor_profiler_base.h"
#include "environment_variables.h"
#include "integration.h"
#include "module_metadata.h"
#include "pal.h"

namespace trace {

class CorProfiler : public CorProfilerBase {
 private:
  bool is_attached_ = false;
  RuntimeInformation runtime_information_;
  std::vector<Integration> integrations_;

  // Startup helper variables
  bool first_jit_compilation_completed = false;

  bool instrument_domain_neutral_assemblies = false;
  bool corlib_module_loaded = false;
  AppDomainID corlib_app_domain_id;
  bool managed_profiler_loaded_domain_neutral = false;
  LPCBYTE managed_profiler_image_base = nullptr;
  std::unordered_set<AppDomainID> managed_profiler_loaded_app_domains;
  std::unordered_set<AppDomainID> first_jit_compilation_app_domains;
  bool in_azure_app_services = false;

  //
  // Module helper variables
  //
  std::mutex module_id_to_info_map_lock_;
  std::unordered_map<ModuleID, ModuleMetadata*> module_id_to_info_map_;

  //
  // Helper methods
  //
  bool GetWrapperMethodRef(ModuleMetadata* module_metadata,
                           ModuleID module_id,
                           const MethodReplacement& method_replacement,
                           mdMemberRef& wrapper_method_ref);
  HRESULT ProcessReplacementCalls(ModuleMetadata* module_metadata,
                                         const FunctionID function_id,
                                         const ModuleID module_id,
                                         const mdToken function_token,
                                         const FunctionInfo& caller,
                                         const std::vector<MethodReplacement> method_replacements,
                                         IMethodInfo* pMethodInfo,
                                         BOOL* bShouldInstrument);
  HRESULT ProcessInsertionCalls(ModuleMetadata* module_metadata,
                                         const FunctionID function_id,
                                         const ModuleID module_id,
                                         const mdToken function_token,
                                         const FunctionInfo& caller,
                                         const std::vector<MethodReplacement> method_replacements);
  bool ProfilerAssemblyIsLoadedIntoAppDomain(AppDomainID app_domain_id);

  //
  // Startup methods
  //
  HRESULT RunILStartupHook(const ComPtr<IMetaDataEmit2>&,
                             const ModuleID module_id, 
                             IModuleInfo4* pModuleInfo,
                             const mdToken function_token);
  HRESULT GenerateVoidILStartupMethod(IModuleInfo4* pModuleInfo,
                           mdMethodDef* ret_method_token);

 public:
  CorProfiler() = default;

  bool IsAttached() const;

  void GetAssemblyAndSymbolsBytes(BYTE** pAssemblyArray, int* assemblySize,
                                 BYTE** pSymbolsArray, int* symbolsSize) const;

  //
  // ICorProfilerCallback methods
  //

  HRESULT STDMETHODCALLTYPE Shutdown() override;

  //
  // IInstrumentationMethod methods
  //
  HRESULT STDMETHODCALLTYPE
  Initialize(_In_ IProfilerManager* pProfilerManager) override;

  HRESULT STDMETHODCALLTYPE
  OnModuleLoaded(_In_ IModuleInfo* pModuleInfo) override;

  HRESULT STDMETHODCALLTYPE
  OnModuleUnloaded(_In_ IModuleInfo* pModuleInfo) override;

  HRESULT STDMETHODCALLTYPE
  OnAssemblyLoaded(_In_ IAssemblyInfo* pAssemblyInfo) override;

  HRESULT STDMETHODCALLTYPE
  ShouldInstrumentMethod(_In_ IMethodInfo* pMethodInfo, _In_ BOOL isRejit,
                         _Out_ BOOL* pbInstrument) override;

  HRESULT STDMETHODCALLTYPE 
  InstrumentMethod(_In_ IMethodInfo* pMethodInfo, _In_ BOOL isRejit) override;
};

// Note: Generally you should not have a single, global callback implementation,
// as that prevents your profiler from analyzing multiply loaded in-process
// side-by-side CLRs. However, this profiler implements the "profile-first"
// alternative of dealing with multiple in-process side-by-side CLR instances.
// First CLR to try to load us into this process wins; so there can only be one
// callback implementation created. (See ProfilerCallback::CreateObject.)
extern CorProfiler* profiler;  // global reference to callback object

}  // namespace trace

#endif  // DD_CLR_PROFILER_COR_PROFILER_H_
