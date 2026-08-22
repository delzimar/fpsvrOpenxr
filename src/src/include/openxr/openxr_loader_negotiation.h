#ifndef OPENXR_LOADER_NEGOTIATION_H_
#define OPENXR_LOADER_NEGOTIATION_H_ 1

/*
** Copyright 2017-2026 The Khronos Group Inc.
**
** SPDX-License-Identifier: Apache-2.0 OR MIT
*/

#include "openxr.h"

#ifdef __cplusplus
extern "C" {
#endif

#define XR_LOADER_VERSION_1_0 1
#define XR_CURRENT_LOADER_API_LAYER_VERSION 1
#define XR_CURRENT_LOADER_RUNTIME_VERSION 1

#define XR_LOADER_INFO_STRUCT_VERSION 1
#define XR_API_LAYER_INFO_STRUCT_VERSION 1
#define XR_RUNTIME_INFO_STRUCT_VERSION 1
#define XR_API_LAYER_NEXT_INFO_STRUCT_VERSION 1
#define XR_API_LAYER_CREATE_INFO_STRUCT_VERSION 1

#define XR_API_LAYER_MAX_SETTINGS_PATH_SIZE 512

#ifndef XR_MAY_ALIAS
#define XR_MAY_ALIAS
#endif

typedef enum XrLoaderInterfaceStructs {
    XR_LOADER_INTERFACE_STRUCT_UNINTIALIZED = 0,
    XR_LOADER_INTERFACE_STRUCT_LOADER_INFO = 1,
    XR_LOADER_INTERFACE_STRUCT_API_LAYER_REQUEST = 2,
    XR_LOADER_INTERFACE_STRUCT_RUNTIME_REQUEST = 3,
    XR_LOADER_INTERFACE_STRUCT_API_LAYER_CREATE_INFO = 4,
    XR_LOADER_INTERFACE_STRUCT_API_LAYER_NEXT_INFO = 5,
    XR_LOADER_INTERFACE_STRUCTS_MAX_ENUM = 0x7FFFFFFF
} XrLoaderInterfaceStructs;

typedef struct XrApiLayerCreateInfo XrApiLayerCreateInfo;
typedef XrResult (XRAPI_PTR *PFN_xrCreateApiLayerInstance)(
    const XrInstanceCreateInfo* info,
    const XrApiLayerCreateInfo* apiLayerInfo,
    XrInstance* instance);

typedef struct XrApiLayerNextInfo {
    XrLoaderInterfaceStructs        structType;
    uint32_t                        structVersion;
    size_t                          structSize;
    char                            layerName[XR_MAX_API_LAYER_NAME_SIZE];
    PFN_xrGetInstanceProcAddr       nextGetInstanceProcAddr;
    PFN_xrCreateApiLayerInstance    nextCreateApiLayerInstance;
    struct XrApiLayerNextInfo*      next;
} XrApiLayerNextInfo;

typedef struct XrApiLayerCreateInfo {
    XrLoaderInterfaceStructs    structType;
    uint32_t                    structVersion;
    size_t                      structSize;
    void* XR_MAY_ALIAS          loaderInstance;
    char                        settings_file_location[XR_API_LAYER_MAX_SETTINGS_PATH_SIZE];
    XrApiLayerNextInfo*         nextInfo;
} XrApiLayerCreateInfo;

typedef struct XrNegotiateApiLayerRequest {
    XrLoaderInterfaceStructs        structType;
    uint32_t                        structVersion;
    size_t                          structSize;
    uint32_t                        layerInterfaceVersion;
    XrVersion                       layerApiVersion;
    PFN_xrGetInstanceProcAddr       getInstanceProcAddr;
    PFN_xrCreateApiLayerInstance    createApiLayerInstance;
} XrNegotiateApiLayerRequest;

typedef struct XrNegotiateLoaderInfo {
    XrLoaderInterfaceStructs    structType;
    uint32_t                    structVersion;
    size_t                      structSize;
    uint32_t                    minInterfaceVersion;
    uint32_t                    maxInterfaceVersion;
    XrVersion                   minApiVersion;
    XrVersion                   maxApiVersion;
} XrNegotiateLoaderInfo;

typedef struct XrNegotiateRuntimeRequest {
    XrLoaderInterfaceStructs     structType;
    uint32_t                     structVersion;
    size_t                       structSize;
    uint32_t                     runtimeInterfaceVersion;
    XrVersion                    runtimeApiVersion;
    PFN_xrGetInstanceProcAddr    getInstanceProcAddr;
} XrNegotiateRuntimeRequest;

typedef XrResult (XRAPI_PTR *PFN_xrNegotiateLoaderRuntimeInterface)(const XrNegotiateLoaderInfo* loaderInfo, XrNegotiateRuntimeRequest* runtimeRequest);
typedef XrResult (XRAPI_PTR *PFN_xrNegotiateLoaderApiLayerInterface)(const XrNegotiateLoaderInfo* loaderInfo, const char* layerName, XrNegotiateApiLayerRequest* apiLayerRequest);

#ifdef __cplusplus
}
#endif

#endif // OPENXR_LOADER_NEGOTIATION_H_
