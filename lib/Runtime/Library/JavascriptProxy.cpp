//-------------------------------------------------------------------------------------------------------
// Copyright (C) Microsoft. All rights reserved.
// Licensed under the MIT license. See LICENSE.txt file in the project root for full license information.
//-------------------------------------------------------------------------------------------------------
#include "RuntimeLibraryPch.h"


namespace Js
{
    BOOL JavascriptProxy::Is(Var obj)
    {
        return JavascriptOperators::GetTypeId(obj) == TypeIds_Proxy;
    }

    RecyclableObject* JavascriptProxy::GetTarget()
    {
        if (target == nullptr)
        {
            JavascriptError::ThrowTypeError(GetScriptContext(), JSERR_ErrorOnRevokedProxy, _u(""));
        }
        return target;
    }

    RecyclableObject* JavascriptProxy::GetHandler()
    {
        if (handler == nullptr)
        {
            JavascriptError::ThrowTypeError(GetScriptContext(), JSERR_ErrorOnRevokedProxy, _u(""));
        }
        return handler;
    }


    Var JavascriptProxy::NewInstance(RecyclableObject* function, CallInfo callInfo, ...)
    {
        PROBE_STACK(function->GetScriptContext(), Js::Constants::MinStackDefault);

        ARGUMENTS(args, callInfo);
        ScriptContext* scriptContext = function->GetScriptContext();

        AssertMsg(args.Info.Count > 0, "Should always have implicit 'this'");
        CHAKRATEL_LANGSTATS_INC_DATACOUNT(ES6_Proxy);

        if (!(args.Info.Flags & CallFlags_New))
        {
            JavascriptError::ThrowTypeError(scriptContext, JSERR_ErrorOnNew, _u("Proxy"));
        }

        JavascriptProxy* proxy = JavascriptProxy::Create(scriptContext, args);
        return proxy;
    }

    JavascriptProxy* JavascriptProxy::Create(ScriptContext* scriptContext, Arguments args)
    {
        // SkipDefaultNewObject function flag should have prevented the default object from
        // being created, except when call true a host dispatch.

        Var newTarget = args.Info.Flags & CallFlags_NewTarget ? args.Values[args.Info.Count] : args[0];

        bool isCtorSuperCall = (args.Info.Flags & CallFlags_New) && newTarget != nullptr && !JavascriptOperators::IsUndefined(newTarget);
        Assert(isCtorSuperCall || !(args.Info.Flags & CallFlags_New) || args[0] == nullptr
            || JavascriptOperators::GetTypeId(args[0]) == TypeIds_HostDispatch);

        RecyclableObject* target, *handler;

        if (args.Info.Count < 3)
        {
            JavascriptError::ThrowTypeError(scriptContext, JSERR_NeedProxyArgument);
        }

        if (!JavascriptOperators::IsObjectType(JavascriptOperators::GetTypeId(args[1])))
        {
            JavascriptError::ThrowTypeError(scriptContext, JSERR_InvalidProxyArgument, _u("target"));
        }
        target = DynamicObject::FromVar(args[1]);
#if ENABLE_COPYONACCESS_ARRAY
        JavascriptLibrary::CheckAndConvertCopyOnAccessNativeIntArray<Var>(target);
#endif
        if (JavascriptProxy::Is(target))
        {
            if (JavascriptProxy::FromVar(target)->target == nullptr)
            {
                JavascriptError::ThrowTypeError(scriptContext, JSERR_InvalidProxyArgument, _u("target"));
            }
        }

        if (!JavascriptOperators::IsObjectType(JavascriptOperators::GetTypeId(args[2])))
        {
            JavascriptError::ThrowTypeError(scriptContext, JSERR_InvalidProxyArgument, _u("handler"));
        }
        handler = DynamicObject::FromVar(args[2]);
        if (JavascriptProxy::Is(handler))
        {
            if (JavascriptProxy::FromVar(handler)->handler == nullptr)
            {
                JavascriptError::ThrowTypeError(scriptContext, JSERR_InvalidProxyArgument, _u("handler"));
            }
        }

        JavascriptProxy* newProxy = RecyclerNew(scriptContext->GetRecycler(), JavascriptProxy, scriptContext->GetLibrary()->GetProxyType(), scriptContext, target, handler);
        if (JavascriptConversion::IsCallable(target))
        {
            newProxy->ChangeType();
            newProxy->GetDynamicType()->SetEntryPoint(JavascriptProxy::FunctionCallTrap);
        }
        return isCtorSuperCall ?
            JavascriptProxy::FromVar(JavascriptOperators::OrdinaryCreateFromConstructor(RecyclableObject::FromVar(newTarget), newProxy, nullptr, scriptContext)) :
            newProxy;
    }

    Var JavascriptProxy::EntryRevocable(RecyclableObject* function, CallInfo callInfo, ...)
    {
        PROBE_STACK(function->GetScriptContext(), Js::Constants::MinStackDefault);

        ARGUMENTS(args, callInfo);
        ScriptContext* scriptContext = function->GetScriptContext();
        AUTO_TAG_NATIVE_LIBRARY_ENTRY(function, callInfo, _u("Proxy.revocable"));

        AssertMsg(args.Info.Count > 0, "Should always have implicit 'this'");
        if (args.Info.Flags & CallFlags_New)
        {
            JavascriptError::ThrowTypeError(scriptContext, JSERR_ErrorOnNew, _u("Proxy.revocable"));
        }

        JavascriptProxy* proxy = JavascriptProxy::Create(scriptContext, args);
        JavascriptLibrary* library = scriptContext->GetLibrary();
        DynamicType* type = library->CreateFunctionWithLengthType(&EntryInfo::Revoke);
        RuntimeFunction* revoker = RecyclerNewEnumClass(scriptContext->GetRecycler(),
            library->EnumFunctionClass, RuntimeFunction,
            type, &EntryInfo::Revoke);

        revoker->SetPropertyWithAttributes(Js::PropertyIds::length, Js::TaggedInt::ToVarUnchecked(0), PropertyNone, NULL);
        revoker->SetInternalProperty(Js::InternalPropertyIds::RevocableProxy, proxy, PropertyOperationFlags::PropertyOperation_Force, nullptr);

        DynamicObject* obj = scriptContext->GetLibrary()->CreateObject(true, 2);
        JavascriptOperators::SetProperty(obj, obj, PropertyIds::proxy, proxy, scriptContext);
        JavascriptOperators::SetProperty(obj, obj, PropertyIds::revoke, revoker, scriptContext);
        return obj;
    }

    Var JavascriptProxy::EntryRevoke(RecyclableObject* function, CallInfo callInfo, ...)
    {
        PROBE_STACK(function->GetScriptContext(), Js::Constants::MinStackDefault);

        ARGUMENTS(args, callInfo);
        ScriptContext* scriptContext = function->GetScriptContext();
        AUTO_TAG_NATIVE_LIBRARY_ENTRY(function, callInfo, _u("Proxy.revoke"));

        AssertMsg(args.Info.Count > 0, "Should always have implicit 'this'");
        Var revokableProxy;
        if (!function->GetInternalProperty(function, Js::InternalPropertyIds::RevocableProxy, &revokableProxy, nullptr, scriptContext))
        {
            JavascriptError::ThrowTypeError(scriptContext, JSERR_InvalidProxyArgument, _u(""));
        }
        TypeId typeId = JavascriptOperators::GetTypeId(revokableProxy);
        if (typeId == TypeIds_Null)
        {
            return scriptContext->GetLibrary()->GetUndefined();
        }
        if (typeId != TypeIds_Proxy)
        {
            JavascriptError::ThrowTypeError(scriptContext, JSERR_InvalidProxyArgument, _u(""));
        }
        function->SetInternalProperty(Js::InternalPropertyIds::RevocableProxy, scriptContext->GetLibrary()->GetNull(), PropertyOperationFlags::PropertyOperation_Force, nullptr);
        (JavascriptProxy::FromVar(revokableProxy))->RevokeObject();

        return scriptContext->GetLibrary()->GetUndefined();
    }

    JavascriptProxy::JavascriptProxy(DynamicType * type) :
        DynamicObject(type),
        handler(nullptr),
        target(nullptr)
    {
        type->SetHasSpecialPrototype(true);
    }

    JavascriptProxy::JavascriptProxy(DynamicType * type, ScriptContext * scriptContext, RecyclableObject* target, RecyclableObject* handler) :
        DynamicObject(type),
        handler(handler),
        target(target)
    {
        type->SetHasSpecialPrototype(true);
    }

    void JavascriptProxy::RevokeObject()
    {
        handler = nullptr;
        target = nullptr;
    }

    template <class Fn, class GetPropertyIdFunc>
    BOOL JavascriptProxy::GetPropertyDescriptorTrap(Var originalInstance, Fn fn, GetPropertyIdFunc getPropertyId, PropertyDescriptor* resultDescriptor, ScriptContext* requestContext)
    {
        PROBE_STACK(GetScriptContext(), Js::Constants::MinStackDefault);

        Assert((static_cast<DynamicType*>(GetType()))->GetTypeHandler()->GetPropertyCount() == 0 ||
            (static_cast<DynamicType*>(GetType()))->GetTypeHandler()->GetPropertyId(GetScriptContext(), 0) == InternalPropertyIds::WeakMapKeyMap);
        JavascriptFunction* gOPDMethod = GetMethodHelper(PropertyIds::getOwnPropertyDescriptor, requestContext);
        Var getResult;
        ThreadContext* threadContext = requestContext->GetThreadContext();
        //7. If trap is undefined, then
        //    a.Return the result of calling the[[GetOwnProperty]] internal method of target with argument P.
        if (nullptr == gOPDMethod || GetScriptContext()->IsHeapEnumInProgress())
        {
            resultDescriptor->SetFromProxy(false);
            return fn();
        }
        // Reject implicit call
        if (threadContext->IsDisableImplicitCall())
        {
            threadContext->AddImplicitCallFlags(Js::ImplicitCall_External);
            return FALSE;
        }

        PropertyId propertyId = getPropertyId();
        CallInfo callInfo(CallFlags_Value, 3);
        Var varArgs[3];
        Js::Arguments arguments(callInfo, varArgs);
        varArgs[0] = handler;
        varArgs[1] = target;
        varArgs[2] = GetName(requestContext, propertyId);

        Assert(JavascriptString::Is(varArgs[2]) || JavascriptSymbol::Is(varArgs[2]));
        //8. Let trapResultObj be the result of calling the[[Call]] internal method of trap with handler as the this value and a new List containing target and P.
        //9. ReturnIfAbrupt(trapResultObj).
        //10. If Type(trapResultObj) is neither Object nor Undefined, then throw a TypeError exception.

        Js::ImplicitCallFlags saveImplicitCallFlags = threadContext->GetImplicitCallFlags();
        getResult = JavascriptFunction::FromVar(gOPDMethod)->CallFunction(arguments);
        threadContext->SetImplicitCallFlags((Js::ImplicitCallFlags)(saveImplicitCallFlags | ImplicitCall_Accessor));

        TypeId getResultTypeId = JavascriptOperators::GetTypeId(getResult);
        if (StaticType::Is(getResultTypeId) && getResultTypeId != TypeIds_Undefined)
        {
            JavascriptError::ThrowTypeError(requestContext, JSERR_NeedObject, _u("getOwnPropertyDescriptor"));
        }
        //11. Let targetDesc be the result of calling the[[GetOwnProperty]] internal method of target with argument P.
        //12. ReturnIfAbrupt(targetDesc).
        PropertyDescriptor targetDescriptor;
        BOOL hasProperty;

        hasProperty = JavascriptOperators::GetOwnPropertyDescriptor(target, getPropertyId(), requestContext, &targetDescriptor);
        //13. If trapResultObj is undefined, then
        //a.If targetDesc is undefined, then return undefined.
        //b.If targetDesc.[[Configurable]] is false, then throw a TypeError exception.
        //c.Let extensibleTarget be the result of IsExtensible(target).
        //d.ReturnIfAbrupt(extensibleTarget).
        //e.If ToBoolean(extensibleTarget) is false, then throw a TypeError exception.
        //f.Return undefined.
        if (getResultTypeId == TypeIds_Undefined)
        {
            if (!hasProperty)
            {
                return FALSE;
            }
            if (!targetDescriptor.IsConfigurable())
            {
                JavascriptError::ThrowTypeError(requestContext, JSERR_InconsistentTrapResult, _u("getOwnPropertyDescriptor"));
            }
            if (!target->IsExtensible())
            {
                JavascriptError::ThrowTypeError(requestContext, JSERR_InconsistentTrapResult, _u("getOwnPropertyDescriptor"));
            }
            return FALSE;
        }

        //14. Let extensibleTarget be the result of IsExtensible(target).
        //15. ReturnIfAbrupt(extensibleTarget).
        //16. Let resultDesc be ToPropertyDescriptor(trapResultObj).
        //17. ReturnIfAbrupt(resultDesc).
        //18. Call CompletePropertyDescriptor(resultDesc, targetDesc).
        //19. Let valid be the result of IsCompatiblePropertyDescriptor(extensibleTarget, resultDesc, targetDesc).
        //20. If valid is false, then throw a TypeError exception.
        //21. If resultDesc.[[Configurable]] is false, then
        //a.If targetDesc is undefined or targetDesc.[[Configurable]] is true, then
        //i.Throw a TypeError exception.
        //22. Return resultDesc.

        BOOL isTargetExtensible = target->IsExtensible();
        BOOL toProperty = JavascriptOperators::ToPropertyDescriptor(getResult, resultDescriptor, requestContext);
        if (!toProperty && isTargetExtensible)
        {
            JavascriptError::ThrowTypeError(requestContext, JSERR_InconsistentTrapResult, _u("getOwnPropertyDescriptor"));
        }

        JavascriptOperators::CompletePropertyDescriptor(resultDescriptor, nullptr, requestContext);
        if (!JavascriptOperators::IsCompatiblePropertyDescriptor(*resultDescriptor, hasProperty ? &targetDescriptor : nullptr, !!isTargetExtensible, true, requestContext))
        {
            JavascriptError::ThrowTypeError(requestContext, JSERR_InconsistentTrapResult, _u("getOwnPropertyDescriptor"));
        }
        if (!resultDescriptor->IsConfigurable())
        {
            if (!hasProperty || targetDescriptor.IsConfigurable())
            {
                JavascriptError::ThrowTypeError(requestContext, JSERR_InconsistentTrapResult, _u("getOwnPropertyDescriptor"));
            }
        }
        resultDescriptor->SetFromProxy(true);
        return toProperty;
    }

