﻿#ifndef DD_CLR_PROFILER_COR_PROFILER_BASE_H_
#define DD_CLR_PROFILER_COR_PROFILER_BASE_H_

#include <corhlpr.h>
#include <corprof.h>
#include <atomic>
#include "InstrumentationEngine.h"

namespace trace {

class CorProfilerBase : public ICorProfilerCallback8, IInstrumentationMethod {
 private:
  std::atomic<int> ref_count_;

 protected:
  ICorProfilerInfo3* info_;

 public:
  CorProfilerBase();
  virtual ~CorProfilerBase();

  HRESULT STDMETHODCALLTYPE Initialize(IUnknown* pICorProfilerInfoUnk) override;
  HRESULT STDMETHODCALLTYPE Shutdown() override;
  HRESULT STDMETHODCALLTYPE
  AppDomainCreationStarted(AppDomainID appDomainId) override;
  HRESULT STDMETHODCALLTYPE
  AppDomainCreationFinished(AppDomainID appDomainId, HRESULT hrStatus) override;
  HRESULT STDMETHODCALLTYPE
  AppDomainShutdownStarted(AppDomainID appDomainId) override;
  HRESULT STDMETHODCALLTYPE
  AppDomainShutdownFinished(AppDomainID appDomainId, HRESULT hrStatus) override;
  HRESULT STDMETHODCALLTYPE AssemblyLoadStarted(AssemblyID assemblyId) override;
  HRESULT STDMETHODCALLTYPE AssemblyLoadFinished(AssemblyID assemblyId,
                                                 HRESULT hrStatus) override;
  HRESULT STDMETHODCALLTYPE
  AssemblyUnloadStarted(AssemblyID assemblyId) override;
  HRESULT STDMETHODCALLTYPE AssemblyUnloadFinished(AssemblyID assemblyId,
                                                   HRESULT hrStatus) override;
  HRESULT STDMETHODCALLTYPE ModuleLoadStarted(ModuleID moduleId) override;
  HRESULT STDMETHODCALLTYPE ModuleLoadFinished(ModuleID moduleId,
                                               HRESULT hrStatus) override;
  HRESULT STDMETHODCALLTYPE ModuleUnloadStarted(ModuleID moduleId) override;
  HRESULT STDMETHODCALLTYPE ModuleUnloadFinished(ModuleID moduleId,
                                                 HRESULT hrStatus) override;
  HRESULT STDMETHODCALLTYPE
  ModuleAttachedToAssembly(ModuleID moduleId, AssemblyID AssemblyId) override;
  HRESULT STDMETHODCALLTYPE ClassLoadStarted(ClassID classId) override;
  HRESULT STDMETHODCALLTYPE ClassLoadFinished(ClassID classId,
                                              HRESULT hrStatus) override;
  HRESULT STDMETHODCALLTYPE ClassUnloadStarted(ClassID classId) override;
  HRESULT STDMETHODCALLTYPE ClassUnloadFinished(ClassID classId,
                                                HRESULT hrStatus) override;
  HRESULT STDMETHODCALLTYPE
  FunctionUnloadStarted(FunctionID functionId) override;
  HRESULT STDMETHODCALLTYPE JITCompilationStarted(FunctionID functionId,
                                                  BOOL fIsSafeToBlock) override;
  HRESULT STDMETHODCALLTYPE JITCompilationFinished(
      FunctionID functionId, HRESULT hrStatus, BOOL fIsSafeToBlock) override;
  HRESULT STDMETHODCALLTYPE JITCachedFunctionSearchStarted(
      FunctionID functionId, BOOL* pbUseCachedFunction) override;
  HRESULT STDMETHODCALLTYPE JITCachedFunctionSearchFinished(
      FunctionID functionId, COR_PRF_JIT_CACHE result) override;
  HRESULT STDMETHODCALLTYPE JITFunctionPitched(FunctionID functionId) override;
  HRESULT STDMETHODCALLTYPE JITInlining(FunctionID callerId,
                                        FunctionID calleeId,
                                        BOOL* pfShouldInline) override;
  HRESULT STDMETHODCALLTYPE ThreadCreated(ThreadID threadId) override;
  HRESULT STDMETHODCALLTYPE ThreadDestroyed(ThreadID threadId) override;
  HRESULT STDMETHODCALLTYPE ThreadAssignedToOSThread(ThreadID managedThreadId,
                                                     DWORD osThreadId) override;
  HRESULT STDMETHODCALLTYPE RemotingClientInvocationStarted() override;
  HRESULT STDMETHODCALLTYPE
  RemotingClientSendingMessage(GUID* pCookie, BOOL fIsAsync) override;
  HRESULT STDMETHODCALLTYPE
  RemotingClientReceivingReply(GUID* pCookie, BOOL fIsAsync) override;
  HRESULT STDMETHODCALLTYPE RemotingClientInvocationFinished() override;
  HRESULT STDMETHODCALLTYPE
  RemotingServerReceivingMessage(GUID* pCookie, BOOL fIsAsync) override;
  HRESULT STDMETHODCALLTYPE RemotingServerInvocationStarted() override;
  HRESULT STDMETHODCALLTYPE RemotingServerInvocationReturned() override;
  HRESULT STDMETHODCALLTYPE RemotingServerSendingReply(GUID* pCookie,
                                                       BOOL fIsAsync) override;
  HRESULT STDMETHODCALLTYPE UnmanagedToManagedTransition(
      FunctionID functionId, COR_PRF_TRANSITION_REASON reason) override;
  HRESULT STDMETHODCALLTYPE ManagedToUnmanagedTransition(
      FunctionID functionId, COR_PRF_TRANSITION_REASON reason) override;
  HRESULT STDMETHODCALLTYPE
  RuntimeSuspendStarted(COR_PRF_SUSPEND_REASON suspendReason) override;
  HRESULT STDMETHODCALLTYPE RuntimeSuspendFinished() override;
  HRESULT STDMETHODCALLTYPE RuntimeSuspendAborted() override;
  HRESULT STDMETHODCALLTYPE RuntimeResumeStarted() override;
  HRESULT STDMETHODCALLTYPE RuntimeResumeFinished() override;
  HRESULT STDMETHODCALLTYPE RuntimeThreadSuspended(ThreadID threadId) override;
  HRESULT STDMETHODCALLTYPE RuntimeThreadResumed(ThreadID threadId) override;
  HRESULT STDMETHODCALLTYPE MovedReferences(
      ULONG cMovedObjectIDRanges, ObjectID oldObjectIDRangeStart[],
      ObjectID newObjectIDRangeStart[], ULONG cObjectIDRangeLength[]) override;
  HRESULT STDMETHODCALLTYPE ObjectAllocated(ObjectID objectId,
                                            ClassID classId) override;
  HRESULT STDMETHODCALLTYPE ObjectsAllocatedByClass(ULONG cClassCount,
                                                    ClassID classIds[],
                                                    ULONG cObjects[]) override;
  HRESULT STDMETHODCALLTYPE ObjectReferences(ObjectID objectId, ClassID classId,
                                             ULONG cObjectRefs,
                                             ObjectID objectRefIds[]) override;
  HRESULT STDMETHODCALLTYPE RootReferences(ULONG cRootRefs,
                                           ObjectID rootRefIds[]) override;
  HRESULT STDMETHODCALLTYPE ExceptionThrown(ObjectID thrownObjectId) override;
  HRESULT STDMETHODCALLTYPE
  ExceptionSearchFunctionEnter(FunctionID functionId) override;
  HRESULT STDMETHODCALLTYPE ExceptionSearchFunctionLeave() override;
  HRESULT STDMETHODCALLTYPE
  ExceptionSearchFilterEnter(FunctionID functionId) override;
  HRESULT STDMETHODCALLTYPE ExceptionSearchFilterLeave() override;
  HRESULT STDMETHODCALLTYPE
  ExceptionSearchCatcherFound(FunctionID functionId) override;
  HRESULT STDMETHODCALLTYPE ExceptionOSHandlerEnter(UINT_PTR __unused) override;
  HRESULT STDMETHODCALLTYPE ExceptionOSHandlerLeave(UINT_PTR __unused) override;
  HRESULT STDMETHODCALLTYPE
  ExceptionUnwindFunctionEnter(FunctionID functionId) override;
  HRESULT STDMETHODCALLTYPE ExceptionUnwindFunctionLeave() override;
  HRESULT STDMETHODCALLTYPE
  ExceptionUnwindFinallyEnter(FunctionID functionId) override;
  HRESULT STDMETHODCALLTYPE ExceptionUnwindFinallyLeave() override;
  HRESULT STDMETHODCALLTYPE ExceptionCatcherEnter(FunctionID functionId,
                                                  ObjectID objectId) override;
  HRESULT STDMETHODCALLTYPE ExceptionCatcherLeave() override;
  HRESULT STDMETHODCALLTYPE COMClassicVTableCreated(ClassID wrappedClassId,
                                                    REFGUID implementedIID,
                                                    void* pVTable,
                                                    ULONG cSlots) override;
  HRESULT STDMETHODCALLTYPE COMClassicVTableDestroyed(ClassID wrappedClassId,
                                                      REFGUID implementedIID,
                                                      void* pVTable) override;
  HRESULT STDMETHODCALLTYPE ExceptionCLRCatcherFound() override;
  HRESULT STDMETHODCALLTYPE ExceptionCLRCatcherExecute() override;
  HRESULT STDMETHODCALLTYPE ThreadNameChanged(ThreadID threadId, ULONG cchName,
                                              WCHAR name[]) override;
  HRESULT STDMETHODCALLTYPE
  GarbageCollectionStarted(int cGenerations, BOOL generationCollected[],
                           COR_PRF_GC_REASON reason) override;
  HRESULT STDMETHODCALLTYPE SurvivingReferences(
      ULONG cSurvivingObjectIDRanges, ObjectID objectIDRangeStart[],
      ULONG cObjectIDRangeLength[]) override;
  HRESULT STDMETHODCALLTYPE GarbageCollectionFinished() override;
  HRESULT STDMETHODCALLTYPE
  FinalizeableObjectQueued(DWORD finalizerFlags, ObjectID objectID) override;
  HRESULT STDMETHODCALLTYPE RootReferences2(ULONG cRootRefs,
                                            ObjectID rootRefIds[],
                                            COR_PRF_GC_ROOT_KIND rootKinds[],
                                            COR_PRF_GC_ROOT_FLAGS rootFlags[],
                                            UINT_PTR rootIds[]) override;
  HRESULT STDMETHODCALLTYPE HandleCreated(GCHandleID handleId,
                                          ObjectID initialObjectId) override;
  HRESULT STDMETHODCALLTYPE HandleDestroyed(GCHandleID handleId) override;
  HRESULT STDMETHODCALLTYPE InitializeForAttach(IUnknown* pCorProfilerInfoUnk,
                                                void* pvClientData,
                                                UINT cbClientData) override;
  HRESULT STDMETHODCALLTYPE ProfilerAttachComplete() override;
  HRESULT STDMETHODCALLTYPE ProfilerDetachSucceeded() override;
  HRESULT STDMETHODCALLTYPE ReJITCompilationStarted(
      FunctionID functionId, ReJITID rejitId, BOOL fIsSafeToBlock) override;
  HRESULT STDMETHODCALLTYPE
  GetReJITParameters(ModuleID moduleId, mdMethodDef methodId,
                     ICorProfilerFunctionControl* pFunctionControl) override;
  HRESULT STDMETHODCALLTYPE
  ReJITCompilationFinished(FunctionID functionId, ReJITID rejitId,
                           HRESULT hrStatus, BOOL fIsSafeToBlock) override;
  HRESULT STDMETHODCALLTYPE ReJITError(ModuleID moduleId, mdMethodDef methodId,
                                       FunctionID functionId,
                                       HRESULT hrStatus) override;
  HRESULT STDMETHODCALLTYPE MovedReferences2(
      ULONG cMovedObjectIDRanges, ObjectID oldObjectIDRangeStart[],
      ObjectID newObjectIDRangeStart[], SIZE_T cObjectIDRangeLength[]) override;
  HRESULT STDMETHODCALLTYPE SurvivingReferences2(
      ULONG cSurvivingObjectIDRanges, ObjectID objectIDRangeStart[],
      SIZE_T cObjectIDRangeLength[]) override;
  HRESULT STDMETHODCALLTYPE ConditionalWeakTableElementReferences(
      ULONG cRootRefs, ObjectID keyRefIds[], ObjectID valueRefIds[],
      GCHandleID rootIds[]) override;
  HRESULT STDMETHODCALLTYPE GetAssemblyReferences(
      const WCHAR* wszAssemblyPath,
      ICorProfilerAssemblyReferenceProvider* pAsmRefProvider) override;
  HRESULT STDMETHODCALLTYPE
  ModuleInMemorySymbolsUpdated(ModuleID moduleId) override;

