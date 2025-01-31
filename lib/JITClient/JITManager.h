//-------------------------------------------------------------------------------------------------------
// Copyright (C) Microsoft. All rights reserved.
// Licensed under the MIT license. See LICENSE.txt file in the project root for full license information.
//-------------------------------------------------------------------------------------------------------

#pragma once

// We need real JITManager code when on _WIN32 or explict ENABLE_OOP_NATIVE_CODEGEN.
// Otherwise we use a dummy JITManager which disables OOP JIT to reduce code noise.

enum class RemoteCallType
{
    CodeGen,
    ThunkCreation,
    HeapQuery,
    StateUpdate
};

#if _WIN32 || ENABLE_OOP_NATIVE_CODEGEN
class JITManager
{
public:
    HRESULT ConnectRpcServer(__in HANDLE jitProcessHandle, __in_opt void* serverSecurityDescriptor, __in UUID connectionUuid);

    bool IsConnected() const;
    bool IsJITServer() const;
    void SetIsJITServer();
    bool IsOOPJITEnabled() const;
    void EnableOOPJIT();

    HANDLE GetServerHandle() const;

    HRESULT InitializeThreadContext(
        __in ThreadContextDataIDL * data,
        __out PPTHREADCONTEXT_HANDLE threadContextInfoAddress,
        __out intptr_t *prereservedRegionAddr);

    HRESULT CleanupThreadContext(
        __inout PPTHREADCONTEXT_HANDLE threadContextInfoAddress);

    HRESULT UpdatePropertyRecordMap(
        __in PTHREADCONTEXT_HANDLE threadContextInfoAddress,
        __in_opt BVSparseNodeIDL * updatedPropsBVHead);

    HRESULT NewInterpreterThunkBlock(
        __in PSCRIPTCONTEXT_HANDLE scriptContextInfoAddress,
        __in InterpreterThunkInputIDL * thunkInput,
        __out InterpreterThunkOutputIDL * thunkOutput);

    HRESULT AddDOMFastPathHelper(
        __in PSCRIPTCONTEXT_HANDLE scriptContextInfoAddress,
        __in intptr_t funcInfoAddr,
        __in int helper);

    HRESULT AddModuleRecordInfo(
            /* [in] */ PSCRIPTCONTEXT_HANDLE scriptContextInfoAddress,
            /* [in] */ unsigned int moduleId,
            /* [in] */ intptr_t localExportSlotsAddr);

    HRESULT SetWellKnownHostTypeId(
        __in  PTHREADCONTEXT_HANDLE threadContextRoot,
        __in  int typeId);

    HRESULT InitializeScriptContext(
        __in ScriptContextDataIDL * data,
        __in  PTHREADCONTEXT_HANDLE threadContextInfoAddress,
        __out PPSCRIPTCONTEXT_HANDLE scriptContextInfoAddress);

    HRESULT CleanupScriptContext(
        __inout PPSCRIPTCONTEXT_HANDLE scriptContextInfoAddress);

    HRESULT CloseScriptContext(
        __in PSCRIPTCONTEXT_HANDLE scriptContextInfoAddress);

    HRESULT FreeAllocation(
        __in PTHREADCONTEXT_HANDLE threadContextInfoAddress,
        __in intptr_t address);

    HRESULT SetIsPRNGSeeded(
        __in PSCRIPTCONTEXT_HANDLE scriptContextInfoAddress,
        __in boolean value);

    HRESULT IsNativeAddr(
        __in PTHREADCONTEXT_HANDLE threadContextInfoAddress,
        __in intptr_t address,
        __out boolean * result);

    HRESULT RemoteCodeGenCall(
        __in CodeGenWorkItemIDL *workItemData,
        __in PSCRIPTCONTEXT_HANDLE scriptContextInfoAddress,
        __out JITOutputIDL *jitData);

    HRESULT Shutdown();


    static JITManager * GetJITManager();
    static void HandleServerCallResult(HRESULT hr, RemoteCallType callType);
private:
    JITManager();
    ~JITManager();

    HRESULT CreateBinding(
        __in HANDLE serverProcessHandle,
        __in_opt void* serverSecurityDescriptor,
        __in UUID* connectionUuid,
        __out RPC_BINDING_HANDLE* bindingHandle);

