#include "irr/asset/ICPUShader.h"
#include "spirv_cross/spirv_parser.hpp"
#include "spirv_cross/spirv_cross.hpp"


namespace irr { namespace asset
{

namespace
{
E_SHADER_STAGE spvExecModel2ESS(spv::ExecutionModel _em)
{
    using namespace spv;
    switch (_em)
    {
    case ExecutionModelVertex: return ESS_VERTEX;
    case ExecutionModelTessellationControl: return ESS_TESSELATION_CONTROL;
    case ExecutionModelTessellationEvaluation: return ESS_TESSELATION_EVALUATION;
    case ExecutionModelGeometry: return ESS_GEOMETRY;
    case ExecutionModelFragment: return ESS_FRAGMENT;
    case ExecutionModelGLCompute: return ESS_COMPUTE;
    default: return ESS_UNKNOWN;
    }
}
spv::ExecutionModel ESS2spvExecModel(E_SHADER_STAGE _ss)
{
    using namespace spv;
    switch (_ss)
    {
    case ESS_VERTEX: return ExecutionModelVertex;
    case ESS_TESSELATION_CONTROL: return ExecutionModelTessellationControl;
    case ESS_TESSELATION_EVALUATION: return ExecutionModelTessellationEvaluation;
    case ESS_GEOMETRY: return ExecutionModelGeometry;
    case ESS_FRAGMENT: return ExecutionModelFragment;
    case ESS_COMPUTE: return ExecutionModelGLCompute;
    default: return ExecutionModelMax;
    }
}
}

void ICPUShader::enableIntrospection()
{
    if (m_introspectionCache.size()) // already enabled
        return;

    spirv_cross::Compiler comp(m_parsed->getUnderlyingRepresentation());
    auto eps = getStageEntryPoints(comp);

    SIntrospectionPerformer introPerformer;
    for (const auto& ep : eps)
        m_introspectionCache.emplace(ep, introPerformer.doIntrospection(comp, ep));
}

auto ICPUShader::getStageEntryPoints() -> const core::vector<SEntryPointStagePair>&
{
    if (m_entryPoints.size())
        return m_entryPoints;

    spirv_cross::Compiler comp(m_parsed->getUnderlyingRepresentation());

    return getStageEntryPoints(comp);
}

auto ICPUShader::getStageEntryPoints(spirv_cross::Compiler& _comp) -> const core::vector<SEntryPointStagePair>&
{
    if (m_entryPoints.size())
        return m_entryPoints;

    auto eps = _comp.get_entry_points_and_stages();
    m_entryPoints.reserve(eps.size());

    for (const auto& ep : eps)
        m_entryPoints.emplace_back(ep.name, spvExecModel2ESS(ep.execution_model));
    std::sort(m_entryPoints.begin(), m_entryPoints.end());

    return m_entryPoints;
}

SIntrospectionData ICPUShader::SIntrospectionPerformer::doIntrospection(spirv_cross::Compiler& _comp, const SEntryPointStagePair& _ep) const
{
    spv::ExecutionModel stage = ESS2spvExecModel(_ep.second);
    if (stage == spv::ExecutionModelMax)
        return SIntrospectionData();

    SIntrospectionData introData;
    introData.pushConstant.present = false;
    auto addResource_common = [&introData, &_comp] (const spirv_cross::Resource& r, E_SHADER_RESOURCE_TYPE restype) -> SShaderResourceVariant& {
        const uint32_t descSet = _comp.get_decoration(r.id, spv::DecorationDescriptorSet);
        assert(descSet < 4u);
        introData.descriptorSetBindings[descSet].emplace_back();

        SShaderResourceVariant& res = introData.descriptorSetBindings[descSet].back();
        res.type = restype;
        res.binding = _comp.get_decoration(r.id, spv::DecorationBinding);

        res.descriptorCount = 1u;
        res.descCountIsSpecConstant = false;
        const spirv_cross::SPIRType& type = _comp.get_type(r.type_id);
        // assuming only 1D arrays because i don't know how desc set layout binding is constructed when it's let's say 2D array (e.g. uniform sampler2D smplr[4][5]; is it even legal?)
        if (type.array.size()) // is array
        {
            res.descriptorCount = type.array[0]; // ID of spec constant if size is spec constant
            res.descCountIsSpecConstant = !type.array_size_literal[0];
        }

        return res;
    };
    auto addInfo_common = [&introData, &_comp](const spirv_cross::Resource& r, E_SHADER_INFO_TYPE type) ->SShaderInfoVariant& {
        introData.inputOutput.emplace_back();
        SShaderInfoVariant& info = introData.inputOutput.back();
        info.type = type;
        info.location = _comp.get_decoration(r.id, spv::DecorationLocation);
        return info;
    };

    _comp.set_entry_point(_ep.first, stage);
    spirv_cross::ShaderResources resources = _comp.get_shader_resources(_comp.get_active_interface_variables());
    for (const spirv_cross::Resource& r : resources.uniform_buffers)
    {
        SShaderResourceVariant& res = addResource_common(r, ESRT_UNIFORM_BUFFER);
        shaderMemBlockIntrospection(_comp, static_cast<impl::SShaderMemoryBlock&>(res.get<ESRT_UNIFORM_BUFFER>()), r.base_type_id, r.id);
    }
    for (const spirv_cross::Resource& r : resources.storage_buffers)
    {
        SShaderResourceVariant& res = addResource_common(r, ESRT_STORAGE_BUFFER);
        shaderMemBlockIntrospection(_comp, static_cast<impl::SShaderMemoryBlock&>(res.get<ESRT_STORAGE_BUFFER>()), r.base_type_id, r.id);
    }
    for (const spirv_cross::Resource& r : resources.subpass_inputs)
    {
        SShaderResourceVariant& res = addResource_common(r, ESRT_INPUT_ATTACHMENT);
        res.get<ESRT_INPUT_ATTACHMENT>().inputAttachmentIndex = _comp.get_decoration(r.id, spv::DecorationInputAttachmentIndex);
    }
    for (const spirv_cross::Resource& r : resources.storage_images)
    {
        const spirv_cross::SPIRType type = _comp.get_type(r.id);
        const bool buffer = type.image.dim == spv::DimBuffer;
        SShaderResourceVariant& res = addResource_common(r, buffer ? ESRT_STORAGE_TEXEL_BUFFER : ESRT_STORAGE_IMAGE);
    }
    for (const spirv_cross::Resource& r : resources.sampled_images)
    {
        SShaderResourceVariant& res = addResource_common(r, ESRT_COMBINED_IMAGE_SAMPLER);
    }
    for (const spirv_cross::Resource& r : resources.separate_images)
    {
        const spirv_cross::SPIRType type = _comp.get_type(r.id);
        const bool buffer = type.image.dim == spv::DimBuffer;
        SShaderResourceVariant& res = addResource_common(r, buffer ? ESRT_UNIFORM_TEXEL_BUFFER : ESRT_SAMPLED_IMAGE);
    }
    for (const spirv_cross::Resource& r : resources.separate_samplers)
    {
        SShaderResourceVariant& res = addResource_common(r, ESRT_SAMPLER);
    }

    for (const spirv_cross::Resource& r : resources.stage_inputs)
    {
        SShaderInfoVariant& res = addInfo_common(r, ESIT_STAGE_INPUT);
    }
    for (const spirv_cross::Resource& r : resources.stage_outputs)
    {
        SShaderInfoVariant& res = addInfo_common(r, ESIT_STAGE_OUTPUT);
        res.get<ESIT_STAGE_OUTPUT>().colorIndex = _comp.get_decoration(r.id, spv::DecorationIndex);
    }
    if (resources.push_constant_buffers.size())
    {
        const spirv_cross::Resource& r = resources.push_constant_buffers.front();
        introData.pushConstant.present = true;
        shaderMemBlockIntrospection(_comp, static_cast<impl::SShaderMemoryBlock&>(introData.pushConstant.info), r.base_type_id, r.id);
    }
    
    // spec constants
    std::vector<spirv_cross::SpecializationConstant> sconsts = _comp.get_specialization_constants();
    introData.specConstants.resize(sconsts.size());
    for (size_t i = 0u; i < sconsts.size(); ++i)
    {
        SIntrospectionData::SSpecConstant& specConst = introData.specConstants[i];
        specConst.id = sconsts[i].constant_id;
        specConst.name = _comp.get_name(sconsts[i].id);

        const spirv_cross::SPIRType& type = _comp.get_type(sconsts[i].id);
        specConst.byteSize = calcBytesizeforType(_comp, type);
    }
}

void ICPUShader::SIntrospectionPerformer::shaderMemBlockIntrospection(spirv_cross::Compiler& _comp, impl::SShaderMemoryBlock& _res, uint32_t _blockBaseTypeID, uint32_t _varID) const
{
    const spirv_cross::SPIRType& type = _comp.get_type(_blockBaseTypeID);
    const uint32_t memberCnt = type.member_types.size();
    _res.members.resize(memberCnt);

    for (uint32_t m = 0u; m < memberCnt; ++m)
    {
        const spirv_cross::SPIRType& mtype = _comp.get_type(type.member_types[m]);
        impl::SShaderMemoryBlock::SMember& member = _res.members[m];

        member.size = _comp.get_declared_struct_member_size(type, m);
        member.offset = _comp.type_struct_member_offset(type, m);

        if (mtype.array.size())
        {
            member.count = mtype.array[0];
            member.arrayStride = _comp.type_struct_member_array_stride(type, m);
        }

        member.mtxRowCnt = mtype.vecsize;
        member.mtxColCnt = mtype.columns;
        if (member.mtxColCnt > 1u)
            member.mtxStride = _comp.type_struct_member_matrix_stride(type, m);
    }
    using MembT = impl::SShaderMemoryBlock::SMember;
    std::sort(_res.members.begin(), _res.members.end(), [](const MembT& _lhs, const MembT& _rhs) { _lhs.offset < _rhs.offset; });

    _res.size = _res.rtSizedArrayOneElementSize = _comp.get_declared_struct_size(type);
    const spirv_cross::SPIRType& lastType = _comp.get_type(type.member_types[memberCnt-1u]);
    if (lastType.array.size() && lastType.array_size_literal[0] && lastType.array[0] == 0u)
        _res.rtSizedArrayOneElementSize += _comp.type_struct_member_array_stride(type, type.member_types[memberCnt-1u]);

    spirv_cross::Bitset flags = _comp.get_buffer_block_flags(_varID);
    _res.restrict = flags.get(spv::DecorationRestrict);
    _res.volatile_ = flags.get(spv::DecorationVolatile);
    _res.coherent = flags.get(spv::DecorationCoherent);
    _res.readonly = flags.get(spv::DecorationNonWritable);
    _res.writeonly = flags.get(spv::DecorationNonReadable);
}

size_t ICPUShader::SIntrospectionPerformer::calcBytesizeforType(spirv_cross::Compiler& _comp, const spirv_cross::SPIRType & _type) const
{
    size_t bytesize = 0u;
    switch (_type.basetype)
    {
    case spirv_cross::SPIRType::Boolean:
    case spirv_cross::SPIRType::Char:
    case spirv_cross::SPIRType::SByte:
    case spirv_cross::SPIRType::UByte:
        bytesize = 1u;
        break;
    case spirv_cross::SPIRType::Short:
    case spirv_cross::SPIRType::UShort:
    case spirv_cross::SPIRType::Half:
        bytesize = 2u;
        break;
    case spirv_cross::SPIRType::Int:
    case spirv_cross::SPIRType::UInt:
    case spirv_cross::SPIRType::Float:
        bytesize = 4u;
        break;
    case spirv_cross::SPIRType::Int64:
    case spirv_cross::SPIRType::UInt64:
    case spirv_cross::SPIRType::Double:
        bytesize = 8u;
        break;
    case spirv_cross::SPIRType::Struct:
        bytesize = _comp.get_declared_struct_size(_type);
        assert(_type.columns > 1u || _type.vecsize > 1u); // something went wrong (cannot have matrix/vector of struct type)
        break;
    default:
        assert(0);
        break;
    }
    bytesize *= _type.vecsize * _type.columns; //vector or matrix
    if (_type.array.size()) //array
        bytesize *= _type.array[0];

    return bytesize;
}

}}