    template <class Fn, class GetPropertyIdFunc>
    BOOL JavascriptProxy::GetPropertyTrap(Var instance, PropertyDescriptor* propertyDescriptor, Fn fn, GetPropertyIdFunc getPropertyId, ScriptContext* requestContext)
    {
        PROBE_STACK(GetScriptContext(), Js::Constants::MinStackDefault);

        ScriptContext* scriptContext = GetScriptContext();

        // Reject implicit call
        ThreadContext* threadContext = scriptContext->GetThreadContext();
        if (threadContext->IsDisableImplicitCall())
        {
            threadContext->AddImplicitCallFlags(Js::ImplicitCall_External);
            return FALSE;
        }

        if (this->handler == nullptr)
        {
            // the proxy has been revoked; TypeError.
            if (!threadContext->RecordImplicitException())
                return FALSE;
            JavascriptError::ThrowTypeError(scriptContext, JSERR_ErrorOnRevokedProxy, _u("get"));
        }

        RecyclableObject *target = this->target;

        JavascriptFunction* getGetMethod = GetMethodHelper(PropertyIds::get, requestContext);
        Var getGetResult;
        if (nullptr == getGetMethod || scriptContext->IsHeapEnumInProgress())
        {
            propertyDescriptor->SetFromProxy(false);
            return fn(target);
        }

        PropertyId propertyId = getPropertyId();
        propertyDescriptor->SetFromProxy(true);
        CallInfo callInfo(CallFlags_Value, 4);
        Var varArgs[4];
        Js::Arguments arguments(callInfo, varArgs);
        varArgs[0] = handler;
        varArgs[1] = target;
        varArgs[2] = GetName(scriptContext, propertyId);
        varArgs[3] = instance;

        Js::ImplicitCallFlags saveImplicitCallFlags = threadContext->GetImplicitCallFlags();
        getGetResult = getGetMethod->CallFunction(arguments);
        threadContext->SetImplicitCallFlags((Js::ImplicitCallFlags)(saveImplicitCallFlags | ImplicitCall_Accessor));

        //    9. Let targetDesc be the result of calling the[[GetOwnProperty]] internal method of target with argument P.
        //    10. ReturnIfAbrupt(targetDesc).
        //    11. If targetDesc is not undefined, then
        //    a.If IsDataDescriptor(targetDesc) and targetDesc.[[Configurable]] is false and targetDesc.[[Writable]] is false, then
        //        i.If SameValue(trapResult, targetDesc.[[Value]]) is false, then throw a TypeError exception.
        //    b.If IsAccessorDescriptor(targetDesc) and targetDesc.[[Configurable]] is false and targetDesc.[[Get]] is undefined, then
        //        i.If trapResult is not undefined, then throw a TypeError exception.
        //    12. Return trapResult.
        PropertyDescriptor targetDescriptor;
        Var defaultAccessor = requestContext->GetLibrary()->GetDefaultAccessorFunction();
        if (JavascriptOperators::GetOwnPropertyDescriptor(target, propertyId, requestContext, &targetDescriptor))
        {
            JavascriptOperators::CompletePropertyDescriptor(&targetDescriptor, nullptr, requestContext);
            if (targetDescriptor.ValueSpecified() && !targetDescriptor.IsConfigurable() && !targetDescriptor.IsWritable())
            {
                if (!JavascriptConversion::SameValue(getGetResult, targetDescriptor.GetValue()))
                {
                    JavascriptError::ThrowTypeError(requestContext, JSERR_InconsistentTrapResult, _u("get"));
                }
            }
            else if (targetDescriptor.GetterSpecified() || targetDescriptor.SetterSpecified())
            {
                if (!targetDescriptor.IsConfigurable() &&
                    targetDescriptor.GetGetter() == defaultAccessor &&
                    JavascriptOperators::GetTypeId(getGetResult) != TypeIds_Undefined)
                {
                    JavascriptError::ThrowTypeError(requestContext, JSERR_InconsistentTrapResult, _u("get"));
                }
            }
        }
        propertyDescriptor->SetValue(getGetResult);

        return TRUE;
    }

    template <class Fn, class GetPropertyIdFunc>
    BOOL JavascriptProxy::HasPropertyTrap(Fn fn, GetPropertyIdFunc getPropertyId)
    {
        PROBE_STACK(GetScriptContext(), Js::Constants::MinStackDefault);

        ScriptContext* scriptContext = GetScriptContext();

        // Reject implicit call
        ThreadContext* threadContext = scriptContext->GetThreadContext();
        if (threadContext->IsDisableImplicitCall())
        {
            threadContext->AddImplicitCallFlags(Js::ImplicitCall_External);
            return FALSE;
        }

        if (this->handler == nullptr)
        {
            // the proxy has been revoked; TypeError.
            if (!threadContext->RecordImplicitException())
                return FALSE;
            JavascriptError::ThrowTypeError(GetScriptContext(), JSERR_ErrorOnRevokedProxy, _u("has"));
        }
        JavascriptFunction* hasMethod = GetMethodHelper(PropertyIds::has, scriptContext);
        Var getHasResult;
        if (nullptr == hasMethod || GetScriptContext()->IsHeapEnumInProgress())
        {
            return fn(target);
        }

        PropertyId propertyId = getPropertyId();
        CallInfo callInfo(CallFlags_Value, 3);
        Var varArgs[3];
        Js::Arguments arguments(callInfo, varArgs);
        varArgs[0] = handler;
        varArgs[1] = target;
        varArgs[2] = GetName(scriptContext, propertyId);

        Js::ImplicitCallFlags saveImplicitCallFlags = threadContext->GetImplicitCallFlags();
        getHasResult = hasMethod->CallFunction(arguments);
        threadContext->SetImplicitCallFlags((Js::ImplicitCallFlags)(saveImplicitCallFlags | ImplicitCall_Accessor));

        //9. Let booleanTrapResult be ToBoolean(trapResult).
        //10. ReturnIfAbrupt(booleanTrapResult).
        //11. If booleanTrapResult is false, then
        //    a.Let targetDesc be the result of calling the[[GetOwnProperty]] internal method of target with argument P.
        //    b.ReturnIfAbrupt(targetDesc).
        //    c.If targetDesc is not undefined, then
        //        i.If targetDesc.[[Configurable]] is false, then throw a TypeError exception.
        //        ii.Let extensibleTarget be the result of IsExtensible(target).
        //        iii.ReturnIfAbrupt(extensibleTarget).
        //        iv.If ToBoolean(extensibleTarget) is false, then throw a TypeError exception
        BOOL hasProperty = JavascriptConversion::ToBoolean(getHasResult, scriptContext);
        if (!hasProperty)
        {
            PropertyDescriptor targetDescriptor;
            BOOL hasTargetProperty = JavascriptOperators::GetOwnPropertyDescriptor(target, propertyId, scriptContext, &targetDescriptor);
            if (hasTargetProperty)
            {
                if (!targetDescriptor.IsConfigurable() || !target->IsExtensible())
                {
                    JavascriptError::ThrowTypeError(scriptContext, JSERR_InconsistentTrapResult, _u("has"));
                }
            }
        }
        return hasProperty;
    }

    BOOL JavascriptProxy::HasProperty(PropertyId propertyId)
    {
        auto fn = [&](RecyclableObject* object)->BOOL {
            return JavascriptOperators::HasProperty(object, propertyId);
        };
        auto getPropertyId = [&]() ->PropertyId {
            return propertyId;
        };
        return HasPropertyTrap(fn, getPropertyId);
    }

    BOOL JavascriptProxy::HasOwnProperty(PropertyId propertyId)
    {
        // should never come here and it will be redirected to GetOwnPropertyDescriptor
        Assert(FALSE);
        PropertyDescriptor propertyDesc;
        return GetOwnPropertyDescriptor(this, propertyId, GetScriptContext(), &propertyDesc);
    }

    BOOL JavascriptProxy::HasOwnPropertyNoHostObject(PropertyId propertyId)
    {
        // the virtual method is for checking if globalobject has local property before we start initializing
        // we shouldn't trap??
        Assert(FALSE);
        return HasProperty(propertyId);
    }

    BOOL JavascriptProxy::HasOwnPropertyCheckNoRedecl(PropertyId propertyId)
    {
        // root object and activation object verification only; not needed.
        Assert(FALSE);
        return false;
    }

    BOOL JavascriptProxy::UseDynamicObjectForNoHostObjectAccess()
    {
        // heapenum check for CEO etc., and we don't want to access external method during enumeration. not applicable here.
        Assert(FALSE);
        return false;
    }

    DescriptorFlags JavascriptProxy::GetSetter(PropertyId propertyId, Var* setterValueOrProxy, PropertyValueInfo* info, ScriptContext* requestContext)
    {
        // This is called when we walk prototype chain looking for setter. It is part of the [[set]] operation, but we don't need to restrict the
        // code to mimic the 'one step prototype chain lookup' spec letter. Current code structure is enough.
        *setterValueOrProxy = this;
        PropertyValueInfo::SetNoCache(info, this);
        PropertyValueInfo::DisablePrototypeCache(info, this); // We can't cache prototype property either
        return DescriptorFlags::Proxy;
    }

    // GetSetter is called for
    DescriptorFlags JavascriptProxy::GetSetter(JavascriptString* propertyNameString, Var* setterValueOrProxy, PropertyValueInfo* info, ScriptContext* requestContext)
    {
        *setterValueOrProxy = this;
        PropertyValueInfo::SetNoCache(info, this);
        PropertyValueInfo::DisablePrototypeCache(info, this); // We can't cache prototype property either
        return DescriptorFlags::Proxy;
    }

    BOOL JavascriptProxy::GetProperty(Var originalInstance, PropertyId propertyId, Var* value, PropertyValueInfo* info, ScriptContext* requestContext)
    {
        // We can't cache the property at this time. both target and handler can be changed outside of the proxy, so the inline cache needs to be
        // invalidate when target, handler, or handler prototype has changed. We don't have a way to achieve this yet.
        PropertyValueInfo::SetNoCache(info, this);
        PropertyValueInfo::DisablePrototypeCache(info, this); // We can't cache prototype property either
        auto fn = [&](RecyclableObject* object)-> BOOL {
            return JavascriptOperators::GetProperty(originalInstance, object, propertyId, value, requestContext, nullptr);
        };
        auto getPropertyId = [&]()->PropertyId {return propertyId; };
        PropertyDescriptor result;
        BOOL foundProperty = GetPropertyTrap(originalInstance, &result, fn, getPropertyId, requestContext);
        if (!foundProperty)
        {
            *value = requestContext->GetMissingPropertyResult();
        }
        else if (result.IsFromProxy())
        {
            *value = GetValueFromDescriptor(RecyclableObject::FromVar(originalInstance), result, requestContext);
        }
        return foundProperty;
    }

    BOOL JavascriptProxy::GetProperty(Var originalInstance, JavascriptString* propertyNameString, Var* value, PropertyValueInfo* info, ScriptContext* requestContext)
    {
        // We can't cache the property at this time. both target and handler can be changed outside of the proxy, so the inline cache needs to be
        // invalidate when target, handler, or handler prototype has changed. We don't have a way to achieve this yet.
        PropertyValueInfo::SetNoCache(info, this);
        PropertyValueInfo::DisablePrototypeCache(info, this); // We can't cache prototype property either
        auto fn = [&](RecyclableObject* object)-> BOOL {
            return JavascriptOperators::GetPropertyWPCache(originalInstance, object, propertyNameString, value, requestContext, nullptr);
        };
        auto getPropertyId = [&]()->PropertyId{
            const PropertyRecord* propertyRecord;
            requestContext->GetOrAddPropertyRecord(propertyNameString->GetString(), propertyNameString->GetLength(), &propertyRecord);
            return propertyRecord->GetPropertyId();
        };
        PropertyDescriptor result;
        BOOL foundProperty = GetPropertyTrap(originalInstance, &result, fn, getPropertyId, requestContext);
        if (!foundProperty)
        {
            *value = requestContext->GetMissingPropertyResult();
        }
        else if (result.IsFromProxy())
        {
            *value = GetValueFromDescriptor(RecyclableObject::FromVar(originalInstance), result, requestContext);
        }
        return foundProperty;
    }

    BOOL JavascriptProxy::GetInternalProperty(Var instance, PropertyId internalPropertyId, Var* value, PropertyValueInfo* info, ScriptContext* requestContext)
    {
        if (internalPropertyId == InternalPropertyIds::WeakMapKeyMap)
        {
            return __super::GetInternalProperty(instance, internalPropertyId, value, info, requestContext);
        }
        return FALSE;
    }
  
    BOOL JavascriptProxy::GetAccessors(PropertyId propertyId, Var* getter, Var* setter, ScriptContext * requestContext)
    {
        PropertyDescriptor result;
        BOOL foundProperty = GetOwnPropertyDescriptor(this, propertyId, requestContext, &result);
        if (foundProperty && result.IsFromProxy())
        {
            if (result.GetterSpecified())
            {
                *getter = result.GetGetter();
            }
            if (result.SetterSpecified())
            {
                *setter = result.GetSetter();
            }
            foundProperty = result.GetterSpecified() || result.SetterSpecified();
        }
        return foundProperty;
    }

