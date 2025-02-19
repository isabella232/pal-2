/*
 ***********************************************************************************************************************
 *
 *  Copyright (c) 2015-2021 Advanced Micro Devices, Inc. All Rights Reserved.
 *
 *  Permission is hereby granted, free of charge, to any person obtaining a copy
 *  of this software and associated documentation files (the "Software"), to deal
 *  in the Software without restriction, including without limitation the rights
 *  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 *  copies of the Software, and to permit persons to whom the Software is
 *  furnished to do so, subject to the following conditions:
 *
 *  The above copyright notice and this permission notice shall be included in all
 *  copies or substantial portions of the Software.
 *
 *  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 *  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 *  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 *  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 *  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 *  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 *  SOFTWARE.
 *
 **********************************************************************************************************************/

#pragma once

#include <limits.h>
#include "palDevice.h"
#include "palIndirectCmdGenerator.h"
#include "core/gpuMemory.h"
#include "core/hw/gfxip/gfxCmdBuffer.h"

namespace Pal
{

class GfxCmdBuffer;
class GfxDevice;
class Pipeline;

// Enumerates the types of indirect commands being generated by a specific generator.
enum class GeneratorType : uint32
{
    Dispatch = 0,
    Draw,
    DrawIndexed,
    DispatchMesh,
};

// Contains properties of a specific command generator.
// NOTE: This *must* be compatible with the same-named structure inside core/hw/gfxip/rpm/gfx6/globals.hlsl !
struct GeneratorProperties
{
    // Set of magic values which the command generator will recognize inside a BindIndexDataIndirectArgs structure
    // to choose an index-buffer type: [0] = 8 bit indices, [1] = 16 bit indices, [2] = 32 bit indices.
    uint32  indexTypeTokens[3];
    // Number of user-data entry mappings per shader stage.
    uint32  maxUserDataEntries;
    // Index of the last user-data entry modified by this command Generator, plus one. Zero indicateas that the
    // generator does not modify user-data entries.
    uint32  userDataWatermark;
    // Size (in DWORDs) of the vertex buffer table. The command Generator will only generate commands to update
    // the vertex buffer table when this is nonzero.
    uint32  vertexBufTableSize;

    uint32  cmdBufStride; // Stride (in bytes) of the generated command buffer per indirect command.
    uint32  argBufStride; // Stride (in bytes) of the argument buffer per indirect command.

    GfxIpLevel  gfxLevel; // GFX IP level for the parent Device.
};

// Contains properties of a specific CmdExecuteIndirectCmds() invocation.
// NOTE: This *must* be compatible with the same-named structure inside core/hw/gfxip/rpm/gfx6/globals.hlsl !
struct InvocationProperties
{
    uint32  maximumCmdCount;    // Maximum number of draw or dispatch commands
    uint32  indexBufSize;       // Maximum number of indices in the bound index buffer
    uint32  argumentBufAddr[2]; // Argument buffer GPU address

    // Hardware-specific data
    union
    {
        struct
        {
            uint32  indexBufMType;      // MTYPE value for index buffer bindings
            uint32  dimInThreads;       // Should dispatch commands be in terms of threads(1) or thread-groups(0)?
            uint32  padding[2];
            uint32  threadsPerGroup[3]; // Compute thread-group dimensions. Ignored for graphics commands.
        } gfx6;

        struct
        {
            uint32  dispatchInitiator;  // COMPUTE_DISPATCH_INITIATOR value for CS dispatches
        } gfx9;
    };
};

// =====================================================================================================================
// Indirect command generator objects are used to generate command buffer chunks on the GPU. These command buffer chunks
// are able to issue draws or dispatches, change the index buffer binding, change user-data entry values, etc. PAL's
// implementation uses a compute shader managed by RPM which reads data describing the application-specified layout of
// the input buffer which the shader uses to generate PM4.
//
// IndirectCmdGenerator is an object which contains some of the data necessary for RPM's shader(s) to correctly
// interpret the application's input data and translate it to the corresponding PM4 stream.
class IndirectCmdGenerator : public IIndirectCmdGenerator
{
public:
    static Result ValidateCreateInfo(
        const IndirectCmdGeneratorCreateInfo& createInfo);