    RPC_BINDING_HANDLE m_rpcBindingHandle;
    HANDLE m_serverHandle;
    UUID m_jitConnectionId;
    bool m_oopJitEnabled;
    bool m_isJITServer;

    static JITManager s_jitManager;

};

#else  // !ENABLE_OOP_NATIVE_CODEGEN
class JITManager
{
public:
    HRESULT ConnectRpcServer(__in HANDLE jitProcessHandle, __in_opt void* serverSecurityDescriptor, __in UUID connectionUuid)
        { Assert(false); return E_FAIL; }

    bool IsConnected() const { return false; }
    bool IsJITServer() const { return false; }
    void SetIsJITServer() { Assert(false); }
    bool IsOOPJITEnabled() const { return false; }
    void EnableOOPJIT() { Assert(false); }

    HANDLE GetServerHandle() const
    {
        Assert(false); return HANDLE();
    }

    HRESULT InitializeThreadContext(
        __in ThreadContextDataIDL * data,
        __out PPTHREADCONTEXT_HANDLE threadContextInfoAddress,
        __out intptr_t *prereservedRegionAddr)
        { Assert(false); return E_FAIL; }

    HRESULT CleanupThreadContext(
        __inout PPTHREADCONTEXT_HANDLE threadContextInfoAddress)
        { Assert(false); return E_FAIL; }

    HRESULT UpdatePropertyRecordMap(
        __in PTHREADCONTEXT_HANDLE threadContextInfoAddress,
        __in_opt BVSparseNodeIDL * updatedPropsBVHead)
        { Assert(false); return E_FAIL; }

    HRESULT AddDOMFastPathHelper(
        __in PSCRIPTCONTEXT_HANDLE scriptContextInfoAddress,
        __in intptr_t funcInfoAddr,
        __in int helper)
        { Assert(false); return E_FAIL; }

    HRESULT AddModuleRecordInfo(
            /* [in] */ PSCRIPTCONTEXT_HANDLE scriptContextInfoAddress,
            /* [in] */ unsigned int moduleId,
            /* [in] */ intptr_t localExportSlotsAddr)
        { Assert(false); return E_FAIL; }

    HRESULT SetWellKnownHostTypeId(
        __in  PTHREADCONTEXT_HANDLE threadContextRoot,
        __in  int typeId)
        { Assert(false); return E_FAIL; }

    HRESULT InitializeScriptContext(
        __in ScriptContextDataIDL * data,
        __in PTHREADCONTEXT_HANDLE threadContextInfoAddress,
        __out PPSCRIPTCONTEXT_HANDLE scriptContextInfoAddress)
        { Assert(false); return E_FAIL; }

    HRESULT CleanupScriptContext(
        __inout PPSCRIPTCONTEXT_HANDLE scriptContextInfoAddress)
        { Assert(false); return E_FAIL; }

    HRESULT CloseScriptContext(
        __in PSCRIPTCONTEXT_HANDLE scriptContextInfoAddress)
        { Assert(false); return E_FAIL; }

    HRESULT FreeAllocation(
        __in PTHREADCONTEXT_HANDLE threadContextInfoAddress,
        __in intptr_t address)
        { Assert(false); return E_FAIL; }

    HRESULT SetIsPRNGSeeded(
        __in PSCRIPTCONTEXT_HANDLE scriptContextInfoAddress,
        __in boolean value)
        { Assert(false); return E_FAIL; }

    HRESULT IsNativeAddr(
        __in PTHREADCONTEXT_HANDLE threadContextInfoAddress,
        __in intptr_t address,
        __out boolean * result)
        { Assert(false); return E_FAIL; }

    HRESULT RemoteCodeGenCall(
        __in CodeGenWorkItemIDL *workItemData,
        __in PSCRIPTCONTEXT_HANDLE scriptContextInfoAddress,
        __out JITOutputIDL *jitData)
        { Assert(false); return E_FAIL; }

    HRESULT Shutdown()
        { Assert(false); return E_FAIL; }

    static JITManager * GetJITManager()
        { return &s_jitManager; }
    static void HandleServerCallResult(HRESULT hr, RemoteCallType callType) { Assert(UNREACHED); }

private:
    static JITManager s_jitManager;
};
#endif  // !ENABLE_OOP_NATIVE_CODEGEN