    BOOL JavascriptProxy::GetPropertyReference(Var originalInstance, PropertyId propertyId, Var* value, PropertyValueInfo* info, ScriptContext* requestContext)
    {
        // We can't cache the property at this time. both target and handler can be changed outside of the proxy, so the inline cache needs to be
        // invalidate when target, handler, or handler prototype has changed. We don't have a way to achieve this yet.
        PropertyValueInfo::SetNoCache(info, this);
        PropertyValueInfo::DisablePrototypeCache(info, this); // We can't cache prototype property either
        auto fn = [&](RecyclableObject* object)-> BOOL {
            return JavascriptOperators::GetPropertyReference(originalInstance, object, propertyId, value, requestContext, nullptr);
        };
        auto getPropertyId = [&]() -> PropertyId {return propertyId; };
        PropertyDescriptor result;
        BOOL foundProperty = GetPropertyTrap(originalInstance, &result, fn, getPropertyId, requestContext);
        if (!foundProperty)
        {
            *value = requestContext->GetMissingPropertyResult();
        }
        else if (result.IsFromProxy())
        {
            *value = GetValueFromDescriptor(RecyclableObject::FromVar(originalInstance), result, requestContext);
        }
        return foundProperty;
    }

    BOOL JavascriptProxy::SetProperty(PropertyId propertyId, Var value, PropertyOperationFlags flags, PropertyValueInfo* info)
    {
        // This is the second half of [[set]] where when the handler does not specified [[set]] so we forward to [[set]] on target
        // with receiver as the proxy.
        //c.Let existingDescriptor be the result of calling the[[GetOwnProperty]] internal method of Receiver with argument P.
        //d.ReturnIfAbrupt(existingDescriptor).
        //e.If existingDescriptor is not undefined, then
        //    i.Let valueDesc be the PropertyDescriptor{ [[Value]]: V }.
        //    ii.Return the result of calling the[[DefineOwnProperty]] internal method of Receiver with arguments P and valueDesc.
        //f.Else Receiver does not currently have a property P,
        //    i.Return the result of performing CreateDataProperty(Receiver, P, V).
        // We can't cache the property at this time. both target and handler can be changed outside of the proxy, so the inline cache needs to be
        // invalidate when target, handler, or handler prototype has changed. We don't have a way to achieve this yet.
        PropertyValueInfo::SetNoCache(info, this);
        PropertyValueInfo::DisablePrototypeCache(info, this); // We can't cache prototype property either

        PropertyDescriptor proxyPropertyDescriptor;
        ScriptContext* scriptContext = GetScriptContext();

        // Set implicit call flag so we bailout and not do copy-prop on field
        ThreadContext* threadContext = scriptContext->GetThreadContext();
        Js::ImplicitCallFlags saveImplicitCallFlags = threadContext->GetImplicitCallFlags();
        threadContext->SetImplicitCallFlags((Js::ImplicitCallFlags)(saveImplicitCallFlags | ImplicitCall_Accessor));

        if (!JavascriptOperators::GetOwnPropertyDescriptor(this, propertyId, scriptContext, &proxyPropertyDescriptor))
        {
            PropertyDescriptor resultDescriptor;
            resultDescriptor.SetConfigurable(true);
            resultDescriptor.SetWritable(true);
            resultDescriptor.SetEnumerable(true);
            resultDescriptor.SetValue(value);
            return Js::JavascriptOperators::DefineOwnPropertyDescriptor(this, propertyId, resultDescriptor, true, scriptContext);
        }
        else
        {
            // ES2017 Spec'ed (9.1.9.1): 
            // If existingDescriptor is not undefined, then
            //    If IsAccessorDescriptor(existingDescriptor) is true, return false.
            //    If existingDescriptor.[[Writable]] is false, return false.

            if (proxyPropertyDescriptor.IsAccessorDescriptor())
            {
                return FALSE;
            }

            if (proxyPropertyDescriptor.WritableSpecified() && !proxyPropertyDescriptor.IsWritable())
            {
                return FALSE;
            }

            proxyPropertyDescriptor.SetValue(value);
            proxyPropertyDescriptor.SetOriginal(nullptr);
            return Js::JavascriptOperators::DefineOwnPropertyDescriptor(this, propertyId, proxyPropertyDescriptor, true, scriptContext);
        }
    }

    BOOL JavascriptProxy::SetProperty(JavascriptString* propertyNameString, Var value, PropertyOperationFlags flags, PropertyValueInfo* info)
    {
        const PropertyRecord* propertyRecord;
        GetScriptContext()->GetOrAddPropertyRecord(propertyNameString->GetString(), propertyNameString->GetLength(), &propertyRecord);
        return SetProperty(propertyRecord->GetPropertyId(), value, flags, info);
    }

    BOOL JavascriptProxy::SetInternalProperty(PropertyId internalPropertyId, Var value, PropertyOperationFlags flags, PropertyValueInfo* info)
    {
        if (internalPropertyId == InternalPropertyIds::WeakMapKeyMap)
        {
            return __super::SetInternalProperty(internalPropertyId, value, flags, info);
        }
        return FALSE;
    }

    BOOL JavascriptProxy::InitProperty(PropertyId propertyId, Var value, PropertyOperationFlags flags, PropertyValueInfo* info)
    {
        return SetProperty(propertyId, value, flags, info);
    }

    BOOL JavascriptProxy::EnsureProperty(PropertyId propertyId)
    {
        // proxy needs to be explicitly constructed. we don't have Ensure code path.
        Assert(FALSE);
        return false;
    }

    BOOL JavascriptProxy::EnsureNoRedeclProperty(PropertyId propertyId)
    {
        // proxy needs to be explicitly constructed. we don't have Ensure code path.
        Assert(FALSE);
        return false;
    }

    BOOL JavascriptProxy::SetPropertyWithAttributes(PropertyId propertyId, Var value, PropertyAttributes attributes, PropertyValueInfo* info, PropertyOperationFlags flags, SideEffects possibleSideEffects)
    {
        // called from untrapped DefineProperty and from DOM side. I don't see this being used when the object is a proxy.
        Assert(FALSE);
        return false;
    }

    BOOL JavascriptProxy::InitPropertyScoped(PropertyId propertyId, Var value)
    {
        // proxy needs to be explicitly constructed. we don't have Ensure code path.
        Assert(FALSE);
        return false;
    }

    BOOL JavascriptProxy::InitFuncScoped(PropertyId propertyId, Var value)
    {
        // proxy needs to be explicitly constructed. we don't have Ensure code path.
        Assert(FALSE);
        return false;
    }

    BOOL JavascriptProxy::DeleteProperty(PropertyId propertyId, PropertyOperationFlags flags)
    {
        PROBE_STACK(GetScriptContext(), Js::Constants::MinStackDefault);

        //1. Assert: IsPropertyKey(P) is true.
        //2. Let handler be the value of the[[ProxyHandler]] internal slot of O.
        //3. If handler is null, then throw a TypeError exception.
        //6. ReturnIfAbrupt(trap).
        ScriptContext* scriptContext = GetScriptContext();
        if (this->target == nullptr)
        {
            // the proxy has been revoked; TypeError.
            JavascriptError::ThrowTypeError(scriptContext, JSERR_ErrorOnRevokedProxy, _u("deleteProperty"));
        }
        // Reject implicit call
        ThreadContext* threadContext = scriptContext->GetThreadContext();
        if (threadContext->IsDisableImplicitCall())
        {
            threadContext->AddImplicitCallFlags(Js::ImplicitCall_External);
            return FALSE;
        }
        //4. Let target be the value of the[[ProxyTarget]] internal slot of O.
        //5. Let trap be the result of GetMethod(handler, "deleteProperty").
        JavascriptFunction* deleteMethod = GetMethodHelper(PropertyIds::deleteProperty, scriptContext);
        Var deletePropertyResult;
        //7. If trap is undefined, then
        //a.Return the result of calling the[[Delete]] internal method of target with argument P.
        Assert(!GetScriptContext()->IsHeapEnumInProgress());
        if (nullptr == deleteMethod)
        {
            uint32 indexVal;
            if (scriptContext->IsNumericPropertyId(propertyId, &indexVal))
            {
                return target->DeleteItem(indexVal, flags);
            }
            else
            {
                return target->DeleteProperty(propertyId, flags);
            }
        }
        //8. Let trapResult be the result of calling the[[Call]] internal method of trap with handler as the this value and a new List containing target and P.
        //9. Let booleanTrapResult be ToBoolean(trapResult).
        //10. ReturnIfAbrupt(booleanTrapResult).
        //11. If booleanTrapResult is false, then return false.
        CallInfo callInfo(CallFlags_Value, 3);
        Var varArgs[3];
        Js::Arguments arguments(callInfo, varArgs);
        varArgs[0] = handler;
        varArgs[1] = target;
        varArgs[2] = GetName(scriptContext, propertyId);

        Js::ImplicitCallFlags saveImplicitCallFlags = threadContext->GetImplicitCallFlags();
        deletePropertyResult = deleteMethod->CallFunction(arguments);
        threadContext->SetImplicitCallFlags((Js::ImplicitCallFlags)(saveImplicitCallFlags | ImplicitCall_Accessor));

        BOOL trapResult = JavascriptConversion::ToBoolean(deletePropertyResult, scriptContext);
        if (!trapResult)
        {
            return trapResult;
        }

        //12. Let targetDesc be the result of calling the[[GetOwnProperty]] internal method of target with argument P.
        //13. ReturnIfAbrupt(targetDesc).
        //14. If targetDesc is undefined, then return true.
        //15. If targetDesc.[[Configurable]] is false, then throw a TypeError exception.
        //16. Return true.
        PropertyDescriptor targetPropertyDescriptor;
        if (!Js::JavascriptOperators::GetOwnPropertyDescriptor(target, propertyId, scriptContext, &targetPropertyDescriptor))
        {
            return TRUE;
        }
        if (!targetPropertyDescriptor.IsConfigurable())
        {
            JavascriptError::ThrowTypeError(scriptContext, JSERR_InconsistentTrapResult, _u("deleteProperty"));
        }
        return TRUE;
    }

    BOOL JavascriptProxy::DeleteProperty(JavascriptString *propertyNameString, PropertyOperationFlags flags)
    {
        PropertyRecord const *propertyRecord = nullptr;
        if (JavascriptOperators::ShouldTryDeleteProperty(this, propertyNameString, &propertyRecord))
        {
            Assert(propertyRecord);
            return DeleteProperty(propertyRecord->GetPropertyId(), flags);
        }

        return TRUE;
    }

    BOOL JavascriptProxy::IsFixedProperty(PropertyId propertyId)
    {
        // TODO: can we add support for fixed property? don't see a clear way to invalidate...
        return false;
    }

    BOOL JavascriptProxy::HasItem(uint32 index)
    {
        const PropertyRecord* propertyRecord;
        auto fn = [&](RecyclableObject* object)-> BOOL {
            return JavascriptOperators::HasItem(object, index);
        };
        auto getPropertyId = [&]() ->PropertyId {
            PropertyIdFromInt(index, &propertyRecord);
            return propertyRecord->GetPropertyId();
        };
        return HasPropertyTrap(fn, getPropertyId);
    }

    BOOL JavascriptProxy::HasOwnItem(uint32 index)
    {
        const PropertyRecord* propertyRecord;
        auto fn = [&](RecyclableObject* object)-> BOOL {
            return JavascriptOperators::HasOwnItem(object, index);
        };
        auto getPropertyId = [&]() ->PropertyId {
            PropertyIdFromInt(index, &propertyRecord);
            return propertyRecord->GetPropertyId();
        };
        return HasPropertyTrap(fn, getPropertyId);
    }

    BOOL JavascriptProxy::GetItem(Var originalInstance, uint32 index, Var* value, ScriptContext * requestContext)
    {
        const PropertyRecord* propertyRecord;
        auto fn = [&](RecyclableObject* object)-> BOOL {
            return JavascriptOperators::GetItem(originalInstance, object, index, value, requestContext);
        };
        auto getPropertyId = [&]() ->PropertyId {
            PropertyIdFromInt(index, &propertyRecord);
            return propertyRecord->GetPropertyId();
        };
        PropertyDescriptor result;
        BOOL foundProperty = GetPropertyTrap(originalInstance, &result, fn, getPropertyId, requestContext);
        if (!foundProperty)
        {
            *value = requestContext->GetMissingItemResult();
        }
        else if (result.IsFromProxy())
        {
            *value = GetValueFromDescriptor(RecyclableObject::FromVar(originalInstance), result, requestContext);
        }
        return foundProperty;
    }

    BOOL JavascriptProxy::GetItemReference(Var originalInstance, uint32 index, Var* value, ScriptContext * requestContext)
    {
        const PropertyRecord* propertyRecord;
        auto fn = [&](RecyclableObject* object)-> BOOL {
            return JavascriptOperators::GetItemReference(originalInstance, object, index, value, requestContext);
        };
        auto getPropertyId = [&]() ->PropertyId {
            PropertyIdFromInt(index, &propertyRecord);
            return propertyRecord->GetPropertyId();
        };
        PropertyDescriptor result;
        BOOL foundProperty = GetPropertyTrap(originalInstance, &result, fn, getPropertyId, requestContext);
        if (!foundProperty)
        {
            *value = requestContext->GetMissingItemResult();
        }
        else if (result.IsFromProxy())
        {
            *value = GetValueFromDescriptor(RecyclableObject::FromVar(originalInstance), result, requestContext);
        }
        return foundProperty;
    }

    DescriptorFlags JavascriptProxy::GetItemSetter(uint32 index, Var* setterValueOrProxy, ScriptContext* requestContext)
    {
        *setterValueOrProxy = this;
        return DescriptorFlags::Proxy;
    }

