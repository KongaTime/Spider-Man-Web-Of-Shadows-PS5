/* SPDX-License-Identifier: LGPL-2.1-or-later
 * Uses the shader interface from vkd3d-proton's d3d12_rov.c
 * (Copyright 2023 Hans-Kristian Arntzen for Valve Corporation).
 */
#include "rov-shaders.h"

static int rov_draw(ID3D12Device *device)
{
    ID3D12CommandQueue *queue;
    D3D12_COMMAND_QUEUE_DESC qd = { .Type = D3D12_COMMAND_LIST_TYPE_DIRECT };
    HR(ID3D12Device_CreateCommandQueue(device, &qd, &IID_ID3D12CommandQueue, (void **)&queue));
    ID3D12CommandAllocator *allocator;
    HR(ID3D12Device_CreateCommandAllocator(device, D3D12_COMMAND_LIST_TYPE_DIRECT, &IID_ID3D12CommandAllocator, (void **)&allocator));
    ID3D12GraphicsCommandList *list;
    HR(ID3D12Device_CreateCommandList(device, 0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator, NULL,
        &IID_ID3D12GraphicsCommandList, (void **)&list));
    D3D12_DESCRIPTOR_RANGE range = { .RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV, .NumDescriptors = 1 };
    D3D12_ROOT_PARAMETER parameters[2] = {0};
    parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    parameters[0].DescriptorTable.NumDescriptorRanges = 1;
    parameters[0].DescriptorTable.pDescriptorRanges = &range;
    parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    parameters[1].Constants.Num32BitValues = 8;
    parameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    D3D12_ROOT_SIGNATURE_DESC rs = { .NumParameters = 2, .pParameters = parameters };
    ID3DBlob *blob;
    HR(vkd3d_serialize_root_signature(&rs, D3D_ROOT_SIGNATURE_VERSION_1_0, &blob, NULL));
    ID3D12RootSignature *signature;
    HR(ID3D12Device_CreateRootSignature(device, 0, ID3D10Blob_GetBufferPointer(blob),
        ID3D10Blob_GetBufferSize(blob), &IID_ID3D12RootSignature, (void **)&signature));
    ID3D10Blob_Release(blob);
    D3D12_GRAPHICS_PIPELINE_STATE_DESC pd = {0};
    pd.pRootSignature = signature;
    pd.VS = (D3D12_SHADER_BYTECODE){vs_code_dxbc, sizeof(vs_code_dxbc)};
    pd.PS = (D3D12_SHADER_BYTECODE){ps_code_single_dxbc, sizeof(ps_code_single_dxbc)};
    pd.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    pd.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    pd.RasterizerState.DepthClipEnable = TRUE;
    pd.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pd.SampleDesc.Count = 1; pd.SampleMask = UINT32_MAX;
    ID3D12PipelineState *pipeline;
    HR(ID3D12Device_CreateGraphicsPipelineState(device, &pd, &IID_ID3D12PipelineState, (void **)&pipeline));
    D3D12_RESOURCE_DESC rd = { .Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D,
        .Width = 4, .Height = 4, .DepthOrArraySize = 1, .MipLevels = 1, .Format = DXGI_FORMAT_R32_FLOAT,
        .SampleDesc = {1, 0}, .Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS };
    D3D12_HEAP_PROPERTIES hp = { .Type = D3D12_HEAP_TYPE_DEFAULT, .CreationNodeMask = 1, .VisibleNodeMask = 1 };
    ID3D12Resource *texture;
    HR(ID3D12Device_CreateCommittedResource(device, &hp, D3D12_HEAP_FLAG_NONE, &rd,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS, NULL, &IID_ID3D12Resource, (void **)&texture));
    D3D12_DESCRIPTOR_HEAP_DESC hd = { .Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
        .NumDescriptors = 1, .Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE };
    ID3D12DescriptorHeap *descriptors;
    HR(ID3D12Device_CreateDescriptorHeap(device, &hd, &IID_ID3D12DescriptorHeap, (void **)&descriptors));
    D3D12_CPU_DESCRIPTOR_HANDLE cpu = ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(descriptors);
    D3D12_GPU_DESCRIPTOR_HANDLE gpu = ID3D12DescriptorHeap_GetGPUDescriptorHandleForHeapStart(descriptors);
    D3D12_UNORDERED_ACCESS_VIEW_DESC uav = { .Format = DXGI_FORMAT_R32_FLOAT, .ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D };
    ID3D12Device_CreateUnorderedAccessView(device, texture, NULL, &uav, cpu);
    ID3D12GraphicsCommandList_SetDescriptorHeaps(list, 1, &descriptors);
    float zero[4] = {0};
    ID3D12GraphicsCommandList_ClearUnorderedAccessViewFloat(list, gpu, cpu, texture, zero, 0, NULL);
    D3D12_RESOURCE_BARRIER barrier = { .Type = D3D12_RESOURCE_BARRIER_TYPE_UAV, .UAV.pResource = texture };
    ID3D12GraphicsCommandList_ResourceBarrier(list, 1, &barrier);
    ID3D12GraphicsCommandList_SetGraphicsRootSignature(list, signature);
    ID3D12GraphicsCommandList_SetGraphicsRootDescriptorTable(list, 0, gpu);
    const float constants[8] = {0, 0, 4, 4, 2, 1, .25f, .25f};
    ID3D12GraphicsCommandList_SetGraphicsRoot32BitConstants(list, 1, 8, constants, 0);
    D3D12_VIEWPORT viewport = {0, 0, 4, 4, 0, 1}; D3D12_RECT scissor = {0, 0, 4, 4};
    ID3D12GraphicsCommandList_RSSetViewports(list, 1, &viewport);
    ID3D12GraphicsCommandList_RSSetScissorRects(list, 1, &scissor);
    ID3D12GraphicsCommandList_IASetPrimitiveTopology(list, D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    ID3D12GraphicsCommandList_SetPipelineState(list, pipeline);
    ID3D12GraphicsCommandList_DrawInstanced(list, 4, 10, 0, 0);
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition = (D3D12_RESOURCE_TRANSITION_BARRIER){texture, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE};
    ID3D12GraphicsCommandList_ResourceBarrier(list, 1, &barrier);
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint;
    UINT64 bytes;
    ID3D12Device_GetCopyableFootprints(device, &rd, 0, 1, 0, &footprint, NULL, NULL, &bytes);
    rd = (D3D12_RESOURCE_DESC){ .Dimension = D3D12_RESOURCE_DIMENSION_BUFFER, .Width = bytes,
        .Height = 1, .DepthOrArraySize = 1, .MipLevels = 1, .SampleDesc = {1, 0}, .Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR };
    hp.Type = D3D12_HEAP_TYPE_READBACK;
    ID3D12Resource *readback;
    HR(ID3D12Device_CreateCommittedResource(device, &hp, D3D12_HEAP_FLAG_NONE, &rd,
        D3D12_RESOURCE_STATE_COPY_DEST, NULL, &IID_ID3D12Resource, (void **)&readback));
    D3D12_TEXTURE_COPY_LOCATION from = { .pResource = texture, .Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX };
    D3D12_TEXTURE_COPY_LOCATION to = { .pResource = readback, .Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT,
        .PlacedFootprint = footprint };
    ID3D12GraphicsCommandList_CopyTextureRegion(list, &to, 0, 0, 0, &from, NULL);
    HR(ID3D12GraphicsCommandList_Close(list));
    ID3D12CommandList *submitted = (ID3D12CommandList *)list;
    ID3D12CommandQueue_ExecuteCommandLists(queue, 1, &submitted);
    ID3D12Fence *fence;
    HR(ID3D12Device_CreateFence(device, 0, D3D12_FENCE_FLAG_NONE, &IID_ID3D12Fence, (void **)&fence));
    HR(ID3D12CommandQueue_Signal(queue, fence, 1));
    unsigned attempt;
    for (attempt = 0; attempt < 10000 && ID3D12Fence_GetCompletedValue(fence) < 1; ++attempt) usleep(1000);
    if (attempt == 10000 || ID3D12Fence_GetCompletedValue(fence) == UINT64_MAX) { LOG("FAIL ROV fence\n"); return 1; }
    unsigned char *mapped;
    D3D12_RANGE read_range = {0, bytes}, empty = {0, 0};
    HR(ID3D12Resource_Map(readback, 0, &read_range, (void **)&mapped));
    unsigned errors = 0;
    for (unsigned y = 0; y < 4; ++y) for (unsigned x = 0; x < 4; ++x) {
        float value = ((float *)(mapped + footprint.Offset + y * footprint.Footprint.RowPitch))[x];
        if (value != 1023.0f) { ++errors; LOG("ROV mismatch x=%u y=%u actual=%f expected=1023\n", x, y, value); }
    }
    ID3D12Resource_Unmap(readback, 0, &empty);
    LOG("D3D12_ROV_DRAW pixels=16 overlapping_instances=10 mismatches=%u\n", errors);
    ID3D12Fence_Release(fence); ID3D12Resource_Release(readback); ID3D12Resource_Release(texture);
    ID3D12PipelineState_Release(pipeline); ID3D12RootSignature_Release(signature);
    ID3D12DescriptorHeap_Release(descriptors); ID3D12GraphicsCommandList_Release(list);
    ID3D12CommandAllocator_Release(allocator); ID3D12CommandQueue_Release(queue);
    return errors != 0;
}