  HRESULT STDMETHODCALLTYPE DynamicMethodJITCompilationStarted(
      FunctionID functionId, BOOL fIsSafeToBlock, LPCBYTE ilHeader,
      ULONG cbILHeader) override;
  HRESULT STDMETHODCALLTYPE DynamicMethodJITCompilationFinished(
      FunctionID functionId, HRESULT hrStatus, BOOL fIsSafeToBlock) override;

  //
  // IInstrumentationMethod methods
  //
  HRESULT STDMETHODCALLTYPE Initialize(
      _In_ IProfilerManager* pProfilerManager) override;

  HRESULT STDMETHODCALLTYPE OnAppDomainCreated(
      _In_ IAppDomainInfo* pAppDomainInfo) override;

  HRESULT STDMETHODCALLTYPE OnAppDomainShutdown(
      _In_ IAppDomainInfo* pAppDomainInfo) override;

  HRESULT STDMETHODCALLTYPE OnAssemblyLoaded(
      _In_ IAssemblyInfo* pAssemblyInfo) override;

  HRESULT STDMETHODCALLTYPE OnAssemblyUnloaded(
      _In_ IAssemblyInfo* pAssemblyInfo) override;

  HRESULT STDMETHODCALLTYPE OnModuleLoaded(
      _In_ IModuleInfo* pModuleInfo) override;