    BOOL JavascriptProxy::SetItem(uint32 index, Var value, PropertyOperationFlags flags)
    {
        const PropertyRecord* propertyRecord;
        PropertyIdFromInt(index, &propertyRecord);
        return SetProperty(propertyRecord->GetPropertyId(), value, flags, nullptr);
    }

    BOOL JavascriptProxy::DeleteItem(uint32 index, PropertyOperationFlags flags)
    {
        const PropertyRecord* propertyRecord;
        PropertyIdFromInt(index, &propertyRecord);
        return DeleteProperty(propertyRecord->GetPropertyId(), flags);
    }

    // No change to foreign enumerator, just forward
    BOOL JavascriptProxy::GetEnumerator(JavascriptStaticEnumerator * enumerator, EnumeratorFlags flags, ScriptContext* requestContext, ForInCache * forInCache)
    {
        // Reject implicit call
        ThreadContext* threadContext = requestContext->GetThreadContext();
        if (threadContext->IsDisableImplicitCall())
        {
            threadContext->AddImplicitCallFlags(Js::ImplicitCall_External);
            return FALSE;
        }

        // 1. Assert: Either Type(V) is Object or Type(V) is Null.
        // 2. Let handler be the value of the[[ProxyHandler]] internal slot of O.
        // 3. If handler is null, then throw a TypeError exception.
        if (this->handler == nullptr)
        {
            // the proxy has been revoked; TypeError.
            if (!threadContext->RecordImplicitException())
                return FALSE;
            JavascriptError::ThrowTypeError(GetScriptContext(), JSERR_ErrorOnRevokedProxy, _u("ownKeys"));
        }    
        
        Var propertyName = nullptr;
        PropertyId propertyId;
        int index = 0;
        JsUtil::BaseDictionary<const char16*, Var, Recycler> dict(requestContext->GetRecycler());
        JavascriptArray* arrResult = requestContext->GetLibrary()->CreateArray();

        // 13.7.5.15 EnumerateObjectProperties(O) (https://tc39.github.io/ecma262/#sec-enumerate-object-properties)
        // for (let key of Reflect.ownKeys(obj)) {    
        Var trapResult = JavascriptOperators::GetOwnPropertyNames(this, requestContext);
        if (JavascriptArray::Is(trapResult))
        {
            JavascriptStaticEnumerator trapEnumerator;
            if (!((JavascriptArray*)trapResult)->GetEnumerator(&trapEnumerator, EnumeratorFlags::SnapShotSemantics, requestContext))
            {
                return FALSE;
            }
            while ((propertyName = trapEnumerator.MoveAndGetNext(propertyId)) != NULL)
            {
                PropertyId  propId = JavascriptOperators::GetPropertyId(propertyName, requestContext);
                Var prop = JavascriptOperators::GetProperty(RecyclableObject::FromVar(trapResult), propId, requestContext);
                // if (typeof key === "string") {
                if (JavascriptString::Is(prop))
                {
                    Js::PropertyDescriptor desc;
                    JavascriptString* str = JavascriptString::FromVar(prop);
                    // let desc = Reflect.getOwnPropertyDescriptor(obj, key);
                    BOOL ret = JavascriptOperators::GetOwnPropertyDescriptor(this, str, requestContext, &desc);
                    // if (desc && !visited.has(key)) {
                    if (ret && !dict.ContainsKey(str->GetSz()))
                    {
                        dict.Add(str->GetSz(), prop);
                        // if (desc.enumerable) yield key;
                        if (desc.IsEnumerable())
                        {                            
                            ret = arrResult->SetItem(index++, CrossSite::MarshalVar(requestContext, prop), PropertyOperation_None);
                            Assert(ret);
                        }
                    }
                }
            }
        }
        else
        {
            AssertMsg(false, "Expect GetOwnPropertyNames result to be array");
        }

        return enumerator->Initialize(IteratorObjectEnumerator::Create(requestContext,
            JavascriptOperators::GetIterator(RecyclableObject::FromVar(arrResult), requestContext)), nullptr, nullptr, flags, requestContext, nullptr);

    }

    BOOL JavascriptProxy::SetAccessors(PropertyId propertyId, Var getter, Var setter, PropertyOperationFlags flags)
    {
        // should be for __definegetter style usage. need to wait for clear spec what it means.
        Assert(FALSE);
        return false;
    }

    BOOL JavascriptProxy::Equals(__in Var other, __out BOOL* value, ScriptContext* requestContext)
    {
        //RecyclableObject* targetObj;
        if (this->target == nullptr)
        {
            // the proxy has been revoked; TypeError.
            JavascriptError::ThrowTypeError(requestContext, JSERR_ErrorOnRevokedProxy, _u("equal"));
        }
        // Reject implicit call
        ThreadContext* threadContext = requestContext->GetThreadContext();
        if (threadContext->IsDisableImplicitCall())
        {
            threadContext->AddImplicitCallFlags(Js::ImplicitCall_External);
            *value = FALSE;
            return FALSE;
        }
        *value = (other == this);
        return true;
    }

    BOOL JavascriptProxy::StrictEquals(__in Var other, __out BOOL* value, ScriptContext* requestContext)
    {
        *value = FALSE;
        //RecyclableObject* targetObj;
        if (this->target == nullptr)
        {
            // the proxy has been revoked; TypeError.
            JavascriptError::ThrowTypeError(requestContext, JSERR_ErrorOnRevokedProxy, _u("strict equal"));
        }
        // Reject implicit call
        ThreadContext* threadContext = requestContext->GetThreadContext();
        if (threadContext->IsDisableImplicitCall())
        {
            threadContext->AddImplicitCallFlags(Js::ImplicitCall_External);
            return FALSE;
        }
        *value = (other == this);
        return true;
    }

    BOOL JavascriptProxy::IsWritable(PropertyId propertyId)
    {
        PropertyDescriptor propertyDescriptor;
        if (!GetOwnPropertyDescriptor(this, propertyId, GetScriptContext(), &propertyDescriptor))
        {
            return FALSE;
        }

        // If property descriptor has getter/setter we should check if writable is specified before checking IsWritable
        return propertyDescriptor.WritableSpecified() ? propertyDescriptor.IsWritable() : FALSE;
    }

    BOOL JavascriptProxy::IsConfigurable(PropertyId propertyId)
    {
        Assert(FALSE);
        return target->IsConfigurable(propertyId);
    }

    BOOL JavascriptProxy::IsEnumerable(PropertyId propertyId)
    {
        Assert(FALSE);
        return target->IsEnumerable(propertyId);
    }

    BOOL JavascriptProxy::IsExtensible()
    {
        PROBE_STACK(GetScriptContext(), Js::Constants::MinStackDefault);

        ScriptContext* scriptContext = GetScriptContext();
        // Reject implicit call
        ThreadContext* threadContext = scriptContext->GetThreadContext();
        if (threadContext->IsDisableImplicitCall())
        {
            threadContext->AddImplicitCallFlags(Js::ImplicitCall_External);
            return FALSE;
        }
        //1. Let handler be the value of the[[ProxyHandler]] internal slot of O.
        //2. If handler is null, then throw a TypeError exception.
        //3. Let target be the value of the[[ProxyTarget]] internal slot of O.
        if (this->handler == nullptr)
        {
            // the proxy has been revoked; TypeError.
            if (!threadContext->RecordImplicitException())
                return FALSE;
            JavascriptError::ThrowTypeError(GetScriptContext(), JSERR_ErrorOnRevokedProxy, _u("isExtensible"));
        }

        //4. Let trap be the result of GetMethod(handler, "isExtensible").
        //5. ReturnIfAbrupt(trap).
        //6. If trap is undefined, then
        //a.Return the result of calling the[[IsExtensible]] internal method of target.
        //7. Let trapResult be the result of calling the[[Call]] internal method of trap with handler as the this value and a new List containing target.
        //8. Let booleanTrapResult be ToBoolean(trapResult).
        //9. ReturnIfAbrupt(booleanTrapResult).
        //10. Let targetResult be the result of calling the[[IsExtensible]] internal method of target.
        //11. ReturnIfAbrupt(targetResult).
        //12. If SameValue(booleanTrapResult, targetResult) is false, then throw a TypeError exception.
        //13. Return booleanTrapResult.
        JavascriptFunction* isExtensibleMethod = GetMethodHelper(PropertyIds::isExtensible, scriptContext);
        Assert(!GetScriptContext()->IsHeapEnumInProgress());
        if (nullptr == isExtensibleMethod)
        {
            return target->IsExtensible();
        }
        CallInfo callInfo(CallFlags_Value, 2);
        Var varArgs[2];
        Js::Arguments arguments(callInfo, varArgs);
        varArgs[0] = handler;
        varArgs[1] = target;

        Js::ImplicitCallFlags saveImplicitCallFlags = threadContext->GetImplicitCallFlags();
        Var isExtensibleResult = isExtensibleMethod->CallFunction(arguments);
        threadContext->SetImplicitCallFlags((Js::ImplicitCallFlags)(saveImplicitCallFlags | ImplicitCall_Accessor));

        BOOL trapResult = JavascriptConversion::ToBoolean(isExtensibleResult, scriptContext);
        BOOL targetIsExtensible = target->IsExtensible();
        if (trapResult != targetIsExtensible)
        {
            JavascriptError::ThrowTypeError(scriptContext, JSERR_InconsistentTrapResult, _u("isExtensible"));
        }
        return trapResult;
    }

    BOOL JavascriptProxy::PreventExtensions()
    {
        PROBE_STACK(GetScriptContext(), Js::Constants::MinStackDefault);

        ScriptContext* scriptContext = GetScriptContext();
        // Reject implicit call
        ThreadContext* threadContext = scriptContext->GetThreadContext();
        if (threadContext->IsDisableImplicitCall())
        {
            threadContext->AddImplicitCallFlags(Js::ImplicitCall_External);
            return  FALSE;
        }
        //1. Let handler be the value of the[[ProxyHandler]] internal slot of O.
        //2. If handler is null, then throw a TypeError exception.
        //3. Let target be the value of the[[ProxyTarget]] internal slot of O.
        if (this->handler == nullptr)
        {
            // the proxy has been revoked; TypeError.
            if (!threadContext->RecordImplicitException())
                return FALSE;
            JavascriptError::ThrowTypeError(GetScriptContext(), JSERR_ErrorOnRevokedProxy, _u("preventExtensions"));
        }

        //4. Let trap be the result of GetMethod(handler, "preventExtensions").
        //5. ReturnIfAbrupt(trap).
        //6. If trap is undefined, then
        //a.Return the result of calling the[[PreventExtensions]] internal method of target.
        //7. Let trapResult be the result of calling the[[Call]] internal method of trap with handler as the this value and a new List containing target.
        JavascriptFunction* preventExtensionsMethod = GetMethodHelper(PropertyIds::preventExtensions, scriptContext);
        Assert(!GetScriptContext()->IsHeapEnumInProgress());
        if (nullptr == preventExtensionsMethod)
        {
            return target->PreventExtensions();
        }
        CallInfo callInfo(CallFlags_Value, 2);
        Var varArgs[2];
        Js::Arguments arguments(callInfo, varArgs);
        varArgs[0] = handler;
        varArgs[1] = target;

        //8. Let booleanTrapResult be ToBoolean(trapResult)
        //9. ReturnIfAbrupt(booleanTrapResult).
        //10. Let targetIsExtensible be the result of calling the[[IsExtensible]] internal method of target.
        //11. ReturnIfAbrupt(targetIsExtensible).
        //12. If booleanTrapResult is true and targetIsExtensible is true, then throw a TypeError exception.
        //13. Return booleanTrapResult.
        Js::ImplicitCallFlags saveImplicitCallFlags = threadContext->GetImplicitCallFlags();
        Var preventExtensionsResult = preventExtensionsMethod->CallFunction(arguments);
        threadContext->SetImplicitCallFlags((Js::ImplicitCallFlags)(saveImplicitCallFlags | ImplicitCall_Accessor));

        BOOL trapResult = JavascriptConversion::ToBoolean(preventExtensionsResult, scriptContext);
        if (trapResult)
        {
            BOOL targetIsExtensible = target->IsExtensible();
            if (targetIsExtensible)
            {
                JavascriptError::ThrowTypeError(scriptContext, JSERR_InconsistentTrapResult, _u("preventExtensions"));
            }
        }
        return trapResult;
    }

    BOOL JavascriptProxy::GetDefaultPropertyDescriptor(PropertyDescriptor& descriptor)
    {
        return target->GetDefaultPropertyDescriptor(descriptor);
    }