    virtual void Destroy() override;

    virtual void GetGpuMemoryRequirements(
        GpuMemoryRequirements* pGpuMemReqs) const override;

    virtual Result BindGpuMemory(
        IGpuMemory* pGpuMemory,
        gpusize     offset) override;

    virtual uint32 CmdBufStride(
        const Pipeline* pPipeline) const = 0;

    // Helper method for RPM to populate an embedded-data constant buffer with the InvocationProperties associated
    // with this command-generator and the given Pipeline object.
    virtual void PopulateInvocationBuffer(
        GfxCmdBuffer*   pCmdBuffer,
        const Pipeline* pPipeline,
        bool            isTaskEnabled,
        gpusize         argsGpuAddr,
        uint32          maximumCount,
        uint32          indexBufSize,
        void*           pSrd) const = 0;

    // Helper method for RPM to populate an embedded data constant buffer with the parameter data
    // for the currently bound compute or graphics pipeline (depending on the value of m_drawType).
    virtual void PopulateParameterBuffer(
        GfxCmdBuffer*   pCmdBuffer,
        const Pipeline* pPipeline,
        void*           pSrd) const = 0;

    // Helper method for RPM to populate an embedded data constant buffer with the generator property
    // for the currently bound compute or graphics pipeline (depending on the value of m_drawType).
    virtual void PopulatePropertyBuffer(
        GfxCmdBuffer*   pCmdBuffer,
        const Pipeline* pPipeline,
        void*           pSrd) const = 0;

    // Helper method for RPM to populate an embedded data constant buffer with the hardware layer's pipeline signature
    // for the currently bound compute or graphics pipeline (depending on the value of m_drawType).
    virtual void PopulateSignatureBuffer(
        GfxCmdBuffer*   pCmdBuffer,
        const Pipeline* pPipeline,
        void*           pSrd) const = 0;

    // Helper method for RPM to populate an embedded data typed buffer with the contents of the user-data entry
    // remapping table for each shader stage in the active pipeline.
    virtual void PopulateUserDataMappingBuffer(
        GfxCmdBuffer*   pCmdBuffer,
        const Pipeline* pPipeline,
        void*           pSrd) const = 0;

    const BoundGpuMemory& Memory() const { return m_gpuMemory; }

    GeneratorType Type() const { return m_type; }
    uint32 ParameterCount() const { return m_paramCount; }

    const GeneratorProperties& Properties() const { return m_properties; }

    const uint32* PropertiesSrd() const { return &m_propertiesSrd[0]; }
    const uint32* ParamBufferSrd() const { return &m_paramBufSrd[0]; }

    const UserDataFlags& TouchedUserDataEntries() const { return m_touchedUserData; }

protected:
    IndirectCmdGenerator(
        const GfxDevice&                      device,
        const IndirectCmdGeneratorCreateInfo& createInfo);
    virtual ~IndirectCmdGenerator() { }

    const GfxDevice&  m_device;

    GeneratorProperties  m_properties;

    BoundGpuMemory  m_gpuMemory;
    gpusize         m_gpuMemSize;

    uint32  m_propertiesSrd[4]; // Buffer SRD for the GeneratorProperties structure
    uint32  m_paramBufSrd[4];   // Buffer SRD for the Indirect-Parameter array

    // Wide-bitfield of user-data entries touched by the generated commands which this generator creates.
    UserDataFlags  m_touchedUserData;

private:
    const GeneratorType  m_type;
    const uint32         m_paramCount;

    PAL_DISALLOW_DEFAULT_CTOR(IndirectCmdGenerator);
    PAL_DISALLOW_COPY_AND_ASSIGN(IndirectCmdGenerator);
};

} // Pal