  HRESULT STDMETHODCALLTYPE OnModuleUnloaded(
      _In_ IModuleInfo* pModuleInfo) override;

  HRESULT STDMETHODCALLTYPE OnShutdown() override;

  HRESULT STDMETHODCALLTYPE ShouldInstrumentMethod(_In_ IMethodInfo* pMethodInfo, _In_ BOOL isRejit,
                         _Out_ BOOL* pbInstrument) override;

  HRESULT STDMETHODCALLTYPE BeforeInstrumentMethod(
      _In_ IMethodInfo* pMethodInfo, _In_ BOOL isRejit) override;

  HRESULT STDMETHODCALLTYPE InstrumentMethod(
      _In_ IMethodInfo* pMethodInfo, _In_ BOOL isRejit) override;

  HRESULT STDMETHODCALLTYPE OnInstrumentationComplete(
      _In_ IMethodInfo* pMethodInfo, _In_ BOOL isRejit) override;

  HRESULT STDMETHODCALLTYPE AllowInlineSite(
      _In_ IMethodInfo* pMethodInfoInlinee, _In_ IMethodInfo* pMethodInfoCaller,
      _Out_ BOOL* pbAllowInline) override;


  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid,
                                           void** ppvObject) override {
    if (riid == __uuidof(ICorProfilerCallback8) ||
        riid == __uuidof(ICorProfilerCallback7) ||
        riid == __uuidof(ICorProfilerCallback6) ||
        riid == __uuidof(ICorProfilerCallback5) ||
        riid == __uuidof(ICorProfilerCallback4) ||
        riid == __uuidof(ICorProfilerCallback3) ||
        riid == __uuidof(ICorProfilerCallback2) ||
        riid == __uuidof(ICorProfilerCallback) ||
        riid == IID_IUnknown) {
      *ppvObject = static_cast<ICorProfilerCallback8*>(this);
      AddRef();
      return S_OK;
    }
    else if (riid == __uuidof(IInstrumentationMethod)) {
      *ppvObject = static_cast<IInstrumentationMethod*>(this);
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

}  // namespace trace

#endif  // DD_CLR_PROFILER_PROFILER_BASE_H_