    // 7.3.12 in ES 2015. While this should have been no observable behavior change. Till there is obvious change warrant this
    // to be moved to JavascriptOperators, let's keep it in proxy only first.
    BOOL JavascriptProxy::TestIntegrityLevel(IntegrityLevel integrityLevel, RecyclableObject* obj, ScriptContext* scriptContext)
    {
        //1. Assert: Type(O) is Object.
        //2. Assert: level is either "sealed" or "frozen".

        //3. Let status be IsExtensible(O).
        //4. ReturnIfAbrupt(status).
        //5. If status is true, then return false
        //6. NOTE If the object is extensible, none of its properties are examined.
        BOOL isExtensible = obj->IsExtensible();
        if (isExtensible)
        {
            return FALSE;
        }

        // at this time this is called from proxy only; when we extend this to other objects, we need to handle the other codepath.
        //7. Let keys be O.[[OwnPropertyKeys]]().
        //8. ReturnIfAbrupt(keys).
        Assert(JavascriptProxy::Is(obj));
        JavascriptArray* resultArray = JavascriptOperators::GetOwnPropertyKeys(obj, scriptContext);

        //9. Repeat for each element k of keys,
        //      a. Let currentDesc be O.[[GetOwnProperty]](k).
        //      b. ReturnIfAbrupt(currentDesc).
        //      c. If currentDesc is not undefined, then
        //          i. If currentDesc.[[Configurable]] is true, return false.
        //          ii. If level is "frozen" and IsDataDescriptor(currentDesc) is true, then
        //              1. If currentDesc.[[Writable]] is true, return false.
        Var itemVar;
        bool writable = false;
        bool configurable = false;
        const PropertyRecord* propertyRecord;
        PropertyDescriptor propertyDescriptor;
        for (uint i = 0; i < resultArray->GetLength(); i++)
        {
            itemVar = resultArray->DirectGetItem(i);
            AssertMsg(JavascriptSymbol::Is(itemVar) || JavascriptString::Is(itemVar), "Invariant check during ownKeys proxy trap should make sure we only get property key here. (symbol or string primitives)");
            JavascriptConversion::ToPropertyKey(itemVar, scriptContext, &propertyRecord);
            PropertyId propertyId = propertyRecord->GetPropertyId();
            if (JavascriptObject::GetOwnPropertyDescriptorHelper(obj, propertyId, scriptContext, propertyDescriptor))
            {
                configurable |= propertyDescriptor.IsConfigurable();
                if (propertyDescriptor.IsDataDescriptor())
                {
                    writable |= propertyDescriptor.IsWritable();
                }
            }
        }
        if (integrityLevel == IntegrityLevel::IntegrityLevel_frozen && writable)
        {
            return FALSE;
        }
        if (configurable)
        {
            return FALSE;
        }
        return TRUE;
    }

    BOOL JavascriptProxy::SetIntegrityLevel(IntegrityLevel integrityLevel, RecyclableObject* obj, ScriptContext* scriptContext)
    {
        //1. Assert: Type(O) is Object.
        //2. Assert : level is either "sealed" or "frozen".
        //3. Let status be O.[[PreventExtensions]]().
        //4. ReturnIfAbrupt(status).
        //5. If status is false, return false.

        // at this time this is called from proxy only; when we extend this to other objects, we need to handle the other codepath.
        Assert(JavascriptProxy::Is(obj));
        if (obj->PreventExtensions() == FALSE)
            return FALSE;

        //6. Let keys be O.[[OwnPropertyKeys]]().
        //7. ReturnIfAbrupt(keys).
        JavascriptArray* resultArray = JavascriptOperators::GetOwnPropertyKeys(obj, scriptContext);

        const PropertyRecord* propertyRecord;        
        if (integrityLevel == IntegrityLevel::IntegrityLevel_sealed)
        {
            //8. If level is "sealed", then
                //a. Repeat for each element k of keys,
                    //i. Let status be DefinePropertyOrThrow(O, k, PropertyDescriptor{ [[Configurable]]: false }).
                    //ii. ReturnIfAbrupt(status).
            PropertyDescriptor propertyDescriptor;
            propertyDescriptor.SetConfigurable(false);
            Var itemVar;
            for (uint i = 0; i < resultArray->GetLength(); i++)
            {
                itemVar = resultArray->DirectGetItem(i);
                AssertMsg(JavascriptSymbol::Is(itemVar) || JavascriptString::Is(itemVar), "Invariant check during ownKeys proxy trap should make sure we only get property key here. (symbol or string primitives)");
                JavascriptConversion::ToPropertyKey(itemVar, scriptContext, &propertyRecord);
                PropertyId propertyId = propertyRecord->GetPropertyId();
                JavascriptObject::DefineOwnPropertyHelper(obj, propertyId, propertyDescriptor, scriptContext);
            }
        }
        else
        {
            //9.Else level is "frozen",
            //  a.Repeat for each element k of keys,
            //      i. Let currentDesc be O.[[GetOwnProperty]](k).
            //      ii. ReturnIfAbrupt(currentDesc).
            //      iii. If currentDesc is not undefined, then
            //          1. If IsAccessorDescriptor(currentDesc) is true, then
            //              a. Let desc be the PropertyDescriptor{[[Configurable]]: false}.
            //          2.Else,
            //              a. Let desc be the PropertyDescriptor { [[Configurable]]: false, [[Writable]]: false }.
            //          3. Let status be DefinePropertyOrThrow(O, k, desc).
            //          4. ReturnIfAbrupt(status).
            Assert(integrityLevel == IntegrityLevel::IntegrityLevel_frozen);
            PropertyDescriptor current, dataDescriptor, accessorDescriptor;
            dataDescriptor.SetConfigurable(false);
            dataDescriptor.SetWritable(false);
            accessorDescriptor.SetConfigurable(false);
            Var itemVar;
            for (uint i = 0; i < resultArray->GetLength(); i++)
            {
                itemVar = resultArray->DirectGetItem(i);
                AssertMsg(JavascriptSymbol::Is(itemVar) || JavascriptString::Is(itemVar), "Invariant check during ownKeys proxy trap should make sure we only get property key here. (symbol or string primitives)");
                JavascriptConversion::ToPropertyKey(itemVar, scriptContext, &propertyRecord);
                PropertyId propertyId = propertyRecord->GetPropertyId();
                PropertyDescriptor propertyDescriptor;
                if (JavascriptObject::GetOwnPropertyDescriptorHelper(obj, propertyId, scriptContext, propertyDescriptor))
                {
                    if (propertyDescriptor.IsDataDescriptor())
                    {
                        JavascriptObject::DefineOwnPropertyHelper(obj, propertyRecord->GetPropertyId(), dataDescriptor, scriptContext);
                    }
                    else if (propertyDescriptor.IsAccessorDescriptor())
                    {
                        JavascriptObject::DefineOwnPropertyHelper(obj, propertyRecord->GetPropertyId(), accessorDescriptor, scriptContext);
                    }
                }
            }
        }

        // 10. Return true
        return TRUE;
    }

    BOOL JavascriptProxy::Seal()
    {
        return SetIntegrityLevel(IntegrityLevel::IntegrityLevel_sealed, this, this->GetScriptContext());
    }

    BOOL JavascriptProxy::Freeze()
    {
        return SetIntegrityLevel(IntegrityLevel::IntegrityLevel_frozen, this, this->GetScriptContext());
    }

    BOOL JavascriptProxy::IsSealed()
    {
        return TestIntegrityLevel(IntegrityLevel::IntegrityLevel_sealed, this, this->GetScriptContext());
    }

    BOOL JavascriptProxy::IsFrozen()
    {
        return TestIntegrityLevel(IntegrityLevel::IntegrityLevel_frozen, this, this->GetScriptContext());
    }

    BOOL JavascriptProxy::SetWritable(PropertyId propertyId, BOOL value)
    {
        Assert(FALSE);
        return FALSE;
    }

    BOOL JavascriptProxy::SetConfigurable(PropertyId propertyId, BOOL value)
    {
        Assert(FALSE);
        return FALSE;
    }

    BOOL JavascriptProxy::SetEnumerable(PropertyId propertyId, BOOL value)
    {
        Assert(FALSE);
        return FALSE;
    }

    BOOL JavascriptProxy::SetAttributes(PropertyId propertyId, PropertyAttributes attributes)
    {
        Assert(FALSE);
        return FALSE;
    }

    BOOL JavascriptProxy::HasInstance(Var instance, ScriptContext* scriptContext, IsInstInlineCache* inlineCache)
    {
        Var funcPrototype = JavascriptOperators::GetProperty(this, PropertyIds::prototype, scriptContext);
        return JavascriptFunction::HasInstance(funcPrototype, instance, scriptContext, NULL, NULL);
    }

    JavascriptString* JavascriptProxy::GetClassName(ScriptContext * requestContext)
    {
        Assert(FALSE);
        return nullptr;
    }

    RecyclableObject* JavascriptProxy::GetPrototypeSpecial()
    {
        ScriptContext* scriptContext = GetScriptContext();
        // Reject implicit call
        ThreadContext* threadContext = scriptContext->GetThreadContext();
        if (threadContext->IsDisableImplicitCall())
        {
            threadContext->AddImplicitCallFlags(Js::ImplicitCall_External);
            return scriptContext->GetLibrary()->GetUndefined();
        }
        if (this->handler == nullptr)
        {
            // the proxy has been revoked; TypeError.
            if (!threadContext->RecordImplicitException())
                return nullptr;
            JavascriptError::ThrowTypeError(GetScriptContext(), JSERR_ErrorOnRevokedProxy, _u("getPrototypeOf"));
        }
        JavascriptFunction* getPrototypeOfMethod = GetMethodHelper(PropertyIds::getPrototypeOf, scriptContext);
        Var getPrototypeOfResult;
        if (nullptr == getPrototypeOfMethod || GetScriptContext()->IsHeapEnumInProgress())
        {
            return RecyclableObject::FromVar(JavascriptObject::GetPrototypeOf(target, scriptContext));
        }
        CallInfo callInfo(CallFlags_Value, 2);
        Var varArgs[2];
        Js::Arguments arguments(callInfo, varArgs);
        varArgs[0] = handler;
        varArgs[1] = target;

        Js::ImplicitCallFlags saveImplicitCallFlags = threadContext->GetImplicitCallFlags();
        getPrototypeOfResult = getPrototypeOfMethod->CallFunction(arguments);
        threadContext->SetImplicitCallFlags((Js::ImplicitCallFlags)(saveImplicitCallFlags | ImplicitCall_Accessor));

        TypeId prototypeTypeId = JavascriptOperators::GetTypeId(getPrototypeOfResult);
        if (!JavascriptOperators::IsObjectType(prototypeTypeId) && prototypeTypeId != TypeIds_Null)
        {
            JavascriptError::ThrowTypeError(scriptContext, JSERR_InconsistentTrapResult, _u("getPrototypeOf"));
        }
        if (!target->IsExtensible() && !JavascriptConversion::SameValue(getPrototypeOfResult, target->GetPrototype()))
        {
            JavascriptError::ThrowTypeError(scriptContext, JSERR_InconsistentTrapResult, _u("getPrototypeOf"));
        }
        return RecyclableObject::FromVar(getPrototypeOfResult);
    }

    RecyclableObject* JavascriptProxy::GetConfigurablePrototype(ScriptContext * requestContext)
    {
        // We should be using GetPrototypeSpecial for proxy object; never should come over here.
        Assert(FALSE);
        return nullptr;
    }

    void JavascriptProxy::RemoveFromPrototype(ScriptContext * requestContext)
    {
        Assert(FALSE);
    }

    void JavascriptProxy::AddToPrototype(ScriptContext * requestContext)
    {
        Assert(FALSE);
    }

    void JavascriptProxy::SetPrototype(RecyclableObject* newPrototype)
    {
        Assert(FALSE);
    }

    BOOL JavascriptProxy::SetPrototypeTrap(RecyclableObject* newPrototype, bool shouldThrow)
    {
        PROBE_STACK(GetScriptContext(), Js::Constants::MinStackDefault);

        Assert(JavascriptOperators::IsObjectOrNull(newPrototype));
        ScriptContext* scriptContext = GetScriptContext();
        // Reject implicit call
        ThreadContext* threadContext = scriptContext->GetThreadContext();
        if (threadContext->IsDisableImplicitCall())
        {
            threadContext->AddImplicitCallFlags(Js::ImplicitCall_External);
            return FALSE;
        }
        //1. Assert: Either Type(V) is Object or Type(V) is Null.
        //2. Let handler be the value of the[[ProxyHandler]] internal slot of O.
        //3. If handler is null, then throw a TypeError exception.
        if (this->handler == nullptr)
        {
            // the proxy has been revoked; TypeError.
            if (shouldThrow)
            {
                if (!threadContext->RecordImplicitException())
                    return FALSE;
                JavascriptError::ThrowTypeError(GetScriptContext(), JSERR_ErrorOnRevokedProxy, _u("setPrototypeOf"));
            }
        }

        //4. Let target be the value of the[[ProxyTarget]] internal slot of O.
        //5. Let trap be the result of GetMethod(handler, "setPrototypeOf").
        //6. ReturnIfAbrupt(trap).
        //7. If trap is undefined, then
        //a.Return the result of calling the[[SetPrototypeOf]] internal method of target with argument V.
        JavascriptFunction* setPrototypeOfMethod = GetMethodHelper(PropertyIds::setPrototypeOf, scriptContext);
        Assert(!GetScriptContext()->IsHeapEnumInProgress());
        if (nullptr == setPrototypeOfMethod)
        {
            JavascriptObject::ChangePrototype(target, newPrototype, shouldThrow, scriptContext);
            return TRUE;
        }
        //8. Let trapResult be the result of calling the[[Call]] internal method of trap with handler as the this value and a new List containing target and V.
        CallInfo callInfo(CallFlags_Value, 3);
        Var varArgs[3];
        Js::Arguments arguments(callInfo, varArgs);
        varArgs[0] = handler;
        varArgs[1] = target;
        varArgs[2] = newPrototype;

        Js::ImplicitCallFlags saveImplicitCallFlags = threadContext->GetImplicitCallFlags();
        Var setPrototypeResult = setPrototypeOfMethod->CallFunction(arguments);
        threadContext->SetImplicitCallFlags((Js::ImplicitCallFlags)(saveImplicitCallFlags | ImplicitCall_Accessor));

        //9. Let booleanTrapResult be ToBoolean(trapResult).
        //10. ReturnIfAbrupt(booleanTrapResult).
        //11. Let extensibleTarget be the result of IsExtensible(target).
        //12. ReturnIfAbrupt(extensibleTarget).
        //13. If extensibleTarget is true, then return booleanTrapResult.
        //14. Let targetProto be the result of calling the[[GetPrototypeOf]] internal method of target.
        //15. ReturnIfAbrupt(targetProto).
        //16. If booleanTrapResult is true and SameValue(V, targetProto) is false, then throw a TypeError exception.
        //17. Return booleanTrapResult.
        BOOL prototypeSetted = JavascriptConversion::ToBoolean(setPrototypeResult, scriptContext);
        BOOL isExtensible = target->IsExtensible();
        if (isExtensible)
        {
            if (!prototypeSetted && shouldThrow)
            {
                JavascriptError::ThrowTypeError(scriptContext, JSERR_ProxyTrapReturnedFalse, _u("setPrototypeOf"));
            }
            return prototypeSetted;
        }
        Var targetProto = target->GetPrototype();
        if (!JavascriptConversion::SameValue(targetProto, newPrototype))
        {
            if (shouldThrow)
            {
                JavascriptError::ThrowTypeError(scriptContext, JSERR_InconsistentTrapResult, _u("setPrototypeOf"));
            }
            return FALSE;
        }
        return TRUE;
    }


    Var JavascriptProxy::ToString(ScriptContext* scriptContext)
    {
        //RecyclableObject* targetObj;
        if (this->handler == nullptr)
        {
            ThreadContext* threadContext = GetScriptContext()->GetThreadContext();
            // the proxy has been revoked; TypeError.
            if (!threadContext->RecordImplicitException())
                return nullptr;
            JavascriptError::ThrowTypeError(GetScriptContext(), JSERR_ErrorOnRevokedProxy, _u("toString"));
        }
        return JavascriptObject::ToStringHelper(target, scriptContext);
    }

    BOOL JavascriptProxy::GetDiagTypeString(StringBuilder<ArenaAllocator>* stringBuilder, ScriptContext* requestContext)
    {
        //RecyclableObject* targetObj;
        if (this->handler == nullptr)
        {
            ThreadContext* threadContext = GetScriptContext()->GetThreadContext();
            // the proxy has been revoked; TypeError.
            if (!threadContext->RecordImplicitException())
                return FALSE;
            JavascriptError::ThrowTypeError(GetScriptContext(), JSERR_ErrorOnRevokedProxy, _u("getTypeString"));
        }
        return target->GetDiagTypeString(stringBuilder, requestContext);
    }

    RecyclableObject* JavascriptProxy::ToObject(ScriptContext * requestContext)
    {
        //RecyclableObject* targetObj;
        if (this->handler == nullptr)
        {
            ThreadContext* threadContext = GetScriptContext()->GetThreadContext();
            // the proxy has been revoked; TypeError.
            if (!threadContext->RecordImplicitException())
                return nullptr;
            JavascriptError::ThrowTypeError(GetScriptContext(), JSERR_ErrorOnRevokedProxy, _u("toObject"));
        }
        return __super::ToObject(requestContext);
    }

    Var JavascriptProxy::GetTypeOfString(ScriptContext* requestContext)
    {
        if (this->handler == nullptr)
        {
            // even if handler is nullptr, return typeof as "object"
            return requestContext->GetLibrary()->GetObjectTypeDisplayString();
        }
        // if exotic object has [[Call]] we should return "function", otherwise return "object"
        if (JavascriptFunction::Is(this->target))
        {
            return requestContext->GetLibrary()->GetFunctionTypeDisplayString();
        }
        else
        {
            return requestContext->GetLibrary()->GetObjectTypeDisplayString();
        }
    }

    BOOL JavascriptProxy::GetOwnPropertyDescriptor(RecyclableObject* obj, PropertyId propertyId, ScriptContext* scriptContext, PropertyDescriptor* propertyDescriptor)
    {
        JavascriptProxy* proxy = JavascriptProxy::FromVar(obj);
        auto fn = [&]()-> BOOL {
            return JavascriptOperators::GetOwnPropertyDescriptor(proxy->target, propertyId, scriptContext, propertyDescriptor);
        };
        auto getPropertyId = [&]() -> PropertyId {return propertyId; };
        BOOL foundProperty = proxy->GetPropertyDescriptorTrap(obj, fn, getPropertyId, propertyDescriptor, scriptContext);
        return foundProperty;
    }


    BOOL JavascriptProxy::DefineOwnPropertyDescriptor(RecyclableObject* obj, PropertyId propId, const PropertyDescriptor& descriptor, bool throwOnError, ScriptContext* scriptContext)
    {
        PROBE_STACK(scriptContext, Js::Constants::MinStackDefault);

        //1. Assert: IsPropertyKey(P) is true.
        //2. Let handler be the value of the[[ProxyHandler]] internal slot of O.
        //3. If handler is null, then throw a TypeError exception.
        //4. Let target be the value of the[[ProxyTarget]] internal slot of O.

        JavascriptProxy* proxy = JavascriptProxy::FromVar(obj);
        if (proxy->target == nullptr)
        {
            // the proxy has been revoked; TypeError.
            JavascriptError::ThrowTypeError(scriptContext, JSERR_ErrorOnRevokedProxy, _u("definePropertyDescriptor"));
        }
        // Reject implicit call
        ThreadContext* threadContext = scriptContext->GetThreadContext();
        if (threadContext->IsDisableImplicitCall())
        {
            threadContext->AddImplicitCallFlags(Js::ImplicitCall_External);
            return FALSE;
        }

        //5. Let trap be the result of GetMethod(handler, "defineProperty").
        //6. ReturnIfAbrupt(trap).
        //7. If trap is undefined, then
        //a.Return the result of calling the[[DefineOwnProperty]] internal method of target with arguments P and Desc.
        JavascriptFunction* defineOwnPropertyMethod = proxy->GetMethodHelper(PropertyIds::defineProperty, scriptContext);
        Var definePropertyResult;
        Assert(!scriptContext->IsHeapEnumInProgress());
        if (nullptr == defineOwnPropertyMethod)
        {
            return JavascriptOperators::DefineOwnPropertyDescriptor(proxy->target, propId, descriptor, throwOnError, scriptContext);
        }

        //8. Let descObj be FromPropertyDescriptor(Desc).
        //9. NOTE If Desc was originally generated from an object using ToPropertyDescriptor, then descObj will be that original object.
        //10. Let trapResult be the result of calling the[[Call]] internal method of trap with handler as the this value and a new List containing target, P, and descObj.
        //11. Let booleanTrapResult be ToBoolean(trapResult).
        //12. ReturnIfAbrupt(booleanTrapResult).
        //13. If booleanTrapResult is false, then return false.
        //14. Let targetDesc be the result of calling the[[GetOwnProperty]] internal method of target with argument P.
        //15. ReturnIfAbrupt(targetDesc).
        Var descVar = descriptor.GetOriginal();
        if (descVar == nullptr)
        {
            descVar = JavascriptOperators::FromPropertyDescriptor(descriptor, scriptContext);
        }

        CallInfo callInfo(CallFlags_Value, 4);
        Var varArgs[4];
        Js::Arguments arguments(callInfo, varArgs);
        varArgs[0] = proxy->handler;
        varArgs[1] = proxy->target;
        varArgs[2] = GetName(scriptContext, propId);
        varArgs[3] = descVar;

        Js::ImplicitCallFlags saveImplicitCallFlags = threadContext->GetImplicitCallFlags();
        definePropertyResult = defineOwnPropertyMethod->CallFunction(arguments);
        threadContext->SetImplicitCallFlags((Js::ImplicitCallFlags)(saveImplicitCallFlags | ImplicitCall_Accessor));

        BOOL defineResult = JavascriptConversion::ToBoolean(definePropertyResult, scriptContext);
        if (!defineResult)
        {
            return defineResult;
        }

        //16. Let extensibleTarget be the result of IsExtensible(target).
        //17. ReturnIfAbrupt(extensibleTarget).
        //18. If Desc has a[[Configurable]] field and if Desc.[[Configurable]] is false, then
        //    a.Let settingConfigFalse be true.
        //19. Else let settingConfigFalse be false.
        //20. If targetDesc is undefined, then
        //    a.If extensibleTarget is false, then throw a TypeError exception.
        //    b.If settingConfigFalse is true, then throw a TypeError exception.
        //21. Else targetDesc is not undefined,
        //    a.If IsCompatiblePropertyDescriptor(extensibleTarget, Desc, targetDesc) is false, then throw a TypeError exception.
        //    b.If settingConfigFalse is true and targetDesc.[[Configurable]] is true, then throw a TypeError exception.
        //22. Return true.
        PropertyDescriptor targetDescriptor;
        BOOL hasProperty = JavascriptOperators::GetOwnPropertyDescriptor(proxy->target, propId, scriptContext, &targetDescriptor);
        BOOL isExtensible = proxy->target->IsExtensible();
        BOOL settingConfigFalse = (descriptor.ConfigurableSpecified() && !descriptor.IsConfigurable());
        if (!hasProperty)
        {
            if (!isExtensible || settingConfigFalse)
            {
                JavascriptError::ThrowTypeError(scriptContext, JSERR_InconsistentTrapResult, _u("defineProperty"));
            }
        }
        else
        {
            if (!JavascriptOperators::IsCompatiblePropertyDescriptor(descriptor, hasProperty? &targetDescriptor : nullptr, !!isExtensible, true, scriptContext))
            {
                JavascriptError::ThrowTypeError(scriptContext, JSERR_InconsistentTrapResult, _u("defineProperty"));
            }
            if (settingConfigFalse && targetDescriptor.IsConfigurable())
            {
                JavascriptError::ThrowTypeError(scriptContext, JSERR_InconsistentTrapResult, _u("defineProperty"));
            }
        }
        return TRUE;
    }


    BOOL JavascriptProxy::SetPropertyTrap(Var receiver, SetPropertyTrapKind setPropertyTrapKind, Js::JavascriptString * propertyNameString, Var newValue, ScriptContext* requestContext)
    {
        const PropertyRecord* propertyRecord;
        requestContext->GetOrAddPropertyRecord(propertyNameString->GetString(), propertyNameString->GetLength(), &propertyRecord);
        return SetPropertyTrap(receiver, setPropertyTrapKind, propertyRecord->GetPropertyId(), newValue, requestContext);

    }

    BOOL JavascriptProxy::SetPropertyTrap(Var receiver, SetPropertyTrapKind setPropertyTrapKind, PropertyId propertyId, Var newValue, ScriptContext* requestContext, BOOL skipPrototypeCheck)
    {
        PROBE_STACK(GetScriptContext(), Js::Constants::MinStackDefault);

        //1. Assert: IsPropertyKey(P) is true.
        //2. Let handler be the value of the[[ProxyHandler]] internal slot of O.
        //3. If handler is undefined, then throw a TypeError exception.
        //4. Let target be the value of the[[ProxyTarget]] internal slot of O.

        ScriptContext* scriptContext = GetScriptContext();
        if (this->target == nullptr)
        {
            // the proxy has been revoked; TypeError.
            JavascriptError::ThrowTypeError(scriptContext, JSERR_ErrorOnRevokedProxy, _u("set"));
        }
        // Reject implicit call
        ThreadContext* threadContext = scriptContext->GetThreadContext();
        if (threadContext->IsDisableImplicitCall())
        {
            threadContext->AddImplicitCallFlags(Js::ImplicitCall_External);
            return FALSE;
        }
        //5. Let trap be the result of GetMethod(handler, "set").
        //6. ReturnIfAbrupt(trap).
        //7. If trap is undefined, then
        //a.Return the result of calling the[[Set]] internal method of target with arguments P, V, and Receiver.
        JavascriptFunction* setMethod = GetMethodHelper(PropertyIds::set, requestContext);
        Var setPropertyResult;
        Assert(!GetScriptContext()->IsHeapEnumInProgress());
        if (nullptr == setMethod)
        {
            PropertyValueInfo info;
            switch (setPropertyTrapKind)
            {
            case SetPropertyTrapKind::SetItemOnTaggedNumberKind:
            {
                uint32 indexVal;
                BOOL isNumericPropertyId = scriptContext->IsNumericPropertyId(propertyId, &indexVal);
                Assert(isNumericPropertyId);
                return JavascriptOperators::SetItemOnTaggedNumber(receiver, this->target, indexVal, newValue, requestContext, PropertyOperationFlags::PropertyOperation_None);
            }
            case SetPropertyTrapKind::SetPropertyOnTaggedNumberKind:
                return JavascriptOperators::SetPropertyOnTaggedNumber(receiver, this->target, propertyId, newValue, requestContext, PropertyOperation_None);
            case SetPropertyTrapKind::SetPropertyKind:
                return JavascriptOperators::SetProperty(receiver, target, propertyId, newValue, requestContext);
            case SetPropertyTrapKind::SetItemKind:
            {
                uint32 indexVal;
                BOOL isNumericPropertyId = scriptContext->IsNumericPropertyId(propertyId, &indexVal);
                Assert(isNumericPropertyId);
                return  JavascriptOperators::SetItem(receiver, target, indexVal, newValue, scriptContext, PropertyOperationFlags::PropertyOperation_None, skipPrototypeCheck);
            }
            case SetPropertyTrapKind::SetPropertyWPCacheKind:
                return JavascriptOperators::SetPropertyWPCache(receiver, target, propertyId, newValue, requestContext,
                    static_cast<PropertyString*>(GetName(requestContext, propertyId)), PropertyOperationFlags::PropertyOperation_None);
            default:
                Assert(FALSE);
            }
        }
        //8. Let trapResult be the result of calling the[[Call]] internal method of trap with handler as the this value and a new List containing target, P, V, and Receiver.
        //9. Let booleanTrapResult be ToBoolean(trapResult).
        //10. ReturnIfAbrupt(booleanTrapResult).
        //11. If booleanTrapResult is false, then return false.

        CallInfo callInfo(CallFlags_Value, 5);
        Var varArgs[5];
        Js::Arguments arguments(callInfo, varArgs);
        varArgs[0] = handler;
        varArgs[1] = target;
        varArgs[2] = GetName(scriptContext, propertyId);
        varArgs[3] = newValue;
        varArgs[4] = receiver;

        Js::ImplicitCallFlags saveImplicitCallFlags = threadContext->GetImplicitCallFlags();
        setPropertyResult = setMethod->CallFunction(arguments);
        threadContext->SetImplicitCallFlags((Js::ImplicitCallFlags)(saveImplicitCallFlags | ImplicitCall_Accessor));

        BOOL setResult = JavascriptConversion::ToBoolean(setPropertyResult, requestContext);
        if (!setResult)
        {
            return setResult;
        }

        //12. Let targetDesc be the result of calling the[[GetOwnProperty]] internal method of target with argument P.
        //13. ReturnIfAbrupt(targetDesc).
        //14. If targetDesc is not undefined, then
        //a.If IsDataDescriptor(targetDesc) and targetDesc.[[Configurable]] is false and targetDesc.[[Writable]] is false, then
        //i.If SameValue(V, targetDesc.[[Value]]) is false, then throw a TypeError exception.
        //b.If IsAccessorDescriptor(targetDesc) and targetDesc.[[Configurable]] is false, then
        //i.If targetDesc.[[Set]] is undefined, then throw a TypeError exception.
        //15. Return true
        PropertyDescriptor targetDescriptor;
        BOOL hasProperty;

        hasProperty = JavascriptOperators::GetOwnPropertyDescriptor(target, propertyId, requestContext, &targetDescriptor);
        if (hasProperty)
        {
            if (targetDescriptor.ValueSpecified())
            {
                if (!targetDescriptor.IsConfigurable() && !targetDescriptor.IsWritable() &&
                    !JavascriptConversion::SameValue(newValue, targetDescriptor.GetValue()))
                {
                    JavascriptError::ThrowTypeError(scriptContext, JSERR_InconsistentTrapResult, _u("set"));
                }
            }
            else
            {
                if (!targetDescriptor.IsConfigurable() && targetDescriptor.GetSetter() == requestContext->GetLibrary()->GetDefaultAccessorFunction())
                {
                    JavascriptError::ThrowTypeError(scriptContext, JSERR_InconsistentTrapResult, _u("set"));
                }
            }
        }
        return TRUE;

    }

    JavascriptFunction* JavascriptProxy::GetMethodHelper(PropertyId methodId, ScriptContext* requestContext)
    {
        //2. Let handler be the value of the[[ProxyHandler]] internal slot of O.
        //3. If handler is null, then throw a TypeError exception.
        if (this->target == nullptr)
        {
            // the proxy has been revoked; TypeError.
            JavascriptError::ThrowTypeError(requestContext, JSERR_ErrorOnRevokedProxy, requestContext->GetPropertyName(methodId)->GetBuffer());
        }
        Var varMethod;
        //5. Let trap be the result of GetMethod(handler, "getOwnPropertyDescriptor").
        //6. ReturnIfAbrupt(trap).

        //7.3.9 GetMethod(V, P)
        //  The abstract operation GetMethod is used to get the value of a specific property of an ECMAScript language value when the value of the
        //  property is expected to be a function. The operation is called with arguments V and P where V is the ECMAScript language value, P is the
        //  property key. This abstract operation performs the following steps:
        //  1. Assert: IsPropertyKey(P) is true.
        //  2. Let func be ? GetV(V, P).
        //  3. If func is either undefined or null, return undefined.
        //  4. If IsCallable(func) is false, throw a TypeError exception.
        //  5. Return func.
        BOOL result = JavascriptOperators::GetPropertyReference(handler, methodId, &varMethod, requestContext);
        if (!result || JavascriptOperators::IsUndefinedOrNull(varMethod))
        {
            return nullptr;
        }
        if (!JavascriptFunction::Is(varMethod))
        {
            JavascriptError::ThrowTypeError(requestContext, JSERR_NeedFunction, requestContext->GetPropertyName(methodId)->GetBuffer());
        }

        varMethod = CrossSite::MarshalVar(requestContext, varMethod);

        return JavascriptFunction::FromVar(varMethod);
    }

    Var JavascriptProxy::GetValueFromDescriptor(RecyclableObject* instance, PropertyDescriptor propertyDescriptor, ScriptContext* requestContext)
    {
        if (propertyDescriptor.ValueSpecified())
        {
            return CrossSite::MarshalVar(requestContext, propertyDescriptor.GetValue());
        }
        if (propertyDescriptor.GetterSpecified())
        {
            return JavascriptOperators::CallGetter(RecyclableObject::FromVar(propertyDescriptor.GetGetter()), instance, requestContext);
        }
        Assert(FALSE);
        return requestContext->GetLibrary()->GetUndefined();
    }

    void JavascriptProxy::PropertyIdFromInt(uint32 index, PropertyRecord const** propertyRecord)
    {
        char16 buffer[20];

        ::_i64tow_s(index, buffer, sizeof(buffer) / sizeof(char16), 10);

        GetScriptContext()->GetOrAddPropertyRecord((LPCWSTR)buffer, static_cast<int>(wcslen(buffer)), propertyRecord);
    }

    Var JavascriptProxy::GetName(ScriptContext* requestContext, PropertyId propertyId)
    {
        const PropertyRecord* propertyRecord = requestContext->GetThreadContext()->GetPropertyName(propertyId);
        Var name;
        if (propertyRecord->IsSymbol())
        {
            name = requestContext->GetLibrary()->CreateSymbol(propertyRecord);
        }
        else
        {
            name = requestContext->GetLibrary()->CreatePropertyString(propertyRecord);
        }
        return name;
    }

#ifdef ENABLE_DEBUG_CONFIG_OPTIONS
    PropertyId JavascriptProxy::EnsureHandlerPropertyId(ScriptContext* scriptContext)
    {
        ThreadContext* threadContext = scriptContext->GetThreadContext();
        if (threadContext->handlerPropertyId == Js::Constants::NoProperty)
        {
            LPCWSTR autoProxyName;

            if (threadContext->GetAutoProxyName() != nullptr)
            {
                autoProxyName = threadContext->GetAutoProxyName();
            }
            else
            {
                autoProxyName = Js::Configuration::Global.flags.autoProxy;
            }

            threadContext->handlerPropertyId = threadContext->GetOrAddPropertyRecordBind(
                JsUtil::CharacterBuffer<WCHAR>(autoProxyName, static_cast<charcount_t>(wcslen(autoProxyName))))->GetPropertyId();
        }
        return threadContext->handlerPropertyId;
    }

    RecyclableObject* JavascriptProxy::AutoProxyWrapper(Var obj)
    {
        RecyclableObject* object = RecyclableObject::FromVar(obj);
        if (!JavascriptOperators::IsObject(object) || JavascriptProxy::Is(object))
        {
            return object;
        }
        ScriptContext* scriptContext = object->GetScriptContext();
        if (!scriptContext->GetThreadContext()->IsScriptActive())
        {
            return object;
        }
        if (!scriptContext->GetConfig()->IsES6ProxyEnabled())
        {
            return object;
        }
        Assert(Js::Configuration::Global.flags.IsEnabled(Js::autoProxyFlag));
        PropertyId handlerId = EnsureHandlerPropertyId(scriptContext);
        GlobalObject* globalObject = scriptContext->GetLibrary()->GetGlobalObject();
        Var handler = nullptr;
        if (!JavascriptOperators::GetProperty(globalObject, handlerId, &handler, scriptContext))
        {
            handler = scriptContext->GetLibrary()->CreateObject();
            JavascriptOperators::SetProperty(globalObject, globalObject, handlerId, handler, scriptContext);
        }
        CallInfo callInfo(CallFlags_Value, 3);
        Var varArgs[3];
        Js::Arguments arguments(callInfo, varArgs);
        varArgs[0] = scriptContext->GetLibrary()->GetProxyConstructor();
        varArgs[1] = object;
        varArgs[2] = handler;
        return Create(scriptContext, arguments);
    }
#endif

    Var JavascriptProxy::ConstructorTrap(Arguments args, ScriptContext* scriptContext, const Js::AuxArray<uint32> *spreadIndices)
    {
        PROBE_STACK(GetScriptContext(), Js::Constants::MinStackDefault);

        Var functionResult;
        if (spreadIndices != nullptr)
        {
            functionResult = JavascriptFunction::CallSpreadFunction(this, args, spreadIndices);
        }
        else
        {
            functionResult = JavascriptFunction::CallFunction<true>(this, this->GetEntryPoint(), args);
        }
        return CrossSite::MarshalVar(scriptContext, functionResult);
    }

    Var JavascriptProxy::FunctionCallTrap(RecyclableObject* function, CallInfo callInfo, ...)
    {
        PROBE_STACK(function->GetScriptContext(), Js::Constants::MinStackDefault);

        ARGUMENTS(args, callInfo);
        ScriptContext* scriptContext = function->GetScriptContext();

        BOOL hasOverridingNewTarget = callInfo.Flags & CallFlags_NewTarget;
        bool isCtorSuperCall = (callInfo.Flags & CallFlags_New) && args[0] != nullptr && RecyclableObject::Is(args[0]);

        AssertMsg(args.Info.Count > 0, "Should always have implicit 'this'");
        if (!JavascriptProxy::Is(function))
        {
            if (args.Info.Flags & CallFlags_New)
            {
                JavascriptError::ThrowTypeError(scriptContext, JSERR_NeedFunction, _u("construct"));
            }
            else
            {
                JavascriptError::ThrowTypeError(scriptContext, JSERR_NeedFunction, _u("call"));
            }
        }

        Var newTarget = nullptr;
        JavascriptProxy* proxy = JavascriptProxy::FromVar(function);
        JavascriptFunction* callMethod;
        Assert(!scriptContext->IsHeapEnumInProgress());

        // To conform with ES6 spec 7.3.13
        if (hasOverridingNewTarget)
        {
            newTarget = args.Values[callInfo.Count];
        }
        else
        {
            newTarget = proxy;
        }

        if (args.Info.Flags & CallFlags_New)
        {
            callMethod = proxy->GetMethodHelper(PropertyIds::construct, scriptContext);
        }
        else
        {
            callMethod = proxy->GetMethodHelper(PropertyIds::apply, scriptContext);
        }
        if (!JavascriptConversion::IsCallable(proxy->target))
        {
            JavascriptError::ThrowTypeError(scriptContext, JSERR_NeedFunction, _u("call"));
        }

        if (nullptr == callMethod)
        {
            // newCount is ushort. If args count is greater than or equal to 65535, an integer
            // too many arguments
            if (args.Info.Count >= USHORT_MAX) //check against CallInfo::kMaxCountArgs if newCount is ever made int
            {
                JavascriptError::ThrowRangeError(scriptContext, JSERR_ArgListTooLarge);
            }

            // in [[construct]] case, we don't need to check if the function is a constructor: the function should throw there.
            Var newThisObject = nullptr;
            if (args.Info.Flags & CallFlags_New)
            {
                if (!JavascriptOperators::IsConstructor(proxy->target))
                {
                    JavascriptError::ThrowTypeError(scriptContext, JSERR_This_NeedFunction, _u("construct"));
                }
                newThisObject = JavascriptOperators::NewScObjectNoCtor(proxy->target, scriptContext);
                args.Values[0] = newThisObject;
            }

            ushort newCount = (ushort)(args.Info.Count + 1);
            Var* newValues;
            const unsigned STACK_ARGS_ALLOCA_THRESHOLD = 8; // Number of stack args we allow before using _alloca
            Var stackArgs[STACK_ARGS_ALLOCA_THRESHOLD];
            if (newCount > STACK_ARGS_ALLOCA_THRESHOLD)
            {
                PROBE_STACK(scriptContext, newCount * sizeof(Var) + Js::Constants::MinStackDefault); // args + function call
                newValues = (Var*)_alloca(newCount * sizeof(Var));
            }
            else
            {
                newValues = stackArgs;
            }
            CallInfo calleeInfo((CallFlags)(args.Info.Flags | CallFlags_ExtraArg | CallFlags_NewTarget), newCount);

            for (uint argCount = 0; argCount < args.Info.Count; argCount++)
            {
                newValues[argCount] = args.Values[argCount];
            }
#pragma prefast(suppress:6386)
            newValues[args.Info.Count] = newTarget;

            Js::Arguments arguments(calleeInfo, newValues);
            Var aReturnValue = JavascriptFunction::CallFunction<true>(proxy->target, proxy->target->GetEntryPoint(), arguments);
            // If this is constructor call, return the actual object instead of function result
            if ((callInfo.Flags & CallFlags_New) && !JavascriptOperators::IsObject(aReturnValue))
            {
                aReturnValue = newThisObject;
            }
            return aReturnValue;
        }

        JavascriptArray* argList = scriptContext->GetLibrary()->CreateArray(callInfo.Count - 1);
        for (uint i = 1; i < callInfo.Count; i++)
        {
            argList->DirectSetItemAt(i - 1, args[i]);
        }

        Var varArgs[4];
        CallInfo calleeInfo(CallFlags_Value, 4);
        Js::Arguments arguments(calleeInfo, varArgs);

        varArgs[0] = proxy->handler;
        varArgs[1] = proxy->target;
        if (args.Info.Flags & CallFlags_New)
        {
            varArgs[2] = argList;
            // 1st preference - overridden newTarget
            // 2nd preference - 'this' in case of super() call
            // 3rd preference - newTarget ( which is same as F)
            varArgs[3] = hasOverridingNewTarget ? newTarget :
                isCtorSuperCall ? args[0] : newTarget;
         }
        else
        {
            varArgs[2] = args[0];
            varArgs[3] = argList;
        }

        Var trapResult = callMethod->CallFunction(arguments);
        if (args.Info.Flags & CallFlags_New)
        {
            if (!Js::JavascriptOperators::IsObject(trapResult))
            {
                JavascriptError::ThrowTypeError(scriptContext, JSERR_InconsistentTrapResult, _u("construct"));
            }
        }
        return trapResult;
    }

    JavascriptArray* JavascriptProxy::PropertyKeysTrap(KeysTrapKind keysTrapKind, ScriptContext* requestContext)
    {
        PROBE_STACK(GetScriptContext(), Js::Constants::MinStackDefault);

        ScriptContext* scriptContext = GetScriptContext();
        // Reject implicit call
        ThreadContext* threadContext = scriptContext->GetThreadContext();
        if (threadContext->IsDisableImplicitCall())
        {
            threadContext->AddImplicitCallFlags(Js::ImplicitCall_External);
            return nullptr;
        }
        //1. Let handler be the value of the[[ProxyHandler]] internal slot of O.
        //2. If handler is null, throw a TypeError exception.
        //3. Assert: Type(handler) is Object.
        if (this->handler == nullptr)
        {
            // the proxy has been revoked; TypeError.
            if (!threadContext->RecordImplicitException())
                return nullptr;
            JavascriptError::ThrowTypeError(GetScriptContext(), JSERR_ErrorOnRevokedProxy, _u("ownKeys"));
        }
        AssertMsg(JavascriptOperators::IsObject(this->handler), "Handler should be object.");

        //4. Let target be the value of the[[ProxyTarget]] internal slot of O.
        //5. Let trap be GetMethod(handler, "ownKeys").
        //6. ReturnIfAbrupt(trap).
        //7. If trap is undefined, then
        //      a. Return target.[[OwnPropertyKeys]]().
        JavascriptFunction* ownKeysMethod = GetMethodHelper(PropertyIds::ownKeys, requestContext);
        Assert(!GetScriptContext()->IsHeapEnumInProgress());

        JavascriptArray *targetKeys;

        if (nullptr == ownKeysMethod)
        {
            switch (keysTrapKind)
            {
                case GetOwnPropertyNamesKind:
                    targetKeys = JavascriptOperators::GetOwnPropertyNames(this->target, scriptContext);
                    break;
                case GetOwnPropertySymbolKind:
                    targetKeys = JavascriptOperators::GetOwnPropertySymbols(this->target, scriptContext);
                    break;
                case KeysKind:
                    targetKeys = JavascriptOperators::GetOwnPropertyKeys(this->target, scriptContext);
                    break;
                default:
                    AssertMsg(false, "Invalid KeysTrapKind.");
                    return scriptContext->GetLibrary()->CreateArray(0);
            }
            return targetKeys;
        }

        //8. Let trapResultArray be Call(trap, handler, <<target>>).
        //9. Let trapResult be CreateListFromArrayLike(trapResultArray, <<String, Symbol>>).
        //10. ReturnIfAbrupt(trapResult).
        //11. Let extensibleTarget be IsExtensible(target).
        //12. ReturnIfAbrupt(extensibleTarget).
        //13. Let targetKeys be target.[[OwnPropertyKeys]]().
        //14. ReturnIfAbrupt(targetKeys).
        CallInfo callInfo(CallFlags_Value, 2);
        Var varArgs[2];
        Js::Arguments arguments(callInfo, varArgs);
        varArgs[0] = handler;
        varArgs[1] = target;

        Js::ImplicitCallFlags saveImplicitCallFlags = threadContext->GetImplicitCallFlags();
        Var ownKeysResult = ownKeysMethod->CallFunction(arguments);
        threadContext->SetImplicitCallFlags((Js::ImplicitCallFlags)(saveImplicitCallFlags | ImplicitCall_Accessor));

        if (!JavascriptOperators::IsObject(ownKeysResult))
        {
            JavascriptError::ThrowTypeError(scriptContext, JSERR_InconsistentTrapResult, _u("ownKeys"));
        }
        RecyclableObject* trapResultArray = RecyclableObject::FromVar(ownKeysResult);

        BOOL isTargetExtensible = target->IsExtensible();

        targetKeys = JavascriptOperators::GetOwnPropertyKeys(this->target, scriptContext);

        //15. Assert: targetKeys is a List containing only String and Symbol values.
        //16. Let targetConfigurableKeys be an empty List.
        //17. Let targetNonconfigurableKeys be an empty List.
        //18. Repeat, for each element key of targetKeys,
        //    a.Let desc be target.[[GetOwnProperty]](key).
        //    b.ReturnIfAbrupt(desc).
        //    c.If desc is not undefined and desc.[[Configurable]] is false, then
        //         i.Append key as an element of targetNonconfigurableKeys.
        //    d.Else,
        //         i.Append key as an element of targetConfigurableKeys.
        //19. If extensibleTarget is true and targetNonconfigurableKeys is empty, then
        //    a. Return trapResult.
        //20. Let uncheckedResultKeys be a new List which is a copy of trapResult.
        //21. Repeat, for each key that is an element of targetNonconfigurableKeys,
        //      a. If key is not an element of uncheckedResultKeys, throw a TypeError exception.
        //      b. Remove key from uncheckedResultKeys
        //22. If extensibleTarget is true, return trapResult.

        /*
        To avoid creating targetConfigurableKeys, targetNonconfigurableKeys and uncheckedResultKeys list in above steps,
        use below algorithm to accomplish same behavior

        // Track if there are any properties that are present in target but not present in trap result
        for(var i = 0; i < trapResult.length; i++)
        {
            PropertyId propId = GetPropertyId(trapResult[i]);
            if(propId != NoProperty) { targetToTrapResultMap[propId] = 1; }
            else { isTrapResultMissingFromTargetKeys = true; }
        }

        isConfigurableKeyMissingFromTrapResult = false;
        isNonconfigurableKeyMissingFromTrapResult = false;
        for(var i = 0; i < targetKeys.length; i++)
        {
            PropertyId propId = GetPropertyId(targetKeys[i]);
            Var desc = GetPropertyDescriptor(propId);
            if(targetToTrapResultMap[propId]) {
                delete targetToTrapResultMap[propId];
                isMissingFromTrapResult = false;
            } else {
                isMissingFromTrapResult = true;
            }
            if(desc->IsConfigurable()) {
                if(isMissingFromTrapResult) {
                    isConfigurableKeyMissingFromTrapResult = true;
                }
            } else {
                isAnyNonconfigurableKeyPresent = true
                if(isMissingFromTrapResult) {
                    isNonconfigurableKeyMissingFromTrapResult = true;
                }
            }
        }

        // 19.
        if(isExtensible && !isAnyNonconfigurableKeyPresent) { return trapResult; }

        // 21.
        if(isNonconfigurableKeyMissingFromTrapResult) { throw TypeError; }

        // 22.
        if(isExtensible) { return trapResult; }

        // 23.
        if(isConfigurableKeyMissingFromTrapResult)  { throw TypeError; }

        // 24.
        if(!targetToTrapResultMap.Empty()) { throw TypeError; }

        return trapResult;
        */

        JavascriptArray* trapResult = scriptContext->GetLibrary()->CreateArray(0);
        bool isConfigurableKeyMissingFromTrapResult = false;
        bool isNonconfigurableKeyMissingFromTrapResult = false;
        bool isKeyMissingFromTrapResult = false;
        bool isKeyMissingFromTargetResult = false;
        bool isAnyNonconfigurableKeyPresent = false;
        Var element;
        PropertyId propertyId;
        const PropertyRecord* propertyRecord = nullptr;

        BEGIN_TEMP_ALLOCATOR(tempAllocator, scriptContext, _u("Runtime"))
        {
            // Dictionary containing intersection of keys present in targetKeys and trapResult
            Var lenValue = JavascriptOperators::OP_GetLength(trapResultArray, scriptContext);
            uint32 len = (uint32)JavascriptConversion::ToLength(lenValue, scriptContext);

            JsUtil::BaseDictionary<Js::PropertyId, bool, ArenaAllocator> targetToTrapResultMap(tempAllocator, len);

            // Trap result to return.
            // Note : This will not necessarily have all elements present in trapResultArray. E.g. If trap was called from GetOwnPropertySymbols()
            // trapResult will only contain symbol elements from trapResultArray.

            switch (keysTrapKind)
            {
            case GetOwnPropertyNamesKind:
                GetOwnPropertyKeysHelper(scriptContext, trapResultArray, len, trapResult, targetToTrapResultMap,
                    [&](const PropertyRecord *propertyRecord)->bool
                {
                    return !propertyRecord->IsSymbol();
                });
                break;
            case GetOwnPropertySymbolKind:
                GetOwnPropertyKeysHelper(scriptContext, trapResultArray, len, trapResult, targetToTrapResultMap,
                    [&](const PropertyRecord *propertyRecord)->bool
                {
                    return propertyRecord->IsSymbol();
                });
                break;
            case KeysKind:
                GetOwnPropertyKeysHelper(scriptContext, trapResultArray, len, trapResult, targetToTrapResultMap,
                    [&](const PropertyRecord *propertyRecord)->bool
                {
                    return true;
                });
                break;
            }

            for (uint32 i = 0; i < targetKeys->GetLength(); i++)
            {
                element = targetKeys->DirectGetItem(i);
                AssertMsg(JavascriptSymbol::Is(element) || JavascriptString::Is(element), "Invariant check during ownKeys proxy trap should make sure we only get property key here. (symbol or string primitives)");
                JavascriptConversion::ToPropertyKey(element, scriptContext, &propertyRecord);
                propertyId = propertyRecord->GetPropertyId();

                if (propertyId == Constants::NoProperty)
                    continue;

                // If not present in intersection means either the property is not present in targetKeys or
                // we have already visited the property in targetKeys
                if (targetToTrapResultMap.ContainsKey(propertyId))
                {
                    isKeyMissingFromTrapResult = false;
                    targetToTrapResultMap.Remove(propertyId);
                }
                else
                {
                    isKeyMissingFromTrapResult = true;
                }

                PropertyDescriptor targetKeyPropertyDescriptor;
                if (Js::JavascriptOperators::GetOwnPropertyDescriptor(target, propertyId, scriptContext, &targetKeyPropertyDescriptor) && !targetKeyPropertyDescriptor.IsConfigurable())
                {
                    isAnyNonconfigurableKeyPresent = true;
                    if (isKeyMissingFromTrapResult)
                    {
                        isNonconfigurableKeyMissingFromTrapResult = true;
                    }
                }
                else
                {
                    if (isKeyMissingFromTrapResult)
                    {
                        isConfigurableKeyMissingFromTrapResult = true;
                    }
                }
            }
            // Keys that were not found in targetKeys will continue to remain in the map
            isKeyMissingFromTargetResult = targetToTrapResultMap.Count() != 0;
        }
        END_TEMP_ALLOCATOR(tempAllocator, scriptContext)


        // 19.
        if (isTargetExtensible && !isAnyNonconfigurableKeyPresent)
        {
            return trapResult;
        }

        // 21.
        if (isNonconfigurableKeyMissingFromTrapResult)
        {
            JavascriptError::ThrowTypeError(scriptContext, JSERR_InconsistentTrapResult, _u("ownKeys"));
        }

        // 22.
        if (isTargetExtensible)
        {
            return trapResult;
        }

        // 23.
        if (isConfigurableKeyMissingFromTrapResult)
        {
            JavascriptError::ThrowTypeError(scriptContext, JSERR_InconsistentTrapResult, _u("ownKeys"));
        }

        // 24.
        if (isKeyMissingFromTargetResult)
        {
            JavascriptError::ThrowTypeError(scriptContext, JSERR_InconsistentTrapResult, _u("ownKeys"));
        }

        return trapResult;
    }

#if ENABLE_TTD
    void JavascriptProxy::MarkVisitKindSpecificPtrs(TTD::SnapshotExtractor* extractor)
    {
        if(this->handler != nullptr)
        {
            extractor->MarkVisitVar(this->handler);
        }

        if(this->target != nullptr)
        {
            extractor->MarkVisitVar(this->target);
        }
    }

    TTD::NSSnapObjects::SnapObjectType JavascriptProxy::GetSnapTag_TTD() const
    {
        return TTD::NSSnapObjects::SnapObjectType::SnapProxyObject;
    }

    void JavascriptProxy::ExtractSnapObjectDataInto(TTD::NSSnapObjects::SnapObject* objData, TTD::SlabAllocator& alloc)
    {
        TTD::NSSnapObjects::SnapProxyInfo* spi = alloc.SlabAllocateStruct<TTD::NSSnapObjects::SnapProxyInfo>();

        const uint32 reserveSize = 2;
        uint32 depOnCount = 0;
        TTD_PTR_ID* depOnArray = alloc.SlabReserveArraySpace<TTD_PTR_ID>(reserveSize);

        spi->HandlerId = TTD_INVALID_PTR_ID;
        if(this->handler != nullptr)
        {
            spi->HandlerId = TTD_CONVERT_VAR_TO_PTR_ID(this->handler);

            if(TTD::JsSupport::IsVarComplexKind(this->handler))
            {
                depOnArray[depOnCount] = TTD_CONVERT_VAR_TO_PTR_ID(this->handler);
                depOnCount++;
            }
        }

        spi->TargetId = TTD_INVALID_PTR_ID;
        if(this->target != nullptr)
        {
            spi->TargetId = TTD_CONVERT_VAR_TO_PTR_ID(this->target);

            if(TTD::JsSupport::IsVarComplexKind(this->handler))
            {
                depOnArray[depOnCount] = TTD_CONVERT_VAR_TO_PTR_ID(this->target);
                depOnCount++;
            }
        }

        if(depOnCount == 0)
        {
            alloc.SlabAbortArraySpace<TTD_PTR_ID>(reserveSize);

            TTD::NSSnapObjects::StdExtractSetKindSpecificInfo<TTD::NSSnapObjects::SnapProxyInfo*, TTD::NSSnapObjects::SnapObjectType::SnapProxyObject>(objData, spi);
        }
        else
        {
            alloc.SlabCommitArraySpace<TTD_PTR_ID>(depOnCount, reserveSize);

            TTD::NSSnapObjects::StdExtractSetKindSpecificInfo<TTD::NSSnapObjects::SnapProxyInfo*, TTD::NSSnapObjects::SnapObjectType::SnapProxyObject>(objData, spi, alloc, depOnCount, depOnArray);
        }
    }
#endif
}
